// SPDX-License-Identifier: MPL-2.0
// Run under an isolated Xvfb by the coordinator. Acceptance of a native cursor
// is not visual readback proof; root separately reads actual cursor pixels.
#include "linux_window.hpp"
#include <X11/Xlib.h>
#include <cstdlib>
#include <iostream>
#include <limits>
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
struct Errors {
    XErrorHandler previous=XSetErrorHandler(on_error);
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
        auto* expose_display=XOpenDisplay(nullptr);need(expose_display!=nullptr,"external expose display");
        XClearArea(expose_display,window.native_id(),0,0,64,48,True);XSync(expose_display,False);XCloseDisplay(expose_display);
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
    // Destruction must remain observable even if Escape was recorded first.
    {
        odh_host::LinuxWindow window(32,32,"OpenDollhouse destroyed cursor window");
        auto* display=XOpenDisplay(nullptr);need(display!=nullptr,"external test display");
        const auto id=window.native_id();
        XEvent expose{};expose.xexpose.type=Expose;expose.xexpose.display=display;expose.xexpose.window=id;
        expose.xexpose.width=32;expose.xexpose.height=32;expose.xexpose.count=0;
        XSendEvent(display,id,False,ExposureMask,&expose);
        XEvent escape{};escape.xkey.type=KeyPress;escape.xkey.display=display;escape.xkey.window=id;
        escape.xkey.root=DefaultRootWindow(display);escape.xkey.same_screen=True;
        escape.xkey.keycode=XKeysymToKeycode(display,0xff1b);
        XSendEvent(display,id,False,KeyPressMask,&escape);
        XDestroyWindow(display,id);XSync(display,False);
        (void)window.poll();
        need(!window.alive(),"native destruction is visible independently of first quit reason");
        reject([&]{window.use_cursor({});});
        reject([&]{window.create_cursor(image());});
        XCloseDisplay(display);
    }
    need(errors==0,"native cursor teardown remained error-free");
    // Expose-origin repaint must not escape a capped poll while a later
    // DestroyNotify is still queued; retain its pending flag until drained.
    {
        odh_host::LinuxWindow window(32,32,"OpenDollhouse capped expose queue test");
        (void)window.poll();
        auto* display=XOpenDisplay(nullptr);need(display!=nullptr,"external capped queue display");
        const auto id=window.native_id();
        XEvent expose{};expose.xexpose.type=Expose;expose.xexpose.display=display;expose.xexpose.window=id;
        expose.xexpose.width=32;expose.xexpose.height=32;
        XSendEvent(display,id,False,ExposureMask,&expose);
        for(unsigned i=0;i<4100;++i){
            XEvent ignored{};ignored.xclient.type=ClientMessage;ignored.xclient.display=display;ignored.xclient.window=id;ignored.xclient.format=32;
            XSendEvent(display,id,False,NoEventMask,&ignored);
        }
        XDestroyWindow(display,id);XSync(display,False);
        const auto capped=window.poll();
        need(window.alive()&&!capped.redraw,"capped event batch does not publish premature Expose redraw");
        for(unsigned i=0;i<4&&window.alive();++i)(void)window.poll();
        need(!window.alive(),"later bounded poll observes queued native destruction");
        XCloseDisplay(display);
    }
    need(errors==0,"capped Expose followed by destruction never repaints a stale drawable");
}
}
int main(){try{
    if(!std::getenv("DISPLAY")){std::cout<<"X11 display unavailable: skipped\n";return 77;}
    Display* available=XOpenDisplay(nullptr);if(!available){std::cout<<"X11 DISPLAY inaccessible: skipped\n";return 77;}XCloseDisplay(available);
    cases();return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
