// SPDX-License-Identifier: MPL-2.0
#include "native_hierarchy_snapshot.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>

namespace {
using namespace odh;using namespace odh_host;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Fixture {
    World world;std::string error;bool active=true;
    Fixture(){for(std::int16_t n=1;n<=4;++n){
        require(world.create_runtime_object(n,100+n,{0,0,3},std::vector<EntryPoint>(27)),"create native bodies");
        require(world.publish_native_slot0(ref(n),{}),"known native zero-slot backing storage");
        words(n,-1,0);require(world.set_position(n,{n*16,n*32,static_cast<std::int16_t>(n%2+1),static_cast<std::uint8_t>(n)}),"set poses");
    }}
    WorldEntityReference ref(std::int16_t n)const{return {n,world.find(n)->generation};}
    void words(std::int16_t n,std::int16_t slot,std::int16_t parent){
        require(world.declare_object_word(n,14,slot)&&world.declare_object_word(n,26,parent),"raw native words");
    }
    void alias(std::int16_t child,std::int16_t parent){words(child,0,parent);require(world.publish_native_slot0(ref(parent),ref(child)),"reciprocal native zero slot");}
    void physical(std::int16_t child,std::int16_t parent,std::size_t slot){require(world.attach(parent,slot,child),"physical source slot");words(child,static_cast<std::int16_t>(slot+1),parent);}
    std::function<bool()> authority(){return [this]{return active;};}
    std::optional<NativeHierarchySnapshot> walked(std::int16_t n=1){return capture_native_hierarchy_snapshot(world,ref(n),authority(),error);}
    std::optional<NativeHierarchySnapshot> direct(std::int16_t n=1){return capture_native_direct_hierarchy_snapshot(world,ref(n),authority(),error);}
};
void ordinary(){Fixture f;const auto s=f.walked();require(s&&s->chain.size()==1&&!s->parent&&s->slot==-1&&!s->normalize_zero_owner,"uncontained -1 stays -1");}
void lone_zero(){Fixture f;f.words(1,0,0);const auto s=f.walked();require(s&&s->slot==-1&&s->normalize_zero_owner==f.ref(1),"zero normalization belongs to final owner");require(f.world.find(1)->object_words.at(14)==0,"capture is read-only");const auto d=f.direct();require(d&&d->slot==0&&!d->normalize_zero_owner,"direct snapshot keeps zero without normalization");}
void alias_chain(){Fixture f;f.alias(1,2);f.alias(2,3);const auto s=f.walked();require(s&&s->chain.size()==3&&s->hierarchy_owner==f.ref(3)&&!s->parent&&s->slot==-1,"walk whole raw zero chain");require(s->root_position==f.world.find(1)->position&&!(s->root_position==f.world.find(3)->position),"pose and level remain root-owned");require(!f.world.find(1)->attachment&&!f.world.find(2)->attachment,"raw aliases do not invent attachments");const auto d=f.direct();require(d&&d->hierarchy_owner==f.ref(1)&&d->parent==f.ref(2)&&d->slot==0&&d->chain.size()==1,"direct snapshot returns immediate raw parent and slot");}
void final_slot(){Fixture f;f.alias(1,2);f.physical(2,4,2);const auto s=f.walked();require(s&&s->parent==f.ref(4)&&s->slot==2&&s->hierarchy_owner==f.ref(2)&&s->chain.size()==2,"convert final slot only, stop before parent");const auto d=capture_native_direct_hierarchy_snapshot(f.world,f.ref(2),f.authority(),f.error);require(d&&d->slot==3,"direct preserves positive native vector index");}
void final_normalization(){Fixture f;f.alias(1,2);f.words(2,0,0);const auto s=f.walked();require(s&&s->normalize_zero_owner==f.ref(2)&&s->slot==-1,"normalization never targets alias root");require(f.world.find(1)->object_words.at(14)==0&&f.world.find(2)->object_words.at(14)==0,"walk never writes raw hierarchy");}
void known_missing_parent(){Fixture f;f.words(1,0,99);const auto s=f.walked();require(s&&!s->parent&&s->slot==-1&&s->chain.back().raw_parent26==99&&s->normalize_zero_owner==f.ref(1),"missing known parent resolves native NULL and retains raw provenance");f.words(1,4,-3);const auto d=f.direct();require(d&&!d->parent&&d->slot==4&&d->chain.back().raw_parent26==-3,"negative parent native NULL distinct from unknown");}
void cycle(){Fixture f;f.alias(1,2);f.words(2,0,1);require(!f.world.publish_native_slot0(f.ref(1),f.ref(2)),"World refuses reciprocal zero cycle");require(!f.walked(),"reject corrupt zero chain instead of inferring storage");f.words(1,0,1);require(!f.walked(),"reject self zero link");}
void positive_self(){Fixture f;f.words(1,2,1);require(!f.walked(),"positive self reference lacks valid physical reciprocal slot");}
void missing_words(){Fixture f;require(f.world.create(5,EntityKind::object),"create incomplete body");require(!f.walked(5),"unknown native words are not native NULL");}
void expired(){Fixture f;f.active=false;require(!f.walked(),"source expiry before capture");f.active=true;const auto s=f.walked();require(bool(s),"valid capture");f.active=false;require(!native_hierarchy_snapshot_still_matches(f.world,*s,f.authority(),f.error),"source expiry after capture");}
void mutation(){Fixture f;f.alias(1,2);const auto s=f.walked();require(bool(s),"capture before mutation");f.words(2,2,4);require(!native_hierarchy_snapshot_still_matches(f.world,*s,f.authority(),f.error),"raw final-owner mutation invalidates proof");}
void parent_generation(){Fixture f;f.physical(1,2,0);const auto s=f.walked();require(bool(s),"capture resolved parent");require(f.world.remove(2)&&f.world.create_runtime_object(2,200,{0,0,0},{}),"replace numeric parent ID");require(!native_hierarchy_snapshot_still_matches(f.world,*s,f.authority(),f.error),"parent numeric ID reuse cannot satisfy captured generation");}
void missing_parent_appears(){Fixture f;f.words(1,0,99);const auto s=f.walked();require(bool(s),"capture native NULL parent");require(f.world.create_runtime_object(99,999,{0,0,0},{}),"publish previously missing numeric parent");require(!native_hierarchy_snapshot_still_matches(f.world,*s,f.authority(),f.error),"NULL-to-live parent invalidates proof");}
void authority_side_effect(){Fixture f;unsigned calls=0;const auto guard=[&]{if(++calls==2)f.words(1,0,2);return true;};require(!capture_native_hierarchy_snapshot(f.world,f.ref(1),guard,f.error),"revalidate after final authority callback mutation");}
NativeHierarchyUnlinkServices services(Fixture& f){NativeHierarchyUnlinkServices s;
    s.native_in_world=[&](auto ref)->std::optional<bool>{const auto p=f.world.native_position(ref);return p?std::optional<bool>{native_in_world_xy(*p,16,16)}:std::nullopt;};
    s.physical_mutation=[](auto,auto,std::size_t,const std::function<bool()>& operation){return operation();};
    s.publish_level=[&](auto r,std::int16_t level,std::string&){const auto* e=f.world.find(r.id);if(!e||e->generation!=r.generation||!e->position)return false;auto p=*e->position;p.level=level;return f.world.publish_native_recovery_position(r,p,false);};
    return s;
}
void unlink_leaf(){Fixture f;f.alias(1,2);const auto pose=f.world.find(1)->position;require(unlink_native_hierarchy_for_out_of_world(f.world,f.ref(1),pose,f.authority(),f.error),"unlink zero-slot leaf");require(f.world.native_slot0(f.ref(2))==WorldEntityReference{}&&f.world.find(1)->object_words.at(14)==-1&&f.world.find(1)->object_words.at(26)==0,"zero leaf clears exact backing and own raw words");require(f.world.find(1)->position==pose,"hierarchy unlink does not publish scalar OOW");}
void promote_alias(){Fixture f;f.alias(1,3);f.alias(2,1);const auto child_before=*f.world.find(2)->position;const auto pose=f.world.find(1)->position;require(unlink_native_hierarchy_for_out_of_world(f.world,f.ref(1),pose,f.authority(),f.error,services(f)),"promote special child to prior special parent");const auto* c=f.world.find(2);require(f.world.native_slot0(f.ref(3))==f.ref(2)&&f.world.native_slot0(f.ref(1))==WorldEntityReference{}&&c->object_words.at(14)==0&&c->object_words.at(26)==3,"promotion updates exact native slot0 relation");require(c->position&&c->position->x==child_before.x&&c->position->y==child_before.y&&c->position->direction==child_before.direction&&c->position->level==pose->level&&!c->attachment,"promoted alias keeps XY heading and native relation, takes old level");}
void promote_physical(){Fixture f;f.physical(1,3,1);f.alias(2,1);const auto pose=f.world.find(1)->position;require(unlink_native_hierarchy_for_out_of_world(f.world,f.ref(1),pose,f.authority(),f.error,services(f)),"promote special child into previous physical slot");const auto* c=f.world.find(2);require(c->attachment&&c->attachment->container==3&&c->attachment->slot==1&&c->object_words.at(14)==2&&c->object_words.at(26)==3&&f.world.find(3)->slots[1]==2&&!f.world.find(1)->attachment,"physical promotion retains native positive-index convention");}
void promote_floor(){Fixture f;const auto pose=*f.world.find(1)->position;auto child=*f.world.find(2)->position;child.x=pose.x;child.y=pose.y;require(f.world.set_position(2,child),"same original floor tile");f.alias(2,1);unsigned published=0;auto s=services(f);s.register_promoted_floor=[&](auto r,const EntityPosition& p,std::string&){++published;return r==f.ref(2)&&p==pose;};require(unlink_native_hierarchy_for_out_of_world(f.world,f.ref(1),pose,f.authority(),f.error,s),"promote special child to original floor snapshot");require(published==1&&!f.world.find(2)->attachment&&f.world.find(2)->object_words.at(14)==-1&&f.world.find(2)->object_words.at(26)==0&&f.world.find(2)->position->level==pose.level,"floor owner published once; child not fake attached");}
void refuse_missing_floor_owner(){Fixture f;f.alias(2,1);require(!unlink_native_hierarchy_for_out_of_world(f.world,f.ref(1),f.world.find(1)->position,f.authority(),f.error,services(f)),"no floor registry fallback");require(f.world.native_slot0(f.ref(1))==f.ref(2)&&f.world.find(2)->object_words.at(14)==0,"missing owner refuses before mutations");}
void physical_callback_expiry(){Fixture f;f.physical(1,3,1);auto s=services(f);s.physical_mutation=[&](auto,auto,std::size_t,const std::function<bool()>& operation){f.active=false;return operation();};require(!unlink_native_hierarchy_for_out_of_world(f.world,f.ref(1),f.world.find(1)->position,f.authority(),f.error,s),"callback cannot execute physical detach after source expiry");require(f.world.find(1)->attachment&&f.world.find(3)->slots[1]==1&&f.world.find(1)->object_words.at(14)==2,"expiry inside phase guard leaves physical relation unchanged");}
void promotion_callback_expiry(){Fixture f;f.physical(1,3,1);f.alias(2,1);auto s=services(f);unsigned calls=0;
    s.physical_mutation=[&](auto,auto,std::size_t,const std::function<bool()>& operation){if(++calls==2)f.active=false;return operation();};
    require(!unlink_native_hierarchy_for_out_of_world(f.world,f.ref(1),f.world.find(1)->position,f.authority(),f.error,s),"promotion refuses expired authority after completed unlink");
    require(calls==2&&!f.world.find(1)->attachment&&!f.world.find(2)->attachment&&f.world.find(3)->slots[1]==0&&
        f.world.native_slot0(f.ref(1))==WorldEntityReference{}&&f.world.find(2)->object_words.at(14)==-1,
        "expired promotion preserves completed detach and never attaches using borrowed parent data");
}
void capture_oow_level(){Fixture f;require(f.world.publish_native_recovery_position(f.ref(1),{},true)&&
    f.world.set_native_out_of_world_level(f.ref(1),1),"known native OOW scalar");const auto s=f.direct();
    require(s&&!s->root_position&&s->native_root_position==std::optional<EntityPosition>{{-16,-16,1,1}},"OOW native scalar captured without inventing represented XYZ");
    require(f.world.set_native_out_of_world_level(f.ref(1),2)&&!native_hierarchy_snapshot_still_matches(f.world,*s,f.authority(),f.error),
        "OOW native level mutation invalidates an earlier hierarchy proof");
}
struct RestoreFixture:Fixture {
    using Tile=std::tuple<std::int32_t,std::int32_t,std::int16_t>;
    std::map<Tile,WorldEntityReference> heads;
    std::vector<std::string> events;
    std::vector<std::function<bool()>> retained;
    static Tile tile(const EntityPosition& p){return {p.x/16,p.y/16,p.level};}
    void oow(){require(world.publish_native_recovery_position(ref(1),{},true)&&
        world.set_native_out_of_world_level(ref(1),1),"real OOW receiver with authenticated native level");}
    NativeHierarchyRestoreServices restore_services(){NativeHierarchyRestoreServices s;
        s.floor_head=[this](const auto& p)->std::optional<WorldEntityReference>{const auto i=heads.find(tile(p));return i==heads.end()?WorldEntityReference{}:i->second;};
        s.publish_floor_head=[this](const auto& p,auto expected,auto next,std::string&){
            auto& value=heads[tile(p)];if(value!=expected)return false;
            events.push_back("floor:"+std::to_string(expected.id)+"->"+std::to_string(next.id));value=next;return true;
        };
        s.physical_mutation=[this](auto child,auto,std::size_t,const std::function<bool()>& operation){
            events.push_back("physical:"+std::to_string(child.id));retained.push_back(operation);return operation();};
        s.publish_level=[this](auto r,std::int16_t value,std::string&){
            const auto* e=world.find(r.id);if(!e||e->generation!=r.generation||!e->position)return false;
            auto p=*e->position;p.level=value;events.push_back("level:"+std::to_string(r.id));
            return world.publish_native_recovery_position(r,p,false);
        };return s;
    }
    bool restore(const NativeHierarchyRestoreDestination& d,NativeHierarchyRestoreServices s){return restore_native_hierarchy_in_world(world,ref(1),d,authority(),error,s);}
};
void restore_empty_floor(){RestoreFixture f;const auto d=NativeHierarchyRestoreDestination{*f.world.find(3)->position,{},-1};f.oow();
    require(f.restore(d,f.restore_services()),"native insertion into actual empty floor head");
    require(f.heads[RestoreFixture::tile(d.position)]==f.ref(1)&&!f.world.find(1)->position&&!f.world.find(1)->attachment&&
        f.world.find(1)->object_words.at(14)==-1&&f.world.find(1)->object_words.at(26)==0&&f.world.native_position(f.ref(1))->level==d.position.level,
        "floor insertion publishes actual head and native level without fabricating XYZ or containment");
    require(f.events==std::vector<std::string>{"floor:0->1"},"empty floor publication has no displaced unlink");
}
void restore_floor_chain(){RestoreFixture f;auto p=*f.world.find(2)->position;auto child=*f.world.find(3)->position;
    child.x=p.x;child.y=p.y;child.level=p.level;require(f.world.set_position(3,child),"same floor native chain child");f.alias(3,2);
    f.heads[RestoreFixture::tile(p)]=f.ref(2);f.oow();const auto before=f.world.find(2)->position;
    require(f.restore({p,{},-1},f.restore_services()),"restore pushes actual old head under inserted receiver");
    require(f.world.native_slot0(f.ref(1))==f.ref(2)&&f.world.native_slot0(f.ref(2))==f.ref(3)&&
        f.world.find(2)->object_words.at(14)==0&&f.world.find(2)->object_words.at(26)==1&&
        !f.world.find(2)->attachment&&!f.world.find(1)->attachment&&f.world.find(2)->position==before,
        "old head retains its own child and XYZ, rather than promoting or dropping it");
    require(f.events==std::vector<std::string>{"floor:2->0","floor:0->1"},"source order clears displaced head before receiver publication");
    World copy;require(copy.restore_snapshot(f.world.snapshot()),"inserted native chain remains a coherent World snapshot");
}
void restore_physical(bool occupied){RestoreFixture f;const auto p=*f.world.find(4)->position;
    if(occupied){f.physical(2,4,1);f.alias(3,2);}f.oow();
    require(f.restore({p,f.ref(4),1},f.restore_services()),"native insertion into physical destination");
    require(f.world.find(4)->slots[1]==1&&f.world.find(1)->attachment&&f.world.find(1)->attachment->container==4&&
        f.world.find(1)->attachment->slot==1&&f.world.find(1)->object_words.at(14)==2&&f.world.find(1)->object_words.at(26)==4&&
        !f.world.find(1)->position,"logical slot conversion uses physical destination and preserves absent receiver XYZ");
    if(occupied)require(f.world.native_slot0(f.ref(1))==f.ref(2)&&f.world.native_slot0(f.ref(2))==f.ref(3)&&
        !f.world.find(2)->attachment&&f.world.find(2)->object_words.at(14)==0&&f.world.find(2)->object_words.at(26)==1,
        "displaced physical occupant becomes raw zero child with its existing child preserved");
    require(f.events==(occupied?std::vector<std::string>{"physical:2","physical:1"}:std::vector<std::string>{"physical:1"}),
        "physical receiver order matches native displaced detach then insertion");
    const auto epoch=f.world.spatial_revision();for(const auto& op:f.retained)require(!op(),"retained insertion phases expire after return");
    require(f.world.spatial_revision()==epoch,"expired insertion phases do not mutate later state");
}
void restore_already_occupying(){RestoreFixture f;const auto p=*f.world.find(1)->position;f.alias(2,1);f.heads[RestoreFixture::tile(p)]=f.ref(1);
    require(f.restore({p,{},-1},f.restore_services())&&f.events.empty()&&f.world.native_slot0(f.ref(1))==f.ref(2),
        "native exact same occupying body is a no-op even with an existing chain");
}
void restore_missing_floor_owner(){RestoreFixture f;const auto p=*f.world.find(2)->position;f.oow();auto s=f.restore_services();s.floor_head={};
    require(!f.restore({p,{},-1},s)&&f.events.empty()&&f.world.find(1)->object_words.at(14)==-1,"missing actual floor owner refuses before mutation");
    s=f.restore_services();s.floor_head=[](const auto&)->std::optional<WorldEntityReference>{return {};};
    require(!f.restore({p,{},-1},s)&&f.events.empty(),"unknown floor head is not an invented empty tile");
}
void restore_expired_destination(){RestoreFixture f;const auto p=*f.world.find(2)->position;f.oow();auto s=f.restore_services();const auto old=f.ref(2);
    s.floor_head=[&](const auto&)->std::optional<WorldEntityReference>{
        require(f.world.remove(2)&&f.world.create_runtime_object(2,202,{0,0,0},{}),"destination ID replaced inside source query");return old;};
    require(!f.restore({p,{},-1},s)&&f.events.empty()&&f.world.native_slot0(f.ref(1))==WorldEntityReference{},
        "expired destination generation cannot become the displaced child");
}
void restore_second_guard_expiry(){RestoreFixture f;f.physical(2,4,1);f.alias(3,2);const auto p=*f.world.find(4)->position;f.oow();auto s=f.restore_services();unsigned calls=0;
    s.physical_mutation=[&](auto,auto,std::size_t,const std::function<bool()>& operation){if(++calls==2)f.active=false;return operation();};
    require(!f.restore({p,f.ref(4),1},s)&&calls==2&&f.world.find(4)->slots[1]==0&&!f.world.find(1)->attachment&&!f.world.find(2)->attachment&&
        f.world.native_slot0(f.ref(2))==f.ref(3)&&f.world.find(2)->object_words.at(14)==-1,
        "second phase expiry leaves completed displaced unlink but never publishes receiver or loses retained chain");
}
void restore_repeated_guard(){RestoreFixture f;const auto p=*f.world.find(4)->position;f.oow();auto s=f.restore_services();
    s.physical_mutation=[](auto,auto,std::size_t,const std::function<bool()>& operation){const bool first=operation();require(!operation(),"native insertion operation is one-shot");return first;};
    require(!f.restore({p,f.ref(4),1},s)&&f.world.find(4)->slots[1]==1,
        "repeated operation is reported as a partial failure instead of accepted success");
}

void native_boundaries(){
    require(!native_in_world_xy({0,24,1,0},8,8)&&!native_in_world_xy({112,24,1,0},8,8)&&
        !native_in_world_xy({24,0,1,0},8,8)&&!native_in_world_xy({24,112,1,0},8,8),"native border cells are outside despite a represented host pose");
    require(native_in_world_xy({4120,24,0,0},8,8)&&!native_in_world_xy({-4080,-16,1,0},8,8),
        "native predicate uses signed cached tile bytes and does not test level");
    Fixture f;require(f.world.set_position(1,{0,24,2,1})&&f.world.set_position(2,{0,24,1,2}),"boundary scalar fixture");
    f.alias(2,1);auto hooks=services(f);
    hooks.unregister_native_floor=[](auto,const auto&,std::string&)->bool{throw std::runtime_error("boundary has no native floor index to clear");};
    hooks.register_promoted_floor=[](auto,const auto&,std::string&)->bool{throw std::runtime_error("outside promotion does not publish a floor head");};
    require(unlink_native_hierarchy_for_out_of_world(f.world,f.ref(1),f.world.native_position(f.ref(1)),f.authority(),f.error,hooks)&&
        f.world.find(2)->position->level==1&&f.world.find(2)->object_words.at(14)==-1&&f.world.native_slot0(f.ref(1))==WorldEntityReference{},
        "direct old-outside snapshot promotes without floor publication or a spurious level write");
}
void unknown_direct_bounds(){Fixture f;
    require(!unlink_native_hierarchy_for_out_of_world(f.world,f.ref(1),f.world.native_position(f.ref(1)),f.authority(),f.error),
        "positive unparented pose alone is not native InWorld proof");
    auto hooks=services(f);hooks.native_in_world=[](auto)->std::optional<bool>{return {};};
    require(!unlink_native_hierarchy_for_out_of_world(f.world,f.ref(1),f.world.native_position(f.ref(1)),f.authority(),f.error,hooks)&&f.world.find(1)->object_words.at(14)==-1,
        "unknown native bounds refuse before hierarchy writes");
}

}
int main(){try{ordinary();lone_zero();alias_chain();final_slot();final_normalization();known_missing_parent();cycle();positive_self();missing_words();expired();mutation();parent_generation();missing_parent_appears();authority_side_effect();unlink_leaf();promote_alias();promote_physical();promote_floor();refuse_missing_floor_owner();physical_callback_expiry();promotion_callback_expiry();capture_oow_level();restore_empty_floor();restore_floor_chain();restore_physical(false);restore_physical(true);restore_already_occupying();restore_missing_floor_owner();restore_expired_destination();restore_second_guard_expiry();restore_repeated_guard();native_boundaries();unknown_direct_bounds();std::cout<<"native hierarchy snapshot: 33 cases passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
