// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/ui_bitmap.hpp"
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace odh_host {
class OriginalControlPanelArt;
struct OriginalControlPanelLayout;
enum class LivePanelTab : unsigned { needs,relationships,career,personality,interests,house };
// Pinned TS1 VMPersonDataVariable / UIJobSubpanel / UIPersonalitySubpanel.
inline constexpr std::array<unsigned,6> live_skill_indices{10,12,11,17,18,15};
inline constexpr std::array<unsigned,5> live_personality_indices{7,6,3,5,2};
// Original EXE0050f668:19topics share four adult/child pairs.
inline constexpr std::array<unsigned,19> live_interest_indices{46,47,48,49,50,51,52,53,46,47,48,49,13,14,16,20,26,54,55};
inline constexpr std::array<const char*,19> live_interest_icon_keys{
    "cpanel\\buttons\\interesttravel.bmp","cpanel\\buttons\\interestviolence.bmp","cpanel\\buttons\\interestpolitics.bmp",
    "cpanel\\buttons\\interestthe60s.bmp","cpanel\\buttons\\interestweather.bmp","cpanel\\buttons\\interestsports.bmp",
    "cpanel\\buttons\\interestmusic.bmp","cpanel\\buttons\\interestoutdoors.bmp","cpanel\\buttons\\interesttoys.bmp",
    "cpanel\\buttons\\interestaliens.bmp","cpanel\\buttons\\interestpets.bmp","cpanel\\buttons\\interestschool.bmp",
    "cpanel\\buttons\\interestexercise.bmp","cpanel\\buttons\\interestfood.bmp","cpanel\\buttons\\interestparties.bmp",
    "cpanel\\buttons\\intereststyle.bmp","cpanel\\buttons\\interestcharisma.bmp","cpanel\\buttons\\interesthealth.bmp",
    "cpanel\\buttons\\interestromance.bmp"};
struct LiveInterestsInfo {std::int16_t age=0;std::array<std::int16_t,19> raws{};};
using LivePersonDataReader=std::function<std::optional<std::int16_t>(unsigned)>;
// Same immutable boundary for both engine adapters. Original TS1 aliases are
// numeric source words, including names that differ in the inherited TSO enum.
std::optional<LiveInterestsInfo> live_interests_snapshot(const LivePersonDataReader&);
// Original0051e1a1:age0 uses adult topics, ages1..17 children,18+adult.
std::vector<unsigned> live_interest_topics(std::int16_t age);
inline constexpr unsigned live_neighbor_index=31,live_job_index=56,live_job_level_index=57,live_job_performance_index=63;
using LiveCareerLabelReader=std::function<std::optional<std::string>(unsigned)>;
struct LiveCareerHelp {
    std::optional<std::string> title,description;
    std::optional<unsigned> icon_row; // Prepared original22-row JobIconMultiPopup.
};
struct LiveCareerSource {std::int16_t job=0,level=0;unsigned performance_word=63;bool school=false;};
LiveCareerSource live_career_source(std::int16_t age,std::int16_t job,std::int16_t level)noexcept;
std::optional<std::string> live_career_title(const LiveCareerLabelReader&,const LiveCareerLabelReader&,
    unsigned level,bool alternate_gender,bool panel_title);
std::optional<std::string> live_career_hours(std::string format,int start,int end,std::uint8_t language);
// Pure presentation lifetime: no timer callbacks enter the VM or mutate Sim data.
class LiveCareerSelection {
public:
    void reconcile(std::string identity);
    void close()noexcept{selected_.reset();}
    bool toggle(unsigned control);
    bool show(unsigned control);
    std::optional<unsigned> selected()const noexcept{return selected_;}
    bool alert(bool negative,std::uint64_t now_ms)noexcept;
private:
    std::string identity_;
    std::optional<unsigned> selected_;
    std::optional<std::uint64_t> negative_since_;
};
struct LiveSchoolInfo {
    std::int16_t raw=0;
    std::optional<std::string> grade;
};
std::optional<std::string> live_school_grade_label(std::int16_t,const LiveCareerLabelReader&);
struct LiveSchoolLayout {odh::UiBitmapRect title,grade;};
// Original report-card container and its centered variablesans18 button.
LiveSchoolLayout live_school_layout(const OriginalControlPanelLayout&);
struct LiveCareerInfo {
    bool employed=false,school=false;
    std::optional<LiveSchoolInfo> report_card;
    std::string identity;
    std::array<LiveCareerHelp,1> help{}; // Profession consultation; additional original-only controls stay separate.
    std::optional<std::string> hours;

    std::optional<std::uint16_t> job_type;
    std::optional<std::string> performance_label;
    // Absent metadata is not a zero salary or a fictitious job title.
    std::optional<std::string> title;
    std::optional<std::int32_t> salary,start_hour,end_hour;
    std::optional<std::int16_t> performance;
    std::array<std::optional<std::int32_t>,6> required_skills{}; // Raw 0..1000.
};
struct LiveRelationshipInfo {
    std::int16_t neighbor_id=0;
    std::string name;
    std::shared_ptr<const odh::BitmapImage> portrait;
    // Directional neighbor matrix offsets 0 (daily), 2 (long term).
    // A missing long-term element is absent, never an invented zero.
    std::optional<std::int16_t> daily,long_term;
};
struct LiveHouseInfo {
    std::optional<std::uint32_t> area_square_feet,bedrooms,bathrooms;
    std::array<std::optional<std::int16_t>,5> ratings{}; // Size/Furnishings/Yard/Upkeep/Layout,0..100.
    // Already localized actual classification/name for Live133[9], not filename.
    std::optional<std::string> lot_name;
};
struct LivePanelSnapshot {
    bool has_selected_person=false;
    std::array<std::optional<std::int16_t>,6> skills{};
    std::array<std::optional<std::int16_t>,5> personality{};
    std::optional<LiveCareerInfo> career;
    // nullopt = unavailable; empty vector = genuinely no known people.
    std::optional<std::vector<LiveRelationshipInfo>> relationships;
    std::optional<std::string> zodiac; // Already localized real zodiac; absent hides it.
    std::optional<LiveInterestsInfo> interests;
    std::optional<LiveHouseInfo> house;
};
enum class HouseAreaUnits { square_feet,square_metres };
// Original interface locale uses imperial for USEnglish1/UKEnglish2 only.
HouseAreaUnits original_house_area_units(std::uint8_t authenticated_language);
std::uint32_t house_display_area(std::uint32_t square_feet,HouseAreaUnits);
struct LivePanelText {
    std::string career="Job",skills="Skills",performance="Performance",salary="Salary",unemployed="Unemployed",
        personality="Personality",relationships="Relationships",no_relationships="";
    std::array<std::string,6> skill_names{"Cooking","Mechanical","Charisma","Body","Logic","Creativity"};
    std::array<std::string,5> personality_names{"Neat","Outgoing","Active","Playful","Nice"};
    std::string salary_unavailable="",performance_unavailable="",report_card="";
    std::string interests="Interests";
    std::array<std::string,19> interest_descriptions{};
    std::array<std::string,19> interest_names{"Travel","Money","Politics","The 60's","Weather","Sports","Music","Outdoors","Toys","Aliens","Pets","School","Exercise","Food","Parties","Style","Hollywood","Technology","Romance"};
    std::string house="House",house_missing="—";
    std::array<std::string,5> house_rating_names{"Size","Furnishings","Yard","Upkeep","Layout"};
    std::array<std::string,4> house_value_formats{"Sq. Ft.: %d","Bedrooms: %d","Bathrooms: %d","Lot: %s"};
    std::array<std::string,5> house_rating_descriptions{};
    HouseAreaUnits house_area_units=HouseAreaUnits::square_feet;
};
struct LivePanelLayout {
    odh::UiBitmapRect title,career_title,salary,performance,performance_title,performance_icon,career_icon,zodiac,previous,next;
    std::array<odh::UiBitmapRect,6> skill_labels,skill_bars;
    std::array<odh::UiBitmapRect,5> personality_labels,personality_bars;
    std::vector<odh::UiBitmapRect> relationship_faces,relationship_daily,relationship_long_term;
};
struct LiveHouseLayout {
    odh::UiBitmapRect title;
    std::array<odh::UiBitmapRect,4> values;
    std::array<odh::UiBitmapRect,5> rating_labels,rating_bars;
};
LiveHouseLayout live_house_layout(const OriginalControlPanelLayout&);
struct LiveInterestCell {unsigned topic=0;odh::UiBitmapRect icon,bar;};
struct LiveInterestLayout {
    std::vector<LiveInterestCell> cells;
    odh::UiBitmapRect previous,next;
    std::size_t page=0,pages=0;
};
LiveInterestLayout live_interest_layout(const OriginalControlPanelLayout&,std::int16_t age,std::size_t page);
LivePanelLayout live_panel_layout(const OriginalControlPanelLayout&);
std::array<bool,6> live_panel_capabilities(const LivePanelSnapshot&) noexcept;
// Saturation is presentation-only. Snapshot raw values are never rewritten.
unsigned live_panel_points(std::int32_t raw) noexcept;
// Original403515 / Work STR4096 grades. Returned index is zero based; the
// original string accessor004705f2 takes1..5 and subtracts one internally.
unsigned live_job_performance_grade(std::int16_t raw) noexcept;
// Provider returns already decoded text from the current selected Work STR4096.
// A missing label never falls back to a raw word or an invented grade name.
std::optional<std::string> live_job_performance_label(std::int16_t,const LiveCareerLabelReader&);
enum class LiveTextAlignment { left,center,right };
using LivePanelTextPainter=std::function<void(odh::RasterFrame&,std::string_view,const odh::UiBitmapRect&,bool,LiveTextAlignment)>;
class LivePanelView {
public:
    explicit LivePanelView(std::shared_ptr<const OriginalControlPanelArt>,LivePanelText={});
    LivePanelTab tab()const noexcept{return tab_;}
    bool select(LivePanelTab,const LivePanelSnapshot&) noexcept;
    void reconcile(const LivePanelSnapshot&) noexcept;
    bool click(const OriginalControlPanelLayout&,std::int32_t x,std::int32_t y,const LivePanelSnapshot&);
    void draw(odh::RasterFrame&,const LivePanelSnapshot&,LivePanelTextPainter={})const;
    std::optional<std::string> hover_text(const OriginalControlPanelLayout&,std::int32_t,std::int32_t,const LivePanelSnapshot&)const;
    std::size_t relationship_page()const noexcept{return relationship_page_;}
    std::size_t interest_page()const noexcept{return interest_page_;}
private:
    std::shared_ptr<const OriginalControlPanelArt> art_;
    LivePanelText text_;
    LivePanelTab tab_=LivePanelTab::needs;
    std::size_t relationship_page_=0,interest_page_=0;
};
} // namespace odh_host
