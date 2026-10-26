// ============================================================
// UTF-8 路径 → 原生 path：Win32（src/platform/win/native_path.cpp）
// ------------------------------------------------------------
// platform::native_path_of 的 Windows 腿。宽串在这里就定下来：此后 `std::ofstream` 与
// `std::filesystem::rename` 拿到的是**已经是宽**的 path，不再触发窄串→码页那次转换（本机
// ACP=936，那次转换会把 UTF-8 中文名解成花码，接口头注记着这条读数）。
// ============================================================

#include "platform/native_path.h"

#include <string>
#include <utility>

#include "win_text.h"

namespace borealis::platform {

auto native_path_of(std::string_view utf8) -> std::filesystem::path {
    if (utf8.empty()) {
        return {};
    }
    auto wide = to_wide(utf8);
    if (wide.empty()) {
        return {};  // 非法 UTF-8：`to_wide` 全程折不出东西，交回空 path 让调用方判掉
    }
    return std::filesystem::path{std::move(wide)};
}

}  // namespace borealis::platform
