// ============================================================
// 实时预览盒的实现（src/ui/settings_preview.cpp）
// ------------------------------------------------------------
// 本文件与 `settings_panel.cpp` / `terminal_view.cpp` / `workspace_view.cpp` 同属触达框架 widget
// 面的翻译单元。这里没有排版也没有校验，只有两件：**把夹具喂进一条真的会话链**，以及把那条链的
// 产出交给真的视口控件。判据文 S7 建议项的代价（面板要持有一份不经 `main` 装配的连接替身）
// 就落成本文件那 30 行 `PreviewConnection`。
// ============================================================

#include "settings_preview.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/utf8.h"

namespace borealis::ui {
namespace {

/// @brief 会话的名义初始尺寸：真实行列由控件首次布局按自身矩形派生并下发（F-d 与主视口同一条腿）。
constexpr session::Size kNominalSize{80U, 24U};

/// @brief 夹具的码点文本（编成 UTF-8 字节后才投给会话，故源码保持 ASCII，§4.3 第 14 条）。
///
/// 每行都以 SGR 复位起头：重发发生在任意行列数下，上一行残留的属性不得渗进下一行。
/// CJK-LITERAL: cjk-fixture - 双宽字形的列位是 F-b 点名要示范的那一档素材，换成拉丁字母就让该事实消失。
[[nodiscard]] auto build_fixture() -> std::string {
    std::u32string text = U"\x1b[2J\x1b[H";  // 清屏 + 归零：② 那条幂等重发的前提
    text += U"\x1b[0m$ borealis --preview\r\n";
    // 框线四格宽 8：`│ 中文 │` 的 1+1+2+2+1+1 与上下两条的 1+6+1 逐列对齐，双宽错位在这里一眼可见。
    text += U"\x1b[0;36m\u250c\u2500\u2500\u2500\u2500\u2500\u2500\u2510\x1b[0m\r\n";
    text += U"\x1b[0;36m\u2502\x1b[0m \u4e2d\u6587 \x1b[0;36m\u2502\x1b[0m\r\n";
    // 第 4 行前半是框底，后半三格是 16 色基本色的**底色**档（`ESC[41m`/`42m`/`44m` = `basic[1..4]`
    // 的奇数档）：底色格是实心填充，像素因此可逐位指认，§8 判据① 那句「同一格色」由此可判。
    text += U"\x1b[0;36m\u2514\u2500\u2500\u2500\u2500\u2500\u2500\u2518\x1b[0m\x1b[0;41m \x1b[0;42m \x1b[0;44m \x1b[0m\x1b[0;90m16\r\n";
    text += U"\x1b[0m\x1b[38;5;208m256\x1b[0m \x1b[38;2;80;180;255mtrue\x1b[0m \x1b[1mbold\x1b[0m \x1b[4munderline\x1b[0m";
    text += U"\x1b[?25h";  // 光标留在末行行尾：F-b 的最后一类素材（失焦态由文件头③解释）
    std::string bytes;
    (void)term::encode_utf8(text, bytes);
    return bytes;
}

const std::string kFixture = build_fixture();

/// @brief 预览专用的内存连接：不读不写，只在「起会话」与「换尺寸」两个时刻重投夹具。
///
/// 与用例里的 `FakeConnection` 同一条形态（`tests/support` 那份是测试私有物，本件是产品路径上的
/// 一根自足横条，故各写一份而不是把测试设施抬进 `src/`）。
class PreviewConnection final : public session::Connection {
  public:
    auto start(session::ConnectionEvents &events) -> void override {
        events_ = &events;
        alive_ = true;
        deliver(preview_fixture());
    }

    /// 预览不收用户输入：不发字节，于是没有回显，也没有把按键送进任何对端的路径。
    auto write(std::span<const std::byte> /*bytes*/) -> void override {}

    /// 文件头②：换尺寸即重投。本函数由 `Session::resize` 在**锁外**调用，故这里的 `on_bytes`
    /// 走得到会话而不会构成重入死锁。
    auto resize(session::Size /*size*/) -> void override { deliver(preview_fixture()); }

    auto close() -> void override {
        alive_ = false;
        events_ = nullptr;
    }

    [[nodiscard]] auto alive() const noexcept -> bool override {
        return alive_;
    }

  private:
    auto deliver(std::string_view bytes) -> void {
        if (events_ == nullptr) {
            return;  // 关停之后不再有事件通道：`close()` 已把指针置空，晚到的重投应当静默丢弃
        }
        std::vector<std::byte> raw;
        raw.reserve(bytes.size());
        for (const char c : bytes) {
            raw.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(c)));
        }
        events_->on_bytes(raw);
    }

    session::ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

}  // namespace

auto preview_fixture() -> std::string_view {
    return kFixture;
}

SettingsPreview::SettingsPreview(TerminalView::Appearance appearance)
    : session_{std::make_unique<session::Session>(std::make_unique<PreviewConnection>(), kNominalSize, 0U,
                                                  width_policy_)},
      view_{std::make_shared<TerminalView>(*session_, std::move(appearance), TerminalView::InteractionOptions{})} {
    // 交互项一律取缺省（`copy_on_select` 为假、候选集为空），且不给浮层宿主：见文件头③。
    view_->set_focusable(false);
    session_->start();
}

SettingsPreview::~SettingsPreview() {
    session_->close();  // 先断通道：连接的读回调持着会话，会话比它活得短就悬垂（`session.h` 头注）
}

auto SettingsPreview::node() -> aurora::Node {
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(view_)};
}

auto SettingsPreview::apply(TerminalView::Appearance appearance) -> void {
    view_->apply_appearance(std::move(appearance));
}

auto SettingsPreview::pump() -> void {
    view_->on_frame();
}

auto SettingsPreview::ensure_mounted(const aurora::BuildContext &ctx) -> void {
    if (mounted_) {
        return;
    }
    mounted_ = true;
    view_->mount(ctx);
}

}  // namespace borealis::ui
