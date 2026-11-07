// SPDX-License-Identifier: MPL-2.0
// TS1 catalog/holder flow: pinned Simitone UIBuyBrowsePanel/UIObjectHolder.
#pragma once
#include "odh/object_trade.hpp"
#include "odh/object_group.hpp"
#include <string>
namespace odh {
struct BuyCatalogItem {
    std::uint32_t guid=0,price=0;
    std::uint8_t category=0,room_sort=0;
    std::string name,description;
};
enum class BuyModePhase { closed,browsing,holding_purchase,holding_existing,awaiting_command };
enum class BuyPreviewValidity { unknown,blocked,clear };
enum class BuyModeIssue { none,no_ground,insufficient_funds,stale_identity,operation_failed };
enum class BuyModeCommandKind { purchase,move,sell };
struct BuyModeCommand {
    BuyModeCommandKind kind=BuyModeCommandKind::purchase;
    std::uint32_t guid=0;
    ObjectTradeIdentity target{};
    EntityPosition position{};
};
// Owned presentation data, not physical-group authority. Unknown legacy uses
// unrotated VM construction offsets and its former BaseObject anchor.
struct BuyModeNativePreviewPart {
    NativeObjectGroupOffset authored; // Exact native194 triple; XY tile units.
    std::uint8_t heading_delta=0; // (this live body's heading - selected heading)&7.
    std::optional<NativeObjectGroupOffset> current; // Exact captured1a0; required, never inferred.
};
struct BuyModePreviewPart {
    std::uint32_t guid=0;
    std::int16_t offset_x=0,offset_y=0,offset_level=0,graphic=0;
    std::vector<std::int16_t> dynamic_sprite_flags;
    std::optional<BuyModeNativePreviewPart> native;
};
struct BuyModePreviewGroup {
    std::size_t base_index=0; // Native: selected anchor, independent of price BaseObject.
    std::vector<BuyModePreviewPart> parts;
    // Presence requires native fields on EVERY part, including anchor delta0.
    // (actual source head heading - selected heading)&7. No heading inference.
    std::optional<std::uint8_t> native_head_heading_delta;
    std::optional<std::uint8_t> native_selected_heading; // Exact captured selected OD1; required with native fields.
};
// Legacy transform remains unchanged. Native fields reproduce selected-anchor
// placement: zero total turn preserves captured current1a0; a nonzero turn
// recomposes authored194 (odd head headings zero XY). Both paths retain each
// body's relative heading and exact signed16/signed8 coordinate wrapping.
// No World writes, permissions, resource lookup or VM execution.
std::vector<EntityPosition> buy_mode_group_positions(const BuyModePreviewGroup&,const EntityPosition& anchor);
struct BuyModeExistingObject {
    ObjectTradeIdentity identity;
    std::uint32_t guid=0;
    EntityPosition position{};
    // Real live Graphic/DynamicSpriteFlags, when available. Missing graphic
    // uses only the catalog base image; preview never executes Init to guess it.
    std::optional<std::int16_t> graphic;
    std::vector<std::int16_t> dynamic_sprite_flags;
    std::optional<BuyModePreviewGroup> preview_group;
};
struct BuyModeHeld {
    std::uint32_t guid=0;
    std::optional<ObjectTradeIdentity> existing;
    std::optional<EntityPosition> position;
    std::uint8_t direction=0;
    std::optional<std::int16_t> graphic;
    std::vector<std::int16_t> dynamic_sprite_flags;
    BuyPreviewValidity validity=BuyPreviewValidity::unknown;
    std::optional<BuyModePreviewGroup> preview_group;
};
// Presentation/intent only. No World, VM, RNG, price publication or ID allocator.
// The real transaction service owns affordability/permissions/lifecycle/save.
class BuyModeController {
public:
    explicit BuyModeController(std::vector<BuyCatalogItem>,std::size_t page_size=8);
    void open(std::optional<std::int32_t> budget);
    void close();
    void cancel(); // Held item→catalog; catalog→closed. No command is emitted.
    void set_budget(std::optional<std::int32_t> value){budget_=value;}
    void set_page_size(std::size_t);
    void category(std::uint8_t);
    void page(int delta);
    bool select(std::size_t visible_index);
    bool select_existing(const BuyModeExistingObject&);
    void hover(std::optional<EntityPosition>,BuyPreviewValidity=BuyPreviewValidity::unknown);
    void rotate(int quarter_turns);
    bool place();
    bool sell();
    std::optional<BuyModeCommand> take_command();
    // Failed real transaction retains the preview; it does not become a UI-only
    // success. A committed command must reopen its actual checkpoint instead.
    void command_rejected(std::string diagnostic);
    BuyModePhase phase()const{return phase_;}
    bool active()const{return phase_!=BuyModePhase::closed;}
    std::uint8_t category()const{return category_;}
    std::size_t page()const{return page_;}
    std::size_t page_count()const;
    std::size_t page_size()const{return page_size_;}
    std::vector<const BuyCatalogItem*> visible()const;
    const BuyCatalogItem* find(std::uint32_t)const;
    const std::optional<BuyModeHeld>& held()const{return held_;}
    const std::optional<std::int32_t>& budget()const{return budget_;}
    BuyModeIssue issue()const{return issue_;}
    const std::string& diagnostic()const{return diagnostic_;}
private:
    bool emit(BuyModeCommandKind);
    std::vector<BuyCatalogItem> catalog_;
    BuyModePhase phase_=BuyModePhase::closed;
    std::uint8_t category_=0;
    std::size_t page_=0,page_size_=8;
    std::optional<std::int32_t> budget_;
    std::optional<BuyModeHeld> held_;
    std::optional<BuyModeCommand> pending_;
    BuyModeIssue issue_=BuyModeIssue::none;
    std::string diagnostic_;
};
}
