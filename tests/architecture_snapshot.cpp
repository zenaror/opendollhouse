// SPDX-License-Identifier: MPL-2.0
#include "odh/architecture_snapshot.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
template<class F>void rejects(F f){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error("malformed architecture accepted");}
odh::ArchitectureResourceRecord material(odh::ArchitectureResourceFamily family,std::uint16_t id,std::string name){
    odh::ArchitectureResourceRecord r;r.family=family;r.catalog={std::move(name),id};return r;
}
}
int main(){try{
    using F=odh::ArchitectureResourceFamily;odh::PreparedArchitecture p;p.width=p.height=2;p.buildable.assign(4,1);
    for(auto& l:p.levels){l.floors.assign(4,0);l.walls.resize(4);}
    p.levels[0].floors[0]=300;p.levels[0].floors[1]=65534;
    auto& diagonal=p.levels[0].walls[2];diagonal.segments=odh::wall_horizontal_diagonal;
    diagonal.top_left_style=300;diagonal.top_left_pattern=301;diagonal.bottom_left_pattern=400;
    p.levels[1].walls[0].segments=odh::wall_top_left;p.levels[1].walls[0].top_left_pattern=400;
    p.diagnostics.floor_lookups=4;p.diagnostics.floor_alias_misses=1;p.diagnostics.missing_floor_aliases[7]=true;
    odh::ArchitectureResourceCatalog old_catalog;old_catalog.resources={material(F::floor,300,"Wood"),material(F::floor,301,"Stone"),material(F::wall,400,"Paint")};
    const auto state=odh::bind_architecture_snapshot(p,old_catalog),copy=odh::decode_architecture_snapshot(odh::encode_architecture_snapshot(state));
    require(copy.materials.size()==3&&copy.materials[0].name=="wood","stable provider keys cover only actual dynamic materials");
    require(odh::encode_architecture_snapshot(copy)==odh::encode_architecture_snapshot(state),"wire roundtrip including preparation diagnostics");
    auto current=old_catalog;current.resources[0].catalog.id=601;current.resources[1].catalog.id=602;current.resources[2].catalog.id=603;
    const auto rebound=odh::resolve_architecture_snapshot(copy,current);
    require(rebound.levels[0].floors[0]==601&&rebound.levels[0].floors[1]==65534&&rebound.levels[0].walls[2].top_left_style==601&&
        rebound.levels[0].walls[2].top_left_pattern==602&&rebound.levels[0].walls[2].bottom_left_pattern==603&&
        rebound.levels[1].walls[0].top_left_pattern==603,"floor halves and wall patterns rebind by family/name, water sentinel unchanged");
    require(p.levels[0].floors[0]==300&&p.levels[0].walls[2].top_left_style==300,"binding never edits caller geometry");
    current.resources.pop_back();rejects([&]{(void)odh::resolve_architecture_snapshot(state,current);});
    auto bad=state;bad.materials.pop_back();rejects([&]{(void)odh::encode_architecture_snapshot(bad);});
    bad=state;bad.materials.push_back(bad.materials[0]);rejects([&]{(void)odh::encode_architecture_snapshot(bad);});
    bad=state;bad.prepared.levels[1].walls[0].segments=0x30;rejects([&]{(void)odh::encode_architecture_snapshot(bad);});
    bad=state;bad.prepared.buildable[0]=2;rejects([&]{(void)odh::encode_architecture_snapshot(bad);});
    auto bytes=odh::encode_architecture_snapshot(state);bytes.push_back(0);rejects([&]{(void)odh::decode_architecture_snapshot(bytes);});
    bytes.pop_back();bytes.resize(16);rejects([&]{(void)odh::decode_architecture_snapshot(bytes);});
    odh::ArchitectureSnapshotLimits limits;limits.bytes=64;rejects([&]{(void)odh::encode_architecture_snapshot(state,limits);});
    limits={};limits.materials=2;rejects([&]{(void)odh::decode_architecture_snapshot(odh::encode_architecture_snapshot(state),limits);});
    std::cout<<"architecture identity and persistence checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
