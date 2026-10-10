// ============================================================
// SSH 密钥管理面板集成用例（tests/integration/itest_keys_panel.cpp）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.10 批 3 面板腿（UI_KEYS.draft.md §8 的「无头真派发」判据）：
// 行表随快照刷新、真点复制交出公钥单行、同名红提示就地拒绝、生成期提交置灰且
// 完成即收框、推送对话框的阶梯与归因随快照重建、删除两段式（取消一物不动）、
// 目录腿经 `file_dialog::headless_folder_result` 钩子证接线（同 settings_panel
// 的导出腿：只测接线不测产物）。
// 驱动台形态照 itest_tunnel_panel 的 Harness（present_root 排真实布局 +
// hit_test_chain 取命中 + EventDispatcher 发真事件）；判据一律走面板交出的观测面
// （visible_rows / generate_open / push_path / delete_confirm_open），浮层树只用于
// 「按钮真点得到」这一条派发腿。
// ============================================================

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "aurora/app/file_dialog.h"
#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/widget/button.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"
#include "conn/key_push.h"
#include "borealis/conn/profile.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/keys_format.h"
#include "../../src/ui/keys_panel.h"
#include "../../src/ui/settings_i18n.h"

namespace borealis::test_cases::itest_keys_panel {

namespace {

namespace conn = borealis::conn;
namespace ui = borealis::ui;
namespace au = aurora;

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 760;

/// @brief 装配层替身：latest-value 快照 + 十条 Hooks 的调用留痕（struct-of-回调同族）。
struct Probe {
    ui::KeysSnapshot snapshot;
    std::vector<conn::Profile> profiles;
    int scan_requests = 0;
    std::vector<std::string> add_dir_calls;
    std::vector<std::string> copy_calls;
    std::vector<ui::KeysPanel::GenerateDraft> generate_calls;
    std::vector<std::string> export_calls;
    std::vector<std::pair<std::string, std::string>> push_calls;
    std::vector<std::string> delete_calls;

