// SPDX-License-Identifier: MPL-2.0
// Run under an isolated Xvfb by the coordinator. Acceptance of a native cursor
// is not visual readback proof; root separately reads actual cursor pixels.
#include "linux_window.hpp"
#include <X11/Xlib.h>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
int errors=0;
int on_error(Display* display,XErrorEvent* event){
    ++errors;
    char message[128]{};XGetErrorText(display,event->error_code,message,sizeof(message));
    std::cerr<<"X11 cursor test error="<<unsigned(event->error_code)<<" ("<<message<<") request="
        <<unsigned(event->request_code)<<" minor="<<unsigned(event->minor_code)<<" resource="
        <<event->resourceid<<" serial="<<event->serial<<'\n';
    return 0;
}
using DisplayOwner=std::unique_ptr<Display,decltype(&XCloseDisplay)>;
DisplayOwner open_display(){return DisplayOwner(XOpenDisplay(nullptr),&XCloseDisplay);}
struct Errors {
    XErrorHandler previous=XSetErrorHandler(on_error);
    Errors()=default;
    Errors(const Errors&)=delete;
    Errors& operator=(const Errors&)=delete;
    ~Errors(){XSetErrorHandler(previous);}
};
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void reject(F action){bool failed=false;try{action();}catch(const odh::FormatError&){failed=true;}need(failed,"malformed cursor/local handle was accepted");}
odh::CursorImage image(){
    odh::CursorImage out;out.width=9;out.height=7;out.hotspot_x=8;out.hotspot_y=6;
    out.pixels.resize(63);out.invert.assign(63,0);
    for(std::size_t i=0;i<out.pixels.size();++i)
        out.pixels[i]=i%3==0?std::array<std::uint8_t,4>{0,0,0,255}:
            i%3==1?std::array<std::uint8_t,4>{255,255,255,255}:std::array<std::uint8_t,4>{0,0,0,0};
    return out;
}
void synchronize(odh_host::LinuxWindow& window){(void)window.readback();need(errors==0,"native cursor operation raised an X11 error");}
// Query a live root, never the destroyed drawable: this proves the server
// processed XDestroyWindow without deliberately manufacturing a BadWindow.
bool server_contains(Display* display,Window id){
    Window root=0,parent=0,*children=nullptr;unsigned count=0;
    const auto queried=XQueryTree(display,DefaultRootWindow(display),&root,&parent,&children,&count);
    std::unique_ptr<Window,decltype(&XFree)> owned(children,&XFree);
    need(queried!=0,"cannot query test server root");
    for(unsigned n=0;n<count;++n)if(children[n]==id)return true;
    return false;
}
void cases(){
    Errors error_handler;
    {
        odh_host::LinuxWindow window(64,48,"OpenDollhouse cursor registry test");
        const auto original=image();
        for(unsigned malformed=0;malformed<8;++malformed){
            auto bad=original;
            if(malformed==0)bad.width=0;
            if(malformed==1)bad.height=257;
            if(malformed==2)bad.hotspot_x=bad.width;
            if(malformed==3)bad.hotspot_y=bad.height;
            if(malformed==4)bad.pixels.pop_back();
            if(malformed==5)bad.invert.pop_back();
            if(malformed==6)bad.invert[0]=2;
            if(malformed==7){bad.invert[0]=1;bad.invert.back()=2;} // Validate whole image before unsupported return.
            reject([&]{window.create_cursor(bad);});
        }
        for(unsigned unsupported=0;unsupported<3;++unsupported){
            auto candidate=original;
            if(unsupported==0)candidate.invert[4]=1;
            if(unsupported==1)candidate.pixels[0]={25,80,140,255};
            if(unsupported==2)candidate.pixels[0]={0,0,0,128};
            need(!window.create_cursor(candidate),"unsupported inversion/color/alpha never becomes transparent approximation");
        }
        synchronize(window);
        const auto first=window.create_cursor(original);
        need(first&&*first==1,"first successful cursor receives opaque local handle1");
        window.use_cursor(first);window.use_cursor(first);synchronize(window);
        // A live drawable still restores its cached pixels after Expose.
        odh::RasterFrame cached{};cached.width=64;cached.height=48;cached.pixels.assign(64*48,{255,0,255,255});
        cached.depth.assign(cached.pixels.size(),std::numeric_limits<float>::infinity());
        window.present(cached);
        auto expose_display=open_display();need(bool(expose_display),"external expose display");
        XClearArea(expose_display.get(),window.native_id(),0,0,64,48,True);XSync(expose_display.get(),False);expose_display.reset();
        need(window.poll().redraw&&window.readback()==cached.pixels,"live Expose preserves cached repaint and requests application redraw");
        reject([&]{window.use_cursor(0);});reject([&]{window.use_cursor(129);});
        synchronize(window); // Invalid handles do not leak native XIDs into requests.
        auto second_image=original;second_image.hotspot_x=second_image.hotspot_y=0;
        for(auto& pixel:second_image.pixels)if(pixel[3])pixel[0]=pixel[1]=pixel[2]=pixel[0]?0:255;
        const auto second=window.create_cursor(second_image);need(second&&*second!=*first,"separate decoded cursors retain distinct local handles");
        window.use_cursor(second);window.use_cursor({});window.use_cursor({});window.use_cursor(first);synchronize(window);
        // The native resource owns its bitmaps after creation; mutating input
        // does not invalidate the handle, and resize retains the window cursor.
        second_image.pixels.clear();second_image.invert.clear();window.use_cursor(second);window.resize(72,48);synchronize(window);
        for(unsigned i=2;i<128;++i)need(bool(window.create_cursor(original)),"bounded registry admits128 native cursors");
        need(!window.create_cursor(original),"registry exhaustion returns none before allocating another resource");
        window.use_cursor(128);synchronize(window);window.use_cursor({});
        {
            odh_host::LinuxWindow other(48,32,"OpenDollhouse independent cursor window");
            const auto independent=other.create_cursor(original);
            need(independent&&*independent==1,"new window has an independent handle registry despite first being full");
            other.use_cursor(independent);synchronize(other);
            window.use_cursor(first);synchronize(window);
            other.use_cursor({});synchronize(other);
        }
        need(errors==0,"disposing one cursor window does not free another window's resources");
        window.use_cursor(second);synchronize(window);
    }
    need(errors==0,"cursor resources dispose before closing their private display");
    // Exercise owned native teardown, including an active cursor, repeatedly.
    for(unsigned i=0;i<4;++i){
        odh_host::LinuxWindow window(32,32,"OpenDollhouse cursor disposal test");
        const auto cursor=window.create_cursor(image());need(bool(cursor),"cursor creation survives previous bank teardown");
        window.use_cursor(cursor);synchronize(window);
    }
    // Destruction must remain observable even with Expose/Escape queued before it.
    {
        odh_host::LinuxWindow window(32,32,"OpenDollhouse destroyed cursor window");
        auto display=open_display();need(bool(display),"external test display");
        const auto id=window.native_id();
        need(server_contains(display.get(),id),"native drawable absent before server destruction");
        XEvent expose{};expose.xexpose.type=Expose;expose.xexpose.display=display.get();expose.xexpose.window=id;
        expose.xexpose.width=32;expose.xexpose.height=32;expose.xexpose.count=0;
        need(XSendEvent(display.get(),id,False,ExposureMask,&expose)!=0,"cannot enqueue native Expose");
        XEvent escape{};escape.xkey.type=KeyPress;escape.xkey.display=display.get();escape.xkey.window=id;
        escape.xkey.root=DefaultRootWindow(display.get());escape.xkey.same_screen=True;
        escape.xkey.keycode=XKeysymToKeycode(display.get(),0xff1b);
        need(XSendEvent(display.get(),id,False,KeyPressMask,&escape)!=0,"cannot enqueue native Escape");
        XDestroyWindow(display.get(),id);XSync(display.get(),False);
        need(!server_contains(display.get(),id),"server still contains destroyed native drawable");
        need(errors==0,"server destruction/query raised an X11 error");
        (void)window.poll();
        need(!window.alive(),"native destruction is visible independently of first quit reason");
        reject([&]{window.use_cursor({});});
        reject([&]{window.create_cursor(image());});
        display.reset();
    }
    need(errors==0,"native cursor teardown remained error-free");
    // Expose-origin repaint must not escape while destruction is queued.
    // Terminal DestroyNotify may be prioritized immediately; a bounded drain
    // is also valid provided NONE of its batches repaint the dead drawable.
    {
        odh_host::LinuxWindow window(32,32,"OpenDollhouse capped expose queue test");
        (void)window.poll();
        auto display=open_display();need(bool(display),"external capped queue display");
        const auto id=window.native_id();
        need(server_contains(display.get(),id),"native drawable absent before server destruction");
        XEvent expose{};expose.xexpose.type=Expose;expose.xexpose.display=display.get();expose.xexpose.window=id;
        expose.xexpose.width=32;expose.xexpose.height=32;
        need(XSendEvent(display.get(),id,False,ExposureMask,&expose)!=0,"cannot enqueue native Expose");
        for(unsigned i=0;i<4100;++i){
            XEvent ignored{};ignored.xclient.type=ClientMessage;ignored.xclient.display=display.get();ignored.xclient.window=id;ignored.xclient.format=32;
            need(XSendEvent(display.get(),id,False,NoEventMask,&ignored)!=0,"cannot enqueue capped native client event");
        }
        XDestroyWindow(display.get(),id);XSync(display.get(),False);
        need(!server_contains(display.get(),id),"server still contains destroyed native drawable");
        need(errors==0,"server destruction/query raised an X11 error");
        const auto capped=window.poll();
        need(!capped.redraw,"first destruction batch published premature Expose redraw");
        for(unsigned i=0;i<4&&window.alive();++i){
            const auto pending=window.poll();
            need(!pending.redraw,"bounded destruction batch published stale Expose redraw");
            need(errors==0,"bounded destruction batch repainted a dead drawable");
        }
        need(!window.alive(),"immediate or bounded poll must observe real server destruction");
        need(window.quit_reason()==odh_host::WindowQuitReason::window_destroyed,
             "server destruction did not retain its terminal quit reason");
        display.reset();
    }
    need(errors==0,"capped Expose followed by destruction never repaints a stale drawable");
}
}
int main(){try{
    if(!std::getenv("DISPLAY")){std::cout<<"X11 display unavailable: skipped\n";return 77;}
    auto available=open_display();if(!available){std::cout<<"X11 DISPLAY inaccessible: skipped\n";return 77;}available.reset();
    cases();return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
