#pragma once

// ============================================================
// URL 识别与打开决策（include/borealis/ui/url.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTERACT.05` 里与绘制、平台都无关的那半（设计稿 `codespec/UI_URL.draft.md` §4 批 1）：
// 一格折算成「这一格在不在一条可点链接上、整条的列游程、目标串、来源」，一条目标串折算成
// 「直接开 / 先问 / 拒绝」，以及「什么算同域」的唯一定义处。
//
// 不含 Aurora 也不含 `config` 与 `session` 类型（同 `right_click.h` 的模块环口径，架构 §2.3）：
// OSC 8 的目标由调用方从 `Session::hyperlink_target` 解出后交进来，本件因此能脱开界面与会话
// 全量单测（AGENTS §4.4 第 20 条），而「标识已被淘汰」那一档就表示成 `std::nullopt`。
//
// 三条口径的由来（细则见设计稿 §3，入册为裁决 7.119）：
// - **协议白名单在识别处把关，且两来源同闸**（稿 D3①＋判据 5）：白名单外的串**根本不产
//   `LinkHit`**——不画线、不进菜单、Ctrl+点击无效，于是「可点」在本产品里只意味着「能打开」。
//   OSC 8 的目标是会话字节流给的，同样过闸：否则 `javascript:` 一族就绕过了本需求唯一的安全腿。
// - **OSC 8 优先，只有「解不出目标」才落回启发式**（稿 D1①）：有标识且解得出目标即用之，不再猜
//   屏上文字；标识已被淘汰（`kMaxHyperlinks` 满表，裁决 7.21②）才按形状重扫。但**标识解得出目标
//   而目标不在白名单时不落回启发式**——那是「不可点」，不是「换一种方式猜」，否则就把稿 F1 那条
//   安全含义（屏上文字与真实目标可以不一致）从缺陷做成功能。
// - **解析不出 host[:port] 的串一律拒绝，含 `Never` 档**（稿 D9① 的 fail-closed 那一半）：确认框的
//   「记住该域并打开」在这种串上无从落笔，而一档「永远在问」的策略又与免问那档自相矛盾。
//
// 目标串一律 UTF-8（裁决 7.11 那句「公共头一律 UTF-8」）：OSC 8 侧的 `std::u32string` 在
// `find_link_at` 内编一次，此后提示条文案、确认框正文与落盘的域名单同持一份编码，不在三处各转一次。
//
// 本件**不认识** `terminal.word_delimiters`（稿 D3① 的决定性理由）：那枚键用户可改，一旦借它划
// URL 边界，「改断词符」就静默改掉链接边界。故字符表自带，产物也刻意与双击选词**不一致**。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/grid/row.h"
#include "borealis/ui/selection.h"

namespace borealis::ui {

/// @brief 一条链接的来源（稿 D1① 的优先级就是本枚举的两档）。
enum class LinkSource : std::uint8_t {
    Osc8,      ///< OSC 8 显式超链接：程序声明的意图。
    Heuristic, ///< 纯文本启发式识别：按形状猜，确认框必须把这层「猜」显示出来。
};

/// @brief 打开前的确认策略三档（配置键 `terminal.link_osc8` 与 `terminal.link_heuristic` 的取值）。
///
/// **本件是该枚举的唯一定义处**，`config::LinkConfirmPolicy` 是其别名（与 `term::PasteNewlinePolicy`、
/// `ui::RightClickAction` 同一条单一真值源迁移先例），免得配置侧与界面侧各持一份取值表在枚举值上分叉。
enum class LinkConfirmPolicy : std::uint8_t {
    Never,      ///< 直接打开，不问。
    AskOnce,    ///< 首次确认并记住同域（缺省）。
    AskAlways,  ///< 每次都问；既不读名单也不写名单。
};

/// @brief 链接扫描的入参。
struct LinkScanOptions {
    /// @brief 协议白名单：小写、不含 `://`，由 `parse_link_schemes` 从配置串拆出。
    ///
    /// **缺省为空即「一切不可点」**，而不是「未配置故放行」：装载侧的唯一真值源是
    /// `terminal.link_schemes` 的缺省串 `"http,https"`，本件不再复制一份缺省值，免得两处取值表分叉
    /// （稿 D8① 登记的那条代价：该键被清空表现为「什么都不可以点」，由设置页旁注解释）。
    std::vector<std::string> schemes;

