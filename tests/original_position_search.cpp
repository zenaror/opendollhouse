// SPDX-License-Identifier: MPL-2.0
#include "odh/original_position_search.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace odh;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Fixture {
    bool authority=true;std::int32_t size=64;std::size_t accept_after=0,room_reads=0;
    std::vector<EntityPosition> checked;
    OriginalPositionSearchRequest request{{160,160,1,6},true};
    OriginalPositionSearchServices services;
    Fixture(){
        services.source_still_matches=[this]{return authority;};
        services.lot_size=[this]()->std::optional<std::int32_t>{return size;};
        services.room_at=[this](auto,auto,auto)->std::optional<std::uint16_t>{++room_reads;return 7;};
        services.tile_mask=[](auto,auto,auto)->std::optional<std::uint8_t>{return 0;};
        services.native_4790e8=[](auto,auto,auto)->std::optional<bool>{return false;};
        services.position_valid=[this](const EntityPosition& candidate){
            checked.push_back(candidate);return Ts1PlacementResult{accept_after&&checked.size()==accept_after?
                Ts1PlacementStatus::success:Ts1PlacementStatus::blocked,10,{},{}};
        };
    }
    OriginalPositionSearchResult run(){return search_original_position(request,services);}
};
void spiral_order_and_centers(){
    Fixture f;f.accept_after=13;const auto result=f.run();
    const std::vector<std::pair<std::int32_t,std::int32_t>> golden{
        {10,10},{10,9},{11,9},{11,10},{11,11},{10,11},{9,11},{9,10},{9,9},{9,8},{10,8},{11,8},{12,8}};
    require(result.status==OriginalPositionSearchStatus::found&&result.position&&result.attempts==13&&result.position_checks==13&&
        f.checked.size()==golden.size(),"fixed native spiral reaches thirteen ordered candidates");
    for(std::size_t i=0;i<golden.size();++i)require(f.checked[i]==EntityPosition{golden[i].first*16+8,golden[i].second*16+8,1,6},
        "original434895 order/434d3d centers preserve XY axes, integer direction and level");
    require(*result.position==f.checked.back()&&f.request.start==EntityPosition{160,160,1,6},
        "search returns selected position without changing request or publishing a move");
}
void clamp_and_signed_bytes(){
    struct Case {std::int32_t x,y,expected_x,expected_y;};
    for(const auto sample:{Case{31,47,24,40},Case{-1,-17,24,24},Case{1008,1008,1000,1000},
            Case{2048,100,24,104},Case{4096,100,24,104}}){
        Fixture f;f.request.start={sample.x,sample.y,2,3};f.accept_after=1;
        const auto result=f.run();require(result.status==OriginalPositionSearchStatus::found&&result.position==
            std::optional<EntityPosition>{EntityPosition{sample.expected_x,sample.expected_y,2,3}},
            "native arithmetic tile shift truncates to signed byte BEFORE clamping to interior");
    }
}
void eligibility_order(){
    Fixture f;std::size_t masks=0,native=0,rooms=0;std::vector<EntityPosition> probes;
    f.services.tile_mask=[&](auto,auto,auto)->std::optional<std::uint8_t>{return masks++==0?0x10:0;};
    f.services.room_at=[&](auto x,auto y,auto)->std::optional<std::uint16_t>{++rooms;return x==10&&y==9?8:7;};
    f.services.native_4790e8=[&](auto x,auto y,auto)->std::optional<bool>{++native;return x==11&&y==9;};
    f.services.position_valid=[&](const auto& candidate){probes.push_back(candidate);return Ts1PlacementResult{
        probes.size()==2?Ts1PlacementStatus::success:Ts1PlacementStatus::blocked,10,{},{}};};
    const auto result=f.run();
    require(result.status==OriginalPositionSearchStatus::found&&result.attempts==5&&result.position_checks==2&&
        masks==5&&rooms==5&&native==3&&probes==std::vector<EntityPosition>{{184,168,1,6},{184,184,1,6}},
        "source mask0x30, same-room,4790e8 and PositionValid predicates retain short-circuit order");
    Fixture unrestricted;unrestricted.request.same_room=false;unrestricted.accept_after=2;
    unrestricted.services.room_at=[&](auto,auto,auto)->std::optional<std::uint16_t>{
        return ++unrestricted.room_reads==1?std::optional<std::uint16_t>{0}:std::nullopt;
    };
    require(unrestricted.run().status==OriginalPositionSearchStatus::found&&unrestricted.room_reads==1,
        "same-room false still reads anchor room, including known zero, but never filters candidate rooms");
    for(const auto mask:{std::uint8_t{0x10},std::uint8_t{0x20},std::uint8_t{0x30}}){
        Fixture filtered;std::size_t reads=0;
        filtered.services.tile_mask=[&](auto,auto,auto)->std::optional<std::uint8_t>{++reads;return mask;};
        require(filtered.run().status==OriginalPositionSearchStatus::exhausted&&reads&&filtered.checked.empty(),
            "each original0x30 selector bit rejects otherwise eligible inside candidates before PositionValid");
    }
}
void native_budgets(){
    Fixture tiny;tiny.size=2;auto result=tiny.run();
    require(result.status==OriginalPositionSearchStatus::exhausted&&result.attempts==16&&result.position_checks==0&&tiny.room_reads==1,
        "search stops after exactly8*lot-size consecutive outside candidates");
    Fixture edge;edge.size=4;edge.request.start={16,16,1,0};
    edge.services.tile_mask=[](auto,auto,auto)->std::optional<std::uint8_t>{return 0x30;};result=edge.run();
    require(result.status==OriginalPositionSearchStatus::exhausted&&result.attempts==38&&result.position_checks==0,
        "inside candidate resets outside streak even when its native mask rejects it");
    Fixture large;large.size=128;large.request.start={1024,1024,1,0};
    large.services.tile_mask=[](auto,auto,auto)->std::optional<std::uint8_t>{return 0x30;};result=large.run();
    require(result.status==OriginalPositionSearchStatus::exhausted&&result.attempts==4096&&result.position_checks==0,
        "native absolute4096 cap is independent of placement checks");
}
void missing_and_expired_services(){
    for(unsigned missing=0;missing<6;++missing){
        Fixture f;
        if(missing==0)f.services.source_still_matches={};
        if(missing==1)f.services.lot_size={};
        if(missing==2)f.services.room_at={};
        if(missing==3)f.services.tile_mask={};
        if(missing==4)f.services.native_4790e8={};
        if(missing==5)f.services.position_valid={};
        const auto result=f.run();require(result.status==OriginalPositionSearchStatus::unsupported&&result.attempts==0&&f.checked.empty(),
            "missing actual service is unsupported, never a fabricated empty map");
    }
    for(unsigned unknown=0;unknown<5;++unknown){
        Fixture f;
        if(unknown==0)f.services.lot_size=[]()->std::optional<std::int32_t>{return {};};
        if(unknown==1)f.services.room_at=[](auto,auto,auto)->std::optional<std::uint16_t>{return {};};
        if(unknown==2)f.services.tile_mask=[](auto,auto,auto)->std::optional<std::uint8_t>{return {};};
        if(unknown==3)f.services.native_4790e8=[](auto,auto,auto)->std::optional<bool>{return {};};
        if(unknown==4)f.services.position_valid=[](const auto&){return Ts1PlacementResult{Ts1PlacementStatus::unsupported,-1,{},"unknown geometry"};};
        require(f.run().status==OriginalPositionSearchStatus::unsupported,"unknown provider result is not known false or exhausted");
    }
    Fixture expired;expired.services.tile_mask=[&](auto,auto,auto)->std::optional<std::uint8_t>{expired.authority=false;return 0;};
    auto result=expired.run();require(result.status==OriginalPositionSearchStatus::unsupported&&result.attempts==1&&result.position_checks==0,
        "authority is rechecked after map callbacks before any later service");
    Fixture effects;unsigned mutations=0;effects.services.position_valid=[&](const auto&){++mutations;effects.authority=false;
        return Ts1PlacementResult{Ts1PlacementStatus::success,-1,{},{}};};
    result=effects.run();require(result.status==OriginalPositionSearchStatus::unsupported&&!result.position&&result.position_checks==1&&mutations==1,
        "expired PositionValid effects are preserved but never accepted as a selected position");
    Fixture throwing;throwing.services.position_valid=[](const EntityPosition&)->Ts1PlacementResult{throw std::runtime_error("callback failed");};
    require(throwing.run().status==OriginalPositionSearchStatus::unsupported,"throwing callback rejects without inventing a candidate");
    for(const auto size:{1,129}){Fixture f;f.size=size;require(f.run().status==OriginalPositionSearchStatus::unsupported,
        "unsupported tile-domain lot size remains explicit");}
    Fixture invalid;invalid.request.start.direction=8;require(invalid.run().status==OriginalPositionSearchStatus::unsupported,"invalid preserved direction rejected");
}
}
int main(){try{spiral_order_and_centers();clamp_and_signed_bytes();eligibility_order();native_budgets();missing_and_expired_services();
    std::cout<<"original fixed-position recovery search checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
