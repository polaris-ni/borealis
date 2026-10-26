#include "borealis/ui/window_close.h"

namespace borealis::ui {

auto window_close_disposition(WindowCloseInitiator initiator, bool has_live_session)
    -> WindowCloseDisposition {
    // 本层自己发起的那一支**先**判：它连「问」这个动作都不能有，与窗内有没有活会话无关——退出序
    // 遍历全部窗时，任何一次询问都可能挂在一扇已经散掉的窗上（公共头第二段）。
    if (initiator == WindowCloseInitiator::AppInitiated) {
        return WindowCloseDisposition::CloseNow;
    }
    return has_live_session ? WindowCloseDisposition::AskUser : WindowCloseDisposition::CloseNow;
}

}  // namespace borealis::ui
