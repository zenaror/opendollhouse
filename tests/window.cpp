#include "linux_window.hpp"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <memory>
int main(){try{
    if(!std::getenv("DISPLAY")){std::cout<<"X11 display unavailable: skipped\n";return 77;}
    Display* availability=XOpenDisplay(nullptr);if(!availability){std::cout<<"X11 DISPLAY inaccessible: skipped\n";return 77;}XCloseDisplay(availability);
    odh_host::LinuxWindow window(128,96,"OpenDollhouse own-window backend test");
    odh::RasterFrame frame{128,96,{}, {}};
    frame.pixels.resize(128*96);frame.depth.resize(128*96,std::numeric_limits<float>::infinity());
    for(std::size_t i=0;i<frame.pixels.size();++i)frame.pixels[i]={std::uint8_t((i&1)?255:0),std::uint8_t((i&2)?255:0),std::uint8_t((i&4)?255:0),255};
    window.present(frame);if(window.readback()!=frame.pixels)throw std::runtime_error("native RGB readback differs");
    auto invalid=frame;invalid.pixels.pop_back();bool rejected=false;try{window.present(invalid);}catch(const odh::FormatError&){rejected=true;}if(!rejected)throw std::runtime_error("short image accepted");
    invalid=frame;invalid.pixels[0][3]=0;rejected=false;try{window.present(invalid);}catch(const odh::FormatError&){rejected=true;}if(!rejected)throw std::runtime_error("transparent frame accepted");
    window.poll();
    std::unique_ptr<Display,decltype(&XCloseDisplay)> sender(XOpenDisplay(nullptr),&XCloseDisplay);
    if(!sender)throw std::runtime_error("cannot open own-window test sender");
    auto key=[&](KeySym sym,int type=KeyPress,unsigned state=0){XEvent event{};event.xkey.type=type;event.xkey.display=sender.get();event.xkey.window=window.native_id();event.xkey.root=DefaultRootWindow(sender.get());event.xkey.same_screen=True;event.xkey.state=state;event.xkey.keycode=XKeysymToKeycode(sender.get(),sym);if(!event.xkey.keycode||!XSendEvent(sender.get(),window.native_id(),False,type==KeyPress?KeyPressMask:KeyReleaseMask,&event))throw std::runtime_error("own-window key injection failed");XSync(sender.get(),False);window.readback();};
    key(XK_space);auto input=window.poll();if(!input.action||input.quit||input.pause||input.primary_click)throw std::runtime_error("Space mapping failed");
    key(XK_Tab);input=window.poll();if(input.interaction_selection_delta!=1||input.action)throw std::runtime_error("Tab interaction selection mapping failed");
    key(XK_Tab,KeyPress,ShiftMask);input=window.poll();if(input.interaction_selection_delta!=-1||input.action)throw std::runtime_error("Shift+Tab interaction selection mapping failed");
    key(XK_space);if(window.poll().action)throw std::runtime_error("held action repeated");key(XK_space,KeyRelease);window.poll();key(XK_space);if(!window.poll().action)throw std::runtime_error("released action not rearmed");
    key(XK_BackSpace);input=window.poll();if(!input.interrupt||input.action||input.quit)throw std::runtime_error("Backspace interrupt mapping failed");
    key(XK_BackSpace);if(window.poll().interrupt)throw std::runtime_error("held interrupt repeated");
    key(XK_BackSpace,KeyRelease);window.poll();key(XK_BackSpace);if(!window.poll().interrupt)throw std::runtime_error("released interrupt not rearmed");
    key(XK_p);input=window.poll();if(!input.pause||input.action)throw std::runtime_error("pause mapping failed");
    key(XK_p);if(window.poll().pause)throw std::runtime_error("held pause repeated");
    key(XK_Left);key(XK_Up);input=window.poll();if(input.rotation!=-1||input.zoom!=1)throw std::runtime_error("camera mapping failed");
    key(XK_a);input=window.poll();if(input.action||input.pause||input.quit||input.rotation||input.zoom)throw std::runtime_error("unknown key mapped");
    auto click=[&](unsigned button,int x,int y){XEvent event{};event.xbutton.type=ButtonPress;event.xbutton.display=sender.get();event.xbutton.window=window.native_id();event.xbutton.root=DefaultRootWindow(sender.get());event.xbutton.same_screen=True;event.xbutton.button=button;event.xbutton.x=x;event.xbutton.y=y;if(!XSendEvent(sender.get(),window.native_id(),False,ButtonPressMask,&event))throw std::runtime_error("own-window click injection failed");XSync(sender.get(),False);window.readback();};
    click(Button1,17,23);input=window.poll();if(!input.action||!input.primary_click||*input.primary_click!=std::array<std::int32_t,2>{17,23})throw std::runtime_error("click coordinates lost");
    if(window.poll().primary_click)throw std::runtime_error("click repeated without event");
    click(Button3,1,2);input=window.poll();if(input.action||input.primary_click)throw std::runtime_error("non-primary click mapped");
    click(Button1,-2,98);input=window.poll();if(!input.primary_click||*input.primary_click!=std::array<std::int32_t,2>{-2,98})throw std::runtime_error("outside click coordinates clamped");
    click(Button1,2,3);click(Button1,127,95);input=window.poll();if(!input.primary_click||*input.primary_click!=std::array<std::int32_t,2>{127,95})throw std::runtime_error("last drained primary click not retained");
    key(XK_Escape);if(!window.poll().quit)throw std::runtime_error("quit mapping failed");
    std::cout<<"Own native X11 window: all12288RGB pixels equal; frame validation, Backspace interrupt, and keyboard mappings passed (synthetic app-directed events).\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
