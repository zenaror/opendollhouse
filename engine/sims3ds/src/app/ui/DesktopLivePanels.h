/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "live_panel_view.hpp"
#include "app/ui/DesktopExpansionPanels.h"
namespace simapp {
class Canvas;
class LotSession;
class LotScreen;
namespace original {
struct DesktopCareerState {
    odh_host::LiveCareerSelection selection;
    DesktopExpansionPopupViewport popup;
    void Close()noexcept{selection.close();popup.Close();}
    void Reset(){selection.reconcile({});popup.Close();}
};
// Original needs resources and current VM values, rendered by the shared PC shaper.
void DrawDesktopNeedsPanel(Canvas&,LotSession&);
void DrawDesktopNeedsPanel(Canvas&,LotSession&,const std::array<int,8>& change_arrows);
void OfferDesktopControlHelp(Canvas&,LotScreen&);
void OfferDesktopNeedsHelp(Canvas&,LotScreen&);
void OfferDesktopLiveTabHelp(Canvas&,LotScreen&);
// The same resource-derived Needs window used by painting. Other live tabs,
// family controls and other locales retain their existing control-panel bounds.
std::optional<odh::UiBitmapRect> DesktopNeedsPanelBounds(int width,int height,LotSession&);
bool HandleDesktopCareerInput(LotSession&,DesktopCareerState&,const std::string&);
// A missing family/provider is unavailable, distinct from a real zero count.
std::optional<int> DesktopFamilyFriendsSnapshot(LotSession&);
// Presentation DTO only: the current Sims3DS VM remains the sole authority.
odh_host::LivePanelSnapshot DesktopLiveSnapshot(LotSession&, odh_host::LivePanelTab);
void DrawDesktopLivePanel(Canvas&, LotScreen&, odh_host::LivePanelTab,
                          const odh_host::LivePanelSnapshot&, int& relationshipPage, int& interestPage, DesktopCareerState&);
}}
#endif
