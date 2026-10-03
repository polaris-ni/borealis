#include "borealis/ui/right_click.h"

namespace borealis::ui {

auto plan_right_click(RightClickAction action, bool has_selection) -> RightClickPlan {
    RightClickPlan plan;
    switch (action) {
        case RightClickAction::ContextMenu: {
            plan.intent = RightClickIntent::Menu;
            plan.items = {
                MenuItem{.command = MenuCommand::Copy, .enabled = has_selection},
                MenuItem{.command = MenuCommand::Paste},
            };
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
