// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/architecture_object_state.hpp"
#include "odh/room_score.hpp"
#include "odh/ts1_simulation.hpp"
#include "odh/formats.hpp"
#include "room_lighting_view.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace odh_host {
// One live owner selected on every call. Initial-load requests are retained
// until the actual event-owned registry exists, rather than sent to a copy.
class SessionRoomLighting {
public:
    using State=std::function<odh::ArchitectureObjectState*()>;
    using Rooms=std::function<const odh::GeometricRoomMap*()>;
    using ImportRegistry=std::function<odh::ObjectPositionRegistry*()>;
    using Services=std::function<odh::RoomLightingServices()>;
    SessionRoomLighting(odh::World& world,State state,Rooms rooms,ImportRegistry importing,Services services)
        :world_(world),state_(std::move(state)),rooms_(std::move(rooms)),importing_(std::move(importing)),services_(std::move(services)){
        initialized_=current()!=nullptr;
        if(current())for(std::int16_t cursor=0;;){const auto next=world_.next_entity(cursor);if(!next)break;cursor=*next;
            const auto* e=world_.find(cursor);if(!e)continue;Pose pose{e->generation,e->position,{}};
            if(e->attachment)pose.attachment=std::make_tuple(e->attachment->container,e->attachment->slot,e->attachment->positioned);
            poses_.emplace(cursor,std::move(pose));}
        // A private object transaction can add/remove identities after the last
        // lighting pass. Restore must publish their authored visual class before
        // the first frame, which precedes the first scheduler/process call.
        // Reconcile only the identity cache: preserve known colors, score state,
        // committed publication and queued requests; never fabricate room zero.
        if(current())sync_visuals();
    }