    [[nodiscard]] auto hooks() -> ui::KeysPanel::Hooks {
        ui::KeysPanel::Hooks out;
        out.snapshot = [this]() -> ui::KeysSnapshot { return snapshot; };
        out.request_scan = [this]() -> void { ++scan_requests; };
        // 真函数（POSIX 腿是 headless 回退）：headless_folder_result 非空即原样返回，
        // 空即等价取消——「目录腿经钩子证接线」判据用的就是这条真路径。
        out.pick_key_dir = []() -> std::string {
            const au::Result<std::string> result = aurora::file_dialog::open_folder();
            return result.ok() ? result.value() : std::string{};
        };
        out.add_dir = [this](const std::string &dir) -> void { add_dir_calls.push_back(dir); };
        out.profiles = [this]() -> std::vector<conn::Profile> { return profiles; };
        out.copy_line = [this](const std::string &line) -> void { copy_calls.push_back(line); };
        out.generate = [this](const ui::KeysPanel::GenerateDraft &draft) -> void {
            generate_calls.push_back(draft);
        };
        out.export_public = [this](const std::string &path) -> void {
            export_calls.push_back(path);
        };
        out.push = [this](const std::string &path, const std::string &profile_id) -> void {
            push_calls.emplace_back(path, profile_id);
        };
        out.delete_key = [this](const std::string &path) -> void { delete_calls.push_back(path); };
        return out;
    }
};

/// @brief 一把可复制可推送的 ed25519 行（base64 与注释由扫盘腿一次带出，批 3 补字段）。
[[nodiscard]] auto ed_row(const std::string &name) -> conn::KeyCandidate {
    auto row = conn::KeyCandidate{};
    row.path = "/home/dev/.ssh/" + name;
    row.basename = name;
    row.type = conn::KeyType::Ed25519;
    row.has_public = true;
    row.public_path = row.path + ".pub";
    row.encrypted = false;
    row.fingerprint = "SHA256:AAAA1111";
    row.public_base64 = "AAAAB3NzaC1";
    row.comment = "dev@host";
    return row;
}

/// @brief 一份带目录表（`~/.ssh` 恒在首位）的缺省快照。
[[nodiscard]] auto snapshot_with(std::vector<conn::KeyCandidate> rows) -> ui::KeysSnapshot {
    auto snapshot = ui::KeysSnapshot{};
    snapshot.rows = std::move(rows);
    snapshot.dirs = {conn::KeyDirEntry{"/home/dev/.ssh", true, true}};
    return snapshot;
}

/// @brief 一条 SSH 档案 + 一条本地档案（推送下拉只该出现前一条）。
[[nodiscard]] auto default_profiles() -> std::vector<conn::Profile> {
    conn::Profile ssh;
    ssh.id = "prof-ssh";
    ssh.name = "srv";
    ssh.type = conn::ConnectionType::Ssh;
    ssh.ssh.host = "example.org";
    conn::Profile local;
    local.id = "prof-local";
    local.name = "here";
    local.type = conn::ConnectionType::Local;
    return {ssh, local};
}

struct HitSpot {
    au::Widget *widget = nullptr;
    float x = 0.0F;
    float y = 0.0F;
};

/// @brief 无头窗口 + 真布局 + 真指针派发的驱动台（itest_tunnel_panel 同族精简腿）。
class Harness {
  public:
    Harness() {
        host_ = std::make_shared<au::OverlayHost>();
        base_ = std::make_shared<au::Text>(
            aurora::TextProps{.content = std::string{"base"}, .text_color = au::Color{0, 0, 0, 0xFF}});
        (void)host_->add_overlay(au::Node{base_});  // 占住子节点 [0]：基础内容。
        root_ = au::Node{std::static_pointer_cast<au::Widget>(host_)};
        focus_.set_root(&root_.widget());
        render();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;

    [[nodiscard]] auto attach(Probe &probe) -> std::unique_ptr<ui::KeysPanel> {
        return std::make_unique<ui::KeysPanel>(*host_, probe.hooks());
    }
    auto render() -> void {
        static_cast<void>(window_.present_root(root_));
    }

    /// @brief 打开面板并排两帧（浮层入树 + 布局落 bounds）。
    auto open(ui::KeysPanel &panel) -> void {
        panel.open();
        render();
        render();
    }

    /// @brief 排帧后按 tick 走一帧状态泵（快照改动经这里进行表）。
    auto tick(ui::KeysPanel &panel) -> void {
        panel.tick();
        render();
        render();
    }

    [[nodiscard]] auto chain_at(float x_dp, float y_dp) -> std::vector<au::HitNode> {
        return root_.widget().hit_test_chain(
            au::Point{.x = x_dp, .y = y_dp},
            au::Rect{.origin = au::Point{.x = 0.0F, .y = 0.0F},
                     .size = au::Size{.width = static_cast<float>(kWindowWidth),
                                      .height = static_cast<float>(kWindowHeight)}},
            au::BuildContext{});
    }

    [[nodiscard]] auto hit(float x_dp, float y_dp) -> au::Widget * {
        const auto chain = chain_at(x_dp, y_dp);
        return chain.empty() ? nullptr : chain.back().ptr;
    }

    auto pointer(au::MouseAction action, float x_dp, float y_dp) -> void {
        au::MouseEvent event;
        event.position = au::Point{.x = x_dp, .y = y_dp};
        event.button = au::MouseButton::Left;
        event.action = action;
        static_cast<void>(dispatcher_.dispatch_mouse(root_.widget(), event, &focus_));
    }

    auto click(float x_dp, float y_dp) -> void {
        pointer(au::MouseAction::Press, x_dp, y_dp);
        pointer(au::MouseAction::Release, x_dp, y_dp);
    }

    /// @brief 单击一个 Spot（先确认派发链在这一点仍交回它——点空不算点中）。
    auto click(const HitSpot &spot) -> void {
        AURORA_TEST_REQUIRE_MSG(spot.widget != nullptr, "the control is not dispatch-reachable");
        AURORA_TEST_REQUIRE_MSG(hit(spot.x, spot.y) == spot.widget,
                                "the probe point no longer claims the widget");
        click(spot.x, spot.y);
    }

    /// @brief 向当前焦点控件逐字符发真实文本输入。
    auto type(std::string_view text) -> bool {
        bool handled = false;
        for (const char ch : text) {
            au::TextInputEvent event;
            event.text = std::string(1U, ch);
            handled = au::EventDispatcher::dispatch(root_.widget(), event, focus_) || handled;
        }
        return handled;
    }

    [[nodiscard]] auto press(au::KeyCode key) -> bool {
        au::KeyEvent event;
        event.key = static_cast<int>(key);
        event.action = au::KeyAction::Down;
        return au::EventDispatcher::dispatch(root_.widget(), event, focus_);
    }

    /// @brief 扫描派发链，找第 nth 个满足 accept 的控件（行序＝扫描序，4 dp 步长）。
    [[nodiscard]] auto find(std::string_view type_name, std::size_t nth,
                            const std::function<bool(au::Widget *)> &accept = {}) -> HitSpot {
        std::vector<au::Widget *> seen;
        for (float y = 4.0F; y < static_cast<float>(kWindowHeight) - 4.0F; y += 4.0F) {
            for (float x = 20.0F; x < static_cast<float>(kWindowWidth) - 20.0F; x += 4.0F) {
                au::Widget *widget = hit(x, y);
                if (widget == nullptr || type_name != widget->type_name() ||
                    std::find(seen.begin(), seen.end(), widget) != seen.end()) {
                    continue;
                }
                if (accept && !accept(widget)) {
                    continue;
                }
                seen.push_back(widget);
                if (seen.size() == nth) {
                    return HitSpot{.widget = widget, .x = x, .y = y};
                }
            }
        }
        return HitSpot{};
    }

    /// @brief 按解析后的显示标签找按钮（accessibility_label 与绘制同源，见 button.h i18n 契约）。
    [[nodiscard]] auto find_button(const std::string &label, std::size_t nth = 1) -> HitSpot {
        return find("Button", nth, [&label](au::Widget *widget) -> bool {
            const auto *button = dynamic_cast<const au::Button *>(widget);
            return button != nullptr && button->accessibility_label() == label;
        });
    }

    /// @brief 按显示标签找正文 Text（红提示与留痕的可见性判据走这条）。
    [[nodiscard]] auto find_text(const std::string &label) -> HitSpot {
        return find("Text", 1, [&label](au::Widget *widget) -> bool {
            const auto *text = dynamic_cast<const au::Text *>(widget);
            return text != nullptr && text->accessibility_label() == label;
        });
    }

    /// @brief 按行序取第 nth 枚输入框（生成对话框的字段次序＝构造次序）。
    [[nodiscard]] auto find_input(std::size_t nth) -> HitSpot {
        return find("TextInput", nth);
    }

    [[nodiscard]] auto overlay_count() const -> std::size_t {
        return host_->overlay_count();
    }

  private:
    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        static_cast<void>(surface->begin_frame(kWindowWidth, kWindowHeight));
        return au::Window{std::move(surface)};
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    std::shared_ptr<au::OverlayHost> host_;
    std::shared_ptr<au::Text> base_;
    au::Node root_;
    au::FocusManager focus_;
    aurora::EventDispatcher dispatcher_;
};

}  // namespace

// ------------------------------------------------------------
// 判据①（稿 §4）：行表随快照刷新（D6①）＋开面板即请扫盘
// ------------------------------------------------------------
AURORA_TEST_CASE(rows_follow_snapshot_across_ticks) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.snapshot = snapshot_with({ed_row("id_ed"), ed_row("id_rsa2")});
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 2U);
    // 开面板即请装配层扫一次（启动序不扫盘的另一半：不点开就一次都不扫）。
    AURORA_TEST_CHECK_EQ(probe.scan_requests, 1U);

