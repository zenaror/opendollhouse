/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "original_settings_view.hpp"
#include <memory>
namespace FSO { namespace LotView { class World; } }
namespace simapp {
class Canvas;class LotScreen;struct AppInput;
namespace front {struct FrontSettings;}
namespace original {
struct DesktopOptionsSnapshot {
    odh_host::OriginalSettingsState state;
    odh_host::OriginalSettingsCapabilities capabilities{};
};
// Observed persisted preferences, with capabilities requiring current consumers.
DesktopOptionsSnapshot CaptureDesktopOptions(const front::FrontSettings&,bool audio,std::optional<bool> live_free_will,
    const FSO::LotView::World* graphics_world=nullptr);
bool ApplyDesktopOption(front::FrontSettings&,const odh_host::OriginalSettingsEvent&,
    const odh_host::OriginalSettingsCapabilities&);
class DesktopOptionsPanel {
public:
    DesktopOptionsPanel();~DesktopOptionsPanel();
    void Reset();
    std::optional<odh_host::OriginalSettingsPage> Page()const;
    void Draw(Canvas&,LotScreen&);
    void Input(const AppInput&,LotScreen&);
    bool Tap(const std::string&);
private:
    struct Impl;std::unique_ptr<Impl> p_;
};
}}
#endif
