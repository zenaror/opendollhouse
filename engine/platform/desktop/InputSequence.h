/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. See https://mozilla.org/MPL/2.0/. */
#pragma once
#include "app/Input.h"
#include <cstddef>
#include <deque>
#include <utility>

namespace odh::desktop {

// App::Frame also renders, ticks audio and advances load states even with dt=0.
// Deliver one ordered packet per ordinary frame instead of calling it for every
// SDL event. Only adjacent motion/state packets or adjacent text edits coalesce.
class InputSequence {
public:
    enum class Kind { State, Text, Action };
    struct Packet {
        simapp::AppInput Input;
        bool Quit = false;
        Kind Type = Kind::State;
    };
    static constexpr size_t MaxPackets = 32;
    static constexpr size_t MaxTextBytes = 16384;
    static constexpr size_t MaxTextEvents = 1024;

    InputSequence() { _Delivered.DesktopPointer=true; _Delivered.TouchX=_Delivered.TouchY=-1; }
    void BeginPoll() { _Discard=false; }
    // SDL supplies actual keyboard focus, independently of mouse hover focus.
    // Dropping focus discards queued edges and publishes the same cancellation
    // used by overflow/leave; inactive hover cannot repopulate camera state.
    void SetFocused(bool focused) {
        if(_Focused==focused)return;
        _Focused=focused;if(!focused)Cancel();
    }
    size_t Pending() const { return _Queue.size(); }
    bool Discarding() const { return _Discard; }
    bool TakeOverflow() { bool result=_Overflow;_Overflow=false;return result; }

    // A lost focus/overflow discards stale clicks and releases capture outside
    // every control. It must not synthesize Escape, which can accept some dialogs.
    void Cancel() {
        bool quit=false;for(const auto& packet:_Queue)quit=quit||packet.Quit;
        _Queue.clear();
        _Delivered=simapp::AppInput();
        _Delivered.DesktopPointer=true;
        _Delivered.TouchX=_Delivered.TouchY=-1;
        auto release=_Delivered;release.TouchUp=true;release.MouseRightUp=true;release.PointerCancelled=true;
        _Queue.push_back({std::move(release),false,Kind::Action});
        if(quit)_Queue.push_back({_Delivered,true,Kind::Action});
        _Discard=true;
    }

    bool Push(simapp::AppInput input,Kind kind,bool quit=false) {
        if(quit && (_Discard || !_Focused)){
            // Window close is inert and independent of gameplay focus/capture.
            for(const auto& packet:_Queue)if(packet.Quit)return true;
            simapp::AppInput idle;idle.DesktopPointer=true;idle.TouchX=idle.TouchY=-1;
            _Queue.push_back({std::move(idle),true,Kind::Action});return true;
        }
        if(_Discard || !_Focused)return false;
        if(input.TextEvents.size()>MaxTextEvents || TextBytes(input)>MaxTextBytes)return Overflow();
        if(!_Queue.empty() && !quit && !_Queue.back().Quit && _Queue.back().Type==kind) {
            if(kind==Kind::State) { _Queue.back().Input=std::move(input);return true; }
            if(kind==Kind::Text && SameState(_Queue.back().Input,input)) {
                auto& target=_Queue.back().Input;
                if(target.TextEvents.size()+input.TextEvents.size()>MaxTextEvents ||
                   TextBytes(target)+TextBytes(input)>MaxTextBytes)return Overflow();
                target.TextEvents.insert(target.TextEvents.end(),input.TextEvents.begin(),input.TextEvents.end());
                target.TextUTF8+=input.TextUTF8;
                target.TextBackspace|=input.TextBackspace;target.TextDelete|=input.TextDelete;
                target.TextHome|=input.TextHome;target.TextEnd|=input.TextEnd;
                target.TextLeft|=input.TextLeft;target.TextRight|=input.TextRight;
                target.TextAccept|=input.TextAccept;target.TextCancel|=input.TextCancel;
                return true;
            }
        }
        if(kind==Kind::State && !quit) {
            const auto& previous=_Queue.empty()?_Delivered:_Queue.back().Input;
            if(SameState(previous,input))return true;
        }
        if(_Queue.size()>=MaxPackets)return Overflow();
        _Queue.push_back({std::move(input),quit,kind});
        return true;
    }

    Packet Next() {
        if(_Queue.empty())return {_Delivered,false,Kind::State};
        auto packet=std::move(_Queue.front());_Queue.pop_front();
        _Delivered=StateOnly(packet.Input);
        return packet;
    }

private:
    std::deque<Packet> _Queue;
    simapp::AppInput _Delivered;
    bool _Discard=false,_Overflow=false,_Focused=true;
    bool Overflow() { Cancel();_Overflow=true;return false; }
    static size_t TextBytes(const simapp::AppInput& input) {
        size_t bytes=input.TextUTF8.size();
        for(const auto& event:input.TextEvents)bytes+=event.Text.size();
        return bytes;
    }
    static bool SameState(const simapp::AppInput& a,const simapp::AppInput& b) {
        return a.TouchX==b.TouchX && a.TouchY==b.TouchY && a.Touching==b.Touching && a.MouseRightHeld==b.MouseRightHeld && a.Held==b.Held &&
            a.DesktopCtrl==b.DesktopCtrl && a.DesktopShift==b.DesktopShift &&
            a.CircleX==b.CircleX && a.CircleY==b.CircleY && a.CStickX==b.CStickX && a.CStickY==b.CStickY;
    }
    static simapp::AppInput StateOnly(const simapp::AppInput& input) {
        simapp::AppInput state;
        state.DesktopPointer=true;
        state.TouchX=input.TouchX;state.TouchY=input.TouchY;state.Touching=input.Touching;
        state.MouseRightHeld=input.MouseRightHeld;
        state.Held=input.Held;state.DesktopCtrl=input.DesktopCtrl;state.DesktopShift=input.DesktopShift;
        state.CircleX=input.CircleX;state.CircleY=input.CircleY;
        state.CStickX=input.CStickX;state.CStickY=input.CStickY;
        return state;
    }
};
}
