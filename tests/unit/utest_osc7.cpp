/// 测试类型: unit
/// 目标单元: include/borealis/ui/osc7.h + src/ui/osc7.cpp
/// 测试说明: `SPEC.FEAT.INTEG.02` 的解析与取舍腿：只认空 host 与 `localhost`（大小写不敏感），
///           远端机器名一律不猜；scheme 限 `file://`；百分号解码从严（坏 `%` 与 NUL 整条拒绝）；
///           问号/井号在解码前截断而 `%3F`/`%23` 留在文件名里；POSIX 要求绝对路径、Windows 要求
///           `/C:/...` 并在折算后去掉盘符前那一枚 `/`；继承只在本机会话发生，配置值在其余档原样交回。

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "borealis/ui/osc7.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_osc7 {

namespace {

using borealis::ui::choose_start_directory;
using borealis::ui::local_directory_from_osc7;
using borealis::ui::Osc7PathStyle;
using borealis::ui::StartDirectory;

/// @brief 解析并核对结果；失败时返回 false（用例里与 `AURORA_TEST_CHECK` 同用）。
auto parses_to(std::u32string_view raw, Osc7PathStyle style, std::string_view expected) -> bool {
    const auto parsed = local_directory_from_osc7(raw, style);
    return parsed.has_value() && *parsed == expected;
}

/// @brief 该原文在该形态下必须被拒绝。
auto rejects(std::u32string_view raw, Osc7PathStyle style) -> bool {
    return !local_directory_from_osc7(raw, style).has_value();
}

}  // namespace

AURORA_TEST_CASE(posix_accepts_empty_host_and_localhost) {
    AURORA_TEST_CHECK(parses_to(U"file:///srv/repo", Osc7PathStyle::Posix, "/srv/repo"));
    AURORA_TEST_CHECK(parses_to(U"file://localhost/srv/repo", Osc7PathStyle::Posix, "/srv/repo"));
    AURORA_TEST_CHECK(parses_to(U"file://LOCALHOST/srv/repo", Osc7PathStyle::Posix, "/srv/repo"));
    AURORA_TEST_CHECK(parses_to(U"FILE:///srv/repo", Osc7PathStyle::Posix, "/srv/repo"));
    AURORA_TEST_CHECK(parses_to(U"file:///", Osc7PathStyle::Posix, "/"));
}

AURORA_TEST_CASE(remote_or_missing_host_is_rejected) {
    AURORA_TEST_CHECK(rejects(U"file://hostA/srv/repo", Osc7PathStyle::Posix));
    AURORA_TEST_CHECK(rejects(U"file://localhost.example/srv", Osc7PathStyle::Posix));
    AURORA_TEST_CHECK(rejects(U"file://localhost:22/srv", Osc7PathStyle::Posix));
    // 空 host 后接 `//server/share`：POSIX 上是一条合法的绝对路径，不按 UNC 解释。
    AURORA_TEST_CHECK(parses_to(U"file:////server/share", Osc7PathStyle::Posix, "//server/share"));
    AURORA_TEST_CHECK(rejects(U"file://localhost", Osc7PathStyle::Posix));  // 只有机器，没有目录
    AURORA_TEST_CHECK(rejects(U"file://", Osc7PathStyle::Posix));
}

AURORA_TEST_CASE(non_file_scheme_or_broken_prefix_is_rejected) {
    AURORA_TEST_CHECK(rejects(U"", Osc7PathStyle::Posix));
    AURORA_TEST_CHECK(rejects(U"http://localhost/srv", Osc7PathStyle::Posix));
    AURORA_TEST_CHECK(rejects(U"file:/srv", Osc7PathStyle::Posix));
    AURORA_TEST_CHECK(rejects(U"files:///srv", Osc7PathStyle::Posix));
    AURORA_TEST_CHECK(rejects(U"vscode://file/srv", Osc7PathStyle::Posix));
}

AURORA_TEST_CASE(percent_decoding_is_strict_and_utf8_aware) {
    AURORA_TEST_CHECK(parses_to(U"file:///srv/repo%20dir", Osc7PathStyle::Posix, "/srv/repo dir"));
    AURORA_TEST_CHECK(parses_to(U"file:///srv/a%2Fb", Osc7PathStyle::Posix, "/srv/a/b"));
    AURORA_TEST_CHECK(parses_to(U"file:///srv/%E4%B8%AD%E6%96%87", Osc7PathStyle::Posix,
                                "/srv/\xE4\xB8\xAD\xE6\x96\x87"));
    // 原样码点也走同一条编码出口，不因「没写百分号」而区别于上一条。
    AURORA_TEST_CHECK(parses_to(U"file:///srv/\u4E2D", Osc7PathStyle::Posix, "/srv/\xE4\xB8\xAD"));

    AURORA_TEST_CHECK(rejects(U"file:///srv/%", Osc7PathStyle::Posix));
    AURORA_TEST_CHECK(rejects(U"file:///srv/%2", Osc7PathStyle::Posix));
    AURORA_TEST_CHECK(rejects(U"file:///srv/%2G", Osc7PathStyle::Posix));
    AURORA_TEST_CHECK(rejects(U"file:///srv/%GG", Osc7PathStyle::Posix));
    AURORA_TEST_CHECK(rejects(U"file:///srv/%00x", Osc7PathStyle::Posix));  // 串内 NUL 会让平台入口截短
    std::u32string with_nul = U"file:///srv/x";  // 字面量会在 NUL 处截断，故显式拼一条含 NUL 的原文
    with_nul.push_back(U'\0');
    with_nul += U"y";
    AURORA_TEST_CHECK(rejects(with_nul, Osc7PathStyle::Posix));
}

