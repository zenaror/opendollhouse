/* SPDX-License-Identifier: MPL-2.0 */
#include "SdlPortableInput.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace odh::desktop;
using simapp::AppInput;
static void Check(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
static std::vector<AppInput> Drain(InputSequence& q){std::vector<AppInput> r;while(q.Pending())r.push_back(q.Next().Input);return r;}
static SDL_Event Finger(Uint32 type,SDL_Window* w,SDL_FingerID id,float x,float y,SDL_TouchID device=0){
 SDL_Event e{};e.type=type;e.tfinger.windowID=SDL_GetWindowID(w);e.tfinger.touchId=device;e.tfinger.fingerId=id;e.tfinger.x=x;e.tfinger.y=y;return e;
}
static SDL_Event Pad(Uint32 type,SDL_JoystickID id,Uint8 button){SDL_Event e{};e.type=type;e.cbutton.which=id;e.cbutton.button=button;e.cbutton.state=type==SDL_CONTROLLERBUTTONDOWN?SDL_PRESSED:SDL_RELEASED;return e;}
int main(){SDL_SetMainReady();try{
 SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
 SdlPortableInput::Configure(PortableInputProfile::Switch);
 Check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_JOYSTICK|SDL_INIT_EVENTS)==0,SDL_GetError());
 struct End{~End(){SDL_Quit();}} end;
 std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("input fixture",0,0,1280,720,SDL_WINDOW_SHOWN),SDL_DestroyWindow);Check(bool(window),SDL_GetError());
 std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(),-1,SDL_RENDERER_SOFTWARE),SDL_DestroyRenderer);Check(bool(renderer),SDL_GetError());
 const auto box=PortableCanvas::Fit(1280,720,800,600);Check(box.Box.x==160&&box.Box.y==0&&box.Box.w==960&&box.Box.h==720,"uniform letterbox");
 Check(box.Point(.5,.5,1280,720)==std::optional<std::array<int,2>>({400,300}),"center maps once");
 Check(!box.Point(0,.5,1280,720)&&!box.Point(1,.5,1280,720)&&!box.Point(.875,.5,1280,720),"black bars/right boundary rejected");
 Check(box.Point(.125,0,1280,720)==std::optional<std::array<int,2>>({0,0}),"content origin");
 Check(PortableCanvas::Fit(800,600,800,600).Point(1,1,800,600)==std::optional<std::array<int,2>>({799,599}),"physical last pixel remains actionable");
 Check(!box.Point(std::nan(""),.5,1280,720),"nonfinite rejected");
 { // Exercise the exact raw-target presentation used by portable main.cpp.
 std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> t(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_STREAMING,800,600),SDL_DestroyTexture);
 std::vector<Uint32> pixels(800*600,0xff224466u);Check(SDL_UpdateTexture(t.get(),nullptr,pixels.data(),800*4)==0,"fixture texture upload");
 SDL_SetRenderDrawColor(renderer.get(),0,0,0,255);SDL_RenderClear(renderer.get());Check(SDL_RenderCopy(renderer.get(),t.get(),nullptr,&box.Box)==0,"explicit content rectangle renders");
 auto pixel=[&](int x,int y){Uint32 v=0;const SDL_Rect r{x,y,1,1};Check(SDL_RenderReadPixels(renderer.get(),&r,SDL_PIXELFORMAT_ABGR8888,&v,4)==0,"read in-memory presentation");return v;};
 Check(pixel(159,360)==0xff000000u&&pixel(160,360)==0xff224466u&&pixel(1119,360)==0xff224466u&&pixel(1120,360)==0xff000000u,"drawn letterbox agrees with input boundary");
 SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);auto raw=Finger(SDL_FINGERDOWN,window.get(),99,.01f,.5f);Check(SDL_PushEvent(&raw)==1,"raw touch fixture enqueued");SDL_Event event{};Check(SDL_PollEvent(&event)&&event.type==SDL_FINGERDOWN&&event.tfinger.x==.01f,"portable event keeps black-bar coordinates");
 Check(SDL_RenderSetLogicalSize(renderer.get(),800,600)==0,"unchanged PC automatic logical presentation available");
 raw=Finger(SDL_FINGERDOWN,window.get(),99,.01f,.5f);Check(SDL_PushEvent(&raw)==1,"logical touch fixture enqueued");
 // Peep this event after the real renderer watch, independent of a preceding
 // PollEvent cycle's internal sentinel; no SDL touch conversion is simulated.
 Check(SDL_PeepEvents(&event,1,SDL_GETEVENT,SDL_FINGERDOWN,SDL_FINGERDOWN)==1&&event.tfinger.x==0,"SDL logical mode clamps bar touch, motivating raw portable rectangle");
 Check(SDL_RenderSetLogicalSize(renderer.get(),0,0)==0,"restore raw portable target after PC fixture");
 }
 const int device=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,SDL_CONTROLLER_AXIS_MAX,SDL_CONTROLLER_BUTTON_MAX,0);Check(device>=0,SDL_GetError());
 SDL_Joystick* joystick=SDL_JoystickOpen(device);Check(joystick!=nullptr,SDL_GetError());
 { // One runtime owner and one real SDL virtual controller, no App/VM/display.
 SdlPortableInput portable(window.get(),renderer.get(),800,600,PortableInputProfile::Switch);
 Check(portable.HasController(),"SDL-mapped virtual controller opens");
 InputSequence q;AppInput state;portable.InitialState(state);q.SetFocused(true);q.BeginPoll();portable.Frame(0,state,q,true);Drain(q);
 auto touch=[&](SDL_Event e,bool focus=true){portable.Handle(e,state,q,focus,false);};
 touch(Finger(SDL_FINGERDOWN,window.get(),7,.5f,.5f));
 SDL_Event synth{};synth.type=SDL_MOUSEBUTTONDOWN;synth.button.which=SDL_TOUCH_MOUSEID;synth.button.windowID=SDL_GetWindowID(window.get());synth.button.button=SDL_BUTTON_LEFT;touch(synth);
 touch(Finger(SDL_FINGERDOWN,window.get(),8,.8f,.8f));touch(Finger(SDL_FINGERUP,window.get(),8,.8f,.8f));
 touch(Finger(SDL_FINGERMOTION,window.get(),7,.6f,.5f));touch(Finger(SDL_FINGERUP,window.get(),7,.6f,.5f));
 auto packets=Drain(q);unsigned downs=0,ups=0;for(auto& p:packets){downs+=p.TouchDown;ups+=p.TouchUp;}
 Check(downs==1&&ups==1&&packets.front().TouchX==400&&packets.back().TouchX==506&&!packets.back().Touching,"one finger, no synthesized duplicate, ordered drag/release");
 q.BeginPoll();touch(Finger(SDL_FINGERDOWN,window.get(),1,.01f,.5f));Check(!q.Pending(),"bar down never acquires");
 auto wrong=Finger(SDL_FINGERDOWN,window.get(),1,.5f,.5f);wrong.tfinger.windowID++;touch(wrong);Check(!q.Pending(),"foreign window rejected");
 touch(Finger(SDL_FINGERDOWN,window.get(),1,.5f,.5f));touch(Finger(SDL_FINGERMOTION,window.get(),1,.01f,.5f));
 packets=Drain(q);Check(packets.size()==1&&packets[0].PointerCancelled&&packets[0].TouchX==-1,"leaving content cancels, never click-up");
 q.BeginPoll();touch(Finger(SDL_FINGERDOWN,window.get(),2,.5f,.5f));Check(!q.Pending(),"second finger cannot adopt cancelled contact");touch(Finger(SDL_FINGERUP,window.get(),1,.01f,.5f));touch(Finger(SDL_FINGERDOWN,window.get(),2,.5f,.5f));Check(q.Pending()==1,"new gesture after old release");
 q.SetFocused(false);portable.Reset(state);state=AppInput();state.DesktopPointer=true;state.TouchX=state.TouchY=-1;
 packets=Drain(q);Check(packets.size()==1&&packets[0].PointerCancelled,"focus discards queued press");touch(Finger(SDL_FINGERUP,window.get(),2,.5f,.5f),false);
 q.SetFocused(true);q.BeginPoll();portable.Frame(0,state,q,true);Drain(q);
 SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_RIGHTX,26000);SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_LEFTY,-20000);SDL_JoystickUpdate();
 const int before=400;state.CircleX=state.CircleY=0;portable.Frame(100,state,q,true);packets=Drain(q);Check(state.TouchX>before&&state.CircleY>0,"right stick cursor, left stick camera");
 SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_RIGHTX,0);SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_LEFTY,0);SDL_JoystickUpdate();
 auto button=[&](Uint8 b,bool holding=false,bool text=false){portable.Handle(Pad(SDL_CONTROLLERBUTTONDOWN,portable.ControllerId(),b),state,q,true,text,holding);portable.Handle(Pad(SDL_CONTROLLERBUTTONUP,portable.ControllerId(),b),state,q,true,text,holding);return Drain(q);};
 packets=button(SDL_CONTROLLER_BUTTON_A);Check(packets.size()==1&&packets[0].DesktopAction==simapp::DesktopCommand::Live,"bottom mode button");
 packets=button(SDL_CONTROLLER_BUTTON_X);Check(packets[0].DesktopAction==simapp::DesktopCommand::Buy,"left mode button");
 packets=button(SDL_CONTROLLER_BUTTON_Y);Check(packets[0].DesktopAction==simapp::DesktopCommand::Build,"top mode button");
 packets=button(SDL_CONTROLLER_BUTTON_B,true);Check(packets[0].DesktopDelete&&!packets[0].Down,"sell grabbed object without Escape cancelling it first");
 packets=button(SDL_CONTROLLER_BUTTON_B);Check((packets[0].Down&simapp::BTN_B)&&!packets[0].DesktopDelete,"otherwise back/cancel");
 packets=button(SDL_CONTROLLER_BUTTON_START);Check(packets[0].DesktopAction==simapp::DesktopCommand::Pause&&!packets[0].Down,"pause does not synthesize map-exit Start");
 packets=button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);Check((packets[0].Down&simapp::BTN_L)&&packets[0].DesktopRotateLeft,"camera/object rotation uses existing command path");
 portable.Handle(Pad(SDL_CONTROLLERBUTTONDOWN,portable.ControllerId(),SDL_CONTROLLER_BUTTON_BACK),state,q,true,false);
 packets=button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);Check(packets[0].DesktopAction==simapp::DesktopCommand::SpeedUltra,"modifier exposes third speed");
 packets=button(SDL_CONTROLLER_BUTTON_DPAD_UP);Check(packets[0].Down&simapp::BTN_ZR,"modifier exposes floor up");
 portable.Handle(Pad(SDL_CONTROLLERBUTTONUP,portable.ControllerId(),SDL_CONTROLLER_BUTTON_BACK),state,q,true,false);
 packets=button(SDL_CONTROLLER_BUTTON_A,false,true);Check(packets[0].TextAccept&&packets[0].DesktopAction==simapp::DesktopCommand::None,"text owner accepts without mode switch");
 SDL_Event axis{};axis.type=SDL_CONTROLLERAXISMOTION;axis.caxis.which=portable.ControllerId();axis.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERRIGHT;axis.caxis.value=32767;
 portable.Handle(axis,state,q,true,false);portable.Handle(axis,state,q,true,false);axis.caxis.value=15000;portable.Handle(axis,state,q,true,false);axis.caxis.value=0;portable.Handle(axis,state,q,true,false);packets=Drain(q);
 Check(packets.size()==2&&packets[0].TouchDown&&packets[0].Touching&&packets[1].TouchUp&&!packets[1].Touching,"trigger one held click/release only");
 // Route actual virtual-driver trigger edges through SDL's event queue.
 SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,32767);SDL_JoystickUpdate();SDL_Event queued{};
 while(SDL_PeepEvents(&queued,1,SDL_GETEVENT,SDL_CONTROLLERAXISMOTION,SDL_CONTROLLERDEVICEREMAPPED)>0)portable.Handle(queued,state,q,true,false);
 SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,-32768);SDL_JoystickUpdate();
 while(SDL_PeepEvents(&queued,1,SDL_GETEVENT,SDL_CONTROLLERAXISMOTION,SDL_CONTROLLERDEVICEREMAPPED)>0)portable.Handle(queued,state,q,true,false);
 packets=Drain(q);downs=0;ups=0;for(auto& p:packets){downs+=p.TouchDown;ups+=p.TouchUp;}Check(downs==1&&ups==1,"actual SDL virtual-trigger queue delivers exactly one click");
 q.BeginPoll();touch(Finger(SDL_FINGERDOWN,window.get(),4,.5f,.5f));axis.caxis.value=32767;portable.Handle(axis,state,q,true,false);touch(Finger(SDL_FINGERUP,window.get(),4,.5f,.5f));axis.caxis.value=0;portable.Handle(axis,state,q,true,false);packets=Drain(q);downs=0;for(auto& p:packets)downs+=p.TouchDown;Check(downs==1,"touch and trigger cannot own two primary clicks");
 // Sampled physical mouse release cancels only that source's capture.
 q.BeginPoll();SDL_Event physical{};physical.type=SDL_MOUSEBUTTONDOWN;physical.button.windowID=SDL_GetWindowID(window.get());physical.button.which=0;physical.button.button=SDL_BUTTON_LEFT;physical.button.x=640;physical.button.y=360;
 portable.Handle(physical,state,q,true,false);Check(state.Touching,"physical mouse press owns capture");Drain(q);
 Check(!(SDL_GetMouseState(nullptr,nullptr)&SDL_BUTTON_LMASK),"fixture physical mouse is released, missing-up event intentionally absent");portable.Frame(0,state,q,true);packets=Drain(q);Check(packets.size()==1&&packets[0].PointerCancelled&&!state.Touching,"missing physical-mouse release cancels instead of clicking");
 q.BeginPoll();portable.Handle(physical,state,q,true,false);Check(state.Touching,"fresh mouse gesture accepted after sampled release");physical.type=SDL_MOUSEBUTTONUP;portable.Handle(physical,state,q,true,false);Drain(q);
 q.BeginPoll();physical.type=SDL_MOUSEBUTTONDOWN;portable.Handle(physical,state,q,true,false);Check(state.Touching,"mouse owns gesture before focus loss");
 q.SetFocused(false);portable.Reset(state);state=AppInput();state.DesktopPointer=true;state.TouchX=state.TouchY=-1;Drain(q);
 q.SetFocused(true);q.BeginPoll();portable.Frame(0,state,q,true);Drain(q);portable.Handle(physical,state,q,true,false);Check(state.Touching,"mouse released outside clears suppression before first new gesture");physical.type=SDL_MOUSEBUTTONUP;portable.Handle(physical,state,q,true,false);Drain(q);
 q.BeginPoll();touch(Finger(SDL_FINGERDOWN,window.get(),40,.5f,.5f));Drain(q);portable.Frame(0,state,q,true);packets=Drain(q);Check(state.Touching&&std::none_of(packets.begin(),packets.end(),[](const auto& p){return p.PointerCancelled;}),"touch hold ignores released physical mouse");touch(Finger(SDL_FINGERUP,window.get(),40,.5f,.5f));Drain(q);
 q.BeginPoll();portable.Frame(0,state,q,true);Drain(q);axis.caxis.value=32767;portable.Handle(axis,state,q,true,false);Drain(q);portable.Frame(0,state,q,true);packets=Drain(q);Check(state.Touching&&std::none_of(packets.begin(),packets.end(),[](const auto& p){return p.PointerCancelled;}),"controller hold ignores released physical mouse");axis.caxis.value=0;portable.Handle(axis,state,q,true,false);Drain(q);
 SDL_Event removed{};removed.type=SDL_CONTROLLERDEVICEREMOVED;removed.cdevice.which=portable.ControllerId();SDL_JoystickDetachVirtual(device);portable.Handle(removed,state,q,true,false);packets=Drain(q);Check(!portable.HasController()&&packets.back().PointerCancelled,"real virtual detach cancels capture");
 }
 SDL_JoystickClose(joystick);
 { // Vita rear halves are independent L2/R2; front remains direct touch.
 const int v=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,SDL_CONTROLLER_AXIS_MAX,SDL_CONTROLLER_BUTTON_MAX,0);Check(v>=0,SDL_GetError());
 SdlPortableInput portable(window.get(),renderer.get(),800,600,PortableInputProfile::Vita);
 InputSequence q;AppInput state;portable.InitialState(state);portable.Frame(0,state,q,true);Drain(q);
 auto rear=[&](Uint32 type,SDL_FingerID id,float x,bool focus=true,bool text=false){portable.Handle(Finger(type,window.get(),id,x,.5f,1),state,q,focus,text);};
 const auto origin=std::array<int,2>{state.TouchX,state.TouchY};
 rear(SDL_FINGERDOWN,10,.75f);auto packets=Drain(q);Check(packets.size()==1&&packets[0].TouchDown&&state.Touching,"rear right starts held cursor click");
 Check(state.TouchX==origin[0]&&state.TouchY==origin[1],"rear buttons never move cursor");
 SDL_Joystick* vitaPad=SDL_JoystickFromInstanceID(portable.ControllerId());Check(vitaPad,"Vita virtual pad instance");SDL_JoystickSetVirtualAxis(vitaPad,SDL_CONTROLLER_AXIS_RIGHTX,32767);SDL_JoystickUpdate();portable.Frame(100,state,q,true);packets=Drain(q);Check(state.Touching&&state.TouchX>origin[0]&&!packets.empty()&&!packets.back().TouchDown,"right stick drags cursor while rear R2 remains held");SDL_JoystickSetVirtualAxis(vitaPad,SDL_CONTROLLER_AXIS_RIGHTX,0);SDL_JoystickUpdate();
 rear(SDL_FINGERDOWN,11,.25f);packets=Drain(q);Check(packets.size()==2&&packets[0].DesktopAction==simapp::DesktopCommand::NextSim&&packets[1].DesktopAction==simapp::DesktopCommand::CenterSim,"rear left sends Space then Enter semantics");
 Check(state.Touching&&packets[0].Touching&&!packets[0].TouchDown&&!packets[1].TouchUp,"left trigger does not release or reclick held right");
 rear(SDL_FINGERMOTION,11,.9f);rear(SDL_FINGERMOTION,10,.1f);Check(!q.Pending()&&state.Touching,"crossing halves neither repeats nor reassigns trigger");
 rear(SDL_FINGERDOWN,12,.75f);rear(SDL_FINGERUP,12,.75f);Check(!q.Pending()&&state.Touching,"second rear finger cannot steal or release right trigger");
 rear(SDL_FINGERUP,11,.9f);Check(!q.Pending()&&state.Touching,"left release leaves right held");
 rear(SDL_FINGERUP,10,.1f);packets=Drain(q);Check(packets.size()==1&&packets[0].TouchUp&&!state.Touching,"right finger release ends exactly one click");
 rear(SDL_FINGERDOWN,13,.25f);Drain(q);rear(SDL_FINGERDOWN,14,.75f);Drain(q);rear(SDL_FINGERUP,14,.75f);packets=Drain(q);Check(packets.size()==1&&packets[0].TouchUp&&!state.Touching,"right release is independent while left remains held");rear(SDL_FINGERMOTION,13,.25f);rear(SDL_FINGERUP,13,.25f);Check(!q.Pending(),"held left does not repeat after independent right release");
 rear(SDL_FINGERDOWN,20,.25f);Drain(q);rear(SDL_FINGERDOWN,21,.75f);Drain(q);
 q.SetFocused(false);portable.Reset(state);state=AppInput();state.DesktopPointer=true;state.TouchX=state.TouchY=-1;packets=Drain(q);Check(packets.size()==1&&packets[0].PointerCancelled&&!packets[0].Touching,"focus cancels both rear roles without selecting anything");
 q.SetFocused(true);q.BeginPoll();rear(SDL_FINGERDOWN,22,.75f);Check(!q.Pending(),"held cancelled rear half requires its old release");
 rear(SDL_FINGERUP,20,.25f);rear(SDL_FINGERUP,21,.75f);rear(SDL_FINGERUP,22,.75f);rear(SDL_FINGERDOWN,23,.75f);packets=Drain(q);Check(packets.size()==1&&packets[0].TouchDown,"fresh rear press accepted after old release");rear(SDL_FINGERUP,23,.75f);Drain(q);
 rear(SDL_FINGERDOWN,30,.25f,true,true);rear(SDL_FINGERMOTION,30,.25f);Check(!q.Pending(),"L2 never types or switches Sims while editing text");rear(SDL_FINGERUP,30,.25f);
 portable.Handle(Finger(SDL_FINGERDOWN,window.get(),31,.55f,.5f,0),state,q,true,false);Drain(q);const int frontX=state.TouchX;
 rear(SDL_FINGERDOWN,32,.75f);Check(!q.Pending()&&state.Touching&&state.TouchX==frontX,"rear cannot take front-owned click");
 portable.Handle(Finger(SDL_FINGERUP,window.get(),31,.55f,.5f,0),state,q,true,false);packets=Drain(q);Check(packets.size()==1&&packets[0].TouchUp,"front gesture retains own release");rear(SDL_FINGERMOTION,32,.75f);rear(SDL_FINGERUP,32,.75f);Check(!q.Pending()&&!state.Touching,"held rear gesture cannot adopt front capture after release");
 rear(SDL_FINGERDOWN,33,.75f);Drain(q);portable.Handle(Finger(SDL_FINGERDOWN,window.get(),34,.8f,.5f,0),state,q,true,false);portable.Handle(Finger(SDL_FINGERUP,window.get(),34,.8f,.5f,0),state,q,true,false);Check(!q.Pending()&&state.Touching,"front cannot release rear-owned click");rear(SDL_FINGERUP,33,.75f);Drain(q);
 rear(SDL_FINGERDOWN,35,std::nan(""));Check(!q.Pending(),"nonfinite rear DOWN rejected");
 const auto id=portable.ControllerId();portable.Frame(0,state,q,true);Drain(q);
 portable.Handle(Pad(SDL_CONTROLLERBUTTONDOWN,id,SDL_CONTROLLER_BUTTON_LEFTSHOULDER),state,q,true,false);portable.Handle(Pad(SDL_CONTROLLERBUTTONUP,id,SDL_CONTROLLER_BUTTON_LEFTSHOULDER),state,q,true,false);
 portable.Handle(Pad(SDL_CONTROLLERBUTTONDOWN,id,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER),state,q,true,false);portable.Handle(Pad(SDL_CONTROLLERBUTTONUP,id,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER),state,q,true,false);packets=Drain(q);Check(packets.size()==2&&packets[0].DesktopRotateLeft&&packets[1].DesktopRotateRight&&!packets[1].TouchDown,"physical Vita L/R remain L1/R1 camera rotation");
 SDL_JoystickDetachVirtual(v);
 }
 { // Android keeps direct touch with no pad, then accepts hotplug/mapping.
 SdlPortableInput portable(window.get(),renderer.get(),800,600,PortableInputProfile::Android);
 InputSequence q;AppInput state;portable.InitialState(state);portable.Frame(0,state,q,true);Drain(q);Check(!portable.HasController(),"Android touch does not require controller");
 portable.Handle(Finger(SDL_FINGERDOWN,window.get(),1,.5f,.5f,9),state,q,true,false);portable.Handle(Finger(SDL_FINGERUP,window.get(),1,.5f,.5f,9),state,q,true,false);auto packets=Drain(q);Check(packets.size()==2&&packets[0].TouchDown&&packets[1].TouchUp,"Android direct touch works before pad connection");
 portable.Handle(Finger(SDL_FINGERDOWN,window.get(),2,.01f,.5f,9),state,q,true,false);Check(!q.Pending(),"Android black-bar touch does not click");
 const int v=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,SDL_CONTROLLER_AXIS_MAX,SDL_CONTROLLER_BUTTON_MAX,0);Check(v>=0,SDL_GetError());SDL_Event added{};added.type=SDL_CONTROLLERDEVICEADDED;added.cdevice.which=v;portable.Handle(added,state,q,true,false);Check(portable.HasController(),"Android opens newly connected mapped controller");portable.Frame(0,state,q,true);Drain(q);
 const auto id=portable.ControllerId();portable.Handle(Pad(SDL_CONTROLLERBUTTONDOWN,id,SDL_CONTROLLER_BUTTON_X),state,q,true,false);portable.Handle(Pad(SDL_CONTROLLERBUTTONUP,id,SDL_CONTROLLER_BUTTON_X),state,q,true,false);packets=Drain(q);Check(packets.size()==1&&packets[0].DesktopAction==simapp::DesktopCommand::Buy,"Android external pad uses physical left-face buy mapping");
 auto axis=[&](Uint8 control,Sint16 value,bool text=false){SDL_Event e{};e.type=SDL_CONTROLLERAXISMOTION;e.caxis.which=id;e.caxis.axis=control;e.caxis.value=value;portable.Handle(e,state,q,true,text);};
 axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,20000);axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,21000);axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,20000);axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,25000);packets=Drain(q);Check(packets.size()==3&&packets[0].TouchDown&&packets[1].DesktopAction==simapp::DesktopCommand::NextSim&&packets[2].DesktopAction==simapp::DesktopCommand::CenterSim,"Android R2 held plus one L2 pair; trigger noise never repeats");
 axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,0);Check(!q.Pending()&&state.Touching,"L2 release does not release R2");axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,0);packets=Drain(q);Check(packets.size()==1&&packets[0].TouchUp&&!state.Touching,"Android R2 release matches its press");
 portable.Handle(Finger(SDL_FINGERDOWN,window.get(),3,.5f,.5f,9),state,q,true,false);Drain(q);axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,20000);Check(!q.Pending(),"front touch wins ownership over R2");portable.Handle(Finger(SDL_FINGERUP,window.get(),3,.5f,.5f,9),state,q,true,false);Drain(q);axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,22000);Check(!q.Pending()&&!state.Touching,"held trigger cannot adopt released front capture on analog noise");axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,0);
 axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,20000,true);axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,25000);Check(!q.Pending(),"text-owned L2 stays suppressed until release");axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,0);
 axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,20000);Drain(q);SDL_JoystickDetachVirtual(v);SDL_Event removed{};removed.type=SDL_CONTROLLERDEVICEREMOVED;removed.cdevice.which=id;portable.Handle(removed,state,q,true,false);packets=Drain(q);Check(!portable.HasController()&&packets.size()==1&&packets[0].PointerCancelled,"Android observed disconnect cancels held controller click");q.BeginPoll();portable.Frame(0,state,q,true);Drain(q);portable.Handle(Finger(SDL_FINGERDOWN,window.get(),4,.6f,.5f,9),state,q,true,false);portable.Handle(Finger(SDL_FINGERUP,window.get(),4,.6f,.5f,9),state,q,true,false);packets=Drain(q);Check(packets.size()==2&&packets[0].TouchDown&&packets[1].TouchUp,"Android touch continues after controller disconnect");
 }
 { // Portable cursor is a presentation-only texture update, including CUR inversion.
 std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> s(SDL_CreateRGBSurfaceWithFormat(0,8,8,32,SDL_PIXELFORMAT_ABGR8888),SDL_FreeSurface);
 std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> r(SDL_CreateSoftwareRenderer(s.get()),SDL_DestroyRenderer);
 std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> t(SDL_CreateTexture(r.get(),SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_STREAMING,8,8),SDL_DestroyTexture);
 std::vector<Uint32> canvas(64,0xff332211u);const auto original=canvas;SDL_UpdateTexture(t.get(),nullptr,canvas.data(),8*4);
 odh::CursorImage cursor;cursor.width=cursor.height=2;cursor.hotspot_x=cursor.hotspot_y=1;cursor.pixels={{{0,0,0,255}},{{0,0,0,0}},{{0,0,0,0}},{{255,255,255,255}}};cursor.invert={0,0,1,0};
 AppInput input;input.TouchX=input.TouchY=3;Check(SdlPortableInput::UploadCursor(t.get(),canvas,8,8,input,&cursor),"cursor upload");SDL_RenderCopy(r.get(),t.get(),nullptr,nullptr);
 std::vector<Uint32> out(64);SDL_RenderReadPixels(r.get(),nullptr,SDL_PIXELFORMAT_ABGR8888,out.data(),8*4);
 Check(out[2*8+2]==0xff000000u&&out[2*8+3]==0xff332211u&&out[3*8+2]==0xffccddeeu&&out[3*8+3]==0xffffffffu,"authored cursor opacity/transparency/inversion/hotspot");Check(canvas==original,"cursor never changes engine canvas");
 }
 {SdlPortableInput disabled(window.get(),renderer.get(),800,600,PortableInputProfile::Disabled);InputSequence q;AppInput state;Check(!disabled.Handle(Finger(SDL_FINGERDOWN,window.get(),1,.5f,.5f),state,q,true,false),"PC keeps existing event path");}
 std::cout<<"PASS portable SDL geometry, ordered single-touch/cancel/focus, real virtual controller/modes/ownership/detach, Vita rear L2/R2, Android touch/controller coexistence, software cursor and PC-disabled path\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
