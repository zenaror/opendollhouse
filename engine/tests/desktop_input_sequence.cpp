/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. See https://mozilla.org/MPL/2.0/. */
#include "InputSequence.h"
#include "app/ui/DesktopCamera.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using odh::desktop::InputSequence;
using simapp::AppInput;
using Kind=InputSequence::Kind;

static void Check(bool okay,const char* message) {
    if(!okay)throw std::runtime_error(message);
}
static AppInput Pointer(int x,int y,bool down=false,bool up=false) {
    AppInput input;input.DesktopPointer=true;
    input.TouchX=x;input.TouchY=y;input.TouchDown=down;input.TouchUp=up;input.Touching=down;
    return input;
}
static AppInput Text(const std::string& value) {
    auto input=Pointer(120,120);
    input.TextUTF8=value;
    input.TextEvents.push_back({AppInput::TextEvent::Type::Insert,value});
    return input;
}
// Like the shared button/CAS contract, capture belongs to the pressed control.
// The test consumes public packets; it does not inspect queue internals.
struct Controls {
    int pressed=-1,activated=-1,focus=-1;
    std::string names[2];
    static int At(int x,int y) {
        if(y<100 || y>=140)return -1;
        if(x>=100 && x<160)return 0;
        if(x>=200 && x<260)return 1;
        return -1;
    }
    void Frame(const AppInput& input) {
        if(input.TouchDown)pressed=At(input.TouchX,input.TouchY);
        if(input.TouchUp) {
            const int target=At(input.TouchX,input.TouchY);
            if(target>=0 && target==pressed)activated=focus=target;
            pressed=-1;
        }
        if(focus>=0)for(const auto& event:input.TextEvents) {
            if(event.Kind==AppInput::TextEvent::Type::Insert)names[focus]+=event.Text;
            else if(event.Kind==AppInput::TextEvent::Type::Backspace && !names[focus].empty())names[focus].pop_back();
        }
    }
};
static void OutsideRelease() {
    InputSequence queue;Controls controls;
    queue.Push(Pointer(20,20,true),Kind::Action);
    queue.Push(Pointer(120,120,false,true),Kind::Action);
    auto down=queue.Next();
    Check(down.Input.TouchDown && !down.Input.TouchUp && down.Input.TouchX==20,"press must retain its own coordinates");
    controls.Frame(down.Input);controls.Frame(queue.Next().Input);
    Check(controls.activated==-1,"release inside must not activate a press from outside");
    Check(!queue.Next().Input.TouchUp,"release edge must not repeat on idle frames");
}
static void DifferentTargetsAndRapidClicks() {
    InputSequence queue;Controls controls;
    queue.Push(Pointer(120,120,true),Kind::Action);
    queue.Push(Pointer(220,120,false,true),Kind::Action);
    controls.Frame(queue.Next().Input);controls.Frame(queue.Next().Input);
    Check(controls.activated==-1,"release over another control must not activate it");
    for(int x:{120,220}) {
        queue.Push(Pointer(x,120,true),Kind::Action);
        queue.Push(Pointer(x,120,false,true),Kind::Action);
    }
    int activated=0;
    while(queue.Pending()) {
        controls.activated=-1;controls.Frame(queue.Next().Input);
        if(controls.activated>=0)++activated;
    }
    Check(activated==2 && controls.focus==1,"two rapid complete clicks must both survive in order");
}
static void FocusBeforeText() {
    InputSequence queue;Controls controls;
    queue.Push(Pointer(120,120,true),Kind::Action);
    queue.Push(Pointer(120,120,false,true),Kind::Action);
    queue.Push(Text("ab"),Kind::Text);
    auto backspace=Pointer(120,120);backspace.TextBackspace=true;
    backspace.TextEvents.push_back({AppInput::TextEvent::Type::Backspace,{}});
    queue.Push(backspace,Kind::Text);queue.Push(Text("c"),Kind::Text);
    queue.Push(Pointer(220,120,true),Kind::Action);
    queue.Push(Pointer(220,120,false,true),Kind::Action);
    auto second=Text("d");second.TouchX=220;queue.Push(second,Kind::Text);
    while(queue.Pending())controls.Frame(queue.Next().Input);
    Check(controls.names[0]=="ac" && controls.names[1]=="d","click focus and text/edit order must be preserved");
    controls.Frame(queue.Next().Input);
    Check(controls.names[1]=="d","idle frame must not repeat text");
}
static void MotionAndState() {
    InputSequence queue;
    queue.Push(Pointer(120,120,true),Kind::Action);
    for(int x=121;x<1000;++x) { auto move=Pointer(x,125);move.Touching=true;queue.Push(move,Kind::State); }
    queue.Push(Pointer(220,130,false,true),Kind::Action);
    Check(queue.Pending()==3,"motion burst should occupy one packet without merging button edges");
    Check(queue.Next().Input.TouchX==120,"coalescing motion must not move the original press");
    auto move=queue.Next().Input;
    Check(move.TouchX==999 && move.Touching,"last motion and held state must survive");
    auto up=queue.Next().Input;
    Check(up.TouchUp && up.TouchX==220 && up.TouchY==130,"release uses its own position, not last motion");
}
static void CancellationAndBounds() {
    InputSequence queue;Controls controls;
    queue.Push(Pointer(120,120,true),Kind::Action);controls.Frame(queue.Next().Input);
    queue.Push(Text("stale"),Kind::Text);
    queue.Cancel();
    Check(queue.Pending()==1 && queue.Discarding(),"focus loss must discard pending activation/text");
    auto cancelled=queue.Next().Input;
    Check(cancelled.PointerCancelled && cancelled.TouchUp && cancelled.TouchX==-1 && cancelled.TouchY==-1 && !cancelled.Touching,"cancel must notify presentation capture and release outside every control");
    Check(!queue.Next().Input.PointerCancelled,"capture cancellation must not repeat on idle frames");
    controls.Frame(cancelled);
    Check(controls.activated==-1 && controls.pressed==-1,"focus loss must release capture without activating");
    queue.BeginPoll();
    for(size_t i=0;i<=InputSequence::MaxPackets;++i)queue.Push(Pointer(120,120,true),Kind::Action);
    Check(queue.TakeOverflow() && queue.Pending()==1,"event overload must have a finite bound and report cancellation");
    Check(queue.Next().Input.TouchX==-1,"overflow cannot leave an actionable partial click");
    queue.BeginPoll();
    queue.Push(Text(std::string(InputSequence::MaxTextBytes,'x')),Kind::Text);
    Check(queue.TakeOverflow() && queue.Pending()==1,"oversized text payload must respect memory budget");
}
static void QuitAndModifiers() {
    InputSequence queue;
    auto first=Pointer(10,10,true);first.DesktopCtrl=true;
    queue.Push(first,Kind::Action);
    auto second=Pointer(10,10,false,true);second.DesktopShift=true;
    queue.Push(second,Kind::Action);queue.Push(second,Kind::Action,true);
    Check(queue.Next().Input.DesktopCtrl,"press modifiers must not be replaced by final poll state");
    Check(queue.Next().Input.DesktopShift,"release modifiers must retain event order");
    Check(queue.Next().Quit && !queue.Next().Quit,"quit request must be ordered and delivered only once");
}
static void SecondaryCameraCapture() {
    simapp::original::DesktopCamera camera;InputSequence queue;
    auto down=Pointer(200,150);down.MouseRightDown=down.MouseRightHeld=true;
    auto move=Pointer(300,170);move.MouseRightHeld=true;
    auto up=Pointer(310,180);up.MouseRightUp=true;
    queue.Push(down,Kind::Action);queue.Push(move,Kind::State);queue.Push(up,Kind::Action);
    const auto start=queue.Next().Input;
    Check(start.MouseRightDown&&start.MouseRightHeld&&!start.MouseRightUp&&start.TouchX==200,"RMB press preserves origin and edges");
    camera.Update(start,true,false,false,{},1000./60);
    const auto motion=queue.Next().Input;
    Check(motion.MouseRightHeld&&!motion.MouseRightDown,"motion preserves RMB hold without repeating Down");
    const auto drag=camera.Update(motion,true,false,false,{},1000./60);
    Check(drag.right_drag&&std::abs(drag.horizontal-.05f)<.00001f&&std::abs(drag.vertical-.01f)<.00001f,"RMB uses retained origin and reference frame-normalized units");
    const auto release=queue.Next().Input;
    Check(release.MouseRightUp&&!release.MouseRightHeld,"RMB release survives as ordered action");
    Check(!camera.Update(release,true,false,false,{},1000./60).right_drag&&!camera.Anchor(),"release clears captured anchor");
    Check(!queue.Next().Input.MouseRightUp&&!queue.Next().Input.MouseRightDown,"RMB action edges do not repeat while idle");
    queue.Push(down,Kind::Action);camera.Update(queue.Next().Input,true,false,false,{},16);
    queue.Cancel();const auto cancel=queue.Next().Input;
    Check(cancel.PointerCancelled&&cancel.MouseRightUp&&!cancel.MouseRightHeld&&!cancel.Down,"focus cancellation releases RMB without Escape");
    Check(!camera.Update(cancel,true,false,true,{-1,0},16).right_drag&&!camera.Anchor(),"focus cancels camera even near an enabled edge");
    Check(!camera.Update(move,true,false,false,{},16).right_drag,"held stale button cannot rearm after cancellation");
}
static void CameraBlockedOriginsAndEdges() {
    auto down=Pointer(200,150);down.MouseRightDown=down.MouseRightHeld=true;
    auto held=Pointer(400,150);held.MouseRightHeld=true;
    for(const bool modal:{false,true}){
        simapp::original::DesktopCamera camera;
        camera.Update(down,false,modal,true,{1,0},16);
        Check(!camera.Update(held,true,false,true,{1,0},16).right_drag,"press outside/HUD/modal cannot capture by entering canvas later");
    }
    for(int interruption=0;interruption<3;++interruption){
        simapp::original::DesktopCamera camera;camera.Update(down,true,false,false,{},16);
        auto blocked=held;if(interruption==2)blocked.PointerCancelled=true;
        camera.Update(blocked,interruption!=0,interruption==1,false,{},16);
        Check(!camera.Update(held,true,false,false,{},16).right_drag&&!camera.Anchor(),"leaving canvas, opening modal or focus cancels until a fresh press");
    }
    for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y){
        simapp::original::DesktopCamera camera;auto input=Pointer(2,2);
        const auto no=camera.Update(input,true,false,false,{x,y},1000./60);
        Check(!no.horizontal&&!no.vertical,"disabled edge setting has no camera consumer effect");
        const auto yes=camera.Update(input,true,false,true,{x,y},1000./60);
        Check(std::abs(yes.horizontal-x*.0625f)<.00001f&&std::abs(yes.vertical-y*.0625f)<.00001f,"enabled edge uses reference units in all eight directions");
        Check(yes.diagonal_edge==bool(x&&y),"diagonal correction is declared only for corner scrolling");
        input.Touching=true;
        const auto primary=camera.Update(input,true,false,true,{x,y},16);
        Check(!primary.horizontal&&!primary.vertical,"primary capture suppresses camera scrolling");
    }
    simapp::original::DesktopCamera camera;
    Check(!camera.Update(AppInput{},true,false,true,{1,0},16).horizontal,"empty consumed modal packet cannot scroll at synthetic position zero");
}

