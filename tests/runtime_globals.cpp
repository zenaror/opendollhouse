#include "odh/runtime_globals.hpp"
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks=0;
void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F f){try{f();}catch(const odh::FormatError&){++checks;return;}throw std::runtime_error("invalid globals accepted");}
void projection(){
    for(const unsigned count:{32U,64U}){
        std::vector<std::int16_t> input(count);for(unsigned n=0;n<count;++n)input[n]=static_cast<std::int16_t>(n+100);
        const auto original=input;
        odh::RuntimeGlobals current(input,odh::Ts1GlobalActivation::saved_new,7);
        require(current.extent()==38&&input==original,"SIMI projection has38 runtime words and preserves source bytes");
        const auto state=current.snapshot();
        for(unsigned n=0;n<38;++n){
            const auto expected=n==17||n==25?4:n==20?255:n<count?static_cast<int>(n+100):0;
            require(state.words[n]==expected,"new activator copies projection and changes only three bootstrap words");
        }
        require(!current.read(3)&&!current.read(-1)&&!current.read(38)&&!current.read(63)&&!current.write(38,1),
            "source-selected ID needs mapping and raw source tail is inaccessible");
        odh::RuntimeGlobals old(input,odh::Ts1GlobalActivation::legacy_old,7);
        require(old.read(3)==0&&old.read(9)==0&&old.read(10)==7&&old.read(32)==0&&old.read(33)==2,
            "legacy activator uses its actual defaults even from raw32");
        require(current.read(9)==109&&current.read(10)==110&&current.read(32)==(count==64?132:0)&&
            current.read(33)==(count==64?133:0),"new activator retains family/house/build/machine state instead of old defaults");
        for(int n=0;n<38;++n)require(current.write(static_cast<std::int16_t>(n),static_cast<std::int16_t>(-n-1)),
            "all source bank fields are writable, not a whitelist");
        require(current.read(3)==-4&&current.read(37)==-38,"raw BHAV writes preserve signed IDs and runtime tails");
    }
    rejects([]{odh::RuntimeGlobals invalid(std::vector<std::int16_t>(38),odh::Ts1GlobalActivation::saved_new,7);});
    rejects([]{odh::RuntimeGlobals invalid(std::vector<std::int16_t>(32),odh::Ts1GlobalActivation::saved_new,0);});
    rejects([]{odh::RuntimeGlobals invalid(std::vector<std::int16_t>(32),static_cast<odh::Ts1GlobalActivation>(9),7);});
}
void clocks(){
    odh::RuntimeGlobals globals(std::vector<std::int16_t>(32),odh::Ts1GlobalActivation::saved_new,7);
    odh::Ts1Clock clock;require(clock.initialize(14,22,47,11,1999),"clock init");
    const auto before=clock.snapshot();
    for(const auto index:{0,1,4,5,6,7,8}){
        require(!globals.read(index)&&globals.write(index,-123)&&globals.snapshot().words[index]==-123,
            "missing clock is unknown while clock shadow remains writable");
        require(globals.read(index,&clock)==clock.global(index),"derived clock read ignores shadow writes");
    }
    require(clock.snapshot().ticks==before.ticks&&clock.snapshot().hour==before.hour&&clock.snapshot().minute==before.minute,
        "global writes do not advance or reset clock");
    require(!globals.read(38,&clock)&&!globals.write(-1,0),"clock backend cannot bypass bank bounds");
    auto bad=globals.snapshot();bad.words.resize(64);
    const auto original=globals.snapshot();require(!globals.restore(bad)&&globals.snapshot().words==original.words,
        "invalid snapshot extent leaves storage untouched");
}
void selections(){
    odh::World world;require(world.create(105,odh::EntityKind::avatar)&&world.create(137,odh::EntityKind::avatar)&&
        world.create(2,odh::EntityKind::object),"selection identities");
    std::vector<std::int16_t> source(64);source[3]=42;
    odh::RuntimeGlobals globals(source,odh::Ts1GlobalActivation::saved_new,7);
    const auto generation=world.find(105)->generation;
    require(!globals.resolve_imported_selection(world,41,{105,generation})&&!globals.read(3),"mapping must prove original selected source ID");
    require(!globals.resolve_imported_selection(world,42,{2,world.find(2)->generation})&&
        !globals.resolve_imported_selection(world,42,{105,generation+1})&&!globals.read(3),"mapping rejects object and stale avatar identities");
    require(globals.resolve_imported_selection(world,42,{105,generation})&&globals.read(3)==105,
        "commercial selected ID maps to proven live avatar");
    require(!globals.resolve_imported_selection(world,42,{137,world.find(137)->generation}),"import mapping cannot be replayed over current selection");
    require(globals.select_avatar(world,{137,world.find(137)->generation})&&globals.read(3)==137,"UI control selects its actual proven avatar");
    require(globals.write(3,2)&&globals.read(3)==2,"BHAV SetGlobalValue is raw storage, not implicit UI avatar switching");
    globals.clear_selection();require(globals.read(3)==0,"explicit no-selection remains source zero");
    require(world.remove(105)&&world.create(105,odh::EntityKind::avatar)&&!globals.select_avatar(world,{105,generation})&&
        globals.read(3)==0,"recycled avatar cannot reuse a selection command");
}
void persistence(){
    odh::LotContext::Snapshot old{};old[10]=7;old[23]=64;old[37]=-7;
    const auto before=old;auto globals=odh::RuntimeGlobals::from_legacy(old);
    require(old==before&&globals.read(10)==7&&globals.read(37)==-7&&!globals.read(3)&&!globals.read(9)&&!globals.read(32),
        "legacy sparse bank preserves unknown runtime fields instead of inventing defaults");
    auto snapshot=globals.snapshot();require(!snapshot.words[3]&&!snapshot.selected_sim_is_runtime_id,"unknown legacy selection survives snapshot");
    odh::RuntimeGlobals fresh(std::vector<std::int16_t>(32),odh::Ts1GlobalActivation::saved_new,1);
    require(fresh.restore(snapshot)&&fresh.snapshot().words==snapshot.words&&fresh.snapshot().selected_sim_is_runtime_id==snapshot.selected_sim_is_runtime_id,
        "fresh service restores sparse bank exactly without normalizing old saves");
    auto invalid=snapshot;invalid.selected_sim_is_runtime_id=true;
    require(!fresh.restore(invalid)&&fresh.snapshot().words==snapshot.words,"known selection cannot name absent shadow word");
    require(fresh.write(9,5)&&fresh.write(32,0)&&fresh.write(3,105)&&fresh.write(0,-222),"runtime bindings publish proven missing fields explicitly");
    const auto complete=fresh.snapshot();require(globals.restore(complete)&&globals.read(9)==5&&globals.read(3)==105&&globals.snapshot().words[0]==-222,
        "all runtime shadow changes and selected-ID authority survive continuation");
    odh::Ts1Clock clock;require(clock.initialize(10,1,0,6,1997),"continuation clock");
    odh::VmMemory memory;memory.read=[&](const auto&,auto scope,auto index){return scope==6?globals.read(index,&clock):std::nullopt;};
    odh::Bhav routine{0x8002,{{2,254,255,{0,0,3,0,0,5,8,6}}},{},odh::Bhav::Metadata{0,0,0,0}};
    odh::Vm vm({{odh::routine_key(4096),routine}},memory);vm.start(odh::routine_key(4096));
    require(vm.run(1)==odh::VmStatus::returned_true&&vm.temps()[0]==105,"real Expression scope6 consumes restored shared globals");
}
}
int main(){try{projection();clocks();selections();persistence();std::cout<<checks<<" runtime global checks passed\n";}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
