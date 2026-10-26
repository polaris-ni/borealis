/// 测试类型: unit
/// 目标单元: src/platform/file_secure.h + 共享腿 src/platform/file_secure.cpp +
///           两腿 src/platform/{posix,win}/file_secure.cpp、src/platform/native_path.h
/// 测试说明: SPEC.FEAT.CONN.10 私钥落盘的第一道闸（裁决 7.106 D5①）——「已存在即不碰」与
///           「权限钉死 0600」两件事各自独立可验：覆盖拒绝要能在既有文件已有内容时验出，
///           权限要在**故意让 umask 削掉属主写位**的现场验出（不注入这条 umask，删掉实现里的
///           fchmod 也照样全绿，因为默认 umask 恰好不动属主位）。Windows 腿同接口，
///           其 ACL 边界见 file_secure.cpp 头注释与稿 §7。
///           CONN.05 三期补上 `write_private_file`（整份就位、不留半成品）与 `native_path_of`
///           （UTF-8 路径名在码页制 Windows 上逐字落位），两件的判据在本文件后半。

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

AURORA_TEST_CASE(the_whole_text_lands_and_no_scratch_is_left_behind) {
    // D23① 的两条判据各一条：字节全在（截断写不是追加）、本次的临时件不留（半成品不外溢给用户）。
    const auto path = make_path("dump.txt");
    const auto scratch = make_path("dump.txt.tmp");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(scratch, ec);

    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::write_private_file(path.string(), "RX  00\n")),
                         static_cast<int>(platform::PrivateWriteOutcome::Written));
    AURORA_TEST_CHECK_MSG(read_text(path) == "RX  00\n", "every byte, in order");
    AURORA_TEST_CHECK_TRUE(!std::filesystem::exists(scratch));
}

AURORA_TEST_CASE(the_second_write_replaces_the_target_whole) {
    // 「用户点了保存」的应有之义：目标已存在即整体换新（稿 §10.2 D23③——本件不替用户拒绝覆盖）。
    // 只看第二次回 Written 不够：写成 append 的实现也会回 Written，而那份转储就成了两屏叠一起。
    const auto path = make_path("dump_twice.txt");
    std::error_code ec;
    std::filesystem::remove(path, ec);

    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::write_private_file(path.string(), "AAAAAAAA")),
                         static_cast<int>(platform::PrivateWriteOutcome::Written));
    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::write_private_file(path.string(), "BB")),
                         static_cast<int>(platform::PrivateWriteOutcome::Written));
    AURORA_TEST_CHECK_MSG(read_text(path) == "BB", "the old bytes are gone");
    AURORA_TEST_CHECK_EQ(static_cast<std::size_t>(std::filesystem::file_size(path)), 2U);
    AURORA_TEST_CHECK_TRUE(!std::filesystem::exists(make_path("dump_twice.txt.tmp")));
}

AURORA_TEST_CASE(a_taken_scratch_name_moves_to_the_next_slot_untouched) {
    // 逐档改名这一条的两半：占位的文件**逐字不动**（那是别人的盘，可能是用户自己的同名文件），
    // 而导出本身照旧成功（不这样，一次崩溃留下的临时件就把这个目标永久封死）。
    const auto path = make_path("dump_ladder.txt");
    const auto first = make_path("dump_ladder.txt.tmp");
    const auto second = make_path("dump_ladder.txt.tmp1");
    const auto third = make_path("dump_ladder.txt.tmp2");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(third, ec);
    write_text(first, "NOT-MINE");
    write_text(second, "ALSO-NOT-MINE");

    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::write_private_file(path.string(), "MINE")),
                         static_cast<int>(platform::PrivateWriteOutcome::Written));
    AURORA_TEST_CHECK_MSG(read_text(path) == "MINE", "the export still landed");
    AURORA_TEST_CHECK_MSG(read_text(first) == "NOT-MINE", "the taken scratch is not mine to touch");
    AURORA_TEST_CHECK_MSG(read_text(second) == "ALSO-NOT-MINE", "same for the second slot");
    AURORA_TEST_CHECK_TRUE(!std::filesystem::exists(third));  // 第三档用完即删
}

