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

// ============================================================
// 递归目录传输的批次计划（SPEC.FEAT.CONN.04 延后子项，
// 稿 codespec/UI_SFTP_RECURSIVE.draft.md，裁决 7.151）。
// 枚举 IO 在 worker 侧（远端 list_directory / 本地 filesystem），本层只做
// 形状折叠：树 → 文件清单 + 目录镜像清单 + 守卫线判定，无 IO、无 libssh。
// ============================================================

/// @brief 预枚举守卫线的越线档（D4①）：整批 fail-closed 拒绝，不弹框。
enum class BatchReject : std::uint8_t {
    None,        ///< 未越线（engaged==false 的计划里才允许出现）。
    OverDepth,   ///< 某目录节点深度越线。
    OverCount,   ///< 文件数越线（跳过件不计）。
    ListFailed,  ///< 某层枚举失败（死会话 / 权限）——批次不带着残树起跑。
    OverBytes,   ///< 预留：字节上限档（本期 D4① 未启用，调用方不得回绕）。
};

/// @brief 批次守卫线（缺省即 D4① 的两条线）。
struct BatchLimits {
    unsigned max_depth{64};
    unsigned max_files{100'000};
};

/// @brief 树里的一条普通文件；size_known==false（本地档）时字节不计入总量，
///        批次只按「件」推进（传输条转不确定态，口径同既有下载无 total 档）。
struct BatchFileNode {
    std::string name;  ///< 仅名字；相对路径由父目录拼出。
    std::uint64_t size_bytes{0};
    bool size_known{true};
};

/// @brief 预枚举折叠出的一个目录节点（children 为该层全部文件，dirs 为子目录）。
///
/// 根节点自身的 name 不参与拼路径（根的目标位置由调用方给），留空即可。
struct BatchDirNode {
    std::string name;  ///< 目录名（相对路径的段；根节点不用此字段）。
    std::vector<BatchFileNode> files;
    std::vector<BatchDirNode> dirs;
};

/// @brief 跳过件的分类计数（D1①：软链与特殊文件一律跳过并计数）。
struct BatchSkips {
    unsigned symlinks{0};
    unsigned others{0};  ///< 设备 / 套接字 / FIFO。

    [[nodiscard]] auto total() const noexcept -> unsigned { return symlinks + others; }
};

/// @brief 一条可执行的批次计划（拒绝档只有 reject，不携带残树）。
struct BatchPlan {
    bool engaged{false};
    BatchReject reject{BatchReject::None};
    std::vector<std::string> files;        ///< 相对路径（'/' 分隔、不以 '/' 开头）。
    std::vector<std::string> dirs;         ///< 相对镜像目录（含空目录，不含根自身）。
    std::uint64_t total_bytes{0};          ///< 只累 size_known 的文件。
    bool bytes_complete{true};             ///< false＝计划里有未知大小的文件。
    BatchSkips skips{};

    [[nodiscard]] auto file_count() const noexcept -> std::size_t { return files.size(); }
};

/// @brief 折叠一棵目录树：深度/数量越线与枚举失败 → 拒绝；软链与 Other 只计数。
///
/// 确定序口径：目录先于文件进入清单，同级沿用入参次序（worker 侧表已由
/// sort_entries 排过，这里不重排）；相对路径 = 入参 rel_dir 拼名字。
/// @param root 根目录的第一层节点（根自身不进 dirs 清单）。
/// @param rel_dir 根目录相对前缀（"" 或形如 "sub/"，尾斜杠可有可无）。
/// @param limits 守卫线。
/// @param list_failed worker 枚举腿的失败位：某层列目录失败＝树不完整，折叠前先于
///                    一切形状判定短路成 ListFailed（fail-closed，不带着残树起跑）。
[[nodiscard]] auto plan_batch_transfer(const BatchDirNode &root, std::string_view rel_dir,
                                       const BatchLimits &limits = {},
                                       bool list_failed = false) -> BatchPlan;

/// @brief 把单件进度折进批次总账（人 2026-10-25 已拍口径③：整批按字节汇总的总进度）。
///
/// 批次态下 UI 只看这一份合成快照：`bytes_done` ＝批次累计（已完成的整件 + 当前件的
/// 已完成字节），`total_bytes` ＝预枚举总量。折出来的形态与单件快照同构，因此
/// `progress_percent()` 与既有传输条一字不用改。
/// @param bytes_before 当前件开始之前、批次内已完成文件的字节数。
/// @param file_progress 当前件的块级快照（传输腿原样给出）。
/// @param batch_total_bytes 计划里的 total_bytes（只累 size_known 的文件）。
/// @param batch_bytes_complete 计划无未知大小的文件；false＝整批转不确定态。
[[nodiscard]] auto fold_batch_progress(std::uint64_t bytes_before,
                                       const SftpProgress &file_progress,
                                       std::uint64_t batch_total_bytes,
                                       bool batch_bytes_complete) -> SftpProgress;

}  // namespace borealis::conn
