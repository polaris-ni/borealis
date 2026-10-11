#include "borealis/ui/right_click.h"

namespace borealis::ui {

auto plan_right_click(RightClickAction action, bool has_selection, LogMenuState log) -> RightClickPlan {
    RightClickPlan plan;
    switch (action) {
        case RightClickAction::ContextMenu: {
            plan.intent = RightClickIntent::Menu;
            plan.items = {
                MenuItem{.command = MenuCommand::Copy, .enabled = has_selection},
                MenuItem{.command = MenuCommand::Paste},
            };
            // 第三条按能力闸决定**出不出现**（裁决 7.116 D1①／D6①）：闸关时不是灰显，而是这条功能
            // 在本产品里不存在。出现时恒可用——两态文案由 `menu_label` 随 `log.logging` 取，
            // 「点了什么也不发生」在这里不成立：两条入口（右键与命令面板）共用装配层同一份闭包。
            if (log.available) {
                plan.items.push_back(MenuItem{.command = MenuCommand::ToggleSessionLog, .enabled = true});
            }
            break;
        }
        case RightClickAction::Paste: {
            plan.intent = RightClickIntent::Paste;
            break;
        }
        case RightClickAction::CopyOnSelect: {
            plan.intent = has_selection ? RightClickIntent::Copy : RightClickIntent::None;
            break;
        }
    }
    return plan;
}

}  // namespace borealis::ui