AURORA_TEST_CASE(a_refused_or_broken_write_never_moves_anything) {
    // 三条「什么都没发生」：空串与内嵌 NUL 判非法（进系统调用只会落到另一份路径），父目录不在判失败。
    // 每条都断言目标那一份**没有**凭空出现：让失败留下半份东西是本件最该防住的形态。
    const auto dir = std::filesystem::path{aurora::testing::isolation::temp_dir()};
    const auto orphan = (dir / "dump_no_parent") / "dump.txt";
    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::write_private_file(std::string_view{}, "x")),
                         static_cast<int>(platform::PrivateWriteOutcome::InvalidPath));
    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::write_private_file(
                             std::string_view{"dump\0bad.txt", 10}, "x")),
                         static_cast<int>(platform::PrivateWriteOutcome::InvalidPath));
    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::write_private_file(orphan.string(), "x")),
                         static_cast<int>(platform::PrivateWriteOutcome::Failed));
    AURORA_TEST_CHECK_TRUE(!std::filesystem::exists(make_path("dump")));
    AURORA_TEST_CHECK_TRUE(!std::filesystem::exists(make_path("dump_no_parent")));
}

#if !defined(_WIN32)
AURORA_TEST_CASE(the_written_file_keeps_the_owner_only_mode) {
    // 钉住「0600 在建出来那一刻定下、截断写与 rename 都不改它」这一条链：只测 create_private_file
    // 的 mode 不够，把实现改成「先 ofstream 再 chmod」在本用例面前同样得红。
    const auto path = make_path("dump_mode.txt");
    std::error_code ec;
    std::filesystem::remove(path, ec);

    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::write_private_file(path.string(), "RX  00\n")),
                         static_cast<int>(platform::PrivateWriteOutcome::Written));
    const auto permissions = std::filesystem::status(path).permissions();
    AURORA_TEST_CHECK_MSG((permissions & std::filesystem::perms::owner_read) != std::filesystem::perms::none,
                          "owner can read");
    AURORA_TEST_CHECK_MSG((permissions & std::filesystem::perms::owner_write) != std::filesystem::perms::none,
                          "owner can write");
    const auto world_bits = permissions & (std::filesystem::perms::group_all |
                                           std::filesystem::perms::others_all);
    AURORA_TEST_CHECK_MSG(world_bits == std::filesystem::perms::none, "no group/other bits at all");
}
#endif

#if defined(_WIN32)
AURORA_TEST_CASE(a_non_ascii_name_lands_at_that_exact_name) {
    // Windows 独有的那条码页闸（本机 2026-10-26 实测 ACP=936）：`std::filesystem::path` 从窄串构造
    // 时按 GBK 解，一串 UTF-8 中文就落成花码名——同一份文本因此会写到用户没点的那条路径上。
    // 判据只能取**盘上真名的 UTF-8 字节**：宽 API 枚举后 `u8string()`，与请求的字节逐字节比。
    // POSIX 侧不设本例——那里本地名本就是字节串，UTF-8 直传没有可过的一道转换。
    const auto directory = std::filesystem::path{aurora::testing::isolation::temp_dir()};
    const std::string leaf = "\xE4\xB8\xAD\xE6\x96\x87.dump";  // 中文名的 UTF-8 转义（字面量仍全 ASCII）
    const std::string target = directory.string() + "/" + leaf;

    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::write_private_file(target, "RX  00\n")),
                         static_cast<int>(platform::PrivateWriteOutcome::Written));

    int hits = 0;
    for (const auto &entry : std::filesystem::directory_iterator{directory}) {
        const auto name = entry.path().filename().u8string();
        if (std::string_view{reinterpret_cast<const char *>(name.data()), name.size()} == leaf) {
            ++hits;
            std::ifstream in{entry.path(), std::ios::binary};
            const std::string content{std::istreambuf_iterator<char>{in},
                                      std::istreambuf_iterator<char>{}};
            AURORA_TEST_CHECK_MSG(content == "RX  00\n", "the bytes are at the exact name");
        }
    }
    AURORA_TEST_CHECK_EQ(hits, 1);  // 0＝落到花码名上；>1＝多写了一份
}
#endif

}  // namespace borealis::test_cases::utest_file_secure
