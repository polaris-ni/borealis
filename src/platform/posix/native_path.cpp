// ============================================================
// UTF-8 路径 → 原生 path：POSIX（src/platform/posix/native_path.cpp）
// ------------------------------------------------------------
// platform::native_path_of 的 POSIX 腿：本地文件名本就是字节串，UTF-8 逐字直传即逐字正确
// （这里没有码页可过）。本文件不含任何 POSIX 系统调用，只为本仓「共享代码不直接从窄串构
// path」这一条纪律留一个落点。
// ============================================================

#include "platform/native_path.h"

#include <string>
#include <utility>

namespace borealis::platform {

auto native_path_of(std::string_view utf8) -> std::filesystem::path {
    if (utf8.empty()) {
        return {};
    }
    return std::filesystem::path{std::string{utf8}};
}

}  // namespace borealis::platform
