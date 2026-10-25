// ============================================================
// SFTP 浏览器纯逻辑模型实现（src/conn/sftp_model.cpp）
// ------------------------------------------------------------
// 只做字符串与位运算：libssh 与文件系统完全不出现（头注释口径）。
// 路径语义与会话侧操作系统无关——SFTP 协议里路径就是 '/' 分隔的字符串。
// ============================================================

#include "conn/sftp_model.h"

#include <algorithm>
#include <string_view>
#include <vector>

namespace borealis::conn {

namespace {

constexpr std::uint32_t kSetUidBit = 04000;
constexpr std::uint32_t kSetGidBit = 02000;
constexpr std::uint32_t kStickyBit = 01000;

/// @brief 名字比较口径：ASCII 大小写折叠（不做 locale 折叠，见头注释）。
[[nodiscard]] auto ascii_lower(std::string_view text) -> std::string {
    auto out = std::string{text};
    for (auto &ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

/// @brief 折叠尾部 '/'（根 "/" 除外），供 parent / basename 共用。
[[nodiscard]] auto trim_trailing_slash(std::string_view path) -> std::string_view {
    while (path.size() > 1 && path.back() == '/') {
        path.remove_suffix(1);
    }
    return path;
}

}  // namespace

auto format_permissions(std::uint32_t mode, SftpEntryKind kind) -> std::string {
    auto out = std::string{};
    out.reserve(10);
    switch (kind) {
        case SftpEntryKind::Directory:
            out.push_back('d');
            break;
        case SftpEntryKind::Symlink:
            out.push_back('l');
            break;
        case SftpEntryKind::File:
        case SftpEntryKind::Other:
            out.push_back('-');
            break;
    }
    // rwx 三组；特殊位（setuid/setgid/sticky）按 ls 口径落在 x 位上。
    for (const auto shift : {6u, 3u, 0u}) {
        out.push_back(((mode >> shift) & 4u) != 0 ? 'r' : '-');
        out.push_back(((mode >> shift) & 2u) != 0 ? 'w' : '-');
        const auto exec = ((mode >> shift) & 1u) != 0;
        auto special = '\0';
        if (shift == 6 && (mode & kSetUidBit) != 0) {
            special = exec ? 's' : 'S';
        } else if (shift == 3 && (mode & kSetGidBit) != 0) {
            special = exec ? 's' : 'S';
        } else if (shift == 0 && (mode & kStickyBit) != 0) {
            special = exec ? 't' : 'T';
        }
        out.push_back(special != '\0' ? special : (exec ? 'x' : '-'));
    }
    return out;
}

auto sftp_path_join(std::string_view dir, std::string_view name) -> std::string {
    if (dir.empty()) {
        return std::string{name};  // 约定：空 dir＝name 是完整路径。
    }
    if (name.empty()) {
        return std::string{dir};
    }
    const auto trimmed = trim_trailing_slash(dir);
    if (trimmed == "/") {
        return std::string{"/"}.append(name);
    }
    auto out = std::string{trimmed};
    out.push_back('/');
    out.append(name);
    return out;
}

auto sftp_parent_of(std::string_view path) -> std::string {
    const auto trimmed = trim_trailing_slash(path);
    if (trimmed.empty() || trimmed == "/") {
        return "/";  // 空输入视作根；根的父仍是根（「向上一级」到顶）。
    }
    const auto pos = trimmed.rfind('/');
    if (pos == std::string_view::npos || pos == 0) {
        return "/";  // 相对单段名按「根下的名字」处理（头注释口径）。
    }
    return std::string{trimmed.substr(0, pos)};
}

auto sftp_basename_of(std::string_view path) -> std::string {
    const auto trimmed = trim_trailing_slash(path);
    if (trimmed.empty()) {
        return {};
    }
    if (trimmed == "/") {
        return "/";  // 根的名字就是 "/"（UI 显示当前目录名用）。
    }
    const auto pos = trimmed.rfind('/');
    if (pos == std::string_view::npos) {
        return std::string{trimmed};
    }
    return std::string{trimmed.substr(pos + 1)};
}

auto sftp_normalize(std::string_view path) -> std::string {
    const auto absolute = !path.empty() && path.front() == '/';
    auto parts = std::vector<std::string_view>{};
    auto begin = std::size_t{0};
    for (;;) {
        const auto end = path.find('/', begin);
        const auto part = path.substr(begin, end == std::string_view::npos
                                                ? std::string_view::npos
                                                : end - begin);
        if (!part.empty() && part != ".") {
            if (part == "..") {
                if (!parts.empty() && parts.back() != "..") {
                    parts.pop_back();
                } else if (!absolute) {
                    parts.push_back(part);  // 相对路径开头的越级 ".." 原样保留。
                }
                // 绝对路径越根：钳回根，忽略该段。
            } else {
                parts.push_back(part);
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }

    if (absolute) {
        if (parts.empty()) {
            return "/";
        }
        auto out = std::string{};
        for (const auto &part : parts) {
            out.push_back('/');
            out.append(part);
        }
        return out;
    }
    if (parts.empty()) {
        // 空输入原样回空串；规整后归零的相对路径按 realpath 口径回 "."。
        return path.empty() ? std::string{} : std::string{"."};
    }
    auto out = std::string{};
    for (const auto &part : parts) {
        if (!out.empty()) {
            out.push_back('/');
        }
        out.append(part);
    }
    return out;
}

auto sort_entries(std::vector<SftpEntry> entries) -> std::vector<SftpEntry> {
    std::stable_sort(entries.begin(), entries.end(),
                     [](const SftpEntry &a, const SftpEntry &b) {
                         const auto a_dir = a.kind == SftpEntryKind::Directory;
                         const auto b_dir = b.kind == SftpEntryKind::Directory;
                         if (a_dir != b_dir) {
                             return a_dir;  // 目录置顶，其余类型不细分。
                         }
                         const auto lower_a = ascii_lower(a.name);
                         const auto lower_b = ascii_lower(b.name);
                         if (lower_a != lower_b) {
                             return lower_a < lower_b;
                         }
                         return a.name < b.name;  // 折叠相等：字节序定确定序。
                     });
    return entries;
}

auto SftpProgress::progress_percent() const -> std::optional<unsigned> {
    if (!total_known) {
        return std::nullopt;
    }
    if (total_bytes == 0) {
        return 100u;  // 总量已知为 0＝没有字节要传，即完成。
    }
    const auto percent = static_cast<unsigned>(bytes_done * 100 / total_bytes);
    return std::min(percent, 100u);
}

namespace {

/// @brief 折叠的累加态：计划本体 + 守卫线（拒绝档不携带残树，回抛即弃）。
struct BatchFoldState {
    BatchPlan plan{};
    BatchLimits limits{};
};

/// @brief 相对前缀的尾斜杠归一："" 与 "sub" 都以可拼接形态参与拼名。
[[nodiscard]] auto with_trailing_slash(std::string_view text) -> std::string {
    auto out = std::string{text};
    if (!out.empty() && out.back() != '/') {
        out += '/';
    }
    return out;
}

/// @brief 递归折叠一个目录节点（node 自身的路径已由调用方记进 dirs）。
///
/// 次序口径与目录列表同形：先子目录整棵，再本层文件——worker 侧的表已由
/// sort_entries 排过（目录置顶），这里不重排，只保证结果确定。
/// @param prefix 本节点相对路径（恒以 '/' 结尾）。
/// @param depth prefix 的段数（根第一层节点＝1）。
auto fold_batch_node(const BatchDirNode &node, const std::string &prefix, unsigned depth,
                     BatchFoldState &state) -> bool {
    for (const auto &dir : node.dirs) {
        const std::string child_prefix = prefix + dir.name + '/';
        // 深度线量「子目录自身的层」：根的 dirs 记为 1 层——limits.max_depth==0
        // 意味着一棵带任何子目录的树都被拒，而纯文件树仍可过。
        if (depth > state.limits.max_depth) {
            state.plan.reject = BatchReject::OverDepth;
            return false;
        }
        state.plan.dirs.push_back(child_prefix);
        if (!fold_batch_node(dir, child_prefix, depth + 1, state)) {
            return false;
        }
    }
    for (const auto &file : node.files) {
        if (state.plan.files.size() >= state.limits.max_files) {
            state.plan.reject = BatchReject::OverCount;
            return false;
        }
        state.plan.files.push_back(prefix + file.name);
        if (file.size_known) {
            state.plan.total_bytes += file.size_bytes;
        } else {
            state.plan.bytes_complete = false;
        }
    }
    return true;
}

}  // namespace

auto plan_batch_transfer(const BatchDirNode &root, std::string_view rel_dir,
                         const BatchLimits &limits, bool list_failed) -> BatchPlan {
    // 枚举失败先于形状判定短路：残树上的深度/数量读数都不可信，越线档会把「枚举断了」
    // 误报成「树太大」，拒绝文案因此必须说得出真正的那一条。
    if (list_failed) {
        auto rejected = BatchPlan{};
        rejected.reject = BatchReject::ListFailed;
        return rejected;
    }
    BatchFoldState state{.plan = {}, .limits = limits};
    state.plan.bytes_complete = true;
    const std::string prefix = with_trailing_slash(rel_dir);
    if (!fold_batch_node(root, prefix, 1, state)) {
        // 拒绝档只带原因，不带残树（fail-closed：调用方不得拿半份清单起跑）。
        auto rejected = BatchPlan{};
        rejected.reject = state.plan.reject;
        return rejected;
    }
    state.plan.engaged = true;
    state.plan.reject = BatchReject::None;
    return state.plan;
}

auto fold_batch_progress(std::uint64_t bytes_before, const SftpProgress &file_progress,
                         std::uint64_t batch_total_bytes, bool batch_bytes_complete)
    -> SftpProgress {
    // 不确定态只由「计划里有没有未知大小的文件」决定：腿上的 total_known 是服务器
    // 对这一个文件的 fstat 读数，批次总量早已在预枚举时定死，不该被单件读数反向推翻
    // ——否则一个不回 size 的目录会让本来算得清的整批反而一直转不确定态。
    return SftpProgress{
        .bytes_done = bytes_before + file_progress.bytes_done,
        .total_bytes = batch_total_bytes,
        .total_known = batch_bytes_complete,
    };
}

}  // namespace borealis::conn