    /// @brief 逐字段全等比较。
    [[nodiscard]] auto operator==(const LinkScanOptions &) const noexcept -> bool = default;
};

/// @brief 一次命中的链接：整条的列游程、目标全文与来源。
struct LinkHit {
    RowSpan span{};  ///< 列区间（闭开）；行号沿用调用方给定的存储行序（`ui::RowSpan` 同口径）。
    std::string target;  ///< 目标全文（UTF-8）。**屏上文字与它可以不一致**：提示条与确认框显示的恒是它。
    LinkSource source{LinkSource::Heuristic};

    /// @brief 逐字段全等比较（用例与界面侧「命中变了吗」的判据）。
    [[nodiscard]] auto operator==(const LinkHit &) const noexcept -> bool = default;
};

/// @brief 把配置里那枚逗号分隔的协议白名单拆成表。
///
/// 逗号分隔、逐项去首尾 ASCII 空白、ASCII 字母转小写、空项丢弃；**不做合法性校验**——非法项只是
/// 永不匹配，面板不拒收（与 `word_delimiters` 的「原样收字符」同口径，稿 D8① 的代价那条）。
/// @param csv 配置串原文。
/// @return 拆分后的表；全空串给空表，即「一切不可点」。
[[nodiscard]] auto parse_link_schemes(std::string_view csv) -> std::vector<std::string>;

/// @brief 折出指针落点所在的那条链接。
///
/// 惰性一行扫（稿 D4①）：三处入口（悬停 Move / Ctrl+左击 / 右键）各自现扫现判，判定不缓存。
/// @param row 落点所在行（权威网格或可见区副本的行，两者内容一致；副本整行拷贝带着侧表）。
/// @param point 落点坐标；`column` 可以指在双宽字符的左半格，本件按基础格折算。
/// @param options 协议白名单。
/// @param osc8_target 调用方从 `Session::hyperlink_target` 解出的目标：`std::nullopt` 即「这一号的
///                    标识已被淘汰或压根没有链接」，此时按启发式重扫（稿 D1① 的落回腿）。
/// @return 命中的链接；落点不在可点内容上、白名单外、或行越界时 `std::nullopt`。
[[nodiscard]] auto find_link_at(const grid::Row &row, GridCellPos point, const LinkScanOptions &options,
                                const std::optional<std::u32string> &osc8_target)
    -> std::optional<LinkHit>;

/// @brief 「已记住的同域」名单（稿 D9①）：纯内存，不含 IO。
///
/// 落盘归装配层——`on_change` 交出去的整表快照写进 `connection.trusted_url_hosts`（一枚
/// `kNonPreferenceLeaves` 例外通道上的数据叶子，设置面板不画它）。本件不写文件也不读配置。
class TrustedLinkHosts final {
  public:
    /// @brief 名单变化时的整表快照回调；新增以外的动作（`load`、重复 `add`）不触发。
    using ChangeSink = std::function<void(std::span<const std::string>)>;

    /// @brief 该域是否已被记住。
    /// @param host `link_host_of` 形态的归一域（比较按字面串，不再归一）。
    [[nodiscard]] auto contains(std::string_view host) const -> bool;

    /// @brief 记住一个域（幂等）。
    /// @param host 归一域；空串不收（「记住」一个解析不出来的域正是 fail-closed 要挡的那件事）。
    /// @return 本次是否为新增；已存在时 `false` 且不回调。
    [[nodiscard]] auto add(std::string_view host) -> bool;

