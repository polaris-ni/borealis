/// 测试类型: unit
/// 目标单元: src/conn/sftp_model.h 的批次计划折叠（plan_batch_transfer）
/// 测试说明: 守住递归目录传输的纯逻辑矩阵——目录先于文件的确定序、相对路径
///           拼接（含 rel_dir 尾斜杠归一）、字节求和只认已知大小档、守卫线
///           两档越线整批拒绝且**不携带残树**、空树可执行。枚举 IO 不在
///           本层（worker 侧 list_directory / filesystem，沙箱无 sshd）。

#include "conn/sftp_model.h"

#include <string>
#include <vector>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_sftp_batch_plan {

namespace {

using conn::BatchDirNode;
using conn::BatchFileNode;
using conn::BatchLimits;
using conn::BatchReject;
using conn::plan_batch_transfer;

[[nodiscard]] auto file(std::string name, std::uint64_t bytes, bool known = true)
    -> BatchFileNode {
    return BatchFileNode{.name = std::move(name), .size_bytes = bytes, .size_known = known};
}

[[nodiscard]] auto dir(std::string name, std::vector<BatchFileNode> files,
                       std::vector<BatchDirNode> subs = {}) -> BatchDirNode {
    return BatchDirNode{.name = std::move(name), .files = std::move(files),
                        .dirs = std::move(subs)};
}

}  // namespace

AURORA_TEST_CASE(flat_tree_lists_directories_before_files) {
    const auto root = dir("", {file("b.txt", 10), file("a.txt", 20)},
                          {dir("logs", {})});
    const auto plan = plan_batch_transfer(root, "", {});

    AURORA_TEST_CHECK_TRUE(plan.engaged);
    AURORA_TEST_CHECK_EQ(plan.dirs.size(), std::size_t{1});
    AURORA_TEST_CHECK_MSG(plan.dirs[0] == "logs/", "directory mirror path carries slash");
    AURORA_TEST_CHECK_EQ(plan.files.size(), std::size_t{2});
    AURORA_TEST_CHECK_MSG(plan.files[0] == "b.txt" && plan.files[1] == "a.txt",
                          "in-level order preserved, not re-sorted");
    AURORA_TEST_CHECK_EQ(plan.total_bytes, std::uint64_t{30});
    AURORA_TEST_CHECK_TRUE(plan.bytes_complete);
}

AURORA_TEST_CASE(nested_tree_walks_subdirs_before_own_files) {
    const auto root = dir("", {file("top.bin", 1)},
                          {dir("sub", {file("mid.txt", 5)}, {dir("deep", {file("low.txt", 4)})})});
    const auto plan = plan_batch_transfer(root, "", {});

    AURORA_TEST_CHECK_TRUE(plan.engaged);
    AURORA_TEST_CHECK_EQ(plan.dirs.size(), std::size_t{2});
    AURORA_TEST_CHECK_MSG(plan.dirs[0] == "sub/" && plan.dirs[1] == "sub/deep/",
                          "dirs descend depth-first");
    AURORA_TEST_CHECK_EQ(plan.files.size(), std::size_t{3});
    AURORA_TEST_CHECK_MSG(plan.files[0] == "sub/deep/low.txt" && plan.files[1] == "sub/mid.txt" &&
                              plan.files[2] == "top.bin",
                          "deepest subtree first, then the level's own files");
    AURORA_TEST_CHECK_EQ(plan.total_bytes, std::uint64_t{10});
}

AURORA_TEST_CASE(rel_dir_prefix_joins_without_double_slash) {
    const auto root = dir("", {file("x", 3)});
    const auto without_slash = plan_batch_transfer(root, "data", {});
    const auto with_slash = plan_batch_transfer(root, "data/", {});

    AURORA_TEST_CHECK_TRUE(without_slash.engaged && with_slash.engaged);
    AURORA_TEST_CHECK_MSG(without_slash.files[0] == "data/x", "bare prefix gets one slash");
    AURORA_TEST_CHECK_MSG(with_slash.files[0] == "data/x", "trailing slash not doubled");
}

AURORA_TEST_CASE(unknown_sizes_exlude_from_total_and_flag_incomplete) {
    const auto root = dir("", {file("known", 7, true), file("mystery", 99, false)});
    const auto plan = plan_batch_transfer(root, "", {});

    AURORA_TEST_CHECK_TRUE(plan.engaged);
    AURORA_TEST_CHECK_EQ(plan.files.size(), std::size_t{2});
    // 未知大小的件一股都不进总账——否则总进度线的分母掺了不可信读数。
    AURORA_TEST_CHECK_EQ(plan.total_bytes, std::uint64_t{7});
    AURORA_TEST_CHECK_TRUE(!plan.bytes_complete);
}

AURORA_TEST_CASE(empty_tree_is_an_executable_no_op_plan) {
    const auto plan = plan_batch_transfer(dir("", {}), "", {});

    AURORA_TEST_CHECK_TRUE(plan.engaged);
    AURORA_TEST_CHECK_EQ(plan.files.size(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(plan.dirs.size(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(plan.total_bytes, std::uint64_t{0});
    AURORA_TEST_CHECK_TRUE(plan.bytes_complete);
}

AURORA_TEST_CASE(file_count_limit_rejects_the_whole_batch_without_residue) {
    const auto root = dir("", {file("a", 1), file("b", 2), file("c", 3)});
    const auto limits = BatchLimits{/*max_depth=*/64, /*max_files=*/2};
    const auto plan = plan_batch_transfer(root, "", limits);

    AURORA_TEST_CHECK_TRUE(!plan.engaged);
    AURORA_TEST_CHECK_TRUE(plan.reject == BatchReject::OverCount);
    AURORA_TEST_CHECK_MSG(plan.files.empty() && plan.dirs.empty() && plan.total_bytes == 0,
                          "rejected plans carry no partial tree (fail-closed)");
}

AURORA_TEST_CASE(depth_limit_measures_directory_levels) {
    const auto root = dir("", {}, {dir("one", {}, {dir("two", {file("deep", 1)})})});

    const auto one_level = plan_batch_transfer(root, "", BatchLimits{/*max_depth=*/1, 100});
    AURORA_TEST_CHECK_TRUE(!one_level.engaged);
    AURORA_TEST_CHECK_TRUE(one_level.reject == BatchReject::OverDepth);

    const auto two_levels = plan_batch_transfer(root, "", BatchLimits{/*max_depth=*/2, 100});
    AURORA_TEST_CHECK_TRUE(two_levels.engaged);
    AURORA_TEST_CHECK_EQ(two_levels.files.size(), std::size_t{1});
    AURORA_TEST_CHECK_MSG(two_levels.files[0] == "one/two/deep", "full relative path kept");
}

AURORA_TEST_CASE(files_directly_at_the_depth_line_still_pass) {
    // max_depth==1 的树：根下一层目录里的文件不越线，再往下的目录才越。
    const auto root = dir("", {}, {dir("one", {file("f", 2)})});
    const auto plan = plan_batch_transfer(root, "", BatchLimits{/*max_depth=*/1, 100});

    AURORA_TEST_CHECK_TRUE(plan.engaged);
    AURORA_TEST_CHECK_MSG(plan.files[0] == "one/f", "file level is not a directory level");
}

}  // namespace borealis::test_cases::utest_sftp_batch_plan
