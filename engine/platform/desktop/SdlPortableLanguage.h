/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "SdlPortableInput.h"
#include "app/ui/OriginalTextBackend.h"
#include "language_picker.hpp"
#include <optional>
#include <stdexcept>
#include <string>

namespace odh::desktop {

// The picker uses the same canvas (and front-touch coordinates) as the game.
inline std::optional<uint8_t> ChoosePortableLanguage(SDL_Window* window,SDL_Renderer* renderer,SDL_Texture* texture,
        SdlPortableInput& portable,int width,int height,uint8_t initial,bool invalidSettings){
    simapp::original::InstallTextBackendIfNeeded();
    odh_host::LanguagePicker picker(initial);
    const bool vita=portable.Profile()==PortableInputProfile::Vita;
    // SDL is configured for physical positions: south is Cross on Vita, east is A on Switch.
    const auto confirm=vita?SDL_CONTROLLER_BUTTON_A:SDL_CONTROLLER_BUTTON_B;
    const auto cancel=vita?SDL_CONTROLLER_BUTTON_B:SDL_CONTROLLER_BUTTON_A;
    const auto instructions=vita?"Choose language: Up/Down, X to confirm, O to cancel, or tap."
                                :"Choose language: Up/Down, A to confirm, B to cancel, or tap.";
    simapp::AppInput state;
    portable.InitialState(state);
    InputSequence inputs;
    const auto windowID=SDL_GetWindowID(window);
    std::optional<uint8_t> pressedRow;
    int pressedButton=-1,stick=0;
    bool dirty=true;
    std::printf("[language-picker] open selected=%u\n",unsigned(picker.language()));std::fflush(stdout);
    for(;;){
        if(dirty){
            const auto frame=picker.draw(width,height,true,true,
                invalidSettings?"Saved language could not be read. Choose a language to continue.":"",instructions);
            const auto box=portable.Canvas().Box;
            if(SDL_UpdateTexture(texture,nullptr,frame.pixels.data(),width*4)!=0 ||
               SDL_RenderClear(renderer)!=0 || SDL_RenderCopy(renderer,texture,nullptr,&box)!=0)
                throw std::runtime_error(std::string("Language picker presentation failed: ")+SDL_GetError());
            SDL_RenderPresent(renderer);dirty=false;
        }
        SDL_Event e;
        if(!SDL_WaitEventTimeout(&e,100))continue;
        if(e.type==SDL_QUIT || (e.type==SDL_WINDOWEVENT && e.window.windowID==windowID &&
           e.window.event==SDL_WINDOWEVENT_CLOSE)){
            SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);
            return {};
        }
        const bool focused=portable.Focused();
        inputs.BeginPoll();inputs.SetFocused(focused);
        if((e.type==SDL_WINDOWEVENT && e.window.windowID==windowID) ||
           e.type==SDL_APP_WILLENTERBACKGROUND || e.type==SDL_CONTROLLERDEVICEREMOVED){
            dirty=true;
            if(!focused || e.type!=SDL_WINDOWEVENT || e.window.event==SDL_WINDOWEVENT_FOCUS_LOST){
                pressedRow.reset();pressedButton=-1;stick=0;portable.Reset(state);
            }
        }
        if(e.type==SDL_CONTROLLERBUTTONDOWN || e.type==SDL_CONTROLLERBUTTONUP){
            if(!focused || e.cbutton.which!=portable.ControllerId())continue;
            const auto button=e.cbutton.button;
            if(e.type==SDL_CONTROLLERBUTTONDOWN){
                if(button==SDL_CONTROLLER_BUTTON_DPAD_UP || button==SDL_CONTROLLER_BUTTON_DPAD_DOWN){
                    picker.move(button==SDL_CONTROLLER_BUTTON_DPAD_UP?-1:1);dirty=true;
                }else if(button==confirm || button==cancel)pressedButton=button;
            }else if(pressedButton==button){
                return button==confirm?std::optional<uint8_t>(picker.language()):std::nullopt;
            }
            continue;
        }
        if(e.type==SDL_CONTROLLERAXISMOTION && e.caxis.which==portable.ControllerId() &&
           e.caxis.axis==SDL_CONTROLLER_AXIS_LEFTY && focused){
            const int value=e.caxis.value;
            if(std::abs(value)<8000)stick=0;
            if(std::abs(value)>16000){
                const int direction=value<0?-1:1;
                if(direction!=stick){stick=direction;picker.move(direction);dirty=true;}
            }
            continue;
        }
        if((e.type==SDL_FINGERDOWN || e.type==SDL_FINGERUP || e.type==SDL_FINGERMOTION) &&
           vita && e.tfinger.touchId!=0)continue;
        portable.Handle(e,state,inputs,focused,false);
        while(inputs.Pending()){
            const auto in=inputs.Next().Input;
            if(in.PointerCancelled){pressedRow.reset();continue;}
            if(!in.TouchDown && !in.TouchUp)continue;
            auto hit=picker;
            const bool inside=hit.click(in.TouchX,in.TouchY,width);
            if(in.TouchDown){
                pressedRow.reset();
                if(inside){picker=hit;pressedRow=hit.language();dirty=true;}
            }else{
                if(inside && pressedRow==hit.language())return hit.language();
                pressedRow.reset();
            }
        }
    }
}
}
