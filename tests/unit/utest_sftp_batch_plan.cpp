/// 测试类型: unit
/// 目标单元: src/conn/sftp_model.h 的批次计划折叠（plan_batch_transfer）
/// 测试说明: 守住递归目录传输的纯逻辑矩阵——目录先于文件的确定序、相对路径
///           拼接（含 rel_dir 尾斜杠归一）、字节求和只认已知大小档、守卫线
///           两档越线整批拒绝且**不携带残树**、空树可执行、枚举失败短路档、
///           批次总账进度折叠。枚举 IO 不在本层（worker 侧 list_directory /
///           filesystem，沙箱无 sshd）。

#include "conn/sftp_model.h"

#include <optional>
#include <string>
#include <vector>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_sftp_batch_plan {

namespace {

using conn::BatchDirNode;
using conn::BatchFileNode;
using conn::BatchLimits;
using conn::BatchReject;
using conn::SftpProgress;
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

AURORA_TEST_CASE(list_failure_short_circuits_before_shape_verdicts) {
    // 枚举断过一层＝树不完整：即便残树形状同时越深度线，拒绝档也必须是 ListFailed——
    // 文案要说得出真正那一条（越线树说「太大」、断链树说「太大」会把人引去压缩而非重试）。
    const auto root = dir("", {}, {dir("one", {}, {dir("two", {file("deep", 1)})})});

    const auto broken = plan_batch_transfer(root, "", BatchLimits{/*max_depth=*/1, 100},
                                            /*list_failed=*/true);
    AURORA_TEST_CHECK_TRUE(!broken.engaged);
    AURORA_TEST_CHECK_TRUE(broken.reject == BatchReject::ListFailed);
    AURORA_TEST_CHECK_MSG(broken.files.empty() && broken.dirs.empty(),
                          "list failure rejects without residue too");

    // 同一棵形状合法的树，缺省 list_failed==false 时照常可执行。
    const auto sound = plan_batch_transfer(root, "", BatchLimits{/*max_depth=*/2, 100});
    AURORA_TEST_CHECK_TRUE(sound.engaged);
}

AURORA_TEST_CASE(fold_batch_progress_accumulates_bytes_over_completed_files) {
    using conn::fold_batch_progress;

    // 第三件传到一半：总账＝前两件整件 + 当前件已完成。
    const auto mid = fold_batch_progress(/*bytes_before=*/300,
                                         SftpProgress{.bytes_done = 40, .total_bytes = 100,
                                                      .total_known = true},
                                         /*batch_total=*/1000, /*complete=*/true);
    AURORA_TEST_CHECK_TRUE(mid.total_known);
    AURORA_TEST_CHECK_EQ(mid.total_bytes, std::uint64_t{1000});
    AURORA_TEST_CHECK_EQ(mid.bytes_done, std::uint64_t{340});
    AURORA_TEST_CHECK_TRUE(mid.progress_percent() == std::optional<unsigned>{34});
}

AURORA_TEST_CASE(fold_batch_progress_indeterminate_is_decided_by_the_plan_alone) {
    using conn::fold_batch_progress;

    // 计划里有未知大小的文件＝整批转不确定态，与当前件自身的 fstat 读数无关。
    const auto unknown_plan =
        fold_batch_progress(0, SftpProgress{.bytes_done = 5, .total_bytes = 9, .total_known = true},
                            0, /*complete=*/false);
    AURORA_TEST_CHECK_TRUE(!unknown_plan.total_known);
    AURORA_TEST_CHECK_TRUE(unknown_plan.progress_percent() == std::nullopt);

    // 反过来：计划算得清、当前件没回 size（total_known==false）——批次总量早已定死，
    // 不被单件读数推翻，进度照旧可判。
    const auto sound_plan =
        fold_batch_progress(/*bytes_before=*/100,
                            SftpProgress{.bytes_done = 30, .total_bytes = 0, .total_known = false},
                            /*batch_total=*/200, /*complete=*/true);
    AURORA_TEST_CHECK_TRUE(sound_plan.total_known);
    AURORA_TEST_CHECK_EQ(sound_plan.bytes_done, std::uint64_t{130});
    AURORA_TEST_CHECK_TRUE(
        sound_plan.progress_percent() == std::optional<unsigned>{65});
}

}  // namespace borealis::test_cases::utest_sftp_batch_plan
