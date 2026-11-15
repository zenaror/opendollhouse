/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <SDL.h>
#include "InputSequence.h"
#include "odh/cursor.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace odh::desktop {
enum class PortableInputProfile { Disabled, Switch, Vita, Android };
// Portable SDL uses one unscaled renderer target and one explicit letterbox.
// This retains raw finger coordinates: SDL logical-size filtering otherwise
// clamps touches in black bars to an actionable canvas edge before our loop.
struct PortableCanvas {
    SDL_Rect Box{};
    int Width=0,Height=0;
    static PortableCanvas Fit(int outputWidth,int outputHeight,int width,int height){
        PortableCanvas r;r.Width=width;r.Height=height;
        if(outputWidth<=0||outputHeight<=0||width<=0||height<=0)return r;
        const double scale=std::min(double(outputWidth)/width,double(outputHeight)/height);
        r.Box.w=std::max(1,int(std::floor(width*scale)));r.Box.h=std::max(1,int(std::floor(height*scale)));
        r.Box.x=(outputWidth-r.Box.w)/2;r.Box.y=(outputHeight-r.Box.h)/2;return r;
    }
    std::optional<std::array<int,2>> Point(double x,double y,int outputWidth,int outputHeight)const{
        if(!std::isfinite(x)||!std::isfinite(y)||x<0||x>1||y<0||y>1||!Box.w||!Box.h)return {};
        const double px=std::min(x*outputWidth,std::nextafter(double(outputWidth),0.)),
            py=std::min(y*outputHeight,std::nextafter(double(outputHeight),0.));
        if(px<Box.x||py<Box.y||px>=Box.x+Box.w||py>=Box.y+Box.h)return {};
        return std::array<int,2>{std::clamp(int((px-Box.x)*Width/Box.w),0,Width-1),
            std::clamp(int((py-Box.y)*Height/Box.h),0,Height-1)};
    }
};

