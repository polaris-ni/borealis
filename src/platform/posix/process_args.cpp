// ============================================================
// 进程参数来源腿：POSIX（src/platform/posix/process_args.cpp）
// ------------------------------------------------------------
// POSIX 的 `argv` 已是字节串；本腿只做所有权转移，不做任何转码。
// ============================================================

#include "platform/process_args.h"

#include <iterator>

namespace borealis::platform {

auto process_arguments(int argc, char **argv) -> std::vector<std::string> {
    if (argc <= 0 || argv == nullptr) {
        return {};
    }
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        out.emplace_back(argv[i] == nullptr ? "" : argv[i]);
    }
    return out;
}

}  // namespace borealis::platform
