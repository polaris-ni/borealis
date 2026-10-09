// ============================================================
// SFTP 传输腿实现（src/conn/sftp_client.cpp）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.04 的 libssh SFTP 落地：建连/认证复用 conn/ssh_dial，
// 之后在 SFTP 子系统上做列目录、建删改名与上传/下载。
//
// 与 ssh_connection.cpp 同款纪律：
//   1. 全阻塞——调用方放工作线程，本层不建线程、不碰 UI。
//   2. 句柄释放顺序固定：先 sftp_free 再 ssh_disconnect/ssh_free
//      （SFTP 子系统必须活在会话上）。
//   3. 日志只打 libssh 错误文本与路径，绝不回显秘密材料（CONN.09）。
//
// 取消口径：cancel 标志在块间检查（阻塞中的单次网络写无法打断，粒度即
// 一块）；取消即关句柄、删半成品（下载删本地、上传删远端），不留垃圾。
// ============================================================

#include "sftp_client.h"

#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#include <libssh/libssh.h>
#include <libssh/sftp.h>

#include "aurora/core/log.h"
#include "conn/ssh_dial.h"

namespace borealis::conn {

namespace {

/// @brief 上传/下载的单块字节数：与 ssh_connection 读循环同量级（32KiB）。
constexpr std::size_t kTransferChunkBytes = 32768;

/// @brief 远端新建目录的权限（不递归，固定口径；UI 不暴露权限编辑）。
constexpr std::uint32_t kDirectoryMode = 0755;

/// @brief 远端新建文件的权限（0644：属主读写、其余只读）。
constexpr std::uint32_t kFileMode = 0644;

/// @brief libssh attributes type → 条目类型翻译（SPECIAL/UNKNOWN 归 Other）。
auto entry_kind_of(std::uint8_t type) -> SftpEntryKind {
    switch (type) {
    case SSH_FILEXFER_TYPE_REGULAR:
        return SftpEntryKind::File;
    case SSH_FILEXFER_TYPE_DIRECTORY:
        return SftpEntryKind::Directory;
    case SSH_FILEXFER_TYPE_SYMLINK:
        return SftpEntryKind::Symlink;
    default:
        return SftpEntryKind::Other;
    }
}

/// @brief sftp_get_error 状态码 → 本仓错误分类（其余一律按会话级失败计）。
auto fx_to_sftp_error(int fx) -> SftpError {
    switch (fx) {
    case SSH_FX_NO_SUCH_FILE:
        return SftpError::NotFound;
    case SSH_FX_PERMISSION_DENIED:
        return SftpError::PermissionDenied;
    case SSH_FX_OP_UNSUPPORTED:
        return SftpError::Protocol;
    default:
        return SftpError::Network;
    }
}

}  // namespace

SftpClient::~SftpClient() {
    disconnect();
}

auto SftpClient::connect(const SshProfile &profile, std::optional<std::string> secret) -> bool {
    // 幂等：一个客户端一条会话，重复 connect 不重拨（UI 侧一个面板一个实例）。
    if (connected_.load()) {
        return true;
    }
    last_error_ = SftpError::None;
    session_ = ssh_new();
    if (session_ == nullptr) {
        last_error_ = SftpError::Network;
        AURORA_LOG_ERROR("conn", "sftp: cannot allocate ssh session");
        return false;
    }
    // 秘密材料移交 dial 腿消费，无论成败返回后即清空；本类不留存（CONN.09）。
    if (ssh_dial_and_authenticate(profile, std::move(secret), session_) != DialOutcome::Ok) {
        ssh_free(session_);
        session_ = nullptr;
        last_error_ = SftpError::Network;
        return false;
    }
    sftp_ = sftp_new(session_);
    if (sftp_ == nullptr || sftp_init(sftp_) != 0) {
        AURORA_LOG_ERROR("conn", "sftp: subsystem init failed: ", ssh_get_error(session_));
        if (sftp_ != nullptr) {
            sftp_free(sftp_);
            sftp_ = nullptr;
        }
        ssh_disconnect(session_);
        ssh_free(session_);
        session_ = nullptr;
        last_error_ = SftpError::Protocol;
        return false;
    }
    connected_.store(true);
    return true;
}

auto SftpClient::disconnect() -> void {
    connected_.store(false);
    if (sftp_ != nullptr) {
        sftp_free(sftp_);
        sftp_ = nullptr;
    }
    if (session_ != nullptr) {
        ssh_disconnect(session_);
        ssh_free(session_);
        session_ = nullptr;
    }
}

auto SftpClient::connected() const noexcept -> bool {
    return connected_.load();
}

auto SftpClient::ensure_connected() -> bool {
    if (sftp_ == nullptr) {
        last_error_ = SftpError::NotConnected;
        return false;
    }
    return true;
}

auto SftpClient::fail(const char *what, const std::string &path) -> bool {
    last_error_ = fx_to_sftp_error(sftp_get_error(sftp_));
    AURORA_LOG_ERROR("conn", "sftp: ", what, " failed for ", path, ": ", ssh_get_error(session_));
    return false;
}

auto SftpClient::list_directory(std::string_view path) -> std::optional<std::vector<SftpEntry>> {
    last_error_ = SftpError::None;
    if (!ensure_connected()) {
        return std::nullopt;
    }
    const std::string dir{path};
    sftp_dir handle = sftp_opendir(sftp_, dir.c_str());
    if (handle == nullptr) {
        fail("opendir", dir);
        return std::nullopt;
    }
    auto entries = std::vector<SftpEntry>{};
    for (;;) {
        sftp_attributes attrs = sftp_readdir(sftp_, handle);
        if (attrs == nullptr) {
            // EOF＝目录读完（正常结束）；其余状态码才是失败。
            if (sftp_get_error(sftp_) != SSH_FX_EOF) {
                fail("readdir", dir);
                sftp_closedir(handle);
                return std::nullopt;
            }
            break;
        }
        const std::string_view name{attrs->name != nullptr ? attrs->name : ""};
        if (name != "." && name != "..") {
            auto &entry = entries.emplace_back();
            entry.name = std::string{name};
            entry.kind = entry_kind_of(attrs->type);
            entry.size_bytes = attrs->size;
            entry.permissions = attrs->permissions;
            entry.mtime = static_cast<std::int64_t>(attrs->mtime);
        }
        sftp_attributes_free(attrs);
    }
    sftp_closedir(handle);
    return entries;
}

auto SftpClient::make_directory(std::string_view path) -> bool {
    last_error_ = SftpError::None;
    if (!ensure_connected()) {
        return false;
    }
    const std::string dir{path};
    if (sftp_mkdir(sftp_, dir.c_str(), static_cast<mode_t>(kDirectoryMode)) < 0) {
        return fail("mkdir", dir);
    }
    return true;
}

auto SftpClient::remove_file(std::string_view path) -> bool {
    last_error_ = SftpError::None;
    if (!ensure_connected()) {
        return false;
    }
    const std::string file{path};
    if (sftp_unlink(sftp_, file.c_str()) < 0) {
        return fail("unlink", file);
    }
    return true;
}

auto SftpClient::remove_directory(std::string_view path) -> bool {
    last_error_ = SftpError::None;
    if (!ensure_connected()) {
        return false;
    }
    const std::string dir{path};
    if (sftp_rmdir(sftp_, dir.c_str()) < 0) {
        return fail("rmdir", dir);
    }
    return true;
}

auto SftpClient::rename(std::string_view from, std::string_view to) -> bool {
    last_error_ = SftpError::None;
    if (!ensure_connected()) {
        return false;
    }
    const std::string source{from};
    const std::string target{to};
    if (sftp_rename(sftp_, source.c_str(), target.c_str()) < 0) {
        return fail("rename", source);
    }
    return true;
}

auto SftpClient::download_file(std::string_view remote_path, const std::filesystem::path &local_path,
                               const std::function<void(const SftpProgress &)> &progress,
                               const std::atomic<bool> &cancel) -> bool {
    last_error_ = SftpError::None;
    if (!ensure_connected()) {
        return false;
    }
    const std::string remote{remote_path};
    sftp_file file = sftp_open(sftp_, remote.c_str(), SSH_FXF_READ, 0);
    if (file == nullptr) {
        return fail("open remote", remote);
    }
    // 总量取远端 fstat 的 size；服务器没给或 fstat 失败就按未知总量走。
    std::uint64_t total_bytes = 0;
    bool total_known = false;
    if (sftp_attributes attrs = sftp_fstat(file); attrs != nullptr) {
        total_bytes = attrs->size;
        total_known = true;
        sftp_attributes_free(attrs);
    }
    std::ofstream out(local_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        sftp_close(file);
        last_error_ = SftpError::LocalIo;
        AURORA_LOG_ERROR("conn", "sftp: cannot open local file for write: ", local_path.string());
        return false;
    }
    auto buffer = std::vector<char>(kTransferChunkBytes);
    std::uint64_t bytes_done = 0;
    for (;;) {
        if (cancel.load()) {
            out.close();
            sftp_close(file);
            std::error_code ec;
            std::filesystem::remove(local_path, ec);
            last_error_ = SftpError::Cancelled;
            AURORA_LOG_INFO("conn", "sftp: download cancelled, partial removed: ", remote);
            return false;
        }
        const auto bytes_read = sftp_read(file, buffer.data(), buffer.size());
        if (bytes_read < 0) {
            out.close();
            sftp_close(file);
            return fail("read", remote);
        }
        if (bytes_read == 0) {
            break;
        }
        out.write(buffer.data(), bytes_read);
        if (!out) {
            out.close();
            sftp_close(file);
            last_error_ = SftpError::LocalIo;
            AURORA_LOG_ERROR("conn", "sftp: local write failed: ", local_path.string());
            return false;
        }
        bytes_done += static_cast<std::uint64_t>(bytes_read);
        if (progress) {
            progress(SftpProgress{bytes_done, total_bytes, total_known});
        }
    }
    out.close();
    if (!out) {
        sftp_close(file);
        last_error_ = SftpError::LocalIo;
        AURORA_LOG_ERROR("conn", "sftp: local flush failed: ", local_path.string());
        return false;
    }
    if (sftp_close(file) < 0) {
        // 数据已落本地盘，远端 close 报错只记警——不把成功传输翻成失败。
        AURORA_LOG_WARN("conn", "sftp: remote close failed: ", ssh_get_error(session_));
    }
    return true;
}

auto SftpClient::upload_file(const std::filesystem::path &local_path, std::string_view remote_path,
                             const std::function<void(const SftpProgress &)> &progress,
                             const std::atomic<bool> &cancel) -> bool {
    last_error_ = SftpError::None;
    if (!ensure_connected()) {
        return false;
    }
    std::error_code ec;
    const auto total_bytes = static_cast<std::uint64_t>(std::filesystem::file_size(local_path, ec));
    const bool total_known = !ec;
    std::ifstream in(local_path, std::ios::binary);
    if (!in.is_open()) {
        last_error_ = SftpError::LocalIo;
        AURORA_LOG_ERROR("conn", "sftp: cannot open local file for read: ", local_path.string());
        return false;
    }
    const std::string remote{remote_path};
    sftp_file file = sftp_open(sftp_, remote.c_str(),
                               SSH_FXF_WRITE | SSH_FXF_CREAT | SSH_FXF_TRUNC,
                               static_cast<mode_t>(kFileMode));
    if (file == nullptr) {
        return fail("open remote", remote);
    }
    auto buffer = std::vector<char>(kTransferChunkBytes);
    std::uint64_t bytes_done = 0;
    for (;;) {
        if (cancel.load()) {
            in.close();
            sftp_close(file);
            sftp_unlink(sftp_, remote.c_str());
            last_error_ = SftpError::Cancelled;
            AURORA_LOG_INFO("conn", "sftp: upload cancelled, partial removed: ", remote);
            return false;
        }
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto chunk = static_cast<std::size_t>(in.gcount());
        if (chunk == 0) {
            if (in.eof()) {
                break;
            }
            in.close();
            sftp_close(file);
            last_error_ = SftpError::LocalIo;
            AURORA_LOG_ERROR("conn", "sftp: local read failed: ", local_path.string());
            return false;
        }
        // sftp_write 可能只吃下部分字节：把一块写满再收进度。
        const char *cursor = buffer.data();
        std::size_t remaining = chunk;
        while (remaining > 0) {
            const auto bytes_written = sftp_write(file, cursor, remaining);
            if (bytes_written <= 0) {
                in.close();
                sftp_close(file);
                return fail("write", remote);
            }
            cursor += bytes_written;
            remaining -= static_cast<std::size_t>(bytes_written);
            bytes_done += static_cast<std::uint64_t>(bytes_written);
        }
        if (progress) {
            progress(SftpProgress{bytes_done, total_bytes, total_known});
        }
    }
    if (sftp_close(file) < 0) {
        AURORA_LOG_WARN("conn", "sftp: remote close failed: ", ssh_get_error(session_));
    }
    in.close();
    return true;
}

auto SftpClient::last_error() const noexcept -> SftpError {
    return last_error_;
}

}  // namespace borealis::conn