// All primary inputs share one owner and the existing ordered InputSequence.
// SDL events are translated once; touch-generated mouse and mouse-generated
// touch are ignored. No synthetic SDL keyboard/mouse events or App::Frame calls.
class SdlPortableInput {
public:
    static void Configure(PortableInputProfile profile){
        if(profile==PortableInputProfile::Disabled)return;
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");
        SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS,"0",SDL_HINT_OVERRIDE); // physical positions
    }
    SdlPortableInput(SDL_Window* window,SDL_Renderer* renderer,int width,int height,
            PortableInputProfile profile,bool openDevices=true)
        :window_(window),renderer_(renderer),width_(width),height_(height),profile_(profile){
        x_=width/2.;y_=height/2.;
        if(Enabled()){
            oldCursor_=SDL_ShowCursor(SDL_QUERY);SDL_ShowCursor(SDL_DISABLE);
            if(openDevices)OpenFirst();
            SDL_version version;SDL_GetVersion(&version);const auto box=Canvas();
            std::printf("[portable-input] SDL=%u.%u.%u video=%s window=%u keyboardFocus=%d mouseFocus=%d touchDevices=%d canvas=%dx%d rect=%d,%d,%d,%d\n",
                version.major,version.minor,version.patch,SDL_GetCurrentVideoDriver()?SDL_GetCurrentVideoDriver():"none",SDL_GetWindowID(window_),
                int(SDL_GetKeyboardFocus()==window_),int(SDL_GetMouseFocus()==window_),SDL_GetNumTouchDevices(),width_,height_,box.Box.x,box.Box.y,box.Box.w,box.Box.h);
        }
    }
    ~SdlPortableInput(){if(pad_)SDL_GameControllerClose(pad_);if(Enabled()&&oldCursor_>=0)SDL_ShowCursor(oldCursor_);}
    SdlPortableInput(const SdlPortableInput&)=delete;
    SdlPortableInput& operator=(const SdlPortableInput&)=delete;
    bool Enabled()const{return profile_!=PortableInputProfile::Disabled;}
    bool Focused()const{
        const auto flags=SDL_GetWindowFlags(window_);
        return active_&&SDL_GetKeyboardFocus()==window_&&(flags&SDL_WINDOW_SHOWN)&&!(flags&SDL_WINDOW_MINIMIZED);
    }
    PortableCanvas Canvas()const{
        int w=0,h=0;if(SDL_GetRendererOutputSize(renderer_,&w,&h)!=0)return {};
        return PortableCanvas::Fit(w,h,width_,height_);
    }
    void LogGeometry(const char* reason)const{
        int ww=0,wh=0,ow=0,oh=0,lw=0,lh=0;
        SDL_GetWindowSize(window_,&ww,&wh);SDL_GetRendererOutputSize(renderer_,&ow,&oh);
        SDL_RenderGetLogicalSize(renderer_,&lw,&lh);SDL_Rect viewport{};SDL_RenderGetViewport(renderer_,&viewport);
        float sx=0,sy=0;SDL_RenderGetScale(renderer_,&sx,&sy);SDL_RendererInfo info{};SDL_GetRendererInfo(renderer_,&info);
        const auto box=PortableCanvas::Fit(ow,oh,width_,height_);
        std::printf("[presentation] reason=%s video=%s renderer=%s window=%dx%d output=%dx%d canvas=%dx%d dst=%d,%d,%d,%d logical=%dx%d viewport=%d,%d,%d,%d scale=%.3f,%.3f\n",
            reason,SDL_GetCurrentVideoDriver()?SDL_GetCurrentVideoDriver():"unknown",info.name?info.name:"unknown",
            ww,wh,ow,oh,width_,height_,box.Box.x,box.Box.y,box.Box.w,box.Box.h,lw,lh,viewport.x,viewport.y,viewport.w,viewport.h,sx,sy);
        std::fflush(stdout);
    }
    void InitialState(simapp::AppInput& state)const{if(Enabled())SetPoint(state);}
    // Caller cancellation (focus, leave, overflow) also clears portable capture.
    void Reset(const simapp::AppInput& state){
        if(owner_==Owner::Touch)blockedFinger_=finger_;
        mouseBlocked_=mouseBlocked_||(owner_==Owner::Mouse&&state.Touching);
        for(size_t side=0;side<rear_.size();++side){
            if(rear_[side])blockedRear_[side]=rear_[side];
            rear_[side].reset();
        }
        owner_=Owner::None;finger_.reset();padPrimary_=padSecondary_=padTriggerHeld_=false;
        buttons_.fill(false);padBlocked_=true;pointerInside_=false;
    }
    bool HasController()const{return pad_!=nullptr;}
    SDL_JoystickID ControllerId()const{return padId_;}
    // Exposed to the finite virtual-device fixture; ordinary runtime opens the
    // first SDL-mapped device, and does not assume raw joystick button indices.
    bool Open(int device){
        if(pad_||!SDL_IsGameController(device))return false;
        pad_=SDL_GameControllerOpen(device);if(!pad_)return false;
        padId_=SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_));
        padBlocked_=true;
        std::printf("[portable-input] controller id=%d name=%s mapping=SDL positions profile=%s\n",int(padId_),
            SDL_GameControllerName(pad_),profile_==PortableInputProfile::Vita?"vita":profile_==PortableInputProfile::Android?"android":"switch");
        return true;
    }
    bool Handle(const SDL_Event& event,simapp::AppInput& state,InputSequence& inputs,bool focused,bool textOwner,bool holdingObject=false){
        if(!Enabled())return false;
        if(event.type==SDL_CONTROLLERDEVICEADDED){if(!pad_)Open(event.cdevice.which);return true;}
        if(event.type==SDL_CONTROLLERDEVICEREMOVED){
            if(pad_&&event.cdevice.which==padId_){Cancel(state,inputs);SDL_GameControllerClose(pad_);pad_=nullptr;padId_=-1;OpenFirst();}
            return true;
        }
        if(event.type==SDL_CONTROLLERDEVICEREMAPPED){
            if(pad_&&event.cdevice.which==padId_)Cancel(state,inputs);
            return true;
        }
        if(event.type==SDL_APP_WILLENTERBACKGROUND){active_=false;Cancel(state,inputs);return true;}
        if(event.type==SDL_APP_DIDENTERFOREGROUND){active_=true;return true;}
        const bool touch=event.type==SDL_FINGERDOWN||event.type==SDL_FINGERMOTION||event.type==SDL_FINGERUP;
        const bool controller=event.type==SDL_CONTROLLERAXISMOTION||event.type==SDL_CONTROLLERBUTTONDOWN||event.type==SDL_CONTROLLERBUTTONUP;
        const bool mouse=event.type==SDL_MOUSEMOTION||event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP;
        if(!touch&&!controller&&!mouse)return false;
        if(touch){
            const auto& e=event.tfinger;
            if(touchTrace_++<16)std::printf("[portable-input] touch type=%u device=%lld finger=%lld window=%u expected=%u xy=%.4f,%.4f focus=%d owner=%d\n",
                event.type,(long long)e.touchId,(long long)e.fingerId,e.windowID,SDL_GetWindowID(window_),e.x,e.y,int(focused),int(owner_));
            if(e.windowID!=SDL_GetWindowID(window_)||e.touchId==SDL_MOUSE_TOUCHID)return true;
            // SDL Vita reports normalized back-panel coordinates directly,
            // without mirroring. The two rear halves are buttons, not a cursor.
            if(profile_==PortableInputProfile::Vita&&e.touchId==1){
                RearTouch(event,state,inputs,focused,textOwner);return true;
            }
            if(profile_==PortableInputProfile::Vita&&e.touchId!=0)return true;
            if(blockedFinger_){if(event.type==SDL_FINGERUP&&Same(*blockedFinger_,e))blockedFinger_.reset();return true;}
            if(!focused||inputs.Discarding())return true;
            const auto point=FingerPoint(e.x,e.y);
            if(event.type==SDL_FINGERDOWN){
                if(owner_!=Owner::None||!point)return true;
                finger_=Finger{e.touchId,e.fingerId};owner_=Owner::Touch;Use(*point,state);Primary(true,state,inputs);
            }else if(owner_==Owner::Touch&&finger_&&Same(*finger_,e)){
                if(!point){Cancel(state,inputs);return true;}
                Use(*point,state);
                if(event.type==SDL_FINGERUP){Primary(false,state,inputs);owner_=Owner::None;finger_.reset();}
                else inputs.Push(state,InputSequence::Kind::State);
            }
            return true;
        }
        if(controller){
            const auto id=event.type==SDL_CONTROLLERAXISMOTION?event.caxis.which:event.cbutton.which;
            if((event.type!=SDL_CONTROLLERAXISMOTION||event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT)&&padTrace_++<16)
                std::printf("[portable-input] pad type=%u id=%d expected=%d control=%u value=%d focus=%d blocked=%d\n",event.type,int(id),int(padId_),
                    event.type==SDL_CONTROLLERAXISMOTION?event.caxis.axis:event.cbutton.button,
                    event.type==SDL_CONTROLLERAXISMOTION?int(event.caxis.value):int(event.cbutton.state),int(focused),int(padBlocked_));
            if(id!=padId_||!focused||inputs.Discarding()||padBlocked_)return true;
            if(event.type==SDL_CONTROLLERAXISMOTION){
                if(event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT){
                    if(event.caxis.value>16000&&!padTriggerHeld_){padTriggerHeld_=true;ControllerPrimary(true,state,inputs);}
                    else if(event.caxis.value<8000&&padTriggerHeld_){padTriggerHeld_=false;ControllerPrimary(false,state,inputs);}
                }else if(event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT){
                    if(event.caxis.value>16000&&!padSecondary_){
                        padSecondary_=true;SelectAndCenter(state,inputs,textOwner);
                    }else if(event.caxis.value<8000)padSecondary_=false;
                }
            }else if(event.cbutton.button<buttons_.size()){
                const auto b=SDL_GameControllerButton(event.cbutton.button);const bool down=event.type==SDL_CONTROLLERBUTTONDOWN;
                if(buttons_[b]==down)return true;
                buttons_[b]=down;
                if(down)Button(b,state,inputs,textOwner,holdingObject);
            }
            return true;
        }
        const Uint32 which=event.type==SDL_MOUSEMOTION?event.motion.which:event.button.which;
        const Uint32 window=event.type==SDL_MOUSEMOTION?event.motion.windowID:event.button.windowID;
        if(which==SDL_TOUCH_MOUSEID||window!=SDL_GetWindowID(window_))return true;
        if(event.type==SDL_MOUSEBUTTONUP&&event.button.button==SDL_BUTTON_LEFT)mouseBlocked_=false;
        if(!focused||inputs.Discarding()||owner_==Owner::Touch||owner_==Owner::Controller||owner_==Owner::RearTrigger)return true;
        const auto point=MousePoint(event.type==SDL_MOUSEMOTION?event.motion.x:event.button.x,
            event.type==SDL_MOUSEMOTION?event.motion.y:event.button.y);
        if(!point){
            if(owner_==Owner::Mouse)Cancel(state,inputs);
            else {pointerInside_=false;state.TouchX=state.TouchY=-1;inputs.Push(state,InputSequence::Kind::State);}
            return true;
        }
        Use(*point,state);
        if(event.type==SDL_MOUSEMOTION)inputs.Push(state,InputSequence::Kind::State);
        else if(event.button.button==SDL_BUTTON_LEFT){
            const bool down=event.type==SDL_MOUSEBUTTONDOWN;
            if(mouseBlocked_||(!down&&owner_!=Owner::Mouse))return true;
            owner_=(down||state.MouseRightHeld)?Owner::Mouse:Owner::None;Primary(down,state,inputs);
        }else if(event.button.button==SDL_BUTTON_RIGHT){
            const bool down=event.type==SDL_MOUSEBUTTONDOWN;
            if(!down&&!state.MouseRightHeld)return true;
            state.MouseRightHeld=down;owner_=(down||state.Touching)?Owner::Mouse:Owner::None;
            auto input=state;input.MouseRightDown=down;input.MouseRightUp=!down;inputs.Push(input,InputSequence::Kind::Action);
        }
        return true;
    }
    void Frame(double milliseconds,simapp::AppInput& state,InputSequence& inputs,bool focused,bool textOwner=false){
        if(!Enabled()||!focused||inputs.Discarding())return;
        // A mouse released while unfocused must clear suppression before its
        // next gesture. This never cancels a touch/controller-owned capture.
        if(owner_==Owner::None&&mouseBlocked_&&!(SDL_GetMouseState(nullptr,nullptr)&SDL_BUTTON_LMASK))mouseBlocked_=false;
        // A physical mouse can lose its release event. Its sampled state is
        // authoritative only for its own capture, never for touch/controller.
        if(owner_==Owner::Mouse){
            const auto held=SDL_GetMouseState(nullptr,nullptr);
            if(SDL_GetMouseFocus()!=window_ || (state.Touching&&!(held&SDL_BUTTON_LMASK)) ||
               (state.MouseRightHeld&&!(held&SDL_BUTTON_RMASK))){
                Cancel(state,inputs);if(!(held&SDL_BUTTON_LMASK))mouseBlocked_=false;return;
            }
        }
        if(pad_&&padBlocked_){
            bool neutral=SDL_GameControllerGetAxis(pad_,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)<8000&&
                SDL_GameControllerGetAxis(pad_,SDL_CONTROLLER_AXIS_TRIGGERLEFT)<8000;
            for(int b=0;b<SDL_CONTROLLER_BUTTON_MAX;++b)neutral=neutral&&!SDL_GameControllerGetButton(pad_,SDL_GameControllerButton(b));
            if(neutral)padBlocked_=false;
        }
        if(pad_&&!padBlocked_){
            // Poll axes only as final state; button/trigger edges stay ordered events.
            const float cx=Axis(SDL_GameControllerGetAxis(pad_,SDL_CONTROLLER_AXIS_LEFTX));
            const float cy=-Axis(SDL_GameControllerGetAxis(pad_,SDL_CONTROLLER_AXIS_LEFTY));
            if(!textOwner){state.CircleX=std::clamp(state.CircleX+cx,-1.f,1.f);state.CircleY=std::clamp(state.CircleY+cy,-1.f,1.f);}
            if(owner_==Owner::None||owner_==Owner::Controller||owner_==Owner::RearTrigger){
                const double dt=std::isfinite(milliseconds)?std::clamp(milliseconds,0.,100.)/1000.:0.;
                const auto rx=Axis(SDL_GameControllerGetAxis(pad_,SDL_CONTROLLER_AXIS_RIGHTX));
                const auto ry=Axis(SDL_GameControllerGetAxis(pad_,SDL_CONTROLLER_AXIS_RIGHTY));
                if(rx||ry)pointerInside_=true;
                x_=std::clamp(x_+rx*480.*dt,0.,double(width_-1));
                y_=std::clamp(y_+ry*480.*dt,0.,double(height_-1));
            }
        }
        SetPoint(state);inputs.Push(state,InputSequence::Kind::State);
    }
    // Upload only the tiny cursor rectangle after the full canvas upload. The
    // engine canvas is immutable; CUR inversion complements its underlying RGB.
    static bool UploadCursor(SDL_Texture* texture,const std::vector<std::uint32_t>& canvas,int width,int height,
            const simapp::AppInput& input,const odh::CursorImage* image, bool restoreBackground=false){
        if(!image||input.PointerCancelled||input.TouchX<0||input.TouchY<0)return true;
        const auto count=std::size_t(image->width)*image->height;
        if(!image->width||!image->height||image->pixels.size()!=count||image->invert.size()!=count||canvas.size()!=std::size_t(width)*height)return false;
        const int ox=input.TouchX-image->hotspot_x,oy=input.TouchY-image->hotspot_y;
        const int x=std::max(0,ox),y=std::max(0,oy),w=std::min(width,ox+image->width)-x,h=std::min(height,oy+image->height)-y;
        if(w<=0||h<=0)return true;
        const SDL_Rect rect{x,y,w,h};
        if(restoreBackground)
            return SDL_UpdateTexture(texture,&rect,canvas.data()+std::size_t(y)*width+x,width*sizeof(Uint32))==0;
        std::vector<Uint32> pixels(std::size_t(w)*h);
        for(int py=0;py<h;++py)for(int px=0;px<w;++px){
            const auto at=std::size_t(y+py-oy)*image->width+(x+px-ox);auto color=canvas[std::size_t(y+py)*width+x+px];
            const auto& p=image->pixels[at];if(image->invert[at])color^=0x00ffffffu;
            else if(p[3])color=Uint32(p[0])|(Uint32(p[1])<<8)|(Uint32(p[2])<<16)|(Uint32(p[3])<<24);
            pixels[std::size_t(py)*w+px]=color;
        }
        return SDL_UpdateTexture(texture,&rect,pixels.data(),w*sizeof(Uint32))==0;
    }
