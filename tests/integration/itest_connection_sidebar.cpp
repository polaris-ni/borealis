// ============================================================
// 连接管理器侧栏 quick connect 集成用例（tests/integration/itest_connection_sidebar.cpp）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.03` 批 7（稿 `CONN03_ASKHOP_QUICKJ.draft.md` D5/D6/D7）的侧栏接线腿：
// `-J` 好值 ⇒ `hooks_.connect` 收到跳链形状正确、id 带 `quick:` 前缀的临时档案；坏值 ⇒
// 不发起连接且留痕行上屏（一次一句话，机制同导入条数）。切分与折算的判据本体在
// utest_profile_views，这里锁的是「UI 只消费」那一侧的两条行为承诺。
// 驱动台形态照 itest_connection_wizard 的 Harness（`present_root` 排真实布局 +
// `hit_test_chain` 取命中 + EventDispatcher 发真事件）；串问泵腿在 main（裁决 7.83），
// 无自动化证人，按既句式源码审查＋真机走查担保。
// ============================================================

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/widget/button.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"
#include "borealis/conn/profile.h"
#include "framework/aurora_test.h"

// 私有头（裁决 D1① 同口径），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/connection_sidebar.h"
#include "../../src/ui/settings_i18n.h"

namespace borealis::test_cases::itest_connection_sidebar {

namespace {

namespace conn = borealis::conn;
namespace ui = borealis::ui;
namespace au = aurora;

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 760;

/// @brief 装配层替身：档案表 + connect 调用留痕（struct-of-回调同族）。
struct Probe {
    std::vector<conn::Profile> profiles;
    std::vector<conn::Profile> connected;

    [[nodiscard]] auto hooks() -> ui::ConnectionSidebar::Hooks {
        auto out = ui::ConnectionSidebar::Hooks{};
        out.load = [this]() -> std::vector<conn::Profile> { return profiles; };
        out.connect = [this](const conn::Profile &profile) -> void {
            connected.push_back(profile);
        };
        return out;
    }
};

struct HitSpot {
    au::Widget *widget = nullptr;
    float x = 0.0F;
    float y = 0.0F;
};

/// @brief 无头窗口 + 真布局 + 真指针派发的驱动台（itest_connection_wizard 同族精简腿）。
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

    [[nodiscard]] auto attach(Probe &probe) -> std::unique_ptr<ui::ConnectionSidebar> {
        return std::make_unique<ui::ConnectionSidebar>(*host_, probe.hooks());
    }

    auto render() -> void {
        static_cast<void>(window_.present_root(root_));
    }

    /// @brief 开侧栏并排两帧（单帧只够入树，布局落 bounds 要第二帧）。
    auto open(ui::ConnectionSidebar &sidebar) -> void {
        sidebar.open();
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

    /// @brief 点一处并排两帧（留痕行随整块重建上屏，要重画才看得到）。
    auto click_and_settle(const HitSpot &spot) -> void {
        click(spot);
        render();
        render();
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

    /// @brief 按解析后的显示标签找按钮（`accessibility_label` 与绘制同源）。
    [[nodiscard]] auto find_button(const std::string &label, std::size_t nth = 1) -> HitSpot {
        return find("Button", nth, [&label](au::Widget *widget) -> bool {
            const auto *button = dynamic_cast<const au::Button *>(widget);
            return button != nullptr && button->accessibility_label() == label;
        });
    }

    /// @brief 按显示标签找正文 Text（留痕行的可见性判据走这条）。
    [[nodiscard]] auto find_text(const std::string &label) -> HitSpot {
        return find("Text", 1, [&label](au::Widget *widget) -> bool {
            const auto *text = dynamic_cast<const au::Text *>(widget);
            return text != nullptr && text->accessibility_label() == label;
        });
    }

    /// @brief quick connect 那一行的输入框（全树第一枚 TextInput：标号 2 排在搜索框之前）。
    [[nodiscard]] auto quick_input_spot() -> HitSpot { return find("TextInput", 1); }

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

/// @brief 向 quick connect 行灌一句话（点击抢焦，再逐字符发文本事件）。
auto type_quick(Harness &h, std::string_view text) -> void {
    h.click(h.quick_input_spot());
    AURORA_TEST_CHECK_TRUE(h.type(text));
}

auto click_connect(Harness &h) -> void {
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.connect")));
}

}  // namespace

// ------------------------------------------------------------
// 判据①（稿 D5/D7）：`-J` 好值 ⇒ 临时档案带跳链形状交回装配层
// ------------------------------------------------------------
AURORA_TEST_CASE(quick_line_with_jump_tail_connects_as_temporary_profile) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    Harness h;
    auto sidebar = h.attach(probe);
    h.open(*sidebar);

    type_quick(h, "target.example -J gw1:2222,deploy@core.example");
    click_connect(h);

    AURORA_TEST_REQUIRE_MSG(probe.connected.size() == 1, "a good -J line must connect exactly once");
    const auto &profile = probe.connected.front();
    AURORA_TEST_CHECK_TRUE(profile.id == "quick:target.example:22");
    AURORA_TEST_CHECK_TRUE(profile.type == conn::ConnectionType::Ssh);
    AURORA_TEST_REQUIRE(profile.ssh.proxy_jumps.size() == 2);
    AURORA_TEST_CHECK_TRUE(profile.ssh.proxy_jumps[0].host == "gw1" &&
                           profile.ssh.proxy_jumps[0].port == 2222);
    AURORA_TEST_CHECK_TRUE(profile.ssh.proxy_jumps[1].host == "core.example" &&
                           profile.ssh.proxy_jumps[1].user == "deploy" &&
                           profile.ssh.proxy_jumps[1].port == 22);
    // 临时档案缺省 agent 认证（不入库那条纪律原样继承，带跳亦然）。
    AURORA_TEST_CHECK_TRUE(profile.ssh.auth_method == "agent");
}

// ------------------------------------------------------------
// 判据②（稿 D6/D7）：坏跳表 ⇒ 不连 + 留痕行上屏（一次一句话）
// ------------------------------------------------------------
AURORA_TEST_CASE(bad_jump_list_refuses_and_paints_notice_line) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    Harness h;
    auto sidebar = h.attach(probe);
    h.open(*sidebar);

    // 「bad hop」含空白，语法层整串拒 —— 坏值不可见比连错更糟，故必须留痕且不连。
    type_quick(h, "target.example -J bad hop");
    click_connect(h);

    AURORA_TEST_CHECK_TRUE(probe.connected.empty());
    const auto notice = h.find_text(ui::settings_label("connections.notice.quick_jump_bad"));
    AURORA_TEST_REQUIRE_MSG(notice.widget != nullptr, "the refusal must paint a notice line");
}

}  // namespace borealis::test_cases::itest_connection_sidebar