    /// @brief 装配层启动时灌入既有名单：不触发回调（灌入不是变化）。
    /// @param hosts 落盘读回来的域表；本件按字典序重排，故快照次序与读盘次序无关。
    auto load(std::vector<std::string> hosts) -> void;

    /// @brief 装填变化回调。
    /// @param sink 空 `std::function` 即摘除（测试替身用它数调用次数）。
    auto set_change_sink(ChangeSink sink) -> void;

    /// @brief 当前名单快照（按字典序升序）。
    [[nodiscard]] auto hosts() const noexcept -> std::span<const std::string>;

  private:
    auto notify() -> void;

    std::vector<std::string> hosts_;  ///< 升序：`add` 走有序插入，`load` 整表排序。
    ChangeSink on_change_;
};

/// @brief 打开一条链接的处置意图。
enum class LinkOpenIntent : std::uint8_t {
    Open,    ///< 直接打开。
    Confirm, ///< 先问；`LinkOpenPlan::host` 就是「记住该域并打开」该记的那一枚。
    Refuse,  ///< 既不打开也不问。
};

/// @brief 处置计划的成因（`Open` 与 `Confirm` 时恒为 `None`）。
enum class LinkOpenReason : std::uint8_t {
    None,           ///< 未触发任何闸。
    HostUnparsable, ///< 解析不出 host[:port]：既无从记也无从比，fail-closed。
};

/// @brief 一次打开请求的处置计划。
struct LinkOpenPlan {
    LinkOpenIntent intent{LinkOpenIntent::Refuse};
    std::string host;  ///< 归一后的域；解析得出就填（`Open` 时也在，供日志与走查核对），解析不出即空。
    LinkOpenReason reason{LinkOpenReason::None};

    /// @brief 逐字段全等比较（计划断言用）。
    [[nodiscard]] auto operator==(const LinkOpenPlan &) const noexcept -> bool = default;
};

/// @brief 把「来源 + 两档策略 + 已记住名单 + 目标串」折算成打开处置计划。
///
/// 纯函数：两来源 × 三档策略 × 名单内/外的全部组合都能脱开界面逐条断言。**白名单不在本调用的
/// 入参里**——入参目标须出自 `find_link_at`，那道闸已在识别处把关（稿判据 5 的形态：白名单外的串
/// 连线都没有，走不到「要不要问」这一步）。
/// @param source 命中的来源；决定取哪一枚策略键（稿 D8① 的「两档分开配置」）。
/// @param osc8_policy `terminal.link_osc8` 的当前取值。
/// @param heuristic_policy `terminal.link_heuristic` 的当前取值。
/// @param trusted 已记住的同域名单；`AskOnce` 档读它，`AskAlways` 与 `Never` 都不读。
/// @param target 目标全文（UTF-8）。
/// @return 处置计划；`Refuse` 时 `reason == HostUnparsable` 且 `host` 为空。
[[nodiscard]] auto plan_link_open(LinkSource source, LinkConfirmPolicy osc8_policy,
                                  LinkConfirmPolicy heuristic_policy, const TrustedLinkHosts &trusted,
                                  std::string_view target) -> LinkOpenPlan;

/// @brief 取目标的「同域」标识：归一成 `scheme://host[:port]`。
///
/// 归一三件事（稿 D9①，本函数是「什么算同域」的唯一定义处）：① scheme 与 host 的 ASCII 字母转小写；
/// ② 端口等于该 scheme 的缺省端口时折叠掉，于是 `http://x` 与 `http://x:80` 记成同一枚；③ userinfo
/// （`user:pw@`）剔除，方括号 IPv6 保留括号。**不做 punycode / IDN 归一**（本沙箱无 IDN 测试面，
/// 标推断／未验证），非 ASCII host 按字面码点记。
/// @param target 目标全文（UTF-8）。
/// @return 归一域；缺 `://`、scheme 或 host 为空、端口非全数字时 `std::nullopt`。
[[nodiscard]] auto link_host_of(std::string_view target) -> std::optional<std::string>;

}  // namespace borealis::ui
