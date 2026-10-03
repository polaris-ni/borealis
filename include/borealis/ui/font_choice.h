#pragma once

// ============================================================
// 字体族选择的判定件（include/borealis/ui/font_choice.h）
// ------------------------------------------------------------
// `SPEC.FEAT.RENDER.02` 的「系统等宽字体枚举 + 内置 Cascadia Code 为默认」在应用侧的判定腿：
// 目录由装配层从框架 `render::list_font_families()` 取一次并搬成本头的 `FontFamilyEntry`，
// 本件只做「配置里那个族名能不能用」的判定，故不含框架类型（架构 §2.3）。
//
// 两条来自框架实测的口径决定了本件的形状：
// ① 框架的族名匹配是**逐字节精确、区分大小写**的，故本件不自造更宽的第二套匹配规则——
//    匹配不上就回落，而不是猜一个「大概是指它」的族。
// ② 框架的枚举有**同源保证**（列出来的族一定解析得到属于该族的面），因此「不在列表里」
//    就是「用了也只能落到默认链（非等宽）」，回落判定不需要第二处探测。
// ============================================================

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace borealis::ui {

/// @brief 内置的缺省等宽族名（裁决 7.3 的「内置 Cascadia Code 为默认」，也是配置默认值的来源）。
///
/// 框架把内嵌字节日注册到这个族名键下，故它一定出现在 `list_font_families()` 的结果里。
inline constexpr std::string_view kDefaultMonospaceFamily{"Cascadia Code"};

/// @brief 字体族目录里的一项：由调用方从框架 `render::FontFamilyInfo` 逐字段搬值。
struct FontFamilyEntry {
    std::string family;
    bool monospace = false;  ///< 框架**以度量判定**的等宽性（族名只作度量不可得时的补充命中）。
};

/// @brief 配置的族名在目录里的判定结果（只作诊断文案的来源，不影响绘制路径）。
enum class FontFamilyVerdict : std::uint8_t {
    Configured,     ///< 命中目录且等宽：原样采用。
    NotMonospace,   ///< 命中目录但度量非等宽：采用会破坏网格对齐，故回落。
    Unlisted,       ///< 目录里没有这个族（未安装、拼写不符或大小写不一致）：回落。
};

/// @brief 字体族选择的结论。
struct FontFamilyChoice {
    std::string family;  ///< 交给框架 `Font::family` 的最终族名（命中时是目录里的规范拼写）。
    FontFamilyVerdict verdict = FontFamilyVerdict::Unlisted;
};

/// @brief 在字体族目录里判定配置的族名可用与否，不可用时回落内置等宽族。
///
/// 回落值恒为 `kDefaultMonospaceFamily` 而**不查目录**：该族由框架内嵌注册，目录里没有它只会
/// 意味着调用方给的是空目录（尚未枚举的降级形态），此时回落名仍是唯一可用答案，而不是「无解」。
/// @param configured 配置键 `appearance.font_family` 的取值。
/// @param catalog 字体族目录（框架枚举结果搬值而来，升序去重由框架保证）。
/// @return 采用的族名与判定来源。
[[nodiscard]] auto choose_font_family(std::string_view configured, std::span<const FontFamilyEntry> catalog)
    -> FontFamilyChoice;

}  // namespace borealis::ui