    probe.snapshot.rows.push_back(ed_row("id_new"));
    h.tick(*panel);
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 3U);

    // 面板关着 tick 是空操作——worker 与快照都归装配层，行表只在开时随泵走。
    panel->close();
    h.render();
    probe.snapshot.rows.clear();
    panel->tick();
    AURORA_TEST_CHECK(!panel->is_open());
}

// ------------------------------------------------------------
// 判据②（稿 §2 判据 4）：真点「复制」交出公钥单行 + 顶行留痕
// ------------------------------------------------------------
AURORA_TEST_CASE(copy_hands_over_the_full_single_line) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.snapshot = snapshot_with({ed_row("id_ed")});
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto copy_spot = h.find_button(ui::settings_label("keys.action.copy"));
    AURORA_TEST_REQUIRE_MSG(copy_spot.widget != nullptr, "the copy button is not dispatch-reachable");
    h.click(copy_spot);
    AURORA_TEST_REQUIRE_EQ(probe.copy_calls.size(), 1U);
    // 交出的字节＝`<线名> <base64> <注释>` 单行、无尾随换行（判据 4 的那一行）。
    AURORA_TEST_CHECK(probe.copy_calls[0] == "ssh-ed25519 AAAAB3NzaC1 dev@host");

    // 顶行留痕「已复制」（装配层 publish_notice 的那一格随快照上屏）。
    probe.snapshot.notice_key = "keys.notice.copied";
    h.tick(*panel);
    auto note_spot = h.find_text(ui::settings_label("keys.notice.copied"));
    AURORA_TEST_REQUIRE_MSG(note_spot.widget != nullptr, "the copied note is not drawn");

    // 拼不出公钥单行的行（两族之外）没有复制/推送/导出三枚——动作整枚不画的判据。
    auto unknown = ed_row("id_weird");
    unknown.type = conn::KeyType::Unknown;
    unknown.public_base64.clear();
    probe.snapshot = snapshot_with({unknown});
    h.tick(*panel);
    AURORA_TEST_CHECK(!h.find_button(ui::settings_label("keys.action.copy")).widget);
    AURORA_TEST_CHECK(!h.find_button(ui::settings_label("keys.action.push")).widget);
    // 它有配对 `.pub`，也不该出现「导出公钥」。
    AURORA_TEST_CHECK(!h.find_button(ui::settings_label("keys.action.export")).widget);
    // 删除这枚常驻动作还在——认不出算法族的钥匙也得删得掉（keys_format.h 头注的同一条口径）。
    AURORA_TEST_CHECK(h.find_button(ui::settings_label("keys.action.delete")).widget != nullptr);
}

