#include "screen_pointer.hpp"
#include <stdexcept>
#include <iostream>
using namespace odh_host;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
WindowInput event(WindowPointerKind kind,int x){WindowInput in;in.pointer_events.push_back({kind,x,10,false,false});return in;}
}
int main(){try{
    const auto hit=[](int x,int)->std::optional<std::size_t>{
        if(x>=0&&x<20)return 1;
        if(x>=30&&x<50)return 2;
        return {};
    };
    ScreenPointer p;
    require(!p.click(event(WindowPointerKind::press,10),hit)&&p.pressed()==1,"press activated before release");
    require(!p.click(event(WindowPointerKind::motion,35),hit)&&p.pressed()==1,"motion lost original capture");
    require(!p.click(event(WindowPointerKind::release,35),hit)&&!p.pressed(),"different release target activated");
    require(!p.click(event(WindowPointerKind::release,10),hit),"orphan release activated");
    p.click(event(WindowPointerKind::press,10),hit);
    p.click(event(WindowPointerKind::motion,100),hit);
    require(p.click(event(WindowPointerKind::release,10),hit).has_value(),"return to captured control failed");
    p.click(event(WindowPointerKind::press,10),hit);
    WindowInput cancelled;cancelled.pointer_cancelled=true;
    cancelled.pointer_events.push_back({WindowPointerKind::release,10,10,false,false});
    require(!p.click(cancelled,hit)&&!p.pressed(),"lost focus activated release");
    p.click(event(WindowPointerKind::press,10),hit);
    WindowInput overflow;overflow.pointer_overflow=true;
    require(!p.click(overflow,hit)&&!p.pressed(),"overflow kept stale capture");
    WindowInput legacy;legacy.primary_click=std::array<std::int32_t,2>{10,10};legacy.action=true;
    require(!p.click(legacy,hit),"press-only legacy field activated");
    WindowInput batched;batched.pointer_events={{WindowPointerKind::press,10,10,false,false},{WindowPointerKind::release,10,10,false,false}};
    require(p.click(batched,hit).has_value(),"same-poll press and release failed");
    p.click(event(WindowPointerKind::press,100),hit);
    require(!p.click(event(WindowPointerKind::release,10),hit),"outside press gained capture");
    p.click(event(WindowPointerKind::press,10),hit);
    require(!p.click(event(WindowPointerKind::release,10),[](int,int)->std::optional<std::size_t>{return {};}),"disabled control activated after press");
    std::cout<<"screen pointer gestures passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
