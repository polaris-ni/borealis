// ============================================================
// 会话恢复快照的落盘与提交（src/config/workspace_store.cpp）
// ------------------------------------------------------------
// 与 `store.cpp` / `window_geometry_store.cpp` 同为「触达 Aurora `Preferences`」的那一小片地方
// （裁决 7.26③ 的口径：框架类型不进公共头）。本件**不**读写 `borealis.json`，也**不**参与 schema
// 自校验与降级备份（稿 D8①），更不懂布局的语义——那些都归 `ui::WorkspaceSnapshot`（批 2）。
// ============================================================

#include "workspace_store.h"

#include <filesystem>
#include <optional>
#include <utility>

namespace borealis::config {

WorkspaceStore::WorkspaceStore()
    // 与 `borealis.json`、`windows.json` 同一层（裁决 7.26③ 的落点：配置目录不含应用子目录）。
    : WorkspaceStore(au::preferences::Preferences::default_config_dir() / kWorkspaceFileName) {}

WorkspaceStore::WorkspaceStore(std::filesystem::path file)
    : prefs_(std::in_place, std::move(file)) {}

auto WorkspaceStore::preferences() noexcept -> au::preferences::Preferences * {
    return &*prefs_;
}

auto WorkspaceStore::path() const noexcept -> const std::filesystem::path & {
    return prefs_->file_path();
}

auto WorkspaceStore::flush() -> std::optional<std::string> {
    if (const auto flushed = prefs_->flush(); !flushed.ok()) {
        return flushed.error().message;
    }
    return std::nullopt;
}

}  // namespace borealis::config
