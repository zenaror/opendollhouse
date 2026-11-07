#include "odh/advertisement_memory.hpp"
#include "odh/vm.hpp"
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
}
int main(){try{
    odh::AdvertisementChanges changes;odh::VmFrame frame;unsigned base_reads=0,base_writes=0,fallbacks=0;
    odh::VmMemory base;
    base.read=[&](const auto&,auto,auto)->std::optional<std::int16_t>{++base_reads;return 73;};
    base.write=[&](const auto&,auto,auto,auto){++base_writes;return true;};
    const auto fallback=[&](const auto&,std::uint8_t type,std::uint16_t index)->std::optional<std::int16_t>{
        ++fallbacks;return static_cast<std::int16_t>(100+type+(index==65535?10:0));};
    auto memory=odh::with_advertisement_memory(base,changes,fallback,3);
    for(const auto scope:{12,28,29}){
        const auto type=*odh::advertisement_scope_type(static_cast<std::uint8_t>(scope));
        const auto before=changes;
        require(memory.read(frame,static_cast<std::uint8_t>(scope),-1)==100+type+10&&changes==before,
            "ordinary advertisement reads use explicit action fallback and ushort indices without allocating overrides");
        require(memory.write(frame,static_cast<std::uint8_t>(scope),-1,0)&&memory.read(frame,static_cast<std::uint8_t>(scope),-1)==0,
            "all three advertisement scopes retain explicit zero overrides instead of falling back");
    }
    require(changes&&changes->size()==3&&odh::advertisement_changes_valid(changes)&&base_reads==0&&base_writes==0,
        "advertisement writes are a bounded Thread dictionary and never actual World motive accesses");
    const auto saved=changes;
    require(!memory.write(frame,12,7,45)&&changes==saved&&memory.write(frame,12,-1,9),
        "a full dictionary rejects additional keys without weakening existing-key updates");
    require(memory.read(frame,12,7)==101&&memory.read(frame,14,7)==73&&memory.write(frame,14,7,4)&&base_reads==1&&base_writes==1,
        "missing dictionary keys still fall back while unrelated real motive scopes retain their own backend");
    require(memory.expression_write(frame,28,-1,-32768)==std::optional<bool>{true}&&memory.read(frame,28,-1)==-32768,
        "expression and operand-scope paths share exact signed dictionary values");
    odh::AdvertisementChanges empty{std::map<std::uint32_t,std::int16_t>{}};
    auto empty_memory=odh::with_advertisement_memory(base,empty,fallback);
    require(empty_memory.read(frame,29,0)==100&&empty&&empty->empty(),"allocated empty dictionary does not suppress ordinary action fallback");
    odh::AdvertisementChanges fresh;auto no_action=odh::with_advertisement_memory(base,fresh);
    require(no_action.read(frame,12,7)==0&&!fresh,"proven no ActiveAction has zero ads and a null dictionary");
    auto unknown=odh::with_advertisement_memory(base,fresh,[](const auto&,auto,auto)->std::optional<std::int16_t>{return {};});
    require(!unknown.read(frame,12,7)&&unknown.write(frame,12,7,0)&&unknown.read(frame,12,7)==0,
        "unknown fallback is unsupported unless a real local override resolves the key");
    auto invalid=fresh;(*invalid)[3U<<16]=1;
    require(!odh::advertisement_changes_valid(invalid)&&!odh::write_advertisement(fresh,3,0,1),"only the three source advertisement types are valid");
    odh::AdvertisementChanges limited;
    for(std::uint32_t n=0;n<odh::advertisement_key_limit;++n)
        require(odh::write_advertisement(limited,0,static_cast<std::uint16_t>(n),1),"fill finite dictionary capacity");
    require(!odh::write_advertisement(limited,0,16384,1)&&limited->size()==odh::advertisement_key_limit,
        "default Thread dictionary budget has a fixed 16384-key ceiling");
    std::cout<<checks<<" advertisement-memory checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
