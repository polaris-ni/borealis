/// 测试类型: integration
/// 目标单元: src/ui/{connection_sidebar,connection_wizard,credential_prompt,keys_panel,sftp_panel,
///                    session_monitor_panel,tunnel_panel,settings_panel,debug_panel,startup_notice}.cpp
///           的搬窗接缝
///           （`SPEC.FEAT.WS.03` 稿 D6「面板搬窗而非各一份」＋稿 §0 的 F3「焦点权威按窗取」）
/// 测试说明: `src/main.cpp` 不进测试运行器（裁决 7.83），装配层那条「散掉的窗把浮层搬到落点」的腿因此
///           没有自动化证人——本文件判的是它所依赖的**十件接缝**，也就是搬窗这件事唯一可能被写错的四处
///           算子：摘自谁、挂给谁、换完之后本件处于什么态、焦点作用域归谁。
///           ① **八只瞬态面板同一条判据**：开在 A 上 → `set_overlay_host(B)` ⇒ A 的浮层回到基线（摘的是
///              **旧**宿主上自己记的那枚序号，不是「当前宿主」的）、B 一层也不增（换完即关闭态，不存在
///              「两份面板」）、`is_open()` 为假、`overlay_host()` 易主；随后在新宿主重开一次，那一层落在
///              B 而 A 恒空——「各一份」与「留在散掉的窗上」两种错法都被这两行判掉。
///           ② **同一宿主重复设是空操作**：装配层的打开腿每次命令都会调这一支，若它把「宿主没变」也当成
///              一次搬迁，则每次开合都会白拆一次浮层（用户看得见的是面板闪一下）。
///           ③ **两只常驻框另走 `set_overlay_destination`**：浮层只建一次、开合只切显隐，故搬窗不能靠
///              `close()` 去摘——`DebugPanel` 摘完重挂到新宿主且**是关闭态**（作用域记账带着按旧栈深度取的
///              下标，跨栈比较会误判），`StartupNotice` 摘完重挂且**仍是开着**（一启动只弹一次的提示不能因
///              搬窗而消失），两者的作用域都恰好收在旧栈、新栈一层不压/压回一层。
///           ④ **散窗兜底腿 `abandon_overlay_destination` 不碰旧宿主与旧焦点栈**：框架在 `on_window_closed`
///              之前回收窗口宿主（aurora 主仓 `codespec/specification/06-app-platform.md` §3「宿主被回收
///              **之后**触发一次」），旧焦点管理器此刻已经不存在——③④ 两条腿的分工因此不是风格而是销毁序。
///              证人形态是**可观测的差别**：走错那条腿会留下「旧宿主仍挂着 / 新宿主也被挂上」与「旧栈被
///              少弹一层」两种读数，本文件逐条判出。旧宿主这里用 `unique_ptr` 真销毁一次，此后任何一次
///              `remove_overlay` 都是往已释放对象上落手。
///           ⑤ 搬窗后 `Dialog` 句柄必须一起放：`toggle()` / `show_if_needed()` 认句柄而不认宿主，留着
///              旧句柄就得到「切成了可见、却谁也没把它挂回去」那种永不再现的面板。
///
///           与 `itest_debug_panel` / `itest_startup_notice` 同一条宿主构造事实：`overlay_count()` 按
///           `children_.size() - 1` 回（**基础内容不计入**），故每只宿主都带一个基础子节点，「挂了一层」
///           的读数是 1。中文一律经词条表在运行期交出，本文件不写中文字面量（§4.3 第 14 条）。
///           第八只（`session_monitor_panel`，`SPEC.FEAT.CONN.05` 稿 D6 那条「搬窗不停采集」）判据与其余
///           七只逐字相同，只多一层含义：留痕活在那条会话的 `Session` 里，本件是它的一份投影，故摘挂
///           浮层这一跳既不动环也不清屏——它走的仍是 `set_overlay_host` 这一支，而不是两只常驻框那条
///           `set_overlay_destination`（本件是瞬态浮层：关闭即摘，重开即挂）。

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "aurora/app/shortcuts.h"
#include "aurora/event/focus.h"
#include "aurora/render/painter.h"
#include "aurora/widget/popup.h"
#include "aurora/widget/text.h"
#include "borealis/config/store.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/connection_sidebar.h"
#include "../../src/ui/connection_wizard.h"
#include "../../src/ui/credential_prompt.h"
#include "../../src/ui/debug_panel.h"
#include "../../src/ui/keys_panel.h"
#include "../../src/ui/session_monitor_panel.h"
#include "../../src/ui/settings_i18n.h"
#include "../../src/ui/settings_panel.h"
#include "../../src/ui/sftp_panel.h"
#include "../../src/ui/startup_notice.h"
#include "../../src/ui/tunnel_panel.h"

