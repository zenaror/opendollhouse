/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "app/Canvas.h"
#include "original_control_panel.hpp"
#include "original_queue_view.hpp"
#include "catalog_info_view.hpp"
#include "catalog_object_thumbnail.hpp"
#include "odh/avatar_body.hpp"
#include <memory>
#include <set>
namespace FSO { namespace Content { class GameObject; } namespace SimAntics { class VMAvatar; class VMEntity; } }
namespace simapp {
// Boundary to the preserved presentation components. The running engine owns
// every Sim/action; this adapter supplies only their immutable UI input data.
class OriginalPCUI {
public:
    static OriginalPCUI& Get();
    ~OriginalPCUI();
    void DrawPanel(Canvas&,odh_host::OriginalPanelMode,const odh_host::OriginalPanelState&);
    void DrawPauseLabel(Canvas&,const std::string& rawCaption);
    void DrawPortrait(Canvas&,odh::UiBitmapRect,bool selected,FSO::SimAntics::VMAvatar*);
    const Canvas* Face(FSO::SimAntics::VMAvatar*,int frame=0);
    // Queue identities and layout need no decoded UI artwork.
    static std::vector<odh_host::QueueTile> QueueTiles(FSO::SimAntics::VMAvatar*,int width,int height);
    void DrawQueue(Canvas&,FSO::SimAntics::VMAvatar*);
    void DrawPieHead(Canvas&,FSO::SimAntics::VMAvatar*,
        std::array<std::int32_t,2> center,std::array<std::int32_t,2> target);
    // Same portrait provider and complete-cell composition as the HUD.
    // Callers retain authored faces; generated fallbacks live only in memory.
    bool GenerateFaces(const odh::AvatarBodyStrings&,Canvas& threeFaces);
    // Immutable content definition, not a simulated VMGameObject or VMAvatar.
    bool GenerateFaces(FSO::Content::GameObject*,Canvas& threeFaces);
    Canvas PetBody(FSO::SimAntics::VMAvatar*, const std::string& outfit);
    // Recovered catalog presentation owns layout, tiles, shaped text and scroll.
    // Content and purchase transactions remain owned by the current engine.
    odh::UiBitmapRect DrawCatalogInfo(Canvas&,const odh_host::CatalogInfoContent&,const std::string& selection);
    bool ScrollCatalogInfo(int wheel,int x,int y);
    // Stateless authenticated content composition: no VM, popup state or selection cache.
    static odh_host::CatalogObjectThumbnailResult BuildCatalogThumbnail(std::uint32_t guid);
    // Needs help uses the authored front view and authored zoom3/2/1 fit,
    // independently of the purchase-popup selection and cache.
    static odh_host::CatalogObjectThumbnailResult BuildHelpThumbnail(std::uint32_t guid);
    std::string PrepareCatalogThumbnail(std::uint32_t guid);
    const odh::BitmapImage* CatalogThumbnail(std::uint32_t guid) const;
    void ClearCatalogInfo();
    const std::string& PortraitSource() const;
    std::string ControlAt(int x,int y) const;
    static bool IsControl(const std::string& id);
private:
    static odh_host::CatalogObjectThumbnailResult BuildCatalogThumbnail(std::uint32_t guid,
        std::uint8_t zoom,bool authoredFront);
    explicit OriginalPCUI(const std::string& root);
    struct Impl;std::unique_ptr<Impl> P;
};
}