// ------------------------------------------------------------
// 判据③（稿 §2 判据 5 前半）：同名即拒——红提示就地、对话框不收、不提交
// ------------------------------------------------------------
AURORA_TEST_CASE(generate_refuses_a_taken_name_in_place) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.snapshot = snapshot_with({ed_row("id_ed")});
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto gen_spot = h.find_button(ui::settings_label("keys.action.generate"));
    AURORA_TEST_REQUIRE_MSG(gen_spot.widget != nullptr, "the generate button is not reachable");
    h.click(gen_spot);
    h.render();
    h.render();
    AURORA_TEST_CHECK_TRUE(panel->generate_open());

    // ed25519 卡的字段行序：名称、注释、目标目录、口令、口令二（无位数下拉）。
    auto name_spot = h.find_input(1);
    AURORA_TEST_REQUIRE(name_spot.widget != nullptr);
    h.click(name_spot);
    static_cast<void>(h.type("id_ed"));  // 与既有行撞名。

    auto save_spot = h.find_button(ui::settings_label("keys.generate.action.save"));
    AURORA_TEST_REQUIRE(save_spot.widget != nullptr);
    h.click(save_spot);
    h.render();
    h.render();

    auto notice_spot = h.find_text(ui::settings_label("keys.generate.name_taken"));
    AURORA_TEST_REQUIRE_MSG(notice_spot.widget != nullptr, "the taken-name notice is not shown");
    AURORA_TEST_CHECK_TRUE(panel->generate_open());          // 拒绝关闭：草稿还在。
    AURORA_TEST_CHECK_TRUE(probe.generate_calls.empty());    // 未提交。
    AURORA_TEST_CHECK(panel->generate_draft().basename == "id_ed");

    // 改成新名后同一对话框可提交（编辑→提交的合法腿）。红提示占了行，重取坐标。
    h.click(name_spot);
    static_cast<void>(h.press(au::KeyCode::End));
    for (int i = 0; i < 5; ++i) {
        static_cast<void>(h.press(au::KeyCode::Backspace));
    }
    static_cast<void>(h.type("id_new"));
    save_spot = h.find_button(ui::settings_label("keys.generate.action.save"));
    AURORA_TEST_REQUIRE_MSG(save_spot.widget != nullptr, "the save button moved out of reach");
    h.click(save_spot);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE_EQ(probe.generate_calls.size(), 1U);
    AURORA_TEST_CHECK(probe.generate_calls[0].basename == "id_new");
    // 草稿缺省腿：目标目录＝目录表首条（`~/.ssh`），位数＝下拉缺省 3072（判据 5）。
    AURORA_TEST_CHECK(probe.generate_calls[0].directory == "/home/dev/.ssh");
    AURORA_TEST_CHECK_EQ(probe.generate_calls[0].rsa_bits, 3072);
    AURORA_TEST_CHECK_FALSE(probe.generate_calls[0].rsa);
}

