/// 测试类型: unit
/// 目标单元: src/platform/file_secure.h + src/platform/posix/file_secure.cpp
/// 测试说明: SPEC.FEAT.CONN.10 私钥落盘的第一道闸（裁决 7.106 D5①）——「已存在即不碰」与
///           「权限钉死 0600」两件事各自独立可验：覆盖拒绝要能在既有文件已有内容时验出，
///           权限要在**故意让 umask 削掉属主写位**的现场验出（不注入这条 umask，删掉实现里的
///           fchmod 也照样全绿，因为默认 umask 恰好不动属主位）。Windows 腿同接口，
///           其 ACL 边界见 file_secure.cpp 头注释与稿 §7（本机无 MSVC，未经编译验证）。

#include "platform/file_secure.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_file_secure {

namespace {

using namespace borealis;

[[nodiscard]] auto make_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path{aurora::testing::isolation::temp_dir()} / std::string{name};
}

/// @brief 写入定长文本（覆盖既有内容），用于制造「这里已经有一份私钥」的现场。
auto write_text(const std::filesystem::path &path, std::string_view text) -> void {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
}

[[nodiscard]] auto read_text(const std::filesystem::path &path) -> std::string {
    std::ifstream in{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

AURORA_TEST_CASE(creates_an_empty_file_then_refuses_the_same_path) {
    const auto path = make_path("fresh.key");
    std::error_code ec;
    std::filesystem::remove(path, ec);

    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::create_private_file(path.string())),
                         static_cast<int>(platform::PrivateFileOutcome::Created));
    AURORA_TEST_CHECK_TRUE(std::filesystem::exists(path));
    // 「只建不写」：交回的是零长度文件，内容由调用方随后截断写入。
    AURORA_TEST_CHECK_EQ(static_cast<std::size_t>(std::filesystem::file_size(path)), 0U);

    // 同一路径第二次必须是 AlreadyExists——这正是 libssh 导出函数静默改写（稿 §0 的 F5）
    // 在本仓被拦下的那一闸。
    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::create_private_file(path.string())),
                         static_cast<int>(platform::PrivateFileOutcome::AlreadyExists));
    AURORA_TEST_CHECK_TRUE(std::filesystem::exists(path));
}

AURORA_TEST_CASE(a_second_call_never_touches_the_existing_bytes) {
    const auto path = make_path("precious.key");
    write_text(path, "PRIVATE-KEY-DO-NOT-OVERWRITE");

    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::create_private_file(path.string())),
                         static_cast<int>(platform::PrivateFileOutcome::AlreadyExists));
    // 拒绝覆盖的**实质**是原字节还在：只看返回值会放过「先截断再报错」的实现。
    AURORA_TEST_CHECK_MSG(read_text(path) == "PRIVATE-KEY-DO-NOT-OVERWRITE", "existing content intact");
}

#if !defined(_WIN32)
AURORA_TEST_CASE(owner_only_mode_is_pinned_even_when_umask_strips_owner_write) {
    // 本机实测（稿 §0 的 F6）：libssh 落盘模式受 umask 支配，同组可写的私钥会被 OpenSSH 拒用。
    // 这里把 umask 设成 0277（削掉属主写位）——open 的 mode 实参因此只剩 0400，
    // 唯有实现里那次 `fchmod` 能把 0600 钉回来。删掉 fchmod 本用例即红。
    const auto path = make_path("umask.key");
    std::error_code ec;
    std::filesystem::remove(path, ec);

    const mode_t previous = ::umask(S_IWUSR | S_IRWXG | S_IRWXO);
    const auto outcome = platform::create_private_file(path.string());
    static_cast<void>(::umask(previous));

    AURORA_TEST_CHECK_EQ(static_cast<int>(outcome),
                         static_cast<int>(platform::PrivateFileOutcome::Created));
    const auto permissions = std::filesystem::status(path).permissions();
    AURORA_TEST_CHECK_MSG((permissions & std::filesystem::perms::owner_read) != std::filesystem::perms::none,
                          "owner can read");
    AURORA_TEST_CHECK_MSG((permissions & std::filesystem::perms::owner_write) != std::filesystem::perms::none,
                          "owner can write");
    const auto world_bits = permissions & (std::filesystem::perms::group_all |
                                           std::filesystem::perms::others_all);
    AURORA_TEST_CHECK_MSG(world_bits == std::filesystem::perms::none, "no group/other bits at all");
}

AURORA_TEST_CASE(a_symlink_counts_as_existing_and_is_never_followed) {
    const auto target = make_path("symlink_target.key");
    const auto link = make_path("symlinked.key");
    std::error_code ec;
    std::filesystem::remove(link, ec);
    std::filesystem::remove(target, ec);
    write_text(target, "THE-REAL-KEY");

    std::error_code ignored;
    std::filesystem::create_symlink(target, link, ignored);
    AURORA_TEST_REQUIRE_MSG(!ignored, "symlink built for the case");

    // 经符号链接改写指向的文件是最坏的失败形态；O_EXCL 的自然结果就是把它算成「已存在」。
    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::create_private_file(link.string())),
                         static_cast<int>(platform::PrivateFileOutcome::AlreadyExists));
    AURORA_TEST_CHECK_MSG(read_text(target) == "THE-REAL-KEY", "link target untouched");
    AURORA_TEST_CHECK_TRUE(std::filesystem::is_symlink(link));
}
#endif

AURORA_TEST_CASE(a_directory_path_counts_as_existing) {
    // 两腿同口径：POSIX 下 open 对目录报 EEXIST，Windows 下 CRT 报 EACCES、由属性判定归位。
    const auto directory = std::filesystem::path{aurora::testing::isolation::temp_dir()};
    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::create_private_file(directory.string())),
                         static_cast<int>(platform::PrivateFileOutcome::AlreadyExists));
}

AURORA_TEST_CASE(missing_parent_and_illegal_names_are_errors_not_successes) {
    // 三类「建不了」都不能回成 AlreadyExists：调用方把「建不了」误读成「别覆盖」就是静默丢功能。
    // 父目录不存在：temp/missing_dir 只是个路径串，没人建过这个目录。
    const auto orphan = make_path("missing_dir") / "orphan.key";
    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::create_private_file(orphan.string())),
                         static_cast<int>(platform::PrivateFileOutcome::Error));

    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::create_private_file({})),
                         static_cast<int>(platform::PrivateFileOutcome::Error));
    // 内嵌 NUL：截断后就建到另一份路径上，比失败更糟，必须在进系统调用前挡掉。
    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::create_private_file(std::string_view{"bad\0name.key", 12})),
                         static_cast<int>(platform::PrivateFileOutcome::Error));
    AURORA_TEST_CHECK_TRUE(!std::filesystem::exists(make_path("bad")));
}

}  // namespace borealis::test_cases::utest_file_secure
