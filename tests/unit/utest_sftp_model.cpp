/// 测试类型: unit
/// 目标单元: src/conn/sftp_model.h（私有头）纯逻辑模型
/// 测试说明: 守住 SFTP 浏览器的纯逻辑矩阵——ls 风格权限渲染（含 setuid /
///           setgid / sticky 的 s/S/t/T 口径）、路径四函数（拼接/父/末段/
///           规整）、目录置顶与大小写折叠排序、进度百分数边界。SFTP 协议
///           I/O（libssh）不在无头单测范围（沙箱无 sshd）；传输腿见
///           conn/sftp_client（后续任务）。

#include "conn/sftp_model.h"

#include <optional>
#include <string>
#include <vector>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_sftp_model {

namespace {

using conn::SftpEntry;
using conn::SftpEntryKind;

/// @brief 便捷构造一条条目（只填测试关注的字段）。
[[nodiscard]] auto make_entry(std::string name, SftpEntryKind kind) -> SftpEntry {
    auto entry = SftpEntry{};
    entry.name = std::move(name);
    entry.kind = kind;
    return entry;
}

}  // namespace

AURORA_TEST_CASE(format_permissions_follows_ls_special_bit_conventions) {
    using conn::format_permissions;

    // 常规三类与 Other 的首字符 + 三组 rwx。
    AURORA_TEST_CHECK_MSG(format_permissions(0644, SftpEntryKind::File) == "-rw-r--r--",
                          "plain file");
    AURORA_TEST_CHECK_MSG(format_permissions(0755, SftpEntryKind::Directory) == "drwxr-xr-x",
                          "plain directory");
    AURORA_TEST_CHECK_MSG(format_permissions(0777, SftpEntryKind::Symlink) == "lrwxrwxrwx",
                          "symlink");
    AURORA_TEST_CHECK_MSG(format_permissions(0600, SftpEntryKind::Other) == "-rw-------",
                          "other kind renders as plain file");

    // 特殊位按 ls 口径落在 x 位：有 x 小写、无 x 大写。
    AURORA_TEST_CHECK_MSG(format_permissions(04755, SftpEntryKind::File) == "-rwsr-xr-x",
                          "setuid with exec");
    AURORA_TEST_CHECK_MSG(format_permissions(04644, SftpEntryKind::File) == "-rwSr--r--",
                          "setuid without exec");
    AURORA_TEST_CHECK_MSG(format_permissions(02755, SftpEntryKind::File) == "-rwxr-sr-x",
                          "setgid with exec");
    AURORA_TEST_CHECK_MSG(format_permissions(02644, SftpEntryKind::File) == "-rw-r-Sr--",
                          "setgid without exec");
    AURORA_TEST_CHECK_MSG(format_permissions(01777, SftpEntryKind::Directory) == "drwxrwxrwt",
                          "sticky with exec");
    AURORA_TEST_CHECK_MSG(format_permissions(01644, SftpEntryKind::Directory) == "drw-r--r-T",
                          "sticky without exec");
}

AURORA_TEST_CASE(sftp_path_join_folds_trailing_slash_and_handles_root) {
    using conn::sftp_path_join;

    AURORA_TEST_CHECK_MSG(sftp_path_join("/a/b", "c") == "/a/b/c", "plain join");
    AURORA_TEST_CHECK_MSG(sftp_path_join("/", "c") == "/c", "root does not double-slash");
    AURORA_TEST_CHECK_MSG(sftp_path_join("/a/", "c") == "/a/c", "trailing slash folded");
    AURORA_TEST_CHECK_MSG(sftp_path_join("", "c") == "c", "empty dir means name is full path");
    AURORA_TEST_CHECK_MSG(sftp_path_join("/a", "") == "/a", "empty name returns dir");
}

AURORA_TEST_CASE(sftp_parent_of_stops_at_root) {
    using conn::sftp_parent_of;

    AURORA_TEST_CHECK_MSG(sftp_parent_of("/a/b") == "/a", "nested path");
    AURORA_TEST_CHECK_MSG(sftp_parent_of("/a") == "/", "first level under root");
    AURORA_TEST_CHECK_MSG(sftp_parent_of("/") == "/", "root parent is root");
    AURORA_TEST_CHECK_MSG(sftp_parent_of("/a/b/") == "/a", "trailing slash folded");
    AURORA_TEST_CHECK_MSG(sftp_parent_of("a/b") == "a", "relative nested path");
    AURORA_TEST_CHECK_MSG(sftp_parent_of("a") == "/", "relative single segment maps to root");
    AURORA_TEST_CHECK_MSG(sftp_parent_of("") == "/", "empty input maps to root");
}

