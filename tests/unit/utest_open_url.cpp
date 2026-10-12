/// 测试类型: unit
/// 目标单元: src/platform/open_url.h + src/platform/{posix,win}/open_url.cpp
/// 测试说明: SPEC.FEAT.INTERACT.05 的生产打开腿（裁决 7.119 D10①）——**只断拒绝腿**。
///           这不是测试面的偷懒而是承重的口径：稿 §0 的 F15 记的是一手事故，一次
///           `timeout 8 xdg-open "https://example.com"` 在本机 rc=0 并真的在用户桌面上
///           开出了一扇 Edge 窗口。故 `Opened` 档**永远不进自动化**，UI 侧的打开动作一律
///           走 `Presentation::open_url` 替身；本件能证的只有「什么样的串根本不该交出去」与
///           「系统里没有桥时给出的是读点而不是猜」。
///           两腿共用同一枚入参闸门（`is_unacceptable` 各写一份，不为三行判据造共享件），
///           故前三条断言在 MSVC 与 GCC 上必须是同一读数；`PATH` 那两条只属 POSIX 腿
///           （Win32 的关联表在注册表里，没有 `PATH` 可挪）。本机无 MSVC，Win 腿未经编译验证。

#include "platform/open_url.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_open_url {

namespace {

using namespace borealis;

[[nodiscard]] auto make_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path{aurora::testing::isolation::temp_dir()} / std::string{name};
}

/// @brief 造一枚定长内容的文件，只为给它一个「在 PATH 上但不可执行」的身份。
auto write_file(const std::filesystem::path &path, std::string_view text) -> void {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
}

#if !defined(_WIN32)
/// @brief 把 `PATH` 换成给定值、跑一次 `open_url`、再把原值换回去。
///
/// 恢复是**必须**的：测试跑在同一进程里，留下一份残缺 `PATH` 会让后面的用次要不到真实工具链。
/// 原值可能压根不存在（`getenv` 交回 `nullptr`），那一档要用 `unsetenv` 复原而不是塞空串。
template <typename Callable>
[[nodiscard]] auto with_path(const char *path_value, Callable &&run) -> platform::OpenUrlOutcome {
    const char *previous = std::getenv("PATH");
    const std::string saved{previous == nullptr ? std::string{} : std::string{previous}};
    if (path_value == nullptr) {
        static_cast<void>(::unsetenv("PATH"));
    } else {
        static_cast<void>(::setenv("PATH", path_value, 1));
    }
    const auto outcome = run();
    if (previous == nullptr) {
        static_cast<void>(::unsetenv("PATH"));
    } else {
        static_cast<void>(::setenv("PATH", saved.c_str(), 1));
    }
    return outcome;
}
#endif

}  // namespace

AURORA_TEST_CASE(an_empty_target_is_refused_and_never_reaches_the_desktop) {
    // 空串是最坏的一档：`xdg-open ""` 会打开浏览器的默认页，于是「没有链接」在屏上表现为
    // 「开了一扇没 asked 的窗口」。实现里的顺序因此是承重的——闸门必须在找桥之前。
    AURORA_TEST_CHECK_EQ(static_cast<int>(platform::open_url("")),
                         static_cast<int>(platform::OpenUrlOutcome::Error));
}

AURORA_TEST_CASE(a_control_character_anywhere_in_the_target_refuses_the_whole_string) {
    // C0 与 DEL 全部拒收而不是清洗（头注⑵）：链接字符表本就不产这些字节，收到它们意味着调用方
    // 越过了识别件，而「猜一个安全形态」正是把注入面重新打开的方式。
    // 逐字节都要单独喂：只看整串会被「某一枚已经挡住」掩盖，删掉判据里的 0x7F 就没人发现。
    static constexpr std::string_view kRefused[] = {
        std::string_view{"https://example.com/a\nb"},
        std::string_view{"https://example.com/a\rb"},
        std::string_view{"https://example.com/a\tb"},
        std::string_view{"https://example.com/a\1b"},   // SOH：闸门是「任何 C0」而不是「换行族」
        std::string_view{"https://example.com/a\033b"},  // ESC：转义序列的引入字节
        std::string_view{"https://example.com/a\177b"},  // DEL
    };
    for (const std::string_view target : kRefused) {
        AURORA_TEST_CHECK_MSG(platform::open_url(target) == platform::OpenUrlOutcome::Error,
                              std::string{target}.c_str());
    }
    // 内嵌 NUL 走不到 `c_str()` 那条腿，故用带长度的 `string_view` 单独验：只按 C 字符串判长度
    // 的实现（`strlen`）会在这里露出来，而那正是「空串后面还藏着东西」的形态。
    static constexpr char kWithNul[] = {'h', 't', 't', 'p', 's', ':', '/', '/', 'x', '\0', 'y'};
    AURORA_TEST_CHECK_EQ(
        static_cast<int>(platform::open_url(std::string_view{kWithNul, sizeof(kWithNul)})),
        static_cast<int>(platform::OpenUrlOutcome::Error));
}