namespace borealis::test_cases::itest_panel_retarget {

namespace au = aurora;

using borealis::conn::SecretAsk;
using borealis::config::LoadOutcome;
using borealis::config::LoadReport;
using borealis::ui::ConnectionSidebar;
using borealis::ui::ConnectionWizard;
using borealis::ui::CredentialPrompt;
using borealis::ui::DebugPanel;
using borealis::ui::DebugSessionSnapshot;
using borealis::ui::KeysPanel;
using borealis::ui::SettingsPanel;
using borealis::ui::SftpPanel;
using borealis::ui::SessionMonitorPanel;
using borealis::ui::StartupNotice;
using borealis::ui::TunnelPanel;

namespace {

/// @brief 造一个「只有基础内容」的浮层宿主（序号哨兵，同 `itest_debug_panel`）。
[[nodiscard]] auto make_host(std::shared_ptr<au::Text> &out_base) -> std::shared_ptr<au::OverlayHost> {
    auto host = std::make_shared<au::OverlayHost>();
    out_base = std::make_shared<au::Text>(
        aurora::TextProps{.content = std::string{"base"}, .text_color = au::Color{0, 0, 0, 0xFF}});
    static_cast<void>(host->add_overlay(au::Node{out_base}));  // 占住子节点 [0]：基础内容
    return host;
}

/// @brief 七只瞬态面板共用的那条判据（稿 D6）：开在 A → 搬到 B → 再在 B 开一次。
///
/// 打开动作交进来而不写死：侧栏是 `open()`、向导是 `open_new()`、问句是 `ask(...)`，三支之外判据逐字相同，
/// 分成七份就会有一支漏判。
template <typename Panel, typename Open>
auto check_transient_retarget(Panel &panel,
                              au::OverlayHost &a,
                              au::OverlayHost &b,
                              const char *what,
                              const Open &open) -> void {
    AURORA_TEST_REQUIRE_MSG(a.overlay_count() == 0U, what);
    AURORA_TEST_REQUIRE_MSG(b.overlay_count() == 0U, what);
    open(panel);
    AURORA_TEST_REQUIRE_MSG(panel.is_open(), what);
    AURORA_TEST_REQUIRE_MSG(a.overlay_count() == 1U, what);
    AURORA_TEST_REQUIRE_MSG(panel.overlay_host() == &a, what);

    // ② 同一宿主重复设＝空操作：这条腿每次命令都会被调一次，不是只有换窗时才调。
    panel.set_overlay_host(a);
    AURORA_TEST_CHECK_MSG(panel.is_open(), what);
    AURORA_TEST_CHECK_MSG(a.overlay_count() == 1U, what);

    // ① 换到另一扇：先摘**旧宿主**上那一层，再换指针——摘错对象（比如摘新宿主）就会在这里露出来。
    panel.set_overlay_host(b);
    AURORA_TEST_CHECK_MSG(a.overlay_count() == 0U, what);
    AURORA_TEST_CHECK_MSG(b.overlay_count() == 0U, what);  // 换完是关闭态：不存在「各一份」
    AURORA_TEST_CHECK_MSG(!panel.is_open(), what);
    AURORA_TEST_CHECK_MSG(panel.overlay_host() == &b, what);

    open(panel);
    AURORA_TEST_CHECK_MSG(panel.is_open(), what);
    AURORA_TEST_CHECK_MSG(b.overlay_count() == 1U, what);
    AURORA_TEST_CHECK_MSG(a.overlay_count() == 0U, what);
}

[[nodiscard]] auto debug_snapshot() -> DebugSessionSnapshot {
    return DebugSessionSnapshot{.title = "snap", .parse_ignored = 1U};
}

/// @brief 同上，但交**独占**所有权：散窗那一只要真销毁一次，才看得出「清零腿」与「摘回腿」的分工。
[[nodiscard]] auto make_owned_host(std::shared_ptr<au::Text> &out_base) -> std::unique_ptr<au::OverlayHost> {
    auto host = std::make_unique<au::OverlayHost>();
    out_base = std::make_shared<au::Text>(
        aurora::TextProps{.content = std::string{"base"}, .text_color = au::Color{0, 0, 0, 0xFF}});
    static_cast<void>(host->add_overlay(au::Node{out_base}));
    return host;
}

[[nodiscard]] auto corrupt_report() -> LoadReport {
    LoadReport report;
    report.outcome = LoadOutcome::RecoveredCorrupt;
    report.message = "config file is not readable json";
    return report;
}

}  // namespace

AURORA_TEST_CASE(the_sidebar_moves_from_the_retiring_host_to_the_landing_one) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base_a;
    std::shared_ptr<au::Text> base_b;
    const std::shared_ptr<au::OverlayHost> host_a = make_host(base_a);
    const std::shared_ptr<au::OverlayHost> host_b = make_host(base_b);
    au::OverlayHost &a = *host_a;
    au::OverlayHost &b = *host_b;
    ConnectionSidebar sidebar{a, {}};
    check_transient_retarget(sidebar, a, b, "sidebar", [](ConnectionSidebar &panel) -> void { panel.open(); });
}

