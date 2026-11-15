/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "app/Input.h"
#include "window_input.hpp"
namespace simapp { namespace original {
inline odh_host::WindowInput PointerInput(const AppInput& in,int x=0,int y=0){
    odh_host::WindowInput result;
    result.pointer_cancelled=in.PointerCancelled||in.TouchX<0||in.TouchY<0;
    if(result.pointer_cancelled)return result;
    result.pointer_position=std::array<int32_t,2>{in.TouchX-x,in.TouchY-y};
    result.primary_held=in.Touching;
    result.pointer_cancelled=in.PointerCancelled;
    if(in.TouchDown)result.primary_click=result.pointer_position;
    if(in.TouchDown)result.pointer_events.push_back({odh_host::WindowPointerKind::press,in.TouchX-x,in.TouchY-y,in.DesktopShift,in.DesktopCtrl});
    result.pointer_events.push_back({odh_host::WindowPointerKind::motion,in.TouchX-x,in.TouchY-y,in.DesktopShift,in.DesktopCtrl});
    if(in.TouchUp)result.pointer_events.push_back({odh_host::WindowPointerKind::release,in.TouchX-x,in.TouchY-y,in.DesktopShift,in.DesktopCtrl});
    return result;
}
}}
#endif
