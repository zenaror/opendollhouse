/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "app/Canvas.h"
#include "catalog_info_view.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace simapp {
class LotSession;
class LotScreen;
namespace original {
// Original PC 6e598c executable,0051e3b5: buttons4112/4113/4114
// select token types{0,4,5}/{7}/{8}. These are not OBJD categories.
enum class DesktopInventorySort { gifts,magic,other };
struct DesktopExpansionPopupViewport {
    std::string root,identity;
    std::shared_ptr<odh_host::CatalogInfoView> view;
    odh_host::CatalogInfoScrollState scroll;
    odh_host::CatalogInfoLayout layout{};
    bool open=false;
    bool visible=false; // Set only by a successful paint in the current frame.
    void Close()noexcept{open=false;visible=false;}
};
inline bool DesktopInventoryIncludes(DesktopInventorySort sort,std::int32_t type) noexcept {
    switch(sort){
    case DesktopInventorySort::gifts:return type==0||type==4||type==5;
    case DesktopInventorySort::magic:return type==7;
    case DesktopInventorySort::other:return type==8;
    }
    return false;
}
struct DesktopInventoryItem {
    std::size_t source_index=0;
    std::int32_t type=0;
    std::uint32_t guid=0;
    std::uint16_t count=0;
    std::optional<std::string> name,description; // Already UTF8, selected provider CTSS.
    std::uint8_t text_language=0;
    bool text_utf8=false;
    // Authored two-frame catalog BMP, retained without catalog heuristics,
    // resizing or substituting a world sprite. Original0050e78c draws width/2.
    std::shared_ptr<const Canvas> icon_strip;
};
struct DesktopInventorySnapshot {
    bool has_selected_person=false;
    std::optional<std::int16_t> neighbor_id;
    // Absent provider is distinct from genuinely empty inventory. Keep every
    // source record, including internal tokens, zero counts and unknown GUIDs;
    // presentation applies the authenticated button filter without data loss.
    std::optional<std::vector<DesktopInventoryItem>> items;
    std::string title;
    std::uint8_t language=0;
};
DesktopInventorySnapshot CaptureDesktopInventory(LotSession&);

// Input state is separate from pixel geometry until the original panel has
// been observed. Consumers pass a source identity returned by VisibleItems;
// this controller never turns an inventory click into a purchase/world action.
class DesktopInventorySelection {
public:
    DesktopInventorySort sort()const noexcept{return sort_;}
    std::size_t page()const noexcept{return page_;}
    std::optional<std::size_t> selected_source()const noexcept{return selected_;}
    bool popup_open()const noexcept{return popup_open_;}
    DesktopExpansionPopupViewport popup;
    void Sort(DesktopInventorySort);
    void Reconcile(const DesktopInventorySnapshot&,std::size_t page_size);
    bool Page(int delta,const DesktopInventorySnapshot&,std::size_t page_size);
    bool Select(std::size_t source_index,const DesktopInventorySnapshot&);
    void ClosePopup()noexcept{popup_open_=false;popup.Close();}
    // Active occurrences, signed-GUID sorted. Repeated GUIDs alias the final
    // source supplying their shared tile/count, preserving original page size.
    std::vector<std::size_t> VisibleItems(const DesktopInventorySnapshot&)const;
private:
    DesktopInventorySort sort_=DesktopInventorySort::gifts;
    std::optional<std::int16_t> neighbor_;
    std::optional<std::size_t> selected_;
    std::size_t page_=0;
    bool popup_open_=false;
    std::vector<std::array<std::uint64_t,4>> source_signature_;
};

struct DesktopFameSnapshot {
    bool has_selected_person=false;
    std::optional<std::int16_t> score,half_stars,high_watermark;
    std::array<std::optional<std::int16_t>,6> skills{};
    // Original sums eligible mutual friends outside the family,then displays
    // half-star fractions. Do not substitute the engine's one-way/truncated
    // promotion helper while that eligibility contract is being recovered.
    std::optional<std::int32_t> friend_half_stars;
    std::optional<std::string> title,level_name,level_title,level_description,friends_name,friends_format;
    std::array<std::string,6> skill_names{};
    std::array<std::optional<std::int32_t>,6> required_skills{};
    std::optional<std::int32_t> required_friend_stars;
    std::uint8_t language=0;
    std::optional<std::int16_t> neighbor_id;
};
DesktopFameSnapshot CaptureDesktopFame(LotSession&);
struct DesktopFameSelection {
    bool level_open=false;
    std::optional<std::int16_t> neighbor_id;
    DesktopExpansionPopupViewport popup;
    void ClosePopup()noexcept{level_open=false;popup.Close();}
    void Reconcile(const DesktopFameSnapshot& snapshot){
        if(neighbor_id!=snapshot.neighbor_id){neighbor_id=snapshot.neighbor_id;level_open=false;popup.Close();}
    }
};
// Default-language PC geometry from original0052bc54/0052cae6 and its
// initializers. Asian150px variants explicitly remain unsupported until the
// surrounding panel/world viewport uses their matching measured geometry.
struct DesktopFameLayout {
    Rect title,stars,level,friends;
    std::array<Rect,6> skill_labels,skill_bars;
};
std::optional<DesktopFameLayout> DesktopOriginalFameLayout(int width,int height,std::uint8_t language);
bool DrawDesktopFame(Canvas&,LotScreen&,const DesktopFameSnapshot&,DesktopFameSelection&);
struct DesktopInventoryLayout {
    Rect title,previous,next;
    std::array<Rect,3> sorts;
    std::vector<Rect> cells;
};
std::optional<DesktopInventoryLayout> DesktopOriginalInventoryLayout(int width,int height,std::uint8_t language);
bool DrawDesktopInventory(Canvas&,LotScreen&,const DesktopInventorySnapshot&,DesktopInventorySelection&);
bool HandleDesktopInventoryInput(LotSession&,DesktopInventorySelection&,const std::string& id,std::size_t page_size);
bool ScrollDesktopExpansionPopup(DesktopExpansionPopupViewport&,int wheel,int x,int y);
}}
#endif
