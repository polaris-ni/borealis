/// 测试类型: unit
/// 目标单元: src/platform/posix/user_dir.cpp
/// 测试说明: `SPEC.FEAT.CONN.10` 的 D10① 平台腿——`platform::ssh_user_dir()` 只是 `$HOME`
///           的搬运工：设了就**原样**回（连尾分隔符都不裁，裁法是 `conn::normalize_key_dirs()`
///           的既有判据，这里再裁一次就是两处真值源），空值或未设都回空串，上层据此「不扫」
///           而不是去扫一个猜出来的目录。
///
///           本文件只在非 Win32 编译（`cmake/BorealisTests.cmake` 按 `/utest_posix_` 把平台专属
///           套件挡在另一侧的构建面之外）；Win32 腿取 `_wgetenv(L"USERPROFILE")` 再转 UTF-8，
///           本机无 MSVC 故该腿未经编译验证。

#include <unistd.h>  // setenv, unsetenv

#include <cstdlib>
#include <string>

#include "framework/aurora_test.h"
#include "platform/user_dir.h"

namespace borealis::test_cases::utest_posix_user_dir {

namespace {

/// @brief 在 `$HOME` 被改成 @p value（nullptr＝删除该变量）期间取一次结果，取完原样归还。
///
/// 用例跑在同一进程里，环境变量必须恢复到进入前的状态（含「原本没有该变量」那一档），
/// 否则后面的用例会看到被污染的值。
[[nodiscard]] auto user_dir_with_home(const char *value) -> std::string {
    const auto *previous = std::getenv("HOME");
    const auto previous_value = previous != nullptr ? std::string{previous} : std::string{};
    const auto had_home = previous != nullptr;

    if (value == nullptr) {
        static_cast<void>(::unsetenv("HOME"));
    } else {
        static_cast<void>(::setenv("HOME", value, 1));
    }
    const auto out = borealis::platform::ssh_user_dir();

    if (had_home) {
        static_cast<void>(::setenv("HOME", previous_value.c_str(), 1));
    } else {
        static_cast<void>(::unsetenv("HOME"));
    }
    return out;
}

/// @brief 进程当前的 `$HOME`（未设时回空串），只用于核对本用例有没有把它原样归还。
[[nodiscard]] auto current_home() -> std::string {
    const auto *raw = std::getenv("HOME");
    return raw != nullptr ? std::string{raw} : std::string{};
}

}  // namespace

AURORA_TEST_CASE(the_home_variable_is_handed_over_verbatim) {
    const auto original = current_home();
    AURORA_TEST_CHECK_MSG(user_dir_with_home("/home/dev") == "/home/dev", "absolute path as given");
    AURORA_TEST_CHECK_MSG(user_dir_with_home("/home/dev/") == "/home/dev/", "no trimming on this side");
    // 归还后进程环境是干净的：同进程的其它用例（含配置目录解析）不该看见被污染的值。
    AURORA_TEST_CHECK_MSG(current_home() == original, "HOME is back to what it was");
}

AURORA_TEST_CASE(an_absent_or_empty_home_reads_as_no_user_directory) {
    AURORA_TEST_CHECK_MSG(user_dir_with_home("").empty(), "an empty HOME is not a directory");
    AURORA_TEST_CHECK_MSG(user_dir_with_home(nullptr).empty(), "an unset HOME is not a directory");
}

}  // namespace borealis::test_cases::utest_posix_user_dir
