#include "odh/household_spawn.hpp"
#include "odh/formats.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F fn, const char* message) {
    try { fn(); } catch (const odh::FormatError&) { return; }
    throw std::runtime_error(message);
}
}

int main() {
    try {
        odh::World constructor;
        require(constructor.create(1,odh::EntityKind::avatar,{0,101,3,16}),"create unpositioned avatar before constructor banks");
        odh::initialize_avatar_constructor_words(constructor,1);
        odh::VmFrame address;address.context.caller=1;
        const auto* created=constructor.find(1);
        require(created&&!created->position&&created->radian_direction==0&&created->object_words.at(1)==0&&
            created->object_words.at(29)==0&&constructor.read(address,3,1)==0&&constructor.read(address,3,29)==-5,
            "new avatar has source NORTH and zero Room backing while OOW Room reads derived minus five");
        for(const auto word:{7,8,34,40,42,51})
            require(created->object_words.count(static_cast<std::int16_t>(word))==1,
                "avatar constructor supplies every placement and registry scalar prerequisite");
        require(created->object_words.at(78)==0&&created->object_words.at(8)==256&&
            !created->object_words.count(41)&&!created->object_words.count(62),
            "complete native TS1 scalar profile ends at78; group value and derived use count remain external authorities");
        std::vector<std::int16_t> own(80);own[0]=138;own[9]=2;own[18]=123;
        require(odh::avatar_constructor_initial_value(own)==0,
            "complete noncatalog avatar has authenticated zero group price even when OBJD price is nonzero");
        own[4]=1;own[40]=1;
        require(odh::avatar_constructor_initial_value(own)==123,"catalog avatar inherits actual own resource price");
        own[18]=-1;require(odh::avatar_constructor_initial_value(own)==-1,"unsigned price65535 retains exact CurrentValue signed16 bits");
        own[16]=1;require(odh::avatar_constructor_initial_value(own)==0,"disabled entry is absent from source catalog");
        own[16]=0;own[40]=0;own[69]=1;
        require(odh::avatar_constructor_initial_value(own)==-1,"build catalog membership also supplies actual price");
        own[69]=23;require(!odh::avatar_constructor_initial_value(own),"invalid build category cannot claim an authenticated catalog entry");own[69]=1;
        auto short_bank=own;short_bank.resize(69);
        require(!odh::avatar_constructor_initial_value(short_bank),"truncated definition cannot authenticate catalog absence");
        own[9]=0;require(!odh::avatar_constructor_initial_value(own),"avatar constructor cannot consume a different object class");
        odh::World priced;
        require(priced.create(1,odh::EntityKind::avatar,{0,101,3,16}),"create fresh explicitly priced avatar");
        odh::initialize_avatar_constructor_words(priced,1,std::int16_t{123});
        require(priced.find(1)->object_words.at(41)==123,"fresh constructor publishes authenticated group value before script Init");
        odh::World unrelated;
        require(unrelated.create(1,odh::EntityKind::avatar,{0,101,3,16})&&!unrelated.find(1)->object_words.count(78)&&
            !unrelated.find(1)->object_words.count(41),"generic World fixture remains unknown outside explicit source constructor profile");
        require(created->kill_timeout==std::optional<std::int16_t>{-1}&&created->slots==std::vector<std::int16_t>(3,0),
            "new avatar collision lifetime and empty containment are authentic before Pre");
        require(constructor.write(address,3,29,-1)&&constructor.read(address,3,29)==-5&&
            constructor.set_position(1,{16,32,1,0})&&constructor.read(address,3,29)==-1,
            "source PositionChange can publish Room backing independently of the OOW derived getter");
        const auto candidates = odh::household_spawn_candidates({168, 168, 1, 0}, 24, 24);
        require(!candidates.empty(), "mailbox has in-lot arrival candidates");
        require(candidates[0] == odh::EntityPosition{168, 168, 1, 0} &&
                candidates[1] == odh::EntityPosition{168, 168, 1, 2} &&
                candidates[2] == odh::EntityPosition{168, 168, 1, 4} &&
                candidates[3] == odh::EntityPosition{168, 168, 1, 6},
                "mailbox is tried first in the four cardinal directions");
        const std::vector<odh::EntityPosition> first_ring{
            {152,152,1,0},{152,184,1,0},{152,152,1,2},{152,184,1,2},
            {152,152,1,4},{152,184,1,4},{152,152,1,6},{152,184,1,6},
            {168,152,1,0},{168,184,1,0},{168,152,1,2},{168,184,1,2},
            {168,152,1,4},{168,184,1,4},{168,152,1,6},{168,184,1,6},
            {184,152,1,0},{184,184,1,0},{184,152,1,2},{184,184,1,2},
            {184,152,1,4},{184,184,1,4},{184,152,1,6},{184,184,1,6},
            {152,168,1,0},{184,168,1,0},{152,168,1,2},{184,168,1,2},
            {152,168,1,4},{184,168,1,4},{152,168,1,6},{184,168,1,6}};
        require(candidates.size() == 1444,
                "nine full expanding rings produce the expected bounded candidate count");
        for (std::size_t i = 0; i < first_ring.size(); ++i)
            require(candidates[4+i] == first_ring[i],
                    "first square ring matches the source's exact candidate order");
        for (const auto& candidate : candidates)
            require(candidate.x >= 0 && candidate.y >= 0 && candidate.x / 16 < 24 &&
                    candidate.y / 16 < 24 && candidate.level == 1 && candidate.direction <= 6,
                    "candidate stays inside the lot and has a cardinal facing");

        const auto edge = odh::household_spawn_candidates({8, 8, 1, 0}, 2, 2);
        require(edge.size() == 1444,
                "edge mailbox preserves all source attempts, including out-of-bounds positions");
        require(edge[4] == odh::EntityPosition{-8,-8,1,0} &&
                edge[5] == odh::EntityPosition{-8,24,1,0},
                "out-of-bounds ring attempts retain source order");
        require(edge.back() == odh::EntityPosition{152,136,1,6},
                "edge mailbox retains the full ninth ring");

        std::size_t attempts = 0;
        const auto accepted_position = candidates[40];
        const auto first_accepted = odh::first_household_spawn_candidate(
            {168, 168, 1, 0}, 24, 24, [&](const auto& candidate) {
                ++attempts;
                return candidate == accepted_position;
            });
        require(first_accepted == accepted_position && attempts == 41,
                "search stops at the first position accepted by the placement validator");
        attempts = 0;
        require(!odh::first_household_spawn_candidate({168, 168, 1, 0}, 24, 24,
                    [&](const auto&) { ++attempts; return false; }) &&
                attempts == candidates.size(),
                "search reports failure after exhausting all bounded candidates");
        rejects([] { (void)odh::household_spawn_candidates({-1, 8, 1, 0}, 2, 2); },
                "negative mailbox coordinate rejected");
        rejects([] { (void)odh::household_spawn_candidates({8, 8, 3, 0}, 2, 2); },
                "unsupported mailbox floor rejected");
        rejects([] { (void)odh::household_spawn_candidates({8, 8, 1, 0}, 0, 2); },
                "empty lot dimensions rejected");
        rejects([] { (void)odh::first_household_spawn_candidate({8, 8, 1, 0}, 2, 2, {}); },
                "missing placement validator rejected");
        // Source VerifyFamily runs AFTER original Load. Its in-place path must
        // keep existing entity addresses and execute each constructor lifecycle
        // completely before starting the next member; no Main is dispatched.
        {
            odh::NeighborRecord adult{},child{};
            adult.version=child.version=4;adult.guid=7001;child.guid=7002;
            adult.neighbor_id=41;child.neighbor_id=42;
            const std::vector<odh::HouseholdSpawnRequest> family{{7001,&adult},{7002,&child}};
            odh::World bootstrap;
            require(bootstrap.create(20,odh::EntityKind::object),"original imported object fixture");
            const auto* original=bootstrap.find(20);
            std::vector<std::string> phases;std::vector<odh::WorldEntityReference> exact;
            const auto init=[&](odh::World& actual,std::int16_t id,std::uint32_t guid){
                require(&actual==&bootstrap&&!actual.find(id)->position,"Init uses actual bootstrap while OOW");
                require(actual.find(id)->native_slot0==std::optional<odh::WorldEntityReference>{odh::WorldEntityReference{}}&&
                    actual.native_position({id,actual.find(id)->generation})==std::optional<odh::EntityPosition>{odh::EntityPosition{-16,-16,1,0}}&&
                    actual.find(id)->object_words.at(14)==-1&&actual.find(id)->object_words.at(26)==0,
                    "actual fresh constructor publishes native empty hierarchy and OOW level before Init");
                phases.push_back("init"+std::to_string(guid));
                exact.push_back({id,actual.find(id)->generation});
                return actual.set_person_data(id,58,guid==7001?27:9);
            };
            const auto place=[&](odh::World& actual,std::int16_t id,const odh::EntityPosition& position){
                const auto* avatar=actual.find(id);
                require(avatar->person_data[31]==(avatar->definition->guid==7001?41:42)&&avatar->person_data[61]==6,
                    "NBRS and FAMI inheritance precede arrival");
                phases.push_back("place"+std::to_string(avatar->definition->guid));
                require(actual.set_position(id,position),"fixture publishes actual arrival");
                return odh::HouseholdPositionAttemptStatus::placed;
            };
            const auto admit=[&](odh::World& actual,std::int16_t id,std::uint32_t guid,bool positioned){
                require(positioned&&actual.find(id)->position.has_value(),"admission follows successful arrival");
                phases.push_back("admit"+std::to_string(guid));return true;
            };
            const auto activated=odh::verify_household_members_in_place(bootstrap,family,6,init,
                odh::EntityPosition{168,168,1,0},24,24,place,admit);
            require(phases==std::vector<std::string>{"init7001","place7001","admit7001","init7002","place7002","admit7002"},
                "source constructor lifecycle order is per member");
            require(bootstrap.find(20)==original&&activated.new_members.size()==2&&exact.size()==2&&
                exact[0].id!=exact[1].id&&exact[0].generation!=exact[1].generation&&
                bootstrap.is_live_reference(exact[0])&&bootstrap.is_live_reference(exact[1]),
                "in-place admission preserves existing addresses and actual constructor identities");
            require(bootstrap.find(exact[0].id)->person_data[58]==27&&bootstrap.find(exact[1].id)->person_data[58]==9,
                "adult and child ages survive inheritance");
            phases.clear();
            const auto existing=odh::verify_household_members_in_place(bootstrap,family,6,init,
                odh::EntityPosition{168,168,1,0},24,24,place,admit);
            require(existing.new_members.empty()&&phases.empty()&&existing.runtime_ids==activated.runtime_ids,
                "existing residents are not recreated or initialized");
            for(int failing=0;failing<3;++failing){
                odh::World partial;unsigned initialized=0,admitted=0;
                rejects([&]{(void)odh::verify_household_members_in_place(partial,family,6,
                    [&](auto&,auto,auto){++initialized;return failing!=0;},odh::EntityPosition{168,168,1,0},24,24,
                    [&](auto& actual,auto id,const auto& position){
                        if(failing==1)return odh::HouseholdPositionAttemptStatus::unsupported;
                        require(actual.set_position(id,position),"partial fixture arrival");
                        return odh::HouseholdPositionAttemptStatus::placed;
                    },[&](auto&,auto,auto,bool){++admitted;return false;} );},
                    "bootstrap lifecycle failure must be explicit");
                require(initialized==1&&partial.find(1)&&!partial.find(2)&&admitted==(failing==2?1U:0U),
                    "failure retains real partial effects for bootstrap discard and never starts next member");
            }
        }
        std::cout << "PASS TS1 mailbox arrival candidates and lot bounds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