private:
    enum class Owner{None,Touch,Controller,Mouse,RearTrigger};
    struct Finger{SDL_TouchID Device;SDL_FingerID Id;};
    SDL_Window* window_;SDL_Renderer* renderer_;int width_,height_;PortableInputProfile profile_;
    SDL_GameController* pad_=nullptr;SDL_JoystickID padId_=-1;int oldCursor_=-1;
    Owner owner_=Owner::None;std::optional<Finger> finger_,blockedFinger_;
    std::array<std::optional<Finger>,2> rear_{},blockedRear_{};
    bool active_=true,padPrimary_=false,padSecondary_=false,padTriggerHeld_=false,padBlocked_=true,mouseBlocked_=false,pointerInside_=true;
    std::array<bool,SDL_CONTROLLER_BUTTON_MAX> buttons_{};
    double x_=0,y_=0;unsigned touchTrace_=0,padTrace_=0;
    static bool Same(Finger f,const SDL_TouchFingerEvent& e){return f.Device==e.touchId&&f.Id==e.fingerId;}
    static float Axis(Sint16 value){const float v=float(value)/32768.f;return std::abs(v)<.18f?0.f:std::copysign((std::abs(v)-.18f)/.82f,v);}
    void SetPoint(simapp::AppInput& state)const{state.DesktopPointer=true;state.TouchX=pointerInside_?int(x_):-1;state.TouchY=pointerInside_?int(y_):-1;}
    void Use(const std::array<int,2>& point,simapp::AppInput& state){x_=point[0];y_=point[1];pointerInside_=true;SetPoint(state);}
    std::optional<std::array<int,2>> FingerPoint(double x,double y)const{
        int w=0,h=0;if(SDL_GetRendererOutputSize(renderer_,&w,&h)!=0)return {};
        return PortableCanvas::Fit(w,h,width_,height_).Point(x,y,w,h);
    }
    std::optional<std::array<int,2>> MousePoint(int x,int y)const{
        int w=0,h=0;SDL_GetWindowSize(window_,&w,&h);if(w<=0||h<=0)return {};
        return FingerPoint(double(x)/w,double(y)/h);
    }
    void Cancel(simapp::AppInput& state,InputSequence& inputs){
        Reset(state);state=simapp::AppInput();state.DesktopPointer=true;state.TouchX=state.TouchY=-1;inputs.Cancel();
    }
    void Primary(bool down,simapp::AppInput& state,InputSequence& inputs){
        state.Touching=down;auto input=state;input.TouchDown=down;input.TouchUp=!down;inputs.Push(input,InputSequence::Kind::Action);
    }
    void ControllerPrimary(bool down,simapp::AppInput& state,InputSequence& inputs){
        if(down){if(owner_!=Owner::None)return;owner_=Owner::Controller;padPrimary_=true;pointerInside_=true;SetPoint(state);Primary(true,state,inputs);}
        else if(padPrimary_){Primary(false,state,inputs);padPrimary_=false;owner_=Owner::None;}
    }
    void SelectAndCenter(const simapp::AppInput& state,InputSequence& inputs,bool textOwner){
        if(textOwner)return;
        // Same two gameplay commands as Space then Enter; no synthetic keys,
        // text insertion or repeated action while the trigger stays held.
        auto input=state;input.DesktopAction=simapp::DesktopCommand::NextSim;
        inputs.Push(input,InputSequence::Kind::Action);
        input.DesktopAction=simapp::DesktopCommand::CenterSim;
        inputs.Push(input,InputSequence::Kind::Action);
    }
    void RearTouch(const SDL_Event& event,simapp::AppInput& state,InputSequence& inputs,bool focused,bool textOwner){
        const auto& e=event.tfinger;
        for(auto& blocked:blockedRear_)if(blocked&&Same(*blocked,e)){
            if(event.type==SDL_FINGERUP)blocked.reset();
            return;
        }
        // Bind the role at DOWN. Crossing the midpoint never presses another
        // trigger, nor releases a trigger still held by its original finger.
        for(size_t side=0;side<rear_.size();++side)if(rear_[side]&&Same(*rear_[side],e)){
            if(event.type==SDL_FINGERUP){
                if(side==1&&owner_==Owner::RearTrigger){Primary(false,state,inputs);owner_=Owner::None;}
                rear_[side].reset();
            }
            return;
        }
        if(event.type!=SDL_FINGERDOWN||!focused||inputs.Discarding()||
            !std::isfinite(e.x)||!std::isfinite(e.y)||e.x<0||e.x>1||e.y<0||e.y>1)return;
        const size_t side=e.x<.5f?0:1;
        if(rear_[side]||blockedRear_[side])return;
        rear_[side]=Finger{e.touchId,e.fingerId};
        if(side==0)SelectAndCenter(state,inputs,textOwner);
        else if(owner_==Owner::None){
            owner_=Owner::RearTrigger;pointerInside_=true;SetPoint(state);Primary(true,state,inputs);
        }
        // An occupied front/mouse/controller capture is not replaced. The
        // rejected rear gesture cannot acquire it later without a fresh DOWN.
    }
    void Button(SDL_GameControllerButton b,simapp::AppInput& state,InputSequence& inputs,bool textOwner,bool holdingObject){
        using C=simapp::DesktopCommand;auto input=state;const bool shift=buttons_[SDL_CONTROLLER_BUTTON_BACK];
        if(textOwner){
            if(b==SDL_CONTROLLER_BUTTON_A){input.TextAccept=true;input.Down=simapp::BTN_A;input.TextEvents.push_back({simapp::AppInput::TextEvent::Type::Accept,{}});}
            else if(b==SDL_CONTROLLER_BUTTON_B){input.TextCancel=true;input.Down=simapp::BTN_B;input.TextEvents.push_back({simapp::AppInput::TextEvent::Type::Cancel,{}});}
            else return;
        }else switch(b){
        case SDL_CONTROLLER_BUTTON_A:input.DesktopAction=C::Live;break;
        case SDL_CONTROLLER_BUTTON_X:input.DesktopAction=C::Buy;break;
        case SDL_CONTROLLER_BUTTON_Y:input.DesktopAction=C::Build;break;
        case SDL_CONTROLLER_BUTTON_B:
            if(holdingObject)input.DesktopDelete=true;
            else {input.Down=simapp::BTN_B;input.TextCancel=true;}break;
        case SDL_CONTROLLER_BUTTON_START:input.DesktopAction=C::Pause;break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:input.Down=simapp::BTN_L;input.DesktopRotateLeft=true;break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:input.Down=simapp::BTN_R;input.DesktopRotateRight=true;break;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:input.Down=simapp::BTN_ZL;break;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:input.Down=simapp::BTN_ZR;break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:if(shift)input.Down=simapp::BTN_ZR;else input.DesktopAction=C::ZoomIn;break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:if(shift)input.Down=simapp::BTN_ZL;else input.DesktopAction=C::ZoomOut;break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:input.DesktopAction=C::SpeedNormal;break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:input.DesktopAction=shift?C::SpeedUltra:C::SpeedFast;break;
        default:return;
        }
        inputs.Push(input,InputSequence::Kind::Action);
    }
    void OpenFirst(){
        for(int i=0;i<SDL_NumJoysticks();++i)if(Open(i))return;
        std::printf("[portable-input] no SDL-mapped controller available (%d joysticks)\n",SDL_NumJoysticks());
    }
};
}
