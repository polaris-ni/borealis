// ============================================================
// 逐窗几何的存储与提交（src/config/window_geometry_store.cpp）
// ------------------------------------------------------------
// 与 `config/store.cpp` 同为「触达 Aurora `Preferences`」的那一小片地方（裁决 7.26③ 的口径：
// 框架类型不进公共头）。本件**不**读写 `borealis.json`，也**不**参与 schema 自校验与降级备份
// ——那份文件里的每一个键都由框架的几何链自己写（`save_window_geometry`），本仓只决定它落在
// 哪一层目录、什么时候提交（稿 §0 的 F26、§6 批 5）。
// ============================================================

#include "window_geometry_store.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace borealis::config {

auto window_persist_id(std::size_t creation_index) -> std::string {
    if (creation_index == 0U) {
        return "main";
    }
    // 序号从 2 起：第一扇窗的名字是 `main`，`window-1` 会与它并存成「同一扇窗两个键」。
    return "window-" + std::to_string(creation_index + 1U);
}

WindowGeometryStore::WindowGeometryStore()
    // 与 `borealis.json` 同一层（裁决 7.26③ 的落点：配置目录不含应用子目录）。
    : WindowGeometryStore(au::preferences::Preferences::default_config_dir() / kWindowGeometryFileName) {}

WindowGeometryStore::WindowGeometryStore(std::filesystem::path file)
    : prefs_(std::in_place, std::move(file)) {}

auto WindowGeometryStore::preferences() noexcept -> au::preferences::Preferences * {
    return &*prefs_;
}

auto WindowGeometryStore::path() const noexcept -> const std::filesystem::path & {
    return prefs_->file_path();
}

auto WindowGeometryStore::flush() -> std::optional<std::string> {
    if (const auto flushed = prefs_->flush(); !flushed.ok()) {
        return flushed.error().message;
    }
    return std::nullopt;
}

}  // namespace borealis::config