static void InactiveFocusRejectsHoverMotion() {
    InputSequence queue;simapp::original::DesktopCamera camera;
    auto edge=Pointer(1,1);
    queue.Push(edge,Kind::State); // Pointer is over the window as focus is lost.
    queue.SetFocused(false);
    const auto cancel=queue.Next().Input;
    Check(cancel.PointerCancelled&&!cancel.Down,"keyboard focus loss publishes nonactivating cancellation");
    for(int frame=0;frame<3;++frame){
        queue.BeginPoll();
        Check(!queue.Push(edge,Kind::State),"mouse hover focus alone cannot enqueue edge motion");
        const auto idle=queue.Next().Input;
        const bool canvas=idle.TouchX>=0&&idle.TouchY>=0;
        const auto motion=camera.Update(idle,canvas,false,true,{-1,-1},1000./60);
        Check(!motion.horizontal&&!motion.vertical,"inactive mouse hover cannot restart edge scrolling after cancellation");
    }
    queue.SetFocused(true);queue.BeginPoll();
    Check(queue.Push(edge,Kind::State),"active focus allows new ordinary pointer state");
    const auto active=queue.Next().Input;
    Check(camera.Update(active,true,false,true,{-1,0},1000./60).horizontal<0,"edge scrolling resumes only after real focus returns");
}

