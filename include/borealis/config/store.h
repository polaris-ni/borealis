#pragma once

// ============================================================
// 配置的装载、校验与落盘（include/borealis/config/store.h）
// ------------------------------------------------------------
// `SPEC.FEAT.PREF.03` 的落盘经 Aurora `Preferences`：单文件 + 按域分组嵌套 + 顶层
// `schema_version`（裁决 7.26③）。**原子写、临时文件与跨进程 advisory 锁都由框架
// `flush()` 承担，本仓不重造**（架构 §11.3）；本层只做框架不做的三件事：
//
//   1. schema 自校验（附录 A.2 G7「应用侧自校验」）：缺键 / 类型不符 / 域外一律回落默认并留痕，
//      schema 之外的键只记录不报错——旧版本留下的键不该让新版本拒启动。
//   2. `SPEC.FEAT.PREF.07` 的损坏降级线（裁决 7.26④ 提前到首版）：JSON 解析失败或
//      `schema_version` 高于本仓支持 → **先备份 `*.corrupt-<epoch 秒>` 再回落默认**，
//      结果以 `LoadReport` 交 UI 侧。备份未成功就落盘会覆盖掉唯一现场，故那种情况下
//      `replace()` 拒绝写文件（内存值照常更新，原因返回给调用方）。
//   3. `SPEC.FEAT.PREF.07` 的快照回滚与本地导出导入（裁决 7.87）：快照取在**写之前**，
//      于是「回滚到某一份」与「回滚本身也不毁现场」由同一条算式一并给出，不必为回滚另设
//      应急备份。导出/导入只搬 schema 内的各域，凭据按裁决 7.26⑥ 结构上就不在其中，
//      文件仍逐键名过一遍禁列名单（外部文件是不可信输入）。
//
// 落盘时机是「用户第一次变更」（裁决 7.26⑤）：装载不产生任何写入，首次启动无配置文件
// 就是全量默认值在内存里。
//
// 本头刻意不含 Aurora 类型（`Preferences` 在 PIMPL 内），故校验报告用值语义的字符串与路径表达。
// ============================================================

#include <cstddef>
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

/// @brief 本仓支持的最高 schema 版本（裁决 7.26③）：高于它的文件按损坏降级。
///
/// 上公共头是因为「本程序支持哪一档」这条事实要出现在界面上（降级对话框的文案要报出两个版本号），
/// 而实现文件里的私有常量不可达；写在 `LoadReport` 旁边而不是让 UI 自己抄一份，是为了不让
/// 「支持的版本」出现第二个真值源。
inline constexpr std::int64_t kSupportedSchemaVersion = 1;

/// @brief 装载结论（`SPEC.FEAT.PREF.07` 的降级分支之一）。
enum class LoadOutcome : std::uint8_t {
    FirstRun,         ///< 没有配置文件：取 `Settings{}` 全量默认值，且不写盘。
    Loaded,           ///< 正常装载（`rejected_keys` 非空表示部分键被回落）。
    RecoveredCorrupt, ///< 文件不是可读 JSON：已备份损坏文件并回落默认值。
    RecoveredVersion, ///< `schema_version` 高于本仓支持：已备份损坏文件并回落默认值。
};

/// @brief 一次装载的可观察结论，交 UI 侧作显著提示。
///
/// 消费方是启动路径上的降级对话框（`src/ui/startup_notice.cpp`，裁决 7.76⑤）：`outcome` 决定弹不弹与
/// 取哪一条中文词条，`corrupt_backup` / `stored_schema_version` / `rejected_keys` 是它的结构化内容，
/// 而 `message` **只进日志**——它是 ASCII 英文诊断（AGENTS.md §4.3 第 14 条的诊断文案不属中文例外），
/// 显示在中文界面上即违那条规则（判据文 S13①）。
struct LoadReport {
    LoadOutcome outcome{LoadOutcome::FirstRun};
    std::filesystem::path file;  ///< 配置文件路径。
    std::optional<std::filesystem::path> corrupt_backup;  ///< 损坏文件的备份路径；未发生降级时为空。
    std::optional<std::int64_t> stored_schema_version;  ///< 文件自报的 schema 版本；只在版本过高那一路有值。
    bool writes_refused{false};  ///< 备份未成功：本会话拒绝再写这个文件，唯一现场原样保留。
    std::string message;  ///< 降级的补充说明；正常装载为空。
    std::vector<std::string> rejected_keys;  ///< 回落默认的键（点号路径）。
    std::vector<std::string> unknown_keys;   ///< schema 之外的键：只记录，不影响装载。
};

