#pragma once

// ============================================================
// 「每扇窗的几何」那份存储（src/config/window_geometry_store.h，私有头）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.03` 的 D9 那一件（稿 §0 的 F26、§6 批 5）：窗口布局分别记忆取框架的
// `WindowOptions::persist_id` + `Application::set_window_geometry_store()`，本仓只补三件事
// ——键怎么分配、那份 `Preferences` 落在哪、什么时候提交。
//
// 口径四条：
// - **不进 `config::Settings` schema**：几何是本机屏幕相关值，跨机导出即坏值，所以它既不在
//   `borealis.json` 里也不在 `SPEC.FEAT.PREF.07` 的导出件里（这条是特性不是漏项，稿 D9）。
// - **键在 `Preferences` 顶层**：框架那条自动链调的是 `load_window_geometry(Preferences&, key)`
//   那一枚重载，键即 `persist_id` 本身；`group("windows")` 只服务于调用方**自己**直接调 save/load
//   的形态。写进分组＝存得回而恢复取不到，故本件不提供分组入口，也不许在外面另建一份键表。
// - **落盘时机归调用方**：框架无隐式 I/O，也没有「自上次 flush 后是否变过」的查询，故两处
//   flush（任一窗关闭后、退出前）是无条件提交。
// - **接缝仍在本域**：`Preferences` 只从 config 侧触达（裁决 7.26③、架构 §2.2），装配层拿到的
//   是一枚现成的指针，不为此把框架类型抬进公共头。
//
// 线程：只在主线程用（几何的保存发生在 `Application` 帧末回收，恢复发生在宿主登记与接线那两处）。
// ============================================================

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "aurora/preferences/preferences.h"

namespace au = aurora;

namespace borealis::config {

/// @brief 几何文件名：与 `borealis.json` 同一层（`Preferences::default_config_dir()`），刻意另起一枚。
inline constexpr const char *kWindowGeometryFileName = "windows.json";

/// @brief 按创建序分配一扇窗的几何键（稿 D9 的「键＝`main`（主窗）与 `window-2`、`window-3`…」）。
///
/// 真值源是**创建序**而不是窗号：`WindowId` 由框架在宿主登记时发（0 留作无效值），拆窗后不回收、
/// 也不保证重启之间稳定，拿它当键会让同一扇窗每次启动换一个名字。
/// @param creation_index 本窗在装配层那容器里的下标，0＝主窗。
/// @return 0 → `main`；其余 → `window-` 加**一位起的序号再 +1**（第二扇窗是 `window-2`）。
[[nodiscard]] auto window_persist_id(std::size_t creation_index) -> std::string;

/// @brief 逐窗几何的那份 `Preferences`：绑定 `<配置目录>/windows.json`，只提供取存储与提交两跳。
///
/// 禁复制禁搬移——`Preferences` 内含互斥量，搬走它等于搬走一把正在被框架读的锁，而
/// `Application` 持的是本件内部那份存储的裸指针。
class WindowGeometryStore {
  public:
    /// @brief 按默认位置绑定（`Preferences::default_config_dir()` / `windows.json`）。
    WindowGeometryStore();

    /// @brief 按显式文件绑定：测试用，装配层不走这一跳。
    /// @param file 几何文件的完整路径；父目录不存在时由框架在落盘时创建。
    explicit WindowGeometryStore(std::filesystem::path file);

    WindowGeometryStore(const WindowGeometryStore &other) = delete;
    auto operator=(const WindowGeometryStore &other) -> WindowGeometryStore & = delete;
    WindowGeometryStore(WindowGeometryStore &&other) = delete;
    auto operator=(WindowGeometryStore &&other) -> WindowGeometryStore & = delete;
    ~WindowGeometryStore() = default;

    /// @brief 交 `Application::set_window_geometry_store()` 的那枚指针。
    /// @return 恒非空：本件两处构造都是文件模式。生存期由本对象保证，故调用方须让它活得比
    ///         `Application` 久（框架只在保存与恢复两跳读它，都在帧循环内）。
    [[nodiscard]] auto preferences() noexcept -> au::preferences::Preferences *;

    /// @brief 当前绑定的文件路径（诊断与用例读它，装配层不据此决定行为）。
    [[nodiscard]] auto path() const noexcept -> const std::filesystem::path &;

    /// @brief 把内存里的几何提交到文件（框架自带 advisory 锁 + 临时文件 + 原子 rename）。
    /// @return 成功为空；失败为 ASCII 英文原因，由调用方落日志——本件不碰 stdio（AGENTS.md §4.3 第 15 条）。
    [[nodiscard]] auto flush() -> std::optional<std::string>;

  private:
    /// `Preferences` 不可搬移，故就地构造在 `optional` 里（与 `config::Store` 同一形态）。
    std::optional<au::preferences::Preferences> prefs_;
};

}  // namespace borealis::config