    const odh::RoomLightingSnapshot* current()const {
        const auto* state=state_();return state&&state->lighting?&*state->lighting:bootstrap_?&*bootstrap_:nullptr;
    }
    void refresh(odh::ObjectPositionIdentity identity) {
        const auto* entity=world_.find(identity.id);
        if(!entity||entity->generation!=identity.generation)throw odh::FormatError("lighting Refresh identity expired");
        auto& light=ensure();enqueue(actual_room(*entity));
        if(entity->kind==odh::EntityKind::object&&!odh::refresh_room_lighting_visual(light,world_,identity,0))
            throw odh::FormatError("lighting Refresh visual classification unavailable");
        touched_=true;
    }
    void refresh_score(odh::ObjectPositionIdentity identity) {
        const auto* entity=world_.find(identity.id);
        if(!entity||entity->generation!=identity.generation)throw odh::FormatError("room score Refresh identity expired");
        const auto room=actual_room(*entity);if(!room)return;
        auto* owner=state_();
        if(!owner||!owner->position_registry)throw odh::FormatError("room score Refresh lacks actual registry");
        attach();auto& light=ensure();
        if(!odh::refresh_room_lighting_score(world_,map(),*owner->position_registry,light,room))
            throw odh::FormatError("room score Refresh did not update actual state");
        touched_=true;
    }
    // Safe point after Scheduler.RunTick and pending lifecycle publications.
    void process() {
        auto* owner=state_();
        if(!owner||!owner->position_registry||!owner->position_registry->complete){
            if(current())throw odh::FormatError("lighting processing requires complete actual registry");
            return; // A legacy probe without a RoomInfo service has no lighting owner.
        }
        (void)ensure();attach();
        if(!initialized_)full_refresh();
        sync_scores();sync_visuals();
        auto& light=*owner->lighting;
        if(owner->position_registry->deferred_lighting.empty()){touched_=true;return;}
        const auto receipt=odh::consume_deferred_lighting(world_,map(),*owner->position_registry,light,services_());
        ++drains_;processed_requests_+=receipt.processed_rooms.size();
        if(receipt.visual_commit)owner->lighting_committed=light;
        // Source RefreshRoomScore and RefreshLighting write the same score.
        sync_scores();touched_=true;
    }
    void prepare_capture() {
        if(!current())return;attach();
        if(!initialized_)full_refresh();
        if(touched_){sync_scores();sync_visuals();}
    }
    // VM.LoadComplete repeats the full pass after asynchronous startup, without
    // consuming requests produced by those startup scripts.
    void finish_loading() { full_refresh(); }
    std::string diagnostic()const {
        const auto* light=current();const auto* owner=state_();std::size_t lights=0;
        if(light)for(const auto& entry:light->by_base_room)lights+=entry.second.lights.size();
        std::ostringstream out;out<<"lighting full_passes="<<full_passes_<<" drains="<<drains_
            <<" processed_requests="<<processed_requests_<<" bases="<<(light?light->by_base_room.size():0)
            <<" lights="<<lights<<" committed_bases="<<(owner&&owner->lighting_committed?owner->lighting_committed->by_base_room.size():0)
            <<" pending="<<(owner&&owner->position_registry?owner->position_registry->deferred_lighting.size():bootstrap_requests_.size())
            <<" visual_owners="<<(light?light->visual_rooms.size():0);return out.str();
    }
    std::optional<odh::RoomScoreUpdate> score_at(const odh::EntityPosition& position)const {
        const auto* rooms=rooms_();if(!rooms)return {};
        if(!current())return odh::calculate_room_score(*rooms,odh::room_score_contributions(world_),position.x,position.y,position.level);
        const auto room=odh::geometric_room_at_runtime(*rooms,position.x,position.y,position.level);
        if(!room||*room>=rooms->rooms.size())return {};
        const auto base=rooms->rooms[*room].light_base_room;
        auto score=odh::room_lighting_score(*current(),*rooms,*room);
        // Position events can update this value synchronously inside a script.
        // Reading it does not process pending light changes or publish colors.
        const auto* owner=state_();
        if(owner&&owner->position_registry&&base<owner->position_registry->rooms.size()){
            const auto& registry=*owner->position_registry;
            if(std::find(registry.score_dirty.begin(),registry.score_dirty.end(),base)!=registry.score_dirty.end()&&registry.rooms[base].room_score)
                score=*registry.rooms[base].room_score;
        }
        return odh::RoomScoreUpdate{*room,base,score,*room!=0};
    }
    std::optional<RoomLightingView> view(const odh::Ts1Clock& clock)const {
        if(!current())return {};
        const auto* owner=state_();
        if(owner&&owner->lighting_committed){
            const auto seconds=clock.daylight_seconds_of_day();
            return make_room_lighting_view(*owner->lighting_committed,map(),seconds/3600,(seconds/60)%60,seconds%60);
        }
        // A native owner without a visual commit retains the source's initially
        // zero RoomColors array. This is distinct from absent legacy lighting.
        RoomLightingView result;result.room_colors.resize(map().rooms.size(),{{0,0,0}});return result;
    }
    std::optional<std::uint16_t> visual_room(std::int16_t id,std::uint64_t generation)const {
        const auto* light=current();return light?odh::room_lighting_cached_visual_room(*light,{id,generation}):std::nullopt;
    }
private:
    struct Pose {
        std::uint64_t generation;std::optional<odh::EntityPosition> position;
        std::optional<std::tuple<std::int16_t,std::size_t,bool>> attachment;
        bool operator==(const Pose& other)const{return generation==other.generation&&position==other.position&&attachment==other.attachment;}
    };
    odh::World& world_;State state_;Rooms rooms_;ImportRegistry importing_;Services services_;
    std::optional<odh::RoomLightingSnapshot> bootstrap_;
    std::vector<std::uint16_t> bootstrap_requests_;
    std::map<std::int16_t,Pose> poses_;bool touched_=false,initialized_=false;
    std::uint64_t full_passes_=0,drains_=0,processed_requests_=0;
    const odh::GeometricRoomMap& map()const {
        const auto* rooms=rooms_();if(!rooms||!rooms->light_base_rooms_resolved)throw odh::FormatError("lighting room map unavailable");return *rooms;
    }
    std::uint16_t actual_room(const odh::Entity& entity)const {
        if(!entity.position)return 0;
        const auto& p=*entity.position;const auto room=odh::geometric_room_at_runtime(map(),p.x,p.y,p.level);
        if(!room)throw odh::FormatError("lighting target room unresolved");return *room;
    }
    static void append(std::vector<std::uint16_t>& requests,std::uint16_t room) {
        if(std::find(requests.begin(),requests.end(),room)!=requests.end())return;
        if(requests.size()>=odh::RoomLightingLimits{}.deferred_requests)throw odh::FormatError("lighting request budget exceeded");requests.push_back(room);
    }
    void enqueue(std::uint16_t room) {
        if(room>=map().rooms.size())throw odh::FormatError("lighting request room unavailable");
        if(auto* importing=importing_()){
            if(!importing->defer_lighting(room,[]{return true;}))throw odh::FormatError("initial lighting request rejected");
        }else if(auto* owner=state_();owner&&owner->position_registry)append(owner->position_registry->deferred_lighting,room);
        else append(bootstrap_requests_,room);
    }
    odh::RoomLightingSnapshot& ensure() {
        if(auto* owner=state_();owner&&owner->lighting)return *owner->lighting;
        if(bootstrap_)return *bootstrap_;
        (void)map();
        auto* owner=state_();auto& light=owner?owner->lighting.emplace():bootstrap_.emplace();
        return light;
    }
    void full_refresh() {
        auto* owner=state_();
        if(!owner||!owner->position_registry||!owner->position_registry->complete)
            throw odh::FormatError("full lighting refresh requires completed lot load");
        (void)ensure();attach();sync_scores();sync_visuals();
        const auto receipt=odh::refresh_all_room_lighting(world_,map(),*owner->position_registry,*owner->lighting,services_());
        if(receipt.visual_commit)owner->lighting_committed=owner->lighting;
        ++full_passes_;
        initialized_=true;touched_=true;
    }
    void attach() {
        auto* owner=state_();if(!owner||!owner->position_registry)return;
        if(bootstrap_){
            if(owner->lighting)throw odh::FormatError("lighting has two bootstrap owners");
            owner->lighting=std::move(bootstrap_);bootstrap_.reset();
        }
        if(!bootstrap_requests_.empty()){
            auto requests=bootstrap_requests_;for(auto room:owner->position_registry->deferred_lighting)append(requests,room);
            for(auto room:requests)if(room>=map().rooms.size())throw odh::FormatError("lighting bootstrap room identity changed");
            owner->position_registry->deferred_lighting=std::move(requests);bootstrap_requests_.clear();
        }
    }
    void sync_scores() {
        auto* owner=state_();if(!owner||!owner->lighting||!owner->position_registry)return;
        auto& light=*owner->lighting;auto& registry=*owner->position_registry;
        for(const auto base:registry.score_dirty){
            if(base>=registry.rooms.size()||!registry.rooms[base].room_score)throw odh::FormatError("dirty room score unavailable");
            if(!base)continue;auto& target=light.by_base_room[base];target.base_room=base;target.room_score=*registry.rooms[base].room_score;
        }
        registry.score_dirty.clear();
    }
    void sync_visuals() {
        auto* owner=state_();if(!owner||!owner->lighting)return;auto& light=*owner->lighting;
        std::set<std::int16_t> live;
        for(std::int16_t cursor=0;;){const auto next=world_.next_entity(cursor);if(!next)break;cursor=*next;
            const auto* e=world_.find(cursor);if(!e||e->dead||e->ui_ghost)continue;live.insert(cursor);
            Pose pose{e->generation,e->position,{}};
            if(e->attachment)pose.attachment=std::make_tuple(e->attachment->container,e->attachment->slot,e->attachment->positioned);
            const auto previous=poses_.find(cursor);
            const bool changed=previous!=poses_.end()&&!(previous->second==pose);
            if(changed||!odh::room_lighting_cached_visual_room(light,{cursor,e->generation}))
                if(!odh::refresh_room_lighting_visual(light,world_,{cursor,e->generation},0))throw odh::FormatError("lighting position visual update unresolved");
            poses_[cursor]=std::move(pose);
        }
        for(auto it=light.visual_rooms.begin();it!=light.visual_rooms.end();){
            const auto* e=world_.find(it->owner.id);
            if(!live.count(it->owner.id)||!e||e->generation!=it->owner.generation)it=light.visual_rooms.erase(it);else ++it;
        }
        for(auto it=poses_.begin();it!=poses_.end();)if(!live.count(it->first))it=poses_.erase(it);else ++it;
    }
};
}
