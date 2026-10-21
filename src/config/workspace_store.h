#pragma once

// ============================================================
// 「上次那套标签布局」那份存储（src/config/workspace_store.h，私有头）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.11` 的第 3 件（稿 §4 第 3 项、D1①）：配置目录里的**第三枚** `Preferences`，
// 与 `borealis.json`（设置域）、`windows.json`（逐窗几何）并排而各管各的键空间。本件依旧只做
// 框架不做的两件事——落在哪个文件、什么时候提交，原子写、临时文件与跨进程 advisory 锁全归
// 框架 `flush()`（架构 §11.3 的「本仓不重造」）。
//
// 三条决定形态的口径，理由见稿 §3：
// - **只有两跳**：取存储 + 提交。本件**没有任何删除入口**（既无 `remove` 也无「清空布局」），
//   判据 ③ 那句「开关关掉之前已有的快照不被删除」因此是结构上的事实而不是某条用例的断言；
//   清这份文件的账归用户（D9① 的在册代价：超界那份等于废掉，界面上没有清理入口）。
// - **键只有顶层这一枚**：`kWorkspaceLayoutKey`，值是一棵 `json::Value`（框架对整值 `Value`
//   直存直取，不做转换），于是快照文档里那份 `schema_version` 留在**值内部**，与 `borealis.json`
//   的顶层 schema 各走各的版本档（稿 D1① 的「刻意分家」）。不自造第二份键表——`windows.json`
//   那条「不许在外面另建一份键表」的口径同样适用于本件。
// - **本件不懂布局的语义**：模型的编解码归 `ui::WorkspaceSnapshot`（批 2），坏文档整份拒绝也归
//   它；本件既不调 `from_json` 也不写降级备份（稿 D8①：布局静默消失、只留日志，不弹框、不备份）。
//
// 声明次序：本对象须活得比 `Application` 久且**早于** `app` 构造——与 `WindowGeometryStore` 同一
// 条注释继承（稿 F3 那条），因为装配层把 `preferences()` 那枚裸指针交给框架。
//
// 线程：只在主线程用（拓扑变更的采集点在用户动作上，稿 D2①）。
// ============================================================

#include <filesystem>
#include <optional>

#include "aurora/preferences/preferences.h"

namespace au = aurora;

namespace borealis::config {

/// @brief 快照文件名：与 `borealis.json` / `windows.json` 同一层，刻意另起一枚（稿 D1①）。
inline constexpr const char *kWorkspaceFileName = "workspace.json";

/// @brief 那份布局在 `Preferences` 里的**唯一**顶层键。
///
/// 叫 `layout` 而不是 `workspace`：文件名已经说过「这是工作区」，键再说一遍只会让日志与手改文件
/// 的人面对 `workspace.workspace` 这种重复形态。
inline constexpr const char *kWorkspaceLayoutKey = "layout";

/// @brief 会话恢复快照的那份 `Preferences`：绑定 `<配置目录>/workspace.json`，只提供取存储与提交。
///
/// 禁复制禁搬移——`Preferences` 内含互斥量，而装配层交出的是本件内部那份存储的裸指针。
class WorkspaceStore {
  public:
    /// @brief 按默认位置绑定（`Preferences::default_config_dir()` / `workspace.json`）。
    WorkspaceStore();

    /// @brief 按显式文件绑定：测试用，装配层不走这一跳。
    /// @param file 快照文件的完整路径；父目录不存在时由框架在落盘时创建。
    explicit WorkspaceStore(std::filesystem::path file);

    WorkspaceStore(const WorkspaceStore &other) = delete;
    auto operator=(const WorkspaceStore &other) -> WorkspaceStore & = delete;
    WorkspaceStore(WorkspaceStore &&other) = delete;
    auto operator=(WorkspaceStore &&other) -> WorkspaceStore & = delete;
    ~WorkspaceStore() = default;

    /// @brief 读写那一份布局用的存储：调用方以 @p kWorkspaceLayoutKey 为唯一键取与放。
    /// @return 恒非空：两处构造都是文件模式。生存期由本对象保证，故调用方须让它活得比使用者久。
    [[nodiscard]] auto preferences() noexcept -> au::preferences::Preferences *;

    /// @brief 当前绑定的文件路径（诊断与用例读它，装配层不据此决定行为）。
    [[nodiscard]] auto path() const noexcept -> const std::filesystem::path &;

    /// @brief 把内存里的布局提交到文件（框架自带 advisory 锁 + 临时文件 + 原子 rename）。
    /// @return 成功为空；失败为 ASCII 英文原因，由调用方落日志——本件不碰 stdio（第 15 条）。
    [[nodiscard]] auto flush() -> std::optional<std::string>;

  private:
    /// `Preferences` 不可搬移，故就地构造在 `optional` 里（与 `WindowGeometryStore` 同一形态）。
    std::optional<au::preferences::Preferences> prefs_;
};

}  // namespace borealis::config
