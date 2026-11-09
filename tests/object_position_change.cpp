// SPDX-License-Identifier: MPL-2.0
#include "odh/object_position_change.hpp"
#include "odh/dynamic_object_groups.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
namespace {
unsigned checks=0;
void require(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
using Status=odh::ObjectPositionChangeStatus;
struct Fixture {
    odh::World world;std::vector<std::string> events;bool authorized=true,blocked=false,admitted=true,expected_room_change=true;
    std::int16_t stories=2;odh::ObjectPositionValidServices valid_services;odh::ObjectPositionChangeServices services;
    std::function<void()> wall_effect,register_effect;
    Fixture(){
        for(std::int16_t id:{1,2}){require(world.create(id,odh::EntityKind::object,{0,0,2})&&world.define_type(id,0x1000+id),"create ordinary typed object");
            for(std::int16_t index=0;index<=78;++index)if(index!=2&&index!=3&&index!=11&&index!=67)
                require(world.declare_object_word(id,index,0),"declare exact constructor ObjectData zero");
            std::vector<odh::EntryPoint> ep(27);ep[5].action=500;ep[8].action=508;ep[9].action=509;
            require(world.define_entry_points(id,ep),"declare live source entry points");
            word(id,1,0);word(id,4,1);word(id,13,128);word(id,23,2);word(id,27,5);word(id,28,6);word(id,42,3);
        }
        require(world.set_position(2,{64,64,1,0}),"place independent source support");
        valid_services.architecture=[&](auto,const auto&)->std::optional<odh::ObjectPositionArchitecture>{events.push_back("architecture");odh::ObjectPositionArchitecture a;a.level_offset=0;a.precise_floor=1;a.out_of_bounds=blocked;return a;};
        valid_services.intersections=[&](auto,const auto&,auto)->std::optional<odh::ObjectPositionIntersections>{events.push_back("intersections");odh::ObjectPositionIntersections i;i.footprint_present=true;i.query.status=odh::RoomInfoObstacleIndexStatus::complete;return i;};
        valid_services.same_group=[](auto,auto)->std::optional<bool>{return false;};valid_services.ignore_intersection=valid_services.same_group;
        valid_services.ghost_image=[](auto)->std::optional<bool>{return false;};valid_services.slot0_height=[](auto)->std::optional<std::int32_t>{return 2;};
        valid_services.entry_point5=[&](const auto& q)->std::optional<bool>{require(q.source_still_matches(),"real factory capability reaches EP5");events.push_back("ep5");return false;};
        services.story_count=[&]()->std::optional<std::int16_t>{events.push_back("stories");return stories;};
        services.admit_single_ordinary=[&](auto)->std::optional<bool>{events.push_back("admit");return admitted&&(static_cast<std::uint16_t>(world.find(1)->object_words.at(40))&0xc000U)==0;};
        services.room_at=[&](const auto&)->std::optional<std::uint16_t>{events.push_back("room");return 3;};
        services.unregister_position=[&](auto,bool change){require(change==expected_room_change,"source room change matches old geometry/container");events.push_back("unregister");return true;};
        services.register_position=[&](auto id,std::uint16_t room,bool change){require(change==expected_room_change&&world.find(id.id)->object_words.at(29)==static_cast<std::int16_t>(room-1),"registration sees source Room29 publication");events.push_back("register");if(register_effect)register_effect();return true;};
        services.wall_use=[&](auto,bool set,bool exclusive){require(!exclusive,"normal source SetWallUse");events.push_back(set?"wall":"wall-clear");if(set&&wall_effect)wall_effect();return true;};
        services.object_supported=[&](auto,const auto& pos,bool set){require(set&&pos==*world.find(1)->position,"support receives actual published position");events.push_back("support");return true;};
        services.dynamic_multitile=[&](auto){events.push_back("dynamic");return true;};
        services.entry_point9=[&](auto){events.push_back("ep9");word(1,0,91);return true;};
        services.restore_container=[&](auto target,auto parent,std::size_t slot){events.push_back("restore-slot");return world.attach(parent.id,slot,target.id)&&world.refresh_contained_position(target.id);};
        services.place_in_slot0=[&](auto target,auto support){events.push_back("slot");auto plan=world.prepare_move_to_slot(support.id,0,target.id);return plan&&world.commit_move_to_slot(*plan);};
    }
    void word(int id,int index,std::int16_t value){odh::VmFrame f;f.context.caller=static_cast<std::int16_t>(id);require(world.write(f,3,index,value),"write live source object word");}
    odh::ObjectPositionValidRequest request(){return {{1,world.find(1)->generation},{24,24,1,0},3,1,[&]{return authorized;}};}
    odh::ObjectPositionChangeResult run(){odh::ObjectPositionValid valid(world,valid_services);odh::ObjectPositionChange change(world,valid,services);return change.apply(request());}
};
struct GroupFixture {
    odh::World world;odh::ObjectGroup group;odh::ObjectPositionChangeServices services;
    odh::ObjectPositionValidServices checks;std::vector<std::string> events;
    std::int16_t blocked=0;bool authorized=true;std::function<void(std::int16_t)> check_effect;
    void word(std::int16_t id,std::int16_t index,std::int16_t value){odh::VmFrame f;f.context.caller=id;
        require(world.write(f,3,index,value),"write group source word");}
    void add(std::int16_t id,bool member,std::optional<std::uint32_t> master=0x9000U){
        require(world.create(id,odh::EntityKind::object,{0,0,2})&&world.define_type(id,0x1000U+static_cast<std::uint32_t>(id),
            member?master:std::nullopt),"create exact typed physical member or contained singleton");
        for(std::int16_t index=0;index<=78;++index)if(index!=2&&index!=3&&index!=11&&index!=67)
            require(world.declare_object_word(id,index,0),"declare group constructor words");
        word(id,4,1);word(id,13,128);word(id,23,1);word(id,27,5);word(id,28,6);word(id,42,3);
        std::vector<odh::EntryPoint> ep(27);ep[5].action=500;ep[8].action=508;ep[9].action=509;
        require(world.define_entry_points(id,ep),"declare group physical entry points");
    }
    explicit GroupFixture(bool imported=false){
        for(const auto id:{5,2,8})add(static_cast<std::int16_t>(id),true,
            imported?(id==2?std::optional<std::uint32_t>{}:std::optional<std::uint32_t>{id==8?0xa000U:0x9000U}):std::optional<std::uint32_t>{0x9000U});
        group={0x9000U,true,2,{{5,world.find(5)->generation,0,0,0},
            {2,world.find(2)->generation,16,0,0},{8,world.find(8)->generation,16,16,1}}};
        services.group=[&](auto)->std::optional<odh::ObjectGroup>{return group;};
        services.admit_group_member=[&](auto id)->std::optional<bool>{const auto* e=world.find(id.id);
            return e&&e->generation==id.generation&&e->definition&&e->definition->master_guid==std::optional<std::uint32_t>{0x9000U}&&
                (static_cast<std::uint16_t>(e->object_words.at(40))&0xc000U)==0;};
        services.admit_single_ordinary=[&](auto id)->std::optional<bool>{const auto* e=world.find(id.id);
            return e&&e->generation==id.generation&&e->definition&&!e->definition->master_guid;};
        services.story_count=[]()->std::optional<std::int16_t>{return 2;};
        services.room_at=[](const auto&)->std::optional<std::uint16_t>{return 3;};
        services.unregister_position=[&](auto id,bool){events.push_back("pre"+std::to_string(id.id));return true;};
        services.register_position=[&](auto id,std::uint16_t room,bool){events.push_back("post"+std::to_string(id.id));
            require(world.find(id.id)->object_words.at(29)==static_cast<std::int16_t>(room-1),"group Post registers current Room29");return true;};
        services.wall_use=[](auto,bool,bool){return true;};services.object_supported=[](auto,const auto&,bool){return true;};
        services.dynamic_multitile=[](auto){return true;};services.entry_point9=[](auto){return true;};
        services.restore_container=[&](auto child,auto parent,std::size_t slot){events.push_back("restore"+std::to_string(child.id));
            return world.attach(parent.id,slot,child.id)&&world.refresh_contained_position(child.id);};
        services.place_in_slot0=[&](auto child,auto parent){return world.place_in_slot(parent.id,0,child.id);};
        services.publish_position=[&](auto id,const auto& pos,std::uint8_t heading){events.push_back("set"+std::to_string(id.id));
            word(id.id,1,heading);return world.place_individual_on_floor(id.id,pos,heading);};
        checks.architecture=[&](auto id,const auto&)->std::optional<odh::ObjectPositionArchitecture>{events.push_back("check"+std::to_string(id.id));
            if(check_effect)check_effect(id.id);odh::ObjectPositionArchitecture a;a.precise_floor=1;a.level_offset=0;a.out_of_bounds=id.id==blocked;return a;};
        checks.intersections=[](auto,const auto&,auto)->std::optional<odh::ObjectPositionIntersections>{
            odh::ObjectPositionIntersections result;result.footprint_present=true;result.query.status=odh::RoomInfoObstacleIndexStatus::complete;return result;};
        checks.same_group=[](auto,auto)->std::optional<bool>{return true;};checks.ignore_intersection=checks.same_group;
        checks.ghost_image=[](auto)->std::optional<bool>{return false;};
        if(imported){
            group.definition_guid=0;const auto original=group;const auto source=world.snapshot();
            const auto exact_member=[&,source](odh::ObjectPositionIdentity id)->std::optional<bool>{
                const auto* current=world.find(id.id);
                const auto original=std::find_if(source.entities.begin(),source.entities.end(),[&](const auto& e){return e.id==id.id;});
                return current&&original!=source.entities.end()&&current->generation==id.generation&&
                    original->generation==id.generation&&current->definition&&original->definition&&
                    current->definition->guid==original->definition->guid&&
                    current->definition->master_guid==original->definition->master_guid;
            };
            services.admit_group_member=exact_member;
            services.authenticate_imported_group=[original,exact_member](const odh::ObjectGroup& value){
                if(value.definition_guid||value.multitile!=original.multitile||value.base_id!=original.base_id||
                   value.members.size()!=original.members.size())return false;
                for(std::size_t n=0;n<value.members.size();++n){const auto& a=value.members[n];const auto& b=original.members[n];
                    if(a.id!=b.id||a.generation!=b.generation||a.offset_x!=b.offset_x||a.offset_y!=b.offset_y||
                       a.offset_level!=b.offset_level||!exact_member({a.id,a.generation}).value_or(false))return false;
                }return true;
            };
        }
    }
    odh::ObjectPositionValidRequest request(std::uint8_t direction=2){return {{group.base_id,world.find(group.base_id)->generation},
        {32,48,1,direction},direction,1,[&]{return authorized;}};}
    odh::ObjectPositionChangeResult run(bool existing=false,std::optional<odh::ObjectPositionValidRequest> request_override={}){
        odh::ObjectPositionValid placement(world,checks);odh::ObjectPositionChange change(world,placement,services);
        const auto r=request_override?*request_override:request();return existing?change.apply_existing(r):change.apply(r);
    }
    void old_positions(){for(const auto id:{5,2,8})require(world.set_position(static_cast<std::int16_t>(id),{100+id,80,1,0}),"position existing physical member");}
};
void imported_group_position(){
    for(const std::uint8_t direction:{0,2,4,6}){
        GroupFixture f(true);f.old_positions();const auto result=f.run(true,f.request(direction));
        require(result.status==Status::success&&result.position_published,
            "authenticated imported heterogeneous group uses the same complete placement lifecycle");
        require(f.world.find(5)->definition->master_guid==std::optional<std::uint32_t>{0x9000U}&&
            !f.world.find(2)->definition->master_guid&&f.world.find(8)->definition->master_guid==std::optional<std::uint32_t>{0xa000U},
            "imported placement preserves each individual master without inventing one common master");
    }
    for(unsigned failure=0;failure<7;++failure){
        GroupFixture f(true);f.old_positions();
        if(failure==0)f.services.authenticate_imported_group={};
        if(failure==1)f.services.authenticate_imported_group=[](const auto&){return false;};
        if(failure==2)++f.group.members[0].offset_x;
        if(failure==3)std::swap(f.group.members[0],f.group.members[1]);
        if(failure==4)f.group.base_id=5;
        if(failure==5)f.group.members.pop_back();
        if(failure==6)++f.group.members[0].generation;
        const auto first=f.world.find(5)->position,base=f.world.find(2)->position,last=f.world.find(8)->position;
        require(f.run(true).status==Status::unsupported&&f.events.empty()&&
            f.world.find(5)->position==first&&f.world.find(2)->position==base&&f.world.find(8)->position==last,
            "missing, false, reordered, partial or changed source manifest cannot start imported placement");
    }
    GroupFixture constructed;constructed.group.definition_guid=0xa000U;
    constructed.services.authenticate_imported_group=[](const auto&){return true;};
    require(constructed.run().status==Status::unsupported&&constructed.events.empty(),
        "import authority cannot bypass the common-master check for a constructed group");
}
void physical_group_position(){
    for(const std::uint8_t direction:{0,1,2,3,4,5,6,7}){
        GroupFixture f;
        const auto result=f.run(false,f.request(direction));
        require(result.status==Status::success&&result.effects_started&&result.position_published,
            "complete ordinary group moves through common source lifecycle");
        const auto* first=f.world.find(5);const auto* base=f.world.find(2);const auto* last=f.world.find(8);
        odh::EntityPosition expected_first{16,48,1,direction},expected_last{32,64,2,direction};
        if(direction==2){expected_first={32,32,1,direction};expected_last={16,48,2,direction};}
        if(direction==4){expected_first={48,48,1,direction};expected_last={32,32,2,direction};}
        if(direction==6){expected_first={32,64,1,direction};expected_last={48,48,2,direction};}
        require(first->position==std::optional<odh::EntityPosition>{expected_first}&&
            base->position==std::optional<odh::EntityPosition>{{32,48,1,direction}}&&last->position==std::optional<odh::EntityPosition>{expected_last},
            "group offsets rotate about explicit base; diagonal headings preserve source NORTH geometry");
        require(f.events==std::vector<std::string>{"pre5","check5","pre2","check2","pre8","check8",
            "set5","set2","set8","post5","post2","post8"},
            "source order is interleaved Pre/check, all SetIndiv, then all Post; not sorted by numeric ID");
    }
    {
        GroupFixture f;f.group.base_id=8;const auto result=f.run(false,f.request(0));
        require(result.status==Status::success&&f.world.find(8)->position==std::optional<odh::EntityPosition>{{32,48,2,0}}&&
            f.world.find(5)->position==std::optional<odh::EntityPosition>{{16,32,1,0}},
            "base is explicit, not minimum ID/list-first, and level offsets do not subtract base level");
    }
    {
        GroupFixture f;f.add(11,false);
        f.group={0x100bU,false,11,{{11,f.world.find(11)->generation,32,-16,1}}};
        f.services.admit_group_member=[&](auto id)->std::optional<bool>{const auto* e=f.world.find(id.id);
            return e&&id.id==11&&e->generation==id.generation&&e->definition&&!e->definition->master_guid;};
        require(f.world.set_position(11,{96,80,1,0}),"direct subpart singleton has existing source pose and raw offsets");
        const auto result=f.run(true,f.request(2));
        require(result.status==Status::success&&f.world.find(11)->position==std::optional<odh::EntityPosition>{{32,48,2,2}}&&
            f.group.members.front().offset_x==32&&f.group.members.front().offset_y==-16&&f.group.members.front().offset_level==1,
            "direct-subpart ChangePosition retains raw offsets: own XY cancels about base while raw LevelOffset is applied");
    }
    {
        GroupFixture f;f.old_positions();const auto before=f.world.snapshot();f.blocked=2;
        f.check_effect=[&](auto id){if(id==2)f.word(2,60,77);};const auto result=f.run(true);
        require(result.status==Status::blocked&&result.effects_started&&!result.position_published&&f.world.find(2)->object_words.at(60)==77,
            "blocked group preserves real check effects without publishing any part");
        for(const auto& e:before.entities)require(f.world.find(e.id)->position==e.position,"blocked group keeps original physical poses");
        require(f.events==std::vector<std::string>{"pre5","check5","pre2","check2","post5","post2"},
            "blocked middle member replays only the already-Pre'd prefix, never later member");
    }
    {
        GroupFixture f;f.old_positions();f.add(11,false);require(f.world.set_position(11,{64,64,1,4})&&
            f.world.attach(11,1,5)&&f.world.refresh_contained_position(5),"existing group member has actual old container");
        f.blocked=2;const auto result=f.run(true);
        require(result.status==Status::blocked&&f.world.find(5)->attachment&&f.world.find(5)->attachment->container==11&&
            f.world.find(11)->slots[1]==5&&std::find(f.events.begin(),f.events.end(),"restore5")!=f.events.end(),
            "blocked group restores exact old member slot before prefix Post");
    }
    {
        GroupFixture f;f.old_positions();auto request=f.request(6);request.candidate={-32768,-32768,1,6};
        const auto result=f.run(true,request);
        require(result.status==Status::success&&!f.world.find(5)->position&&!f.world.find(2)->position&&!f.world.find(8)->position&&
            f.events==std::vector<std::string>{"pre5","pre2","pre8","set5","set2","set8","post5","post2","post8"},
            "group OOW skips PositionValid but performs every ordered Pre/publication/Post");
    }
    {
        GroupFixture f;f.add(11,false);require(f.world.attach(5,0,11)&&f.world.refresh_contained_position(11),"group part owns real contained child");
        const auto result=f.run();
        require(result.status==Status::success&&f.world.find(11)->attachment&&f.world.find(11)->attachment->container==5&&
            f.world.find(11)->position&&f.world.find(11)->position->x==32&&f.world.find(11)->position->y==32,
            "group movement preserves and refreshes actual contained child through existing recursive lifecycle");
        const auto post_child=std::find(f.events.begin(),f.events.end(),"post11"),post_parent=std::find(f.events.begin(),f.events.end(),"post5");
        require(post_child<post_parent,"contained child registers before its owning group member");
    }
    for(unsigned mode=0;mode<4;++mode){
        GroupFixture f;
        if(mode==0)f.services.group=[](auto)->std::optional<odh::ObjectGroup>{return {};};
        if(mode==1)++f.group.members.front().generation;
        if(mode==2)f.group.members.resize(65);
        if(mode==3)f.services.admit_group_member={};
        const auto result=f.run();require(result.status==Status::unsupported&&!result.effects_started&&!result.position_published&&f.events.empty(),
            "unknown/stale/oversized/unadmitted group cannot start partial physical effects");
    }
    {
        GroupFixture f;f.check_effect=[&](auto id){if(id==2)f.word(5,40,0x4000);};const auto result=f.run();
        require(result.status==Status::unsupported&&result.effects_started&&!result.position_published&&
            f.world.find(5)->object_words.at(40)==0x4000,
            "later member's EP5 cannot turn an earlier part architectural and publish it under stale ordinary admission");
    }
    {
        GroupFixture f;f.check_effect=[&](auto){++f.group.members.front().generation;};const auto result=f.run();
        require(result.status==Status::unsupported&&result.effects_started&&!result.position_published&&
            std::find(f.events.begin(),f.events.end(),"post5")==f.events.end(),
            "membership changes during actual check retain effects and fail closed before any member publication");
    }
}
void dynamic_group_support_footprint(){
    for(const bool provide:{false,true}){
        Fixture f;f.expected_room_change=false;f.word(1,4,2);
        require(f.world.set_position(1,{32,32,1,0})&&f.world.set_static_footprint(1,false),"same-room dynamic object starts on floor");
        f.services.group=[&](auto)->std::optional<odh::ObjectGroup>{return odh::ObjectGroup{0x1001U,false,1,{{1,f.world.find(1)->generation,0,0,0}}};};
        f.services.admit_group_member=[&](auto id){return f.services.admit_single_ordinary(id);};
        f.valid_services.intersections=[](auto,const auto&,auto)->std::optional<odh::ObjectPositionIntersections>{
            odh::ObjectPositionIntersections i;i.footprint_present=true;i.query.status=odh::RoomInfoObstacleIndexStatus::complete;
            i.query.intersections.push_back({2,3,{0,0,8,8},odh::RoomInfoObstacleCollection::static_set});return i;};
        unsigned calls=0;
        if(provide)f.services.unregister_support_footprint=[&](auto id){++calls;
            require(id.id==1&&!f.world.find(1)->attachment&&!f.world.find(1)->static_footprint,
                "extra dynamic footprint unregister happens before support-slot publication");f.events.push_back("footprint");return true;};
        odh::ObjectPositionValid valid(f.world,f.valid_services);odh::ObjectPositionChange change(f.world,valid,f.services);
        const auto result=change.apply_existing(f.request());
        if(provide)require(result.status==Status::success&&calls==1&&f.world.find(1)->attachment&&
            std::find(f.events.begin(),f.events.end(),"footprint")<std::find(f.events.begin(),f.events.end(),"slot"),
            "dynamic same-room support case uses exact separate Footprint.Unregister capability once");
        else require(result.status==Status::unsupported&&result.effects_started&&!result.position_published&&
            !f.world.find(1)->attachment&&calls==0,"missing extra footprint authority cannot silently admit dynamic support placement");
    }
}
void retained_parent_source_pre(){
    for(const bool refuse:{false,true}){
        Fixture f;
        require(f.world.attach(2,1,1)&&f.world.refresh_contained_position(1),"retired parent holds source target with its current source pose");
        const odh::WorldEntityReference parent{2,f.world.find(2)->generation};
        const odh::WorldEntityReference child{1,f.world.find(1)->generation};
        unsigned detach_calls=0;
        require(f.world.remove_retaining_contents(parent,[&](const auto& token){
            require(f.world.create(2,odh::EntityKind::object,{0,0,2}),"source parent ID reusable before child Pre");
            f.services.detach_container=[&](auto item,auto owner,std::size_t slot){
                ++detach_calls;
                require(item.id==child.id&&item.generation==child.generation&&owner.id==parent.id&&owner.generation==parent.generation&&slot==1,
                    "Pre detach carries both exact identities, never replacement parent");
                return !refuse&&f.world.detach_retained_slot(token,slot,{item.id,item.generation});
            };
            auto request=f.request();request.candidate={-32768,-32768,1,6};request.direction=6;
            request.source_still_matches=[&]{return f.world.retired_container_active(token);};
            odh::ObjectPositionValid valid(f.world,f.valid_services);odh::ObjectPositionChange change(f.world,valid,f.services);
            const auto result=change.apply_existing(request);
            require(detach_calls==1,"source Pre invokes retained detach exactly once");
            if(refuse){require(result.status==Status::unsupported&&f.world.find(1)->attachment,
                "refused detach preserves already executed unregister effects and containment");return false;}
            require(result.status==Status::success&&!f.world.find(1)->attachment&&!f.world.find(1)->position&&
                f.world.find(2)->slots[1]==0,"source child OOW placement leaves replacement parent untouched");
            return true;
        })==!refuse,"retired parent source lifecycle outcome");
    }
}
void lifecycle_phase_boundaries(){
    using Phase=odh::ObjectPositionChangePhase;
    // One producer, including restoration/support/recursive descendants/OOW.
    for(unsigned mode=0;mode<6;++mode){
        Fixture f;bool existing=false;auto request=f.request();
        if(mode==1){existing=true;f.blocked=true;require(f.world.attach(2,1,1)&&f.world.refresh_contained_position(1),"phase fixture holds the existing root");}
        if(mode==2||mode==3){existing=true;f.expected_room_change=mode==3;
            require(f.world.set_position(1,{32,32,1,0})&&f.world.place_in_slot(1,0,2),"phase fixture owns a real child");
            f.services.admit_single_ordinary=[](auto)->std::optional<bool>{return true;};
            if(mode==3)request.candidate={-32768,-32768,1,0};
        }
        if(mode==4){f.word(1,4,2);f.valid_services.intersections=[&](auto,const auto&,auto)->std::optional<odh::ObjectPositionIntersections>{
            f.events.push_back("intersections");odh::ObjectPositionIntersections i;i.footprint_present=true;i.query.status=odh::RoomInfoObstacleIndexStatus::complete;
            i.query.intersections.push_back({2,3,{0,0,8,8},odh::RoomInfoObstacleCollection::static_set});return i;};}
        if(mode==5)f.blocked=true;
        std::optional<Phase> active;std::vector<Phase> phases;unsigned checks_outside=0,ep5=0;
        f.services.lifecycle_phase=[&](auto id,Phase phase,const std::function<bool()>& operation){
            require(!active&&id.id==1&&id.generation==f.world.find(1)->generation,"phase hook owns the exact root once without nested guards");
            phases.push_back(phase);active=phase;
            struct Exit{std::optional<Phase>& active;~Exit(){active.reset();}} exit{active};
            return operation();
        };
        const auto unregister=f.services.unregister_position;const auto registered=f.services.register_position;
        f.services.unregister_position=[&](auto id,bool changed){require(active.has_value(),"source root/child unregister runs inside its lifecycle phase");return unregister(id,changed);};
        f.services.register_position=[&](auto id,std::uint16_t room,bool changed){require(active==Phase::post,"source registration runs only inside the Post guard");return registered(id,room,changed);};
        const auto wall=f.services.wall_use;
        f.services.wall_use=[&](auto id,bool set,bool exclusive){require(active.has_value()&&(!set||active==Phase::post),"wall mutations carry their exact Pre/Post guard");return wall(id,set,exclusive);};
        const auto dynamic=f.services.dynamic_multitile,entry9=f.services.entry_point9;
        f.services.dynamic_multitile=[&](auto id){require(active==Phase::post,"dynamic source EP8 is inside Post only");return dynamic(id);};
        f.services.entry_point9=[&](auto id){require(active==Phase::post,"placement source EP9 is inside Post only");return entry9(id);};
        const auto arch=f.valid_services.architecture;const auto intersection=f.valid_services.intersections;const auto entry5=f.valid_services.entry_point5;
        f.valid_services.architecture=[&](auto id,const auto& point){require(!active,"PositionValid architecture is outside Pre/Post guards");++checks_outside;return arch(id,point);};
        f.valid_services.intersections=[&](auto id,const auto& point,auto direction){require(!active,"PositionValid RoomInfo query is outside lifecycle guards");return intersection(id,point,direction);};
        f.valid_services.entry_point5=[&](const auto& call){require(!active,"actual EP5 executes outside lifecycle guards");++ep5;return entry5(call);};
        const auto slot=f.services.place_in_slot0;const auto restore=f.services.restore_container;
        f.services.place_in_slot0=[&](auto id,auto parent){require(!active,"support-slot scalar publication is outside lifecycle guards");return slot(id,parent);};
        f.services.restore_container=[&](auto id,auto parent,std::size_t index){require(!active,"blocked old-slot restoration occurs before guarded Post");return restore(id,parent,index);};
        f.services.publish_position=[&](auto id,const std::optional<odh::EntityPosition>& position,std::uint8_t direction){
            require(!active,"scalar position publication has a separate host guard");f.word(id.id,1,direction);
            return f.world.place_individual_on_floor(id.id,position,direction);
        };
        odh::ObjectPositionValid valid(f.world,f.valid_services);odh::ObjectPositionChange change(f.world,valid,f.services);
        const auto result=existing?change.apply_existing(request):change.apply(request);
        require(result.status==(mode==1||mode==5?Status::blocked:Status::success)&&
            phases==std::vector<Phase>{Phase::pre,Phase::post}&&!active,
            "new/existing/held/recursive/support/OOW paths have exactly one guarded Pre and Post");
        require(checks_outside==(mode==3?0U:1U)&&ep5==(mode==4?2U:0U),
            "source check runs once, or skips OOW, with each bilateral EP5 performed once");
    }
    for(unsigned mode=0;mode<5;++mode){
        Fixture f;std::function<bool()> retained;unsigned pre_calls=0,post_calls=0;
        f.services.lifecycle_phase=[&](auto,Phase phase,const std::function<bool()>& operation){
            if(phase==Phase::post)++post_calls;else ++pre_calls;
            retained=operation;
            if(mode==0)return true; // A wrapper cannot invent completion.
            const bool completed=operation();
            if(mode==1){require(!operation(),"a phase operation cannot execute twice");return true;}
            if(mode==2)return false; // Completed effects remain durable.
            if(mode==3){f.word(1,60,77);throw std::runtime_error("phase wrapper failed after effects");}
            return completed;
        };
        const auto result=f.run();
        if(mode<4){require(result.status==Status::unsupported&&result.effects_started&&pre_calls==1&&post_calls==0,
                "skipped/repeated/rejected/throwing Pre cannot promote lifecycle success");
            if(mode==1||mode==2||mode==3)require(std::count(f.events.begin(),f.events.end(),"unregister")==1,
                "completed Pre effects are preserved exactly once on hook failure");
            if(mode==3)require(f.world.find(1)->object_words.at(60)==77,"throwing guard preserves durable source effects");
        }else require(result.status==Status::success&&pre_calls==1&&post_calls==1,"valid phase wrapper completes its actual operations");
        const auto events=f.events;require(retained&&!retained()&&f.events==events,"retained operation expires without touching World or stack closures");
    }
    Fixture post_failure;post_failure.services.lifecycle_phase=[](auto,Phase phase,const std::function<bool()>& operation){
        const auto result=operation();return phase==Phase::post?false:result;
    };
    const auto failed=post_failure.run();require(failed.status==Status::unsupported&&failed.position_published&&
        post_failure.world.find(1)->object_words.at(0)==91,"rejected Post retains physical publication and completed placement trees");
}
void source_order(){
    Fixture f;f.word(1,59,8);const auto result=f.run();
    require(result.status==Status::success&&result.effects_started&&result.position_published&&f.world.find(1)->position==std::optional<odh::EntityPosition>{{24,24,1,2}},"source SetIndivPosition quantizes heading then publishes tile");
    require(f.events==std::vector<std::string>{"admit","stories","room","unregister","architecture","intersections","admit","room","register","wall","support","dynamic","ep9"},"complete single ordinary source callback order");
    require(f.world.find(1)->object_words.at(29)==2&&f.world.find(1)->object_words.at(0)==91,"Room29 and real placement-tree effects persist");
    Fixture live;live.wall_effect=[&]{live.word(1,59,8);live.word(1,63,2);};require(live.run().status==Status::success&&
        live.events==f.events,"EP6 changes are observed before source Category8 support");
    Fixture blocked;blocked.blocked=true;const auto no=blocked.run();require(no.status==Status::blocked&&no.placement.placement_error==0&&no.effects_started&&!no.position_published&&
        !blocked.world.find(1)->position&&blocked.world.find(1)->object_words.at(29)==-1,"failed new placement remains OOW but runs PositionChange Room0");
    require(blocked.events==std::vector<std::string>{"admit","stories","room","unregister","architecture","admit","register"},"blocked source performs registration without wall EP8 or EP9");
    Fixture high;high.stories=0;require(high.run().status==Status::unsupported,"invalid architecture stories remain unsupported");
    Fixture above;auto req=above.request();req.candidate.level=3;odh::ObjectPositionValid valid(above.world,above.valid_services);odh::ObjectPositionChange service(above.world,valid,above.services);
    const auto reject=service.apply(req);require(reject.status==Status::blocked&&reject.placement.placement_error==18&&!reject.effects_started&&
        above.events==std::vector<std::string>{"admit","stories"},"source Level exceeds Stories guard precedes unregister and PositionValid");
}
void support_branch(){
    Fixture f;f.word(1,4,2);
    f.valid_services.intersections=[&](auto,const auto&,auto)->std::optional<odh::ObjectPositionIntersections>{f.events.push_back("intersections");odh::ObjectPositionIntersections i;i.footprint_present=true;i.query.status=odh::RoomInfoObstacleIndexStatus::complete;
        i.query.intersections.push_back({2,3,{0,0,8,8},odh::RoomInfoObstacleCollection::static_set});return i;};
    const auto result=f.run();require(result.status==Status::success&&result.placement.support_slot&&f.world.find(1)->attachment&&
        f.world.find(1)->attachment->container==2&&f.world.find(2)->slots[0]==1&&f.world.find(1)->position->x==64,"source support publishes exact reciprocal slot0 and support position");
    require(f.events==std::vector<std::string>{"admit","stories","room","unregister","architecture","intersections","ep5","ep5","admit","slot","room","register"}&&
        f.world.find(1)->object_words.at(0)==0,"contained PositionChange returns before wall support dynamic and placement trees");
}
void existing_move_and_held_tree(){
    Fixture move;move.expected_room_change=false;
    require(move.world.set_position(1,{32,32,1,6}),"place existing source object");
    odh::ObjectPositionValid valid(move.world,move.valid_services);odh::ObjectPositionChange service(move.world,valid,move.services);
    auto result=service.apply_existing(move.request());
    require(result.status==Status::success&&move.world.find(1)->position==std::optional<odh::EntityPosition>{{24,24,1,2}},"existing floor move and quantized rotation publish");
    require(move.events==std::vector<std::string>{"admit","stories","room","room","unregister","wall-clear","architecture","intersections","admit","room","register","wall","dynamic","ep9"},"existing floor clears wall before validation and registers before placement callbacks");
    Fixture no;no.expected_room_change=false;no.blocked=true;require(no.world.set_position(1,{32,32,1,6}),"existing blocked source pose");
    odh::ObjectPositionValid nv(no.world,no.valid_services);odh::ObjectPositionChange nc(no.world,nv,no.services);
    result=nc.apply_existing(no.request());require(result.status==Status::blocked&&!result.position_published&&no.world.find(1)->position==std::optional<odh::EntityPosition>{{32,32,1,6}},"blocked existing move preserves exact original pose/heading");
    Fixture held;require(held.world.attach(2,1,1)&&held.world.refresh_contained_position(1),"held root uses actual nonzero original slot");
    held.word(1,1,6); // Keep real direction bank aligned to source pose.
    held.blocked=true;bool detached_during_check=false;
    const auto original_arch=held.valid_services.architecture;
    held.valid_services.architecture=[&](auto id,const auto& p){detached_during_check=!held.world.find(1)->attachment&&held.world.find(2)->slots[1]==0;return original_arch(id,p);};
    odh::ObjectPositionValid hv(held.world,held.valid_services);odh::ObjectPositionChange hc(held.world,hv,held.services);
    result=hc.apply_existing(held.request());require(result.status==Status::blocked&&detached_during_check&&held.world.find(1)->attachment&&held.world.find(1)->attachment->container==2&&held.world.find(1)->attachment->slot==1&&held.world.find(2)->slots[1]==1,"failed held placement restores exact original slot before PositionChange");
    require(std::find(held.events.begin(),held.events.end(),"restore-slot")!=held.events.end()&&std::find(held.events.begin(),held.events.end(),"wall-clear")==held.events.end()&&std::find(held.events.begin(),held.events.end(),"dynamic")==held.events.end(),"held PrePositionChange skips old walls; restored contained root skips floor trees");

    Fixture tree;tree.expected_room_change=false;
    require(tree.world.create(3,odh::EntityKind::object,{0,0,2})&&tree.world.define_type(3,0x1003),"create ordinary grandchild");
    for(std::int16_t index:{1,29,40,51,59,63})require(tree.world.declare_object_word(3,index,0),"grandchild source fields");
    require(tree.world.define_entry_points(3,std::vector<odh::EntryPoint>(27))&&tree.world.set_position(1,{32,32,1,0})&&tree.world.set_position(2,{32,32,1,4})&&tree.world.set_position(3,{32,32,1,6}),"source tree poses");
    require(tree.world.place_in_slot(1,0,2)&&tree.world.place_in_slot(2,0,3),"physical two-level source tree");
    tree.services.admit_single_ordinary=[](auto)->std::optional<bool>{return true;};
    std::vector<std::string> order;
    tree.services.unregister_position=[&](auto id,bool change){require(!change,"tree same-room move");order.push_back("u"+std::to_string(id.id));
        if(id.id==2)require(tree.world.find(2)->position->x==32&&tree.world.find(3)->position->x==32,"child not moved before its own unregister");
        if(id.id==3)require(tree.world.find(2)->position->x==24&&tree.world.find(3)->position->x==32,"grandchild not moved prematurely with parent");return true;};
    tree.services.register_position=[&](auto id,std::uint16_t,bool){order.push_back("r"+std::to_string(id.id));require(tree.world.find(id.id)->position->x==24,"registered node physical position updated");return true;};
    odh::ObjectPositionValid tv(tree.world,tree.valid_services);odh::ObjectPositionChange tc(tree.world,tv,tree.services);
    result=tc.apply_existing(tree.request());require(result.status==Status::success&&order==std::vector<std::string>{"u1","u2","u3","r3","r2","r1"},"source recursive unregister/update and deepest-first registration order");
    require(tree.world.find(2)->position->direction==4&&tree.world.find(3)->position->direction==6&&tree.world.find(2)->attachment->positioned&&tree.world.find(3)->attachment->positioned,"physical refresh preserves individual child headings and reciprocal identity");
    auto oow=tree.request();oow.candidate={-32768,-32768,1,0};tree.expected_room_change=true;
    order.clear();tree.services.unregister_position=[&](auto id,bool change){require(change,"OOW changes room");order.push_back("u"+std::to_string(id.id));return true;};
    tree.services.register_position=[&](auto id,std::uint16_t room,bool){require(room==0&&!tree.world.find(id.id)->position,"OOW register has source null pose");order.push_back("r"+std::to_string(id.id));return true;};
    odh::ObjectPositionChange out_service(tree.world,tv,tree.services);
    result=out_service.apply_existing(oow);require(result.status==Status::success&&order==std::vector<std::string>{"u1","u2","u3","r3","r2","r1"}&&!tree.world.find(1)->position&&!tree.world.find(2)->position&&!tree.world.find(3)->position,"OOW uses identical per-node source order without early descendant propagation");
}
void clean_old_transfer_lifecycle(){
    Fixture f;require(f.world.set_position(1,{32,32,1,0}),"source item starts on floor");
    f.services.lifecycle_phase=[](auto,auto,const std::function<bool()>&)->bool{
        throw std::runtime_error("public slot lifecycle must not wrap its outer guard again");
    };
    odh::ObjectPositionValid valid(f.world,f.valid_services);odh::ObjectPositionChange change(f.world,valid,f.services);
    require(change.pre_position_change({1,f.world.find(1)->generation},true,[&]{return f.authorized;}),"real cleanOld performs source PrePositionChange");
    require(f.events==std::vector<std::string>{"admit","unregister","wall-clear"},"source transfer removes floor wall use before slot publication");
    require(f.world.attach(2,1,1)&&f.world.refresh_contained_position(1),"caller publishes actual reciprocal slot and only item XYZ");
    require(change.position_change({1,f.world.find(1)->generation},true,[&]{return f.authorized;}),"actual contained PositionChange completes");
    require(f.events==std::vector<std::string>{"admit","unregister","wall-clear","admit","room","register"}&&f.world.find(1)->object_words.at(0)==0,"contained transfer registers Room without floor EP8/EP9");
    f.events.clear();f.authorized=false;
    require(!change.pre_position_change({1,f.world.find(1)->generation},true,[&]{return f.authorized;})&&f.events.empty()&&f.world.find(2)->slots[1]==1,"expired authority cannot detach transferred item");
    Fixture avatar;require(avatar.world.create(3,odh::EntityKind::avatar,{0,0,2})&&avatar.world.define_type(3,0x1003)&&
        avatar.world.declare_object_word(3,29,0)&&avatar.world.define_entry_points(3,std::vector<odh::EntryPoint>(27))&&avatar.world.set_position(3,{32,32,1,4}),"actual avatar item source facts");
    odh::ObjectPositionValid av(avatar.world,avatar.valid_services);odh::ObjectPositionChange ac(avatar.world,av,avatar.services);
    require(ac.pre_position_change({3,avatar.world.find(3)->generation},true,[]{return true;}),"avatar PrePositionChange uses registry without ordinary-object walls");
    require(avatar.world.attach(2,1,3)&&avatar.world.refresh_contained_position(3)&&ac.position_change({3,avatar.world.find(3)->generation},true,[]{return true;}),"contained avatar source PositionChange completes");
    require(std::find(avatar.events.begin(),avatar.events.end(),"wall-clear")==avatar.events.end()&&std::find(avatar.events.begin(),avatar.events.end(),"ep9")==avatar.events.end(),"contained avatar has no object-wall or placement-tree effects");
}
void loaded_position_lifecycle(){
    Fixture f;require(f.world.set_position(1,{32,32,1,0}),"actual imported root pose");
    f.services.lifecycle_phase=[](auto,auto,const std::function<bool()>&)->bool{
        throw std::runtime_error("loaded replay must not invoke apply-only phase hooks");
    };
    f.services.admit_single_ordinary=[&](auto)->std::optional<bool>{throw std::runtime_error("load must not use ordinary move admission");};
    f.services.admit_loaded_entity=[&](auto id)->std::optional<bool>{f.events.push_back("load"+std::to_string(id.id));return true;};
    f.word(1,40,static_cast<std::int16_t>(0x8000));
    f.services.architectural_wall_style=[&](auto id){require(id.id==1,"actual architectural root");f.events.push_back("style");return true;};
    f.wall_effect=[&]{f.word(1,59,8);};
    odh::ObjectPositionValid valid(f.world,f.valid_services);odh::ObjectPositionChange change(f.world,valid,f.services);
    const odh::ObjectPositionIdentity root{1,f.world.find(1)->generation};
    require(change.loaded_position_change(root,[&]{return f.authorized;}),"source load accepts general loaded admission without PositionValid");
    require(f.events==std::vector<std::string>{"load1","room","register","style","wall","support","dynamic"},
        "load preserves architectural style then wall EP6/category8/EP8 while suppressing only base EP9");
    require(f.world.find(1)->position==std::optional<odh::EntityPosition>{{32,32,1,0}}&&f.world.find(1)->object_words.at(0)==0,
        "load neither chooses new pose nor runs placement EP9");
    f.events.clear();f.authorized=false;
    require(!change.loaded_position_change(root,[&]{return f.authorized;})&&f.events.empty(),"expired source load capability has no effects");
    f.authorized=true;f.services.admit_loaded_entity={};odh::ObjectPositionChange no_admission(f.world,valid,f.services);
    require(!no_admission.loaded_position_change(root,[]{return true;}),"ordinary admission cannot stand in for unknown loaded identity");

    Fixture unavailable;require(unavailable.world.set_position(1,{32,32,1,0}),"load partial-effect source");
    unavailable.services.admit_loaded_entity=[](auto)->std::optional<bool>{return true;};
    unavailable.services.dynamic_multitile={};unavailable.wall_effect=[&]{unavailable.word(1,0,77);};
    odh::ObjectPositionValid uv(unavailable.world,unavailable.valid_services);odh::ObjectPositionChange partial(unavailable.world,uv,unavailable.services);
    require(!partial.loaded_position_change({1,unavailable.world.find(1)->generation},[]{return true;})&&
        unavailable.world.find(1)->object_words.at(0)==77&&unavailable.events.back()=="wall",
        "missing load EP8 retains real earlier effects without manufacturing success or EP9");

    Fixture outside;outside.services.admit_loaded_entity=[](auto)->std::optional<bool>{return true;};
    odh::ObjectPositionValid ov(outside.world,outside.valid_services);odh::ObjectPositionChange oc(outside.world,ov,outside.services);
    require(oc.loaded_position_change({1,outside.world.find(1)->generation},[]{return true;})&&outside.events==std::vector<std::string>{"register"},
        "OOW load registers source Room0 then returns before walls/support/EP8/EP9");

    Fixture tree;require(tree.world.create(3,odh::EntityKind::avatar,{0,0,2})&&tree.world.define_type(3,0x1003)&&tree.world.declare_object_word(3,29,0)&&
        tree.world.define_entry_points(3,std::vector<odh::EntryPoint>(27))&&tree.world.set_position(3,{120,120,1,4})&&
        tree.world.set_position(1,{32,32,1,2})&&tree.world.set_position(2,{48,48,1,6})&&
        tree.world.attach(3,0,1)&&tree.world.attach(1,0,2),"loaded avatar with nested actual contained objects");
    std::vector<std::string> order;
    tree.services.admit_loaded_entity=[&](auto id)->std::optional<bool>{order.push_back("a"+std::to_string(id.id));return true;};
    tree.services.room_at=[&](const auto& pose)->std::optional<std::uint16_t>{require(pose.x==120,"load copies parent pose before child room lookup");order.push_back("room");return 3;};
    tree.services.unregister_position=[&](auto id,bool room_change){require(room_change,"load uses source roomChange=true");order.push_back("u"+std::to_string(id.id));return true;};
    tree.services.register_position=[&](auto id,std::uint16_t room,bool room_change){require(room_change&&room==3&&tree.world.find(id.id)->object_words.at(29)==2,
        "load Room29 precedes actual registration");order.push_back("r"+std::to_string(id.id));return true;};
    odh::ObjectPositionValid tv(tree.world,tree.valid_services);odh::ObjectPositionChange tc(tree.world,tv,tree.services);
    require(tc.loaded_position_change({3,tree.world.find(3)->generation},[]{return true;})&&
        order==std::vector<std::string>{"a3","room","u1","a1","u2","a2","room","r2","room","r1","r3"},
        "load avatar room comes first, then slot-ordered children and deepest-first registration without root unregister");
    require(tree.world.find(1)->position->direction==2&&tree.world.find(2)->position->direction==6&&
        !tc.loaded_position_change({1,tree.world.find(1)->generation},[]{return true;}),"child headings survive and contained roots must be skipped by load orchestrator");

    // Source GameObject propagates children before its GhostImage return;
    // Avatar returns before child propagation. Neither ghost registers itself.
    Fixture ghost;require(ghost.world.set_position(1,{32,32,1,0})&&ghost.world.attach(1,0,2)&&ghost.world.set_ui_ghost(1,true),"ghost object subtree");
    std::vector<std::string> ghosts;
    ghost.services.admit_loaded_entity=[](auto)->std::optional<bool>{return true;};
    ghost.services.unregister_position=[&](auto id,bool){ghosts.push_back("u"+std::to_string(id.id));return true;};
    ghost.services.register_position=[&](auto id,auto,bool){ghosts.push_back("r"+std::to_string(id.id));return true;};
    odh::ObjectPositionValid gv(ghost.world,ghost.valid_services);odh::ObjectPositionChange gc(ghost.world,gv,ghost.services);
    require(gc.loaded_position_change({1,ghost.world.find(1)->generation},[]{return true;})&&ghosts==std::vector<std::string>{"u2","r2"}&&ghost.world.find(2)->position->x==32,
        "ghost object updates contained child but skips own registration");
    ghosts.clear();require(ghost.world.set_ui_ghost(2,true)&&gc.loaded_position_change({1,ghost.world.find(1)->generation},[]{return true;})&&ghosts.empty(),
        "object skips unregistering ghost child while retaining source ghost recursion");

    Fixture mutation;require(mutation.world.create(3,odh::EntityKind::object)&&mutation.world.declare_object_word(3,29,0)&&
        mutation.world.set_position(1,{32,32,1,0})&&mutation.world.set_position(3,{64,64,1,0})&&mutation.world.attach(1,0,2),"loaded mutable slot sequence");
    std::vector<std::int16_t> registered;
    mutation.services.admit_loaded_entity=[](auto)->std::optional<bool>{return true;};
    mutation.services.unregister_position=[](auto,bool){return true;};
    mutation.services.register_position=[&](auto id,auto,bool){registered.push_back(id.id);if(id.id==2)require(mutation.world.attach(1,1,3),"prior real callback adds later source slot");return true;};
    odh::ObjectPositionValid mv(mutation.world,mutation.valid_services);odh::ObjectPositionChange mc(mutation.world,mv,mutation.services);
    require(mc.loaded_position_change({1,mutation.world.find(1)->generation},[]{return true;})&&registered==std::vector<std::int16_t>{2,3,1}&&mutation.world.find(3)->position->x==32,
        "load reads later slots after earlier callbacks rather than freezing an obsolete tree");

    odh::WorldLimits limits;limits.entities=4097;limits.words=100000;limits.bank_words=8192;limits.room_registration_events=8192;
    odh::World large(limits);require(large.create(1,odh::EntityKind::object,{0,0,4096})&&large.set_ui_ghost(1,true),"bounded load root with source slots");
    for(std::int16_t id=2;id<=4097;++id)require(large.create(id,odh::EntityKind::object)&&large.declare_object_word(id,1,0)&&
        large.declare_object_word(id,29,0)&&large.attach(1,std::size_t(id-2),id),"bounded actual loaded child");
    std::size_t registered_count=0;odh::ObjectPositionChangeServices bounded;
    bounded.admit_loaded_entity=[](auto)->std::optional<bool>{return true;};bounded.room_at=[](const auto&)->std::optional<std::uint16_t>{return 0;};
    bounded.unregister_position=[](auto,bool){return true;};bounded.register_position=[&](auto,auto,bool){++registered_count;return true;};
    odh::ObjectPositionValid bv(large,{});odh::ObjectPositionChange bc(large,bv,bounded);
    require(!bc.loaded_position_change({1,large.find(1)->generation},[]{return true;})&&registered_count==4095,
        "4096-call load budget retains completed earlier callbacks and refuses an oversized replay");
}
void capability_and_effects(){
    Fixture f;f.authorized=false;require(f.run().status==Status::unsupported&&f.events.empty(),"no effects without real factory authority");
    Fixture moved;moved.services.admit_single_ordinary=[&](auto)->std::optional<bool>{require(moved.world.set_position(1,{32,32,1,0}),"effectful profile resolver moved object");return true;};
    require(moved.run().status==Status::unsupported&&moved.world.find(1)->position&&moved.events.empty(),"profile callback pose mutation is retained but cannot enter new-OOW lifecycle");
    Fixture no;no.admitted=false;require(no.run().status==Status::unsupported&&no.events==std::vector<std::string>{"admit"},"unknown ordinary profile cannot fabricate a successful placement");
    Fixture positioned;require(positioned.world.set_position(1,{32,32,1,0})&&positioned.run().status==Status::unsupported&&positioned.events.empty(),"positioned-object lifecycle needs broader source implementation");
    Fixture missing;missing.services.dynamic_multitile={};auto partial=missing.run();require(partial.status==Status::unsupported&&partial.position_published&&
        missing.world.find(1)->position&&missing.world.find(1)->object_words.at(29)==2&&missing.world.find(1)->object_words.at(0)==0,"missing live EP8 capability retains completed position/registration but never fakes EP9");
    Fixture throwing;throwing.wall_effect=[&]{throwing.word(1,0,77);throw std::runtime_error("effectful EP6 failure");};auto effect=throwing.run();
    require(effect.status==Status::unsupported&&effect.effects_started&&effect.position_published&&throwing.world.find(1)->object_words.at(0)==77,"throwing source callback preserves completed effects without clone rollback");
    Fixture recycled;const auto old=recycled.world.find(1)->generation;recycled.register_effect=[&]{require(recycled.world.remove(1)&&recycled.world.create(1,odh::EntityKind::object),"replace object inside live registration callback");};
    require(recycled.run().status==Status::unsupported&&recycled.world.find(1)->generation!=old&&recycled.events.back()=="register","generation replacement stops remaining lifecycle without touching new object");
    for(const auto architectural: {0x4000U,0x8000U}){
        Fixture changed;
        changed.valid_services.intersections=[](auto,const auto&,auto)->std::optional<odh::ObjectPositionIntersections>{
            odh::ObjectPositionIntersections result;result.footprint_present=true;result.query.status=odh::RoomInfoObstacleIndexStatus::complete;
            result.query.intersections.push_back({2,3,{0,0,8,8},odh::RoomInfoObstacleCollection::static_set});return result;};
        changed.valid_services.entry_point5=[&](const auto&)->std::optional<bool>{changed.events.push_back("ep5");changed.word(1,40,static_cast<std::int16_t>(architectural));changed.word(1,0,44);return true;};
        const auto outcome=changed.run();require(outcome.status==Status::unsupported&&outcome.effects_started&&!outcome.position_published&&
            !changed.world.find(1)->position&&static_cast<std::uint16_t>(changed.world.find(1)->object_words.at(40))==architectural&&changed.world.find(1)->object_words.at(0)==44&&
            changed.events.back()=="admit","EP5 door/window profile change preserves its effects but cannot publish through ordinary lifecycle");
    }
    Fixture contents;
    contents.valid_services.intersections=[](auto,const auto&,auto)->std::optional<odh::ObjectPositionIntersections>{
        odh::ObjectPositionIntersections result;result.footprint_present=true;result.query.status=odh::RoomInfoObstacleIndexStatus::complete;
        result.query.intersections.push_back({2,3,{0,0,8,8},odh::RoomInfoObstacleCollection::static_set});return result;};
    contents.valid_services.entry_point5=[&](const auto&)->std::optional<bool>{auto move=contents.world.prepare_move_to_slot(1,0,2);
        require(move&&contents.world.commit_move_to_slot(*move),"EP5 creates real contained relation");return true;};
    const auto contained=contents.run();require(contained.status==Status::unsupported&&!contained.position_published&&!contents.world.find(1)->position&&contents.world.find(1)->slots[0]==2,
        "EP5-created contents survive while invalidating the empty-single profile");
    Fixture reenter;odh::ObjectPositionValid valid(reenter.world,reenter.valid_services);odh::ObjectPositionChange service(reenter.world,valid,reenter.services);
    reenter.wall_effect=[&]{require(service.apply(reenter.request()).status==Status::unsupported,"same-service reentry cannot publish twice");};
    require(service.apply(reenter.request()).status==Status::success,"rejected callback reentry leaves legitimate outer operation intact");
}
void scalar_publication_hook(){
    Fixture f;unsigned publications=0;
    f.services.publish_position=[&](auto id,const std::optional<odh::EntityPosition>& position,std::uint8_t direction){
        ++publications;require(id.id==1&&id.generation==f.world.find(1)->generation&&!f.world.find(1)->position&&
            position==std::optional<odh::EntityPosition>{{24,24,1,2}}&&direction==2&&f.events.back()=="admit",
            "source publisher receives exact current identity/quantized pose only after Pre and real PositionValid");
        require(f.world.find(1)->object_words.at(1)==0&&f.world.find(1)->object_words.at(29)==0,
            "hook owns direction publication before scalar placement, while Post has not run");
        f.word(1,1,direction);f.events.push_back("publish");
        return f.world.place_individual_on_floor(id.id,position,direction);
    };
    const auto result=f.run();require(result.status==Status::success&&result.position_published&&publications==1&&
        f.world.find(1)->object_words.at(29)==2&&std::find(f.events.begin(),f.events.end(),"publish")<std::find(f.events.begin(),f.events.end(),"register"),
        "hook executes fixed direction/scalar publication once before source recursive Post");
    Fixture forged;forged.services.publish_position=[](auto,const auto&,auto){return true;};
    require(forged.run().status==Status::unsupported&&!forged.world.find(1)->position,
        "publisher return true cannot manufacture physical pose or direction");
    Fixture wrong;wrong.services.publish_position=[&](auto id,const auto&,auto direction){wrong.word(1,1,direction);return wrong.world.place_individual_on_floor(id.id,odh::EntityPosition{32,32,1,direction},direction);};
    require(wrong.run().status==Status::unsupported&&wrong.world.find(1)->position->x==32,
        "wrong physical destination is unsupported while retaining actual effects");
    Fixture declined;declined.services.publish_position=[](auto,const auto&,auto){return false;};
    const auto rejected=declined.run();require(rejected.status==Status::unsupported&&!rejected.position_published&&rejected.effects_started&&!declined.world.find(1)->position,
        "failed source publisher preserves Pre effects without pretending position publication");
}
void lifecycle_failure_diagnostics(){
    Fixture f;
    f.services.register_position=[](auto,std::uint16_t,bool){return false;};
    odh::ObjectPositionValid valid(f.world,f.valid_services);
    odh::ObjectPositionChange change(f.world,valid,f.services);
    const odh::ObjectPositionIdentity root{1,f.world.find(1)->generation};
    require(!change.position_change(root,true,[&]{return f.authorized;})&&
        change.diagnostic().find("Post register root=1")!=std::string::npos&&
        change.diagnostic().find("entity=1")!=std::string::npos,
        "boolean registration failure exposes actual stage and identity without changing effects");
    f.authorized=false;
    require(!change.position_change(root,true,[&]{return f.authorized;})&&
        change.diagnostic().find("authority root=1")!=std::string::npos,
        "expired authority is distinguishable from registration callback failure");
    Fixture child;
    require(child.world.set_position(1,{24,24,1,0})&&child.world.attach(1,0,2)&&child.world.refresh_contained_position(2),
        "actual carried child diagnostic fixture");
    child.services.unregister_position=[](auto,bool){return false;};
    odh::ObjectPositionValid cv(child.world,child.valid_services);
    odh::ObjectPositionChange cc(child.world,cv,child.services);
    require(!cc.position_change({1,child.world.find(1)->generation},true,[]{return true;})&&
        cc.diagnostic().find("child unregister root=1")!=std::string::npos&&
        cc.diagnostic().find("entity=2")!=std::string::npos&&child.world.find(2)->attachment,
        "carried child failure records actual child while preserving reciprocal attachment");
}
void dynamic_split_invocation_authority(){
    for(unsigned invalid=0;invalid<4;++invalid){GroupFixture f;f.old_positions();unsigned splits=0;
        const auto before=f.world.snapshot();const auto member_count=f.group.members.size();
        f.services.split_dynamic_group=[&](auto)->std::optional<std::vector<odh::ObjectPositionIdentity>>{++splits;f.group.members.clear();return std::vector<odh::ObjectPositionIdentity>{};};
        auto request=f.request();if(invalid==0)request.source_still_matches={};else if(invalid==1)f.authorized=false;
        else if(invalid==2)request.direction=8;else request.candidate.x=32768;
        const auto result=f.run(true,request);require(result.status==Status::unsupported&&splits==0&&f.group.members.size()==member_count&&f.world.snapshot().spatial_revision==before.spatial_revision,
            "unauthorized or malformed SetPosition cannot mutate the association before placement preflight");
    }
    GroupFixture f;f.old_positions();unsigned splits=0;odh::ObjectPositionValid placement(f.world,f.checks);
    odh::ObjectPositionChange* service=nullptr;f.services.split_dynamic_group=[&](auto)->std::optional<std::vector<odh::ObjectPositionIdentity>>{++splits;
        require(service&&service->apply_existing(f.request()).status==Status::unsupported,"split callback cannot reenter the same SetPosition owner");return std::vector<odh::ObjectPositionIdentity>{};};
    odh::ObjectPositionChange change(f.world,placement,f.services);service=&change;
    require(change.apply_existing(f.request()).status==Status::success&&splits==1,"rejected split callback reentry preserves legitimate outer placement");
}
void prepare_readonly_native_group(GroupFixture& f){
    f.group.native_head=odh::WorldEntityReference{f.group.members.front().id,f.group.members.front().generation};
    for(std::size_t n=0;n<f.group.members.size();++n){auto& member=f.group.members[n];
        const odh::NativeObjectGroupOffset fields{member.offset_x/16,member.offset_y/16,member.offset_level};
        member.native=odh::NativeObjectGroupMember{fields,fields,n+1<f.group.members.size()?
            odh::WorldEntityReference{f.group.members[n+1].id,f.group.members[n+1].generation}:odh::WorldEntityReference{}};}
    // This fixture tests stage separation only, never a simulated rotation.
    // Rotating through this read-only physical owner explicitly declines.
    f.services.begin_native_rotation=[](auto,auto)->std::optional<odh::NativeObjectGroupRotation>{return {};};
    f.services.rollback_native_rotation=[](const auto&){return false;};
    f.services.native_group_head=[&f](auto ref){for(const auto& m:f.group.members)if(m.id==ref.id&&m.generation==ref.generation)
        return odh::NativeObjectGroupLink{odh::NativeObjectGroupLinkStatus::member,*f.group.native_head};return odh::NativeObjectGroupLink{};};
    f.services.native_group_next=[&f](auto ref){for(const auto& m:f.group.members)if(m.id==ref.id&&m.generation==ref.generation)
        return odh::NativeObjectGroupLink{m.native->next.id?odh::NativeObjectGroupLinkStatus::member:odh::NativeObjectGroupLinkStatus::end,m.native->next};return odh::NativeObjectGroupLink{};};
    f.services.native_current_offset=[&f](auto ref)->std::optional<odh::NativeObjectGroupOffset>{
        for(const auto& m:f.group.members)if(m.id==ref.id&&m.generation==ref.generation)return m.native->current;return {};};
}
void shared_native_stages(){
    GroupFixture f;f.old_positions();prepare_readonly_native_group(f);f.blocked=5;
    f.check_effect=[&](std::int16_t id){if(id==5)f.word(5,0,std::int16_t(f.world.find(5)->object_words.at(0)+1));};
    odh::ObjectPositionValid placement(f.world,f.checks);odh::ObjectPositionChange owner(f.world,placement,f.services);
    const auto result=owner.with_native_position_stages(f.request(0),false,false,[&](auto& stages){
        require(stages.pre(),"actual stage Pre uses existing positioning lifecycle");
        require(stages.check54().status==odh::ObjectPositionValidStatus::blocked&&f.world.find(5)->object_words.at(0)==1,
            "first actual placement check retains authored World effects and blocks");
        require(std::none_of(f.events.begin(),f.events.end(),[](const auto& e){return e.rfind("set",0)==0;}),"first check never publishes or repeats Pre");
        f.blocked=0;
        require(stages.check54().status==odh::ObjectPositionValidStatus::success&&f.world.find(5)->object_words.at(0)==2,
            "second check executes real placement pipeline again, not cached first result");
        return stages.publish58()?Status::success:Status::unsupported;
    });
    require(result.status==Status::success&&result.effects_started&&result.position_published&&
        std::count(f.events.begin(),f.events.end(),"pre5")==1&&std::count(f.events.begin(),f.events.end(),"check5")==2&&
        std::count(f.events.begin(),f.events.end(),"set5")==1&&std::count(f.events.begin(),f.events.end(),"post5")==1,
        "shared stages perform one actual removal/publication/Post around two actual checks");
    GroupFixture missing;missing.old_positions();prepare_readonly_native_group(missing);bool driver_called=false;
    odh::ObjectPositionValid check(missing.world,missing.checks);odh::ObjectPositionChange guarded(missing.world,check,missing.services);
    const auto before=missing.world.snapshot();const auto denied=guarded.with_native_position_stages(missing.request(0),true,false,[&](auto&){driver_called=true;return Status::success;});
    require(denied.status==Status::unsupported&&!denied.effects_started&&!driver_called&&missing.events.empty()&&
        missing.world.snapshot().spatial_revision==before.spatial_revision,"original class hook absence rejects before any removal or driver effects");
    for(bool only_pre:{false,true}){
        GroupFixture forged;forged.old_positions();prepare_readonly_native_group(forged);
        odh::ObjectPositionValid validator(forged.world,forged.checks);odh::ObjectPositionChange service(forged.world,validator,forged.services);
        const auto fake=service.with_native_position_stages(forged.request(0),false,false,[&](auto& stages){if(only_pre)require(stages.pre(),"adversarial actual Pre completes");return Status::success;});
        require(fake.status==Status::unsupported&&!fake.position_published&&fake.effects_started==only_pre,
            "driver success without stages or with Pre alone cannot certify positioning");
    }
    for(bool exceed_budget:{false,true}){
        GroupFixture stale;stale.old_positions();prepare_readonly_native_group(stale);
        odh::ObjectPositionValid validator(stale.world,stale.checks);odh::ObjectPositionChange service(stale.world,validator,stale.services);
        const auto rejected=service.with_native_position_stages(stale.request(0),false,false,[&](auto& stages){
            require(stages.pre()&&stages.check54().status==odh::ObjectPositionValidStatus::success,"actual first stage check establishes publication certificate");
            if(exceed_budget)require(stages.check54().status==odh::ObjectPositionValidStatus::success,"actual second check within bounded stage budget");
            else stale.authorized=false;
            require(stages.check54().status==odh::ObjectPositionValidStatus::unsupported,"later check cannot complete with missing authority or exhausted budget");
            stale.authorized=true;
            require(!stages.publish58(),"failed newer check revokes earlier certificate even after authority resumes");
            return Status::success;
        });
        require(rejected.status==Status::unsupported&&!rejected.position_published&&
            std::count(stale.events.begin(),stale.events.end(),"check5")==std::int64_t(exceed_budget?2:1),
            "a rejected fresh check never reuses old successful placement proof");
    }
    auto high=missing.request(0);high.candidate.level=3;
    const auto blocked=guarded.with_native_position_stages(high,false,false,[](auto& stages){return stages.pre()?Status::success:Status::unsupported;});
    require(blocked.status==Status::blocked&&blocked.placement.placement_error==18&&!blocked.effects_started&&missing.events.empty(),
        "absolute story refusal remains blocked18 before effects after extraction");
}
void shared_native_rotation_invalidates_checks(){
    for(bool partial_failure:{false,true}){
    GroupFixture f;f.old_positions();odh::SessionObjectTradeLedger ledger;ledger.version=2;ledger.revision=1;
    odh::SessionObjectValueGroup price;price.group_id=price.revision=1;price.multitile=true;price.initial_price=100;
    price.base=odh::ObjectTradeIdentity{f.group.base_id,f.world.find(f.group.base_id)->generation};
    price.price_origin={odh::SessionObjectPriceSource::constructed_catalog,0x9000,"fixture","part.iff",128,{},{}};
    for(const auto& member:f.group.members){odh::SessionObjectValueMember value;
        value.object={member.id,member.generation,f.world.find(member.id)->definition->guid,"fixture","part.iff",128,0x9000};
        value.offset_x=member.offset_x;value.offset_y=member.offset_y;value.offset_level=member.offset_level;
        value.lead_object=std::uint16_t(member.id==f.group.base_id);price.members.push_back(value);}
    ledger.groups={price};odh::ObjectValueRegistry values(f.world,ledger);require(values.bind(),"actual staged rotation value owner");
    odh::ObjectGroupSnapshot snapshot{{f.group}};odh::DynamicObjectGroupServices authority;authority.authority=[&]{return f.authorized;};
    authority.origin=[&](auto id)->std::optional<odh::ObjectGroupMemberOrigin>{for(const auto& m:ledger.groups.front().members)
        if(m.object.id==id.id&&m.object.generation==id.generation){const auto& o=m.object;return odh::ObjectGroupMemberOrigin{
            odh::ObjectGroupMemberSource::native_created,o.guid,o.master_guid,o.source,o.asset,o.definition_chunk_id};}return {};};
    authority.publish_native_direction=[&](auto ref,auto heading){return f.world.publish_native_object_heading(ref,heading);};
    odh::DynamicObjectGroups groups(f.world,values,snapshot,std::move(authority));
    std::vector<odh::NativeObjectGroupSourceMember> rows;for(const auto& m:f.group.members){const odh::NativeObjectGroupOffset o{m.offset_x/16,m.offset_y/16,m.offset_level};rows.push_back({{m.id,m.generation},o,o});}
    const auto head=rows.front().reference;require(groups.bind_native_created_group({head.id,head.generation},head,rows),"actual native constructor admission for stage epoch test");
    f.services.group=[&](auto)->std::optional<odh::ObjectGroup>{return snapshot.groups.front();};
    f.services.authenticate_live_group=[&](const auto&){try{odh::validate_object_groups(snapshot,f.world.snapshot());return true;}catch(...){return false;}};
    f.services.native_group_head=[&](auto ref){return groups.group_head(ref);};f.services.native_group_next=[&](auto ref){return groups.group_next(ref);};
    f.services.native_current_offset=[&](auto ref){return groups.native_offset(ref);};
    unsigned rotations=0;
    f.services.begin_native_rotation=[&](auto id,auto heading)->std::optional<odh::NativeObjectGroupRotation>{
        const auto token=groups.begin_native_rotation(id,heading);if(++rotations==2&&partial_failure)return {};return token;};
    f.services.rollback_native_rotation=[&](const auto& token){return groups.rollback_native_rotation(token);};
    odh::ObjectPositionValid validator(f.world,f.checks);odh::ObjectPositionChange service(f.world,validator,f.services);
    const auto result=service.with_native_position_stages(f.request(0),false,false,[&](auto& stages){
        require(stages.pre()&&stages.rotate_to(2).has_value(),"actual native prospective rotation after Pre");
        require(stages.check54().status==odh::ObjectPositionValidStatus::success,"actual check certifies heading2");
        require(stages.rotate_to(4).has_value()!=partial_failure&&!stages.publish58(),"later actual rotation or partial callback failure invalidates earlier publication certification");
        return Status::success;
    });
    require(result.status==Status::unsupported&&result.effects_started&&!result.position_published&&f.world.find(f.group.base_id)->object_words.at(1)==4,
        "unchecked second rotation cannot be published or falsely reported successful; completed heading effects remain");
    }
}
void native_rotation_requires_actual_owner(){
    GroupFixture f;f.old_positions();const auto before=f.world.snapshot();
    f.group.native_head=odh::WorldEntityReference{f.group.members.front().id,f.group.members.front().generation};
    const auto result=f.run(true);
    require(result.status==Status::unsupported&&!result.effects_started&&f.events.empty()&&
        f.world.snapshot().spatial_revision==before.spatial_revision,
        "a native metadata tag cannot authorize rotation without the real group heading/offset/list owner");
}

void constructor_capture_receipts(){
    for(unsigned mode=0;mode<3;++mode){
        Fixture f;const auto generation=f.world.find(1)->generation;
        unsigned admits=0;f.services.admit_single_ordinary=[&](auto)->std::optional<bool>{
            ++admits;
            if(mode==0)return false;
            if(mode==1)return {};
            f.authorized=false;return true;
        };
        const auto result=f.run();
        const auto stage=mode==0?"ordinary admission rejected":mode==1?"ordinary admission unknown":"authority";
        require(result.status==Status::unsupported&&!result.effects_started&&!result.position_published&&admits==1,
            "constructor receipt preserves admission invocation count and refusal before effects");
        require(result.diagnostic.find(stage)!=std::string::npos&&result.diagnostic.find("root=1")!=std::string::npos&&
            result.diagnostic.find("root_generation="+std::to_string(generation))!=std::string::npos&&
            !f.world.find(1)->position,
            "constructor failure identifies exact failed capture stage and generation without publication");
    }
    {
        Fixture f;const auto generation=f.world.find(1)->generation;
        f.services.admit_single_ordinary=[&](auto)->std::optional<bool>{
            require(f.world.remove(1)&&f.world.create(1,odh::EntityKind::object),"actual body replacement during admission");return true;};
        const auto result=f.run();
        require(result.status==Status::unsupported&&!result.effects_started&&f.world.find(1)->generation!=generation&&
            result.diagnostic.find("live identity")!=std::string::npos&&
            result.diagnostic.find("actual_generation="+std::to_string(f.world.find(1)->generation))!=std::string::npos,
            "replacement body is rejected and receipt preserves both exact generations");
    }
    Fixture f;unsigned admits=0;
    f.services.admit_single_ordinary=[&](auto)->std::optional<bool>{++admits;
        require(f.world.set_position(1,{40,40,1,0}),"actual admission callback changes constructor pose");return true;};
    const auto result=f.run();
    require(result.status==Status::unsupported&&!result.effects_started&&admits==1&&f.world.find(1)->position->x==40&&
        result.diagnostic.find("constructor pose/attachment changed during capture")!=std::string::npos,
        "pose failure is distinct from tree failure and preserves callback partial effects");
}

}
void architectural_phase_order(){
    for(const bool blocked:{false,true}){
        Fixture f;f.blocked=blocked;f.word(1,40,std::int16_t(-32768));
        f.services.admit_single_ordinary=[&](auto)->std::optional<bool>{return f.admitted;};
        f.services.architectural_wall_style_change=[&](auto,bool set){f.events.push_back(set?"style-set":"style-clear");return true;};
        require(f.world.set_position(1,{8,8,1,0}),"style Pre fixture begins at an actual existing source pose");
        f.expected_room_change=false; // Both source and candidate resolve real fixture room3.
        odh::ObjectPositionValid valid(f.world,f.valid_services);odh::ObjectPositionChange change(f.world,valid,f.services);
        const auto result=change.apply_existing(f.request());
        require(result.status==(blocked?odh::ObjectPositionChangeStatus::blocked:odh::ObjectPositionChangeStatus::success),"architectural source phase shares actual success/refusal lifecycle");
        const auto at=[&](const char* name){return std::find(f.events.begin(),f.events.end(),name)-f.events.begin();};
        require(at("style-clear")<at("wall-clear")&&at("style-set")<at("wall"),"source style clear/apply each precedes SetWallUse, including restoration after refusal");
    }
    Fixture absent;absent.word(1,40,std::int16_t(-32768));
    absent.services.admit_single_ordinary=[](auto)->std::optional<bool>{return true;};
    require(absent.run().status==odh::ObjectPositionChangeStatus::unsupported,"admission cannot invent missing architectural style consumer");
}
int main(){try{architectural_phase_order();constructor_capture_receipts();shared_native_rotation_invalidates_checks();shared_native_stages();native_rotation_requires_actual_owner();dynamic_split_invocation_authority();imported_group_position();physical_group_position();dynamic_group_support_footprint();retained_parent_source_pre();lifecycle_phase_boundaries();source_order();support_branch();existing_move_and_held_tree();clean_old_transfer_lifecycle();loaded_position_lifecycle();capability_and_effects();scalar_publication_hook();lifecycle_failure_diagnostics();std::cout<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
