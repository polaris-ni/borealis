#pragma once

// ============================================================
// 配置的装载、校验与落盘（include/borealis/config/store.h）
// ------------------------------------------------------------
// `SPEC.FEAT.PREF.03` 的落盘经 Aurora `Preferences`：单文件 + 按域分组嵌套 + 顶层
// `schema_version`（裁决 7.26③）。**原子写、临时文件与跨进程 advisory 锁都由框架
// `flush()` 承担，本仓不重造**（架构 §11.3）；本层只做框架不做的两件事：
//
//   1. schema 自校验（附录 A.2 G7「应用侧自校验」）：缺键 / 类型不符 / 域外一律回落默认并留痕，
//      schema 之外的键只记录不报错——旧版本留下的键不该让新版本拒启动。
//   2. `SPEC.FEAT.PREF.07` 的损坏降级线（裁决 7.26④ 提前到首版）：JSON 解析失败或
//      `schema_version` 高于本仓支持 → **先备份 `*.corrupt-<epoch 秒>` 再回落默认**，
//      结果以 `LoadReport` 交 UI 侧。备份未成功就落盘会覆盖掉唯一现场，故那种情况下
//      `replace()` 拒绝写文件（内存值照常更新，原因返回给调用方）。
//
// 落盘时机是「用户第一次变更」（裁决 7.26⑤）：装载不产生任何写入，首次启动无配置文件
// 就是全量默认值在内存里。
//
// 本头刻意不含 Aurora 类型（`Preferences` 在 PIMPL 内），故校验报告用值语义的字符串与路径表达。
// ============================================================

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "borealis/config/settings.h"

namespace borealis::config {

/// @brief 配置文件名：落在 Aurora `Preferences::default_config_dir()` 下（裁决 7.26③）。
inline constexpr const char *kConfigFileName = "borealis.json";

/// @brief 装载结论（`SPEC.FEAT.PREF.07` 的降级分支之一）。
enum class LoadOutcome : std::uint8_t {
    FirstRun,         ///< 没有配置文件：取 `Settings{}` 全量默认值，且不写盘。
    Loaded,           ///< 正常装载（`rejected_keys` 非空表示部分键被回落）。
    RecoveredCorrupt, ///< 文件不是可读 JSON：已备份损坏文件并回落默认值。
    RecoveredVersion, ///< `schema_version` 高于本仓支持：已备份损坏文件并回落默认值。
};

/// @brief 一次装载的可观察结论，交 UI 侧作显著提示。
///
/// TODO(SPEC.FEAT.PREF.07): 「绝不静默清空」要求的对话框尚未落地（设置面板随
/// `SPEC.FEAT.PREF.02` 前置后接入），届时按 `outcome` 分支取 `message` 与 `corrupt_backup`
/// 组织文案，并按 `rejected_keys` 列表提示被回落的键。
struct LoadReport {
    LoadOutcome outcome{LoadOutcome::FirstRun};
    std::filesystem::path file;  ///< 配置文件路径。
    std::optional<std::filesystem::path> corrupt_backup;  ///< 损坏文件的备份路径；未发生降级时为空。
    std::string message;  ///< 降级的补充说明；正常装载为空。
    std::vector<std::string> rejected_keys;  ///< 回落默认的键（点号路径）。
    std::vector<std::string> unknown_keys;   ///< schema 之外的键：只记录，不影响装载。
};

/// @brief 配置仓库：装载即得一份 `Settings`，`replace()` 提交并落盘。
class Store {
  public:
    /// @brief 按默认位置装载（`Preferences::default_config_dir()` / `borealis.json`）。
    Store();

    /// @brief 按显式路径装载：测试与「便携模式」用。
    /// @param file 配置文件完整路径；父目录不存在时由框架在落盘时创建。
    explicit Store(std::filesystem::path file);

    Store(const Store &other) = delete;
    auto operator=(const Store &other) -> Store & = delete;
    Store(Store &&other) noexcept;
    auto operator=(Store &&other) noexcept -> Store &;
    ~Store() noexcept;

    /// @brief 当前生效配置。
    [[nodiscard]] auto settings() const noexcept -> const Settings &;

    /// @brief 装载结论（含降级与校验留痕）。
    [[nodiscard]] auto report() const noexcept -> const LoadReport &;

    /// @brief 整体替换生效配置并提交落盘（裁决 7.26⑤ 的首次变更即写盘）。
    ///
    /// 内存值总是更新；只有落盘可能失败，失败原因以 ASCII 英文文本返回，供诊断与 UI 提示。
    /// @param next 新的完整配置。
    /// @return 成功为空；失败为原因。
    [[nodiscard]] auto replace(const Settings &next) -> std::optional<std::string>;

  private:
    struct Impl;  ///< 持有 Aurora `Preferences` 与装载产物；类型藏在实现内。

    std::unique_ptr<Impl> impl_;
};

}  // namespace borealis::config
