#pragma once

// ============================================================
// 设置面板的实时预览盒（src/ui/settings_preview.h，私有头）
// ------------------------------------------------------------
// `codespec/UI_SETTINGS.draft.md` 的 S7 与判据 F-a~F-e 落地（裁决 7.66）：卡片底部一条 140 dp 的
// 横条，内容由**一个独立的内存会话 + 真实的 `ui::TerminalView`** 画出，于是在面板里改色、改字号、
// 改内边距都能在同一屏里看见，而用户自己的会话不被接管（F-a）。
//
// 三条决定形态的口径：
// ① 不写假预览，复用真链路（裁决 7.25⑩ 的 N5）：夹具是一串**字节**，经 `session::Session` 的
//    解码 → 解析 → 状态机 → 网格，再由 `TerminalView` 既有的五层绘制序列画上屏。预览因此与主视口
//    共用同一份 `PaletteSpec`、同一套排版选项与同一个绘制件——§8 判据① 那句「预览盒与主视口同一
//    格色逐位变化」只有这一条实现路径，且它判的是接线而不是替身行为。
// ② 尺寸一变就重发夹具：本件的连接把 `resize()` 当作「对端换了行列数」，随即把整份夹具重投一次。
//    这条腿能成立是因为 `Session::resize` 调连接是在**锁外**（`src/session/session.cpp` 的 `resize`
//    末句），故在 `TerminalView::on_layout` 的下发途中重发不会自锁；夹具以 `ESC[2J` + `ESC[H]` 开头
//    因此幂等，任意尺寸下重发得到的都是同一张图。代价是重发产出的脏要下一帧才排，故宿主在布局之后
//    须再排一帧（面板 `Hooks::preview_wake` 就是为此而开的那一条接缝）。
// ③ 不给预览派焦点、不装浮层宿主、`copy_on_select` 恒假：少一条都会让一根只用来看的横条摸到用户的
//    东西——焦点被抢则面板的输入框失灵，右键有宿主则真弹菜单，选中即复制则泵帧时写系统剪贴板
//    （§4.5 第 25 条禁止在帧路径上做 IO）。于是预览里的光标恒是**失焦**的空心形态：那是本件的口径
//    而非缺陷，判据 F-b 要的「光标形态」在这里示范的是降级档。
//
// 夹具只有 5 行且高度做成常量（F-e：扁条按 14 pt 约 6~7 行，示范面因此收窄）；本件不按内容算高，
// 也不改卡片总高——卡片尺寸由外层 `LayoutBuilder` 显式钳定，多一条子节点只会从行区的高度里扣。
//
// 私有头（裁决 D1① 同口径）：本件含框架类型，不进 `include/borealis/`。
// ============================================================

#include <memory>
#include <string>
#include <string_view>

#include "aurora/widget/widget.h"

#include "borealis/term/width.h"
#include "terminal_view.h"

namespace borealis::session {
class Session;
}  // namespace borealis::session

namespace borealis::ui {

/// @brief 预览夹具的字节流（F-b 的五类素材：CJK 双宽、框线码点、16/256/真彩三段、粗体与下划线、光标）。
///
/// 交出来是因为 §8 判据① 要拿**同一份素材**喂第二个预览实例，好在同一屏里比「同一格色逐位相等」；
/// 用例自己另抄一份夹具文本就是第二个素材真值源，那种比较会因两份素材不同而失去意义。
[[nodiscard]] auto preview_fixture() -> std::string_view;

/// @brief 一根自足的可预览终端：内存连接 + 独立会话 + 真实视口控件。
///
/// 本件不属于面板私有：装配层与用例都用它造「一个不吃用户输入的终端画面」，于是预览盒与对照视口
/// 结构上同源（同一夹具、同一条绘制路径、同一份 `Appearance` 载体）。
class SettingsPreview {
  public:
    /// @brief 按外观包装好会话与视口，并立即起会话（夹具在 `Connection::start` 里投出）。
    /// @param appearance 外观包；与主视口共用同一个载体，取值域由配置侧把守（裁决 7.46②）。
    explicit SettingsPreview(TerminalView::Appearance appearance);

    SettingsPreview(const SettingsPreview &other) = delete;
    auto operator=(const SettingsPreview &other) -> SettingsPreview & = delete;
    SettingsPreview(SettingsPreview &&other) = delete;
    auto operator=(SettingsPreview &&other) -> SettingsPreview & = delete;

    ~SettingsPreview();

    /// @brief 交进控件树的节点（本件持有所有权，宿主只是把它挂上）。
    [[nodiscard]] auto node() -> aurora::Node;

    /// @brief 视口控件本体（宿主据此设不可获焦并取行列数）。
    [[nodiscard]] auto view() noexcept -> TerminalView & {
        return *view_;
    }

    /// @brief 换外观包：走主视口同一条运行期入口（裁决 7.53 的 S4），不重建视口也不重投夹具。
    /// @param appearance 新的外观包。
    auto apply(TerminalView::Appearance appearance) -> void;

    /// @brief 排一帧：取脏行提交并标脏绘制（宿主在 `Application::set_on_frame` 里逐帧调）。
    auto pump() -> void;

    /// @brief 首次进树时补一次挂载。
    ///
    /// `Window::present_root` 只在根变化时遍历挂载，运行期新加的浮层子节点不在那一次里（裁决 7.49④
    /// 同一条物理事实），不补则本视口的闪烁档与主题订阅永不注册。
    /// @param ctx 宿主布局时的那一份构建上下文。
    auto ensure_mounted(const aurora::BuildContext &ctx) -> void;

  private:
    term::UnicodeWidthPolicy width_policy_{};  ///< 必须先于会话析构：会话持它的引用。
    std::unique_ptr<session::Session> session_{};
    std::shared_ptr<TerminalView> view_{};  ///< 必须先于会话析构：视口持会话的裸引用。
    bool mounted_ = false;
};

}  // namespace borealis::ui
