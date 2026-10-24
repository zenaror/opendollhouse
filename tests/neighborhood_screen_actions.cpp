#include "neighborhood_screen.hpp"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <array>
#include <cstdlib>
#include <chrono>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void key(Display* display, unsigned long window, KeySym symbol, int type) {
    XEvent event{};
    event.xkey.type = type;
    event.xkey.display = display;
    event.xkey.window = window;
    event.xkey.root = DefaultRootWindow(display);
    event.xkey.same_screen = True;
    event.xkey.keycode = XKeysymToKeycode(display, symbol);
    const long mask = type == KeyPress ? KeyPressMask : KeyReleaseMask;
    if (!event.xkey.keycode || !XSendEvent(display, window, False, mask, &event))
        throw std::runtime_error("could not send synthetic key to neighborhood window");
    XSync(display, False);
}

void click(Display* display, unsigned long window, int x, int y, int type) {
    XEvent event{};
    event.xbutton.type = type;
    event.xbutton.display = display;
    event.xbutton.window = window;
    event.xbutton.root = DefaultRootWindow(display);
    event.xbutton.same_screen = True;
    event.xbutton.button = Button1;
    event.xbutton.x = x;
    event.xbutton.y = y;
    const long mask = type == ButtonPress ? ButtonPressMask : ButtonReleaseMask;
    if (!XSendEvent(display, window, False, mask, &event))
        throw std::runtime_error("could not send synthetic mouse click to neighborhood window");
    XSync(display, False);
}