static void InactiveQuitIsInertAndRetained() {
    InputSequence queue;queue.SetFocused(false);
    auto stale=Pointer(10,10,true);stale.Down=simapp::BTN_B;stale.MouseRightHeld=true;
    Check(queue.Push(stale,Kind::Action,true),"unfocused window close must be retained");
    queue.Cancel(); // A later leave/focus/overflow cannot swallow the close.
    Check(queue.Next().Input.PointerCancelled,"capture cancellation still precedes window close");
    const auto close=queue.Next();
    Check(close.Quit&&!close.Input.Down&&!close.Input.TouchDown&&!close.Input.Touching&&!close.Input.MouseRightHeld&&close.Input.TouchX==-1,
          "inactive close must carry no stale gameplay action");
    Check(!queue.Next().Quit,"window close is delivered exactly once");
    simapp::original::DesktopCamera camera;
    const auto motion=camera.Update(close.Input,close.Input.TouchX>=0,false,true,{-1,0},16);
    Check(!motion.horizontal&&!motion.vertical,"same-poll inactive quit cannot carry camera motion");
    InputSequence later;later.SetFocused(false);later.Next();later.BeginPoll();
    Check(later.Push(stale,Kind::Action,true),"inactive quit must also survive a later poll without discard");
    const auto laterClose=later.Next();
    Check(laterClose.Quit&&!laterClose.Input.TouchDown&&!laterClose.Input.MouseRightHeld&&laterClose.Input.TouchX==-1,
          "later-poll inactive quit must also be inert");
    InputSequence active;active.Push(stale,Kind::Action,true);active.SetFocused(false);
    active.Next();Check(active.Next().Quit,"focus loss after queued close must preserve it");
}

int main() {
    try {
        OutsideRelease();DifferentTargetsAndRapidClicks();FocusBeforeText();MotionAndState();CancellationAndBounds();QuitAndModifiers();SecondaryCameraCapture();CameraBlockedOriginsAndEdges();InactiveFocusRejectsHoverMotion();InactiveQuitIsInertAndRetained();
        std::cout<<"desktop input sequence: 10 grouped regression scenarios passed\n";
        return EXIT_SUCCESS;
    } catch(const std::exception& error) {
        std::cerr<<"desktop input sequence: "<<error.what()<<'\n';return EXIT_FAILURE;
    }
}