/// @brief 滚动保留的快照份数（`SPEC.FEAT.PREF.07` 的那个 N，人已拍板取 5）。
inline constexpr std::size_t kSnapshotRetention = 5;

/// @brief 一份配置快照的可见信息（面板的快照列表按 newest-first 画这一列）。
struct SnapshotInfo {
    std::filesystem::path path;  ///< 快照文件完整路径。
    std::int64_t timestamp_epoch{};  ///< 命名里的 epoch 秒（同一秒内的多份按序号递增）。
    std::uint64_t size_bytes{};  ///< 文件字节数；读不到时为 0。
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

    /// @brief 现有快照，按**新→旧**排列（`SPEC.FEAT.PREF.07` 的「保留最近 N 份」）。
    ///
    /// 每次成功落盘都会先把当时的配置文件复制成一份快照，故列表最长 `kSnapshotRetention` 项，
    /// 且第一项通常与当前文件同内容——回滚点因此总在「上一次变更之前」那一格上。
    /// 目录里不属于本仓命名形态的文件一律不出现在这里。
    [[nodiscard]] auto snapshots() const -> std::vector<SnapshotInfo>;

    /// @brief 回滚到一份快照：把它装成生效配置并落盘（面板的「一键回滚」）。
    ///
    /// 只接受**本目录内、按快照命名形态存在**的文件；其余路径一律拒绝，因为这条入口拿到的
    /// 是 UI 交回的字符串，让它指向目录外任一文件就是把「回滚」变成「覆盖任意文件」。
    /// 落盘走 `replace()` 同一条腿，于是当前现场先被自动拍成一份快照（裁决 7.87②）。
    /// @param snapshot `snapshots()` 交回的路径。
    /// @return 成功为空；失败为 ASCII 英文原因，供诊断与 UI 提示。
    [[nodiscard]] auto rollback_to(const std::filesystem::path &snapshot) -> std::optional<std::string>;

    /// @brief 导出当前设置为一个可换机迁移的 JSON 文件（`SPEC.FEAT.PREF.07`）。
    ///
    /// 文件形态与配置文件逐字同构（顶层 `schema_version` + 各域），故导出的文件也能被
    /// `import_settings()` 原样装回，不必另立一套 envelope。写入是「临时文件 + rename」，
    /// 与框架 `flush()` 同一条纪律，只是目标由用户点名。
    /// @param file 目标路径。
    /// @return 成功为空；失败为 ASCII 英文原因。
    [[nodiscard]] auto export_settings(const std::filesystem::path &file) const -> std::optional<std::string>;

    /// @brief 从一份导出的 JSON 装载并落盘（`SPEC.FEAT.PREF.07` 的导入腿）。
    ///
    /// 外部文件按不可信输入处理：不是合法 JSON、顶层不是对象、没有各域中任一域、
    /// `schema_version` 缺失或高于本仓支持、任一层键名撞禁列名单，都**整体拒绝**而不回落默认——
    /// 装载侧那套「逐键回落 + 留痕」的宽容是给自家损坏文件的，给外部文件就会把一份错文件
    /// 读成「一堆默认值」并覆盖掉用户现场。
    /// @param file 来源路径。
    /// @return 成功为空；失败为 ASCII 英文原因，内存值与配置文件一字未动。
    [[nodiscard]] auto import_settings(const std::filesystem::path &file) -> std::optional<std::string>;

  private:
    struct Impl;  ///< 持有 Aurora `Preferences` 与装载产物；类型藏在实现内。

    std::unique_ptr<Impl> impl_;
};

}  // namespace borealis::config
