#include "odh/object_group.hpp"
#include <algorithm>
#include <iostream>
namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char* why){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error(why);}
}
int main()try{
    {
        odh::ObjectDefinition d{};d.master_id=7;d.sub_index=std::int16_t(-254);d.level_offset=0xff80;
        const auto fields=odh::native_object_group_constructor_fields(d);
        need(fields&&fields->authored.x==-1&&fields->authored.y==2&&fields->authored.level==-128&&
            fields->current.x==0&&fields->current.y==0&&fields->current.level==0&&fields->next==odh::WorldEntityReference{},
            "actual native constructor signed high byte and signed16 level; current zero is an explicit source assignment");
        d.sub_index=0x02fd;d.level_offset=0x0180;
        const auto unsigned_y=odh::native_object_group_constructor_fields(d);
        need(unsigned_y&&unsigned_y->authored.x==2&&unsigned_y->authored.y==253&&unsigned_y->authored.level==384,
            "native low byte is unsigned and level is not narrowed to signed byte construction offsets");
        d.sub_index=0;d.level_offset=0x8000;
        need(odh::native_object_group_constructor_fields(d)->authored.level==-32768,"full signed16 native level lower bound");
        d.level_offset.reset();rejects([&]{odh::native_object_group_constructor_fields(d);},"missing native constructor level accepted");
        d.level_offset=0;d.sub_index=-1;rejects([&]{odh::native_object_group_constructor_fields(d);},"abstract native master materialized as body");
        d.master_id=0;d.level_offset.reset();
        need(!odh::native_object_group_constructor_fields(d),"ordinary master0 body never invents native MT fields");
    }
    odh::World world;
    for(const auto id:{std::int16_t{8},std::int16_t{2},std::int16_t{4},std::int16_t{7}}){
        need(world.create(id,odh::EntityKind::object),"create source fixture identity");
        need(world.define_type(id,static_cast<std::uint32_t>(100+id),id==7?std::optional<std::uint32_t>{}:std::optional<std::uint32_t>{900}),"bind actual part/master identity");
    }
    const auto source=world.snapshot();
    const auto member=[&](std::int16_t id,std::int16_t x,std::int16_t y,std::int16_t level){return odh::ObjectGroupMember{id,world.find(id)->generation,x,y,level};};
    const odh::ObjectGroup group{900,true,4,{member(8,-32,16,-1),member(4,16,0,0),member(2,0,32,1)}};
    const odh::ObjectGroup single{107,false,7,{member(7,16,-32,-1)}};
    const auto captured=odh::capture_object_groups({single,group},source);
    need(captured.groups.front().base_id==4&&captured.groups.back().base_id==7&&
        captured.groups.front().members[0].id==8&&captured.groups.front().members[1].id==4&&
        captured.groups.front().members[2].offset_level==1,
        "capture canonicalizes only groups; source member order, non-minimum base and relative level remain intact");
    const auto encoded=odh::encode_object_group_snapshot(captured);const auto reopened=odh::decode_object_group_snapshot(encoded);
    odh::validate_object_groups(reopened,source);
    need(odh::encode_object_group_snapshot(reopened)==encoded&&reopened.groups.back().members.front().offset_x==16&&
        reopened.groups.back().members.front().offset_y==-32&&reopened.groups.back().members.front().offset_level==-1,
        "signed offsets and generations roundtrip for real groups and raw direct-subpart singletons");
    const odh::ObjectGroupSnapshot golden{{{0x81234567,false,7,{{7,0x0102030405060708ULL,0,0,0}}}}};
    const std::vector<std::uint8_t> expected{1,0,0,0,1,0,0,0,0x67,0x45,0x23,0x81,0,0,7,0,1,0,0,0,7,0,8,7,6,5,4,3,2,1,0,0,0,0,0,0};
    need(odh::encode_object_group_snapshot(golden)==expected,"independent OGRP v1 byte fixture pins all field offsets");
    for(std::size_t n=0;n<encoded.size();++n){auto prefix=encoded;prefix.resize(n);
        rejects([&]{odh::decode_object_group_snapshot(prefix);},"truncated object groups accepted");}
    auto corrupt=expected;corrupt[0]=2;rejects([&]{odh::decode_object_group_snapshot(corrupt);},"unsupported snapshot version accepted");
    corrupt=expected;corrupt[12]=2;rejects([&]{odh::decode_object_group_snapshot(corrupt);},"nonboolean multitile flag accepted");
    corrupt=expected;corrupt[13]=1;rejects([&]{odh::decode_object_group_snapshot(corrupt);},"reserved flag accepted");
    corrupt=expected;for(unsigned i=22;i<30;++i)corrupt[i]=0;
    rejects([&]{odh::decode_object_group_snapshot(corrupt);},"zero generation accepted");
    corrupt=expected;corrupt.push_back(0);rejects([&]{odh::decode_object_group_snapshot(corrupt);},"trailing bytes accepted");
    auto invalid=captured;std::reverse(invalid.groups.begin(),invalid.groups.end());
    rejects([&]{odh::encode_object_group_snapshot(invalid);},"noncanonical group order accepted");
    invalid=captured;invalid.groups[0].base_id=3;
    rejects([&]{odh::validate_object_groups(invalid,source);},"base outside membership accepted");
    invalid=captured;++invalid.groups[0].members[0].generation;
    rejects([&]{odh::validate_object_groups(invalid,source);},"stale member generation accepted");
    invalid=captured;invalid.groups[0].members[0].id=20;
    rejects([&]{odh::validate_object_groups(invalid,source);},"missing live member accepted");
    invalid=captured;invalid.groups[1].members[0]=invalid.groups[0].members[0];invalid.groups[1].base_id=8;
    rejects([&]{odh::encode_object_group_snapshot(invalid);},"one physical identity in two groups accepted");
    invalid=captured;invalid.groups[1].members.push_back(invalid.groups[0].members[0]);
    rejects([&]{odh::validate_object_groups(invalid,source);},"direct singleton with multiple physical members accepted");
    auto wrong_world=source;wrong_world.entities[0].ui_ghost=true;
    rejects([&]{odh::validate_object_groups(captured,wrong_world);},"ghost member accepted");
    wrong_world=source;wrong_world.entities[0].dead=true;
    rejects([&]{odh::validate_object_groups(captured,wrong_world);},"dead indexed member accepted");
    wrong_world=source;wrong_world.entities[0].definition->master_guid=901;
    rejects([&]{odh::validate_object_groups(captured,wrong_world);},"foreign master accepted");
    wrong_world=source;wrong_world.entities[0].kind=odh::EntityKind::avatar;
    rejects([&]{odh::validate_object_groups(captured,wrong_world);},"avatar as ordinary multipart member accepted");
    auto limits=odh::ObjectGroupLimits{};limits.members=3;
    rejects([&]{odh::encode_object_group_snapshot(captured,limits);},"aggregate membership budget accepted");
    limits={};limits.groups=1;rejects([&]{odh::decode_object_group_snapshot(encoded,limits);},"group allocation budget accepted");
    limits={};limits.payload_bytes=expected.size()-1;
    rejects([&]{odh::encode_object_group_snapshot(golden,limits);},"encoded byte budget accepted");
    rejects([&]{odh::decode_object_group_snapshot(expected,limits);},"decoded byte budget accepted");
    need(world.snapshot().entities.size()==source.entities.size()&&world.snapshot().generation_sequence==source.generation_sequence,
        "capture/codec/failed validation must not mutate World ownership");
    odh::ObjectGroupSnapshot one_part{{{900,true,4,{member(4,16,0,0)}}}};
    odh::validate_object_groups(one_part,source);
    {
        auto a=member(8,-16,32,1),b=member(7,0,0,0);
        a.origin=odh::ObjectGroupMemberOrigin{odh::ObjectGroupMemberSource::native_created,108,900,"Objects.far","part.iff",128};
        b.origin=odh::ObjectGroupMemberOrigin{odh::ObjectGroupMemberSource::original_import,107,{},"ExpansionPack.far","other.iff",129};
        odh::ObjectGroup association{0,false,7,{a,b},odh::ObjectGroupKind::live_association};
        auto live=odh::capture_object_groups({association},source);
        const auto wire=odh::encode_object_group_snapshot(live);
        need(wire[0]==2&&wire[13]==1,"live association uses explicit OGRPv2 kind independently of MultiTile false");
        auto copy=odh::decode_object_group_snapshot(wire);odh::validate_object_groups(copy,source);
        need(odh::encode_object_group_snapshot(copy)==wire&&copy.groups[0].members[0].id==8&&
            copy.groups[0].members[1].origin->source_kind==odh::ObjectGroupMemberSource::original_import&&
            copy.groups[0].members[0].offset_x==-16&&copy.groups[0].members[0].offset_level==1,
            "mixed original/native resources and source order survive live association roundtrip");
        for(std::size_t n=0;n<wire.size();++n){auto prefix=wire;prefix.resize(n);
            rejects([&]{odh::decode_object_group_snapshot(prefix);},"truncated live association accepted");}
        auto bad=live;bad.groups[0].members[0].origin.reset();rejects([&]{odh::encode_object_group_snapshot(bad);},"live group without provenance accepted");
        bad=live;bad.groups[0].definition_guid=900;rejects([&]{odh::encode_object_group_snapshot(bad);},"live association forged as construction accepted");
        bad=live;bad.groups[0].members[0].origin->source_kind=static_cast<odh::ObjectGroupMemberSource>(2);
        rejects([&]{odh::encode_object_group_snapshot(bad);},"unknown live source kind accepted");
        bad=live;bad.groups[0].members[0].origin->source.clear();rejects([&]{odh::encode_object_group_snapshot(bad);},"empty live source accepted");
        bad=live;bad.groups[0].members[0].origin->master_guid=901;
        rejects([&]{odh::validate_object_groups(bad,source);},"live World master provenance mismatch accepted");
        bad=live;bad.groups[0].kind=odh::ObjectGroupKind::constructed;bad.groups[0].definition_guid=900;bad.groups[0].multitile=true;
        rejects([&]{odh::encode_object_group_snapshot(bad);},"constructed group can smuggle live origins accepted");
        auto limited=odh::ObjectGroupLimits{};limited.payload_bytes=wire.size()-1;
        rejects([&]{odh::encode_object_group_snapshot(live,limited);},"live provenance bytes escaped exact payload budget");
        limited.payload_bytes=wire.size();need(odh::encode_object_group_snapshot(live,limited)==wire,"exact live payload byte budget accepted");
        auto forged=wire;forged[0]=1;rejects([&]{odh::decode_object_group_snapshot(forged);},"live association reinterpreted as v1 accepted");
    }
    {
        auto native=captured;auto& g=native.groups.front();
        g.native_head=odh::WorldEntityReference{g.members.front().id,g.members.front().generation};
        for(std::size_t n=0;n<g.members.size();++n)g.members[n].native=odh::NativeObjectGroupMember{
            {std::int32_t(n)-2,3,std::int32_t(n)},{9,std::int32_t(n)-7,-3},
            n+1<g.members.size()?odh::WorldEntityReference{g.members[n+1].id,g.members[n+1].generation}:odh::WorldEntityReference{}};
        odh::validate_object_groups(native,source);const auto wire=odh::encode_object_group_snapshot(native);
        const auto copy=odh::decode_object_group_snapshot(wire);odh::validate_object_groups(copy,source);
        need(wire[0]==3&&copy.groups[0].native_head->id==8&&copy.groups[0].base_id==4&&
            copy.groups[0].members[0].native->authored.x==-2&&copy.groups[0].members[0].native->current.y==-7&&
            !copy.groups[1].native_head&&odh::encode_object_group_snapshot(copy)==wire,
            "v3 preserves explicit head independent of BaseObject and both triples without inferring legacy fields");
        auto bad=native;bad.groups[0].members[0].native->next={2,world.find(2)->generation};
        rejects([&]{odh::encode_object_group_snapshot(bad);},"native NEXT silently reordered source membership");
        bad=native;bad.groups[0].members[1].native.reset();rejects([&]{odh::encode_object_group_snapshot(bad);},"partial native fields accepted");
        bad=native;bad.groups[0].native_head=odh::WorldEntityReference{4,world.find(4)->generation};
        rejects([&]{odh::encode_object_group_snapshot(bad);},"BaseObject substituted for actual native head");
        for(std::size_t n=0;n<wire.size();++n){auto prefix=wire;prefix.resize(n);rejects([&]{odh::decode_object_group_snapshot(prefix);},"truncated v3 accepted");}
        auto bound=odh::ObjectGroupLimits{};bound.payload_bytes=wire.size()-1;
        rejects([&]{odh::encode_object_group_snapshot(native,bound);},"native data escaped payload bound");
        bound.payload_bytes=wire.size();need(odh::encode_object_group_snapshot(native,bound)==wire,"exact native payload bound");
        const auto removed=*g.native_head;auto fields=*g.members.front().native;fields.next={};
        need(world.remove_retaining_body(removed),"actual retained native-head removal");
        g.members.erase(g.members.begin());g.native_head=odh::WorldEntityReference{g.members.front().id,g.members.front().generation};
        native.retired_native_members.push_back({removed,fields});odh::validate_object_groups(native,world.snapshot());
        const auto retired=odh::decode_object_group_snapshot(odh::encode_object_group_snapshot(native));
        need(retired.retired_native_members.size()==1&&retired.retired_native_members[0].reference==removed&&
            retired.retired_native_members[0].fields.next==odh::WorldEntityReference{},"same owner persists exact retained cleared NEXT");
        bound={};bound.members=3;rejects([&]{odh::encode_object_group_snapshot(native,bound);},"retired node escaped aggregate bound");
        need(world.collect_retained_bodies({}),"collect native cursor body");
        rejects([&]{odh::validate_object_groups(retired,world.snapshot());},"collected body retained native authority");
    }
    std::cout<<"dynamic object group authority and persistence checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
