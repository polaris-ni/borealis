#pragma once

// ============================================================
// SFTP 传输腿（src/conn/sftp_client.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.04（SFTP 文件浏览）的 libssh 传输层：自开一条 SSH 会话
// （建连/认证/主机密钥核对走共用腿 conn/ssh_dial，CONN.02 同款），在其上
// 挂 SFTP 子系统，提供列目录、建删改名与上传/下载（进度 + 取消）。
//
// 阻塞语义：所有操作都可能阻塞在网络往返上，调用方必须放到工作线程执行，
// 结果按 SPEC.NF.PERF.06 经有界队列交回 UI（本层不碰线程与 UI）。
//
// 本头是**私有头**（裁决 D1① 同款纪律）：libssh 类型只以前向声明的指针
// 形态出现，实现文件才包含 <libssh/libssh.h> <libssh/sftp.h>。纯逻辑部分
// （条目/路径/权限/排序/进度/错误枚举）在 conn/sftp_model.h，本层只做
// libssh 取值翻译与 IO。
// ============================================================

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/conn/profile.h"
#include "conn/sftp_model.h"

// libssh 前向声明：头里只出现指针，实现文件才包含 libssh 头。
struct ssh_session_struct;
struct sftp_session_struct;

namespace borealis::conn {

/// @brief SFTP 客户端：一条独立 SSH 会话上的 SFTP 传输腿。
///
/// 生命周期：connect() → 列目录/增删改/传输 → disconnect()/析构。全部操作
/// 阻塞且不抛出；每个操作开始时把 last_error() 清为 None，失败时置因并回
/// false / nullopt，细节走 AURORA_LOG_*（只含 libssh 错误文本与路径，绝不
/// 回显秘密材料）。凭据纪律（CONN.09）：secret 只在 connect() 调用栈内存
/// 活，认证完即被 dial 腿消费清空，本类不保存任何秘密材料。
class SftpClient {
public:
    SftpClient() = default;
    ~SftpClient();

    SftpClient(const SftpClient &) = delete;
    auto operator=(const SftpClient &) -> SftpClient & = delete;

    /// @brief 建连 → 主机密钥核对 → 认证 → 开 SFTP 子系统（阻塞，勿在 UI 线程调）。
    ///
    /// @param secret 已解析的秘密材料，口径同 ssh_dial_and_authenticate：
    ///               password＝口令、privatekey＝passphrase、其余方式按 dial
    ///               腿口径；本参数无论成败返回后即被清空。
    /// @return 成功回 true；已连接时幂等回 true（一个客户端一条会话，不重拨）。
    ///         失败看 last_error()（建连/认证失败一律 Network，dial 腿已记日志）。
    auto connect(const SshProfile &profile, std::optional<std::string> secret) -> bool;

    /// @brief 断开并释放 SFTP 与 SSH 会话（幂等，可随时调用）。
    auto disconnect() -> void;

    /// @brief 是否已有可用的 SFTP 会话（原子读，UI 线程可随时问）。
    [[nodiscard]] auto connected() const noexcept -> bool;

    /// @brief 列目录（阻塞）。"." 与 ".." 已跳过；结果保持服务器顺序、未排序，
    ///        呈现前由调用方用 sort_entries() 排。失败回 nullopt，原因看 last_error()。
    [[nodiscard]] auto list_directory(std::string_view path) -> std::optional<std::vector<SftpEntry>>;

    /// @brief 建目录（远端权限 0755，不递归）。
    auto make_directory(std::string_view path) -> bool;

    /// @brief 删文件（服务器端 unlink）。
    auto remove_file(std::string_view path) -> bool;

    /// @brief 删空目录（服务器端 rmdir，不递归——递归删除是 UI 层的组合决策）。
    auto remove_directory(std::string_view path) -> bool;

    /// @brief 改名/移动（服务器语义；能否覆盖已存在目标由服务器决定）。
    auto rename(std::string_view from, std::string_view to) -> bool;

    /// @brief 下载远端文件到本地（本地已存在则截断重写）。
    ///
    /// 总量取远端 fstat 的 size（服务器给了才算 total_known）。@p progress
    /// 每收一块回调一次（同一工作线程上同步调用，UI 侧自行节流投递）。
    /// @p cancel 置 true 即取消：关远端句柄、删本地半成品、
    /// last_error()=Cancelled、回 false。
    auto download_file(std::string_view remote_path, const std::filesystem::path &local_path,
                       const std::function<void(const SftpProgress &)> &progress,
                       const std::atomic<bool> &cancel) -> bool;

    /// @brief 上传本地文件到远端（远端已存在则截断重写）。
    ///
    /// 总量取本地 file_size（可知即 total_known）。进度与取消语义同
    /// download_file()；取消时删远端半成品。
    auto upload_file(const std::filesystem::path &local_path, std::string_view remote_path,
                     const std::function<void(const SftpProgress &)> &progress,
                     const std::atomic<bool> &cancel) -> bool;

    /// @brief 最近一次操作失败的原因（每个操作开始时清为 None）。
    [[nodiscard]] auto last_error() const noexcept -> SftpError;

private:
    /// @brief 操作前置检查：未连接置 NotConnected 并回 false。
    auto ensure_connected() -> bool;

    /// @brief 失败收尾：把 sftp_get_error 映射进 last_error_ 并记日志，恒回 false。
    auto fail(const char *what, const std::string &path) -> bool;

    ssh_session_struct *session_ = nullptr;  ///< libssh 会话（connect 分配，disconnect 释放）。
    sftp_session_struct *sftp_ = nullptr;    ///< SFTP 子系统句柄（认证成功后分配）。
    std::atomic<bool> connected_{false};     ///< 原子在线标志：UI 线程可无锁问询。
    SftpError last_error_{SftpError::None};
};

}  // namespace borealis::conn
