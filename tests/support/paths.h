#pragma once

// ============================================================
// 测试公共设施（tests/support/paths.h）—— 仓库相对路径解析
// ------------------------------------------------------------
// 复刻 Aurora 主仓 tests/support/paths.h（许可与同步口径见 tests/framework/README.md）：
// 框架启动时已把 cwd 统一切到仓库根，本头提供显式的仓库根拼接，供夹具回放等需要
// 绝对路径的场景使用。仓库根不可定位时原样退回相对路径（保持可运行）。
// ============================================================

#include <filesystem>
#include <string>
#include <string_view>

#include "framework/isolation.h"

namespace aurora::testing::paths {

/// @brief 仓库根绝对路径（从可执行文件位置向上定位；失败返回空串）。
[[nodiscard]] inline auto repo_root() -> const std::string & { return isolation::repo_root(); }

/// @brief 仓库根下的绝对路径；仓库根不可定位时原样返回相对路径（保持可运行）。
[[nodiscard]] inline auto under_repo(std::string_view relative) -> std::string {
    const auto &root = repo_root();
    if (root.empty()) {
        return std::string{relative};
    }
    return (std::filesystem::path{root} / std::string_view{relative}).string();
}

}  // namespace aurora::testing::paths