template <typename Action>
void run_with_keys_after_showing(unsigned long window, std::vector<KeySym> symbols,
                                 Action&& action,
                                 std::chrono::milliseconds initial_delay =
                                     std::chrono::milliseconds(100)) {
    std::exception_ptr sender_error;
    std::thread sender([window, symbols = std::move(symbols), &sender_error, initial_delay] {
        try {
            std::this_thread::sleep_for(initial_delay);
            std::unique_ptr<Display, decltype(&XCloseDisplay)> display(
                XOpenDisplay(nullptr), &XCloseDisplay);
            if (!display) throw std::runtime_error("cannot open synthetic X11 sender");
            for (const auto symbol : symbols) {
                key(display.get(), window, symbol, KeyPress);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                key(display.get(), window, symbol, KeyRelease);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } catch (...) {
            sender_error = std::current_exception();
        }
    });
    try {
        action();
    } catch (...) {
        sender.join();
        throw;
    }
    sender.join();
    if (sender_error) std::rethrow_exception(sender_error);
}

template <typename Action>
void run_with_click_after_showing(unsigned long window, int x, int y, Action&& action,
                                  std::chrono::milliseconds initial_delay =
                                      std::chrono::milliseconds(100)) {
    std::exception_ptr sender_error;
    std::thread sender([window, x, y, &sender_error, initial_delay] {
        try {
            std::this_thread::sleep_for(initial_delay);
            std::unique_ptr<Display, decltype(&XCloseDisplay)> display(
                XOpenDisplay(nullptr), &XCloseDisplay);
            if (!display) throw std::runtime_error("cannot open synthetic X11 mouse sender");
            click(display.get(), window, x, y, ButtonPress);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            click(display.get(), window, x, y, ButtonRelease);
        } catch (...) {
            sender_error = std::current_exception();
        }
    });
    try {
        action();
    } catch (...) {
        sender.join();
        throw;
    }
    sender.join();
    if (sender_error) std::rethrow_exception(sender_error);
}

template <typename Action>
void run_with_clicks_after_showing(unsigned long window,
        std::vector<std::array<int,2>> points, Action&& action,
        std::chrono::milliseconds initial_delay = std::chrono::milliseconds(100)) {
    std::exception_ptr sender_error;
    std::thread sender([window, points = std::move(points), &sender_error, initial_delay] {
        try {
            std::this_thread::sleep_for(initial_delay);
            std::unique_ptr<Display, decltype(&XCloseDisplay)> display(
                XOpenDisplay(nullptr), &XCloseDisplay);
            if (!display) throw std::runtime_error("cannot open synthetic X11 mouse sender");
            for (const auto& point : points) {
                click(display.get(), window, point[0], point[1], ButtonPress);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                click(display.get(), window, point[0], point[1], ButtonRelease);
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
            }
        } catch (...) {
            sender_error = std::current_exception();
        }
    });
    try {
        action();
    } catch (...) {
        sender.join();
        throw;
    }
    sender.join();
    if (sender_error) std::rethrow_exception(sender_error);
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    try {
        std::filesystem::path ui_graphics;
        if (argc == 3 && std::string(argv[1]) == "--ui-graphics") ui_graphics = argv[2];
        else if (argc != 1) throw std::runtime_error("usage: neighborhood screen actions [--ui-graphics FAR]");
        if (!std::getenv("DISPLAY")) {
            std::cout << "X11 display unavailable: skipped\n";
            return 77;
        }
        Display* availability = XOpenDisplay(nullptr);
        if (!availability) {
            std::cout << "X11 DISPLAY inaccessible: skipped\n";
            return 77;
        }
        XCloseDisplay(availability);
        odh_host::LinuxWindow window(800, 600, "OpenDollhouse neighborhood action test");
        require(odh_host::neighborhood_mode_button_at(24, 520) == 1,
                "first neighborhood tab must select map mode 1");
        require(odh_host::neighborhood_mode_button_at(16 + 3 * 128 + 8, 520) == 4,
                "fourth neighborhood tab must select map mode 4");
        require(odh_host::neighborhood_mode_button_at(16 + 126, 520) == 0,
                "gap between neighborhood tabs must not select a map");
        require(odh_host::neighborhood_mode_button_at(24, 507) == 0,
                "click above neighborhood tabs must not select a map");

        odh_host::PreparedNeighborhoodMap map;
        map.frame.width = 800;
        map.frame.height = 600;
        map.frame.pixels.assign(800 * 600, {32, 36, 44, 255});
        map.frame.depth.assign(800 * 600, std::numeric_limits<float>::infinity());
        map.mode = 1;
        odh_host::NeighborhoodPickerResult picker_result{};
        run_with_click_after_showing(window.native_id(), 16 + 3 * 128 + 8, 520, [&] {
            const auto selected = odh_host::show_neighborhood_picker_with_actions(
                window, map, "Neighborhood", "Cancel", 1, false);
            require(selected.has_value(), "map mode click must return a selection");
            picker_result = *selected;
        });
        require(picker_result.action == odh_host::NeighborhoodPickerAction::switch_mode &&
                picker_result.mode == 4,
                "clicking the Old Town map tab must request mode 4");

        map.markers.push_back({{5, 320, 240}, true, false});
        auto panel_choice = odh_host::NeighborhoodLotPanelChoice::back_to_map;
        run_with_click_after_showing(window.native_id(), 70, 482, [&] {
            panel_choice = odh_host::show_neighborhood_lot_panel(window, map, 5,
                "5 Sim Lane", "A house for your family.", "Cost: 12,000",
                "OK", "Cancel", 1);
        });
        require(panel_choice == odh_host::NeighborhoodLotPanelChoice::continue_to_lot,
                "lot details OK button must continue to the existing lot flow");
        run_with_keys_after_showing(window.native_id(), {XK_Return}, [&] {
            panel_choice = odh_host::show_neighborhood_lot_panel(window, map, 5,
                "5 Sim Lane", "A house for your family.", "Cost: 12,000",
                "OK", "Cancel", 1);
        });
        require(panel_choice == odh_host::NeighborhoodLotPanelChoice::continue_to_lot,
                "Enter in lot details must continue to the existing lot flow");
        run_with_keys_after_showing(window.native_id(), {XK_Escape}, [&] {
            panel_choice = odh_host::show_neighborhood_lot_panel(window, map, 5,
                "5 Sim Lane", "A house for your family.", "Cost: 12,000",
                "OK", "Cancel", 1);
        });
        require(panel_choice == odh_host::NeighborhoodLotPanelChoice::back_to_map &&
                window.quit_reason() == odh_host::WindowQuitReason::none,
                "Escape in lot details must return to the neighborhood map");
        run_with_click_after_showing(window.native_id(), 250, 482, [&] {
            panel_choice = odh_host::show_neighborhood_lot_panel(window, map, 5,
                "5 Sim Lane", "A house for your family.", "Cost: 12,000",
                "OK", "Cancel", 1);
        });
        require(panel_choice == odh_host::NeighborhoodLotPanelChoice::back_to_map,
                "lot details Cancel button must return to the neighborhood map");

        odh::Family family;
        family.family_number = 42;
        family.house_number = 5;
        family.budget = 7000;
        family.value_in_arch = 2300;
        family.member_guids = {0x12345678};
        const odh::FamilyEntry entry{17, family};

        odh_host::OccupiedLotChoice choice{};
        run_with_keys_after_showing(window.native_id(), {XK_e}, [&] {
            choice = odh_host::show_occupied_lot_choice(window, entry, 5, true, 1);
        });
        std::cerr << "synthetic E returned occupied-lot choice "
                  << static_cast<int>(choice) << " (move_out=1, enter=0, cancel=2)\n";
        require(choice == odh_host::OccupiedLotChoice::move_out,
                "E must select move-out when the refund has been verified");

        run_with_keys_after_showing(window.native_id(), {XK_Return}, [&] {
            choice = odh_host::show_occupied_lot_choice(window, entry, 5, true, 1);
        });
        require(choice == odh_host::OccupiedLotChoice::enter,
                "Enter must continue into the occupied house");

        bool confirmed = false;
        run_with_keys_after_showing(window.native_id(), {XK_Return}, [&] {
            confirmed = odh_host::show_move_out_confirmation(window, entry, 5, 1);
        });
        require(confirmed,
                "Enter must confirm the displayed move-out and refund");

        run_with_keys_after_showing(window.native_id(), {XK_Escape}, [&] {
            confirmed = odh_host::show_move_out_confirmation(window, entry, 5, 1);
        });
        require(!confirmed,
                "Escape must cancel the move-out confirmation");

        odh_host::LinuxWindow unavailable_window(800, 600,
            "OpenDollhouse unsupported move-out choice test");
        run_with_keys_after_showing(unavailable_window.native_id(), {XK_e, XK_Escape}, [&] {
            choice = odh_host::show_occupied_lot_choice(unavailable_window, entry, 5, false, 1);
        });
        require(choice == odh_host::OccupiedLotChoice::cancel,
                "E must not enable an unverified move-out");

        std::optional<odh_host::MoveInFamilySelection> family_selection;
        odh_host::LinuxWindow family_picker_window(800, 600,
            "OpenDollhouse family picker action test");
        odh::Family poor_family;
        poor_family.family_number = 43;
        poor_family.house_number = 0;
        poor_family.budget = 100;
        poor_family.member_guids = {0x22345678};
        const std::vector<odh::FamilyEntry> poor_families{{18, poor_family}};
        run_with_keys_after_showing(family_picker_window.native_id(), {XK_Escape}, [&] {
            family_selection = odh_host::show_move_in_family_picker(family_picker_window, poor_families,
                5, 1000, 14, "Selecionar Esta Família", "SELECIONAR FAMÍLIA",
                "Desculpe!", "Muito cara. Você não consegue pagar.", "Cancelar", ui_graphics);
        }, std::chrono::milliseconds(1000));
        require(!family_selection,
                "Escape must close the localized no-affordable-family screen");
        odh::Family affordable_family;
        affordable_family.family_number = 44;
        affordable_family.house_number = 0;
        affordable_family.budget = 20000;
        affordable_family.value_in_arch = 3000;
        affordable_family.family_friends = 7;
        affordable_family.member_guids = {0x32345678, 0x42345678};
        const odh::FamilyEntry affordable_entry{19, affordable_family, "Família Teste"};
        const std::vector<odh::FamilyEntry> affordable_families{affordable_entry};
        run_with_click_after_showing(family_picker_window.native_id(), 550, 560, [&] {
            family_selection = odh_host::show_move_in_family_picker(family_picker_window,
                affordable_families, 5, 1000, 14, "Selecionar Esta Família",
                "SELECIONAR FAMÍLIA", "Desculpe!", "Muito cara.", "Cancelar", ui_graphics);
        }, std::chrono::milliseconds(300));
        require(family_selection.has_value(),
                "original-style family action button must return a selection");
        require(family_selection->family_chunk_id == affordable_entry.chunk_id &&
                family_selection->family_identity == odh::family_overlay_identity(affordable_entry),
                "original-style family action button must select the affordable family identity");
        run_with_click_after_showing(family_picker_window.native_id(), 360, 560, [&] {
            family_selection = odh_host::show_move_in_family_picker(family_picker_window,
                affordable_families, 5, 1000, 14, "Selecionar Esta Família",
                "SELECIONAR FAMÍLIA", "Desculpe!", "Muito cara.", "Cancelar", ui_graphics);
        }, std::chrono::milliseconds(300));
        require(!family_selection, "family picker cancel button must return without selection");
        run_with_clicks_after_showing(family_picker_window.native_id(), {{250, 130}, {550, 560}}, [&] {
            family_selection = odh_host::show_move_in_family_picker(family_picker_window,
                affordable_families, 5, 1000, 14, "Selecionar Esta Família",
                "SELECIONAR FAMÍLIA", "Desculpe!", "Muito cara.", "Cancelar", ui_graphics);
        }, std::chrono::milliseconds(300));
        require(family_selection && family_selection->family_chunk_id == affordable_entry.chunk_id,
                "clicking an original-style family row and move-in button must select that family");
        require(family_selection->action == odh_host::MoveInFamilySelection::Action::move_in,
                "move-in button returns a move-in action");
        run_with_click_after_showing(family_picker_window.native_id(), 420, 560, [&] {
            family_selection = odh_host::show_move_in_family_picker(family_picker_window,
                affordable_families, 5, 1000, 14, "Selecionar Esta Família",
                "SELECIONAR FAMÍLIA", "Desculpe!", "Muito cara.", "Cancelar", ui_graphics,
                "Patrimônio", "Amigos", "Excluir Família", {affordable_entry.chunk_id});
        }, std::chrono::milliseconds(300));
        require(family_selection && family_selection->family_chunk_id == affordable_entry.chunk_id &&
                family_selection->action == odh_host::MoveInFamilySelection::Action::archive,
                "delete button returns a logical archive action for a player-created family");
        run_with_click_after_showing(family_picker_window.native_id(), 580, 560, [&] {
            const auto confirmed = odh_host::show_family_archive_confirmation(
                family_picker_window, affordable_entry, 14, "Excluir Família?",
                "Tem certeza que deseja excluir esta família?", "Sim", "Não", ui_graphics);
            family_selection = confirmed ? std::optional<odh_host::MoveInFamilySelection>{
                odh_host::MoveInFamilySelection{affordable_entry.chunk_id,
                    odh::family_overlay_identity(affordable_entry),
                    odh_host::MoveInFamilySelection::Action::archive}} : std::nullopt;
        }, std::chrono::milliseconds(300));
        require(family_selection.has_value(),
                "archive confirmation yes button accepts the selected family");
        run_with_click_after_showing(family_picker_window.native_id(), 460, 560, [&] {
            const auto confirmed = odh_host::show_family_archive_confirmation(
                family_picker_window, affordable_entry, 14, "Excluir Família?",
                "Tem certeza que deseja excluir esta família?", "Sim", "Não", ui_graphics);
            family_selection = confirmed ? std::optional<odh_host::MoveInFamilySelection>{
                odh_host::MoveInFamilySelection{affordable_entry.chunk_id,
                    odh::family_overlay_identity(affordable_entry),
                    odh_host::MoveInFamilySelection::Action::archive}} : std::nullopt;
        }, std::chrono::milliseconds(300));
        require(!family_selection,
                "archive confirmation no button cancels without a family selection");
        const std::vector<odh::FamilyEntry> no_families;
        run_with_keys_after_showing(family_picker_window.native_id(), {XK_Escape}, [&] {
            family_selection = odh_host::show_move_in_family_picker(family_picker_window, no_families,
                5, 1000, 14, "Move In Family", "SELECT A FAMILY", "Sorry",
                "You can't afford this house.", "Cancel", ui_graphics);
        }, std::chrono::milliseconds(1000));
        require(!family_selection,
                "Escape must close the no-family-available screen");

        std::cout << "Neighborhood action and family-picker input paths passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