// ------------------------------------------------------------
// 判据④（稿 §2 判据 5 后半）：提交后置灰（连点不重复提交）＋快照离开即收框
// ------------------------------------------------------------
AURORA_TEST_CASE(generate_in_flight_grays_submit_and_closes_on_completion) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.snapshot = snapshot_with({ed_row("id_ed")});
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    h.click(h.find_button(ui::settings_label("keys.action.generate")));
    h.render();
    h.render();
    auto name_spot = h.find_input(1);
    AURORA_TEST_REQUIRE(name_spot.widget != nullptr);
    h.click(name_spot);
    static_cast<void>(h.type("brand_new"));
    h.click(h.find_button(ui::settings_label("keys.generate.action.save")));
    h.render();
    h.render();
    AURORA_TEST_REQUIRE_EQ(probe.generate_calls.size(), 1U);

    // 置灰腿：再点一次提交钮不呼第二遍（禁用态忽略点击）。
    auto save_spot = h.find_button(ui::settings_label("keys.generate.action.save"));
    AURORA_TEST_REQUIRE(save_spot.widget != nullptr);
    h.click(save_spot);
    h.render();
    h.render();
    AURORA_TEST_CHECK_EQ(probe.generate_calls.size(), 1U);

    // 快照仍在生成档：对话框不收（判据 5 的「行内生成中」那句还在等终值）。
    probe.snapshot.op = ui::KeysOpKind::Generate;
    h.tick(*panel);
    AURORA_TEST_CHECK_TRUE(panel->generate_open());

    // 终值回投＋重扫行表到货：tick 收框（那个对话框的唯一自动出口）。
    probe.snapshot.op = ui::KeysOpKind::Idle;
    probe.snapshot.rows.push_back(ed_row("brand_new"));
    h.tick(*panel);
    AURORA_TEST_CHECK(!panel->generate_open());
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 2U);
}

