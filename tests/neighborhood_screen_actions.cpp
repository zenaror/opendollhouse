#include "neighborhood_screen.hpp"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <cstdlib>
#include <chrono>
#include <exception>
#include <iostream>
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

template <typename Action>
void run_with_keys_after_showing(unsigned long window, std::vector<KeySym> symbols,
                                 Action&& action) {
    std::exception_ptr sender_error;
    std::thread sender([window, symbols = std::move(symbols), &sender_error] {
        try {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        if (!std::getenv("DISPLAY")) {
            std::cout << "X11 display unavailable: skipped\n";
            return 77;
        }
        odh_host::LinuxWindow window(800, 600, "OpenDollhouse neighborhood action test");
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

        std::cout << "Neighborhood move-out and confirmation keyboard paths passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