AURORA_TEST_CASE(sftp_basename_of_takes_last_segment) {
    using conn::sftp_basename_of;

    AURORA_TEST_CHECK_MSG(sftp_basename_of("/a/b") == "b", "nested path");
    AURORA_TEST_CHECK_MSG(sftp_basename_of("/a") == "a", "first level under root");
    AURORA_TEST_CHECK_MSG(sftp_basename_of("/") == "/", "root name is slash itself");
    AURORA_TEST_CHECK_MSG(sftp_basename_of("/a/") == "a", "trailing slash folded");
    AURORA_TEST_CHECK_MSG(sftp_basename_of("a") == "a", "relative name");
    AURORA_TEST_CHECK_MSG(sftp_basename_of("") == "", "empty input stays empty");
}

AURORA_TEST_CASE(sftp_normalize_collapses_dot_segments_and_clamps_at_root) {
    using conn::sftp_normalize;

    AURORA_TEST_CHECK_MSG(sftp_normalize("/a//b") == "/a/b", "double slash collapsed");
    AURORA_TEST_CHECK_MSG(sftp_normalize("/a/./b") == "/a/b", "dot segment removed");
    AURORA_TEST_CHECK_MSG(sftp_normalize("/a/b/../c") == "/a/c", "dotdot pops one level");
    AURORA_TEST_CHECK_MSG(sftp_normalize("/..") == "/", "absolute dotdot clamps at root");
    AURORA_TEST_CHECK_MSG(sftp_normalize("/../..") == "/", "repeated overshoot clamps");
    AURORA_TEST_CHECK_MSG(sftp_normalize("//") == "/", "root with extra slash");
    AURORA_TEST_CHECK_MSG(sftp_normalize("/a/b/") == "/a/b", "trailing slash dropped");
    AURORA_TEST_CHECK_MSG(sftp_normalize("a/../..") == "..", "relative overshoot kept");
    AURORA_TEST_CHECK_MSG(sftp_normalize("a/.") == "a", "relative dot removed");
    AURORA_TEST_CHECK_MSG(sftp_normalize("") == "", "empty input stays empty");
}

AURORA_TEST_CASE(sort_entries_puts_directories_first_then_case_folds) {
    using conn::sort_entries;

    auto entries = std::vector<SftpEntry>{
        make_entry("zeta", SftpEntryKind::File),
        make_entry("Beta", SftpEntryKind::File),
        make_entry("apple", SftpEntryKind::File),
        make_entry("mango", SftpEntryKind::Directory),
    };
    const auto sorted = sort_entries(std::move(entries));
    AURORA_TEST_CHECK_EQ(sorted.size(), std::size_t{4});
    AURORA_TEST_CHECK_MSG(sorted[0].name == "mango" &&
                              sorted[0].kind == SftpEntryKind::Directory,
                          "directory first");
    AURORA_TEST_CHECK_MSG(sorted[1].name == "apple", "case-folded order: apple");
    AURORA_TEST_CHECK_MSG(sorted[2].name == "Beta", "case-folded order: Beta");
    AURORA_TEST_CHECK_MSG(sorted[3].name == "zeta", "case-folded order: zeta");

    // 折叠相等时按字节序定确定序：'F'(0x46) < 'f'(0x66)。
    auto tied = std::vector<SftpEntry>{
        make_entry("file", SftpEntryKind::File),
        make_entry("File", SftpEntryKind::File),
    };
    const auto tie_sorted = sort_entries(std::move(tied));
    AURORA_TEST_CHECK_MSG(tie_sorted[0].name == "File" && tie_sorted[1].name == "file",
                          "byte order breaks fold ties");
}

AURORA_TEST_CASE(progress_percent_covers_unknown_zero_and_overflow) {
    using conn::SftpProgress;

    const auto unknown = SftpProgress{.bytes_done = 50, .total_bytes = 0, .total_known = false};
    AURORA_TEST_CHECK_TRUE(unknown.progress_percent() == std::nullopt);

    const auto empty = SftpProgress{.bytes_done = 0, .total_bytes = 0, .total_known = true};
    AURORA_TEST_CHECK_TRUE(empty.progress_percent() == std::optional<unsigned>{100});

    const auto quarter = SftpProgress{.bytes_done = 50, .total_bytes = 200, .total_known = true};
    AURORA_TEST_CHECK_TRUE(quarter.progress_percent() == std::optional<unsigned>{25});

    const auto full = SftpProgress{.bytes_done = 200, .total_bytes = 200, .total_known = true};
    AURORA_TEST_CHECK_TRUE(full.progress_percent() == std::optional<unsigned>{100});

    const auto over = SftpProgress{.bytes_done = 300, .total_bytes = 200, .total_known = true};
    AURORA_TEST_CHECK_TRUE(over.progress_percent() == std::optional<unsigned>{100});
}

AURORA_TEST_CASE(sftp_entry_equality_is_fieldwise) {
    auto a = make_entry("log", SftpEntryKind::File);
    a.size_bytes = 12;
    auto b = a;
    AURORA_TEST_CHECK_TRUE(a == b);
    b.kind = SftpEntryKind::Symlink;
    AURORA_TEST_CHECK_TRUE(!(a == b));
}

}  // namespace borealis::test_cases::utest_sftp_model