// ------------------------------------------------------------
// 判据⑤（稿 §2 判据 6）：推送对话框——SSH 子集下拉、提交呼 Hooks、
// 阶梯当前格与失败归因随快照重建
// ------------------------------------------------------------
AURORA_TEST_CASE(push_dialog_follows_the_report) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.snapshot = snapshot_with({ed_row("id_ed")});
    probe.profiles = default_profiles();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto push_spot = h.find_button(ui::settings_label("keys.action.push"));
    AURORA_TEST_REQUIRE_MSG(push_spot.widget != nullptr, "the push button is not dispatch-reachable");
    h.click(push_spot);
    h.render();
    h.render();
    AURORA_TEST_CHECK_TRUE(panel->push_open());
    AURORA_TEST_CHECK(panel->push_path() == "/home/dev/.ssh/id_ed");

    // 目标下拉只列 SSH 子集（本地档案不进表）。
    auto box_spot = h.find("Dropdown", 1);
    AURORA_TEST_REQUIRE_MSG(box_spot.widget != nullptr, "the profile dropdown is not reachable");
    auto *box = dynamic_cast<au::Dropdown *>(box_spot.widget);
    AURORA_TEST_REQUIRE(box != nullptr);
    AURORA_TEST_CHECK_EQ(box->option_count(), 1U);

    // 点「推送」动作 → hooks.push(行路径, 选中的档案 id)。
    auto go_spot = h.find_button(ui::settings_label("keys.push.action"));
    AURORA_TEST_REQUIRE_MSG(go_spot.widget != nullptr, "the push action is not reachable");
    h.click(go_spot);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE_EQ(probe.push_calls.size(), 1U);
    AURORA_TEST_CHECK(probe.push_calls[0].first == "/home/dev/.ssh/id_ed");
    AURORA_TEST_CHECK(probe.push_calls[0].second == "prof-ssh");

    // 在途档（装配层提交时发布）：提交钮置灰，再点不呼第二遍。
    probe.snapshot.op = ui::KeysOpKind::Push;
    h.tick(*panel);
    go_spot = h.find_button(ui::settings_label("keys.push.action"));
    AURORA_TEST_REQUIRE(go_spot.widget != nullptr);
    h.click(go_spot);
    h.render();
    h.render();
    AURORA_TEST_CHECK_EQ(probe.push_calls.size(), 1U);

    // 失败归因（认证被拒）：阶梯当前格随 push_step 走、留痕上屏。
    probe.snapshot.op = ui::KeysOpKind::Idle;
    probe.snapshot.push_step = conn::push_stage_index(conn::PushStage::Auth);
    probe.snapshot.notice_key = "keys.push.fail.auth";
    h.tick(*panel);
    auto fail_spot = h.find_text(ui::settings_label("keys.push.fail.auth"));
    AURORA_TEST_REQUIRE_MSG(fail_spot.widget != nullptr, "the failure finding is not drawn");

    // 成功且未发写的留痕（判据 §6 第一句）也经同一格（已在授权表里）。
    probe.snapshot.push_step = conn::push_stage_index(conn::PushStage::Done);
    probe.snapshot.notice_key = "keys.push.already_authorized";
    h.tick(*panel);
    auto ok_spot = h.find_text(ui::settings_label("keys.push.already_authorized"));
    AURORA_TEST_REQUIRE_MSG(ok_spot.widget != nullptr, "the already-authorized note is not drawn");
}