#if !defined(_WIN32)
AURORA_TEST_CASE(an_empty_path_reports_no_bridge_rather_than_guessing_one) {
    // 「没有桥」必须是一条**读点**：由 `access(X_OK)` 在 `PATH` 上逐目录找出来，而不是靠孙进程
    // `execv` 失败再猜（那条腿父侧根本收不到退出码，见头注⑵）。这里给一枚空目录当整条 `PATH`，
    // 三档缺失形态（空值、空串、只有分隔符）都必须落在同一个 `NotFound` 上。
    const auto dir = make_path("open_url_empty_path");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    AURORA_TEST_REQUIRE_MSG(!ec, "scratch PATH directory created");
    const std::string path_value{dir.string()};

    AURORA_TEST_CHECK_EQ(static_cast<int>(with_path(path_value.c_str(),
                                                     []() -> platform::OpenUrlOutcome {
                                                         return platform::open_url("https://example.com");
                                                     })),
                         static_cast<int>(platform::OpenUrlOutcome::NotFound));
    AURORA_TEST_CHECK_EQ(static_cast<int>(with_path("",
                                                    []() -> platform::OpenUrlOutcome {
                                                        return platform::open_url("https://example.com");
                                                    })),
                         static_cast<int>(platform::OpenUrlOutcome::NotFound));
    AURORA_TEST_CHECK_EQ(static_cast<int>(with_path(":/nonexistent-directory-for-this-case:",
                                                    []() -> platform::OpenUrlOutcome {
                                                        return platform::open_url("https://example.com");
                                                    })),
                         static_cast<int>(platform::OpenUrlOutcome::NotFound));
}

AURORA_TEST_CASE(path_is_absent_entirely_and_that_is_still_no_bridge) {
    // `PATH` 未定义与 `PATH` 为空不是一件事：前者让任何「拼出来再找」的实现退化成猜，
    // 本腿的口径是同一档 `NotFound`。这一条会在「改用 `execvp`（它自带 PATH 查找、
    // 且在 `PATH` 缺失时回落到系统默认目录）」的实现上变红——那一档回落正是本腿刻意不要的。
    AURORA_TEST_CHECK_EQ(static_cast<int>(with_path(nullptr,
                                                    []() -> platform::OpenUrlOutcome {
                                                        return platform::open_url("https://example.com");
                                                    })),
                         static_cast<int>(platform::OpenUrlOutcome::NotFound));
}

AURORA_TEST_CASE(a_present_but_non_executable_bridge_is_not_a_bridge) {
    // 判据是 `X_OK` 而不是 `F_OK`：一枚存在但不可执行的 `xdg-open`（比如刚 `chmod -x` 掉的
    // 半成品脚本）交出去只会得到孙进程里的 `EACCES`，而那一档父侧收不到 ⇒ 屏上表现为
    // 「点了没反应」。本用例的实现若退化成 `F_OK`，这里 fork 出的 `execv` 同样不会执行任何东西
    // （内核直接拒绝），只是返回值会变成 `Opened` ⇒ 断言变红，而不是留下一次真打开。
    const auto dir = make_path("open_url_non_executable");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    AURORA_TEST_REQUIRE_MSG(!ec, "scratch bridge directory created");
    const auto bridge = dir / "xdg-open";
    write_file(bridge, "#!/bin/sh\necho THIS FILE IS NOT EXECUTABLE\n");
    ::chmod(bridge.c_str(), S_IRUSR | S_IWUSR);  // 0644：可读可写、不可执行
    AURORA_TEST_REQUIRE_MSG(std::filesystem::exists(bridge), "the fake bridge is in place");

    const std::string path_value{dir.string()};
    AURORA_TEST_CHECK_EQ(static_cast<int>(with_path(path_value.c_str(),
                                                    []() -> platform::OpenUrlOutcome {
                                                        return platform::open_url("https://example.com");
                                                    })),
                         static_cast<int>(platform::OpenUrlOutcome::NotFound));
    static_cast<void>(std::filesystem::remove(bridge, ec));
}
#endif

}  // namespace borealis::test_cases::utest_open_url