AURORA_TEST_CASE(query_and_fragment_are_cut_before_decoding) {
    AURORA_TEST_CHECK(parses_to(U"file:///srv/repo?rev=1", Osc7PathStyle::Posix, "/srv/repo"));
    AURORA_TEST_CHECK(parses_to(U"file:///srv/repo#frag", Osc7PathStyle::Posix, "/srv/repo"));
    // 编码后的问号是文件名的一部分：截断必须发生在解码之前。
    AURORA_TEST_CHECK(parses_to(U"file:///srv/a%3Fb%23c", Osc7PathStyle::Posix, "/srv/a?b#c"));
}

AURORA_TEST_CASE(windows_drive_letter_paths_are_unwrapped) {
    AURORA_TEST_CHECK(parses_to(U"file:///C:/work", Osc7PathStyle::Windows, "C:/work"));
    AURORA_TEST_CHECK(parses_to(U"file://localhost/C:/work", Osc7PathStyle::Windows, "C:/work"));
    AURORA_TEST_CHECK(parses_to(U"file:///c:/work", Osc7PathStyle::Windows, "c:/work"));
    AURORA_TEST_CHECK(parses_to(U"file:///D:/", Osc7PathStyle::Windows, "D:/"));
    AURORA_TEST_CHECK(parses_to(U"file:///C%3A/work", Osc7PathStyle::Windows, "C:/work"));
    AURORA_TEST_CHECK(parses_to(U"file:///C:%5Cwork", Osc7PathStyle::Windows, "C:\\work"));
}

AURORA_TEST_CASE(windows_rejects_forms_that_are_not_drive_absolute) {
    AURORA_TEST_CHECK(rejects(U"file:///srv/repo", Osc7PathStyle::Windows));
    AURORA_TEST_CHECK(rejects(U"file:///", Osc7PathStyle::Windows));
    AURORA_TEST_CHECK(rejects(U"file:///C:", Osc7PathStyle::Windows));      // 只报盘符＝该盘当前目录
    AURORA_TEST_CHECK(rejects(U"file:///C:work", Osc7PathStyle::Windows));  // 缺冒号后的分隔符
    AURORA_TEST_CHECK(rejects(U"file:///1:/work", Osc7PathStyle::Windows));
    AURORA_TEST_CHECK(rejects(U"file:////server/share", Osc7PathStyle::Windows));  // UNC 不在本棒范围
}

AURORA_TEST_CASE(posix_style_keeps_drive_like_paths_verbatim) {
    AURORA_TEST_CHECK(parses_to(U"file:///C:/work", Osc7PathStyle::Posix, "/C:/work"));
    AURORA_TEST_CHECK(parses_to(U"file:///srv/%5C", Osc7PathStyle::Posix, "/srv/\\"));
}

AURORA_TEST_CASE(invalid_code_points_make_the_whole_report_unusable) {
    std::u32string raw = U"file:///srv/";
    raw.push_back(static_cast<char32_t>(0xD800U));  // 孤立代理项：UTF-8 不可表示
    AURORA_TEST_CHECK(rejects(raw, Osc7PathStyle::Posix));
}

AURORA_TEST_CASE(choose_start_directory_prefers_osc7_only_for_local_sessions) {
    AURORA_TEST_CHECK(choose_start_directory(true, U"file:///srv/repo", "/configured",
                                             Osc7PathStyle::Posix) ==
                      (StartDirectory{.path = "/srv/repo", .inherited = true}));
    // 远端 host 解析不出 ⇒ 配置值原样交回，且不标记继承。
    AURORA_TEST_CHECK(choose_start_directory(true, U"file://hostA/srv/repo", "/configured",
                                             Osc7PathStyle::Posix) ==
                      (StartDirectory{.path = "/configured", .inherited = false}));
    // SSH / 串口 / Telnet 标签：即使原文看着可用也不继承。
    AURORA_TEST_CHECK(choose_start_directory(false, U"file:///srv/repo", "/configured",
                                             Osc7PathStyle::Posix) ==
                      (StartDirectory{.path = "/configured", .inherited = false}));
    AURORA_TEST_CHECK(choose_start_directory(true, U"", "", Osc7PathStyle::Posix) ==
                      (StartDirectory{.path = "", .inherited = false}));
    AURORA_TEST_CHECK(choose_start_directory(true, U"file:///srv/repo", "", Osc7PathStyle::Windows) ==
                      (StartDirectory{.path = "", .inherited = false}));
}

}  // namespace borealis::test_cases::utest_osc7
