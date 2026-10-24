#include "linux_window.hpp"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <memory>
int main(int argc,char** argv){try{
    using odh_host::WindowQuitReason;
    const WindowQuitReason reasons[]{WindowQuitReason::none,WindowQuitReason::escape_key,
        WindowQuitReason::window_manager_close,WindowQuitReason::window_destroyed};
    const char* names[]{"none","escape_key","window_manager_close","window_destroyed"};
    if(odh_host::WindowInput{}.quit||odh_host::WindowInput{}.quit_reason!=WindowQuitReason::none)
        throw std::runtime_error("fresh input reports a quit");
    for(unsigned i=0;i<4;++i){
        if(std::string(odh_host::window_quit_reason_name(reasons[i]))!=names[i])
            throw std::runtime_error("quit diagnostic name differs");
        for(const auto incoming:reasons)
            if(odh_host::record_window_quit_reason(reasons[i],incoming)!=
                (reasons[i]==WindowQuitReason::none?incoming:reasons[i]))
                throw std::runtime_error("first quit reason was overwritten");
    }
    if(odh_host::WindowInput{}.create_family ||
       !odh_host::window_create_family_shortcut(XK_c,false) ||
       !odh_host::window_create_family_shortcut(XK_C,false) ||
       odh_host::window_create_family_shortcut(XK_c,true) ||
       odh_host::window_create_family_shortcut(XK_C,true) ||
       odh_host::window_create_family_shortcut(XK_x,false))
        throw std::runtime_error("create family shortcut mapping differs");
    if(odh_host::WindowInput{}.confirm ||
       !odh_host::window_confirm_key(XK_Return,false) ||
       !odh_host::window_confirm_key(XK_KP_Enter,false) ||
       odh_host::window_confirm_key(XK_Return,true) ||
       odh_host::window_confirm_key(XK_space,false))
        throw std::runtime_error("form confirmation key mapping differs");
    odh_host::WindowInput text;
    if(text.text_backspace || !text.text_input_utf8.empty())
        throw std::runtime_error("fresh text input differs");
    if(!odh_host::append_window_text_utf8(text,"Rafael ") ||
       !odh_host::append_window_text_utf8(text,u8"João 日本 😀") ||
       text.text_input_utf8!=u8"Rafael João 日本 😀")
        throw std::runtime_error("printable Unicode commit differs");
    const auto original_text=text.text_input_utf8;
    const std::string invalid_text[]{std::string("a\0b",3),"\n","\t","\x7f",
        "\xc2\x85","\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80",
        "\xe2\x82","\x80","\xe2" "ab"};
    for(const auto& value:invalid_text)
        if(odh_host::append_window_text_utf8(text,value)||text.text_input_utf8!=original_text)
            throw std::runtime_error("invalid/control commit accepted or partially appended");
    odh_host::WindowInput bounded;
    if(!odh_host::append_window_text_utf8(bounded,std::string(4095,'a')) ||
       odh_host::append_window_text_utf8(bounded,u8"é") ||
       bounded.text_input_utf8.size()!=4095 ||
       !odh_host::append_window_text_utf8(bounded,"b") ||
       odh_host::append_window_text_utf8(bounded,"c") || bounded.text_input_utf8.size()!=4096)
        throw std::runtime_error("UTF-8 commit budget differs");
    if(argc==2&&std::string(argv[1])=="--input-only"){
        std::cout<<"Window input: quit diagnostics and bounded printable UTF-8 commits passed without X11.\n";return 0;
    }
    if(!std::getenv("DISPLAY")){std::cout<<"X11 display unavailable: skipped\n";return 77;}
    Display* availability=XOpenDisplay(nullptr);if(!availability){std::cout<<"X11 DISPLAY inaccessible: skipped\n";return 77;}XCloseDisplay(availability);
    odh_host::LinuxWindow window(128,96,"OpenDollhouse own-window backend test");
    odh::RasterFrame frame{128,96,{}, {}};
    frame.pixels.resize(128*96);frame.depth.resize(128*96,std::numeric_limits<float>::infinity());
    for(std::size_t i=0;i<frame.pixels.size();++i)frame.pixels[i]={std::uint8_t((i&1)?255:0),std::uint8_t((i&2)?255:0),std::uint8_t((i&4)?255:0),255};
    window.present(frame);if(window.readback()!=frame.pixels)throw std::runtime_error("native RGB readback differs");
    auto invalid=frame;invalid.pixels.pop_back();bool rejected=false;try{window.present(invalid);}catch(const odh::FormatError&){rejected=true;}if(!rejected)throw std::runtime_error("short image accepted");
    invalid=frame;invalid.pixels[0][3]=0;rejected=false;try{window.present(invalid);}catch(const odh::FormatError&){rejected=true;}if(!rejected)throw std::runtime_error("transparent frame accepted");
    const auto original_window_id=window.native_id();
    window.resize(64,48);
    if(window.width()!=64||window.height()!=48||window.native_id()!=original_window_id)
        throw std::runtime_error("resize did not preserve the native window identity and requested size");
    odh::RasterFrame resized{64,48,{}, {}};
    resized.pixels.resize(64*48);resized.depth.resize(64*48,std::numeric_limits<float>::infinity());
    for(std::size_t i=0;i<resized.pixels.size();++i)
        resized.pixels[i]={std::uint8_t((i&1)?32:224),std::uint8_t((i&2)?64:192),
                           std::uint8_t((i&4)?96:160),255};
    window.present(resized);
    if(window.readback()!=resized.pixels)throw std::runtime_error("resized native RGB readback differs");
    rejected=false;try{window.resize(0,48);}catch(const odh::FormatError&){rejected=true;}
    if(!rejected||window.width()!=64||window.height()!=48||window.readback()!=resized.pixels)
        throw std::runtime_error("invalid resize changed the active viewport or image buffer");
    window.resize(128,96);
    if(window.width()!=128||window.height()!=96||window.native_id()!=original_window_id)
        throw std::runtime_error("resize back to original viewport failed");
    window.present(frame);
    if(window.readback()!=frame.pixels)throw std::runtime_error("restored viewport RGB readback differs");
    window.poll();
    std::unique_ptr<Display,decltype(&XCloseDisplay)> sender(XOpenDisplay(nullptr),&XCloseDisplay);
    if(!sender)throw std::runtime_error("cannot open own-window test sender");
    const std::string localized_title="OpenDollhouse — Sofás ポーズ 暂停";
    window.set_title(localized_title);
    (void)window.readback(); // Round-trip on the same X connection before the second client queries the property.
    const Atom utf8_name=XInternAtom(sender.get(),"_NET_WM_NAME",False);
    const Atom utf8_type=XInternAtom(sender.get(),"UTF8_STRING",False);
    Atom actual_type=None;int actual_format=0;unsigned long item_count=0,bytes_after=0;unsigned char* property=nullptr;
    if(XGetWindowProperty(sender.get(),window.native_id(),utf8_name,0,4096,False,utf8_type,
        &actual_type,&actual_format,&item_count,&bytes_after,&property)!=Success||
        actual_type!=utf8_type||actual_format!=8||bytes_after!=0||item_count!=localized_title.size()||
        !property||std::string(reinterpret_cast<char*>(property),item_count)!=localized_title){
        if(property)XFree(property);
        throw std::runtime_error("UTF-8 window title property differs");
    }
    XFree(property);
    auto key=[&](KeySym sym,int type=KeyPress,unsigned state=0){XEvent event{};event.xkey.type=type;event.xkey.display=sender.get();event.xkey.window=window.native_id();event.xkey.root=DefaultRootWindow(sender.get());event.xkey.same_screen=True;event.xkey.state=state;event.xkey.keycode=XKeysymToKeycode(sender.get(),sym);if(!event.xkey.keycode||!XSendEvent(sender.get(),window.native_id(),False,type==KeyPress?KeyPressMask:KeyReleaseMask,&event))throw std::runtime_error("own-window key injection failed");XSync(sender.get(),False);window.readback();};
    key(XK_space);auto input=window.poll();if(!input.action||input.quit||input.pause||input.primary_click)throw std::runtime_error("Space mapping failed");
    key(XK_Tab);input=window.poll();if(input.interaction_selection_delta!=1||input.action)throw std::runtime_error("Tab interaction selection mapping failed");
    key(XK_Tab,KeyPress,ShiftMask);input=window.poll();if(input.interaction_selection_delta!=-1||input.action)throw std::runtime_error("Shift+Tab interaction selection mapping failed");
    key(XK_space);if(window.poll().action)throw std::runtime_error("held action repeated");key(XK_space,KeyRelease);window.poll();key(XK_space);if(!window.poll().action)throw std::runtime_error("released action not rearmed");
    key(XK_BackSpace);input=window.poll();if(!input.interrupt||!input.text_backspace||!input.text_input_utf8.empty()||input.action||input.quit)throw std::runtime_error("Backspace interrupt/text mapping failed");
    key(XK_BackSpace);input=window.poll();if(input.interrupt||!input.text_backspace)throw std::runtime_error("held interrupt/text repeat differs");
    key(XK_BackSpace,KeyRelease);window.poll();key(XK_BackSpace);if(!window.poll().interrupt)throw std::runtime_error("released interrupt not rearmed");
    key(XK_u);input=window.poll();if(!input.stand||input.action||input.quit)throw std::runtime_error("U Stand mapping failed");
    key(XK_u);if(window.poll().stand)throw std::runtime_error("held Stand repeated");
    key(XK_u,KeyRelease);window.poll();key(XK_u);if(!window.poll().stand)throw std::runtime_error("released Stand not rearmed");
    key(XK_m);input=window.poll();if(!input.neighborhood||input.action||input.quit)throw std::runtime_error("M neighborhood mapping failed");
    key(XK_m);if(window.poll().neighborhood)throw std::runtime_error("held neighborhood key repeated");
    key(XK_m,KeyRelease);window.poll();key(XK_m);if(!window.poll().neighborhood)throw std::runtime_error("released neighborhood key not rearmed");
    key(XK_e);input=window.poll();if(!input.move_out||input.action||input.quit)throw std::runtime_error("E move-out mapping failed");
    key(XK_e);if(window.poll().move_out)throw std::runtime_error("held move-out key repeated");
    key(XK_e,KeyRelease);window.poll();key(XK_e);if(!window.poll().move_out)throw std::runtime_error("released move-out not rearmed");
    key(XK_p);input=window.poll();if(!input.pause||input.action)throw std::runtime_error("pause mapping failed");
    key(XK_p);if(window.poll().pause)throw std::runtime_error("held pause repeated");
    key(XK_Left);key(XK_Up);input=window.poll();if(input.rotation!=-1||input.zoom!=1)throw std::runtime_error("camera mapping failed");
    key(XK_a);input=window.poll();if(input.action||input.pause||input.quit||input.rotation||input.zoom||input.text_input_utf8!="a")throw std::runtime_error("printable A input differs");
    key(XK_a,KeyRelease);window.poll();key(XK_a,KeyPress,ShiftMask);
    if(window.poll().text_input_utf8!="A")throw std::runtime_error("Shift printable input differs");
    key(XK_a,KeyRelease);window.poll();key(XK_a,KeyPress,ControlMask);
    if(!window.poll().text_input_utf8.empty())throw std::runtime_error("Control shortcut produced text");
    key(XK_a,KeyRelease);window.poll();key(XK_a,KeyPress,Mod1Mask);
    if(!window.poll().text_input_utf8.empty())throw std::runtime_error("Alt shortcut produced text");
    key(XK_c);input=window.poll(true);
    if(!input.create_family||input.text_input_utf8!="c")throw std::runtime_error("C create-family shortcut differs");
    key(XK_c);if(window.poll(true).create_family)throw std::runtime_error("held C shortcut repeated");
    key(XK_c,KeyRelease);window.poll(true);key(XK_c);
    if(!window.poll(true).create_family)throw std::runtime_error("released C shortcut not rearmed");
    auto click=[&](unsigned button,int x,int y){XEvent event{};event.xbutton.type=ButtonPress;event.xbutton.display=sender.get();event.xbutton.window=window.native_id();event.xbutton.root=DefaultRootWindow(sender.get());event.xbutton.same_screen=True;event.xbutton.button=button;event.xbutton.x=x;event.xbutton.y=y;if(!XSendEvent(sender.get(),window.native_id(),False,ButtonPressMask,&event))throw std::runtime_error("own-window click injection failed");XSync(sender.get(),False);window.readback();};
    click(Button1,17,23);input=window.poll();if(!input.action||!input.primary_click||*input.primary_click!=std::array<std::int32_t,2>{17,23})throw std::runtime_error("click coordinates lost");
    if(window.poll().primary_click)throw std::runtime_error("click repeated without event");
    click(Button3,1,2);input=window.poll();if(input.action||input.primary_click)throw std::runtime_error("non-primary click mapped");
    click(Button1,-2,98);input=window.poll();if(!input.primary_click||*input.primary_click!=std::array<std::int32_t,2>{-2,98})throw std::runtime_error("outside click coordinates clamped");
    click(Button1,2,3);click(Button1,127,95);input=window.poll();if(!input.primary_click||*input.primary_click!=std::array<std::int32_t,2>{127,95})throw std::runtime_error("last drained primary click not retained");
    key(XK_Escape);input=window.poll();
    if(!input.quit||input.quit_reason!=WindowQuitReason::escape_key||window.quit_reason()!=input.quit_reason)
        throw std::runtime_error("Escape quit reason mapping failed");
    input=window.poll();
    if(!input.quit||input.quit_reason!=WindowQuitReason::escape_key)
        throw std::runtime_error("quit reason did not persist across polls");
    window.clear_escape_quit_reason();input=window.poll();
    if(input.quit||input.quit_reason!=WindowQuitReason::none||
        window.quit_reason()!=WindowQuitReason::none)
        throw std::runtime_error("Escape cancellation did not clear the pending quit state");
    {
        odh_host::LinuxWindow closing(16,16,"OpenDollhouse own-window close diagnostic test");
        XEvent event{};event.xclient.type=ClientMessage;event.xclient.window=closing.native_id();
        event.xclient.message_type=XInternAtom(sender.get(),"WM_PROTOCOLS",False);
        event.xclient.format=32;event.xclient.data.l[0]=static_cast<long>(XInternAtom(sender.get(),"WM_DELETE_WINDOW",False));
        if(!XSendEvent(sender.get(),closing.native_id(),False,NoEventMask,&event))
            throw std::runtime_error("own-window close injection failed");
        XSync(sender.get(),False);(void)closing.readback();input=closing.poll();
        if(!input.quit||input.quit_reason!=WindowQuitReason::window_manager_close||closing.quit_reason()!=input.quit_reason)
            throw std::runtime_error("window manager close reason mapping failed");
        // Real destruction can follow a close request; retain the first cause.
        XDestroyWindow(sender.get(),closing.native_id());XSync(sender.get(),False);
        input=closing.poll();
        if(!input.quit||input.quit_reason!=WindowQuitReason::window_manager_close)
            throw std::runtime_error("destruction overwrote window manager close reason");
    }
    {
        odh_host::LinuxWindow destroyed(16,16,"OpenDollhouse own-window destruction diagnostic test");
        (void)destroyed.poll();(void)destroyed.readback(); // Drain initial exposes before destruction.
        XDestroyWindow(sender.get(),destroyed.native_id());XSync(sender.get(),False);
        input=destroyed.poll();
        if(!input.quit||input.quit_reason!=WindowQuitReason::window_destroyed||destroyed.quit_reason()!=input.quit_reason)
            throw std::runtime_error("destroyed window reason mapping failed");
    }
    std::cout<<"Own native X11 window: all12288RGB pixels equal; frame validation, Backspace interrupt, and keyboard mappings passed (synthetic app-directed events).\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