AURORA_TEST_CASE(the_wizard_moves_and_reopens_on_the_landing_host) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base_a;
    std::shared_ptr<au::Text> base_b;
    const std::shared_ptr<au::OverlayHost> host_a = make_host(base_a);
    const std::shared_ptr<au::OverlayHost> host_b = make_host(base_b);
    au::OverlayHost &a = *host_a;
    au::OverlayHost &b = *host_b;
    ConnectionWizard wizard{a, {}};
    check_transient_retarget(wizard, a, b, "wizard", [](ConnectionWizard &panel) -> void { panel.open_new(); });
}

AURORA_TEST_CASE(the_secret_prompt_moves_before_it_asks) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base_a;
    std::shared_ptr<au::Text> base_b;
    const std::shared_ptr<au::OverlayHost> host_a = make_host(base_a);
    const std::shared_ptr<au::OverlayHost> host_b = make_host(base_b);
    au::OverlayHost &a = *host_a;
    au::OverlayHost &b = *host_b;
    CredentialPrompt prompt{a, {}};
    // 装配层那条漏斗腿的次序在此判死：**先搬再问**。反过来（先问后搬）会把材料问句弹在用户没碰过的那一扇，
    // 而问句已经挂上的那一层会留在旧宿主——正是判据 ① 那两行读出来的形态。
    check_transient_retarget(prompt, a, b, "prompt", [](CredentialPrompt &panel) -> void {
        panel.ask(SecretAsk::Password, "profile");
    });
}

AURORA_TEST_CASE(the_keys_panel_moves_its_rows_off_the_retiring_host) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base_a;
    std::shared_ptr<au::Text> base_b;
    const std::shared_ptr<au::OverlayHost> host_a = make_host(base_a);
    const std::shared_ptr<au::OverlayHost> host_b = make_host(base_b);
    au::OverlayHost &a = *host_a;
    au::OverlayHost &b = *host_b;
    KeysPanel keys{a, {}};
    check_transient_retarget(keys, a, b, "keys", [](KeysPanel &panel) -> void { panel.open(); });
}

AURORA_TEST_CASE(the_sftp_panel_moves_without_leaving_a_copy_behind) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base_a;
    std::shared_ptr<au::Text> base_b;
    const std::shared_ptr<au::OverlayHost> host_a = make_host(base_a);
    const std::shared_ptr<au::OverlayHost> host_b = make_host(base_b);
    au::OverlayHost &a = *host_a;
    au::OverlayHost &b = *host_b;
    SftpPanel sftp{a, {}};
    // D6 那句代价的本体：两扇窗不能同时各开一块 SFTP——搬过来那一瞬旧窗必须先空着。
    check_transient_retarget(sftp, a, b, "sftp", [](SftpPanel &panel) -> void { panel.open(); });
}

AURORA_TEST_CASE(the_monitor_card_moves_without_disturbing_the_tap) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base_a;
    std::shared_ptr<au::Text> base_b;
    const std::shared_ptr<au::OverlayHost> host_a = make_host(base_a);
    const std::shared_ptr<au::OverlayHost> host_b = make_host(base_b);
    au::OverlayHost &a = *host_a;
    au::OverlayHost &b = *host_b;
    // 空 Hooks：本件的搬窗判据与其余七只逐字相同，不需要一条真会话在场——留痕环归 `Session`，
    // 浮层这一跳碰不到它（稿 D6 那句「搬窗不停采集」的结构根据就在这条判据里）。
    SessionMonitorPanel monitor{a, {}};
    check_transient_retarget(monitor, a, b, "monitor", [](SessionMonitorPanel &panel) -> void { panel.open(); });
}

