#pragma once

// ============================================================
// UTF-8 路径串 → 原生 path（src/platform/native_path.h）
// ------------------------------------------------------------
// 本仓对外与跨层的路径串一律 UTF-8（`create_private_file` 收的串、各面板交回的选路、
// `SessionLog` 的路径都是），而 `std::filesystem::path` **从窄串构造时走本地码页**。
// Windows 上这条实测过（2026-10-26，一次性探针不入仓）：本机 ACP=936，同一串 UTF-8 中文文件名
// 经 `create_private_file`（腿内自做 UTF-8→UTF-16）落到的是那个真名，而经
// `std::ofstream{std::filesystem::path{narrow}}` 落到的是按 GBK 逐对字节解出来的花码名——
// 两份不同的文件。共享代码因此不得直接从窄串构 path，只经本件（AGENTS.md §4.5 第 23 条；
// 共享私有头 + 两腿的形态同 `platform/credential_store.{h,cpp}`）。
// ============================================================

#include <filesystem>
#include <string_view>

namespace borealis::platform {

/// @brief 把一段 UTF-8 路径串折成可以交给标准流与 `std::filesystem` 的原生 path。
///
/// POSIX 的本地文件名本就是字节串，直传即逐字正确；Windows 先转 UTF-16 再以宽串构 path，
/// 后续 CRT 与 Win32 全程收宽串，不再经过码页。
/// @param utf8 路径串（UTF-8）。内嵌 NUL 在这里只会被截短，「判非法」是调用方的事。
/// @return 原生 path；空串、以及**非法 UTF-8 折不出任何宽字符**时回空 path——调用方须先判空，
///         空 path 在两套实现里各有别的含义（POSIX 上是「当前目录」）。
[[nodiscard]] auto native_path_of(std::string_view utf8) -> std::filesystem::path;

}  // namespace borealis::platform
