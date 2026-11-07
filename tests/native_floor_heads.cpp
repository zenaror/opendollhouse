// SPDX-License-Identifier: MPL-2.0
#include "native_floor_heads.hpp"
#include "native_hierarchy_snapshot.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace odh;using namespace odh_host;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Fixture {
    World world;bool active=true;std::string error;
    Fixture(){for(std::int16_t id=1;id<=4;++id){require(world.create_runtime_object(id,100+id,{0,0,3},std::vector<EntryPoint>(27)),"actual native constructor");
        require(world.set_position(id,{16*id,32*id,1,0}),"native floor source position");}}
    WorldEntityReference ref(std::int16_t id)const{return {id,world.find(id)->generation};}
    std::vector<WorldEntityReference> refs()const{return {ref(1),ref(2),ref(3),ref(4)};}
    std::function<bool()> authority(){return [this]{return active;};}
    void alias(std::int16_t child,std::int16_t parent){const auto p=*world.find(parent)->position;
        require(world.set_position(child,p)&&world.declare_object_word(child,14,0)&&world.declare_object_word(child,26,parent)&&world.publish_native_slot0(ref(parent),ref(child)),"actual reciprocal same-tile native alias");}
    void oow(std::int16_t id){require(world.publish_native_recovery_position(ref(id),{},true)&&world.set_native_out_of_world_level(ref(id),1),"proven native OOW scalar");}
};
void unique_and_empty(){Fixture f;NativeFloorHeads index(f.world,8,8,f.authority());require(index.seed(f.refs(),f.error),"whole authenticated floor seed");
    require(index.ready()&&index.head(*f.world.find(2)->position)==f.ref(2)&&index.head({24,24,1,0})==WorldEntityReference{},"unique head and actual known empty are distinct");
    require(!index.head({0,24,1,0})&&!index.head({112,24,1,0})&&!index.head({24,24,0,0}),"native boundary and non-indexed floor queries are not empty-cell claims");
    require(index.settled(f.error),"initial native index is settled");
}
void reciprocal_chain(){Fixture f;f.alias(2,1);f.alias(3,2);NativeFloorHeads index(f.world,8,8,f.authority());
    require(index.seed(f.refs(),f.error)&&index.head(*f.world.find(1)->position)==f.ref(1),"chain head comes from reciprocal source topology");
    require(index.head({32,64,1,0})==WorldEntityReference{},"alias body is not a second independent floor head");
}
void duplicate_heads(){Fixture f;const auto p=*f.world.find(1)->position;require(f.world.set_position(2,p),"two canonical heads in one tile");
    NativeFloorHeads index(f.world,8,8,f.authority());require(index.seed(f.refs(),f.error),"ambiguous cohort still records partial knowledge");
    require(!index.head(p)&&index.head(*f.world.find(3)->position)==f.ref(3)&&index.head({24,24,1,0})==WorldEntityReference{},
        "ambiguity is cell-specific and no sorted-ID head is invented");
    require(!index.settled(f.error),"ambiguous source is not accepted as a settled native index");
}
void whole_cohort_required(){Fixture f;NativeFloorHeads index(f.world,8,8,f.authority());auto refs=f.refs();refs.pop_back();
    require(!index.seed(refs,f.error)&&!index.ready()&&!index.head({24,24,1,0}),"omitted body cannot establish empty cells");
    refs=f.refs();refs.push_back(refs.back());require(!index.seed(refs,f.error),"duplicate cohort references rejected");
}
void unknown_state(){Fixture f;require(f.world.create(5,EntityKind::object),"legacy generic body with unknown native scalar/backing");auto refs=f.refs();refs.push_back(f.ref(5));
    NativeFloorHeads index(f.world,8,8,f.authority());require(index.seed(refs,f.error)&&index.ready()&&!index.head({24,24,1,0}),
        "complete cohort with unknown native state cannot prove a cell empty");
}
void physical_excluded(){Fixture f;const auto p=*f.world.find(1)->position;require(f.world.set_position(2,p)&&f.world.attach(1,1,2)&&
    f.world.declare_object_word(2,14,2)&&f.world.declare_object_word(2,26,1),"actual physical slot");f.alias(3,2);
    NativeFloorHeads index(f.world,8,8,f.authority());require(index.seed(f.refs(),f.error)&&index.head(p)==f.ref(1),
        "physical occupant and its logical aliases are not native floor heads");
}
void clear_and_promote(){Fixture f;f.alias(2,1);const auto p=*f.world.find(1)->position;NativeFloorHeads index(f.world,8,8,f.authority());require(index.seed(f.refs(),f.error),"seed old head/child");
    NativeHierarchyUnlinkServices services;
    services.native_in_world=[&](auto ref)->std::optional<bool>{const auto p=f.world.native_position(ref);return p?std::optional<bool>{native_in_world_xy(*p,8,8)}:std::nullopt;};
    services.unregister_native_floor=[&](auto ref,const auto& position,std::string& error){return index.publish(position,ref,{},error);};
    services.register_promoted_floor=[&](auto ref,const auto& position,std::string& error){return index.publish(position,{},ref,error);};
    require(unlink_native_hierarchy_for_out_of_world(f.world,f.ref(1),p,f.authority(),f.error,services),"native index follows actual head removal/promotion");
    require(index.head(p)==f.ref(2)&&!index.settled(f.error),"new head is exact while old source scalar removal is still pending");
    f.oow(1);require(index.head(p)==f.ref(2)&&index.settled(f.error),"actual scalar removal settles pending old head");
}
void restore_pending(){Fixture f;f.alias(3,2);const auto p=*f.world.find(2)->position;f.oow(1);NativeFloorHeads index(f.world,8,8,f.authority());require(index.seed(f.refs(),f.error),"seed displaced chain");
    NativeHierarchyRestoreServices services;
    services.floor_head=[&](const auto& at){return index.head(at);};
    services.publish_floor_head=[&](const auto& at,auto expected,auto next,std::string& error){return index.publish(at,expected,next,error);};
    require(restore_native_hierarchy_in_world(f.world,f.ref(1),{p,{},-1},f.authority(),f.error,services),"source insertion publishes pending actual floor head");
    require(index.head(p)==f.ref(1)&&f.world.native_slot0(f.ref(1))==f.ref(2)&&f.world.native_slot0(f.ref(2))==f.ref(3)&&!index.settled(f.error),
        "pending receiver may remain OOW between327d9 and its distinct scalar publisher");
    require(f.world.publish_native_recovery_position(f.ref(1),p,false)&&index.head(p)==f.ref(1)&&index.settled(f.error),"actual scalar publication settles native insertion");
}
void cas_mismatch(){Fixture f;NativeFloorHeads index(f.world,8,8,f.authority());require(index.seed(f.refs(),f.error),"CAS source");const auto p=*f.world.find(1)->position;
    require(!index.publish(p,f.ref(2),{},f.error)&&index.head(p)==f.ref(1),"wrong expected head cannot replace actual ownership");
    require(!index.publish({24,24,1,0},{},f.ref(1),f.error)&&index.head(p)==f.ref(1),"one receiver cannot steal ownership of a second tile");
}
void stale_and_expired(){Fixture f;NativeFloorHeads index(f.world,8,8,f.authority());require(index.seed(f.refs(),f.error),"lifetime source");
    f.active=false;require(!index.head({24,24,1,0}),"expired source cannot claim known-empty");f.active=true;
    require(f.world.remove(2)&&f.world.create_runtime_object(2,202,{0,0,0},{}),"reuse numerical head ID");
    require(!index.head({24,24,1,0})&&!index.head({32,64,1,0}),"unacknowledged generation reuse invalidates old cohort proof");
}
void constructor_admission(){Fixture f;NativeFloorHeads index(f.world,8,8,f.authority());require(index.seed(f.refs(),f.error),"constructor source");
    require(f.world.create_runtime_object(5,105,{0,0,0},{})&&!index.head({24,24,1,0}),"unowned constructor initially invalidates complete cohort proof");
    require(index.admit_out_of_world(f.ref(5),f.error)&&index.head({24,24,1,0})==WorldEntityReference{},"explicit known-OOW constructor owner restores complete proof");
    const auto old=f.ref(5);require(f.world.remove(5)&&index.forget_unlinked(old,f.error)&&index.head({24,24,1,0})==WorldEntityReference{},"explicit actual removal forgets an unindexed generation");
}
void unexplained_scalar_move(){Fixture f;NativeFloorHeads index(f.world,8,8,f.authority());require(index.seed(f.refs(),f.error),"scalar source");const auto old=*f.world.find(1)->position;
    require(f.world.set_position(1,{24,24,1,0}),"scalar owner bypassed native floor publication");
    require(!index.head(old)&&!index.head({24,24,1,0})&&index.head(*f.world.find(3)->position)==f.ref(3),"unexplained movement makes affected cells unknown instead of silently rebuilding order");
}
}
int main(){try{unique_and_empty();reciprocal_chain();duplicate_heads();whole_cohort_required();unknown_state();physical_excluded();clear_and_promote();restore_pending();cas_mismatch();stale_and_expired();constructor_admission();unexplained_scalar_move();
    std::cout<<"native floor heads: 12 cases passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
