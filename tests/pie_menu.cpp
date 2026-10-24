#include "odh/pie_menu.hpp"

#include <cassert>

int main() {
    using odh::PieMenuChoice;
    const auto menu = odh::make_pie_menu_model({
        PieMenuChoice{4, std::string("Food/Have A Snack")},
        PieMenuChoice{7, std::string("Food/Serve Food")},
        PieMenuChoice{9, std::string("Have A Snack")},
        PieMenuChoice{12, std::string("Food/Have A Snack")},
        PieMenuChoice{15, std::nullopt},
        PieMenuChoice{16, std::string("")},
    });

    assert(menu.size() == 3);
    assert(menu[0].label == "Food" && !menu[0].interaction_row);
    assert(menu[0].children.size() == 3);
    assert(menu[0].children[0].label == "Have A Snack" && menu[0].children[0].interaction_row == 4);
    assert(menu[0].children[1].label == "Serve Food" && menu[0].children[1].interaction_row == 7);
    assert(menu[0].children[2].label == "Have A Snack" && menu[0].children[2].interaction_row == 12);
    assert(menu[1].label == "Have A Snack" && menu[1].interaction_row == 9);
    assert(menu[2].label == "???" && menu[2].interaction_row == 15);
    assert(menu[2].children.size() == 1);
    assert(menu[2].children[0].label == "???" && menu[2].children[0].interaction_row == 16);

    const auto shared_path = odh::make_pie_menu_model({
        PieMenuChoice{2, std::string("Food")},
        PieMenuChoice{3, std::string("Food/Serve")},
    });
    assert(shared_path.size() == 1);
    assert(shared_path[0].label == "Food" && shared_path[0].interaction_row == 2);
    assert(shared_path[0].children.size() == 1);
    assert(shared_path[0].children[0].label == "Serve" && shared_path[0].children[0].interaction_row == 3);

    const auto layout = odh::layout_pie_menu(320, 240, {160, 120}, 4, true);
    assert(layout && layout->items.size() == 4 && layout->back_button);
    assert(layout->center[0] == 160 && layout->center[1] == 120);
    for (const auto& item : layout->items) {
        assert(item.x >= 0 && item.y >= 0);
        assert(item.x + item.width <= 320 && item.y + item.height <= 240);
        assert(!item.contains(layout->back_button->x + 1, layout->back_button->y + 1));
    }
    assert(!odh::layout_pie_menu(96, 80, {48, 40}, 1, false));
}