AURORA_TEST_CASE(the_tunnel_panel_moves_as_one_list) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base_a;
    std::shared_ptr<au::Text> base_b;
    const std::shared_ptr<au::OverlayHost> host_a = make_host(base_a);
    const std::shared_ptr<au::OverlayHost> host_b = make_host(base_b);
    au::OverlayHost &a = *host_a;
    au::OverlayHost &b = *host_b;
    TunnelPanel tunnels{a, {}};
    check_transient_retarget(tunnels, a, b, "tunnels", [](TunnelPanel &panel) -> void { panel.open(); });
}

AURORA_TEST_CASE(the_settings_page_moves_instead_of_existing_twice) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base_a;
    std::shared_ptr<au::Text> base_b;
    const std::shared_ptr<au::OverlayHost> host_a = make_host(base_a);
    const std::shared_ptr<au::OverlayHost> host_b = make_host(base_b);
    au::OverlayHost &a = *host_a;
    au::OverlayHost &b = *host_b;
    au::ShortcutRegistry shortcuts;
    SettingsPanel settings{a, shortcuts, {}};
    check_transient_retarget(settings, a, b, "settings", [](SettingsPanel &panel) -> void { panel.open(); });
}

AURORA_TEST_CASE(the_debug_panel_is_detached_from_the_old_host_and_remounted_on_the_new_one) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base_a;
    std::shared_ptr<au::Text> base_b;
    const std::shared_ptr<au::OverlayHost> host_a = make_host(base_a);
    const std::shared_ptr<au::OverlayHost> host_b = make_host(base_b);
    au::OverlayHost &a = *host_a;
    au::OverlayHost &b = *host_b;
    au::FocusManager focus_a;
    au::FocusManager focus_b;
    DebugPanel panel{a, focus_a};

    panel.toggle({debug_snapshot()});
    AURORA_TEST_REQUIRE(panel.is_showing());
    AURORA_TEST_REQUIRE_EQ(a.overlay_count(), 1U);
    AURORA_TEST_REQUIRE_EQ(focus_a.scope_depth(), 1U);  // 栈外打开：本件自压一次

    panel.set_overlay_destination(b, focus_b);
    AURORA_TEST_CHECK_EQ(a.overlay_count(), 0U);   // 摘的是旧宿主那层——常驻浮层不搬就会每帧给散窗作画
    AURORA_TEST_CHECK_EQ(b.overlay_count(), 1U);   // 重挂：本件的浮层形态是「挂一次只切显隐」
    AURORA_TEST_CHECK_TRUE(panel.overlay_host() == &b);
    AURORA_TEST_CHECK_FALSE(panel.is_showing());   // 搬完是关闭态（作用域记账按旧栈深度取，跨栈比较会误判）
    AURORA_TEST_CHECK_EQ(focus_a.scope_depth(), 0U);  // 旧栈那一层由**旧**焦点权威收回去
    AURORA_TEST_CHECK_EQ(focus_b.scope_depth(), 0U);

    panel.toggle({debug_snapshot()});
    AURORA_TEST_CHECK_TRUE(panel.is_showing());
    AURORA_TEST_CHECK_EQ(b.overlay_count(), 1U);
    AURORA_TEST_CHECK_EQ(a.overlay_count(), 0U);
    AURORA_TEST_CHECK_EQ(focus_b.scope_depth(), 1U);  // 新栈压新栈，此后由新栈弹出
    AURORA_TEST_CHECK_EQ(focus_a.scope_depth(), 0U);
}

AURORA_TEST_CASE(the_startup_notice_follows_its_window_and_stays_open) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base_a;
    std::shared_ptr<au::Text> base_b;
    const std::shared_ptr<au::OverlayHost> host_a = make_host(base_a);
    const std::shared_ptr<au::OverlayHost> host_b = make_host(base_b);
    au::OverlayHost &a = *host_a;
    au::OverlayHost &b = *host_b;
    au::FocusManager focus_a;
    au::FocusManager focus_b;
    StartupNotice notice{a, focus_a};

    AURORA_TEST_REQUIRE_TRUE(notice.show_if_needed(corrupt_report()));
    AURORA_TEST_REQUIRE(notice.is_showing());
    AURORA_TEST_REQUIRE_EQ(a.overlay_count(), 1U);
    AURORA_TEST_REQUIRE_EQ(focus_a.scope_depth(), 1U);

    notice.set_overlay_destination(b, focus_b);
    AURORA_TEST_CHECK_EQ(a.overlay_count(), 0U);
    AURORA_TEST_CHECK_EQ(b.overlay_count(), 1U);
    // 与调试面板的唯一差别：一启动只弹一次的降级提示不能因搬窗而消失，故它**跟着搬过去仍开着**。
    AURORA_TEST_CHECK_TRUE(notice.is_showing());
    AURORA_TEST_CHECK_EQ(focus_a.scope_depth(), 0U);
    AURORA_TEST_CHECK_EQ(focus_b.scope_depth(), 1U);  // 作用域随框一起搬到新栈

    notice.set_overlay_destination(b, focus_b);  // 同一对落点重复设＝空操作
    AURORA_TEST_CHECK_TRUE(notice.is_showing());
    AURORA_TEST_CHECK_EQ(b.overlay_count(), 1U);
    AURORA_TEST_CHECK_EQ(focus_b.scope_depth(), 1U);
}

