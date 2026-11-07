// SPDX-License-Identifier: MPL-2.0
#include "ui_hover.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace odh_host;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
UiHoverTarget target(){return {17,{10,20,100,40},"Original character name\nOriginal description"};}
UiHoverInput input(){UiHoverInput out;out.pointer=std::array<std::int32_t,2>{30,30};out.context=4;out.selection=6;return out;}
void cases(){
    for(const std::uint64_t delay:{0u,1u,500u,1500u}){
        UiHover hover({delay});auto cell=target();auto in=input();
        const auto first=hover.update(100,cell,in);
        need(bool(first)==(delay==0),"explicit delay controls initial display");
        need(hover.pending()==(delay!=0),"only a waiting target has a pending transition");
        if(delay)need(hover.next_transition_ms()==100+delay&&!hover.refresh_due(100+delay-1)&&hover.refresh_due(100+delay),
            "paused UI has one exact wakeup deadline");
        if(delay)need(!hover.update(100+delay-1,cell,in),"never show before dwell deadline");
        auto visible=hover.update(100+delay,cell,in);
        need(visible&&visible->id==17&&visible->text==cell.text,"exact deadline exposes owned original text and identity");
        need(!hover.pending()&&!hover.next_transition_ms()&&!hover.refresh_due(100+delay+500),"visible help does not schedule perpetual repaint");
        cell.text="Changed text";
        need(visible->text!="Changed text","returned help owns its text");
    }
    UiHover hover({500});auto cell=target();auto in=input();
    need(!hover.update(0,cell,in),"begin stable target");
    for(std::uint64_t now=1;now<500;++now){in.pointer=std::array<std::int32_t,2>{30+int(now%5),30};
        need(!hover.update(now,cell,in),"small movement within stable target retains dwell without early reveal");}
    need(bool(hover.update(500,cell,in)),"small movement does not restart every frame");
    // Every semantic change restarts even if the pointer and displayed name match.
    for(unsigned change=0;change<5;++change){
        hover.clear();cell=target();in=input();hover.update(100,cell,in);need(bool(hover.update(600,cell,in)),"precondition visible");
        if(change==0)++cell.id;
        if(change==1)cell.text+=" updated";
        if(change==2)++cell.rect.width;
        if(change==3)++in.context;
        if(change==4)++in.selection;
        need(!hover.update(601,cell,in)&&!hover.update(1100,cell,in)&&bool(hover.update(1101,cell,in)),"identity/text/rect/context/selection invalidates visible help");
    }
    // Blocking transitions drop help immediately and cannot accrue hidden dwell.
    for(unsigned blocker=0;blocker<9;++blocker){
        hover.clear();cell=target();in=input();hover.update(0,cell,in);need(bool(hover.update(500,cell,in)),"visible before blocking");
        auto blocked=in;std::optional<UiHoverTarget> candidate=cell;
        if(blocker==0)blocked.pointer.reset();
        if(blocker==1)blocked.pointer=std::array<std::int32_t,2>{110,30}; // Exclusive right edge.
        if(blocker==2)candidate.reset();
        if(blocker==3)blocked.enabled=false;
        if(blocker==4)blocked.focused=false;
        if(blocker==5)blocked.modal=true;
        if(blocker==6)blocked.button_down=true;
        if(blocker==7)blocked.interacted=true;
        if(blocker==8)blocked.pointer=std::array<std::int32_t,2>{30,60}; // Exclusive bottom edge.
        need(!hover.update(501,candidate,blocked)&&!hover.update(5000,candidate,blocked),"leave/focus/click/modal/gate hides immediately");
        need(!hover.pending()&&!hover.next_transition_ms()&&!hover.refresh_due(10000),"cancelled help never schedules a stale wakeup");
        need(!hover.update(5001,cell,in)&&!hover.update(5500,cell,in)&&bool(hover.update(5501,cell,in)),"return/reenter starts a fresh full delay");
    }
    cell=target();in=input();hover.clear();hover.update(1000,cell,in);need(bool(hover.update(1500,cell,in)),"visible before clock rollback");
    need(!hover.update(2,cell,in)&&!hover.update(501,cell,in)&&bool(hover.update(502,cell,in)),"backward clock never underflows to visible");
    hover.clear();const auto max=std::numeric_limits<std::uint64_t>::max();
    need(!hover.update(max-500,cell,in)&&bool(hover.update(max,cell,in)),"deadline near integer maximum uses elapsed subtraction");
    for(unsigned malformed=0;malformed<5;++malformed){
        hover.clear();auto bad=target();
        if(malformed==0)bad.text.clear();
        if(malformed==1)bad.text.assign(4097,'x');
        if(malformed==2)bad.text=std::string("one\0two",7);
        if(malformed==3)bad.rect.width=0;
        if(malformed==4)bad.rect.height=-1;
        need(!hover.update(0,bad,in)&&!hover.update(10000,bad,in),"bad text/rectangle cannot become visible");
    }
    UiHover limited({0,3});cell.text="abc";need(bool(limited.update(0,cell,in)),"inclusive text byte limit");
    cell.text="abcd";need(!limited.update(1,cell,in),"oversized text is rejected without truncating original UTF8");
    UiHover instant({0});cell=target();cell.rect={std::numeric_limits<std::int32_t>::max()-10,0,20,40};
    in.pointer=std::array<std::int32_t,2>{std::numeric_limits<std::int32_t>::max(),1};
    need(bool(instant.update(0,cell,in)),"rectangle arithmetic is widened before adding extent");
    for(const auto bytes:{std::size_t(0),std::size_t(65537)}){
        bool rejected=false;try{UiHover invalid({500,bytes});}catch(const std::invalid_argument&){rejected=true;}
        need(rejected,"invalid allocation budget rejected");
    }
}
}
int main(){try{cases();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
