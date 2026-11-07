#include "odh/family_spells.hpp"
#include <iostream>
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace {
int checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char* message){bool caught=false;try{f();}catch(const odh::FormatError&){caught=true;}require(caught,message);}
odh::FamilyEntry family(bool loaded=true,std::uint16_t chunk=7) {
    odh::FamilyEntry entry{};entry.chunk_id=chunk;entry.family.family_number=-123;
    entry.family.version=loaded?9:8;
    if(loaded){entry.family.remaining_bytes=16;entry.family.tail_bytes.resize(16);entry.family.original_v9_extension=odh::FamilyV9Extension{0x87654321U,{-32767,-2,1,-32768,12,32766}};}
    return entry;
}
odh::FamilySpellsGlobals globals(std::optional<std::int16_t> active=7,std::optional<std::int16_t> house=7) {
    return [=](std::int16_t index)->std::optional<std::int16_t>{if(index==9)return active;if(index==10)return house;return {};};
}
void controller(odh::World& world,std::int16_t id=1,std::size_t words=6) {
    require(world.create(id,odh::EntityKind::object,{words,0,0})&&world.define_type(id,0xb6c90029U),"create existing spell controller");
}
void set(odh::World& world,std::int16_t id,const std::array<std::int16_t,6>& words) {
    odh::VmFrame frame{};frame.context.stack_object=id;
    for(std::size_t n=0;n<6;++n)require(world.write(frame,1,static_cast<std::int16_t>(n),words[n]),"initialize controller attributes");
}
std::array<std::int16_t,6> bank(const odh::World& world,std::int16_t id=1) {
    std::array<std::int16_t,6> result{};std::copy_n(world.find(id)->attributes.begin(),6,result.begin());return result;
}
void codec_and_identity() {
    odh::FamilySpells state(family());const auto snapshot=state.snapshot();
    const auto bytes=odh::encode_family_spells_snapshot(snapshot);
    require(bytes.size()==28,"bounded FSPLv1 wire size");
    const auto decoded=odh::decode_family_spells_snapshot(bytes);
    require(decoded.family_chunk_id==7&&decoded.family_number==-123&&decoded.loaded_presence&&
        decoded.extension_dword==0x87654321U&&decoded.words==snapshot.words,"signed words and independent identities roundtrip");
    state.restore(decoded);
    auto bad=decoded;++bad.family_chunk_id;rejects([&]{state.restore(bad);},"chunk identity cannot change");
    bad=decoded;++bad.family_number;rejects([&]{state.restore(bad);},"FamilyNumber provenance cannot change");
    bad=decoded;bad.version=2;rejects([&]{state.restore(bad);},"unknown state version rejects");
    bad=decoded;bad.loaded_presence=false;rejects([&]{state.restore(bad);},"save cannot downgrade original loaded presence");
    bad=decoded;++bad.extension_dword;rejects([&]{state.restore(bad);},"save cannot replace unknown immutable extension metadata");
    require(odh::encode_family_spells_snapshot(state.snapshot())==bytes,"failed restores preserve carrier bytes");
    for(std::size_t n=0;n<bytes.size();++n)rejects([&]{odh::decode_family_spells_snapshot({bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(n)});},"truncated state rejects");
    for(const auto at:{0U,6U,7U}){auto invalid=bytes;invalid[at]=2;rejects([&]{odh::decode_family_spells_snapshot(invalid);},"wire version and flags canonical");}
    auto extra=bytes;extra.push_back(0);rejects([&]{odh::decode_family_spells_snapshot(extra);},"unbounded trailing state rejects");
    auto incomplete=family();incomplete.family.original_v9_extension.reset();rejects([&]{odh::FamilySpells broken(incomplete);},"incomplete FAMI9 never invents zero spells");
    auto future=family();future.family.version=10;rejects([&]{odh::FamilySpells broken(future);},"future FAMI profile needs its own proof");
    auto original8=family(false);original8.family.tail_bytes={0x78,0x56,0x34,0x12};original8.family.remaining_bytes=4;
    odh::FamilySpells legacy8(original8);require(legacy8.snapshot().extension_dword==0x12345678U&&!legacy8.snapshot().loaded_presence,
        "legacy v8 original extension DWORD remains intact without claiming six loaded words");
    odh::FamilySpells legacy(family(false));require(!legacy.snapshot().loaded_presence&&legacy.snapshot().words==std::array<std::int16_t,6>{},"legacy original ctor starts unloaded zero words");
}
void max_and_capture() {
    odh::World world;controller(world);set(world,1,{-32768,-1,0,4,10,32767});
    odh::FamilySpells state(family());const auto source=state.snapshot();const odh::LotZoning zoning{{7,0}};
    const auto handler=odh::family_spells_primitive(world,&state,&zoning,globals());
    const odh::Instruction instruction{1,254,255,{43,0,0,0,0,0,0,0}};
    require(handler({},instruction,{},{}).disposition==odh::PrimitiveDisposition::unsupported&&
        bank(world)==std::array<std::int16_t,6>{-32768,-1,0,4,10,32767},
        "forged dispatch rejects before changing Attributes");
    const auto key=odh::routine_key(4096);
    odh::Vm vm({{key,odh::Bhav{0x8002,{instruction},{},odh::Bhav::Metadata{0,4,0,0}}}},
        world.vm_memory(),{},handler);
    vm.start(key);
    require(vm.run(1)==odh::VmStatus::returned_true&&
        bank(world)==std::array<std::int16_t,6>{-32767,-1,1,4,12,32767},
        "actual Generic43 applies signed max to six actual Attributes");
    const auto before_stale=bank(world);
    require(handler({},instruction,{}, {&vm,1}).disposition==odh::PrimitiveDisposition::unsupported&&bank(world)==before_stale,
        "finished VM cannot authenticate a stale Generic43 dispatch");
    require(state.snapshot().words==source.words,"forward publication does not mutate durable family words");
    require(!odh::apply_family_spells(world,&state,&zoning,globals()).changed,"forward merge is idempotent");
    const auto capture=odh::capture_family_spells(world,state,&zoning,globals());
    require(capture.supported&&capture.changed&&state.snapshot().words==bank(world)&&state.snapshot().loaded_presence,
        "pre-save reverse merge captures progress monotonically");
    odh::FamilySpells legacy(family(false));
    require(odh::capture_family_spells(world,legacy,&zoning,globals()).supported&&
        legacy.snapshot().words==std::array<std::int16_t,6>{0,0,1,4,12,32767}&&!legacy.snapshot().loaded_presence,
        "reverse capture works without loaded flag and never promotes it");
    odh::FamilySpells reopened(family(false));reopened.restore(odh::decode_family_spells_snapshot(odh::encode_family_spells_snapshot(legacy.snapshot())));
    require(reopened.snapshot().words==legacy.snapshot().words&&!reopened.snapshot().loaded_presence,"reopen preserves exact loaded flag after legacy capture");
    require(handler({}, {1,254,255,{42,0,0,0,0,0,0,0}}, {}, {}).disposition==odh::PrimitiveDisposition::unsupported,
        "shared primitive owns only Generic43");
}
void guards_and_failures() {
    odh::World world;controller(world);odh::FamilySpells state(family()),legacy(family(false));
    const odh::LotZoning residential{{7,0}},community{{7,1}},missing{},invalid{{7,2}};
    require(!odh::apply_family_spells(world,nullptr,&residential,globals()).supported,"missing family authority unsupported");
    require(odh::apply_family_spells(world,&legacy,nullptr,{}).supported,"known unloaded flag short-circuits extra authorities");
    require(!odh::apply_family_spells(world,&state,&residential,{}).supported,"missing globals unsupported");
    require(!odh::apply_family_spells(world,&state,&residential,globals({},7)).supported,"unknown Global9 unsupported");
    require(odh::apply_family_spells(world,&state,nullptr,globals(8,{})).supported,"known family mismatch precedes missing lot authority");
    require(!odh::apply_family_spells(world,&state,&residential,globals(7,{})).supported,"unknown Global10 unsupported");
    require(!odh::apply_family_spells(world,&state,nullptr,globals()).supported,"missing zoning authority unsupported");
    require(!odh::apply_family_spells(world,&state,&invalid,globals()).supported,"invalid explicit classification unsupported");
    for(const auto* zoning:{&community,&missing})require(odh::apply_family_spells(world,&state,zoning,globals()).supported&&bank(world)==std::array<std::int16_t,6>{},"community and missing explicit row are known no-ops, no fallback");
    odh::FamilySpells high(family(true,65535));require(odh::apply_family_spells(world,&high,nullptr,globals(-1,{})).supported,
        "signed Global9 does not alias high unsigned chunkID");
    odh::FamilySpells none;require(odh::apply_family_spells(world,&none,nullptr,{}).supported,"known absent family is a no-op");
    require(odh::apply_family_spells(world,&state,nullptr,globals(7,0)).changed,"forward original key0 residential guard requires no zoning table");
    odh::FamilySpellsGlobals homezero=[](std::int16_t index)->std::optional<std::int16_t>{if(index==10)return 0;throw std::runtime_error("reverse guard read Global9 too early");};
    require(odh::capture_family_spells(world,state,nullptr,homezero).supported,"reverse Global10 zero precedes all other source guards");
    odh::World empty;require(odh::apply_family_spells(empty,&state,&residential,globals()).supported,"known missing controller is source no-op");
    odh::World short_bank;controller(short_bank,1,5);const auto revision=short_bank.entity_identity_revision();
    require(!odh::apply_family_spells(short_bank,&state,&residential,globals()).supported&&
        short_bank.find(1)->attributes==std::vector<std::int16_t>(5)&&short_bank.entity_identity_revision()==revision,
        "all six attributes preflight before any write or growth");
    const auto before=state.snapshot();require(!odh::capture_family_spells(short_bank,state,&residential,globals()).supported&&state.snapshot().words==before.words,
        "rejected reverse capture retains carrier");
    odh::World duplicates;controller(duplicates);controller(duplicates,2);
    require(!odh::apply_family_spells(duplicates,&state,&residential,globals()).supported&&bank(duplicates)==std::array<std::int16_t,6>{}&&bank(duplicates,2)==std::array<std::int16_t,6>{},
        "ambiguous source order rejects instead of selecting lowest WorldID");
    require(!odh::capture_family_spells(duplicates,state,&residential,globals()).supported&&state.snapshot().words==before.words,
        "ambiguous reverse capture retains carrier");
}
}
int main(){try{codec_and_identity();max_and_capture();guards_and_failures();std::cout<<checks<<" family spells checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
