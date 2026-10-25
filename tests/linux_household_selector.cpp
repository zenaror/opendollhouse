#include "linux_household_selector.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}

int main(){try{
    constexpr std::uint32_t width=800,height=600;
    odh::Objt object_types{};
    object_types.entries.resize(2);
    object_types.entries[0].guid=850010198;
    object_types.entries[0].metadata=odh::ObjtMetadata{};
    object_types.entries[0].metadata->objd_type=2;
    object_types.entries[0].name="Bob";
    object_types.entries[1].guid=844195932;
    object_types.entries[1].metadata=odh::ObjtMetadata{};
    object_types.entries[1].metadata->objd_type=2;
    object_types.entries[1].name="Betty";
    std::vector<odh::SavedWorldEntity> saved_records(2);
    saved_records[0].saved.object_id=105;saved_records[0].saved.type_id=1;
    saved_records[0].saved.guid=850010198;saved_records[0].serialized_object_type=2;
    saved_records[1].saved.object_id=137;saved_records[1].saved.type_id=2;
    saved_records[1].saved.guid=844195932;saved_records[1].serialized_object_type=2;
    check(odh_host::household_selector_saved_name(105,850010198,saved_records,object_types)=="Bob",
          "saved resident name follows the restored object ID and OBJT type");
    check(odh_host::household_selector_saved_name(137,844195932,saved_records,object_types)=="Betty",
          "second saved resident name follows its own OBJT type");
    check(!odh_host::household_selector_saved_name(105,844195932,saved_records,object_types),
          "resident ID and GUID must identify the same saved avatar");
    auto duplicated_records=saved_records;duplicated_records.push_back(saved_records.front());
    check(!odh_host::household_selector_saved_name(105,850010198,duplicated_records,object_types),
          "ambiguous saved resident identity falls back instead of guessing");

    const std::vector<odh_host::HouseholdSelectorResident> residents{
        {48,"Ada"},{52,"Bea"},{67,"Sim 67"},{71,"Dee"},{89,"Eve"}};
    const auto button=odh_host::household_selector_button_rect(width,height);
    check(odh_host::household_selector_button_hit(width,height,button.x+1,button.y+1),
          "visible Sim button accepts a click");
    check(!odh_host::household_selector_button_hit(width,height,button.x+button.width,button.y+1),
          "button hit area ends at its right edge");

    const auto first=odh_host::household_selector_pick(residents,width,height,20,35);
    const auto fifth=odh_host::household_selector_pick(residents,width,height,20,92);
    check(first&&*first==48,"first household card selects its resident");
    check(fifth&&*fifth==89,"second row card selects its resident");
    const std::vector<odh_host::HouseholdSelectorResident> two_residents{
        {105,"Sim 105"},{137,"Sim 137"}};
    const auto second_of_two=odh_host::household_selector_pick(two_residents,width,height,250,40);
    check(second_of_two&&*second_of_two==137,"second card in a two-resident household is selectable");
    check(residents[2].runtime_id==67&&residents[2].label=="Sim 67",
          "display label is separate from the runtime identity used for selection");
    check(!odh_host::household_selector_pick(residents,width,height,790,90),
          "click outside the cards does not select a resident");
    const std::vector<odh_host::HouseholdSelectorResident> too_many(9);
    check(!odh_host::household_selector_pick(too_many,width,height,20,35),
          "selector refuses a household larger than its visible card layout");
    const std::vector<odh_host::HouseholdSelectorResident> eight_residents{
        {101,"One"},{102,"Two"},{103,"Three"},{104,"Four"},
        {105,"Five"},{106,"Six"},{107,"Seven"},{108,"Eight"}};
    for(std::size_t count=1;count<=eight_residents.size();++count){
        const auto panel=odh_host::household_selector_panel_rect(160,120,count);
        check(panel.height<=108,"compact selector panel stays inside the available window height");
        const auto rows=(count+3)/4;
        const auto card_height=rows==1?54:38;
        const auto row=(count-1)/4,column=(count-1)%4;
        const auto x=10+static_cast<std::int32_t>(column)*35+1;
        const auto y=6+panel.height-4-static_cast<std::int32_t>(rows)*(card_height+3)+
                     static_cast<std::int32_t>(row)*(card_height+3)+card_height-1;
        const auto picked=odh_host::household_selector_pick(
            std::vector<odh_host::HouseholdSelectorResident>(eight_residents.begin(),
                eight_residents.begin()+static_cast<std::ptrdiff_t>(count)),160,120,x,y);
        check(picked&&*picked==eight_residents[count-1].runtime_id,
              "last resident remains visible and selectable in a minimum-size window");
        check(!odh_host::household_selector_pick(eight_residents,160,120,x,114),
              "compact resident cards do not extend beyond the panel bottom");
    }

    odh::RasterFrame frame{width,height,
        std::vector<std::array<std::uint8_t,4>>(width*height,{1,2,3,255}),{},0,0,{}};
    odh_host::draw_household_selector_button(frame,14);
    odh_host::draw_household_selector(frame,residents,67,14);
    check(frame.pixels[static_cast<std::size_t>(button.y)*width+button.x]!=
          std::array<std::uint8_t,4>{1,2,3,255},"selector button draws into the panel");
    const auto panel=odh_host::household_selector_panel_rect(width,height,residents.size());
    check(frame.pixels[static_cast<std::size_t>(panel.y)*width+panel.x]!=
          std::array<std::uint8_t,4>{1,2,3,255},"resident chooser draws its overlay");
    for(const auto& pixel:frame.pixels)
        check(pixel[3]==255,"window overlays preserve the opaque pixel rule");
    odh::RasterFrame named{width,height,
        std::vector<std::array<std::uint8_t,4>>(width*height,{1,2,3,255}),{},0,0,{}};
    auto fallback=residents;fallback[2].label="Sim 67";
    auto real_name=residents;real_name[2].label="Nina";
    odh_host::draw_household_selector(named,real_name,67,14);
    odh::RasterFrame fallback_frame{width,height,
        std::vector<std::array<std::uint8_t,4>>(width*height,{1,2,3,255}),{},0,0,{}};
    odh_host::draw_household_selector(fallback_frame,fallback,67,14);
    check(named.pixels!=fallback_frame.pixels,
          "resident label is drawn while the selected runtime identity remains independent");
    odh_host::HouseholdSelectorPortrait portrait{};
    portrait.fill({230,30,40,255});
    const std::vector<odh_host::HouseholdSelectorResident> portrait_residents{{105,"Bob",&portrait}};
    odh::RasterFrame portrait_frame{width,height,
        std::vector<std::array<std::uint8_t,4>>(width*height,{1,2,3,255}),{},0,0,{}};
    odh_host::draw_household_selector(portrait_frame,portrait_residents,105,14);
    check(portrait_frame.pixels[static_cast<std::size_t>(58)*width+37]==
          std::array<std::uint8_t,4>{230,30,40,255},
          "resident card draws the cached portrait pixels at thumbnail size");
    std::cout<<"linux household selector: checks passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"linux household selector: "<<error.what()<<'\n';return 1;}}