AURORA_TEST_CASE(the_debug_panel_abandons_a_host_that_is_already_gone) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base_b;
    const std::shared_ptr<au::OverlayHost> host_b = make_host(base_b);
    au::OverlayHost &b = *host_b;
    au::FocusManager focus_b;

    std::shared_ptr<au::Text> base_a;
    std::unique_ptr<au::OverlayHost> retiring_holder = make_owned_host(base_a);
    au::OverlayHost &retiring = *retiring_holder;
    au::FocusManager focus_a;
    DebugPanel panel{retiring, focus_a};
    panel.toggle({debug_snapshot()});
    AURORA_TEST_REQUIRE_EQ(retiring.overlay_count(), 1U);

    // 散窗：宿主连同它挂的那层一起没了（生产路径上框架在本钩子之前就先收了窗口宿主）。此后**任何一次**
    // `remove_overlay` 都是往已释放对象上落手，而 `set_overlay_destination` 那条腿正是这么做的——
    // 这一条判的就是本件不许走那条腿。
    retiring_holder.reset();  // 释放即本条要的现场

    panel.abandon_overlay_destination(b, focus_b);
    AURORA_TEST_CHECK_TRUE(panel.overlay_host() == &b);
    AURORA_TEST_CHECK_FALSE(panel.is_showing());
    AURORA_TEST_CHECK_TRUE(panel.dialog() == nullptr);  // 句柄一起放，否则下一次 toggle 永不重挂
    AURORA_TEST_CHECK_EQ(b.overlay_count(), 0U);        // 清零而非搬：这一条腿不给新宿主挂东西
    AURORA_TEST_CHECK_EQ(focus_b.scope_depth(), 0U);    // 新栈也没被压（旧栈那一次随窗一起没了）

    panel.toggle({debug_snapshot()});
    AURORA_TEST_CHECK_TRUE(panel.is_showing());
    AURORA_TEST_CHECK_EQ(b.overlay_count(), 1U);
    AURORA_TEST_CHECK_EQ(focus_b.scope_depth(), 1U);
}

AURORA_TEST_CASE(the_startup_notice_abandons_without_touching_the_dead_stack) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base_b;
    const std::shared_ptr<au::OverlayHost> host_b = make_host(base_b);
    au::OverlayHost &b = *host_b;
    au::FocusManager focus_b;

    std::shared_ptr<au::Text> base_a;
    std::unique_ptr<au::OverlayHost> a = make_owned_host(base_a);
    au::FocusManager focus_a;
    StartupNotice notice{*a, focus_a};
    AURORA_TEST_REQUIRE_TRUE(notice.show_if_needed(corrupt_report()));
    au::OverlayHost *const retiring = a.get();
    AURORA_TEST_REQUIRE_EQ(retiring->overlay_count(), 1U);

    notice.abandon_overlay_destination(b, focus_b);
    AURORA_TEST_CHECK_TRUE(notice.overlay_host() == &b);
    AURORA_TEST_CHECK_FALSE(notice.is_showing());
    AURORA_TEST_CHECK_TRUE(notice.dialog() == nullptr);
    AURORA_TEST_CHECK_EQ(b.overlay_count(), 0U);        // 新宿主也不许被挂上
    AURORA_TEST_CHECK_EQ(retiring->overlay_count(), 1U);  // 旧宿主那层**没被摘**：走错腿就会在这里露出来
    // 旧栈**没被弹**：这里旧焦点管理器还活着，正好看得住这件事——走错腿的话那一层会被少弹掉，
    // 而真实散窗时它根本已经不存在（本件留下的这一次由旧栈随窗一起带走）。代价登记在稿 §10。
    AURORA_TEST_CHECK_EQ(focus_a.scope_depth(), 1U);
    a.reset();

    // 幂等闸不动：搬窗不重弹，提示属窗散去即消失（稿 D6 的代价档）。
    AURORA_TEST_CHECK_FALSE(notice.show_if_needed(corrupt_report()));
}

}  // namespace borealis::test_cases::itest_panel_retarget
