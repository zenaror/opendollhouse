// SPDX-License-Identifier: MPL-2.0
#include "native_seating.hpp"
#include "native_floor_heads.hpp"
#include "odh/vm.hpp"
#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>

namespace {
using namespace odh;using namespace odh_host;using Ref=WorldEntityReference;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Fixture {
    World world;bool active=true;std::string error;
    std::shared_ptr<const VmProgram> program;
    std::map<Ref,std::unique_ptr<Vm>> owners;
    std::shared_ptr<NativeFloorHeads> floors;
    std::vector<Ref> entries;
    std::vector<std::pair<int,int>> queries;
    std::set<std::pair<int,int>> unknown;
    std::function<void(Ref)> on_entry,on_preflight;
    std::optional<bool> expected;
    Fixture(){Bhav code{0x8002,{{100,255,255,{}}},{},Bhav::Metadata{0,4,0,0}};
        program=std::make_shared<const VmProgram>(RoutineSet{{routine_key(4096),code}});}
    Ref ref(std::int16_t id)const{return {id,world.find(id)->generation};}
    Ref add(std::int16_t id,int x,int y,std::uint16_t flags=0,bool gate25=false,bool seat26=false,std::uint8_t facing=0,bool positioned=true,bool empty_functions=false){
        std::vector<EntryPoint> table(27);table[9].action=4096;table[25].action=gate25?4096:0;table[26].action=seat26?4096:0;
        if(empty_functions)table.clear();
        require(world.create_runtime_object(id,1000+id,{0,0,2},table),"actual native object constructor");
        const auto r=ref(id);require(world.declare_object_word(id,8,static_cast<std::int16_t>(flags)),"actual source Flags8 bank");
        if(positioned)require(world.set_position(id,{x*16+8,y*16+8,1,facing}),"actual native tile position");
        auto owner=std::make_unique<Vm>(program,ShareVmProgramTag{},world.vm_memory());
        for(unsigned i=0;i<4;++i)owner->temps()[i]=static_cast<std::int16_t>(id*10+i);
        owners.emplace(r,std::move(owner));return r;
    }
    void alias(std::int16_t child,std::int16_t parent){
        auto pose=*world.find(parent)->position;pose.direction=world.find(child)->position->direction;
        require(world.set_position(child,pose)&&world.declare_object_word(child,14,0)&&
            world.declare_object_word(child,26,parent)&&world.publish_native_slot0(ref(parent),ref(child)),"actual reciprocal zero child");
    }
    void physical(std::int16_t child,std::int16_t parent,std::size_t slot){
        require(world.attach(parent,slot,child)&&world.refresh_contained_position(child)&&
            world.declare_object_word(child,14,static_cast<std::int16_t>(slot+1))&&world.declare_object_word(child,26,parent),"actual reciprocal physical child");
    }
    void seed(){std::vector<Ref> all;for(const auto& e:world.snapshot().entities)all.push_back({e.id,e.generation});
        floors=std::make_shared<NativeFloorHeads>(world,8,8,[this]{return active;});require(floors->seed(all,error),"actual native floor index");}
    std::uint16_t flags(std::int16_t id)const{return static_cast<std::uint16_t>(world.find(id)->object_words.at(8));}
    NativeSeatingContext context(){NativeSeatingContext c;c.world=&world;c.width=c.height=8;c.source_still_matches=[this]{return active;};
        c.floor_head=[this](const EntityPosition& p)->std::optional<Ref>{queries.emplace_back(p.x/16,p.y/16);if(unknown.count(queries.back()))return {};return floors->head(p);};
        c.preflight_entry=[this](Ref r,std::uint16_t ep,std::string& why){
            if(!active||ep!=9||!world.is_live_reference(r)||!owners.count(r)){why="fixture source entry unavailable";return false;}
            if(on_preflight)on_preflight(r);return true;
        };
        c.native_entry=[this](Ref r,std::uint16_t ep,std::string& why){
            if(!active||ep!=9||!world.is_live_reference(r)||!owners.count(r)){why="fixture actual entry expired";return false;}
            auto& owner=*owners.at(r);
            VmPrimitive primitive=[&,r](const VmFrame& frame,const auto&,const auto&,const auto&){
                const std::vector<std::int16_t> defaults(owner.temps().begin(),owner.temps().begin()+4);
                require(frame.context.caller==r.id&&frame.context.stack_object==0&&frame.arguments==defaults,"native EP9 actual Thread TEMP defaults and NULL StackObject");
                if(expected)require(bool(flags(r.id)&0x100U)==*expected,"source flag publication precedes actual EP9 code");
                require(world.write(frame,3,18,static_cast<std::int16_t>(owner.temps()[0]+1)),"actual receiver BHAV World effect");
                entries.push_back(r);if(on_entry)on_entry(r);return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
            };
            Vm check(program,ShareVmProgramTag{},world.vm_memory(),{},primitive);
            const std::vector<std::int16_t> args(owner.temps().begin(),owner.temps().begin()+4);
            if(check.start(routine_key(4096),{r.id,r.id,0},args)!=VmStatus::running)return false;
            const auto outcome=check.run_synchronous_check_with_registers(owner,false,100,primitive);
            return outcome.started&&(outcome.status==VmStatus::returned_true||outcome.status==VmStatus::returned_false);
        };return c;
    }
};
void cardinal_matrix(){constexpr std::array<int,4> dx{-1,0,1,0},dy{0,1,0,-1};
    for(unsigned n=0;n<4;++n)for(const bool correct:{false,true}){
        Fixture f;f.add(1,3,3,0x2040,true);const auto direction=static_cast<unsigned>(n*2);
        f.add(2,3+dx[n],3+dy[n],0,false,true,static_cast<std::uint8_t>(correct?((direction+4)&7U):((direction+5)&7U)));f.seed();f.expected=correct;
        const auto result=update_native_seating(f.context(),{3,3,1});
        require(result.completed&&result.matching_neighbor==correct&&result.visited==1&&result.changed==unsigned(correct)&&result.entry_calls==unsigned(correct),"all source cardinals/facing conditions");
        require(f.flags(1)==static_cast<std::uint16_t>(correct?0x2140:0x2040),"unrelated source flags retained");
    }
}
void clearing_and_gate(){for(const bool gate:{false,true}){
    Fixture f;f.add(1,3,3,0x8100,gate);f.seed();f.expected=false;
    const auto result=update_native_seating(f.context(),{3,3,1});
    require(result.completed&&result.matching_neighbor==false&&result.changed==1&&result.entry_calls==unsigned(gate)&&f.flags(1)==0x8000,"clear source flag and only EP25-enabled changed receivers call EP9");
}}
void native_tree_order(){Fixture f;
    f.add(1,3,3,4,true);f.add(2,4,4,0,false);f.add(3,4,5,0,true);f.add(4,5,4,0,true);f.add(5,5,5,0,true);f.add(6,2,3,0,false,true,4);
    f.alias(2,1);f.physical(3,1,0);f.alias(4,3);f.physical(5,2,1);f.seed();f.expected=true;
    auto c=f.context();std::string why;require(preflight_native_seating(c,{3,3,1},why)&&f.entries.empty()&&f.flags(1)==4,"whole graph admission is read-only");
    const auto result=update_native_seating(c,{3,3,1});
    require(result.completed&&result.visited==5&&result.changed==5&&result.entry_calls==4&&
        f.entries==std::vector<Ref>{f.ref(1),f.ref(5),f.ref(3),f.ref(4)},"native DFS visits raw0 before physical slots; gate25 controls code, not flag publication");
    for(const auto id:{1,2,3,4,5})require((f.flags(id)&0x100U)!=0,"every actual native descendant updated");
}
void hidden_neighbors(){
    {Fixture f;f.add(1,3,3,0,true);f.add(2,2,3,4);f.add(3,4,4,4);f.add(4,4,5,0,false,true,4);f.alias(3,2);f.alias(4,3);f.seed();f.expected=true;
        require(update_native_seating(f.context(),{3,3,1}).completed&&f.flags(1)==0x100,"hidden neighbor head skipped only through native zero chain");}
    {Fixture f;f.add(1,3,3,0x100,true);f.add(2,2,3,4);f.add(3,4,4,0,false,true,4);f.physical(3,2,0);f.seed();f.expected=false;
        require(update_native_seating(f.context(),{3,3,1}).completed&&f.flags(1)==0,"hidden neighbor does not search a physical child instead of raw0");}
}
void native_noops(){Fixture f;f.add(1,3,3);f.seed();auto c=f.context();c.native_entry={};c.preflight_entry={};
    for(const NativeSeatingTile at:std::array<NativeSeatingTile,5>{{{0,3,1},{7,3,1},{3,0,1},{3,3,3},{3,3,0}}}){
        const auto r=update_native_seating(c,at);require(r.completed&&!r.effects_started&&!r.matching_neighbor&&r.visited==0,"native geometry/level guard is proven no-op");}
    require(f.queries.empty(),"native invalid coordinates do not query unknown floor owner");
    const auto empty=update_native_seating(c,{2,2,1});require(empty.completed&&empty.visited==0&&f.queries.size()==1,"knownempty center does not inspect neighbors or require EP9");
}
void unknown_and_shortcircuit(){
    {Fixture f;f.add(1,3,3,0,true);f.seed();f.unknown.insert({2,3});const auto r=update_native_seating(f.context(),{3,3,1});require(!r.completed&&!r.effects_started&&f.flags(1)==0,"unknown neighbor never means empty");}
    {Fixture f;f.add(1,3,3,0,true);f.add(2,2,3,0,false,true,4);f.seed();f.unknown.insert({3,4});f.expected=true;
        const auto r=update_native_seating(f.context(),{3,3,1});require(r.completed&&f.queries.size()==2,"first matching source direction short-circuits later unknown heads");}
}
void explicit_empty_functions(){Fixture f;f.add(1,3,3,0x100,false,false,0,true,true);f.add(2,2,3,0,false,false,0,true,true);f.seed();auto c=f.context();c.preflight_entry={};c.native_entry={};
    const auto r=update_native_seating(c,{3,3,1});require(r.completed&&r.changed==1&&r.entry_calls==0&&f.flags(1)==0,"known absent EP26/25 differs from unavailable tables");}
void missing_owner_and_readonly(){
    {Fixture f;f.add(1,3,3,0,true);f.add(2,2,3,0,false,true,4);f.seed();auto c=f.context();c.preflight_entry={};const auto r=update_native_seating(c,{3,3,1});require(!r.completed&&!r.effects_started&&f.flags(1)==0,"complete graph refuses missing entry owner before writes");}
    {Fixture f;f.add(1,3,3,0,true);f.add(2,2,3,0,false,true,4);f.seed();f.on_preflight=[&](auto ref){require(f.world.declare_object_word(ref.id,8,0x800),"preflight mutation fixture");};
        const auto r=update_native_seating(f.context(),{3,3,1});require(!r.completed&&!r.effects_started&&f.flags(1)==0x800,"read-only owner mutation is detected and not overwritten");}
}
void changed_live_children(){Fixture f;f.add(1,3,3,0,true);f.add(2,4,4,0,true);f.add(3,4,5,0,true);f.add(6,2,3,0,false,true,4);f.alias(2,1);f.physical(3,1,0);f.seed();f.expected=true;
    f.on_entry=[&](Ref r){if(r!=f.ref(1))return;
        require(f.world.publish_native_slot0(f.ref(1),{})&&f.world.declare_object_word(2,14,-1)&&f.world.declare_object_word(2,26,0)&&f.world.publish_native_recovery_position(f.ref(2),{},true),"actual EP9 removes old unvisited zero child");
        const auto created=f.add(4,0,0,0,true,false,0,false);require(f.floors->admit_out_of_world(created,f.error),"actual construction membership owner");f.alias(4,1);
    };
    const auto r=update_native_seating(f.context(),{3,3,1});require(r.completed&&f.entries==std::vector<Ref>{f.ref(1),f.ref(4),f.ref(3)}&&f.flags(2)==0,"iterator reads CURRENT children after EP9, not frozen preview list");
}
void callback_expiry_and_effect(){
    {Fixture f;f.add(1,3,3,0,true);f.add(2,4,4,0,true);f.add(6,2,3,0,false,true,4);f.alias(2,1);f.seed();f.expected=true;f.on_entry=[&](auto){f.active=false;};
        const auto r=update_native_seating(f.context(),{3,3,1});require(!r.completed&&r.effects_started&&r.changed==1&&r.entry_calls==1&&f.flags(1)==0x100&&f.flags(2)==0,"expiry retains first actual effects and prevents later publications");}
    {Fixture f;f.add(1,3,3,0,true);f.add(2,2,3,0,false,true,4);f.seed();f.expected=true;f.on_entry=[&](Ref r){require(f.world.declare_object_word(r.id,8,0),"actual EP9 changes own flag");};
        const auto r=update_native_seating(f.context(),{3,3,1});require(r.completed&&r.changed==1&&f.flags(1)==0,"callback flag effects are not overwritten by a frozen result");}
    {Fixture f;f.add(1,3,3,0,true);f.add(2,2,3,0,false,true,4);f.seed();f.expected=true;f.on_entry=[](Ref){throw std::runtime_error("deliberate EP9 failure");};
        const auto r=update_native_seating(f.context(),{3,3,1});require(!r.completed&&r.effects_started&&r.changed==1&&r.entry_calls==1&&f.flags(1)==0x100&&!r.diagnostic.empty(),"throwing entry retains actual flag publication and reports incomplete owner");}
}
void offsets_and_byte_rules(){std::string why;const EntityPosition p{56,56,1,0};
    constexpr std::array<int,8> dx{-1,-1,0,1,1,1,0,-1},dy{0,1,1,1,0,-1,-1,-1};
    for(std::int16_t d=0;d<8;++d){const auto at=native_seating_front_tile(p,d,why);require(at&&at->x==3+dx[d]&&at->y==3+dy[d]&&at->level==1,"exact source WorldXY direction arrays");}
    require(!native_seating_front_tile(p,-1,why)&&!native_seating_front_tile(p,8,why),"invalid captured direction is not wrapped into a fabricated source offset");
    const auto negative=native_seating_center_tile({-1,-16,1,0});require(negative.x==-1&&negative.y==-1,"native arithmetic tile shift for negative scalars");
    Fixture f;f.add(1,3,3,0x100);f.seed();const auto r=update_native_seating(f.context(),{259,259,257});require(r.completed&&f.flags(1)==0,"native call coordinates and level use signed low byte");
}
}
int main(){try{cardinal_matrix();clearing_and_gate();native_tree_order();hidden_neighbors();native_noops();unknown_and_shortcircuit();explicit_empty_functions();missing_owner_and_readonly();changed_live_children();callback_expiry_and_effect();offsets_and_byte_rules();std::cout<<"native seating: grouped cardinal/tree/callback/boundary matrix passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
