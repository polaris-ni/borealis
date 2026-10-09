#pragma once

// ============================================================
// SFTP 浏览器纯逻辑模型（src/conn/sftp_model.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.04（SFTP 文件浏览）的纯逻辑层：条目模型、路径四函数、
// 权限渲染、目录排序、进度百分数——全部只依赖标准类型，不碰 libssh、
// 不碰文件系统、不碰 UI（AGENTS.md §4.4 第 20 条，utest_sftp_model 无头单测）。
//
// libssh 取值的翻译（sftp_attributes → SftpEntry）落在传输腿 sftp_client；
// UI 只消费这里算好的行。SftpClient（第二消费方）见 conn/sftp_client.h。
//
// 路径口径：SFTP 路径以 '/' 分隔、'/' 为根，与会话侧操作系统无关
// （Windows OpenSSH 服务器同样报 '/' 路径）；名字比较一律字节序 +
// ASCII 大小写折叠，不做 locale / Unicode 折叠（不引入平台差异）。
// ============================================================

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace borealis::conn {

/// @brief 目录条目类型（libssh sftp attributes type 的翻译在 sftp_client）。
enum class SftpEntryKind : std::uint8_t {
    File,       ///< 普通文件。
    Directory,  ///< 目录（排序时置顶的一类）。
    Symlink,    ///< 符号链接（不跟随；跟随后的真实类型以服务器为准）。
    Other,      ///< 设备 / 套接字 / FIFO 等，UI 按不可操作项呈现。
};

/// @brief 一条 SFTP 目录条目（列目录结果的一行，UI 直接消费）。
struct SftpEntry {
    std::string name;              ///< 仅名字，不含路径；"." 与 ".." 由 client 跳过。
    SftpEntryKind kind{SftpEntryKind::File};
    std::uint64_t size_bytes{0};   ///< kind==File 才有意义；目录取值不保证有意义。
    std::uint32_t permissions{0};  ///< st_mode 全量（含类型位与特殊位），渲染用。
    std::int64_t mtime{0};         ///< 秒级 Unix 时间（UI 本地化显示，本层不格式化）。

    [[nodiscard]] auto operator==(const SftpEntry &) const noexcept -> bool = default;
};

/// @brief 权限位渲染成 ls 风格 10 字符串（如 "-rw-r--r--"）。
///
/// 首字符按 kind：File '-'、Directory 'd'、Symlink 'l'、Other '-'；
/// 之后 rwx 三组。特殊位按 ls 口径落在 x 位上：setuid→s/S、setgid→s/S、
/// sticky→t/T（有 x 用小写、无 x 用大写）。
[[nodiscard]] auto format_permissions(std::uint32_t mode, SftpEntryKind kind) -> std::string;

/// @brief 目录与名字拼成一条 SFTP 路径。
///
/// dir 尾部多余的 '/' 自动折叠；dir 为 "/" 时不再产生 "//"；dir 为空＝
/// 把 name 当完整路径原样返回（调用方约定，不猜当前目录）。
[[nodiscard]] auto sftp_path_join(std::string_view dir, std::string_view name) -> std::string;

/// @brief 父目录："/a/b"→"/a"、"/a"→"/"；根的父仍是根（UI「向上一级」到顶）。
///        尾部 '/' 先折叠；不含 '/' 的输入按「根下的名字」处理，父为 "/"。
[[nodiscard]] auto sftp_parent_of(std::string_view path) -> std::string;

/// @brief 最后一段名字："/a/b"→"b"、"/"→"/"；尾部 '/' 先折叠；空输入回空串。
[[nodiscard]] auto sftp_basename_of(std::string_view path) -> std::string;

/// @brief 规整路径：折叠重复 '/'、消去 "." 与 ".."（绝对路径越根钳回 "/"）。
///
/// 绝对路径（'/' 开头）结果总以 '/' 开头且不以 '/' 结尾（根除外）；
/// 相对输入保留相对性，开头越级的 ".." 原样保留；空输入回空串。
[[nodiscard]] auto sftp_normalize(std::string_view path) -> std::string;

/// @brief 目录列表排序：目录置顶，同类按名字 ASCII 大小写不敏感字典序
///        （大小写折叠相等时按字节序，保证结果确定）。纯函数，返回新向量。
[[nodiscard]] auto sort_entries(std::vector<SftpEntry> entries) -> std::vector<SftpEntry>;

/// @brief 上传 / 下载进度快照（传输腿逐块回调投递，UI 节流渲染）。
struct SftpProgress {
    std::uint64_t bytes_done{0};
    std::uint64_t total_bytes{0};  ///< total_known==false 时无意义（读 0）。
    bool total_known{false};       ///< 上传＝本地文件大小可知；下载看服务器是否给 total。

    /// @brief 0–100 的整数百分比；总量未知回 nullopt（UI 转不确定态）。
    ///        总量已知为 0 回 100（没有字节要传＝完成）。
    [[nodiscard]] auto progress_percent() const -> std::optional<unsigned>;
};

/// @brief SFTP 操作错误分类（sftp_client 的结果载体；文案映射在 UI）。
enum class SftpError : std::uint8_t {
    None,             ///< 无错。
    NotConnected,     ///< 未 connect() 就发起操作。
    Network,          ///< 连接 / 会话级失败。
    PermissionDenied, ///< 服务器拒绝（权限 / 只读文件系统）。
    NotFound,         ///< 远端路径不存在。
    Cancelled,        ///< 调用方取消（半成品已清理）。
    LocalIo,          ///< 本地文件读写失败。
    Protocol,         ///< SFTP 协议层意外（含服务器不支持某操作）。
};

}  // namespace borealis::conn
