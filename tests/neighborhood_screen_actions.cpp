#include "neighborhood_screen.hpp"
#include "original_name_prompt.hpp"
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
#include "linux_vm_dialog.hpp"
#endif

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
std::array<int,2> confirmation_button(const std::string& title,const std::string& message,
        std::uint8_t language,std::uint8_t response){
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    odh::DialogState state;state.instruction.operands[5]=1;
    state.prompt.title=title;state.prompt.message=message;
    const auto layout=odh_host::vm_message_dialog_layout(800,600,state,language);
    if(!layout)throw std::runtime_error("test confirmation has no layout");
    // Use the shared hit contract, rather than duplicating button spacing.
    for(int y=layout->ok_y;y<layout->ok_y+layout->ok_height;++y)
        for(int x=layout->x;x<layout->x+layout->width;++x)
            if(odh_host::vm_dialog_button_hit(*layout,1,x,y)==response)
                return {x+2,y+2};
    throw std::runtime_error("test confirmation button not found");
#else
    (void)title;(void)message;(void)language;
    return {response?420:300,335};
#endif
}

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

void hover(Display* display,unsigned long window,int x,int y){
    XEvent event{};event.xmotion.type=MotionNotify;event.xmotion.display=display;event.xmotion.window=window;
    event.xmotion.root=DefaultRootWindow(display);event.xmotion.same_screen=True;event.xmotion.x=x;event.xmotion.y=y;
    if(!XSendEvent(display,window,False,PointerMotionMask,&event))throw std::runtime_error("could not hover neighborhood lot");
    XSync(display,False);
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
                                      std::chrono::milliseconds(100),bool visit_first=false) {
    std::exception_ptr sender_error;
    std::thread sender([window, x, y, &sender_error, initial_delay,visit_first] {
        try {
            std::this_thread::sleep_for(initial_delay);
            std::unique_ptr<Display, decltype(&XCloseDisplay)> display(
                XOpenDisplay(nullptr), &XCloseDisplay);
            if (!display) throw std::runtime_error("cannot open synthetic X11 mouse sender");
            if(visit_first){
                // A bare Xvfb has no window manager to focus the new test
                // window. The production backend only tracks focused motion.
                XSetInputFocus(display.get(),window,RevertToParent,CurrentTime);
                XSync(display.get(),False);
                hover(display.get(),window,x,y);
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                hover(display.get(),window,x-2,y+1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
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

template<class Action>
void run_with_drag_then_cancel(unsigned long window,std::array<int,2> start,
        std::array<int,2> end,std::array<int,2> cancel,Action&& action){
    std::exception_ptr sender_error;
    std::thread sender([&]{try{
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::unique_ptr<Display,decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr),&XCloseDisplay);
        if(!display)throw std::runtime_error("cannot open drag sender");
        XSetInputFocus(display.get(),window,RevertToParent,CurrentTime);XSync(display.get(),False);
        click(display.get(),window,start[0],start[1],ButtonPress);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        hover(display.get(),window,end[0],end[1]);
        click(display.get(),window,end[0],end[1],ButtonRelease);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        click(display.get(),window,cancel[0],cancel[1],ButtonPress);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        click(display.get(),window,cancel[0],cancel[1],ButtonRelease);
    }catch(...){sender_error=std::current_exception();}});
    try{action();}catch(...){sender.join();throw;}
    sender.join();if(sender_error)std::rethrow_exception(sender_error);
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
        require(odh_host::neighborhood_mode_button_at(400,0)==2&&odh_host::neighborhood_mode_button_at(449,51)==2&&
                odh_host::neighborhood_mode_button_at(450,20)==3&&odh_host::neighborhood_mode_button_at(500,20)==5&&
                odh_host::neighborhood_mode_button_at(550,20)==7&&odh_host::neighborhood_mode_button_at(350,20)==0&&
                odh_host::neighborhood_mode_button_at(400,52)==0&&odh_host::neighborhood_arrow_at(110,18)==-1&&
                odh_host::neighborhood_arrow_at(150,18)==1&&odh_host::neighborhood_arrow_at(136,18)==0,
                "four travel buttons and the two neighborhood arrows occupy their original bounds");
        odh_host::PreparedNeighborhoodMap map;
        map.frame.width = 800;
        map.frame.height = 600;
        map.frame.pixels.assign(800 * 600, {32, 36, 44, 255});
        map.frame.depth.assign(800 * 600, std::numeric_limits<float>::infinity());
        map.mode = 1;
        odh_host::NeighborhoodPickerResult picker_result{};
        run_with_drag_then_cancel(window.native_id(),{225,20},{420,20},{620,20},[&]{
            const auto selected=odh_host::show_neighborhood_picker_with_actions(window,map,"Neighborhood","Cancel",1,true);
            require(!selected,"dragging from Families to Downtown must not invoke either button");
        });
        run_with_click_after_showing(window.native_id(), 420,20, [&] {
            const auto selected = odh_host::show_neighborhood_picker_with_actions(
                window, map, "Neighborhood", "Cancel", 1, false);
            require(selected.has_value(), "map mode click must return a selection");
            picker_result = *selected;
        });
        require(picker_result.action == odh_host::NeighborhoodPickerAction::switch_mode &&
                picker_result.mode == 2,
                "clicking the Downtown map button must request mode 2");

        run_with_click_after_showing(window.native_id(),225,22,[&]{
            const auto selected=odh_host::show_neighborhood_picker_with_actions(window,map,"Neighborhood","Cancel",1,true);
            require(selected&&selected->action==odh_host::NeighborhoodPickerAction::select_family,
                "Family control opens the family picker without entering CAS");
        });
        map.region_available[2]=false;
        run_with_clicks_after_showing(window.native_id(),{{420,20},{225,22}},[&]{
            const auto selected=odh_host::show_neighborhood_picker_with_actions(window,map,"Neighborhood","Cancel",1,true);
            require(selected&&selected->action==odh_host::NeighborhoodPickerAction::select_family,
                "disabled region consumes the click without changing regions");
        });
        map.region_available[2]=true;
        {
            auto pictured=map;
            pictured.markers={{{5,320,240},true,false},{{6,335,250},true,false}};
            pictured.houses.resize(2);pictured.houses[0].house=5;pictured.houses[0].hit={272,216,96,48};
            pictured.houses[1].house=6;pictured.houses[1].hit={311,238,48,24};
            require(odh_host::pick_map_lot(pictured,280,220)==5&&odh_host::pick_map_lot(pictured,323,244)==6&&
                !odh_host::pick_map_lot(pictured,250,220),"lot artwork hotspot and overlapping house priority match rendering");
        }
        {
            auto occupied=map;
            occupied.markers.push_back({{5,320,240},true,true});
            occupied.markers.push_back({{6,410,270},true,false});
            odh::FamilyEntry residents{};residents.family.house_number=5;residents.family.family_number=6;
            residents.display_name="Example";occupied.effective_families.push_back(residents);
            unsigned requested=0;
            run_with_click_after_showing(window.native_id(),320,240,[&]{
                const auto picked=odh_host::show_neighborhood_picker_with_actions(window,occupied,"Neighborhood","Cancel",1,false,{},
                    [&](std::uint16_t house){
                        ++requested;require(house==5,"metadata request matches actual occupied lot");
                        return odh_host::NeighborhoodHoverText{"5 Test Street","Example"};
                    });
                if(!picked||picked->action!=odh_host::NeighborhoodPickerAction::lot||picked->house_number!=5||requested!=1)
                    throw std::runtime_error("occupied hover/click mismatch: metadata reads="+std::to_string(requested)+
                        " house="+std::to_string(picked?picked->house_number:0)+
                        " action="+std::to_string(picked?static_cast<int>(picked->action):-1));
            },std::chrono::milliseconds(150),true);
            run_with_clicks_after_showing(window.native_id(),{{320,22},{410,270},{320,240}},[&]{
                const auto picked=odh_host::show_neighborhood_picker_with_actions(window,occupied,"Neighborhood","Cancel",1,false);
                require(picked&&picked->action==odh_host::NeighborhoodPickerAction::manage_lot&&picked->house_number==5,
                    "explicit management consumes vacant lot and opens occupied family management");
            });
        }
        map.markers.push_back({{5, 320, 240}, true, false});
        std::optional<odh_host::NeighborhoodMapRequest> resume_request;
        run_with_keys_after_showing(window.native_id(), {XK_e}, [&] {
            resume_request = odh_host::show_neighborhood_resume_picker(window, map, 5);
        });
        require(resume_request &&
                resume_request->kind == odh_host::NeighborhoodRequestKind::manage_families &&
                resume_request->destination_house == 0,
                "E on the live-lot map must request family management without a travel destination");
        run_with_click_after_showing(window.native_id(), 180, 568, [&] {
            resume_request = odh_host::show_neighborhood_resume_picker(window, map, 5);
        });
        require(resume_request &&
                resume_request->kind == odh_host::NeighborhoodRequestKind::manage_families &&
                resume_request->destination_house == 0,
                "the live-lot map management button must return management intent");
        run_with_click_after_showing(window.native_id(), 320, 240, [&] {
            resume_request = odh_host::show_neighborhood_resume_picker(window, map, 5);
        });
        require(resume_request &&
                resume_request->kind == odh_host::NeighborhoodRequestKind::travel &&
                resume_request->destination_house == 5,
                "clicking the active house must retain its travel selection for same-lot resume");
        map.markers.push_back({{9, 180, 568}, true, false});
        run_with_click_after_showing(window.native_id(), 180, 568, [&] {
            resume_request = odh_host::show_neighborhood_resume_picker(window, map, 5);
        });
        require(resume_request &&
                resume_request->kind == odh_host::NeighborhoodRequestKind::travel &&
                resume_request->destination_house == 9,
                "a lot beneath the previous management-button position must remain selectable for travel");
        odh_host::LinuxWindow resume_cancel_window(800, 600,
            "OpenDollhouse live-lot map cancellation test");
        run_with_keys_after_showing(resume_cancel_window.native_id(), {XK_Escape}, [&] {
            resume_request = odh_host::show_neighborhood_resume_picker(resume_cancel_window, map, 5);
        });
        require(!resume_request,
                "Escape on the live-lot map must return no management or travel request");

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

        require(window.quit_reason()==odh_host::WindowQuitReason::none,
                "Escape in management confirmation is consumed by the modal");
        const auto buy_no=confirmation_button("Purchase House?",
            "Are you sure you want to buy this house?",1,1);
        run_with_clicks_after_showing(window.native_id(),{{10,10},buy_no},[&]{
            confirmed=odh_host::show_move_in_confirmation(window,entry,6,1000,1);
        });
        require(!confirmed,"outside click must not buy a house; No cancels the purchase");
        run_with_keys_after_showing(window.native_id(),{XK_Return},[&]{
            confirmed=odh_host::show_move_in_confirmation(window,entry,6,1000,1);
        });
        require(confirmed,"Enter confirms a valid displayed purchase");

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
        run_with_keys_after_showing(family_picker_window.native_id(), {XK_r}, [&] {
            family_selection = odh_host::show_move_in_family_picker(family_picker_window, poor_families,
                5, 1000, 14);
        });
        require(family_selection && family_selection->family_chunk_id == poor_families[0].chunk_id &&
                family_selection->family_identity == odh::family_overlay_identity(poor_families[0]) &&
                family_selection->action == odh_host::MoveInFamilySelection::Action::rename,
                "rename shortcut must select an eligible household even when it cannot afford the house");
        run_with_click_after_showing(family_picker_window.native_id(), 100, 490, [&] {
            family_selection = odh_host::show_move_in_family_picker(family_picker_window, poor_families,
                5, 1000, 14);
        });
        require(family_selection && family_selection->action == odh_host::MoveInFamilySelection::Action::rename,
                "rename caption must select an eligible household independently of move-in affordability");
        auto townie_family = poor_family; townie_family.family_number = -1;
        const std::vector<odh::FamilyEntry> townie_families{{20, townie_family}};
        run_with_keys_after_showing(family_picker_window.native_id(), {XK_r, XK_Escape}, [&] {
            family_selection = odh_host::show_move_in_family_picker(family_picker_window, townie_families,
                5, 1000, 14);
        });
        require(!family_selection,
                "rename shortcut must ignore the townie sentinel until Escape cancels the picker");
        odh::Family affordable_family;
        affordable_family.family_number = 44;
        affordable_family.house_number = 0;
        affordable_family.budget = 20000;
        affordable_family.value_in_arch = 3000;
        affordable_family.family_friends = 7;
        affordable_family.member_guids = {0x32345678, 0x42345678};
        const odh::FamilyEntry affordable_entry{19, affordable_family, "Família Teste"};
        const std::vector<odh::FamilyEntry> affordable_families{affordable_entry};
        run_with_drag_then_cancel(family_picker_window.native_id(),{350,560},{550,560},{180,560},[&]{
            family_selection=odh_host::show_move_in_family_picker(family_picker_window,affordable_families,5,1000,1);
            require(!family_selection,"dragging from Create to Move In must not create or select a family");
        });
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
        run_with_click_after_showing(family_picker_window.native_id(), 180, 560, [&] {
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
                "delete button returns a logical deletion action for an eligible family");
        const auto archive_yes=confirmation_button("Excluir Família?",
            "Tem certeza que deseja excluir esta família?",14,0);
        const auto archive_no=confirmation_button("Excluir Família?",
            "Tem certeza que deseja excluir esta família?",14,1);
        const auto before_confirmation=family_picker_window.readback();
        run_with_click_after_showing(family_picker_window.native_id(), archive_yes[0], archive_yes[1], [&] {
            const auto confirmed = odh_host::show_family_archive_confirmation(
                family_picker_window, affordable_entry, 14, "Excluir Família?",
                "Tem certeza que deseja excluir esta família?", "Sim", "Não", ui_graphics);
            family_selection = confirmed ? std::optional<odh_host::MoveInFamilySelection>{
                odh_host::MoveInFamilySelection{affordable_entry.chunk_id,
                    odh::family_overlay_identity(affordable_entry),
                    odh_host::MoveInFamilySelection::Action::archive}} : std::nullopt;
        }, std::chrono::milliseconds(300));
        require(family_selection.has_value(),
                "delete confirmation yes button accepts the selected family");
        require(family_picker_window.readback()==before_confirmation,
                "confirming a management dialog restores the unchanged family-picker backdrop");
        run_with_clicks_after_showing(family_picker_window.native_id(), {{10,10},archive_no}, [&] {
            const auto confirmed = odh_host::show_family_archive_confirmation(
                family_picker_window, affordable_entry, 14, "Excluir Família?",
                "Tem certeza que deseja excluir esta família?", "Sim", "Não", ui_graphics);
            family_selection = confirmed ? std::optional<odh_host::MoveInFamilySelection>{
                odh_host::MoveInFamilySelection{affordable_entry.chunk_id,
                    odh::family_overlay_identity(affordable_entry),
                    odh_host::MoveInFamilySelection::Action::archive}} : std::nullopt;
        }, std::chrono::milliseconds(300));
        require(!family_selection,
                "outside click is ignored and delete confirmation No cancels without a family selection");
        run_with_click_after_showing(family_picker_window.native_id(),358,556,[&]{
            family_selection=odh_host::show_move_in_family_picker(family_picker_window,{},0,0,1);
        });
        require(family_selection&&family_selection->action==odh_host::MoveInFamilySelection::Action::create_family,
            "Add opens creation even with no unhoused families");
        run_with_clicks_after_showing(family_picker_window.native_id(),{{550,560},{200,99},{550,560}},[&]{
            family_selection=odh_host::show_move_in_family_picker(family_picker_window,affordable_families,0,0,1);
        });
        require(family_selection&&family_selection->action==odh_host::MoveInFamilySelection::Action::move_in&&
            family_selection->family_identity==odh::family_overlay_identity(affordable_entry),
            "family-first move-in requires selecting the actual household before choosing a house");
        run_with_keys_after_showing(family_picker_window.native_id(),{XK_Tab,XK_r,XK_Escape},[&]{
            family_selection=odh_host::show_move_in_family_picker(family_picker_window,affordable_families,0,0,1);
        });
        require(!family_selection,"R cannot rename from the global picker where rename is not exposed");
        const std::vector<odh::FamilyEntry> no_families;
        run_with_keys_after_showing(family_picker_window.native_id(), {XK_Escape}, [&] {
            family_selection = odh_host::show_move_in_family_picker(family_picker_window, no_families,
                5, 1000, 14, "Move In Family", "SELECT A FAMILY", "Sorry",
                "You can't afford this house.", "Cancel", ui_graphics);
        }, std::chrono::milliseconds(1000));
        require(!family_selection,
                "Escape must close the no-family-available screen");

        odh_host::LinuxWindow name_window(800,600,"OpenDollhouse family name modal test");
        std::optional<std::string> renamed;
        const auto edit_name=[&](const std::string& initial){
            renamed=odh_host::show_family_name_editor(name_window,initial,14,
                "Renomear família","Feito","Cancelar",ui_graphics);
        };
        run_with_keys_after_showing(name_window.native_id(),{XK_n,XK_o,XK_v,XK_a,XK_Return},[&]{edit_name("Original");});
        require(renamed==std::optional<std::string>{"nova"},"first name input must replace only the editor draft");
        run_with_keys_after_showing(name_window.native_id(),{XK_x,XK_Escape},[&]{edit_name("Original");});
        require(!renamed,"Escape must discard typed family name draft");
        run_with_keys_after_showing(name_window.native_id(),{XK_Tab,XK_Tab,XK_Return},[&]{edit_name("Original");});
        require(!renamed,"keyboard focus on Cancel must not confirm a name");
        run_with_keys_after_showing(name_window.native_id(),{XK_Return,XK_Escape},[&]{edit_name("   ");});
        require(!renamed,"blank family name must remain a draft until cancelled");
        run_with_keys_after_showing(name_window.native_id(),{XK_BackSpace,XK_Return,XK_Escape},[&]{edit_name("Original");});
        require(!renamed,"clearing the selected name must render an empty draft, reject confirmation and remain cancellable");
        run_with_keys_after_showing(name_window.native_id(),{XK_Return,XK_Escape},[&]{edit_name(std::string(257,'a'));});
        require(!renamed,"family name exceeding legacy byte limit must not confirm");
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
        run_with_keys_after_showing(name_window.native_id(),{XK_Return,XK_Escape},[&]{edit_name(u8"Família 😀");});
        require(!renamed,"unrepresentable family name must not silently replace characters");
        run_with_keys_after_showing(name_window.native_id(),{XK_Return},[&]{edit_name(u8"  Família Açúcar  ");});
        require(renamed==std::optional<std::string>{u8"Família Açúcar"},"supported translated family name must roundtrip and trim");
#endif
        const auto name_layout=odh_host::original_name_prompt_layout(800,600);
        const auto point=[](const odh::UiBitmapRect& b){return std::array<int,2>{b.x+b.width/2,b.y+b.height/2};};
        run_with_drag_then_cancel(name_window.native_id(),point(name_layout.buttons[0]),point(name_layout.buttons[1]),
            point(name_layout.buttons[1]),[&]{edit_name("Original");});
        require(!renamed,"dragging Accept to Cancel must not accept the draft");
        run_with_click_after_showing(name_window.native_id(),point(name_layout.buttons[0])[0],point(name_layout.buttons[0])[1],
            [&]{edit_name("Original");});
        require(renamed==std::optional<std::string>{"Original"},"matching press/release must confirm the current family draft");

        std::cout << "Neighborhood action and family-picker input paths passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