// ------------------------------------------------------------
// 判据⑥（稿 §2 判据 7 / D12⑴）：删除两段式——确认框复述路径、取消一物不动
// ------------------------------------------------------------
AURORA_TEST_CASE(delete_is_two_stage_and_cancel_touches_nothing) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.snapshot = snapshot_with({ed_row("id_ed")});
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto delete_spot = h.find_button(ui::settings_label("keys.action.delete"));
    AURORA_TEST_REQUIRE_MSG(delete_spot.widget != nullptr, "the delete button is not reachable");
    h.click(delete_spot);
    h.render();
    h.render();
    AURORA_TEST_CHECK_TRUE(panel->delete_confirm_open());
    AURORA_TEST_CHECK(panel->pending_delete_path() == "/home/dev/.ssh/id_ed");
    AURORA_TEST_CHECK_TRUE(probe.delete_calls.empty());  // 确认前一物不动。

    // 确认框复述**具体路径**（D12⑴）：用户点的才是自己认得的那一对。
    auto path_spot = h.find_text("/home/dev/.ssh/id_ed");
    AURORA_TEST_REQUIRE_MSG(path_spot.widget != nullptr, "the confirm dialog does not name the path");

    // 取消：框收、整条不跑。
    auto cancel_spot = h.find_button(ui::settings_label("keys.delete.action.cancel"));
    AURORA_TEST_REQUIRE(cancel_spot.widget != nullptr);
    h.click(cancel_spot);
    h.render();
    h.render();
    AURORA_TEST_CHECK(!panel->delete_confirm_open());
    AURORA_TEST_CHECK_TRUE(probe.delete_calls.empty());

    // 再来一遍走确认腿。
    h.click(delete_spot);
    h.render();
    h.render();
    AURORA_TEST_CHECK_TRUE(panel->delete_confirm_open());
    auto confirm_spot = h.find_button(ui::settings_label("keys.delete.action"));
    AURORA_TEST_REQUIRE_MSG(confirm_spot.widget != nullptr, "the confirm button is not reachable");
    h.click(confirm_spot);
    h.render();
    h.render();
    AURORA_TEST_CHECK(!panel->delete_confirm_open());
    AURORA_TEST_REQUIRE_EQ(probe.delete_calls.size(), 1U);
    AURORA_TEST_CHECK(probe.delete_calls[0] == "/home/dev/.ssh/id_ed");
}

// ------------------------------------------------------------
// 判据⑦（D11②）：目录腿经 file_dialog headless 钩子证接线——
// 给了路径就整条交回；空串（取消/起不来）整条不跑且不留痕
// ------------------------------------------------------------
AURORA_TEST_CASE(the_directory_leg_hands_the_picked_path_to_the_assembly) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.snapshot = snapshot_with({});
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    aurora::file_dialog::headless_folder_result = "/srv/keys";
    auto add_spot = h.find_button(ui::settings_label("keys.action.add_dir"));
    AURORA_TEST_REQUIRE_MSG(add_spot.widget != nullptr, "the add-dir button is not reachable");
    h.click(add_spot);
    AURORA_TEST_REQUIRE_EQ(probe.add_dir_calls.size(), 1U);
    AURORA_TEST_CHECK(probe.add_dir_calls[0] == "/srv/keys");

    // 钩子置空＝取消（或平台起不来）：整条不跑且不留痕。
    aurora::file_dialog::headless_folder_result.clear();
    h.click(add_spot);
    AURORA_TEST_CHECK_EQ(probe.add_dir_calls.size(), 1U);
    aurora::file_dialog::headless_folder_result.clear();
}

// ------------------------------------------------------------
// 判据⑧（D2③ 细则⑶）：空态与不可达目录留痕上屏；可达目录不占一句
// ------------------------------------------------------------
AURORA_TEST_CASE(empty_state_and_unreachable_dir_notes_are_drawn) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    auto snapshot = ui::KeysSnapshot{};
    snapshot.dirs = {conn::KeyDirEntry{"/home/dev/.ssh", true, true},
                     conn::KeyDirEntry{"/srv/gone", false, false}};
    probe.snapshot = snapshot;
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto empty_spot = h.find_text(ui::settings_label("keys.empty"));
    AURORA_TEST_REQUIRE_MSG(empty_spot.widget != nullptr, "the empty state is not drawn");
    auto gone_spot = h.find_text(
        ui::settings_label("keys.dir.unreachable", {aurora::LocalizedString{"/srv/gone"}}));
    AURORA_TEST_REQUIRE_MSG(gone_spot.widget != nullptr, "the unreachable note is not drawn");
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 0U);
}

}  // namespace borealis::test_cases::itest_keys_panel
