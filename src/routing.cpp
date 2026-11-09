#include "odh/routing.hpp"
#include "odh/live_entry_point5.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace odh {
struct Routing::SourceResetOperation {
    Routing& route;
    explicit SourceResetOperation(Routing& value):route(value){++route.source_reset_operation_depth_;}
    ~SourceResetOperation(){route.leave_source_reset_operation();}
};
void Routing::clear_source_reset_storage() noexcept {
    job_.reset();suspended_jobs_.clear();parked_immediate_routes_.clear();replay_placement_authorization_.reset();
    active_range_lease_.reset();replay_sparse_consideration_.reset();source_sparse_continue_=false;
    last_tick_.reset();
}
void Routing::leave_source_reset_operation() noexcept {
    if(source_reset_operation_depth_)--source_reset_operation_depth_;
    if(!source_reset_operation_depth_&&source_reset_retired_){clear_source_reset_storage();source_reset_retired_=false;}
}
namespace {
thread_local unsigned char resource_owner_thread_identity=0;
constexpr std::array<RoutePoint,8> headings={{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
std::uint8_t heading(RoutePoint from,RoutePoint to) {
    const RoutePoint d{(to.x>from.x)-(to.x<from.x),(to.y>from.y)-(to.y<from.y)};
    for(std::uint8_t n=0;n<8;++n) if(headings[n]==d) return n;
    return 0;
}
PrimitiveOutcome unsupported(const std::string& why) {return {PrimitiveDisposition::unsupported,why};}
bool supported_bed_north_circle_slot_profile(const SlotRecord& slot) {
    return (slot.flags==0x01||slot.flags==0x10)&&slot.facing==-2&&
        slot.proximity==std::optional<std::array<std::int32_t,3>>{{16,16,16}}&&
        slot.offset_bits==std::array<std::uint32_t,3>{0,0,0}&&
        slot.standing==1&&slot.sitting==0&&slot.ground==0&&slot.snap_target_slot==-1&&
        slot.max_size==0&&slot.unknown_i10==0&&slot.gradient_bits==0x3E400000&&
        slot.height==0&&slot.resolution==16;
}
// Pinned VMGotoRelativePosition: OnTopOf is an exact standing goal;
// OldTrapCount and RouteCount are parsed but unused by Execute.
bool source_exact_relative(const Instruction& i) {
    return is_source_exact_goto_relative_instruction(i);
}
bool relative_failure_trees(const Instruction& i) {
    return (source_exact_relative(i)||is_source_ranged_goto_relative_instruction(i)||
        is_source_ordinary_go_here_instruction(i))&&(i.operands[6]&2U)==0;
}
bool supported_circle_slot_profile(const SlotRecord& slot) {
    const bool house07=slot.flags==0x55&&slot.facing==-2;
    const bool bed_north=supported_bed_north_circle_slot_profile(slot);
    const bool door_north=slot.flags==0x0801&&slot.facing==-3;
    const bool door_south=slot.flags==0x0810&&slot.facing==-3;
    return (house07||bed_north||door_north||door_south) &&
        slot.offset_bits==std::array<std::uint32_t,3>{0,0,0} &&
        slot.standing==1 && slot.sitting==0 && slot.ground==0 &&
        slot.snap_target_slot==-1 && slot.max_size==0 && slot.unknown_i10==0 &&
        slot.gradient_bits==0x3E400000 && slot.height==0 && slot.proximity &&
        slot.resolution==16 && (*slot.proximity)[0]>0 &&
        (*slot.proximity)[0]==(*slot.proximity)[1] &&
        (*slot.proximity)[0]==(*slot.proximity)[2] && (*slot.proximity)[0]<=4096;
}
bool supported_ranged_slot_profile(const SlotRecord& slot) {
    const bool door_range=(slot.flags==0x54||slot.flags==0x45)&&slot.facing==-2&&
        slot.offset_bits==std::array<std::uint32_t,3>{0,0,0}&&slot.proximity&&
        *slot.proximity==std::array<std::int32_t,3>{16,32,16}&&
        slot.standing==1&&slot.sitting==0&&slot.ground==0&&slot.snap_target_slot==-1&&
        slot.max_size==0&&slot.unknown_i10==0&&slot.gradient_bits==0x3E400000&&
        slot.height==0&&slot.resolution==16;
    const bool house07_global_range=slot.flags==0xA0FF&&slot.facing==-3&&
        slot.offset_bits==std::array<std::uint32_t,3>{0,0,0}&&slot.proximity&&
        *slot.proximity==std::array<std::int32_t,3>{16,64,16}&&
        slot.standing==1&&slot.sitting==0&&slot.ground==0&&slot.snap_target_slot==-1&&
        slot.max_size==0&&slot.unknown_i10==0&&slot.gradient_bits==0&&
        slot.height==0&&slot.resolution==8;
    return door_range||house07_global_range||is_source_relative_range_slot(slot);
}
bool supported_point_offset_slot_profile(const SlotRecord& slot) {
    const auto y=slot.offset_bits[1];
    const bool house07_offset=y==0xC1800000U||y==0xC0400000U||
        y==0xC1500000U||y==0xC0C00000U;
    return slot.type==3&&slot.flags==0&&slot.offset_bits[0]==0&&
        house07_offset&&slot.offset_bits[2]==0&&slot.standing==1&&
        slot.sitting==0&&slot.ground==0&&slot.snap_target_slot==-1&&
        slot.proximity==std::optional<std::array<std::int32_t,3>>{{16,16,16}}&&
        slot.max_size==0&&slot.unknown_i10==0&&slot.gradient_bits==0x3E400000&&
        slot.height==0&&slot.facing==4&&slot.resolution==16;
}
float slot_float(std::uint32_t bits) {
    float value=0;static_assert(sizeof(value)==sizeof(bits),"SLOT requires IEEE float32");
    static_assert(std::numeric_limits<float>::is_iec559,"SLOT source geometry requires IEEE float32");
    std::memcpy(&value,&bits,sizeof(value));return value;
}
bool same_float_bits(float a,float b) {
    std::uint32_t x=0,y=0;std::memcpy(&x,&a,sizeof(x));std::memcpy(&y,&b,sizeof(y));return x==y;
}
bool valid_sparse_walk_to(const SourceSparseRouteSnapshot& sparse,const RouteFrameContext& frame,
    bool ranked,bool parked,const std::vector<RoutePoint>& path,std::size_t next,
    const EntityPosition& expected,const World& world) {
    if(sparse.fallback_room_route.size()>32||(!ranked&&sparse.walk_to_purpose!=SourceWalkToPurpose::architecture_invalidation&&!sparse.fallback_room_route.empty()))return false;
    for(std::size_t n=0;n<sparse.fallback_room_route.size();++n) {
        const auto& edge=sparse.fallback_room_route[n];const auto* portal=world.find(edge.portal_id);
        if(!portal||portal->kind!=EntityKind::object||!portal->position||!(*portal->position==edge.position)||
           edge.source_room==edge.target_room||(n&&sparse.fallback_room_route[n-1].target_room!=edge.source_room))return false;
        for(std::size_t previous=0;previous<n;++previous) {
            const auto& old=sparse.fallback_room_route[previous];
            if(old.source_room==edge.source_room&&old.target_room==edge.target_room&&old.portal_id==edge.portal_id)return false;
        }
    }
    const bool shoo=sparse.walk_to_purpose==SourceWalkToPurpose::shoo;
    const bool architecture=sparse.walk_to_purpose==SourceWalkToPurpose::architecture_invalidation;
    if(sparse.walk_to_purpose!=SourceWalkToPurpose::ranked_alternative&&!shoo&&!architecture)return false;
    if(architecture)return sparse.walk_to_pending&&!path.empty()&&next>0&&next<=path.size()&&
        ((sparse.lifecycle==0&&(frame.state==RouteFrameState::initial||frame.state==RouteFrameState::walking||
            (frame.state==RouteFrameState::turn_only&&path.back()==RoutePoint{expected.x,expected.y})||
            (parked&&frame.state==RouteFrameState::room_portal)))||
         (sparse.lifecycle==1&&frame.state==RouteFrameState::begin_turn&&sparse.turn)||
         (sparse.lifecycle==2&&frame.state==RouteFrameState::end_turn&&sparse.turn));
    if(!sparse.walk_to_pending)return !shoo;
    if(shoo) {
        if(path.empty()||next==0||next>path.size()||sparse.lifecycle==1||sparse.avatars_to_consider.empty())return false;
        if(sparse.lifecycle==2)return frame.state==RouteFrameState::end_turn&&sparse.turn.has_value()&&
            path.back()==RoutePoint{expected.x,expected.y};
        return sparse.lifecycle==0&&(frame.state==RouteFrameState::shooed||frame.state==RouteFrameState::walking||
            (frame.state==RouteFrameState::turn_only&&path.back()==RoutePoint{expected.x,expected.y})||
            (parked&&frame.state==RouteFrameState::room_portal));
    }
    if(!ranked||!sparse.motion.current||next!=1||path.empty()||!(path.front()==RoutePoint{expected.x,expected.y})||
       sparse.lifecycle==1)return false;
    const bool turn=frame.state==RouteFrameState::turn_only||frame.state==RouteFrameState::end_turn;
    if(turn&&!(path.back()==RoutePoint{expected.x,expected.y}))return false;
    if(sparse.lifecycle==2)return frame.state==RouteFrameState::end_turn&&sparse.turn.has_value();
    return sparse.lifecycle==0&&(frame.state==RouteFrameState::walking||frame.state==RouteFrameState::turn_only||
        (parked&&frame.state==RouteFrameState::room_portal));
}
bool collision_consideration_matches(const SourceSparseRouteSnapshot& sparse){
    return !sparse.collision||(sparse.collision->considered.size()==sparse.avatars_to_consider.size()&&
        std::equal(sparse.collision->considered.begin(),sparse.collision->considered.end(),
            sparse.avatars_to_consider.begin(),[](const auto& a,const auto& b){return a.id==b.id&&a.generation==b.generation;}));
}
constexpr double source_pi=3.141592653589793238462643383279502884;
double source_turn_difference(float from,float to) {
    const auto value=std::fmod(std::fmod(static_cast<double>(to)-from,source_pi*2)+source_pi*2,source_pi*2);
    return value>source_pi?value-source_pi*2:value;
}
struct SourceTurnSelection {std::uint8_t index;float residual;};
SourceTurnSelection source_turn_selection(float from,float to) {
    const auto difference=source_turn_difference(from,to),magnitude=std::abs(difference);
    const auto offset=static_cast<std::uint8_t>(difference>0?0:1);
    if(magnitude>=source_pi-0.01)return {static_cast<std::uint8_t>(4+offset),0};
    if(magnitude>=source_pi/2-0.01)return {static_cast<std::uint8_t>(6+offset),static_cast<float>(magnitude-source_pi/2)};
    if(magnitude>=source_pi/4-0.01)return {static_cast<std::uint8_t>(8+offset),static_cast<float>(magnitude-source_pi/4)};
    return {0,0};
}
EntityPosition source_look_position(const Entity& entity) {
    return entity.position.value_or(EntityPosition{-32768,-32768,1,0});
}
std::int32_t source_short_sum(std::int32_t a,std::int32_t b) {
    const auto bits=(static_cast<std::uint32_t>(a)+static_cast<std::uint32_t>(b))&0xffffU;
    return bits>=0x8000U?static_cast<std::int32_t>(bits)-65536:static_cast<std::int32_t>(bits);
}
bool source_point_slot_profile(const SlotRecord& slot) {
    return slot.type==3&&slot.flags==0&&slot.sitting==0&&slot.snap_target_slot==-1&&
        slot_float(slot.offset_bits[2])==0&&slot.facing&&*slot.facing>=-3&&*slot.facing<=7&&
        std::isfinite(slot_float(slot.offset_bits[0]))&&std::isfinite(slot_float(slot.offset_bits[1]));
}
double source_round_even(double value) {
    const auto lower=std::floor(value),fraction=value-lower;
    return fraction<0.5?lower:fraction>0.5?lower+1:
        std::fmod(std::abs(lower),2.0)==0?lower:lower+1;
}
std::uint8_t source_direction_notch(float radians) {
    constexpr double pi=3.141592653589793238462643383279502884;
    auto direction=std::fmod(source_round_even(static_cast<double>(radians)/(pi/4)),8.0);
    if(direction<0)direction+=8;
    return static_cast<std::uint8_t>(direction);
}
bool same_slot_record(const SlotRecord& a,const SlotRecord& b) {
    return a.type==b.type&&a.offset_bits==b.offset_bits&&a.standing==b.standing&&a.sitting==b.sitting&&a.ground==b.ground&&
        a.flags==b.flags&&a.snap_target_slot==b.snap_target_slot&&a.proximity==b.proximity&&a.max_size==b.max_size&&
        a.unknown_i10==b.unknown_i10&&a.gradient_bits==b.gradient_bits&&a.height==b.height&&a.facing==b.facing&&
        a.resolution==b.resolution;
}
bool same_circle_goal(const SourceSlotCircleGoal& a,const SourceSlotCircleGoal& b) {
    return a.position==b.position&&a.entry_flags==b.entry_flags&&a.facing==b.facing&&
        same_float_bits(a.radian_direction,b.radian_direction);
}
float source_slot_object_radians(const Entity& entity) {
    if(entity.kind==EntityKind::avatar)return entity.radian_direction;
    return source_game_object_radians(entity.position->direction);
}
std::int32_t point_offset_y(const SlotRecord& slot) {
    switch(slot.offset_bits[1]) {
        case 0xC1800000U:return -16;
        case 0xC0400000U:return -3;
        case 0xC1500000U:return -13;
        case 0xC0C00000U:return -6;
        default:throw std::invalid_argument("unverified House07 point SLOT offset");
    }
}
std::int32_t round_q16_even(std::int64_t value) {
    constexpr std::int64_t scale=65536;
    const bool negative=value<0;
    const auto magnitude=negative?static_cast<std::uint64_t>(-(value+1))+1U:
                                 static_cast<std::uint64_t>(value);
    auto whole=magnitude/static_cast<std::uint64_t>(scale);
    const auto remainder=magnitude%static_cast<std::uint64_t>(scale);
    if(remainder>32768U||(remainder==32768U&&(whole&1U)))++whole;
    const auto signed_value=static_cast<std::int64_t>(whole);
    return static_cast<std::int32_t>(negative?-signed_value:signed_value);
}
bool supported_child_shy_sitting_fallback_profile(const SlotTable& table,std::size_t type3_index,
    const SlotRecord& slot) {
    return table.version==10&&type3_index==10&&slot.type==3&&slot.flags==0x8055&&
        slot.offset_bits==std::array<std::uint32_t,3>{0,0,0}&&
        slot.proximity==std::optional<std::array<std::int32_t,3>>{{32,80,32}}&&
        slot.standing==1&&slot.sitting==10&&slot.ground==0&&slot.snap_target_slot==-1&&
        slot.max_size==0&&slot.unknown_i10==0&&slot.gradient_bits==0x3E400000&&
        slot.height==0&&slot.facing==-2&&slot.resolution==16;
}
std::int32_t floor_tile(std::int32_t coordinate) {
    auto quotient=coordinate/16;
    if(coordinate<0 && coordinate%16) --quotient;
    return quotient;
}
}
bool is_legacy_ui_go_here_request(const Instruction& i,bool target_is_ui_ghost) noexcept {
    return i.opcode==27&&target_is_ui_ghost&&i.operands==std::array<std::uint8_t,8>{10,0,254,255,10,0,5,0};
}
bool is_source_ordinary_go_here_instruction(const Instruction& i) noexcept {
    const auto request=decode_source_goto_relative_position(i);
    return request&&request->location==-2&&request->direction==-1;
}
std::optional<SourceGotoRelativePositionRequest> decode_source_goto_relative_position(const Instruction& i) noexcept {
    const auto defined=[](std::uint8_t value){return value<=7||value>=254;};
    if(i.opcode!=27||!defined(i.operands[2])||!defined(i.operands[3]))return {};
    const auto signed_byte=[](std::uint8_t value)->std::int16_t{
        return value<=127?value:static_cast<std::int16_t>(value)-256;
    };
    return SourceGotoRelativePositionRequest{signed_byte(i.operands[2]),signed_byte(i.operands[3]),(i.operands[6]&2U)!=0};
}
bool is_source_exact_goto_relative_instruction(const Instruction& i) noexcept {
    const auto request=decode_source_goto_relative_position(i);
    return request&&request->location==-2;
}
bool is_source_ranged_goto_relative_instruction(const Instruction& i) noexcept {
    const auto request=decode_source_goto_relative_position(i);
    return request&&request->location!=-2;
}
bool is_source_goto_relative_slot_instruction(const Instruction& i) noexcept {
    return decode_source_goto_relative_position(i).has_value();
}
std::optional<SlotTable> make_source_goto_relative_slot(const Instruction& i) {
    const auto request=decode_source_goto_relative_position(i);
    if(!request)return {};
    SlotRecord slot{};slot.type=3;slot.offset_bits={0,0,0};slot.standing=1;
    slot.sitting=0;slot.ground=0;slot.flags=0;slot.snap_target_slot=-1;
    slot.proximity=std::array<std::int32_t,3>{0,0,0};
    if(request->location==-1){slot.flags=255;slot.proximity=std::array<std::int32_t,3>{16,32,16};}
    else if(request->location>=0){slot.flags=1U<<request->location;slot.proximity=std::array<std::int32_t,3>{16,24,0};}
    slot.max_size=100;slot.unknown_i10=0;slot.gradient_bits=0;slot.height=0;slot.resolution=16;
    slot.facing=request->direction==-1?-3:request->direction;
    return SlotTable{0,10,{slot},0};
}
float source_slot_facing_radians(const SlotRecord& slot,const EntityPosition& target,RoutePoint point) {
    if(!slot.facing||target.direction>7||(*slot.facing<-3)||*slot.facing>7||*slot.facing==-1||
       target.x<INT16_MIN||target.x>INT16_MAX||target.y<INT16_MIN||target.y>INT16_MAX||
       point.x<INT16_MIN||point.x>INT16_MAX||point.y<INT16_MIN||point.y>INT16_MAX)
        throw std::invalid_argument("source standing SLOT facing or short-coordinate domain is unavailable");
    if(*slot.facing==-3)return 0.0f;
    if(*slot.facing>=0){
        constexpr double pi=3.141592653589793238462643383279502884;
        const auto rotated=(*slot.facing+target.direction)%8;
        return static_cast<float>(rotated>4?static_cast<double>(rotated)*pi/4.0:
            static_cast<double>(rotated-8)*pi/4.0);
    }
    const auto cx=static_cast<float>(target.x)/16.0f,cy=static_cast<float>(target.y)/16.0f;
    const auto px=cx+static_cast<float>(point.x-target.x)/16.0f;
    const auto py=cy+static_cast<float>(point.y-target.y)/16.0f;
    return static_cast<float>(std::atan2(static_cast<double>(cx-px),static_cast<double>(-(cy-py))));
}
bool is_source_failure_tree_continuation(const RouteJobSnapshot& job) noexcept {
    if(!job.failure_tree_invoked||!job.external_frame_active||job.external_frame_sitting||job.external_frame_purpose||
       !job.external_frame_call||(job.frame.state!=RouteFrameState::room_portal&&
        !(job.unplaced_room_failure&&job.frame.state==RouteFrameState::failed))||job.failure_code<=0||job.failure_blocker<0)return false;
    const auto& call=*job.external_frame_call;const auto& context=job.pending.context;
    return call.key.scope==RoutineScope::global&&call.key.id==398&&call.return_mode==VmCallReturnMode::ordinary&&
        !call.exact_arguments&&call.context.caller==job.actor&&vm_context_equal(call.context,context)&&call.arguments.size()==4&&
        call.arguments[0]==job.failure_code&&call.arguments[1]==job.failure_blocker&&call.arguments[2]==0&&call.arguments[3]==0;
}
bool is_source_relative_range_slot(const SlotRecord& slot) {
    const bool side=slot.flags!=0&&slot.flags<=128&&(slot.flags&(slot.flags-1U))==0;
    return slot.type==3&&(side||slot.flags==255)&&
        slot.offset_bits==std::array<std::uint32_t,3>{0,0,0}&&
        slot.proximity==std::optional<std::array<std::int32_t,3>>{{16,slot.flags==255?32:24,slot.flags==255?16:0}}&&
        slot.standing==1&&slot.sitting==0&&slot.ground==0&&slot.snap_target_slot==-1&&
        slot.max_size==100&&slot.unknown_i10==0&&slot.gradient_bits==0&&
        slot.height==0&&slot.facing&&(*slot.facing==-3||*slot.facing==-2||
            (*slot.facing>=0&&*slot.facing<=7))&&slot.resolution==16;
}
bool is_source_slot_circle(const SlotRecord& slot) {
    constexpr std::uint32_t allowed=255U|256U|512U|1024U|2048U|8192U|16384U|32768U|(1U<<29)|(1U<<30);
    return slot.type==3&&(slot.flags&255U)!=0&&(slot.flags&~allowed)==0&&
        slot_float(slot.offset_bits[0])==0&&slot_float(slot.offset_bits[1])==0&&slot_float(slot.offset_bits[2])==0&&
        slot.standing>0&&slot.standing<=32767&&slot.sitting==0&&slot.snap_target_slot==-1&&
        slot.proximity&&(*slot.proximity)[0]>0&&(*slot.proximity)[0]<=4096&&
        (*slot.proximity)[1]==(*slot.proximity)[0]&&(*slot.proximity)[2]>=0&&(*slot.proximity)[2]<=4096&&
        slot.max_size&&slot.unknown_i10&&slot.gradient_bits&&slot.height&&
        (slot.facing==-2||slot.facing==-3)&&slot.resolution;
}
std::vector<SourceSlotCircleGoal> enumerate_source_slot_circle_candidates(
    const SlotTable& table,std::size_t index,const EntityPosition& target,float radians) {
    const auto* slot=slot_of_type(table,3,index);
    if(table.version!=10||!slot||!is_source_slot_circle(*slot)||!std::isfinite(radians)||
       target.direction>7||target.level<1||target.x<INT16_MIN||target.x>INT16_MAX||
       target.y<INT16_MIN||target.y>INT16_MAX)
        throw std::invalid_argument("source circle requires a finite positioned target and bounded v10 directional-circle SLOT");
    constexpr double pi=3.141592653589793238462643383279502884;
    const auto radius=(*slot->proximity)[0];
    const float cx=static_cast<float>(target.x)/16.0f,cy=static_cast<float>(target.y)/16.0f;
    const auto sector_angle=(slot->flags&512U)?0.0f:radians;
    std::vector<SourceSlotCircleGoal> result;result.reserve(8);
    for(unsigned n=0;n<8;++n){
        const auto angle=static_cast<double>(n)*pi/4.0;
        const auto x=source_round_even(std::sin(angle)*radius),y=source_round_even(std::cos(angle)*radius);
        const float px=cx+static_cast<float>(x)/16.0f,py=cy+static_cast<float>(y)/16.0f;
        const auto gx=source_round_even(static_cast<double>(px*16.0f)),gy=source_round_even(static_cast<double>(py*16.0f));
        if(gx<INT16_MIN||gx>INT16_MAX||gy<INT16_MIN||gy>INT16_MAX)
            throw std::invalid_argument("source circle candidate exceeds signed-short position domain");
        const RoutePoint point{static_cast<std::int32_t>(gx),static_cast<std::int32_t>(gy)};
        const float dx=cx-px,dy=-(cy-py);
        const float facing=slot->facing==-3?0.0f:
            static_cast<float>(std::atan2(static_cast<double>(dx),static_cast<double>(dy)));
        result.push_back({point,reference_slot_entry_flags({target.x,target.y},target.direction,point,
            static_cast<std::uint8_t>(slot->flags),sector_angle),source_direction_notch(facing),facing});
    }
    return result;
}
std::vector<RoutePoint> enumerate_reference_slot_range_lattice(
    const SlotTable& table,std::size_t type3_index,std::size_t candidate_limit) {
    const auto* slot=slot_of_type(table,3,type3_index);
    if(table.version!=10 || !slot || !slot->proximity || !slot->resolution)
        throw std::invalid_argument("reference SLOT lattice requires a version-10 type-3 slot with range fields");
    const auto min_proximity=(*slot->proximity)[0];
    const auto raw_max_proximity=(*slot->proximity)[1];
    const auto max_proximity=raw_max_proximity==0?min_proximity:raw_max_proximity;
    const auto resolution=*slot->resolution;
    if(min_proximity<0 || max_proximity<0 || max_proximity<min_proximity || resolution<=0 ||
       max_proximity==min_proximity)
        throw std::invalid_argument("reference SLOT lattice requires a valid ranged proximity profile");
    if(candidate_limit==0) throw std::invalid_argument("reference SLOT lattice candidate budget is zero");

    // VMSlotParser computes the last lattice coordinate by truncating the
    // maximum proximity to a whole resolution step, then visits x outer/y
    // inner, both inclusive. Distance, room and SLOT flags are applied later.
    const auto bound=(static_cast<std::int64_t>(max_proximity)/resolution)*resolution;
    const auto steps=static_cast<std::uint64_t>(bound/resolution);
    const auto axis=steps*2U+1U;
    if(axis>candidate_limit || axis>candidate_limit/axis)
        throw std::length_error("reference SLOT lattice exceeds candidate budget");
    const auto count=static_cast<std::size_t>(axis*axis);
    std::vector<RoutePoint> offsets;
    offsets.reserve(count);
    for(std::uint64_t x_index=0;x_index<axis;++x_index) {
        const auto x=-bound+static_cast<std::int64_t>(x_index)*resolution;
        for(std::uint64_t y_index=0;y_index<axis;++y_index) {
            const auto y=-bound+static_cast<std::int64_t>(y_index)*resolution;
            if(x<std::numeric_limits<std::int32_t>::min() || x>std::numeric_limits<std::int32_t>::max() ||
               y<std::numeric_limits<std::int32_t>::min() || y>std::numeric_limits<std::int32_t>::max())
                throw std::length_error("reference SLOT lattice coordinate exceeds int32");
            offsets.push_back({static_cast<std::int32_t>(x),static_cast<std::int32_t>(y)});
        }
    }
    return offsets;
}
std::vector<RoutePoint> filter_reference_slot_range_proximity(
    const SlotTable& table,std::size_t type3_index,const std::vector<RoutePoint>& offsets) {
    const auto* slot=slot_of_type(table,3,type3_index);
    if(table.version!=10 || !slot || !slot->proximity || !slot->resolution || *slot->resolution<=0)
        throw std::invalid_argument("reference SLOT proximity filter requires a version-10 type-3 range slot");
    const auto min_proximity=(*slot->proximity)[0];
    const auto raw_max_proximity=(*slot->proximity)[1];
    const auto max_proximity=raw_max_proximity==0?min_proximity:raw_max_proximity;
    if(min_proximity<0 || max_proximity<0 || max_proximity<=min_proximity)
        throw std::invalid_argument("reference SLOT proximity filter requires a valid ranged proximity profile");

    constexpr std::uint32_t fso_square=1U<<30;
    const bool square=(slot->flags&fso_square)!=0;
    const auto max=static_cast<std::uint64_t>(max_proximity);
    const auto min=static_cast<std::uint64_t>(min_proximity);
    // For integer-coordinate offsets, sqrt(n) <= max+0.5 is exactly
    // n <= max^2+max; sqrt(n) >= min-0.5 is n >= min^2-min+1.
    const auto max_squared=max*max+max;
    const auto min_squared=min==0?0:min*min-min+1;
    std::vector<RoutePoint> candidates;
    candidates.reserve(offsets.size());
    for(const auto point:offsets) {
        const auto ax=point.x<0?static_cast<std::uint64_t>(-static_cast<std::int64_t>(point.x)):
                                static_cast<std::uint64_t>(point.x);
        const auto ay=point.y<0?static_cast<std::uint64_t>(-static_cast<std::int64_t>(point.y)):
                                static_cast<std::uint64_t>(point.y);
        std::uint64_t distance_squared=0;
        if(square) distance_squared=std::max(ax,ay)*std::max(ax,ay);
        else {
            // Values beyond max+1 cannot survive the upper band. Reject them
            // before squaring so the remaining unsigned sum cannot overflow.
            if(ax>max+1 || ay>max+1) continue;
            distance_squared=ax*ax+ay*ay;
        }
        if(distance_squared>=min_squared && distance_squared<=max_squared)
            candidates.push_back(point);
    }
    return candidates;
}
std::uint8_t reference_slot_entry_flags(
    RoutePoint object_position,std::uint8_t object_direction,RoutePoint candidate_position,
    std::uint8_t slot_direction_flags,std::optional<float> object_radians) {
    if(object_radians&&!std::isfinite(*object_radians))
        throw std::invalid_argument("SLOT sector target angle must be finite");
    constexpr double pi=3.141592653589793238462643383279502884;
    constexpr double angle_error=-0.1;
    const auto dx=floor_tile(candidate_position.x)-floor_tile(object_position.x);
    const auto neg_dy=floor_tile(object_position.y)-floor_tile(candidate_position.y);
    auto degrees=std::atan2(static_cast<double>(dx),static_cast<double>(neg_dy))*(180.0/pi)-
                 (object_radians?static_cast<double>(*object_radians)*(180.0/pi):
                    static_cast<double>(object_direction%8)*45.0);
    degrees=std::fmod(std::fmod(degrees,360.0)+360.0,360.0);
    if(degrees>180.0) degrees-=360.0;
    std::uint8_t directions=0;
    if(degrees>=-45.0-angle_error && degrees<=45.0+angle_error) directions|=1U<<0;
    if(degrees>=0.0-angle_error && degrees<=90.0+angle_error) directions|=1U<<1;
    if(degrees>=45.0-angle_error && degrees<=135.0+angle_error) directions|=1U<<2;
    if((degrees>=90.0-angle_error && degrees<=180.0+angle_error)||degrees<=-180.0+angle_error) directions|=1U<<3;
    if(degrees>=135.0-angle_error||degrees<=-135.0+angle_error) directions|=1U<<4;
    if((degrees>=-180.0-angle_error&&degrees<=-90.0+angle_error)||degrees>=180.0+angle_error) directions|=1U<<5;
    if(degrees>=-135.0-angle_error&&degrees<=-45.0+angle_error) directions|=1U<<6;
    if(degrees>=-90.0-angle_error&&degrees<=0.0+angle_error) directions|=1U<<7;
    return directions&slot_direction_flags;
}
std::vector<ReferenceSlotCircleCandidate> enumerate_reference_slot_circle_candidates(
    const SlotTable& table,std::size_t type3_index,RoutePoint object_position,std::uint8_t object_direction) {
    const auto* slot=slot_of_type(table,3,type3_index);
    if(table.version!=10 || !slot || !supported_circle_slot_profile(*slot))
        throw std::invalid_argument("reference SLOT circle requires a verified House07 or Door v10 type-3 profile");
    const auto radius=(*slot->proximity)[0];
    // The diagonal rounding is an ODH candidate-generation rule, not a claim
    // about the original executable's tie-breaking or destination scoring.
    const auto diagonal=static_cast<std::int32_t>((static_cast<std::int64_t>(radius)*46341+32768)/65536);
    std::vector<ReferenceSlotCircleCandidate> candidates;
    candidates.reserve(headings.size());
    for(std::size_t n=0;n<headings.size();++n) {
        const auto step=(n&1U)?diagonal:radius;
        const auto x=static_cast<std::int64_t>(object_position.x)+headings[n].x*step;
        const auto y=static_cast<std::int64_t>(object_position.y)+headings[n].y*step;
        if(x<std::numeric_limits<std::int32_t>::min() || x>std::numeric_limits<std::int32_t>::max() ||
           y<std::numeric_limits<std::int32_t>::min() || y>std::numeric_limits<std::int32_t>::max())
            throw std::length_error("reference SLOT circle candidate exceeds int32");
        const RoutePoint point{static_cast<std::int32_t>(x),static_cast<std::int32_t>(y)};
        candidates.push_back({point,reference_slot_entry_flags(object_position,object_direction,point,
            static_cast<std::uint8_t>(slot->flags))});
    }
    return candidates;
}
ReferenceSlotPointCandidate enumerate_reference_slot_point_candidate(
    const SlotTable& table,std::size_t type3_index,RoutePoint object_position,
    std::uint8_t object_direction) {
    const auto* slot=slot_of_type(table,3,type3_index);
    if(table.version!=10||!slot||!supported_point_offset_slot_profile(*slot)||object_direction>=8)
        throw std::invalid_argument("reference SLOT point requires a verified House07 Medicine Cabinet profile");
    constexpr std::array<std::int32_t,8> cosine{{65536,46341,0,-46341,-65536,-46341,0,46341}};
    constexpr std::array<std::int32_t,8> sine{{0,46341,65536,46341,0,-46341,-65536,-46341}};
    const auto y_offset=point_offset_y(*slot);
    const auto dx=round_q16_even(-static_cast<std::int64_t>(y_offset)*sine[object_direction]);
    const auto dy=round_q16_even(static_cast<std::int64_t>(y_offset)*cosine[object_direction]);
    const auto x=static_cast<std::int64_t>(object_position.x)+dx;
    const auto y=static_cast<std::int64_t>(object_position.y)+dy;
    if(x<std::numeric_limits<std::int32_t>::min()||x>std::numeric_limits<std::int32_t>::max()||
       y<std::numeric_limits<std::int32_t>::min()||y>std::numeric_limits<std::int32_t>::max())
        throw std::length_error("reference SLOT point exceeds int32");
    return {{static_cast<std::int32_t>(x),static_cast<std::int32_t>(y)},
            static_cast<std::uint8_t>((object_direction+static_cast<std::uint8_t>(*slot->facing))&7U)};
}
SourceSlotPointCandidate enumerate_source_slot_point_candidate(const SlotTable& table,std::size_t index,
    const EntityPosition& object,float radians) {
    const auto* slot=slot_of_type(table,3,index);
    if(table.version!=10||!slot||!source_point_slot_profile(*slot)||object.direction>7||
       object.level<1||!std::isfinite(radians))
        throw std::invalid_argument("source exact SLOT requires finite same-floor non-sitting flags0 geometry");
    const float x=slot_float(slot->offset_bits[0]),y=slot_float(slot->offset_bits[1]);
    const float cosine=static_cast<float>(std::cos(static_cast<double>(radians)));
    const float sine=static_cast<float>(std::sin(static_cast<double>(radians)));
    const float center_x=static_cast<float>(object.x)/16.0f,center_y=static_cast<float>(object.y)/16.0f;
    const float rotated_x=x*cosine-y*sine,rotated_y=x*sine+y*cosine;
    const float point_x=center_x+rotated_x/16.0f,point_y=center_y+rotated_y/16.0f;
    const auto goal_x=source_round_even(static_cast<double>(point_x*16.0f));
    const auto goal_y=source_round_even(static_cast<double>(point_y*16.0f));
    if(!std::isfinite(goal_x)||!std::isfinite(goal_y)||goal_x<INT16_MIN||goal_x>INT16_MAX||
       goal_y<INT16_MIN||goal_y>INT16_MAX)
        throw std::invalid_argument("source exact SLOT coordinate exceeds the bounded signed-short lattice");
    float facing=0;const auto face=*slot->facing;
    if(face==-2)facing=static_cast<float>(std::atan2(center_x-point_x,-(center_y-point_y)));
    else if(face==-1)facing=static_cast<float>(std::atan2(point_x-center_x,-(point_y-center_y)));
    else if(face>=0){
        constexpr double pi=3.141592653589793238462643383279502884;
        const auto relative=(static_cast<unsigned>(face)+object.direction)&7U;
        facing=static_cast<float>((relative>4?static_cast<double>(relative):static_cast<double>(relative)-8.0)*pi/4.0);
    }
    const auto notch=source_direction_notch(facing);
    return {{static_cast<std::int32_t>(goal_x),static_cast<std::int32_t>(goal_y),object.level,notch},
        notch,facing,face==-3};
}
PreRankedSlotRoute find_pre_ranked_slot_route(const RouteGrid& grid,RoutePoint start,
    const std::vector<PreRankedSlotGoal>& goals,RouteLimits limits) {
    PreRankedSlotRoute result{{RouteStatus::invalid_input,{},0,0},std::nullopt};
    if(!grid.walkable(start) || goals.empty()) return result;
    if(goals.size()>limits.candidates) {
        result.path.status=RouteStatus::budget_exhausted;return result;
    }
    std::vector<RoutePoint> positions;
    positions.reserve(goals.size());
    for(std::size_t n=0;n<goals.size();++n) {
        const auto& goal=goals[n];
        if(!grid.contains(goal.position) || goal.entry_flags==0 || goal.facing>=8) {
            result.path={RouteStatus::invalid_input,{},0,0};return result;
        }
        if(goal.sitting_approach&&!goal.sitting_object) {
            result.path={RouteStatus::invalid_input,{},0,0};return result;
        }
        const auto route_position=goal.sitting_approach.value_or(goal.position);
        if(!grid.contains(route_position)) {
            result.path={RouteStatus::invalid_input,{},0,0};return result;
        }
        positions.push_back(route_position);
    }
    auto ranked=find_ranked_route(grid,start,positions,limits);
    result.path=std::move(ranked.path);
    result.selected=ranked.selected;
    return result;
}
namespace {
bool placement_frame_equal(const VmFrame& a,const VmFrame& b) {
    return a.key.scope==b.key.scope&&a.key.id==b.key.id&&a.pc==b.pc&&
        vm_context_equal(a.context,b.context)&&
        a.arguments==b.arguments&&a.locals==b.locals&&a.return_mode==b.return_mode&&
        a.invocation_ticket==b.invocation_ticket&&a.exact_arguments==b.exact_arguments&&a.action_tree==b.action_tree;
}
bool placement_instruction_equal(const Instruction& a,const Instruction& b) {
    return a.opcode==b.opcode&&a.operands==b.operands&&a.on_true==b.on_true&&a.on_false==b.on_false;
}
bool placement_attachment_equal(const std::optional<Attachment>& a,const std::optional<Attachment>& b) {
    return a.has_value()==b.has_value()&&(!a||(a->container==b->container&&a->slot==b->slot&&a->positioned==b->positioned));
}
std::vector<RouteCarriedTargetLink> capture_carried_target(const World& world,const Entity& actor,const Entity& target){
    std::vector<RouteCarriedTargetLink> result;
    const auto* child=&target;
    while(child&&child->id!=actor.id&&child->attachment&&child->position&&result.size()<4096){
        if(std::any_of(result.begin(),result.end(),[&](const auto& link){return link.child.id==child->id;}))break;
        const auto a=*child->attachment;const auto* parent=world.find(a.container);
        if(!parent||a.slot>=parent->slots.size()||parent->slots[a.slot]!=child->id)break;
        result.push_back({{child->id,child->generation},{parent->id,parent->generation},a.slot,*child->position,child->radian_direction,a.positioned});
        child=parent;
    }
    if(!child||child->id!=actor.id||child->generation!=actor.generation)result.clear();
    return result;
}
template<class Route> bool captured_source_goals(const Route& route) noexcept {
    if(route.look_towards)return false;
    const bool choices=route.source_choices.has_value()||(route.source_sparse&&route.source_sparse->choices);
    const bool point=route.arrival_radian_direction&&!route.source_circle&&!route.source_range&&!choices;
    // Older point archives did not retain a non-self target's continuous angle.
    // Keep their conservative transform check rather than inventing that fact.
    if(point)return route.target_anchor_radian_direction.has_value();
    return route.source_sparse.has_value()||choices||route.source_circle.has_value()||route.source_range.has_value();
}
bool target_checkpoint_matches(const World& world,std::int16_t actor,std::uint64_t actor_generation,
        std::int16_t target,std::uint64_t target_generation,const std::vector<RouteCarriedTargetLink>& chain,const EntityPosition& anchor,bool captured_goals=false){
    if(world.reference_is_dead({target,target_generation}))return world.is_live_reference({actor,actor_generation});
    if(!chain.empty())return route_carried_target_matches(world,{actor,actor_generation},{target,target_generation},chain,anchor);
    const auto* e=world.resolve_reference({target,target_generation});
    return e&&e->generation==target_generation&&e->position&&(captured_goals||actor==target||*e->position==anchor);
}
}
bool route_has_captured_source_goals(const LocalRouteSnapshot& route) noexcept {
    return captured_source_goals(route);
}
bool route_carried_target_matches(const World& world,LiveAttemptWalkIdentity actor,LiveAttemptWalkIdentity target,
        const std::vector<RouteCarriedTargetLink>& chain,const EntityPosition& anchor,bool allow_original){
    if(chain.empty()||chain.size()>4096||actor.id==target.id||chain.front().child.id!=target.id||
       chain.front().child.generation!=target.generation||!(chain.front().original==anchor)||
       chain.back().parent.id!=actor.id||chain.back().parent.generation!=actor.generation)return false;
    const bool deleted_target=world.reference_is_dead({target.id,target.generation});
    if(deleted_target&&!world.is_live_reference({actor.id,actor.generation}))return false;
    for(std::size_t n=0;n<chain.size();++n){
        const auto& link=chain[n];
        if(link.child.id<=0||link.parent.id<=0||!link.child.generation||!link.parent.generation||
           !std::isfinite(link.radian_direction)||link.original.direction>7||link.original.x<0||link.original.y<0||
           link.original.x>32767||link.original.y>32767||link.original.level<1||link.child.id==actor.id||
           (n&&(chain[n-1].parent.id!=link.child.id||chain[n-1].parent.generation!=link.child.generation)))return false;
        for(std::size_t p=0;p<n;++p)if(chain[p].child.id==link.child.id)return false;
        const auto* child=world.resolve_reference({link.child.id,link.child.generation});
        const auto* parent=world.resolve_reference({link.parent.id,link.parent.generation});
        if(!child||!parent||link.slot>=parent->slots.size())return false;
        // Delete legitimately detaches a retained target. These are historical
        // request links, not authority to republish or refresh dead children.
        if(deleted_target)continue;
        if(child->generation!=link.child.generation||parent->generation!=link.parent.generation||
           !child->position||!parent->position||!child->attachment||child->attachment->container!=parent->id||
           child->attachment->slot!=link.slot||link.slot>=parent->slots.size()||parent->slots[link.slot]!=child->id||
           child->radian_direction!=link.radian_direction||child->position->direction!=link.original.direction||
           (link.positioned&&!child->attachment->positioned))return false;
        const bool synced=child->position->x==parent->position->x&&child->position->y==parent->position->y&&child->position->level==parent->position->level;
        if(!synced&&!(allow_original&&!link.positioned&&!child->attachment->positioned&&*child->position==link.original))return false;
    }
    return true;
}
struct RoutePlacementProof::State {
    const Vm* vm=nullptr;VmFrame frame;Instruction instruction{};VmDispatch dispatch{};
    bool active=true;std::function<bool()> validate;
};
bool RoutePlacementProof::matches(const VmFrame& frame,const Vm& vm,const Instruction& instruction,
    const VmDispatch& dispatch) const noexcept {
    try {const auto lease=state_.lock();
        return lease&&lease->active&&lease->vm==&vm&&dispatch.origin==&vm&&!dispatch.check_mode&&
            dispatch.ticket==lease->dispatch.ticket&&placement_frame_equal(frame,lease->frame)&&
            placement_instruction_equal(instruction,lease->instruction)&&lease->validate&&lease->validate();
    }catch(...){return false;}
}
struct RouteResourceOwnerProof::State {
    Routing* owner=nullptr;const SourceSparseMotionContext* context=nullptr;
    const void* thread=nullptr;std::function<bool()> current;bool active=true;
};
struct RouteExecutionProof::State {
    const Vm* vm=nullptr;VmFrame frame;Instruction instruction{};VmDispatch dispatch{};
    std::uint64_t run_generation=0;std::size_t stack_depth=0;
    RouteExecutionPhase phase=RouteExecutionPhase::attempt_walk;
    bool active=true;std::function<bool()> validate,restored;
};
bool RouteExecutionProof::matches(const VmFrame& frame,const Vm& vm,const Instruction& instruction,
    const VmDispatch& dispatch,RouteExecutionPhase phase) const noexcept {
    try {const auto lease=state_.lock();
        return lease&&lease->active&&lease->phase==phase&&lease->vm==&vm&&dispatch.origin==&vm&&
            !dispatch.check_mode&&dispatch.check_mode==lease->dispatch.check_mode&&dispatch.ticket==lease->dispatch.ticket&&
            vm.run_generation()==lease->run_generation&&vm.stack().size()==lease->stack_depth&&
            placement_frame_equal(frame,lease->frame)&&placement_instruction_equal(instruction,lease->instruction)&&
            lease->validate&&lease->validate();
    }catch(...){return false;}
}
bool RouteExecutionProof::matches_after_restore(const VmFrame& frame,const Vm& vm,
    const Instruction& instruction,const VmDispatch& dispatch,RouteExecutionPhase phase) const noexcept {
    try {const auto lease=state_.lock();
        return matches(frame,vm,instruction,dispatch,phase)&&lease&&lease->restored&&lease->restored();
    }catch(...){return false;}
}
bool Routing::placement_source_matches(const VmFrame& frame,const Instruction& instruction,
    const VmDispatch& dispatch,bool replay) const noexcept {
    if(!vm_||(instruction.opcode!=45&&!source_exact_relative(instruction)&&
       !is_source_ranged_goto_relative_instruction(instruction)&&!is_source_ordinary_go_here_instruction(instruction))||
       dispatch.origin!=vm_||dispatch.check_mode||!dispatch.ticket||vm_->stack().empty())return false;
    if(vm_->matches_active_primitive(frame,instruction,dispatch))return true;
    if(!replay||!replay_placement_authorization_||vm_->dispatch_active()||vm_->status()!=VmStatus::waiting||
       replay_placement_authorization_->first!=vm_->run_generation()||replay_placement_authorization_->second!=dispatch.ticket||
       !vm_->pending()||vm_->pending()->ticket!=dispatch.ticket||!placement_frame_equal(frame,vm_->stack().back()))return false;
    const auto& pending=*vm_->pending();const auto& trace=pending.instruction;
    return trace.depth==vm_->stack().size()&&trace.key.scope==frame.key.scope&&trace.key.id==frame.key.id&&
        trace.pc==frame.pc&&trace.code_owner==frame.context.code_owner&&
        vm_context_equal(pending.context,frame.context)&&
        placement_instruction_equal(trace.instruction,instruction);
}
bool Routing::route_execution_source_matches(const VmFrame& frame,const Instruction& instruction,
    const VmDispatch& dispatch,RouteExecutionPhase phase) const noexcept {
    if(!vm_||(instruction.opcode!=45&&instruction.opcode!=27)||dispatch.origin!=vm_||
       dispatch.check_mode||!dispatch.ticket)return false;
    const bool pending_walk=phase==RouteExecutionPhase::attempt_walk&&job_&&job_->source_sparse&&
        job_->source_sparse->walk_to_pending&&job_->frame.state==RouteFrameState::walking;
    const bool collision_replan=phase==RouteExecutionPhase::attempt_walk&&job_&&job_->source_sparse&&
        job_->source_sparse->collision&&job_->source_sparse->collision->phase==SourceRouteCollisionPhase::reroute_pending&&
        job_->source_motion&&job_->source_motion->phase()==WalkMotionPhase::collision_required;
    // A failed EP26 may select the next frozen standing goal before any
    // sparse WalkTo exists. Authenticate that exact pending route, not an
    // active-dispatch token or a fabricated legacy motion snapshot.
    const bool architecture_replan=phase==RouteExecutionPhase::attempt_walk&&job_&&job_->room_route_replan_active&&
        job_->frame.state==RouteFrameState::initial&&!job_->external_frame_active;
    const bool selected_replan=phase==RouteExecutionPhase::attempt_walk&&job_&&general_slot_provider_&&
        job_->source_choices&&(!job_->source_choices->selected.chair||job_->source_choices->chair_attempted)&&
        !job_->source_sparse&&!job_->source_motion&&!job_->external_frame_active&&
        job_->frame.state==RouteFrameState::initial;
    if(phase==RouteExecutionPhase::attempt_walk&&!collision_replan&&!pending_walk&&!selected_replan&&!architecture_replan){
        if(vm_->matches_active_primitive(frame,instruction,dispatch))return true;
        if(!replay_placement_authorization_||vm_->dispatch_active()||vm_->status()!=VmStatus::waiting||
           replay_placement_authorization_->first!=vm_->run_generation()||
           replay_placement_authorization_->second!=dispatch.ticket||!vm_->pending()||
           vm_->pending()->ticket!=dispatch.ticket||vm_->stack().empty()||
           !placement_frame_equal(frame,vm_->stack().back()))return false;
        const auto& pending=*vm_->pending();const auto& trace=pending.instruction;
        return trace.depth==vm_->stack().size()&&trace.key.scope==frame.key.scope&&trace.key.id==frame.key.id&&
            trace.pc==frame.pc&&trace.code_owner==frame.context.code_owner&&
            vm_context_equal(pending.context,frame.context)&&
            placement_instruction_equal(trace.instruction,instruction);
    }
    if((phase!=RouteExecutionPhase::set_position&&!collision_replan&&!pending_walk&&!selected_replan&&!architecture_replan)||!job_||job_->look_towards||
       job_->ticket!=dispatch.ticket||job_->run_generation!=vm_->run_generation()||
       vm_->dispatch_active()||vm_->status()!=VmStatus::waiting||!vm_->pending()||
       vm_->pending()->ticket!=dispatch.ticket||vm_->stack().empty())return false;
    const auto& pending=*vm_->pending();const auto& trace=pending.instruction;
    return trace.depth==vm_->stack().size()&&trace.key.scope==frame.key.scope&&trace.key.id==frame.key.id&&
        trace.pc==frame.pc&&placement_frame_equal(frame,vm_->stack().back())&&
        trace.code_owner==frame.context.code_owner&&vm_context_equal(pending.context,frame.context)&&placement_instruction_equal(trace.instruction,instruction)&&
        frame.context.caller==job_->actor&&frame.context.stack_object==job_->target;
}
std::optional<std::string> Routing::invoke_route_execution_callback(RouteExecutionPhase phase,
    const VmFrame& frame,const Instruction& instruction,const VmDispatch& dispatch,
    std::int16_t actor_id,std::uint64_t actor_generation,std::int16_t target_id,
    std::uint64_t target_generation,const EntityPosition& actor_position,
    const EntityPosition& target_position,RoutePoint goal,std::size_t candidate_index) {
    if(phase!=RouteExecutionPhase::attempt_walk&&phase!=RouteExecutionPhase::set_position)
        return "unknown route execution callback phase";
    const auto& callback=phase==RouteExecutionPhase::attempt_walk?attempt_walk_execution_:set_position_execution_;
    const bool has_sparse_place=phase==RouteExecutionPhase::set_position&&job_&&job_->source_sparse&&
        bool(source_sparse_services_.place);
    if(!callback&&!has_sparse_place)return {};
    if(!route_execution_source_matches(frame,instruction,dispatch,phase))
        return "source route execution context is not an authenticated active/pending 45/27 operation";
    const auto* actor=world_.find(actor_id);const auto* target=world_.resolve_reference({target_id,target_generation});
    if(!actor||!target||actor->generation!=actor_generation||target->generation!=target_generation||
       !actor->position||!target->position||!(*actor->position==actor_position)||
       !(*target->position==target_position))
        return "source route execution participant identity changed before callback";
    const auto actor_attachment=actor->attachment,target_attachment=target->attachment;
    const auto actor_radian=actor->radian_direction,target_radian=target->radian_direction;
    auto lease=std::make_shared<RouteExecutionProof::State>();
    lease->vm=vm_;lease->frame=frame;lease->instruction=instruction;lease->dispatch=dispatch;
    lease->run_generation=vm_->run_generation();lease->stack_depth=vm_->stack().size();lease->phase=phase;
    std::weak_ptr<RouteExecutionProof::State> weak=lease;
    const auto actor_transform_matches=[&,weak,phase] (bool restored) {
        const auto state=weak.lock();const auto* current=world_.find(actor_id);
        if(!state||!state->active||!current||current->generation!=actor_generation||!current->position||
           current->position->x!=actor_position.x||current->position->y!=actor_position.y||
           current->position->level!=actor_position.level||
           !placement_attachment_equal(current->attachment,actor_attachment))return false;
        const bool original=current->position->direction==actor_position.direction&&current->radian_direction==actor_radian;
        if(restored||phase==RouteExecutionPhase::attempt_walk)return original;
        const bool north=current->position->direction==0&&current->radian_direction==0.0f;
        return original||north;
    };
    const auto target_matches=[&,weak] {
        const auto state=weak.lock();const auto* current=world_.resolve_reference({target_id,target_generation});
        if(!state||!state->active||!current||current->generation!=target_generation)return false;
        if(target_id==actor_id)return true;
        if(!current->position||
           !(*current->position==target_position)||!placement_attachment_equal(current->attachment,target_attachment))
            return false;
        return current->kind!=EntityKind::avatar||current->radian_direction==target_radian;
    };
    lease->validate=[this,weak,phase,&frame,&instruction,&dispatch,&actor_transform_matches,&target_matches] {
        const auto state=weak.lock();
        return state&&state->active&&vm_&&vm_->run_generation()==state->run_generation&&
            vm_->stack().size()==state->stack_depth&&
            route_execution_source_matches(frame,instruction,dispatch,phase)&&
            actor_transform_matches(false)&&target_matches();
    };
    lease->restored=[&actor_transform_matches]{return actor_transform_matches(true);};
    RouteExecutionContext context{vm_,frame,instruction,dispatch,phase,RouteExecutionProof(lease),
        actor_id,target_id,actor_generation,target_generation,actor_position,target_position,goal,candidate_index};
    struct LeaseEnd {std::shared_ptr<RouteExecutionProof::State>& state;~LeaseEnd(){state->active=false;}} lease_end{lease};
    const bool was_executing=executing_;executing_=true;
    try {
        if(callback)callback(context);
        if(source_reset_retired_)throw SourceCheckResetRetirement(vm_);
        if(has_sparse_place){
            auto candidate=*actor->position;candidate.x=goal.x;candidate.y=goal.y;candidate.direction=0;
            last_sparse_placement_=source_sparse_services_.place(context,candidate);
            if(source_reset_retired_)throw SourceCheckResetRetirement(vm_);
        }
    }
    catch(const SourceCheckResetRetirement&){executing_=was_executing;throw;}
    catch(const std::exception& error){executing_=was_executing;return std::string("source route execution callback failed: ")+error.what();}
    catch(...){executing_=was_executing;return "source route execution callback raised a non-standard exception";}
    executing_=was_executing;
    if(!context.proof.matches_after_restore(frame,*vm_,instruction,dispatch,phase))
        return "source route execution callback changed or failed to restore its participants";
    return {};
}
std::optional<SourceSparsePlacementOutcome> Routing::publish_route_position(const EntityPosition& destination,
        RoutePoint goal,std::size_t candidate_index,bool existing_check,std::string* error){
    const auto reject=[&](const std::string& reason)->std::optional<SourceSparsePlacementOutcome>{if(error)*error=reason;return {};};
    if(!position_publication_||!job_||!vm_||!vm_->pending()||vm_->stack().empty())return reject("route position publication lacks an actual pending source job");
    const auto frame=vm_->stack().back();const auto instruction=vm_->pending()->instruction.instruction;
    const VmDispatch dispatch{vm_,job_->ticket,false};
    if(!route_execution_source_matches(frame,instruction,dispatch,RouteExecutionPhase::set_position))return reject("route position publication source capability is stale");
    const auto* actor=world_.find(job_->actor);const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!target||!actor->position||!target->position||actor->generation!=job_->actor_generation||target->generation!=job_->target_generation)
        return reject("route position publication participant is unavailable");
    struct Publication {
        struct CarriedLink {
            std::int16_t id=0;std::uint64_t generation=0,parent_generation=0;
            EntityPosition position{};Attachment attachment{};float radians=0;
        };
        std::shared_ptr<RouteExecutionProof::State> proof;
        std::int16_t actor=0,target=0;std::uint64_t actor_generation=0,target_generation=0,ticket=0;
        EntityPosition old_position{},new_position{},target_position{};
        std::optional<Attachment> actor_attachment,target_attachment;
        float old_radians=0,target_radians=0;
        bool active=true,publishing=false,published=false,checked=false,invalid=false,has_check=false;
        std::optional<SourceSparsePlacementOutcome> checked_outcome;
        std::string diagnostic;
        std::vector<CarriedLink> carried_target;
    };
    auto state=std::make_shared<Publication>();state->proof=std::make_shared<RouteExecutionProof::State>();
    if(!job_->carried_target.empty()&&!route_carried_target_matches(world_,{actor->id,actor->generation},
        {target->id,target->generation},job_->carried_target,job_->target_position))
        return reject("route carried target differs from its frozen request ancestry");
    state->actor=actor->id;state->target=target->id;state->actor_generation=actor->generation;state->target_generation=target->generation;state->ticket=job_->ticket;
    state->old_position=*actor->position;state->new_position=destination;state->target_position=*target->position;
    state->actor_attachment=actor->attachment;state->target_attachment=target->attachment;state->old_radians=actor->radian_direction;state->target_radians=target->radian_direction;
    // Capture only the actual target's reciprocal ancestry, before Pre/check.
    // A carried routing target follows its actor during ordered PositionChange;
    // an unrelated moving target never receives this publication capability.
    if(target->id!=actor->id){
        std::vector<Publication::CarriedLink> chain;
        const auto* child=target;
        while(child&&child->id!=actor->id&&child->attachment&&child->position&&chain.size()<4096){
            if(std::any_of(chain.begin(),chain.end(),[&](const auto& link){return link.id==child->id;}))break;
            const auto attachment=*child->attachment;const auto* parent=world_.find(attachment.container);
            if(!parent||attachment.slot>=parent->slots.size()||parent->slots[attachment.slot]!=child->id)break;
            chain.push_back({child->id,child->generation,parent->generation,*child->position,attachment,child->radian_direction});
            child=parent;
        }
        if(child&&child->id==actor->id&&child->generation==actor->generation)state->carried_target=std::move(chain);
    }
    state->has_check=existing_check&&(bool(set_position_execution_)||(job_->source_sparse&&bool(source_sparse_services_.place)));
    auto& proof=*state->proof;proof.vm=vm_;proof.frame=frame;proof.instruction=instruction;proof.dispatch=dispatch;
    proof.run_generation=vm_->run_generation();proof.stack_depth=vm_->stack().size();proof.phase=RouteExecutionPhase::set_position;
    std::weak_ptr<Publication> weak=state;
    const auto matches=[this,weak](bool restored){
        const auto lease=weak.lock();if(!lease||!lease->active||lease->invalid||!job_||vm_!=lease->proof->vm||job_->ticket!=lease->ticket||
           job_->actor!=lease->actor||job_->target!=lease->target||job_->actor_generation!=lease->actor_generation||job_->target_generation!=lease->target_generation||
           job_->run_generation!=lease->proof->run_generation||!route_execution_source_matches(lease->proof->frame,lease->proof->instruction,lease->proof->dispatch,RouteExecutionPhase::set_position))return false;
        const auto* current=world_.find(lease->actor);const auto* other=world_.resolve_reference({lease->target,lease->target_generation});
        if(!current||!other||current->generation!=lease->actor_generation||other->generation!=lease->target_generation||!current->position||!other->position||
           !placement_attachment_equal(current->attachment,lease->actor_attachment))return false;
        if(lease->carried_target.empty()&&!placement_attachment_equal(other->attachment,lease->target_attachment))return false;
        const auto old_pose=[&]{
            const auto& expected=lease->old_position;
            if(current->position->x!=expected.x||current->position->y!=expected.y||current->position->level!=expected.level)return false;
            const bool original=*current->position==expected&&current->radian_direction==lease->old_radians;
            const bool north=!restored&&current->position->direction==0&&current->radian_direction==0.f;
            return original||north;
        };
        if(lease->published){if(!(*current->position==lease->new_position))return false;}
        else if(lease->publishing){
            // Only the fixed scalar publisher owns this transition. Its room
            // resolver observes the old pose, and its actual spatial observer
            // observes the newly published pose before the call returns.
            constexpr float pi_over_four=0.78539816339744830962f;
            const auto new_radians=current->kind==EntityKind::avatar?float(lease->new_position.direction)*pi_over_four:lease->old_radians;
            const bool new_pose=*current->position==lease->new_position&&current->radian_direction==new_radians;
            if(!old_pose()&&!new_pose)return false;
        }else if(!old_pose())return false;
        if(!lease->carried_target.empty()){
            for(const auto& link:lease->carried_target){
                const auto* child=world_.find(link.id);const auto* parent=world_.find(link.attachment.container);
                if(!child||!parent||child->generation!=link.generation||parent->generation!=link.parent_generation||
                   !child->position||!child->attachment||child->attachment->container!=link.attachment.container||
                   child->attachment->slot!=link.attachment.slot||link.attachment.slot>=parent->slots.size()||
                   parent->slots[link.attachment.slot]!=child->id||child->radian_direction!=link.radians)return false;
                const bool original=*child->position==link.position;
                auto predicted=lease->new_position;predicted.direction=link.position.direction;
                const bool refreshed=lease->published&&*child->position==predicted&&parent->position&&
                    parent->position->x==predicted.x&&parent->position->y==predicted.y&&parent->position->level==predicted.level;
                if(!original&&!refreshed)return false;
                // refresh_contained_position marks this exact existing link
                // positioned. It cannot unposition a link or reparent a child.
                if(child->attachment->positioned!=link.attachment.positioned&&
                   (link.attachment.positioned||!child->attachment->positioned||!refreshed))return false;
            }
        }else if(lease->target!=lease->actor&&(!(*other->position==lease->target_position)||
           (other->kind==EntityKind::avatar&&other->radian_direction!=lease->target_radians)))return false;
        return true;
    };
    proof.validate=[matches]{return matches(false);};proof.restored=[matches]{return matches(true);};
    struct End {std::shared_ptr<Publication>& state;~End(){state->active=false;state->proof->active=false;}}end{state};
    RoutePositionPublicationContext context;
    context.execution={vm_,frame,instruction,dispatch,RouteExecutionPhase::set_position,RouteExecutionProof(state->proof),
        state->actor,state->target,state->actor_generation,state->target_generation,state->old_position,state->target_position,goal,candidate_index};
    context.old_position=state->old_position;context.new_position=destination;context.has_placement_check=state->has_check;
    context.source_still_matches=[matches]{return matches(false);};
    context.check_position=[this,weak,matches,goal,candidate_index]()->std::optional<SourceSparsePlacementOutcome>{
        const auto lease=weak.lock();if(!lease||!lease->active)return {};
        if(!matches(true)||lease->publishing||lease->published||lease->checked||!lease->has_check){lease->invalid=true;return {};}
        lease->checked=true;
        if(job_->source_sparse)last_sparse_placement_.reset();
        if(auto failure=invoke_route_execution_callback(RouteExecutionPhase::set_position,lease->proof->frame,lease->proof->instruction,lease->proof->dispatch,
            lease->actor,lease->actor_generation,lease->target,lease->target_generation,lease->old_position,lease->target_position,goal,candidate_index)){
            lease->diagnostic=*failure;return {};}
        if(!matches(true)){lease->diagnostic="route PositionValid failed to restore source participants";return {};}
        if(job_->source_sparse&&source_sparse_services_.place)lease->checked_outcome=last_sparse_placement_;
        else{SourceSparsePlacementOutcome accepted;accepted.status=SourceSparsePlacementStatus::success;lease->checked_outcome=accepted;}
        return lease->checked_outcome;
    };
    context.publish_position=[this,weak,matches]{
        const auto lease=weak.lock();if(!lease||!lease->active)return false;
        const auto admitted=[](const auto& value){return value.status==SourceSparsePlacementStatus::success;};
        if(!matches(true)||lease->publishing||lease->published||(lease->has_check&&(!lease->checked_outcome||!admitted(*lease->checked_outcome)))){lease->invalid=true;return false;}
        lease->publishing=true;
        struct PublishEnd {bool& flag;~PublishEnd(){flag=false;}}publish_end{lease->publishing};
        if(!world_.place_individual_on_floor(lease->actor,lease->new_position,lease->new_position.direction))return false;
        lease->published=true;return matches(true);
    };
    std::optional<SourceSparsePlacementOutcome> result;
    try{result=position_publication_(context);}catch(const SourceCheckResetRetirement&){throw;}
    catch(const std::exception& exception){return reject(std::string("route position lifecycle callback failed: ")+exception.what());}
    catch(...){return reject("route position lifecycle callback threw");}
    if(source_reset_retired_)throw SourceCheckResetRetirement(vm_);
    if(!result)return reject(state->diagnostic.empty()?"route position lifecycle is unavailable":state->diagnostic);
    const auto status=result->status;const bool admitted=status==SourceSparsePlacementStatus::success;
    const bool blocked=status==SourceSparsePlacementStatus::blocked_object||status==SourceSparsePlacementStatus::blocked_avatar||status==SourceSparsePlacementStatus::cant_be_through_wall;
    if(!admitted&&!blocked&&status!=SourceSparsePlacementStatus::unsupported)return reject("route position lifecycle returned invalid placement status");
    if(state->has_check&&(!state->checked||!state->checked_outcome||state->checked_outcome->status!=status||
       state->checked_outcome->blocker!=result->blocker||state->checked_outcome->blocker_generation!=result->blocker_generation))return reject("route position lifecycle changed or skipped the actual placement verdict");
    if(state->invalid||!matches(true)||((admitted||blocked)&&state->published!=admitted))return reject("route position lifecycle did not preserve its exact source publication");
    if(admitted&&!job_->carried_target.empty()&&!route_carried_target_matches(world_,{state->actor,state->actor_generation},
        {state->target,state->target_generation},job_->carried_target,job_->target_position,false))
        return reject("route position lifecycle did not finish its carried target refresh");
    if(status==SourceSparsePlacementStatus::unsupported&&state->published)return reject("unsupported route lifecycle published a position");
    return result;
}
std::optional<std::vector<LiveAttemptWalkIdentity>> Routing::capture_source_sparse_consideration(
    const VmFrame& frame,const Instruction& instruction,const VmDispatch& dispatch,
    std::int16_t actor_id,std::uint64_t actor_generation,std::int16_t target_id,
    std::uint64_t target_generation,const EntityPosition& actor_position,
    const EntityPosition& target_position,
    std::optional<std::vector<LiveAttemptWalkIdentity>> inherited) {
    if(!source_sparse_services_.capture_avatars_to_consider)return {};
    if(!route_execution_source_matches(frame,instruction,dispatch,RouteExecutionPhase::attempt_walk))
        return {};
    const auto* actor=world_.find(actor_id);const auto* target=world_.resolve_reference({target_id,target_generation});
    if(!actor||!target||actor->generation!=actor_generation||target->generation!=target_generation||
       !actor->position||!target->position||!(*actor->position==actor_position)||!(*target->position==target_position))return {};
    const auto actor_attachment=actor->attachment,target_attachment=target->attachment;
    const auto actor_radian=actor->radian_direction,target_radian=target->radian_direction;
    auto lease=std::make_shared<RouteExecutionProof::State>();
    lease->vm=vm_;lease->frame=frame;lease->instruction=instruction;lease->dispatch=dispatch;
    lease->run_generation=vm_->run_generation();lease->stack_depth=vm_->stack().size();
    lease->phase=RouteExecutionPhase::attempt_walk;
    std::weak_ptr<RouteExecutionProof::State> weak=lease;
    lease->validate=[this,weak,frame,instruction,dispatch,actor_id,actor_generation,target_id,target_generation,
        actor_position,target_position,actor_attachment,target_attachment,actor_radian,target_radian]{
        const auto state=weak.lock();const auto* a=world_.find(actor_id);const auto* t=world_.resolve_reference({target_id,target_generation});
        return state&&state->active&&vm_&&vm_->run_generation()==state->run_generation&&
            vm_->stack().size()==state->stack_depth&&route_execution_source_matches(frame,instruction,dispatch,RouteExecutionPhase::attempt_walk)&&
            a&&t&&a->generation==actor_generation&&t->generation==target_generation&&a->position&&t->position&&
            *a->position==actor_position&&*t->position==target_position&&a->radian_direction==actor_radian&&
            (t->kind!=EntityKind::avatar||t->radian_direction==target_radian)&&
            placement_attachment_equal(a->attachment,actor_attachment)&&placement_attachment_equal(t->attachment,target_attachment);
    };
    lease->restored=lease->validate;
    RouteExecutionContext context{vm_,frame,instruction,dispatch,RouteExecutionPhase::attempt_walk,
        RouteExecutionProof(lease),actor_id,target_id,actor_generation,target_generation,actor_position,target_position,
        {actor_position.x,actor_position.y},0};
    struct LeaseEnd {std::shared_ptr<RouteExecutionProof::State>& state;~LeaseEnd(){state->active=false;}} end{lease};
    const bool was_executing=executing_;executing_=true;
    std::optional<std::vector<LiveAttemptWalkIdentity>> result;
    try {result=source_sparse_services_.capture_avatars_to_consider(context,inherited);}
    catch(const std::exception& error){diagnostic_=std::string("source avatar consideration capture failed: ")+error.what();}
    catch(...){diagnostic_="source avatar consideration capture raised a non-standard exception";}
    executing_=was_executing;
    if(!context.proof.matches_after_restore(frame,*vm_,instruction,dispatch,RouteExecutionPhase::attempt_walk)){
        diagnostic_="source avatar consideration capture changed route execution context";return {};
    }
    if(!result){diagnostic_="source avatar consideration capture is unknown";return {};}
    if(result->size()>4096||!std::is_sorted(result->begin(),result->end(),[](const auto& a,const auto& b){return a.id<b.id;})||
       std::adjacent_find(result->begin(),result->end(),[](const auto& a,const auto& b){return a.id==b.id;})!=result->end()||
       std::any_of(result->begin(),result->end(),[](const auto& item){return item.id<=0||item.generation==0;})){
        diagnostic_="source avatar consideration capture returned invalid identities/order";return {};
    }
    if(inherited&&(result->size()!=inherited->size()||!std::equal(result->begin(),result->end(),inherited->begin(),
        [](const auto& a,const auto& b){return a.id==b.id&&a.generation==b.generation;}))){
        diagnostic_="source nested route changed inherited avatars-to-consider set";return {};
    }
    return result;
}
std::optional<SourceWalkPlanResult> Routing::source_sparse_plan(const VmFrame& frame,
    const Instruction& instruction,const VmDispatch& dispatch,std::int16_t actor_id,
    std::uint64_t actor_generation,std::int16_t target_id,std::uint64_t target_generation,
    const EntityPosition& actor_position,const EntityPosition& target_position,RoutePoint goal,
    std::size_t candidate_index,std::optional<float> outgoing_direction,
    std::optional<std::vector<LiveAttemptWalkIdentity>> inherited_consideration,WalkMotionProfile& profile,
    std::vector<LiveAttemptWalkIdentity>& avatars_to_consider) {
    if(!source_sparse_services_.plan_input)return std::nullopt;
    if(!route_execution_source_matches(frame,instruction,dispatch,RouteExecutionPhase::attempt_walk)){
        diagnostic_="sparse AttemptWalk input lacks an authenticated active or replay source";return std::nullopt;
    }
    const auto* actor=world_.find(actor_id);const auto* target=world_.resolve_reference({target_id,target_generation});
    if(!actor||!target||actor->generation!=actor_generation||target->generation!=target_generation||
       !actor->position||!target->position||!(*actor->position==actor_position)||!(*target->position==target_position)){
        diagnostic_="sparse AttemptWalk participants changed before planning";return std::nullopt;
    }
    const auto actor_attachment=actor->attachment,target_attachment=target->attachment;
    const auto actor_radian=actor->radian_direction,target_radian=target->radian_direction;
    auto lease=std::make_shared<RouteExecutionProof::State>();
    lease->vm=vm_;lease->frame=frame;lease->instruction=instruction;lease->dispatch=dispatch;
    lease->run_generation=vm_->run_generation();lease->stack_depth=vm_->stack().size();
    lease->phase=RouteExecutionPhase::attempt_walk;
    std::weak_ptr<RouteExecutionProof::State> weak=lease;
    lease->validate=[this,weak,frame,instruction,dispatch,actor_id,actor_generation,target_id,
        target_generation,actor_position,target_position,actor_radian,target_radian,
        actor_attachment,target_attachment] {
        const auto state=weak.lock();const auto* a=world_.find(actor_id);const auto* t=world_.resolve_reference({target_id,target_generation});
        return state&&state->active&&vm_&&vm_->run_generation()==state->run_generation&&
            vm_->stack().size()==state->stack_depth&&
            route_execution_source_matches(frame,instruction,dispatch,RouteExecutionPhase::attempt_walk)&&
            a&&t&&a->generation==actor_generation&&t->generation==target_generation&&a->position&&t->position&&
            *a->position==actor_position&&*t->position==target_position&&a->radian_direction==actor_radian&&
            (t->kind!=EntityKind::avatar||t->radian_direction==target_radian)&&
            placement_attachment_equal(a->attachment,actor_attachment)&&
            placement_attachment_equal(t->attachment,target_attachment);
    };
    lease->restored=lease->validate;
    RouteExecutionContext context{vm_,frame,instruction,dispatch,RouteExecutionPhase::attempt_walk,
        RouteExecutionProof(lease),actor_id,target_id,actor_generation,target_generation,
        actor_position,target_position,goal,candidate_index};
    struct LeaseEnd {std::shared_ptr<RouteExecutionProof::State>& state;~LeaseEnd(){state->active=false;}} lease_end{lease};
    const bool was_executing=executing_;executing_=true;
    std::optional<SourceWalkPlanResult> result;
    try {
        auto input=source_sparse_services_.plan_input(context,goal,outgoing_direction,inherited_consideration);
        if(input){
            if(inherited_consideration&&(input->avatars_to_consider.size()!=inherited_consideration->size()||
               !std::equal(input->avatars_to_consider.begin(),input->avatars_to_consider.end(),
                   inherited_consideration->begin(),[](const auto& a,const auto& b){return a.id==b.id&&a.generation==b.generation;})))
                throw std::logic_error("source avatars-to-consider set changed from its inherited route snapshot");
            const auto previous_context_check=input->services.context_still_matches;
            input->services.context_still_matches=[previous_context_check,&context,vm=vm_,frame,instruction,dispatch]{
                if(!context.proof.matches(frame,*vm,instruction,dispatch,RouteExecutionPhase::attempt_walk))return false;
                return !previous_context_check||previous_context_check();
            };
            input->request.actor={actor_id,actor_generation};
            input->request.destination={goal.x,goal.y};
            // A new nested request sees job_ as its nearest parent. During
            // private replay job_ is the current route, so consult its parent.
            const auto* incoming_parent=job_&&job_->ticket!=dispatch.ticket?&job_->frame:
                (suspended_jobs_.empty()?nullptr:&suspended_jobs_.back().frame);
            input->request.incoming_direction=route_incoming_direction(
                job_?job_->frame:RouteFrameContext{},incoming_parent,actor_radian);
            input->request.outgoing_direction=outgoing_direction;
            profile=input->profile;
            avatars_to_consider=input->avatars_to_consider;
            result=plan_source_walk(world_,input->request,input->services);
        }
    }
    catch(const std::exception& error){diagnostic_=std::string("sparse AttemptWalk input failed: ")+error.what();}
    catch(...){diagnostic_="sparse AttemptWalk input raised a non-standard exception";}
    executing_=was_executing;
    if(!context.proof.matches_after_restore(frame,*vm_,instruction,dispatch,RouteExecutionPhase::attempt_walk)){
        diagnostic_="sparse AttemptWalk input changed source state or participants";return std::nullopt;
    }
    return result;
}
bool is_source_slot_range(const SlotRecord& slot) {
    constexpr std::uint32_t known=0xffU|256U|512U|1024U|2048U|8192U|16384U|32768U|(1U<<29)|(1U<<30);
    if(!slot.proximity)return false;
    const auto min=(*slot.proximity)[0],max=(*slot.proximity)[1]==0?min:(*slot.proximity)[1];
    return slot.type==3&&(slot.flags&255U)!=0&&(slot.flags&~known)==0&&
        slot.offset_bits==std::array<std::uint32_t,3>{0,0,0}&&slot.standing>0&&slot.standing<=32767&&
        slot.sitting==0&&slot.ground==0&&slot.snap_target_slot<0&&min>0&&max>min&&max<=4096&&
        (*slot.proximity)[2]>=0&&(*slot.proximity)[2]<=4096&&slot.resolution&&*slot.resolution>0&&
        (slot.facing==-2||slot.facing==-3||(slot.facing&&*slot.facing>=0&&*slot.facing<=7));
}
bool is_source_standing_range_slot(const SlotRecord& slot) {return is_source_slot_range(slot);}
std::vector<SourceSlotCircleGoal> enumerate_source_slot_range_candidates(const SlotTable& table,std::size_t index,
    const EntityPosition& target,float angle) {
    const auto* slot=slot_of_type(table,3,index);
    if(table.version!=10||!slot||!is_source_slot_range(*slot)||!std::isfinite(angle)||target.direction>7||
       target.x<INT16_MIN||target.x>INT16_MAX||target.y<INT16_MIN||target.y>INT16_MAX||target.level<1||target.level>127)
        throw std::invalid_argument("source range manifest requires exact bounded standing geometry");
    const auto offsets=filter_reference_slot_range_proximity(table,index,enumerate_reference_slot_range_lattice(table,index,4096));
    std::vector<SourceSlotCircleGoal> result;result.reserve(offsets.size());
    for(const auto& offset:offsets){
        const auto x=static_cast<std::int64_t>(target.x)+offset.x,y=static_cast<std::int64_t>(target.y)+offset.y;
        if(x<INT16_MIN||x>INT16_MAX||y<INT16_MIN||y>INT16_MAX)throw std::invalid_argument("source range exceeds LotTilePos coordinates");
        const RoutePoint point{static_cast<std::int32_t>(x),static_cast<std::int32_t>(y)};
        const auto entry=reference_slot_entry_flags({target.x,target.y},target.direction,point,
            static_cast<std::uint8_t>(slot->flags),(slot->flags&512U)?0.0f:angle);
        if(!entry)continue;
        const auto facing=source_slot_facing_radians(*slot,target,point);
        result.push_back({point,entry,source_direction_notch(facing),facing});
    }
    return result;
}
bool Routing::bind(Vm& vm) {
    if(executing_ || (vm_ && vm_!=&vm)) return false;
    vm_=&vm;return true;
}
VmPrimitive Routing::primitive() {
    return [this](const auto& frame,const auto& instruction,const auto&,const VmDispatch& dispatch) {
        std::optional<RouteOutcomeReceipt> immediate;
        if(outcome_observer_){try{
            immediate.emplace();auto& row=*immediate;row.ticket=dispatch.ticket;
            row.run_generation=dispatch.origin?dispatch.origin->run_generation():0;
            row.source_available=true;row.owner=frame.context.code_owner;row.routine=frame.key;
            row.pc=frame.pc;row.instruction=instruction;row.source_depth=dispatch.origin?dispatch.origin->stack().size():0;
            row.actor=vm_context_reference(frame.context,VmEntityRole::caller);
            row.target=vm_context_reference(frame.context,VmEntityRole::stack_object);
        }catch(...){immediate.reset();}}
        const auto outcome=request(frame,instruction,dispatch,immediate?&*immediate:nullptr);
        if(immediate&&(outcome.disposition==PrimitiveDisposition::goto_true||outcome.disposition==PrimitiveDisposition::goto_false)){
            try{auto& row=*immediate;row.result=outcome.disposition==PrimitiveDisposition::goto_true;
                // The primitive returned its branch; no pending VM completion was attempted.
                row.reason=outcome.diagnostic;
                const auto facts=[&](WorldEntityReference ref,std::optional<EntityPosition>& position,std::optional<std::int16_t>& room){
                    if(const auto* entity=world_.resolve_reference(ref)){position=entity->position;
                        if(const auto at=entity->object_words.find(29);at!=entity->object_words.end())room=at->second;}
                };
                facts(row.actor,row.actor_position,row.actor_object_room);facts(row.target,row.target_position,row.target_object_room);
                emit_outcome(row);
            }catch(...){}}
        return outcome;
    };
}
std::optional<RouteFrameContext> Routing::active_frame_context() const {
    if(!job_) return {};
    return job_->frame;
}
bool is_source_standing_before_walk(const LocalRouteSnapshot& route) noexcept {
    return route.source_choices&&!route.source_sparse&&
        (!route.source_choices->selected.chair||route.source_choices->chair_attempted)&&route.path.size()==1&&route.next==1&&
        route.path.front()==RoutePoint{route.expected.x,route.expected.y}&&
        (route.frame.state==RouteFrameState::initial||route.frame.state==RouteFrameState::stand_func);
}
bool Routing::validate_source_slot_choices(const Job& job,std::string* why) const {
    const auto reject=[&](const char* text){if(why)*why=text;return false;};
    if(!job.source_choices)return true;
    if(!vm_||(!general_slot_provider_&&!job.failure_pending&&!job.failure_tree_invoked))
        return reject("source choices require the general provider or a retained failed attempt");
    const auto& choices=*job.source_choices;
    if(job.source_circle||job.source_range||job.look_towards||choices.slot.type!=3||choices.slot_version<4||choices.slot_version>10||
       choices.slot_index>65535||choices.remaining.size()>4095||!std::isfinite(choices.target_radian_direction))return reject("source choice manifest metadata is invalid");
    const VmPending* pending=nullptr;
    const auto take=[&](const VmPending& value){if(value.ticket!=job.ticket)return true;if(pending)return false;pending=&value;return true;};
    if(vm_->pending()&&!take(*vm_->pending()))return reject("source choices repeat a pending ticket");
    for(const auto& value:vm_->suspended_pending())if(!take(value))return reject("source choices repeat a suspended ticket");
    for(const auto& value:vm_->immediate_continuations())if(value.parent_pending&&!take(*value.parent_pending))return reject("source choices repeat an immediate ticket");
    if(!pending||!pending->instruction.depth||pending->instruction.depth>vm_->stack().size())return reject("source choices lack their real VM frame");
    const auto& frame=vm_->stack()[pending->instruction.depth-1];const auto& instruction=pending->instruction.instruction;
    const SlotTable* table=nullptr;std::optional<SlotTable> relative;
    std::size_t index=instruction.operands[0]|(instruction.operands[1]<<8);
    if(instruction.opcode==27){relative=make_source_goto_relative_slot(instruction);if(!relative)return reject("source choices have an unknown relative request");table=&*relative;index=0;}
    else if(instruction.opcode==45){const auto scope=instruction.operands[2]|(instruction.operands[3]<<8);
        if(scope==2)table=&globals_;
        else if(scope==0||scope==1){if(!stack_object_slots_)return reject("source choice resource provider is unavailable");table=stack_object_slots_(frame);
            if(scope==0){if(index>=frame.arguments.size()||frame.arguments[index]<0)return reject("source choice argument slot index is invalid");index=static_cast<std::size_t>(frame.arguments[index]);}}
        else return reject("source choices have an invalid routing scope");
    }else return reject("source choices do not belong to Route45 or source27");
    if(job.failure_pending){
        const auto relative_request=instruction.opcode==27?decode_source_goto_relative_position(instruction):std::nullopt;
        const bool failure_policy=instruction.opcode==27?relative_request&&!relative_request->no_failure_trees:
            instruction.opcode==45&&(instruction.operands[4]&1U)==0;
        if(!job.call_failure_trees||!failure_policy||job.failure_tree_invoked||job.external_frame_active||
           job.deferred_room_route||job.room_context_open||job.source_sparse||job.source_motion||job.walking_started||
           job.frame.state!=RouteFrameState::initial||job.failure_code<=0||job.failure_reason.empty()||
           choices.chair_attempted||!choices.remaining.empty()||job.path.size()!=1||job.next!=1||
           !(job.path.front()==RoutePoint{job.expected.x,job.expected.y})||
           frame.context.caller!=job.actor||frame.context.stack_object!=job.target||
           !vm_context_equal(frame.context,pending->context)||
           pending->instruction.code_owner!=frame.context.code_owner||
           pending->instruction.key.scope!=frame.key.scope||pending->instruction.key.id!=frame.key.id||
           pending->instruction.pc!=frame.pc)
            return reject("failed source choice lacks its exact unstarted failure/pending authority");
    }
    const auto* slot=table?slot_of_type(*table,3,index):nullptr;
    if(!table||table->version!=choices.slot_version||index!=choices.slot_index||!slot||!same_slot_record(*slot,choices.slot))
        return reject("source choice resource does not match the exact installed SLOT");
    const auto goal_valid=[&](const SourceSlotChoiceGoal& goal){
        if(goal.position.x<INT16_MIN||goal.position.x>INT16_MAX||goal.position.y<INT16_MIN||goal.position.y>INT16_MAX||
           goal.facing>7||goal.level<1||goal.level>127||!std::isfinite(goal.radian_direction))return false;
        if(goal.level!=job.expected.level)return false;
        if(goal.chair){const auto* chair=world_.find(goal.chair->id);
            if(choices.slot.sitting<=0||!chair||chair->kind!=EntityKind::object||!goal.chair->generation||chair->generation!=goal.chair->generation||
               !chair->entry_points||chair->entry_points->size()<=26||!chair->entry_points->at(26).action)return false;}
        return true;
    };
    if(!goal_valid(choices.selected)||job.facing!=choices.selected.facing||job.entry_flags!=choices.selected.entry_flags||
       job.face_anywhere!=choices.selected.face_anywhere||!job.arrival_radian_direction||
       !same_float_bits(*job.arrival_radian_direction,choices.selected.radian_direction))return reject("source selected goal fields or chair epoch changed");
    if(job.selected_sitting_object!=(choices.selected.chair?std::optional<std::int16_t>{choices.selected.chair->id}:std::nullopt)||
       choices.remaining.size()!=job.fallback_goals.size())return reject("source semantic and legacy choice views disagree");
    for(std::size_t n=0;n<choices.remaining.size();++n){const auto& goal=choices.remaining[n];const auto& legacy=job.fallback_goals[n];
        if(!goal_valid(goal)||!(goal.position==legacy.position)||goal.entry_flags!=legacy.entry_flags||goal.facing!=legacy.facing||
           !legacy.radian_direction||!same_float_bits(*legacy.radian_direction,goal.radian_direction)||legacy.sitting_approach||
           legacy.sitting_object!=(goal.chair?std::optional<std::int16_t>{goal.chair->id}:std::nullopt))return reject("source remaining choice order or identity changed");}
    if(job.external_frame_sitting&&(!choices.chair_attempted||!choices.selected.chair))return reject("source chair external call has no attempted semantic chair");
    if(choices.selected.chair&&(!choices.chair_attempted||job.external_frame_sitting)){
        if(job.source_sparse||job.source_motion||job.path.size()!=1||job.next!=1||job.walking_started||
           !(job.path.front()==RoutePoint{job.expected.x,job.expected.y})||
           (job.frame.state!=RouteFrameState::initial&&job.frame.state!=RouteFrameState::room_portal))return reject("source chair must precede AttemptWalk");
        if(job.external_frame_sitting){const auto* chair=world_.find(choices.selected.chair->id);const auto& call=job.external_frame_call;
            if(!call||call->context.caller!=job.actor||call->context.callee!=chair->id||call->context.stack_object!=chair->id||
               call->key.id!=chair->entry_points->at(26).action||!call->exact_arguments)return reject("source chair external call does not match EP26");
            const auto bank=vm_->shared_program_for_owner(call->key.scope==RoutineScope::global?0:call->context.code_owner);
            const auto found=bank?bank->routines().find(call->key):RoutineSet::const_iterator{};
            if(!bank||found==bank->routines().end())return reject("source chair external resource bank is absent");
            const auto& routine=found->second;const auto extent=routine.metadata?routine.metadata->arguments:
                (routine.version==0x8000||routine.version==0x8001?std::size_t{4}:std::size_t{0});
            if(call->arguments!=std::vector<std::int16_t>(extent,0))return reject("source chair external arguments differ from the exact routine ABI");
        }
    }else if(!job.failure_tree_invoked&&!job.failure_pending&&!job.room_route_replan_active&&!is_source_standing_before_walk(local_snapshot(job))){
        if(!job.source_sparse||job.path.empty()||!(job.path.back()==choices.selected.position)||(choices.chair_attempted&&!choices.selected.chair))
            return reject("source standing choice has no matching AttemptWalk continuation");
    }
    if(!job.failure_pending&&!job.failure_tree_invoked&&is_source_standing_before_walk(local_snapshot(job))){
        const auto* actor=world_.find(job.actor);
        const bool active=job.external_frame_purpose==RouteExternalFramePurpose::standing;
        if(!actor||actor->person_data.empty()||(!active&&actor->person_data[0]!=1)||
           (active&&(!job.external_frame_active||job.frame.state!=RouteFrameState::stand_func)))
            return reject("source prewalk standing phase lacks posture or actual EP27 ancestry");
    }
    return true;
}
bool Routing::valid_local_session_job(const Job& job,std::optional<CodeOwner> verified_dynamic_owner,
    std::string* rejection_reason) const {
    const auto reject=[&](const std::string& reason){
        if(rejection_reason)*rejection_reason=reason;
        return false;
    };
    if(rejection_reason)rejection_reason->clear();
    if(!vm_)return reject("route controller is not bound to a VM");
    if(executing_)return reject("route callback is still running");
    if(job.failure_pending||job.failure_tree_invoked)return reject("failed source route requires its full pending/failure-tree stack");
    if(job.look_towards)return valid_look_towards_job(job,verified_dynamic_owner,rejection_reason);
    if(job.room_route_replan_active)return reject("architecture room replan requires the full route stack");
    if(job.source_choices){
        if(!validate_source_slot_choices(job,rejection_reason))return false;
        if(job.external_frame_active||job.room_context_open||job.source_choices->chair_attempted||!suspended_jobs_.empty())return reject("general source route requires stack-aware capture for its external ancestry");
        const auto* actor=world_.find(job.actor);const auto* target=world_.resolve_reference({job.target,job.target_generation});
        if(!actor||!target||actor->generation!=job.actor_generation||target->generation!=job.target_generation||!actor->position||(actor->attachment&&!is_source_standing_before_walk(local_snapshot(job)))||
           !(*actor->position==job.expected)||actor->radian_direction!=job.expected_radian_direction||
           !vm_->pending()||vm_->pending()->ticket!=job.ticket||vm_->run_generation()!=job.run_generation)return reject("general source route actor or VM changed");
        return true;
    }
    if(!job.source_sparse&&!grid_)return reject("legacy route has no actual dense grid");
    if(!job.same_room_verified&&!job.source_sparse&&!job.room_route_invalid)return reject("route has no verified same-room proof");
    if(job.external_frame_active||job.external_frame_call)
        return reject("route is inside an external BHAV call");
    if(job.deferred_room_route||job.room_context_open||job.portal_steps||job.portal_retries||
       job.portal_replan_required)
        return reject("route uses a room portal");
    if(job.replay_frame||job.replay_instruction)
        return reject("route is replaying a saved instruction");
    if(job.failure_tree_invoked||job.failure_pending){
        if(rejection_reason){
            *rejection_reason=job.failure_tree_invoked?"route already entered a failure branch":
                "route is waiting for a failure branch";
            if(!job.failure_reason.empty())*rejection_reason+=" : "+job.failure_reason;
        }
        return false;
    }
    if(job.room_route_replan_active)
        return reject("architecture room replan requires the full route stack");
    if(job.interrupt_pending)
        return reject("route has a pending interruption");
    if(job.selected_sitting_object||job.external_frame_sitting)
        return reject("route is attached to a sitting action");
    if(!suspended_jobs_.empty())
        return reject("route has a suspended parent route");
    if(job.frame.state!=RouteFrameState::initial&&job.frame.state!=RouteFrameState::walking&&
       job.frame.state!=RouteFrameState::turn_only&&
       !(job.source_sparse&&(job.frame.state==RouteFrameState::begin_turn||job.frame.state==RouteFrameState::end_turn||
           (job.frame.state==RouteFrameState::shooed&&job.source_sparse->walk_to_purpose==SourceWalkToPurpose::shoo))))
        return reject("route is outside a supported walking frame");
    if(job.frame.portal_turns||(job.frame.wait_time&&!(job.source_sparse&&job.source_sparse->collision))||job.frame.retries_remaining||
       job.path.empty()||job.path.size()-1>limits_.expanded||
       job.next==0||job.next>job.path.size()||job.facing>7||
       (job.entry_flags<0)||
       !std::isfinite(job.expected_radian_direction))
        return reject("route progress or destination data is incomplete (portal_turns="+
            std::to_string(job.frame.portal_turns)+", wait="+
            (job.frame.wait_time?std::to_string(*job.frame.wait_time):"none")+", retries="+
            (job.frame.retries_remaining?std::to_string(*job.frame.retries_remaining):"none")+
            ", actor="+std::to_string(job.actor)+", target="+std::to_string(job.target)+
            ", points="+std::to_string(job.path.size())+", next="+std::to_string(job.next)+
            ", facing="+std::to_string(job.facing)+", flags="+std::to_string(job.entry_flags)+")");
    const auto& stack=vm_->stack();
    const auto& pending=vm_->pending();
    if(vm_->status()!=VmStatus::waiting)return reject("VM is not waiting on Route45");
    if(!pending)return reject("VM has no pending instruction for the route");
    if(pending->ticket!=job.ticket)return reject("pending VM ticket does not match the active route");
    const auto& pending_instruction=pending->instruction.instruction;
    const auto* relative_profile_target=world_.resolve_reference({job.target,job.target_generation});
    const bool saved_go_here_instruction=job.call_failure_trees&&pending_instruction.opcode==27&&
        (pending_instruction.operands==std::array<std::uint8_t,8>{10,0,254,255,10,0,5,0}||
         (relative_failure_trees(pending_instruction)&&
          (source_exact_relative(pending_instruction)||(relative_profile_target&&!relative_profile_target->ui_ghost))));
    const auto route_scope=static_cast<std::uint16_t>(pending_instruction.operands[2]|(
        static_cast<std::uint16_t>(pending_instruction.operands[3])<<8));
    const bool supported_relative=source_exact_relative(pending_instruction)||
        is_source_ranged_goto_relative_instruction(pending_instruction)||
        (relative_profile_target&&!relative_profile_target->ui_ghost&&is_source_ordinary_go_here_instruction(pending_instruction));
    if(job.source_sparse&&relative_profile_target&&relative_profile_target->ui_ghost&&
       is_source_ordinary_go_here_instruction(pending_instruction))
        return reject("ordinary GoHere sparse continuation has a historical UI ghost target");
    const bool supported_route45=pending_instruction.opcode==45&&route_scope<=2&&
        (pending_instruction.operands[4]&0xFEU)==0&&pending_instruction.operands[5]==0&&
        pending_instruction.operands[6]==0&&pending_instruction.operands[7]==0;
    const bool source_circle=supported_route45&&job.source_circle.has_value();
    const bool source_range=(supported_route45||is_source_ranged_goto_relative_instruction(pending_instruction))&&job.source_range.has_value();
    const bool source_choices=supported_route45&&job.source_sparse&&job.source_sparse->choices.has_value();
    if((job.source_range&&(job.source_circle||job.look_towards||source_choices))||
       (source_choices&&(job.source_circle||job.look_towards)))
        return reject("SLOT choices manifest conflicts with another route profile");
    if(job.source_sparse){
        const auto& sparse=*job.source_sparse;
        SourceRouteCollision collision;
        if(sparse.collision&&(!collision.restore(*sparse.collision)||!collision_consideration_matches(sparse)||
           job.frame.wait_time.value_or(0)!=static_cast<std::uint32_t>(sparse.collision->wait_time)))
            return reject("sparse collision policy or wait counter is malformed");
        const bool waiting=sparse.collision&&sparse.collision->phase==SourceRouteCollisionPhase::waiting;
        if(waiting&&!source_sparse_services_.publish_velocity)return reject("source waiting animation service is unavailable");
        if(job.source_motion&&job.source_motion->phase()!=WalkMotionPhase::walking&&
           job.source_motion->phase()!=WalkMotionPhase::finished&&
           !((waiting||sparse.walk_to_pending)&&job.source_motion->phase()==WalkMotionPhase::collision_required))
            return reject("sparse route cannot save an unresolved placement or collision");
        const bool sparse_range=job.source_range.has_value();
        const bool sparse_circle=job.source_circle.has_value();
        const bool sparse_choices=sparse.choices.has_value();
        if(!valid_sparse_walk_to(sparse,job.frame,sparse_range||sparse_circle||sparse_choices,false,
            job.path,job.next,job.expected,world_))return reject("sparse pending WalkTo or retained Rooms is malformed");
        const bool sparse_point=job.arrival_radian_direction.has_value()&&!sparse_circle&&!sparse_range&&!sparse_choices;
        const bool sparse_relative=supported_relative&&!sparse_circle&&!sparse_range&&!sparse_point&&!sparse_choices;
        if((!sparse_range&&!sparse_circle&&!sparse_point&&!sparse_relative&&!sparse_choices)||
           (sparse_choices&&!supported_route45)||job.look_towards||sparse.lifecycle>2||
           !std::isfinite(sparse.target_radian_direction)||
           sparse.avatars_to_consider.size()>4096||
           (sparse.lifecycle==1&&(job.frame.state!=RouteFrameState::begin_turn||!sparse.turn))||
           (sparse.lifecycle==2&&(job.frame.state!=RouteFrameState::end_turn||!sparse.turn))||
           (sparse.lifecycle==0&&(job.frame.state==RouteFrameState::begin_turn||job.frame.state==RouteFrameState::end_turn)))
            return reject("sparse route continuation profile or turn lifecycle is malformed");
        const auto& motion=sparse.motion;
        const bool no_motion=!motion.current&&motion.remaining.empty()&&motion.phase==WalkMotionPhase::uninitialized&&
            motion.move_frames==0&&motion.move_total_frames==0&&motion.velocity==0&&motion.turn_frames==0&&
            motion.walk_direction==0&&motion.target_direction==0;
        if(!no_motion){WalkMotion verifier;if(verifier.restore(motion)!=WalkMotionStatus::success)
            return reject("sparse route motion continuation is invalid");}
        if(job.frame.state==RouteFrameState::shooed) {
            if(sparse.walk_to_purpose!=SourceWalkToPurpose::shoo||!sparse.walk_to_pending)
                return reject("sparse SHOOED route lacks its typed pending walk");
        }else if(job.frame.state==RouteFrameState::walking||job.frame.state==RouteFrameState::begin_turn){
            if(!(sparse.walk_to_pending&&sparse.walk_to_purpose==SourceWalkToPurpose::shoo&&no_motion)&&
               (no_motion||!job.source_motion||(motion.phase!=WalkMotionPhase::walking&&
               !((waiting||sparse.walk_to_pending)&&motion.phase==WalkMotionPhase::collision_required)&&
               !(sparse.walk_to_pending&&motion.phase==WalkMotionPhase::finished))))
                return reject("sparse walking or BeginWalk lifecycle has no active motion continuation");
        }else if(job.frame.state==RouteFrameState::turn_only){
            if(!sparse.walk_to_pending&&(!no_motion||job.source_motion))
                return reject("sparse TURN_ONLY route has unexpected motion state");
        }else if(job.frame.state==RouteFrameState::end_turn){
            if(!sparse.walk_to_pending&&(no_motion==bool(job.source_motion)||(!no_motion&&motion.phase!=WalkMotionPhase::finished)))
                return reject("sparse EndWalk lifecycle does not match its finished motion state");
        }else return reject("sparse route motion is outside its supported lifecycle");
    }
    const bool source_point=supported_route45&&job.arrival_radian_direction.has_value()&&!source_circle&&!source_range&&!source_choices;
    const bool self_target=job.actor==job.target;
    if((self_target&&!job.target_anchor_radian_direction)||
       (!self_target&&job.target_anchor_radian_direction&&(!source_point||!std::isfinite(*job.target_anchor_radian_direction)||
           source_direction_notch(*job.target_anchor_radian_direction)!=job.target_position.direction))||
       (self_target&&(!(supported_route45||(is_source_ranged_goto_relative_instruction(pending_instruction)&&source_range))||
        job.actor_generation!=job.target_generation||
        !std::isfinite(*job.target_anchor_radian_direction)||job.target_position.direction>7||
        job.target_position.x<0||job.target_position.x>32767||
        job.target_position.y<0||job.target_position.y>32767||job.target_position.level<1||
        source_direction_notch(*job.target_anchor_radian_direction)!=job.target_position.direction||
        job.target_position.level!=job.expected.level||
        (!job.source_circle&&!job.source_range&&!source_choices&&!(job.path.front()==RoutePoint{job.target_position.x,job.target_position.y}))||
        (!job.source_sparse&&!grid().contains(job.path.front())))))
        return reject("self-targeted Route45 has no valid frozen request anchor");
    if((job.source_circle&&(!source_circle||!job.arrival_radian_direction||job.entry_flags<=0))||
       (job.source_range&&(!source_range||!job.arrival_radian_direction||job.entry_flags<=0))||
       (job.arrival_radian_direction&&((!source_point&&!source_circle&&!source_range&&!source_choices)||!std::isfinite(*job.arrival_radian_direction)||
            (source_point&&job.entry_flags!=0)||job.call_failure_trees!=(pending_instruction.opcode==27?
                (pending_instruction.operands[6]&2U)==0:(pending_instruction.operands[4]&1U)==0)))||
       (job.entry_flags==0&&!saved_go_here_instruction&&!supported_relative&&!source_point)||
       (job.call_failure_trees&&!saved_go_here_instruction&&!source_point&&!source_circle&&!source_range&&!source_choices)||
       (!job.call_failure_trees&&!supported_route45&&!supported_relative)||
       (supported_relative&&job.call_failure_trees!=relative_failure_trees(pending_instruction)))
        return reject("VM is waiting on an instruction that does not match this route");
    if(pending->instruction.depth!=stack.size())return reject("Route45 frame depth changed");
    if(stack.empty())return reject("Route45 has no active VM frame");
    if(pending->context.caller!=job.actor||pending->context.stack_object!=job.target)
        return reject("Route45 caller or target changed");
    if(pending->context.code_owner!=static_cast<CodeOwner>(job.actor)&&
       (!verified_dynamic_owner||pending->context.code_owner!=*verified_dynamic_owner))
        return reject("Route45 dynamic code owner is not verified");
    if(stack.back().context.caller!=job.actor||stack.back().context.stack_object!=job.target||
       stack.back().context.code_owner!=pending->context.code_owner)
        return reject("active VM frame does not match the Route45 call");
    if(job.run_generation!=vm_->run_generation())return reject("route belongs to an earlier VM run");
    if(!room_pair_)return reject("route has no room lookup for save validation");
    const auto* actor=world_.find(job.actor);
    const auto* target=world_.resolve_reference({job.target,job.target_generation});
    if(!job.carried_target.empty()&&!route_carried_target_matches(world_,{job.actor,job.actor_generation},
        {job.target,job.target_generation},job.carried_target,job.target_position))
        return reject("carried target ancestry or live pose changed since route request");
    if(!actor||!target||actor->kind!=EntityKind::avatar||actor->generation!=job.actor_generation||
       target->generation!=job.target_generation||actor->attachment||!actor->position||(!source_circle&&!source_range&&!target->position)||
       actor->position->level!=job.target_position.level||
       !(*actor->position==job.expected)||actor->radian_direction!=job.expected_radian_direction||
        (!self_target&&!source_circle&&!source_range&&!target_checkpoint_matches(world_,job.actor,job.actor_generation,job.target,job.target_generation,job.carried_target,job.target_position,(captured_source_goals(job)||job.deferred_room_route)))||(actor->static_footprint==job.walking_started&&job.frame.state!=RouteFrameState::shooed))
        return reject("actor or destination changed since the route movement checkpoint");
    if(!job.source_sparse&&!job.room_route_invalid){
        try {
            const auto rooms=room_pair_(*actor->position,job.target_position);
            if(!rooms||rooms->first!=rooms->second)
                return reject("actor and destination are no longer in the same verified room");
        } catch(...) {return reject("room lookup failed while checking the route checkpoint");}
        if(!(job.path[job.next-1]==RoutePoint{job.expected.x,job.expected.y})||
           !grid().contains(job.path[job.next-1])||!grid().walkable(job.path[job.next-1]))
            return reject("current route point is outside the walkable grid");
        for(std::size_t n=job.next;n<job.path.size();++n)
            if(!grid().can_step(job.path[n-1],job.path[n]))
                return reject("remaining route is no longer walkable");
    }
    // A retained dead target can be captured before the next source dispatch.
    // Its frozen job/resource manifest survives; current live placement/slot
    // eligibility is not re-executed merely by saving or loading that wait.
    if(!world_.reference_is_dead({job.target,job.target_generation})){
        if(source_point&&!valid_source_slot_point_job(job,rejection_reason))return false;
        if(source_circle&&!valid_source_slot_circle_job(job,rejection_reason))return false;
        if(source_range&&!valid_source_slot_range_job(job,rejection_reason))return false;
        if(source_choices&&!valid_source_slot_choices_job(job,rejection_reason))return false;
    }
    return true;
}
bool Routing::valid_look_towards_job(const Job& job,std::optional<CodeOwner> owner,std::string* reason) const {
    const auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(!job.look_towards||!vm_||!suspended_jobs_.empty()||job.external_frame_active||job.external_frame_call||
       job.deferred_room_route||job.room_context_open||job.portal_steps||job.portal_retries||job.portal_replan_required||
       job.replay_frame||job.replay_instruction||job.failure_tree_invoked||job.failure_pending||job.interrupt_pending||
       job.selected_sitting_object||job.external_frame_sitting||!job.fallback_goals.empty()||job.source_sparse)
        return reject("LookTowards is outside a simple local turn frame");
    const auto& turn=*job.look_towards;
    if(turn.mode<2||turn.mode>5||!std::isfinite(turn.start_radian_direction)||
       !std::isfinite(turn.target_radian_direction)||!std::isfinite(turn.turn_tweak)||
       !std::isfinite(job.expected_radian_direction)||!job.same_room_verified||
       job.source_circle||job.source_range||job.arrival_radian_direction||job.target_anchor_radian_direction||
       job.target!=job.actor||job.target_generation!=job.actor_generation||job.path.size()!=1||job.next!=1||
       job.target_position.x<INT16_MIN||job.target_position.x>INT16_MAX||
       job.target_position.y<INT16_MIN||job.target_position.y>INT16_MAX||
       job.target_position.level<1||job.target_position.level>INT8_MAX||
       !(job.path.front()==RoutePoint{job.expected.x,job.expected.y})||
       job.expected.x!=job.target_position.x||job.expected.y!=job.target_position.y||
       job.expected.level!=job.target_position.level||job.expected.direction>7||
       job.target_position.direction!=source_direction_notch(turn.start_radian_direction)||
       job.facing!=source_direction_notch(turn.target_radian_direction)||
       job.entry_flags!=1||job.face_anywhere||job.walking_started||job.call_failure_trees||
       job.frame.portal_turns||job.frame.wait_time||job.frame.retries_remaining)
        return reject("LookTowards frozen geometry or route profile is invalid");
    const auto selection=source_turn_selection(turn.start_radian_direction,turn.target_radian_direction);
    if(job.frame.state==RouteFrameState::turn_only){
        if(turn.animation_index||turn.animation_num_frames||turn.turn_tweak!=0||turn.animation_speed!=1||
           !same_float_bits(job.expected_radian_direction,turn.start_radian_direction))
            return reject("unstarted LookTowards contains animation progress");
    }else if(job.frame.state==RouteFrameState::end_turn){
        const auto speed=turn.walk_style==1&&!turn.in_pool?2.0f:1.0f;
        if(!selection.index||selection.index!=turn.animation_index||!turn.animation_num_frames||
           turn.animation_num_frames>INT32_MAX||!same_float_bits(turn.animation_speed,speed))
            return reject("LookTowards turn animation differs from its source selection");
        auto tweak=selection.residual/(static_cast<float>(turn.animation_num_frames)/speed);
        if((selection.index&1U)!=0)tweak=-tweak;
        if(!same_float_bits(tweak,turn.turn_tweak))return reject("LookTowards turn tweak differs from clip duration");
    }else return reject("LookTowards is outside TURN_ONLY or END_TURN");
    const auto& pending=vm_->pending();const auto& stack=vm_->stack();
    if(vm_->status()!=VmStatus::waiting||!pending||pending->ticket!=job.ticket||stack.empty()||
       pending->instruction.instruction.opcode!=22||pending->instruction.instruction.operands[0]!=turn.mode||
       pending->instruction.depth!=stack.size()||pending->context.caller!=job.actor||
       stack.back().context.caller!=job.actor||stack.back().context.code_owner!=pending->context.code_owner||
       stack.back().context.callee!=pending->context.callee||
       stack.back().context.stack_object!=pending->context.stack_object||
       (pending->context.code_owner!=static_cast<CodeOwner>(job.actor)&&pending->context.code_owner!=owner)||
       vm_->run_generation()!=job.run_generation)
        return reject("LookTowards pending VM instruction or resource owner is stale");
    const auto* actor=world_.find(job.actor);
    if(!actor||actor->kind!=EntityKind::avatar||actor->generation!=job.actor_generation||!actor->position||
       !(*actor->position==job.expected)||!same_float_bits(actor->radian_direction,job.expected_radian_direction))
        return reject("LookTowards actor identity or continuous angle changed");
    if(!look_towards_services_.profile||!look_towards_services_.start||
       !look_towards_services_.ended||!look_towards_services_.reset)
        return reject("LookTowards animation services are unavailable");
    return true;
}
bool Routing::valid_source_slot_point_job(const Job& job,std::string* reason) const {
    const auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(!job.arrival_radian_direction||!vm_||!vm_->pending()||vm_->stack().empty()||job.path.empty())
        return reject("source point SLOT continuation has no matching VM or goal");
    const auto frame=vm_->stack().back();const auto instruction=vm_->pending()->instruction.instruction;
    const auto data=static_cast<std::uint16_t>(instruction.operands[0]|(instruction.operands[1]<<8));
    const auto scope=static_cast<std::uint16_t>(instruction.operands[2]|(instruction.operands[3]<<8));
    if(instruction.opcode!=45||scope>2)return reject("source point SLOT is not a supported Route45 request");
    const SlotTable* table=&globals_;std::size_t index=data;
    if(scope==0){
        if(data>=frame.arguments.size()||frame.arguments[data]<0)return reject("source point SLOT argument is invalid");
        index=static_cast<std::size_t>(frame.arguments[data]);
    }
    const auto* initial_target=world_.resolve_reference({job.target,job.target_generation});
    if(!initial_target||!initial_target->position)return reject("source target pose is unavailable");
    const auto validation_target_position=*initial_target->position;
    const auto validation_target_radians=source_slot_object_radians(*initial_target);
    try {
        if(scope<=1){
            if(!stack_object_slots_)return reject("source point SLOT resource provider is unavailable");
            struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(executing_);
            table=stack_object_slots_(frame);
            if(!table)return reject("source point SLOT resource is absent or ambiguous");
        }
        const auto* actor=world_.find(job.actor);const auto* target=world_.resolve_reference({job.target,job.target_generation});
        if(vm_->run_generation()!=job.run_generation||!vm_->pending()||vm_->pending()->ticket!=job.ticket||
           !actor||!target||actor->generation!=job.actor_generation||target->generation!=job.target_generation||
           !actor->position||!target->position||!(*target->position==validation_target_position)||
           !same_float_bits(source_slot_object_radians(*target),validation_target_radians)||!(*actor->position==job.expected)||
           !target_checkpoint_matches(world_,job.actor,job.actor_generation,job.target,job.target_generation,job.carried_target,job.target_position,(captured_source_goals(job)||job.deferred_room_route))||actor->radian_direction!=job.expected_radian_direction)
            return reject("source point SLOT participants changed during resource verification");
        const auto target_radians=job.target_anchor_radian_direction.value_or(job.carried_target.empty()?source_slot_object_radians(*target):
            target->kind==EntityKind::avatar?job.carried_target.front().radian_direction:source_game_object_radians(job.carried_target.front().original.direction));
        const auto goal=enumerate_source_slot_point_candidate(*table,index,job.target_position,target_radians);
        if(goal.position.level!=job.expected.level||!(job.path.back()==RoutePoint{goal.position.x,goal.position.y})||
           goal.facing!=job.facing||goal.face_anywhere!=job.face_anywhere||
           !same_float_bits(goal.radian_direction,*job.arrival_radian_direction))
            return reject("source point SLOT saved goal or facing differs from its exact resource");
        if(!goal_validator_&&!job.source_sparse)return reject("source point SLOT host placement validation is unavailable");
        const auto live_target_radians=source_slot_object_radians(*target);
        if(goal_validator_&&!job.source_sparse){
            struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(executing_);
            if(!goal_validator_(frame,{goal.position.x,goal.position.y}))
                return reject("source point SLOT goal is no longer valid under host bounds/wall/placement policy");
        }
        actor=world_.find(job.actor);target=world_.resolve_reference({job.target,job.target_generation});
        if(vm_->run_generation()!=job.run_generation||!vm_->pending()||vm_->pending()->ticket!=job.ticket||
           !actor||!target||actor->generation!=job.actor_generation||target->generation!=job.target_generation||
           !actor->position||!target->position||!(*target->position==validation_target_position)||
           !same_float_bits(source_slot_object_radians(*target),validation_target_radians)||!(*actor->position==job.expected)||
           !target_checkpoint_matches(world_,job.actor,job.actor_generation,job.target,job.target_generation,job.carried_target,job.target_position,(captured_source_goals(job)||job.deferred_room_route))||actor->radian_direction!=job.expected_radian_direction||
           !same_float_bits(source_slot_object_radians(*target),live_target_radians))
            return reject("source point SLOT participants changed during host goal validation");
    }catch(...){return reject("source point SLOT resource geometry could not be verified");}
    return true;
}
bool Routing::valid_source_slot_circle_job(const Job& job,std::string* reason) const {
    const auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(!job.source_circle||!job.arrival_radian_direction||!vm_||!vm_->pending()||vm_->stack().empty()||job.path.empty())
        return reject("source circle lacks its frozen manifest or VM");
    const auto& saved=*job.source_circle;const auto frame=vm_->stack().back();
    const auto instruction=vm_->pending()->instruction.instruction;
    const auto data=static_cast<std::uint16_t>(instruction.operands[0]|(instruction.operands[1]<<8));
    const auto scope=static_cast<std::uint16_t>(instruction.operands[2]|(instruction.operands[3]<<8));
    std::size_t index=data;const SlotTable* table=&globals_;
    if(instruction.opcode!=45||scope>2||!std::isfinite(saved.target_radian_direction)||saved.fallback_goals.size()>7||
       source_direction_notch(saved.target_radian_direction)!=job.target_position.direction||
       (job.target_anchor_radian_direction&&!same_float_bits(*job.target_anchor_radian_direction,saved.target_radian_direction)))
        return reject("source circle transform, option count or Route45 shape is invalid");
    if(scope==0){if(data>=frame.arguments.size()||frame.arguments[data]<0)return reject("source circle argument is invalid");
        index=static_cast<std::size_t>(frame.arguments[data]);}
    try{
        if(scope<=1){
            if(!stack_object_slots_)return reject("source circle resource provider is unavailable");
            struct Guard {bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(executing_);
            table=stack_object_slots_(frame);
        }
        const auto* slot=table?slot_of_type(*table,3,index):nullptr;
        if(!slot||table->version!=saved.slot_version||index!=saved.slot_index||!same_slot_record(*slot,saved.slot)||
           !is_source_slot_circle(*slot))return reject("source circle full SLOT row or resource identity changed");
        const auto geometry=enumerate_source_slot_circle_candidates(*table,index,job.target_position,saved.target_radian_direction);
        const auto valid=[&](const auto& goal){return goal.entry_flags!=0&&std::any_of(geometry.begin(),geometry.end(),
            [&](const auto& expected){return same_circle_goal(goal,expected);});};
        if(!valid(saved.selected)||!(saved.selected.position==job.path.back())||saved.selected.entry_flags!=job.entry_flags||
           saved.selected.facing!=job.facing||job.face_anywhere!=(saved.slot.facing==-3)||
           !same_float_bits(saved.selected.radian_direction,*job.arrival_radian_direction))
            return reject("source circle selected goal differs from its exact frozen geometry");
        std::vector<RoutePoint> seen{saved.selected.position};
        for(const auto& goal:saved.fallback_goals){
            if(!valid(goal)||std::find(seen.begin(),seen.end(),goal.position)!=seen.end())
                return reject("source circle fallback geometry or identity is invalid");
            seen.push_back(goal.position);
        }
    }catch(...){return reject("source circle resource verification failed");}
    const auto* actor=world_.find(job.actor);const auto* target=world_.resolve_reference({job.target,job.target_generation});
    if(!actor||!target||actor->generation!=job.actor_generation||target->generation!=job.target_generation||
       !actor->position||!(*actor->position==job.expected)||actor->radian_direction!=job.expected_radian_direction||
       !vm_->pending()||vm_->pending()->ticket!=job.ticket||vm_->run_generation()!=job.run_generation)
        return reject("source circle participants changed during manifest verification");
    return true;
}
bool Routing::valid_source_slot_range_job(const Job& job,std::string* reason) const {
    const auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(!job.source_range||!job.arrival_radian_direction||!vm_||!vm_->pending()||vm_->stack().empty()||job.path.empty())
        return reject("source range lacks its frozen manifest or VM");
    const auto& saved=*job.source_range;const auto frame=vm_->stack().back();
    const auto pending=*vm_->pending();
    const auto instruction=pending.instruction.instruction;
    const auto data=static_cast<std::uint16_t>(instruction.operands[0]|(instruction.operands[1]<<8));
    const auto scope=static_cast<std::uint16_t>(instruction.operands[2]|(instruction.operands[3]<<8));
    std::optional<SlotTable> transient;
    const bool relative=is_source_ranged_goto_relative_instruction(instruction);
    std::size_t index=relative?0:data;const SlotTable* table=&globals_;
    if((!relative&&(instruction.opcode!=45||scope>2))||!std::isfinite(saved.target_radian_direction)||saved.fallback_goals.size()>=4096||
       source_direction_notch(saved.target_radian_direction)!=job.target_position.direction||
       (job.target_anchor_radian_direction&&!same_float_bits(*job.target_anchor_radian_direction,saved.target_radian_direction)))
        return reject("source range transform, option count or primitive shape is invalid");
    if(!relative&&scope==0){if(data>=frame.arguments.size()||frame.arguments[data]<0)return reject("source range argument is invalid");
        index=static_cast<std::size_t>(frame.arguments[data]);}
    try{
        if(relative){transient=make_source_goto_relative_slot(instruction);table=transient?&*transient:nullptr;}
        else if(scope<=1){
            if(!stack_object_slots_)return reject("source range resource provider is unavailable");
            struct Guard {bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(executing_);
            table=stack_object_slots_(frame);
        }
        const auto* slot=table?slot_of_type(*table,3,index):nullptr;
        if(!slot||table->version!=saved.slot_version||index!=saved.slot_index||!same_slot_record(*slot,saved.slot)||
           !is_source_slot_range(*slot))return reject("source range full SLOT row or resource identity changed");
        const auto geometry=enumerate_source_slot_range_candidates(*table,index,job.target_position,saved.target_radian_direction);
        const auto valid=[&](const auto& goal){return goal.entry_flags!=0&&std::any_of(geometry.begin(),geometry.end(),
            [&](const auto& expected){return same_circle_goal(goal,expected);});};
        if(!valid(saved.selected)||!(saved.selected.position==job.path.back())||saved.selected.entry_flags!=job.entry_flags||
           saved.selected.facing!=job.facing||job.face_anywhere!=(saved.slot.facing==-3)||
           !same_float_bits(saved.selected.radian_direction,*job.arrival_radian_direction))
            return reject("source range selected goal differs from its exact frozen geometry");
        std::vector<RoutePoint> seen{saved.selected.position};
        for(const auto& goal:saved.fallback_goals){
            if(!valid(goal)||std::find(seen.begin(),seen.end(),goal.position)!=seen.end())
                return reject("source range fallback geometry or identity is invalid");
            seen.push_back(goal.position);
        }
    }catch(...){return reject("source range resource verification failed");}
    const auto* actor=world_.find(job.actor);const auto* target=world_.resolve_reference({job.target,job.target_generation});
    if(!actor||!target||actor->generation!=job.actor_generation||target->generation!=job.target_generation||
       !actor->position||!(*actor->position==job.expected)||actor->radian_direction!=job.expected_radian_direction||
       actor->attachment||!vm_->pending()||vm_->pending()->ticket!=job.ticket||vm_->run_generation()!=job.run_generation||
       vm_->stack().empty()||!placement_frame_equal(frame,vm_->stack().back())||
       vm_->pending()->instruction.step!=pending.instruction.step||vm_->pending()->instruction.depth!=pending.instruction.depth||
       vm_->pending()->instruction.key.scope!=pending.instruction.key.scope||vm_->pending()->instruction.key.id!=pending.instruction.key.id||
       vm_->pending()->instruction.pc!=pending.instruction.pc||vm_->pending()->instruction.code_owner!=pending.instruction.code_owner||
       !placement_instruction_equal(vm_->pending()->instruction.instruction,pending.instruction.instruction)||
       !vm_context_equal(vm_->pending()->context,pending.context))
        return reject("source range participants or pending frame changed during manifest verification");
    return true;
}
bool Routing::valid_source_slot_choices_job(const Job& job,std::string* reason) const {
    const auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(!job.source_sparse||!job.source_sparse->choices||!job.arrival_radian_direction||!vm_||
       !vm_->pending()||vm_->stack().empty()||job.path.empty())
        return reject("generic SLOT choices lack their sparse route or VM");
    const auto& saved=*job.source_sparse->choices;
    const auto& frame=vm_->stack().back();
    const auto pending=*vm_->pending();
    const auto instruction=pending.instruction.instruction;
    const auto data=static_cast<std::uint16_t>(instruction.operands[0]|(instruction.operands[1]<<8));
    const auto scope=static_cast<std::uint16_t>(instruction.operands[2]|(instruction.operands[3]<<8));
    std::size_t index=data;const SlotTable* table=&globals_;
    if(instruction.opcode!=45||scope>2||saved.slot_version!=10||saved.slot_index>UINT16_MAX||
       saved.fallback_goals.size()>=4096||!std::isfinite(saved.target_radian_direction)||
       source_direction_notch(saved.target_radian_direction)!=job.target_position.direction||
       !same_float_bits(saved.selected.radian_direction,*job.arrival_radian_direction)||
       (job.target_anchor_radian_direction&&!same_float_bits(*job.target_anchor_radian_direction,saved.target_radian_direction)))
        return reject("generic SLOT choices have invalid resource identity, angle or bounds");
    if(scope==0){if(data>=frame.arguments.size()||frame.arguments[data]<0)return reject("generic SLOT argument is invalid");
        index=static_cast<std::size_t>(frame.arguments[data]);}
    const auto* initial_target=world_.resolve_reference({job.target,job.target_generation});
    if(!initial_target||!initial_target->position)return reject("source target pose is unavailable");
    const auto validation_target_position=*initial_target->position;
    const auto validation_target_radians=source_slot_object_radians(*initial_target);
    try{
        if(scope<=1){
            if(!stack_object_slots_)return reject("generic SLOT resource provider is unavailable");
            struct Guard {bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(executing_);
            table=stack_object_slots_(frame);
        }
        const auto* slot=table?slot_of_type(*table,3,index):nullptr;
        if(!slot||table->version!=saved.slot_version||index!=saved.slot_index||!same_slot_record(*slot,saved.slot)||
           slot->standing<=0||slot->sitting!=0)
            return reject("generic SLOT full row or resource identity changed");
        const auto valid=[&](const SourceSlotCircleGoal& goal){
            return goal.entry_flags!=0&&goal.facing<8&&std::isfinite(goal.radian_direction)&&
                (goal.entry_flags&static_cast<std::uint8_t>(~slot->flags))==0&&
                goal.entry_flags==reference_slot_entry_flags(
                    {job.target_position.x,job.target_position.y},job.target_position.direction,
                    goal.position,static_cast<std::uint8_t>(slot->flags));
        };
        if(!valid(saved.selected)||!(saved.selected.position==job.path.back())||
           saved.selected.entry_flags!=job.entry_flags||saved.selected.facing!=job.facing)
            return reject("generic SLOT selected choice differs from the frozen route");
        if(job.face_anywhere!=(slot->facing&&*slot->facing==-3))
            return reject("generic SLOT FaceAnywhere state differs from the frozen row");
        std::vector<RoutePoint> seen{saved.selected.position};
        for(const auto& goal:saved.fallback_goals){
            if(!valid(goal)||std::find(seen.begin(),seen.end(),goal.position)!=seen.end())
                return reject("generic SLOT fallback choices are invalid or duplicated");
            seen.push_back(goal.position);
        }
    }catch(...){return reject("generic SLOT resource verification failed");}
    const auto* actor=world_.find(job.actor);const auto* target=world_.resolve_reference({job.target,job.target_generation});
    if(!actor||!target||actor->generation!=job.actor_generation||target->generation!=job.target_generation||
       !actor->position||!(*actor->position==job.expected)||actor->radian_direction!=job.expected_radian_direction||
       actor->attachment||!target->position||!(*target->position==validation_target_position)||
       (job.actor!=job.target&&(!target_checkpoint_matches(world_,job.actor,job.actor_generation,job.target,job.target_generation,job.carried_target,job.target_position,(captured_source_goals(job)||job.deferred_room_route))||
          !same_float_bits(source_slot_object_radians(*target),validation_target_radians)))||
       !vm_->pending()||vm_->pending()->ticket!=job.ticket||vm_->run_generation()!=job.run_generation||
       vm_->stack().empty()||!placement_frame_equal(frame,vm_->stack().back())||
       vm_->pending()->instruction.step!=pending.instruction.step||vm_->pending()->instruction.depth!=pending.instruction.depth||
       vm_->pending()->instruction.key.scope!=pending.instruction.key.scope||vm_->pending()->instruction.key.id!=pending.instruction.key.id||
       vm_->pending()->instruction.pc!=pending.instruction.pc||vm_->pending()->instruction.code_owner!=pending.instruction.code_owner||
       !placement_instruction_equal(vm_->pending()->instruction.instruction,pending.instruction.instruction)||
       !vm_context_equal(vm_->pending()->context,pending.context))
        return reject("generic SLOT participants or pending frame changed during manifest verification");
    return true;
}
LocalRouteSnapshot Routing::local_snapshot(const Job& j) {
    return LocalRouteSnapshot{j.ticket,j.actor_generation,j.target_generation,j.run_generation,
        j.actor,j.target,j.expected,j.target_position,j.expected_radian_direction,j.frame,
        j.path,j.next,j.entry_flags,j.facing,j.face_anywhere,j.walking_started,
        j.call_failure_trees,j.same_room_verified,j.arrival_radian_direction,j.target_anchor_radian_direction,
        j.source_circle,j.look_towards,j.source_range,j.source_sparse,j.source_choices,j.room_route_invalid,j.room_route_replan_active,j.carried_target};
}
std::optional<LocalRouteSnapshot> Routing::capture_local_session_route(
    std::optional<CodeOwner> verified_dynamic_owner,std::string* rejection_reason) const {
    if(rejection_reason)rejection_reason->clear();
    if(!job_){if(rejection_reason)*rejection_reason="no route is active";return {};}
    if(!valid_local_session_job(*job_,verified_dynamic_owner,rejection_reason))return {};
    return local_snapshot(*job_);
}
bool Routing::restore_local_session_route(const LocalRouteSnapshot& snapshot,
    std::optional<CodeOwner> verified_dynamic_owner) {
    if(executing_||job_||!suspended_jobs_.empty()||!vm_||(!snapshot.same_room_verified&&!snapshot.source_sparse))
        return false;
    Job candidate{};
    candidate.ticket=snapshot.ticket;
    candidate.actor_generation=snapshot.actor_generation;
    candidate.target_generation=snapshot.target_generation;
    candidate.actor=snapshot.actor;
    candidate.target=snapshot.target;
    candidate.expected=snapshot.expected;
    candidate.target_position=snapshot.target_position;
    candidate.expected_radian_direction=snapshot.expected_radian_direction;
    candidate.frame=snapshot.frame;
    candidate.path=snapshot.path;
    candidate.next=snapshot.next;
    candidate.entry_flags=snapshot.entry_flags;
    candidate.facing=snapshot.facing;
    candidate.face_anywhere=snapshot.face_anywhere;
    candidate.walking_started=snapshot.walking_started;
    candidate.call_failure_trees=snapshot.call_failure_trees;
    candidate.run_generation=snapshot.run_generation;
    candidate.same_room_verified=true;
    candidate.arrival_radian_direction=snapshot.arrival_radian_direction;
    candidate.target_anchor_radian_direction=snapshot.target_anchor_radian_direction;
    candidate.carried_target=snapshot.carried_target;
    candidate.source_circle=snapshot.source_circle;
    candidate.look_towards=snapshot.look_towards;
    candidate.source_range=snapshot.source_range;
    candidate.source_sparse=snapshot.source_sparse;
    candidate.source_choices=snapshot.source_choices;
    candidate.room_route_invalid=snapshot.room_route_invalid;
    candidate.room_route_replan_active=snapshot.room_route_replan_active;
    if(candidate.source_sparse&&(candidate.source_sparse->motion.current||
       candidate.source_sparse->motion.phase!=WalkMotionPhase::uninitialized)){
        candidate.source_motion=std::make_unique<WalkMotion>();
        if(candidate.source_motion->restore(candidate.source_sparse->motion)!=WalkMotionStatus::success)return false;
    }
    if(candidate.source_circle)for(const auto& goal:candidate.source_circle->fallback_goals){
        PreRankedSlotGoal value{goal.position,goal.entry_flags,goal.facing};value.radian_direction=goal.radian_direction;
        candidate.fallback_goals.push_back(value);
    }
    if(candidate.source_range)for(const auto& goal:candidate.source_range->fallback_goals){
        PreRankedSlotGoal value{goal.position,goal.entry_flags,goal.facing};value.radian_direction=goal.radian_direction;
        candidate.fallback_goals.push_back(value);
    }
    if(candidate.source_sparse&&candidate.source_sparse->choices)
        for(const auto& goal:candidate.source_sparse->choices->fallback_goals){
            PreRankedSlotGoal value{goal.position,goal.entry_flags,goal.facing};value.radian_direction=goal.radian_direction;
            candidate.fallback_goals.push_back(value);
        }
    if(candidate.source_choices)for(const auto& goal:candidate.source_choices->remaining){
        PreRankedSlotGoal value{goal.position,goal.entry_flags,goal.facing,
            goal.chair?std::optional<std::int16_t>{goal.chair->id}:std::nullopt};
        value.radian_direction=goal.radian_direction;candidate.fallback_goals.push_back(value);
    }
    if(!valid_local_session_job(candidate,verified_dynamic_owner))return false;
    candidate.diagnostic_restored=true;
    job_=std::move(candidate);route_mutated();
    last_tick_.reset();
    diagnostic_.clear();
    return true;
}
std::optional<Routing::Job> Routing::job_from_snapshot(const RouteJobSnapshot& saved) {
    Job result{};result.diagnostic_restored=true;
    result.ticket=saved.ticket;
    result.actor_generation=saved.actor_generation;
    result.target_generation=saved.target_generation;
    result.run_generation=saved.run_generation;
    result.actor=saved.actor;
    result.target=saved.target;
    result.expected=saved.expected;
    result.target_position=saved.target_position;
    result.expected_radian_direction=saved.expected_radian_direction;
    result.frame=saved.frame;
    result.path=saved.path;
    result.next=saved.next;
    result.entry_flags=saved.entry_flags;
    result.facing=saved.facing;
    result.face_anywhere=saved.face_anywhere;
    result.walking_started=saved.walking_started;
    result.call_failure_trees=saved.call_failure_trees;
    result.same_room_verified=saved.same_room_verified;
    result.arrival_radian_direction=saved.arrival_radian_direction;
    result.target_anchor_radian_direction=saved.target_anchor_radian_direction;
    result.carried_target=saved.carried_target;
    result.source_circle=saved.source_circle;
    result.look_towards=saved.look_towards;
    result.source_range=saved.source_range;
    result.source_sparse=saved.source_sparse;
    result.source_choices=saved.source_choices;
    result.room_route_invalid=saved.room_route_invalid;
    result.room_route_replan_active=saved.room_route_replan_active;
    result.external_frame_active=saved.external_frame_active;
    result.external_frame_owner=saved.external_frame_owner;
    result.external_frame_call=saved.external_frame_call;
    result.portal_retries=saved.portal_retries;
    result.portal_replan_required=saved.portal_replan_required;
    result.deferred_room_route=saved.deferred_room_route;
    result.replay_frame=saved.replay_frame;
    result.replay_instruction=saved.replay_instruction;
    result.portal_steps=saved.portal_steps;
    result.room_context_open=saved.room_context_open;
    result.failure_tree_invoked=saved.failure_tree_invoked;
    result.failure_pending=saved.failure_pending;
    result.interrupt_pending=saved.interrupt_pending;
    result.failure_code=saved.failure_code;
    result.failure_blocker=saved.failure_blocker;
    result.failure_reason=saved.failure_reason;
    result.unplaced_room_failure=saved.unplaced_room_failure;
    result.selected_sitting_object=saved.selected_sitting_object;
    result.fallback_goals=saved.fallback_goals;
    result.external_frame_sitting=saved.external_frame_sitting;
    result.sparse_consideration=saved.sparse_consideration;
    result.external_frame_purpose=saved.external_frame_purpose;
    result.external_frame_callee_generation=saved.external_frame_callee_generation;
    if(result.source_sparse&&(result.source_sparse->motion.current||
       result.source_sparse->motion.phase!=WalkMotionPhase::uninitialized)) {
        result.source_motion=std::make_unique<WalkMotion>();
        if(result.source_motion->restore(result.source_sparse->motion)!=WalkMotionStatus::success)return {};
    }
    return result;
}
bool bind_legacy_route_references(RouteStackSnapshot& state,const World& world,const VmSnapshot& vm){
    if(!vm.action_tree_known)return false;
    auto candidate=state;
    const auto bind=[&](VmContext& context){
        if(!context.entity_references_bound){
            for(const auto id:{context.caller,context.callee,context.stack_object})if(id>0&&!world.find(id))return false;
        }
        return world.bind_context_live(context);
    };
    for(auto& job:candidate.jobs){
        const auto depth=job.pending.instruction.depth;
        if(!depth||depth>vm.stack.size())return false;
        const auto& actual=vm.stack[depth-1];
        const bool legacy_frame=!job.route_frame.context.entity_references_bound;
        if(!bind(job.pending.context)||!bind(job.route_frame.context))return false;
        if(legacy_frame){
            // This only supplies metadata missing on old wire. All existing
            // frame values must match the real continuation before inheritance.
            auto expected=job.route_frame;expected.action_tree=actual.action_tree;
            if(!vm_frame_equal(expected,actual))return false;
            job.route_frame.action_tree=actual.action_tree;
        }
        if(job.replay_frame){
            const bool legacy=!job.replay_frame->context.entity_references_bound;
            if(!bind(job.replay_frame->context))return false;
            if(legacy)job.replay_frame->action_tree=job.route_frame.action_tree;
        }
        if(job.external_frame_call&&!bind(job.external_frame_call->context))return false;
    }
    for(auto& portal:candidate.portal_contexts)if(portal.pending_call&&!bind(portal.pending_call->context))return false;
    state=std::move(candidate);return true;
}
namespace {
bool route_key_equal(const RoutineKey& a,const RoutineKey& b) {return a.scope==b.scope&&a.id==b.id;}
bool route_context_equal(const VmContext& a,const VmContext& b) {
    return vm_context_equal(a,b);
}
bool route_instruction_equal(const Instruction& a,const Instruction& b) {
    return a.opcode==b.opcode&&a.on_true==b.on_true&&a.on_false==b.on_false&&a.operands==b.operands;
}
bool route_frame_equal(const VmFrame& a,const VmFrame& b) {
    return route_key_equal(a.key,b.key)&&a.pc==b.pc&&route_context_equal(a.context,b.context)&&
        a.arguments==b.arguments&&a.locals==b.locals&&a.return_mode==b.return_mode&&
        a.invocation_ticket==b.invocation_ticket&&a.exact_arguments==b.exact_arguments&&a.action_tree==b.action_tree;
}
bool route_pending_equal(const VmPending& a,const VmPending& b) {
    return a.ticket==b.ticket&&route_context_equal(a.context,b.context)&&
        a.instruction.step==b.instruction.step&&a.instruction.depth==b.instruction.depth&&
        route_key_equal(a.instruction.key,b.instruction.key)&&a.instruction.pc==b.instruction.pc&&
        a.instruction.code_owner==b.instruction.code_owner&&
        route_instruction_equal(a.instruction.instruction,b.instruction.instruction);
}
bool route_call_equal(const VmCall& a,const VmCall& b) {
    return route_key_equal(a.key,b.key)&&route_context_equal(a.context,b.context)&&
        a.arguments==b.arguments&&a.return_mode==b.return_mode&&a.exact_arguments==b.exact_arguments;
}
}
bool Routing::validates_immediate_parent(const World& world,const Vm& vm,
        const VmPending& pending,const VmFrame& frame,const RouteJobSnapshot* source_failure) {
    if(vm.dispatch_active()||!pending.ticket||!pending.instruction.depth||
       pending.instruction.depth>vm.stack().size()||!route_frame_equal(frame,vm.stack()[pending.instruction.depth-1])||
       !route_context_equal(pending.context,frame.context)||!route_key_equal(pending.instruction.key,frame.key)||
       pending.instruction.pc!=frame.pc||pending.instruction.code_owner!=frame.context.code_owner)return false;
    const auto* actor=world.frame_entity(frame,VmEntityRole::caller);
    const auto* target=world.frame_entity(frame,VmEntityRole::stack_object);
    const bool failed_unplaced=source_failure&&is_source_unplaced_room_failure(*source_failure)&&
        source_failure->ticket==pending.ticket&&source_failure->run_generation==vm.run_generation()&&
        source_failure->target==frame.context.stack_object&&target&&source_failure->target_generation==target->generation&&
        source_failure->actor==frame.context.caller&&actor&&source_failure->actor_generation==actor->generation&&
        route_pending_equal(source_failure->pending,pending)&&route_frame_equal(source_failure->route_frame,frame);
    if(!actor||actor->kind!=EntityKind::avatar||(!actor->position&&(!failed_unplaced||actor->attachment))||
       !std::isfinite(actor->radian_direction))return false;
    const auto& instruction=pending.instruction.instruction;
    const bool relative=make_source_goto_relative_slot(instruction).has_value();
    const auto scope=static_cast<unsigned>(instruction.operands[2])|(static_cast<unsigned>(instruction.operands[3])<<8);
    if(!(relative||(instruction.opcode==45&&scope<=2&&(instruction.operands[4]&~1U)==0&&
        !instruction.operands[5]&&!instruction.operands[6]&&!instruction.operands[7])||
        (instruction.opcode==22&&instruction.operands[0]>=2&&instruction.operands[0]<=5)))return false;
    const auto bank=vm.shared_program_for_owner(frame.key.scope==RoutineScope::global?0:frame.context.code_owner);
    if(!bank)return false;
    const auto routine=bank->routines().find(frame.key);
    if(routine==bank->routines().end()||frame.pc>=routine->second.instructions.size()||
       !route_instruction_equal(instruction,routine->second.instructions[frame.pc]))return false;
    if(vm.pending()&&route_pending_equal(pending,*vm.pending()))
        return vm.status()==VmStatus::waiting&&pending.instruction.depth==vm.stack().size();
    for(const auto& continuation:vm.immediate_continuations())
        if(continuation.parent_pending&&route_pending_equal(pending,*continuation.parent_pending))
            return continuation.parent_status==VmStatus::waiting&&continuation.parent_depth==pending.instruction.depth;
    return false;
}
bool Routing::validates_immediate_parent(const VmPending& pending,const VmFrame& frame) const {
    if(executing_||!vm_)return false;
    const Job* source=nullptr;
    if(job_&&job_->ticket==pending.ticket)source=&*job_;
    for(const auto& parent:suspended_jobs_)if(parent.ticket==pending.ticket)source=&parent;
    if(source&&source->unplaced_room_failure){
        RouteJobSnapshot proof;static_cast<LocalRouteSnapshot&>(proof)=local_snapshot(*source);
        proof.pending=pending;proof.route_frame=frame;proof.unplaced_room_failure=source->unplaced_room_failure;
        proof.failure_pending=source->failure_pending;proof.failure_tree_invoked=source->failure_tree_invoked;
        proof.failure_code=source->failure_code;proof.failure_blocker=source->failure_blocker;proof.failure_reason=source->failure_reason;
        proof.external_frame_active=source->external_frame_active;proof.external_frame_call=source->external_frame_call;
        return validates_immediate_parent(world_,*vm_,pending,frame,&proof);
    }
    return validates_immediate_parent(world_,*vm_,pending,frame);
}
bool Routing::can_park_for_immediate(const VmPending& pending,const VmFrame& frame) const {
    if(!job_||!vm_||!vm_->pending()||!route_pending_equal(pending,*vm_->pending())||
       job_->ticket!=pending.ticket||job_->external_frame_active||job_->run_generation!=vm_->run_generation()||
       !validates_immediate_parent(pending,frame))return false;
    const auto* actor=world_.find(job_->actor);const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
    return actor&&target&&actor->generation==job_->actor_generation&&target->generation==job_->target_generation&&
        ((job_->unplaced_room_failure&&!actor->attachment)||
         (actor->position&&*actor->position==job_->expected))&&
        same_float_bits(actor->radian_direction,job_->expected_radian_direction);
}
bool Routing::park_for_immediate(std::uint64_t ticket) {
    if(executing_||!vm_||!job_||job_->ticket!=ticket||job_->run_generation!=vm_->run_generation()||
       vm_->immediate_continuations().empty())return false;
    const auto& continuation=vm_->immediate_continuations().back();
    if(!continuation.parent_pending||continuation.parent_pending->ticket!=ticket||
       !continuation.parent_depth||continuation.parent_depth>=vm_->stack().size()||
       !validates_immediate_parent(*continuation.parent_pending,vm_->stack()[continuation.parent_depth-1]))return false;
    if(std::find(parked_immediate_routes_.begin(),parked_immediate_routes_.end(),ticket)!=parked_immediate_routes_.end())return false;
    parked_immediate_routes_.push_back(ticket);route_mutated();return true;
}
bool Routing::parked_for_immediate() const {
    if(!vm_||!job_||job_->run_generation!=vm_->run_generation())return false;
    std::size_t depth=0;
    if(vm_->pending()&&vm_->pending()->ticket==job_->ticket)depth=vm_->pending()->instruction.depth;
    for(const auto& pending:vm_->suspended_pending())if(pending.ticket==job_->ticket)depth=pending.instruction.depth;
    for(const auto& continuation:vm_->immediate_continuations())
        if(continuation.parent_pending&&continuation.parent_pending->ticket==job_->ticket)depth=continuation.parent_depth;
    if(!depth)return false;
    return std::any_of(vm_->immediate_continuations().begin(),vm_->immediate_continuations().end(),
        [&](const auto& continuation){return continuation.parent_depth>=depth;});
}
bool Routing::retry_immediate_parent(std::uint64_t ticket) {
    if(executing_||!vm_||!job_||job_->ticket!=ticket||!vm_->pending()||vm_->pending()->ticket!=ticket||
       vm_->stack().empty()||!validates_immediate_parent(*vm_->pending(),vm_->stack().back()))return false;
    const auto parked=std::find(parked_immediate_routes_.begin(),parked_immediate_routes_.end(),ticket);
    const auto* actor=world_.find(job_->actor);const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
    if(parked==parked_immediate_routes_.end()||!actor||!target||actor->generation!=job_->actor_generation||
       target->generation!=job_->target_generation||(!actor->position&&!job_->unplaced_room_failure)||actor->attachment)return false;
    // Source keeps routing state and shared World effects from the immediate child.
    // Only the host's anti-external-mutation baseline is advanced at this authenticated return.
    if(!job_->unplaced_room_failure)job_->expected=*actor->position;
    job_->expected_radian_direction=actor->radian_direction;
    if(!job_->look_towards&&!job_->unplaced_room_failure)job_->walking_started=!actor->static_footprint;
    if(job_->look_towards){job_->path.front()={actor->position->x,actor->position->y};
        job_->target_position.x=actor->position->x;job_->target_position.y=actor->position->y;job_->target_position.level=actor->position->level;}
    parked_immediate_routes_.erase(parked);route_mutated();return true;
}
std::optional<RouteStackSnapshot> Routing::capture_session_route_stack(
    std::optional<CodeOwner> owner,std::string* reason) const {
    const auto reject=[&](const char* why)->std::optional<RouteStackSnapshot>{if(reason)*reason=why;return {};};
    if(reason)reason->clear();
    if(executing_||!vm_||!job_)return reject("route stack has no quiescent active controller");
    const auto vm=vm_->snapshot();
    RouteStackSnapshot result;result.last_tick=last_tick_;
    const auto capture=[&](const Job& job)->bool {
        RouteJobSnapshot saved;static_cast<LocalRouteSnapshot&>(saved)=local_snapshot(job);
        saved.external_frame_active=job.external_frame_active;
        saved.external_frame_owner=job.external_frame_owner;
        saved.external_frame_call=job.external_frame_call;
        saved.portal_retries=job.portal_retries;
        saved.portal_replan_required=job.portal_replan_required;
        saved.deferred_room_route=job.deferred_room_route;
        saved.room_route_replan_active=job.room_route_replan_active;
        saved.replay_frame=job.replay_frame;
        saved.replay_instruction=job.replay_instruction;
        saved.portal_steps=job.portal_steps;
        saved.room_context_open=job.room_context_open;
        saved.failure_tree_invoked=job.failure_tree_invoked;
        saved.failure_pending=job.failure_pending;
        saved.interrupt_pending=job.interrupt_pending;
        saved.failure_code=job.failure_code;
        saved.failure_blocker=job.failure_blocker;
        saved.failure_reason=job.failure_reason;
        saved.unplaced_room_failure=job.unplaced_room_failure;
        saved.selected_sitting_object=job.selected_sitting_object;
        saved.fallback_goals=job.fallback_goals;
        saved.external_frame_sitting=job.external_frame_sitting;
        saved.sparse_consideration=job.sparse_consideration;
        saved.external_frame_purpose=job.external_frame_purpose;
        saved.external_frame_callee_generation=job.external_frame_callee_generation;
        // Capture the live integrator, never an earlier cached progress copy.
        if(job.source_motion) {
            const auto motion=job.source_motion->snapshot();
            if(motion.status!=WalkMotionStatus::success||!motion.snapshot||!saved.source_sparse)return false;
            saved.source_sparse->motion=*motion.snapshot;
        }
        const VmPending* pending=nullptr;
        if(vm.pending&&vm.pending->ticket==job.ticket)pending=&*vm.pending;
        for(const auto& suspended:vm.suspended_pending)if(suspended.ticket==job.ticket) {
            if(pending)return false;
            pending=&suspended;
        }
        for(const auto& continuation:vm.immediate_continuations)
            if(continuation.parent_pending&&continuation.parent_pending->ticket==job.ticket){
                if(pending)return false;pending=&*continuation.parent_pending;
            }
        if(!pending||!pending->instruction.depth||pending->instruction.depth>vm.stack.size())return false;
        saved.pending=*pending;saved.route_frame=vm.stack[pending->instruction.depth-1];
        result.jobs.push_back(std::move(saved));return true;
    };
    for(const auto& parent:suspended_jobs_)if(!capture(parent))return reject("suspended route has no unique VM continuation");
    if(!capture(*job_))return reject("active route has no unique VM continuation");
    if(!valid_session_route_stack(result,false,owner,reason))return {};
    return result;
}
bool Routing::valid_session_route_stack(const RouteStackSnapshot& saved,bool require_host_contexts,
    std::optional<CodeOwner> owner,std::string* reason) const {
    const auto reject=[&](const std::string& why){if(reason)*reason=why;return false;};
    if(reason)reason->clear();
    if(executing_||!vm_||saved.jobs.empty()||saved.jobs.size()>max_nested_jobs_)
        return reject("route stack is empty, busy, unbound or exceeds its depth limit");
    const auto vm=vm_->snapshot();

    std::size_t previous_depth=0;
    for(std::size_t index=0;index<saved.jobs.size();++index) {
        const auto& job=saved.jobs[index];const bool active=index+1==saved.jobs.size();
        const bool shoo=job.external_frame_purpose==RouteExternalFramePurpose::shoo;
        const bool standing=job.external_frame_purpose==RouteExternalFramePurpose::standing;
        if((job.external_frame_purpose&&!shoo&&!standing)||(!shoo&&!standing&&job.external_frame_callee_generation))
            return reject("route external purpose or callee generation is unsupported");
        if(standing){
            if(!job.external_frame_active||job.external_frame_sitting||!job.external_frame_call||!job.external_frame_callee_generation||
               !is_source_standing_before_walk(job)||job.frame.state!=RouteFrameState::stand_func)
                return reject("source standing external phase is inconsistent");
            const auto& call=*job.external_frame_call;const auto* chair=world_.find(call.context.callee);
            if(!chair||chair->generation!=job.external_frame_callee_generation||!chair->entry_points||chair->entry_points->size()<=27||
               call.key.id!=chair->entry_points->at(27).action||call.context.caller!=job.actor||
               call.context.stack_object!=chair->id||!call.exact_arguments)
                return reject("source standing external chair or EP27 identity changed");
            const auto bank=vm_->shared_program_for_owner(call.key.scope==RoutineScope::global?0:call.context.code_owner);
            const auto code=bank?bank->routines().find(call.key):RoutineSet::const_iterator{};
            if(!bank||code==bank->routines().end())return reject("source standing EP27 bank is unavailable");
            const auto& routine=code->second;const auto count=routine.metadata?routine.metadata->arguments:
                (routine.version==0x8000||routine.version==0x8001?std::size_t{4}:std::size_t{0});
            if(call.arguments!=std::vector<std::int16_t>(count,0))return reject("source standing EP27 arguments differ from exact metadata");
        }
        if(shoo) {
            if(!job.external_frame_active||!job.source_sparse||!job.source_sparse->walk_to_pending||
               job.source_sparse->walk_to_purpose!=SourceWalkToPurpose::shoo||job.frame.state!=RouteFrameState::shooed||
               job.failure_tree_invoked||job.external_frame_sitting||!job.external_frame_call||!job.external_frame_callee_generation)
                return reject("source shoo external continuation has inconsistent lifecycle");
            const auto& call=*job.external_frame_call;
            if(call.key.scope!=RoutineScope::private_object||call.key.id!=4107||call.context.caller!=job.actor||
               call.context.stack_object!=call.context.callee||!call.exact_arguments||call.arguments.size()<4)
                return reject("source shoo external call has changed its source identity");
            for(std::size_t n=0;n<call.arguments.size();++n)if(call.arguments[n]!=(n<4?0:-1))
                return reject("source shoo external default arguments changed");
            const auto bank=vm_->shared_program_for_owner(call.context.code_owner);
            const auto routine=bank?bank->routines().find(call.key):RoutineSet::const_iterator{};
            if(!bank||routine==bank->routines().end())return reject("source shoo routine bank is absent");
            const auto& code=routine->second;
            const auto extent=code.metadata?std::max<std::size_t>(4,code.metadata->arguments):
                (code.version==0x8000||code.version==0x8001?std::size_t{4}:std::size_t{0});
            if(!extent||call.arguments.size()!=extent)return reject("source shoo restored argument extent differs from its routine");
            const auto* destination=world_.find(call.context.callee);
            const bool returned=active&&vm.external_frame_return&&vm.external_frame_return->ticket==job.ticket;
            if((!destination&&!returned)||(destination&&(destination->generation!=job.external_frame_callee_generation||
               destination->kind!=EntityKind::object||!destination->definition||destination->definition->guid!=0x7c4)))
                return reject("source shoo Destination participant generation changed");
        }
        if(!job.ticket||!job.actor_generation||!job.target_generation||job.run_generation!=vm.run_generation||
           job.actor!=saved.jobs.front().actor||job.actor_generation!=saved.jobs.front().actor_generation||
           !std::isfinite(job.expected_radian_direction)||job.facing>7||job.entry_flags<0||
           static_cast<unsigned>(job.frame.state)>static_cast<unsigned>(RouteFrameState::failed)||
           job.portal_steps>max_portal_retries_||job.portal_retries>max_portal_retries_||
           job.failure_reason.size()>65536||job.fallback_goals.size()>4096||
           (job.sparse_consideration&&job.sparse_consideration->size()>4096))
            return reject("route stack identity, lifecycle or bounded progress is malformed");
        if(job.room_route_replan_active&&(job.deferred_room_route||job.look_towards||
           !room_route_invalidation_||!room_portal_step_||!source_sparse_services_.room_route||
           (!job.source_choices&&!job.source_sparse&&job.path.empty())||
           (job.frame.state!=RouteFrameState::initial&&job.frame.state!=RouteFrameState::room_portal)||
           (job.external_frame_active&&job.frame.state!=RouteFrameState::room_portal)))
            return reject("architecture room replan has no selected source goal or event service");
        for(std::size_t n=0;n<index;++n)if(saved.jobs[n].ticket==job.ticket)
            return reject("route stack repeats a pending ticket");
        const auto* actor=world_.find(job.actor);const auto* target=world_.resolve_reference({job.target,job.target_generation});
        if(!actor||!target||actor->kind!=EntityKind::avatar||actor->generation!=job.actor_generation||
           target->generation!=job.target_generation||(!job.unplaced_room_failure&&(!actor->position||!target->position)))
            return reject("route stack participant identity is unavailable");
        if(!job.carried_target.empty()&&!route_carried_target_matches(world_,{job.actor,job.actor_generation},
            {job.target,job.target_generation},job.carried_target,job.target_position))
            return reject("route stack carried target ancestry or live pose changed");
        const auto& pending=job.pending;const auto depth=pending.instruction.depth;
        if(pending.ticket!=job.ticket||!depth||depth>vm.stack.size()||depth<=previous_depth||
           !route_frame_equal(job.route_frame,vm.stack[depth-1])||
           !route_context_equal(pending.context,job.route_frame.context)||
           !route_key_equal(pending.instruction.key,job.route_frame.key)||
           pending.instruction.pc!=job.route_frame.pc||pending.context.caller!=job.actor||
           (!job.look_towards&&pending.context.stack_object!=job.target)||
           !vm_->has_routine(job.route_frame.key,pending.context.code_owner))
            return reject("route stack does not match its original VM frame");
        previous_depth=depth;
        bool authorized=pending.context.code_owner==static_cast<CodeOwner>(job.actor)||pending.context.code_owner==owner;
        for(std::size_t n=0;n<index&&!authorized;++n)
            authorized=saved.jobs[n].external_frame_call&&
                saved.jobs[n].external_frame_call->context.code_owner==pending.context.code_owner;
        if(!authorized)for(const auto& continuation:vm.immediate_continuations){
            if(continuation.parent_depth>=depth||continuation.parent_depth>=vm.stack.size())continue;
            const auto& child=vm.stack[continuation.parent_depth];
            if(child.return_mode==VmCallReturnMode::immediate&&child.invocation_ticket==continuation.ticket&&
               child.context.caller==job.actor&&child.context.code_owner==pending.context.code_owner&&
               vm_->shared_program_for_owner(child.context.code_owner))authorized=true;
        }
        if(!authorized)return reject("route stack dynamic owner has no verified caller or external ancestor");
        const VmPending* live=nullptr;std::size_t matches=0;
        if(vm.pending&&vm.pending->ticket==job.ticket){live=&*vm.pending;++matches;}
        for(const auto& wait:vm.suspended_pending)if(wait.ticket==job.ticket){live=&wait;++matches;}
        bool immediate_parent=false;
        for(const auto& continuation:vm.immediate_continuations)
            if(continuation.parent_pending&&continuation.parent_pending->ticket==job.ticket){
                live=&*continuation.parent_pending;++matches;immediate_parent=true;
                if(!validates_immediate_parent(world_,*vm_,*live,job.route_frame,job.unplaced_room_failure?&job:nullptr))
                    return reject("parked routing parent has no exact immediate continuation");
            }
        if(matches!=1||!route_pending_equal(pending,*live))return reject("route stack pending VM linkage changed");
        const auto opcode=pending.instruction.instruction.opcode;
        if(opcode!=45&&opcode!=27&&!(opcode==22&&job.look_towards))
            return reject("route stack ticket does not belong to a routing primitive");
        if(job.replay_frame.has_value()!=job.replay_instruction.has_value()||
           (job.replay_frame&&(!route_frame_equal(*job.replay_frame,job.route_frame)||
            !route_instruction_equal(*job.replay_instruction,pending.instruction.instruction))))
            return reject("deferred route replay does not match its original instruction");
        if(job.unplaced_room_failure){
            if(is_legacy_ui_go_here_request(pending.instruction.instruction,target->ui_ghost))
                return reject("legacy UI-only marker cannot carry source unplaced relative failure");
            const auto policy=source_route_failure_tree_policy(pending.instruction.instruction);
            if(!policy||!is_source_unplaced_room_failure(job)||job.call_failure_trees!=(*policy&&index==0))
                return reject("unplaced room failure lifecycle differs from its source request");
            const auto& origin=*job.unplaced_room_failure;const auto& op=pending.instruction.instruction;
            const auto scope=op.operands[2]|(op.operands[3]<<8);std::optional<SlotTable> transient;
            const SlotTable* table=nullptr;
            if(op.opcode==27){transient=make_source_goto_relative_slot(op);if(transient)table=&*transient;}
            else table=scope==2?&globals_:(stack_object_slots_?stack_object_slots_(job.route_frame):nullptr);
            const auto* slot=table?slot_of_type(*table,3,origin.slot_index):nullptr;
            if(!slot||table->version!=origin.slot_version||!same_slot_record(*slot,origin.slot))
                return reject("unplaced room failure lost its exact source SLOT");
        }else if(route_job_requires_dense_grid(job)&&!grid_)return reject("legacy route stack has no actual dense grid");
        if(job.deferred_room_route) {
            if(!room_portal_step_||!room_pair_||!route_grid_refresh_)
                return reject("deferred route continuation services are unavailable");
            if(!job.replay_frame||!job.room_context_open||!job.path.empty()||job.next!=1||
               job.source_sparse||job.look_towards||job.source_circle||job.source_range||
               job.selected_sitting_object||!job.fallback_goals.empty()||job.entry_flags!=0||
               job.frame.state!=RouteFrameState::room_portal)
                return reject("deferred room route contains incompatible local motion");
            const auto& original=pending.instruction.instruction;
            if(opcode==27&&!(source_exact_relative(original)||
               is_source_ranged_goto_relative_instruction(original)||
               (!target->ui_ghost&&is_source_ordinary_go_here_instruction(original))))
                return reject("deferred relative route changed its source transient SLOT profile");
            const bool cross_floor_slot=opcode==45&&job.expected.level!=job.target_position.level;
            if(cross_floor_slot&&!cross_floor_slot_matches(job.route_frame,original,false))
                return reject("deferred cross-floor SLOT lost its authenticated placement services");
            const bool failure_trees=opcode==27?(original.operands[6]&2U)==0:
                (original.operands[4]&1U)==0;
            if(job.facing!=0||job.face_anywhere!=(opcode==27&&original.operands[3]==255)||
               ((opcode==27||cross_floor_slot)&&job.call_failure_trees!=(failure_trees&&index==0))||job.arrival_radian_direction||
               job.target_anchor_radian_direction)
                return reject("deferred route source facing or failure policy changed");
        }else if(!job.unplaced_room_failure&&(job.path.empty()||job.path.size()-1>limits_.expanded||job.next==0||job.next>job.path.size()))
            return reject("local route stack path progress is malformed");
        if(job.external_frame_active) {
            if(!job.external_frame_call||job.external_frame_owner!=job.external_frame_call->context.code_owner||
               job.external_frame_call->return_mode!=VmCallReturnMode::ordinary||
               job.external_frame_call->arguments.size()>4096||
               !vm_->has_routine(job.external_frame_call->key,job.external_frame_owner))
                return reject("route external frame has no authenticated call");
            const bool returned=active&&vm.external_frame_return&&vm.external_frame_return->ticket==job.ticket;
            if(returned) {
                if(vm.status!=VmStatus::yielded||!vm.pending||vm.pending->ticket!=job.ticket||
                   vm.external_frame_return->run_generation!=job.run_generation||
                   vm.external_frame_return->code_owner!=job.external_frame_owner||depth!=vm.stack.size())
                    return reject("route external return has mismatched ownership");
            }else {
                if(depth>=vm.stack.size()||(vm.pending&&vm.pending->ticket==job.ticket))
                    return reject("route external call has no suspended parent");
                const auto& boundary=vm.stack[depth];const auto& call=*job.external_frame_call;
                if(boundary.return_mode!=VmCallReturnMode::external_frame_next_tick||
                   boundary.invocation_ticket!=job.ticket||!route_key_equal(boundary.key,call.key)||
                   boundary.context.caller!=call.context.caller||boundary.context.callee!=call.context.callee||
                   boundary.context.entity_references_bound!=call.context.entity_references_bound||
                   boundary.context.caller_generation!=call.context.caller_generation||boundary.context.callee_generation!=call.context.callee_generation||
                   boundary.context.code_owner!=call.context.code_owner||boundary.exact_arguments!=call.exact_arguments||
                   boundary.action_tree!=call.action_tree_override.value_or(job.route_frame.action_tree))
                    return reject("route external frame boundary changed");
            }
        }else if(!immediate_parent&&(!active||vm.status!=VmStatus::waiting||!vm.pending||vm.pending->ticket!=job.ticket||depth!=vm.stack.size()))
            return reject("active route is not the VM pending primitive");
        if(job.look_towards&&(!look_towards_services_.profile||!look_towards_services_.start||
           !look_towards_services_.ended||!look_towards_services_.reset))
            return reject("route LookTowards continuation services are unavailable");
        if(job.source_sparse) {
            if(!source_sparse_services_.plan_input||!source_sparse_services_.place||
               !source_sparse_services_.begin_locomotion||!source_sparse_services_.end_locomotion||
               !source_sparse_services_.publish_motion)
                return reject("route sparse continuation services are unavailable");
            const auto& sparse=*job.source_sparse;
            if(opcode==27&&is_source_ordinary_go_here_instruction(pending.instruction.instruction)&&target->ui_ghost)
                return reject("ordinary GoHere sparse continuation changed its source profile or target kind");
            const auto& motion=sparse.motion;
            if(!valid_sparse_walk_to(sparse,job.frame,job.source_circle||job.source_range||sparse.choices||job.source_choices,
                job.external_frame_active,job.path,job.next,job.expected,world_))
                return reject("route pending WalkTo or retained Rooms is malformed");
            if(sparse.collision){SourceRouteCollision collision;
                const bool failure_rejected_motion=is_source_failure_tree_continuation(job)&&motion.phase==WalkMotionPhase::collision_required&&
                    (sparse.collision->phase==SourceRouteCollisionPhase::ready||sparse.collision->phase==SourceRouteCollisionPhase::soft_failure);
                if(!collision.restore(*sparse.collision)||!collision_consideration_matches(sparse)||
                   (sparse.collision->phase==SourceRouteCollisionPhase::waiting&&!source_sparse_services_.publish_velocity)||
                   job.frame.wait_time.value_or(0)!=static_cast<std::uint32_t>(sparse.collision->wait_time)||
                   (!sparse.walk_to_pending&&!failure_rejected_motion&&((sparse.collision->phase==SourceRouteCollisionPhase::waiting)!=
                    (motion.phase==WalkMotionPhase::collision_required))))
                    return reject("route collision policy and motion continuation differ actor="+std::to_string(job.actor)+
                        " frame="+std::to_string(static_cast<unsigned>(job.frame.state))+" motion="+std::to_string(static_cast<unsigned>(motion.phase))+
                        " policy="+std::to_string(static_cast<unsigned>(sparse.collision->phase))+" wait="+std::to_string(sparse.collision->wait_time)+
                        " failure_child="+std::to_string(job.failure_tree_invoked));}
            const bool no_motion=!motion.current&&motion.remaining.empty()&&motion.phase==WalkMotionPhase::uninitialized&&
                motion.move_frames==0&&motion.move_total_frames==0&&motion.velocity==0&&motion.turn_frames==0&&
                motion.walk_direction==0&&motion.target_direction==0;
            if(!no_motion) {
                WalkMotion verifier;
                if(verifier.restore(motion)!=WalkMotionStatus::success)
                    return reject("route sparse motion snapshot is malformed");
            }
            if(motion.remaining.size()>4096||
               (sparse.lifecycle==0&&job.frame.state==RouteFrameState::walking&&no_motion&&
                !(sparse.walk_to_pending&&sparse.walk_to_purpose==SourceWalkToPurpose::shoo))||
               (job.frame.state==RouteFrameState::turn_only&&!no_motion&&!sparse.walk_to_pending))
                return reject("route sparse motion does not match its lifecycle");
            if(sparse.lifecycle>2||sparse.avatars_to_consider.size()>4096||!std::isfinite(sparse.target_radian_direction)||
               (sparse.lifecycle!=0&&!sparse.turn)||
               (sparse.lifecycle==1&&job.frame.state!=RouteFrameState::begin_turn)||
               (sparse.lifecycle==2&&job.frame.state!=RouteFrameState::end_turn))
                return reject("route sparse lifecycle is malformed");
        }
        const auto reconstructed=job_from_snapshot(job);
        if(!reconstructed)return reject("route motion continuation is unavailable or malformed");
        if(job.source_choices&&!world_.reference_is_dead({job.target,job.target_generation})&&!validate_source_slot_choices(*reconstructed,reason))return false;
        if(active&&!immediate_parent&&!job.external_frame_active&&!job.deferred_room_route&&!job.unplaced_room_failure&&
           (!(*actor->position==job.expected)||!same_float_bits(actor->radian_direction,job.expected_radian_direction)||
            (actor->attachment&&!is_source_standing_before_walk(job))||(!job.look_towards&&job.frame.state!=RouteFrameState::shooed&&actor->static_footprint==job.walking_started)))
        {
            const auto bits=[](float value){std::uint32_t result;std::memcpy(&result,&value,sizeof(result));return result;};
            std::ostringstream why;
            why<<"active route actor moved since its checkpoint actor="<<job.actor<<" ticket="<<job.ticket
               <<" active="<<active<<" immediate_parent="<<immediate_parent
               <<" state="<<static_cast<unsigned>(job.frame.state)
               <<" sparse_lifecycle="<<(job.source_sparse?static_cast<int>(job.source_sparse->lifecycle):-1)
               <<" actual="<<actor->position->x<<','<<actor->position->y<<','<<actor->position->level<<','<<unsigned(actor->position->direction)
               <<" expected="<<job.expected.x<<','<<job.expected.y<<','<<job.expected.level<<','<<unsigned(job.expected.direction)
               <<" actual_radian_bits="<<bits(actor->radian_direction)<<" expected_radian_bits="<<bits(job.expected_radian_direction)
               <<" attached="<<bool(actor->attachment)<<" static_footprint="<<actor->static_footprint
               <<" walking_started="<<job.walking_started;
            return reject(why.str());
        }
        if(active&&!immediate_parent&&!job.external_frame_active&&!job.deferred_room_route&&!job.unplaced_room_failure&&!job.look_towards) {
            if(job.actor!=job.target&&!job.source_circle&&!job.source_range&&
               !target_checkpoint_matches(world_,job.actor,job.actor_generation,job.target,job.target_generation,job.carried_target,job.target_position,(captured_source_goals(job)||job.deferred_room_route)))return reject("active route destination moved since its checkpoint");
            if(!job.source_sparse&&!job.source_choices&&!job.room_route_invalid&&!job.room_route_replan_active) {
                if(!(job.path[job.next-1]==RoutePoint{job.expected.x,job.expected.y})||
                   !grid().contains(job.path[job.next-1])||!grid().walkable(job.path[job.next-1]))
                    return reject("active route progress is outside the prepared grid");
                for(std::size_t n=job.next;n<job.path.size();++n)
                    if(!grid().can_step(job.path[n-1],job.path[n]))return reject("active route remaining path is no longer walkable");
            }
        }
        for(const auto& goal:job.fallback_goals)if(goal.facing>7||(!goal.entry_flags&&!job.source_choices)||
            (goal.radian_direction&&!std::isfinite(*goal.radian_direction)))
            return reject("route frozen fallback goal is malformed");
        if((job.arrival_radian_direction&&!std::isfinite(*job.arrival_radian_direction))||
           (job.target_anchor_radian_direction&&!std::isfinite(*job.target_anchor_radian_direction)))
            return reject("route frozen direction is malformed");
    }
    if(require_host_contexts||!saved.portal_contexts.empty()) {
        std::size_t expected=0;
        for(const auto& job:saved.jobs)if(job.room_context_open)++expected;
        if(saved.portal_contexts.size()!=expected)return reject("route room context ticket set is incomplete");
        for(std::size_t n=0;n<saved.portal_contexts.size();++n) {
            const auto& context=saved.portal_contexts[n];
            const auto found=std::find_if(saved.jobs.begin(),saved.jobs.end(),[&](const auto& job){return job.ticket==context.ticket;});
            if(found==saved.jobs.end()||!found->room_context_open||context.ignored.size()>4096||
               context.pending_edge.has_value()!=context.pending_call.has_value())
                return reject("route room context is malformed or has no owner");
            for(std::size_t previous=0;previous<n;++previous)if(saved.portal_contexts[previous].ticket==context.ticket)
                return reject("route room context repeats a ticket");
            if(context.pending_call&&((!found->failure_tree_invoked&&(!found->external_frame_call||
               !route_call_equal(*context.pending_call,*found->external_frame_call)))||
               context.pending_edge->portal_id!=context.pending_call->context.callee||
               !context.portal_generation))
                return reject("route room portal context does not match its frozen call");
        }
    }
    return true;
}
bool Routing::restore_session_route_stack(const RouteStackSnapshot& saved,
    std::optional<CodeOwner> owner,std::string* reason) {
    if(executing_||job_||!suspended_jobs_.empty()) {
        if(reason)*reason="route controller already has active state";return false;
    }
    if(!valid_session_route_stack(saved,true,owner,reason))return false;
    std::vector<Job> parents;parents.reserve(saved.jobs.size()-1);
    for(std::size_t n=0;n+1<saved.jobs.size();++n) {
        auto value=job_from_snapshot(saved.jobs[n]);if(!value)return false;
        parents.push_back(std::move(*value));
    }
    auto active=job_from_snapshot(saved.jobs.back());if(!active)return false;
    suspended_jobs_=std::move(parents);job_=std::move(*active);route_mutated();
    parked_immediate_routes_.clear();
    for(const auto& continuation:vm_->immediate_continuations())if(continuation.parent_pending)
        for(const auto& job:saved.jobs)if(job.ticket==continuation.parent_pending->ticket)
            parked_immediate_routes_.push_back(job.ticket);
    last_tick_=saved.last_tick;diagnostic_.clear();return true;
}
std::optional<float> Routing::incoming_direction(float caller_radian_direction) const noexcept {
    if(!job_) return {};
    const auto* parent=suspended_jobs_.empty()?nullptr:&suspended_jobs_.back().frame;
    return route_incoming_direction(job_->frame,parent,caller_radian_direction);
}
bool Routing::consume_portal_turn() noexcept {
    if(!job_) return false;
    route_mutated();
    auto* parent=suspended_jobs_.empty()?nullptr:&suspended_jobs_.back().frame;
    return consume_route_portal_turn(job_->frame,parent);
}
struct PreparedSourceRouteShoo::State {
    const Routing* owner=nullptr;
    std::weak_ptr<int> lifetime;
    std::uint64_t revision=0,generation_floor=0;
    VmSnapshot vm;
    LiveAttemptWalkIdentity actor{},target{},shooer{};
    EntityPosition actor_position{},target_position{},shooer_position{};
    float actor_radians=0,target_radians=0,shooer_radians=0;
    std::optional<Attachment> target_attachment;
    std::uint64_t ticket=0;
    bool consumed=false;
};
bool Routing::source_shoo_owner_binding_current(const SourceSparseMotionContext& context) const noexcept {
    try{
        const auto state=context.owner_binding.state_.lock();
        // The discriminator is immutable and the weak lock pins its lifetime.
        // Do not read mutable controller/VM state from another native thread.
        if(!state||state->thread!=&resource_owner_thread_identity)return false;
        if(!state->active||state->owner!=this||state->context!=&context||!state->current||
           !executing_||source_reset_retired_||!vm_||context.vm!=vm_||!job_||context.lifecycle_instruction||!state->current())return false;
        const auto* actor=world_.find(context.actor);
        return actor&&actor->generation==context.actor_generation&&actor->position&&*actor->position==context.position&&
            job_->actor==context.actor&&job_->actor_generation==context.actor_generation&&
            job_->ticket==context.dispatch.ticket&&job_->run_generation==context.run_generation&&
            vm_->run_generation()==context.run_generation&&context.dispatch.origin==vm_&&!context.dispatch.check_mode&&
            vm_->pending()&&vm_->pending()->ticket==context.dispatch.ticket&&!vm_->stack().empty()&&
            route_frame_equal(vm_->stack().back(),context.frame);
    }catch(...){return false;}
}
bool Routing::prepare_source_shoo_owner(const SourceSparseMotionContext& context,Vm& receiver,CodeOwner owner){
    if(!source_shoo_owner_binding_current(context))return false;
    return receiver.prepare_source_owner(owner,[&]{return source_shoo_owner_binding_current(context);});
}
std::optional<PreparedSourceRouteShoo> Routing::prepare_source_shoo(LiveAttemptWalkIdentity shooer,std::string* reason) const {
    const auto reject=[&](const char* why)->std::optional<PreparedSourceRouteShoo>{if(reason)*reason=why;return {};};
    if(reason)reason->clear();
    if(executing_||!job_||!job_->source_sparse||job_->external_frame_active||job_->look_towards||
       job_->deferred_room_route||job_->failure_pending||job_->failure_tree_invoked||job_->interrupt_pending||
       !vm_||vm_->dispatch_active()||vm_->status()!=VmStatus::waiting||!vm_->pending()||
       vm_->pending()->ticket!=job_->ticket||vm_->run_generation()!=job_->run_generation||vm_->stack().empty()||
       vm_->pending()->instruction.depth!=vm_->stack().size())
        return reject("source shoo target has no exact top sparse routing frame");
    const auto& oldest=suspended_jobs_.empty()?job_->frame:suspended_jobs_.front().frame;
    if(oldest.state==RouteFrameState::shooed)return reject("source oldest routing frame is already SHOOED");
    const auto* actor=world_.find(job_->actor);const auto* target=world_.resolve_reference({job_->target,job_->target_generation});const auto* source=world_.find(shooer.id);
    if(!actor||!target||!source||source->kind!=EntityKind::avatar||actor->kind!=EntityKind::avatar||
       actor->generation!=job_->actor_generation||target->generation!=job_->target_generation||source->generation!=shooer.generation||
       actor->id==source->id||!actor->position||!target->position||!source->position||actor->attachment||source->attachment||
       !(*actor->position==job_->expected)||!same_float_bits(actor->radian_direction,job_->expected_radian_direction))
        return reject("source shoo participant identity or pose is unavailable");
    if(job_->source_motion){const auto motion=job_->source_motion->snapshot();
        if(motion.status!=WalkMotionStatus::success||!motion.snapshot)return reject("source shoo cannot suspend an in-flight motion lease");}
    if(job_->source_sparse->avatars_to_consider.size()>4096)return reject("source shoo consideration exceeds bounds");
    if(job_->source_sparse->collision){SourceRouteCollision collision;
        if(!collision.restore(*job_->source_sparse->collision)||!collision_consideration_matches(*job_->source_sparse)||
           (collision.phase()!=SourceRouteCollisionPhase::ready&&collision.phase()!=SourceRouteCollisionPhase::waiting))
            return reject("source shoo cannot suspend an in-flight collision decision");}
    PreparedSourceRouteShoo prepared;prepared.state_=std::make_shared<PreparedSourceRouteShoo::State>();
    auto& state=*prepared.state_;state.owner=this;state.lifetime=shoo_lifetime_;state.revision=route_revision_;
    state.generation_floor=std::get<0>(world_.entity_identity_revision());
    state.vm=vm_->snapshot();
    if(state.vm.external_frame_return||state.vm.immediate_return||!state.vm.immediate_continuations.empty())
        return reject("source shoo overlaps an unfinished VM continuation");
    state.actor={actor->id,actor->generation};state.target={target->id,target->generation};state.shooer=shooer;
    state.actor_position=*actor->position;state.target_position=*target->position;state.shooer_position=*source->position;
    state.actor_radians=actor->radian_direction;state.target_radians=target->radian_direction;state.shooer_radians=source->radian_direction;
    state.target_attachment=target->attachment;state.ticket=job_->ticket;return prepared;
}
bool Routing::source_shoo_token_matches(const PreparedSourceRouteShoo& prepared,std::string* reason) const {
    const auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(reason)reason->clear();const auto state=prepared.state_;
    if(!state||state->consumed||state->owner!=this||state->lifetime.expired()||state->revision!=route_revision_||
       executing_||!job_||!job_->source_sparse||job_->ticket!=state->ticket||job_->external_frame_active||!vm_||vm_->dispatch_active())
        return reject("source shoo prepared route authority is stale");
    const auto current=vm_->snapshot();
    if(current.status!=state->vm.status||current.run_generation!=state->vm.run_generation||current.steps!=state->vm.steps||
       current.ticket_sequence!=state->vm.ticket_sequence||
       current.stack.size()!=state->vm.stack.size()||!current.pending||!state->vm.pending||
       !route_pending_equal(*current.pending,*state->vm.pending)||current.suspended_pending.size()!=state->vm.suspended_pending.size()||
       current.external_frame_return||current.immediate_return||!current.immediate_continuations.empty())
        return reject("source shoo prepared VM continuation changed");
    for(std::size_t n=0;n<current.stack.size();++n)if(!route_frame_equal(current.stack[n],state->vm.stack[n]))
        return reject("source shoo prepared VM stack changed");
    for(std::size_t n=0;n<current.suspended_pending.size();++n)if(!route_pending_equal(current.suspended_pending[n],state->vm.suspended_pending[n]))
        return reject("source shoo prepared parent wait changed");
    const auto matches=[&](LiveAttemptWalkIdentity identity,const EntityPosition& position,float radians,bool avatar){
        const auto* live=world_.find(identity.id);
        return live&&live->generation==identity.generation&&(!avatar||live->kind==EntityKind::avatar)&&
            live->position&&*live->position==position&&same_float_bits(live->radian_direction,radians)&&(!avatar||!live->attachment);
    };
    if(!matches(state->actor,state->actor_position,state->actor_radians,true)||
       !matches(state->target,state->target_position,state->target_radians,false)||
       !placement_attachment_equal(world_.find(state->target.id)->attachment,state->target_attachment)||
       !matches(state->shooer,state->shooer_position,state->shooer_radians,true))
        return reject("source shoo prepared participant epoch or pose changed");
    return true;
}
bool Routing::can_commit_source_shoo(const PreparedSourceRouteShoo& prepared,const VmCall& call,std::string* reason) const {
    const auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(!source_shoo_token_matches(prepared,reason))return false;
    const auto* destination=world_.find(call.context.callee);
    if(call.key.scope!=RoutineScope::private_object||call.key.id!=4107||call.return_mode!=VmCallReturnMode::ordinary||
       !call.exact_arguments||call.context.caller!=job_->actor||call.context.stack_object!=call.context.callee||
       !destination||destination->generation<=prepared.state_->generation_floor||destination->kind!=EntityKind::object||
       !destination->definition||destination->definition->guid!=0x7c4||destination->attachment||
       (destination->position&&(destination->position->x!=prepared.state_->actor_position.x||
         destination->position->y!=prepared.state_->actor_position.y||destination->position->level!=prepared.state_->actor_position.level))||
       !vm_->can_accept_call(call))return reject("source shoo call is not the exact live Destination subroutine");
    std::size_t arguments=0;
    try{const auto bank=vm_->shared_program_for_owner(call.context.code_owner);
        if(!bank)return reject("source shoo routine owner is not installed");
        const auto found=bank->routines().find(call.key);
        if(found==bank->routines().end())return reject("source shoo private routine is absent from its owner bank");
        const auto& routine=found->second;
        if(routine.metadata)arguments=std::max<std::size_t>(4,routine.metadata->arguments);
        else if(routine.version==0x8000||routine.version==0x8001)arguments=4;
        else return reject("source shoo argument declaration is unsupported");
    }catch(...){return reject("source shoo routine owner is not installed");}
    if(call.arguments.size()!=arguments)return reject("source shoo subroutine argument extent changed");
    for(std::size_t n=0;n<arguments;++n)if(call.arguments[n]!=(n<4?0:-1))
        return reject("source shoo subroutine default argument differs from the source operand");
    const auto& considered=job_->source_sparse->avatars_to_consider;
    const auto shooer=prepared.state_->shooer;
    const bool present=std::any_of(considered.begin(),considered.end(),[&](const auto& item){return item.id==shooer.id&&item.generation==shooer.generation;});
    if(!present&&considered.size()==4096)return reject("source shoo consideration capacity is exhausted");
    return true;
}
bool Routing::commit_source_shoo(PreparedSourceRouteShoo& prepared,const VmCall& call,std::string* reason) {
    if(!can_commit_source_shoo(prepared,call,reason))return false;
    // Prepare every allocation before changing the VM or the route. A failed
    // call_pending leaves the original pending service intact.
    auto next=*job_->source_sparse;auto frame_call=call;const auto identity=prepared.state_->shooer;
    if(std::none_of(next.avatars_to_consider.begin(),next.avatars_to_consider.end(),[&](const auto& value){
        return value.id==identity.id&&value.generation==identity.generation;}))next.avatars_to_consider.push_back(identity);
    SourceRouteCollisionSnapshot policy=next.collision.value_or(SourceRouteCollisionSnapshot{});
    policy.considered.clear();policy.considered.reserve(next.avatars_to_consider.size());
    for(const auto member:next.avatars_to_consider)policy.considered.push_back({member.id,member.generation});
    next.collision=std::move(policy);next.walk_to_pending=true;next.walk_to_purpose=SourceWalkToPurpose::shoo;next.lifecycle=0;
    auto consideration=next.avatars_to_consider;
    const auto* destination=world_.find(call.context.callee);const auto generation=destination->generation;
    if(!vm_->call_pending(job_->ticket,call)){if(reason)*reason="source shoo VM rejected its prepared subroutine";return false;}
    job_->source_sparse=std::move(next);job_->sparse_consideration=std::move(consideration);
    job_->frame.state=RouteFrameState::shooed;job_->external_frame_active=true;
    job_->external_frame_purpose=RouteExternalFramePurpose::shoo;job_->external_frame_callee_generation=generation;
    job_->external_frame_owner=call.context.code_owner;job_->external_frame_call=std::move(frame_call);if(job_->external_frame_call)world_.bind_context_live(job_->external_frame_call->context);
    prepared.state_->consumed=true;route_mutated();return true;
}
bool Routing::call_external_frame(const VmCall& call) {
    if(executing_) return false;
    return schedule_external_frame(call,false);
}
bool Routing::schedule_external_frame(const VmCall& call,bool sitting) {
    if(!vm_ || !job_ || job_->external_frame_active ||
       vm_->run_generation()!=job_->run_generation || vm_->status()!=VmStatus::waiting ||
       !vm_->pending() || vm_->pending()->ticket!=job_->ticket ||
       call.return_mode!=VmCallReturnMode::ordinary) return false;
    if(!vm_->call_pending(job_->ticket,call)) return false;
    route_mutated();job_->external_frame_active=true;
    job_->external_frame_purpose.reset();job_->external_frame_callee_generation=0;
    job_->external_frame_sitting=sitting;
    job_->external_frame_owner=call.context.code_owner;
    job_->external_frame_call=call;if(job_->external_frame_call)world_.bind_context_live(job_->external_frame_call->context);
    job_->frame.state=RouteFrameState::room_portal;
    return true;
}
bool Routing::try_next_sitting_goal() {
    if(!job_||job_->fallback_goals.empty())return false;
    const auto* actor=world_.find(job_->actor);
    const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!target||actor->generation!=job_->actor_generation||
       target->generation!=job_->target_generation||!actor->position||!target->position||actor->attachment)
        return false;
    if(route_grid_refresh_&&!route_grid_refresh_(*actor->position))return false;
    actor=world_.find(job_->actor);target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!target||!actor->position||!target->position||actor->attachment||
       actor->generation!=job_->actor_generation||target->generation!=job_->target_generation||
       (job_->target!=job_->actor&&!target_checkpoint_matches(world_,job_->actor,job_->actor_generation,job_->target,job_->target_generation,job_->carried_target,job_->target_position,(captured_source_goals(*job_)||job_->deferred_room_route))))
        return false;
    const auto result=find_pre_ranked_slot_route(grid(),{actor->position->x,actor->position->y},
                                                 job_->fallback_goals,limits_);
    if(result.path.status!=RouteStatus::found||!result.selected)return false;
    const auto index=*result.selected;
    const auto selected=job_->fallback_goals[index];
    job_->fallback_goals.erase(job_->fallback_goals.begin()+static_cast<std::ptrdiff_t>(index));
    job_->path=result.path.points;
    job_->next=1;
    job_->entry_flags=selected.entry_flags;
    job_->facing=selected.facing;
    job_->selected_sitting_object=selected.sitting_object;
    job_->expected=*actor->position;
    job_->expected_radian_direction=actor->radian_direction;
    job_->walking_started=true;
    job_->external_frame_sitting=false;
    job_->frame.state=job_->path.size()==1?RouteFrameState::turn_only:RouteFrameState::walking;
    diagnostic_.clear();
    return true;
}
std::optional<bool> Routing::intersects_destination(RoutePoint point,
    std::optional<std::int16_t> candidate_chair,std::string* diagnostic) const {
    if(diagnostic)diagnostic->clear();
    if(!job_)return false;
    std::string unknown;
    const auto test=[&](const Job& job){
        const auto reject=[&](const std::string& why){
            if(unknown.empty()){
                unknown="route ticket "+std::to_string(job.ticket)+": "+why+
                    " state="+std::to_string(static_cast<unsigned>(job.frame.state))+
                    " paths="+std::to_string(job.path.size())+" next="+std::to_string(job.next)+
                    " failure_pending="+std::to_string(job.failure_pending)+
                    " failure_tree="+std::to_string(job.failure_tree_invoked)+
                    " failure_code="+std::to_string(job.failure_code)+
                    " deferred="+std::to_string(job.deferred_room_route)+
                    " external="+std::to_string(job.external_frame_active)+
                    " source_choices="+std::to_string(job.source_choices.has_value())+
                    " source_circle="+std::to_string(job.source_circle.has_value())+
                    " source_range="+std::to_string(job.source_range.has_value())+
                    " source_sparse="+std::to_string(job.source_sparse.has_value())+
                    " look_towards="+std::to_string(job.look_towards.has_value())+
                    " restored="+std::to_string(job.diagnostic_restored)+
                    " actor="+std::to_string(job.actor)+"/"+std::to_string(job.actor_generation)+
                    " target="+std::to_string(job.target)+"/"+std::to_string(job.target_generation)+
                    " failure_reason="+job.failure_reason;
            }
            return false;
        };
        if(!vm_||job.run_generation!=vm_->run_generation())return reject("VM generation is stale");
        const auto* actor=world_.find(job.actor);const auto* target=world_.resolve_reference({job.target,job.target_generation});
        if(!actor||actor->kind!=EntityKind::avatar||actor->generation!=job.actor_generation||
           !target||target->generation!=job.target_generation)
            return reject("participant identity is stale");
        if(candidate_chair&&*candidate_chair<=0)return reject("candidate chair identity is invalid");
        if(job.unplaced_room_failure){
            RouteJobSnapshot value;static_cast<LocalRouteSnapshot&>(value)=local_snapshot(job);
            value.unplaced_room_failure=job.unplaced_room_failure;
            value.failure_pending=job.failure_pending;value.failure_tree_invoked=job.failure_tree_invoked;
            value.failure_code=job.failure_code;value.failure_blocker=job.failure_blocker;value.failure_reason=job.failure_reason;
            value.external_frame_active=job.external_frame_active;value.external_frame_call=job.external_frame_call;
            const auto snapshot=vm_->snapshot();const VmPending* pending=nullptr;
            if(snapshot.pending&&snapshot.pending->ticket==job.ticket)pending=&*snapshot.pending;
            for(const auto& p:snapshot.suspended_pending)if(p.ticket==job.ticket)pending=&p;
            for(const auto& c:snapshot.immediate_continuations)if(c.parent_pending&&c.parent_pending->ticket==job.ticket)pending=&*c.parent_pending;
            if(!pending||!pending->instruction.depth||pending->instruction.depth>snapshot.stack.size())return reject("unplaced failure lost VM owner");
            value.pending=*pending;value.route_frame=snapshot.stack[pending->instruction.depth-1];
            if(!is_source_unplaced_room_failure(value))return reject("unplaced failure has no authenticated pre-selection lifecycle");
            return false; // InitRoutes failed before the source ever selected CurRoute.
        }
        if(job.deferred_room_route){
            // InitRoutes(SLOT,target) calls AttemptRoute(null) before room
            // traversal. Route45 and supported source27 (including its
            // transient exact/ranged SLOT) preserve that pre-selection request.
            // No CurRoute exists yet: this is a known source false,
            // not an unknown placement result or an assumed free destination.
            const bool no_selected_goal=job.path.empty()&&job.next==1&&job.entry_flags==0&&job.facing==0&&
                job.frame.state==RouteFrameState::room_portal&&job.room_context_open&&
                job.fallback_goals.empty()&&!job.arrival_radian_direction&&!job.target_anchor_radian_direction&&
                job.replay_frame&&job.replay_instruction&&
                job.face_anywhere==(job.replay_instruction->opcode==27&&job.replay_instruction->operands[3]==255)&&
                (job.replay_instruction->opcode==45||source_exact_relative(*job.replay_instruction)||
                 is_source_ranged_goto_relative_instruction(*job.replay_instruction)||
                 is_source_ordinary_go_here_instruction(*job.replay_instruction))&&
                job.replay_frame->context.caller==job.actor&&
                job.replay_frame->context.stack_object==job.target&&
                !job.selected_sitting_object&&!job.source_circle&&!job.source_range&&
                !job.source_sparse&&!job.look_towards;
            if(no_selected_goal)return false;
            // Other route shapes need their own retained selected-choice
            // evidence; emptiness of a coarse path is not sufficient proof.
            return reject("deferred route has no proven SLOT pre-selection state");
        }
        if(job.source_circle&&job.source_range)return reject("selected destination kinds overlap");
        if(job.source_choices){
            if(job.failure_pending){std::string why;if(!validate_source_slot_choices(job,&why))
                return reject("failed selected destination manifest is not authenticated: "+why);}
            const auto& selected=job.source_choices->selected;
            if(selected.chair){const auto* chair=world_.find(selected.chair->id);
                if(!chair||chair->generation!=selected.chair->generation)return reject("selected chair identity is stale");
                if(candidate_chair==std::optional<std::int16_t>{selected.chair->id})return true;}
            const auto dx=static_cast<std::int64_t>(point.x)-selected.position.x,dy=static_cast<std::int64_t>(point.y)-selected.position.y;
            // CurRoute survives failed AttemptWalk. Its source occupancy is
            // the inclusive VMObstacle rectangle, independent of a motion path.
            return dx>=-6&&dx<=6&&dy>=-6&&dy<=6;
        }
        if(job.path.empty()||!job.next||job.next>job.path.size())
            return reject("selected destination path is unavailable");
        if(candidate_chair&&job.selected_sitting_object==candidate_chair){
            // The common direct-chair request carries the selected chair's
            // generation as its target identity. Other sitting discoveries
            // need their own retained identity before an ID can prove equality.
            if(job.selected_sitting_object!=std::optional<std::int16_t>{job.target})
                return reject("selected chair lacks a retained generation");
            if(target->kind!=EntityKind::object)return reject("selected chair is not an object");
            return true;
        }
        // Older sitting routes retain an approach endpoint, which need not
        // be CurRoute.Position. Do not replace an unknown semantic destination
        // with that endpoint. The same-chair proof above remains sufficient.
        if(job.selected_sitting_object&&!job.source_circle&&!job.source_range&&
           !(job.source_sparse&&job.source_sparse->choices))
            return reject("selected sitting destination position was not retained");
        const auto goal=job.source_circle?job.source_circle->selected.position:
            job.source_range?job.source_range->selected.position:
            job.source_sparse&&job.source_sparse->choices?job.source_sparse->choices->selected.position:
            job.path.back();
        const auto dx=static_cast<std::int64_t>(point.x)-goal.x,dy=static_cast<std::int64_t>(point.y)-goal.y;
        return dx>=-6&&dx<=6&&dy>=-6&&dy<=6;
    };
    if(test(*job_))return true;
    for(const auto& parent:suspended_jobs_)if(test(parent))return true;
    if(unknown.empty())return false;
    if(diagnostic)*diagnostic=std::move(unknown);
    return std::nullopt;
}
std::optional<bool> Routing::try_next_circle_goal() {
    if(!job_||(!job_->source_circle&&!job_->source_range)||!vm_||vm_->stack().empty())return false;
    const auto& remaining=job_->source_range?job_->source_range->fallback_goals:job_->source_circle->fallback_goals;
    if(remaining.empty())return false;
    const auto* actor=world_.find(job_->actor);const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!target||actor->generation!=job_->actor_generation||target->generation!=job_->target_generation||
       !actor->position||!(*actor->position==job_->expected)||actor->attachment)return false;
    const auto expected=*actor->position;const auto direction=actor->radian_direction;
    if(route_grid_refresh_&&!route_grid_refresh_(expected)){
        diagnostic_="source circle retry grid refresh is unavailable";return std::nullopt;
    }
    const auto frame=vm_->stack().back();
    std::vector<PreRankedSlotGoal> accepted;
    for(const auto& goal:remaining){
        if(!grid().contains(goal.position))continue;
        if(goal_validator_&&!goal_validator_(frame,goal.position))continue;
        PreRankedSlotGoal value{goal.position,goal.entry_flags,goal.facing};value.radian_direction=goal.radian_direction;
        accepted.push_back(value);
    }
    actor=world_.find(job_->actor);target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!target||actor->generation!=job_->actor_generation||target->generation!=job_->target_generation||
       !actor->position||!(*actor->position==expected)||actor->radian_direction!=direction||actor->attachment){
        diagnostic_="source circle participants changed during retry verification";return std::nullopt;
    }
    if(accepted.empty())return false;
    const auto result=find_pre_ranked_slot_route(grid(),{actor->position->x,actor->position->y},accepted,limits_);
    if(result.path.status==RouteStatus::budget_exhausted||result.path.status==RouteStatus::invalid_input){
        diagnostic_="source circle retry search reached an unsupported input or work bound";return std::nullopt;
    }
    if(result.path.status!=RouteStatus::found||!result.selected)return false;
    const auto& selected=accepted[*result.selected];
    auto& selected_goal=job_->source_range?job_->source_range->selected:job_->source_circle->selected;
    auto& fallback=job_->source_range?job_->source_range->fallback_goals:job_->source_circle->fallback_goals;
    selected_goal={selected.position,selected.entry_flags,selected.facing,*selected.radian_direction};
    fallback.clear();job_->fallback_goals.clear();
    for(std::size_t n=*result.selected+1;n<accepted.size();++n){
        const auto& goal=accepted[n];fallback.push_back({goal.position,goal.entry_flags,goal.facing,*goal.radian_direction});
        job_->fallback_goals.push_back(goal);
    }
    job_->path=result.path.points;job_->next=1;job_->entry_flags=selected.entry_flags;job_->facing=selected.facing;
    job_->arrival_radian_direction=selected.radian_direction;
    job_->frame.state=job_->path.size()==1?RouteFrameState::turn_only:RouteFrameState::walking;
    diagnostic_.clear();return true;
}
bool route_local_requires_dense_grid(const LocalRouteSnapshot& job) noexcept {
    return !job.source_sparse&&!job.source_choices&&!job.look_towards;
}
bool route_job_requires_dense_grid(const RouteJobSnapshot& job) noexcept {
    return route_local_requires_dense_grid(job)&&!job.unplaced_room_failure&&!job.deferred_room_route;
}
bool route_stack_requires_dense_grid(const RouteStackSnapshot& saved) noexcept {
    return std::any_of(saved.jobs.begin(),saved.jobs.end(),route_job_requires_dense_grid);
}
bool Routing::prepare_dense_grid(const VmFrame& frame,const Instruction& instruction,const VmDispatch& dispatch){
    if(grid_)return true;
    if(!dense_grid_provider_){diagnostic_="legacy routing requires its actual prepared dense grid";return false;}
    if(!placement_source_matches(frame,instruction,dispatch,true)){
        diagnostic_="dense grid preparation lacks the actual primitive or authenticated portal replay";return false;
    }
    const auto* actor=world_.frame_entity(frame,VmEntityRole::caller);
    const auto* target=world_.frame_entity(frame,VmEntityRole::stack_object);
    if(!actor||!target){diagnostic_="dense grid preparation lost its live participants";return false;}
    const auto actor_ref=WorldEntityReference{actor->id,actor->generation},target_ref=WorldEntityReference{target->id,target->generation};
    const auto actor_position=actor->position,target_position=target->position;
    const auto actor_attachment=actor->attachment,target_attachment=target->attachment;
    const auto run=vm_->run_generation();
    RouteGrid* prepared=nullptr;std::string error;
    try {
        struct Guard{bool& busy;explicit Guard(bool& value):busy(value){busy=true;}~Guard(){busy=false;}} guard(executing_);
        prepared=dense_grid_provider_(&error);
    }catch(const std::exception& failure){diagnostic_=std::string("dense route grid preparation failed: ")+failure.what();return false;}
    if(!prepared){diagnostic_=error.empty()?"dense route grid preparation was refused":std::move(error);return false;}
    actor=world_.resolve_reference(actor_ref);target=world_.resolve_reference(target_ref);
    if(!actor||!target||!(actor->position==actor_position)||!(target->position==target_position)||
       !placement_attachment_equal(actor->attachment,actor_attachment)||!placement_attachment_equal(target->attachment,target_attachment)||
       vm_->run_generation()!=run||!placement_source_matches(frame,instruction,dispatch,true)){
        diagnostic_="dense grid preparation changed its actual source participants or dispatch";return false;
    }
    // This is additive geometry for the legacy request. Any existing parent is
    // a gridless source lifecycle, since an admitted legacy parent owns a grid.
    grid_=prepared;return true;
}
bool Routing::attach_grid(RouteGrid& value){
    if(executing_||grid_)return false;
    if(job_&&!job_->unplaced_room_failure)return false;
    for(const auto& parent:suspended_jobs_)if(!parent.unplaced_room_failure)return false;
    grid_=&value;return true;
}
std::optional<bool> source_route_failure_tree_policy(const Instruction& op) noexcept {
    if(op.opcode==27){const auto relative=decode_source_goto_relative_position(op);
        return relative?std::optional<bool>{!relative->no_failure_trees}:std::nullopt;}
    const auto scope=op.operands[2]|(op.operands[3]<<8);
    if(op.opcode!=45||scope>2||(op.operands[4]&~1U)||op.operands[5]||op.operands[6]||op.operands[7])return {};
    return (op.operands[4]&1U)==0;
}
bool is_source_unplaced_room_failure(const RouteJobSnapshot& job){
    if(!job.unplaced_room_failure)return false;
    const auto& origin=*job.unplaced_room_failure;const auto& op=job.pending.instruction.instruction;
    const auto policy=source_route_failure_tree_policy(op);if(!policy)return false;
    std::size_t index=0;
    if(op.opcode==27){const auto transient=make_source_goto_relative_slot(op);
        if(!transient||origin.slot_version!=transient->version||origin.slot_index||
           !same_slot_record(origin.slot,transient->records.front()))return false;
    }else{
        const auto scope=op.operands[2]|(op.operands[3]<<8);index=op.operands[0]|(op.operands[1]<<8);
        if(scope==0){if(index>=job.route_frame.arguments.size()||job.route_frame.arguments[index]<0)return false;
            index=static_cast<std::size_t>(job.route_frame.arguments[index]);}
    }
    if(origin.slot_version<4||origin.slot_version>10||origin.slot_index!=index||origin.slot_index>65535||
       origin.slot.type!=3||(origin.slot.flags&0x0800U)||!origin.target_room||
       job.failure_code!=2||job.failure_blocker||job.failure_reason.empty()||
       (job.call_failure_trees&&!*policy)||job.failure_pending==job.failure_tree_invoked||
       (job.failure_tree_invoked&&(!job.call_failure_trees||!is_source_failure_tree_continuation(job)))||
       (job.failure_pending&&(job.external_frame_active||(job.frame.state!=RouteFrameState::initial&&job.frame.state!=RouteFrameState::failed)))||
       job.deferred_room_route||job.room_context_open||job.room_route_replan_active||
       job.portal_steps||job.portal_retries||job.portal_replan_required||job.external_frame_purpose||job.external_frame_callee_generation||
       job.replay_frame||job.replay_instruction||job.source_circle||job.source_range||job.source_choices||job.source_sparse||job.look_towards||
       job.walking_started||job.same_room_verified||!job.path.empty()||job.next!=1||job.entry_flags||job.facing||job.face_anywhere||
       job.arrival_radian_direction||job.target_anchor_radian_direction||job.selected_sitting_object||!job.fallback_goals.empty()||
       job.sparse_consideration||!job.carried_target.empty()||job.external_frame_sitting||
       !(job.expected==EntityPosition{-32768,-32768,1,0}))return false;
    return true;
}
PrimitiveOutcome Routing::request(const VmFrame& frame,const Instruction& i,const VmDispatch& dispatch,RouteOutcomeReceipt* immediate) {
    SourceResetOperation source_operation(*this);
    try {
    // Pinned Execute returns before creating a routing frame or reading any
    // operand geometry in check mode and for null/OOW StackObject. In
    // particular these queries cannot invalidate a live route's receipts.
    if(i.opcode==45&&dispatch.check_mode)return {PrimitiveDisposition::goto_false,{}};
    if(i.opcode==27){
        if(executing_)return unsupported("routing callback cannot reenter controller");
        if(!vm_||dispatch.origin!=vm_)return unsupported("routing controller bound to another VM");
        if(dispatch.check_mode)return {PrimitiveDisposition::goto_false,{}};
        const auto* target=world_.frame_entity(frame,VmEntityRole::stack_object);
        if(!target||!target->position)return {PrimitiveDisposition::goto_false,{}};
    }
    std::optional<RouteGoalSelectionReceipt> goal_receipt;
    bool goal_receipt_emitted=false;
    const auto report_goal_selection=[&](const char* outcome,const std::string& reason){
        if(!goal_receipt||goal_receipt_emitted||!goal_selection_observer_)return;
        goal_receipt_emitted=true;
        goal_receipt->outcome=outcome;goal_receipt->reason=reason.substr(0,1024);
        // Observation has no source VM, placement proof or dispatch authority.
        // Block controller reentry/reconfiguration while the host copies values.
        struct Guard {bool& flag;bool before;explicit Guard(bool& value):flag(value),before(value){flag=true;}
            ~Guard(){flag=before;}} guard(executing_);
        goal_selection_observer_(*goal_receipt);
    };
    const auto unsupported=[&](const std::string& why){
        report_goal_selection("unsupported",why);
        return PrimitiveOutcome{PrimitiveDisposition::unsupported,why};
    };
    route_mutated();
    const bool look_towards=i.opcode==22;
    if(look_towards){
        if(i.operands[0]==1)return {PrimitiveDisposition::goto_true,{}};
        if(i.operands[0]<2||i.operands[0]>5)return unsupported("LookTowards mode requires another native service");
        if(dispatch.check_mode)return {PrimitiveDisposition::goto_false,{}};
    }
    const bool goto_relative=i.opcode==27;
    if(i.opcode!=45&&!goto_relative&&!look_towards) return {};
    if(executing_) return unsupported("routing callback cannot reenter controller");
    if(!vm_ || dispatch.origin!=vm_) return unsupported("routing controller bound to another VM");
    bool nested_request=false;
    if(job_) {
        if(job_->run_generation!=vm_->run_generation()) {
            end_room_context(*job_);
            if(const auto* actor=world_.find(job_->actor);actor&&actor->generation==job_->actor_generation&&!job_->look_towards)
                world_.set_static_footprint(job_->actor,true);
            for(auto& parent:suspended_jobs_){
                end_room_context(parent);
                if(const auto* actor=world_.find(parent.actor);actor&&actor->generation==parent.actor_generation&&!parent.look_towards)
                    world_.set_static_footprint(parent.actor,true);
            }
            job_.reset();suspended_jobs_.clear();parked_immediate_routes_.clear();
        } else {
            const auto& stack=vm_->stack();
            const bool in_external_parent=job_->external_frame_active&&
                std::any_of(stack.begin(),stack.end(),[&](const auto& active_frame){
                    return active_frame.return_mode==VmCallReturnMode::external_frame_next_tick&&
                        active_frame.invocation_ticket==job_->ticket&&
                        active_frame.context.code_owner==job_->external_frame_owner;
                });
            if(!in_external_parent&&!parked_for_immediate())
                return unsupported("routing request already active outside a portal BHAV child frame");
            if(suspended_jobs_.size()+1>=max_nested_jobs_)
                return unsupported("nested routing frame budget exhausted");
            nested_request=true;
        }
    }
    diagnostic_.clear();
    if(look_towards)return request_look_towards(frame,i,dispatch,nested_request);
    const auto* relative_target=goto_relative?world_.frame_entity(frame,VmEntityRole::stack_object):nullptr;
    const bool ordinary_go_here_relative=relative_target&&!relative_target->ui_ghost&&is_source_ordinary_go_here_instruction(i);
    const bool saved_go_here_relative=goto_relative&&
        (i.operands==std::array<std::uint8_t,8>{10,0,254,255,10,0,5,0}||
         (source_exact_relative(i)&&relative_failure_trees(i))||ordinary_go_here_relative);
    // Preserve the already admitted historical UI-marker/grid request. Its
    // exact old raw instruction plus explicit ghost target selects that legacy
    // path, not bit4 (which the source never uses). Persisted marker ownership
    // is separately authenticated by the session/host manifest.
    const bool legacy_ui_go_here=relative_target&&is_legacy_ui_go_here_request(i,relative_target->ui_ghost);
    const bool bounded_goto_relative=source_exact_relative(i)&&!legacy_ui_go_here;
    // Both 16-bit counters are ignored by the source Execute. Location picks
    // a sector mask or AnywhereNear; Direction independently selects facing.
    const bool ranged_goto_relative=is_source_ranged_goto_relative_instruction(i);
    const bool stair_goto_relative_shape=goto_relative&&i.operands[0]==0&&i.operands[1]==0&&
        i.operands[2]==254&&(i.operands[3]==0||i.operands[3]==4)&&i.operands[4]==0&&
        i.operands[5]==0&&i.operands[6]==0&&i.operands[7]==0;
    bool stair_goto_relative=false;
    if(stair_goto_relative_shape&&stair_goto_relative_) {
        struct Guard {bool& flag;bool previous;explicit Guard(bool& value):flag(value),previous(value){flag=true;}~Guard(){flag=previous;}} guard(executing_);
        stair_goto_relative=stair_goto_relative_(frame,i);
    }
    if(goto_relative&&!saved_go_here_relative&&!bounded_goto_relative&&
       !ranged_goto_relative&&!stair_goto_relative)
        return unsupported("GotoRelativePosition outside the synthetic route profiles or supported source relative geometry");
    const auto data=static_cast<std::uint16_t>(i.operands[0]|(i.operands[1]<<8));
    const auto scope=static_cast<std::uint16_t>(i.operands[2]|(i.operands[3]<<8));
    if(!goto_relative && scope!=0&&scope!=1&&scope!=2)
        return unsupported("routing operand scope requires StackObject StackVariable/Literal or Global");
    if(!goto_relative && ((i.operands[4]&0xFEU)!=0 || i.operands[5] || i.operands[6] || i.operands[7]))
        return unsupported("routing operand flags/tail outside NoFailureTrees profile");
    SlotTable relative_slots{};
    const SlotTable* selected_slots=&globals_;
    if(goto_relative){
        if(const auto source_slot=make_source_goto_relative_slot(i))relative_slots=*source_slot;
        else return unsupported("relative routing instruction lacks a canonical transient SLOT");
        selected_slots=&relative_slots;
    }
    if(!goto_relative && (scope==0||scope==1)) {
        if(!stack_object_slots_)
            return unsupported("StackObject StackVariable/Literal SLOT table provider unavailable");
        struct Guard {bool& flag;bool previous;explicit Guard(bool& value):flag(value),previous(value){flag=true;}~Guard(){flag=previous;}} guard(executing_);
        selected_slots=stack_object_slots_(frame);
        if(!selected_slots)
            return unsupported("StackObject StackVariable/Literal SLOT table missing or ambiguous");
    }
    // GotoRelativePosition's first word is OldTrapCount, not a SLOT index.
    // The temporary relative SLOT is always entry zero.
    auto slot_index=goto_relative?std::size_t{0}:static_cast<std::size_t>(data);
    if(!goto_relative && scope==0) {
        if(data>=frame.arguments.size())
            return unsupported("StackObject StackVariable SLOT argument unavailable");
        if(frame.arguments[data]<0)return {PrimitiveDisposition::goto_false,{}};
        slot_index=static_cast<std::size_t>(frame.arguments[data]);
    }
    const auto* slot=(goto_relative&&!ranged_goto_relative&&!general_slot_provider_)?nullptr:slot_of_type(*selected_slots,3,slot_index);
    const bool general_source_slot=general_slot_provider_&&authenticated_range_goals_&&slot&&(!goto_relative||(relative_target&&!relative_target->ui_ghost&&is_source_goto_relative_slot_instruction(i)));
    const bool child_shy_sitting_fallback=!general_source_slot&&!goto_relative&&scope==2&&selected_slots==&globals_&&slot&&
        supported_child_shy_sitting_fallback_profile(*selected_slots,slot_index,*slot);
    const bool source_range_slot=!general_source_slot&&(!goto_relative||ranged_goto_relative)&&authenticated_range_goals_&&slot&&
        selected_slots->version==10&&is_source_slot_range(*slot);
    const bool ranged_slot=slot&&selected_slots->version==10&&
        (supported_ranged_slot_profile(*slot)||child_shy_sitting_fallback||source_range_slot);
    const bool authenticated_circle_slot=!general_source_slot&&!goto_relative&&authenticated_range_goals_&&slot&&selected_slots->version==10&&is_source_slot_circle(*slot);
    const bool authenticated_source_slot=general_source_slot||source_range_slot||authenticated_circle_slot;
    if(authenticated_source_slot&&replay_placement_authorization_)replay_placement_range_=true;
    const bool source_circle_slot=!general_source_slot&&slot&&selected_slots->version==10&&is_source_slot_circle(*slot)&&
        (authenticated_circle_slot||ranged_slot_goals_||continuous_ranged_slot_goals_||!supported_circle_slot_profile(*slot));
    const bool bed_circle_slot=slot&&selected_slots->version==10&&
        supported_bed_north_circle_slot_profile(*slot);
    const bool point_offset_slot=!general_source_slot&&!source_sparse_services_.plan_input&&slot&&selected_slots->version==10&&
        supported_point_offset_slot_profile(*slot);
    const bool source_point_slot=!general_source_slot&&slot&&selected_slots->version==10&&!point_offset_slot&&
        source_point_slot_profile(*slot);
    const bool sparse_standing_route45=!goto_relative&&slot&&slot->standing>0&&slot->sitting==0&&!child_shy_sitting_fallback&&
        ((ranged_slot||bed_circle_slot||source_circle_slot)?
            (authenticated_source_slot||bool(ranged_slot_goals_)||bool(continuous_ranged_slot_goals_)):
            bool(ranked_goals_));
    const bool source_generic_sparse_slot=general_source_slot||(bool(source_sparse_services_.plan_input)&&sparse_standing_route45&&
        !source_range_slot&&!source_circle_slot&&!source_point_slot);
    // The sparse planner is the source-faithful AttemptWalk backend for every
    // ranked standing goal profile. Resource-specific SLOT manifests remain separate.
    const bool sparse_source_requested=bool(source_sparse_services_.plan_input)&&
        (general_source_slot||sparse_standing_route45||source_point_slot||ranged_goto_relative||
         (bounded_goto_relative&&!stair_goto_relative)||ordinary_go_here_relative);
    if(general_source_slot&&!sparse_source_requested)return unsupported("full SLOT routing requires source AttemptWalk services");
    const auto relative_request=goto_relative?decode_source_goto_relative_position(i):std::nullopt;
    const bool source_failure_trees=goto_relative?
        (relative_request&&!relative_request->no_failure_trees):
        ((source_point_slot||source_circle_slot||source_range_slot||source_generic_sparse_slot)&&(i.operands[4]&1U)==0);
    if(!goto_relative||(relative_request&&!legacy_ui_go_here)){
        // Both source primitives create the same routing frame. Relative27
        // owns this exact transient SLOT even when no installed SLOT scorer
        // is needed for a room search that fails before local planning.
        const auto* unplaced_slot=goto_relative?slot_of_type(*selected_slots,3,0):slot;
        if(!unplaced_slot)return {PrimitiveDisposition::goto_false,{}};
        const auto* actor=world_.frame_entity(frame,VmEntityRole::caller);
        const auto* target=world_.frame_entity(frame,VmEntityRole::stack_object);
        if(!target)return {PrimitiveDisposition::goto_false,{}};
        if(actor&&!actor->position){
            if(actor->kind!=EntityKind::avatar||actor->attachment||!actor->generation||actor->person_data.size()<=62||
               !target->position||target->generation==0||selected_slots->version<4||selected_slots->version>10||
               (unplaced_slot->flags&0x0800U)||!unplaced_room_failure_)
                return unsupported("unplaced source Route45/27 requires a free source avatar, positioned target and complete room proof");
            if(nested_request&&(actor->id!=job_->actor||actor->generation!=job_->actor_generation))
                return unsupported("nested unplaced source Route45/27 changed avatar identity");
            const auto actor_id=actor->id,target_id=target->id;
            const auto actor_generation=actor->generation,target_generation=target->generation;
            const auto target_position=*target->position;
            const auto room=[&]{struct Guard{bool& busy;explicit Guard(bool& value):busy(value){busy=true;}~Guard(){busy=false;}} guard(executing_);
                return unplaced_room_failure_(target_position);}();
            actor=world_.find(actor_id);target=world_.resolve_reference({target_id,target_generation});
            if(!room||!*room||!actor||!target||actor->generation!=actor_generation||actor->position||actor->attachment||
               !target->position||!(*target->position==target_position))
                return unsupported("unplaced source Route45/27 room search is unknown or participants changed");
            if(!source_sparse_services_.source_tick||!source_sparse_services_.end_locomotion)
                return unsupported("unplaced source routing requires authenticated FAILED RNG and PreExit services");
            Job failed{};failed.ticket=dispatch.ticket;failed.actor=actor_id;failed.target=target_id;
            failed.actor_generation=actor_generation;failed.target_generation=target_generation;
            failed.run_generation=vm_->run_generation();failed.expected={-32768,-32768,1,0};
            failed.expected_radian_direction=actor->radian_direction;failed.target_position=target_position;
            const auto failure_policy=source_route_failure_tree_policy(i);
            if(!failure_policy)return unsupported("unplaced source routing operand policy unavailable");
            failed.call_failure_trees=*failure_policy&&!nested_request;
            failed.failure_pending=true;failed.failure_code=2;failed.frame.state=RouteFrameState::failed;
            failed.failure_reason="source InitRoutes has no path from unplaced room 0";
            failed.unplaced_room_failure=SourceUnplacedRoomFailure{selected_slots->version,slot_index,*unplaced_slot,*room};
            if(nested_request)suspended_jobs_.push_back(std::move(*job_));
            job_=std::move(failed);
            if(job_->call_failure_trees){
                VmCall failure{routine_key(398),frame.context,{2,0,0,0}};
                if(!vm_->prepare_primitive_call(frame,i,dispatch,failure))
                    return unsupported("required source failure tree398 failed primitive child preflight");
                const auto depth=vm_->stack().size();const auto run=vm_->run_generation();
                PrimitiveOutcome outcome{PrimitiveDisposition::suspend_and_call,{}};outcome.call=failure;
                outcome.call_commit=[this,frame,i,dispatch,depth,run,failure=std::move(failure),actor_id,actor_generation,target_id,target_generation,target_position]()mutable{
                    if(!vm_||!vm_->matches_active_call_commit(frame,i,dispatch,depth)||vm_->run_generation()!=run||
                       !job_||job_->ticket!=dispatch.ticket||!job_->unplaced_room_failure)return false;
                    const auto* actor=world_.find(actor_id);const auto* target=world_.resolve_reference({target_id,target_generation});
                    if(!actor||actor->generation!=actor_generation||actor->position||actor->attachment||
                       !target||!target->position||!(*target->position==target_position)||
                       !world_.bind_context_live(failure.context))return false;
                    // All identities/allocation preflight precede source publication.
                    // Moving the already owned call cannot allocate after RouteResult.
                    if(!world_.set_person_data(actor_id,62,2))return false;
                    job_->failure_pending=false;job_->failure_tree_invoked=true;
                    job_->external_frame_active=true;job_->external_frame_owner=failure.context.code_owner;
                    job_->external_frame_call.emplace(std::move(failure));
                    job_->frame.state=RouteFrameState::failed;return true;
                };
                return outcome;
            }
            // Source's no-child FAILED frame executes immediately after CONTINUE.
            // No asynchronous wait, motion grid or private VM is introduced.
            if(!vm_->matches_active_primitive(frame,i,dispatch)||!source_sparse_services_.source_tick||
               !source_sparse_services_.end_locomotion)
                return unsupported("source inline FAILED frame requires authenticated RNG and PreExit services");
            if(!world_.set_person_data(actor_id,62,2))return unsupported("source RouteResult is unavailable");
            SourceSparseMotionContext context;context.vm=vm_;context.frame=frame;context.dispatch=dispatch;
            context.actor=actor_id;context.actor_generation=actor_generation;context.run_generation=vm_->run_generation();
            context.actor_unplaced=true;context.position={-32768,-32768,1,0};
            context.root_route=suspended_jobs_.empty();context.lifecycle_instruction=i;
            const auto matches=[&]{const auto* current=world_.find(actor_id);
                return vm_->matches_active_primitive(frame,i,dispatch)&&job_&&job_->ticket==dispatch.ticket&&
                    current&&current->generation==actor_generation&&!current->position&&!current->attachment;};
            std::string exit_error;
            {struct Guard{bool& busy;explicit Guard(bool& value):busy(value){busy=true;}~Guard(){busy=false;}} guard(executing_);
                if(!source_sparse_services_.source_tick(context)||!matches())
                    return unsupported("source inline FAILED frame RNG rejected its active primitive");
                if(!source_sparse_services_.end_locomotion(context,WalkMotionProfile{},&exit_error)||!matches())
                    return unsupported(exit_error.empty()?"source inline FAILED frame PreExit rejected":exit_error);
            }
            if(context.root_route){job_.reset();last_tick_.reset();}
            else{job_=std::move(suspended_jobs_.back());suspended_jobs_.pop_back();}
            return {PrimitiveDisposition::goto_false,"source InitRoutes has no room route"};
        }
    }
    if(!goto_relative||ranged_goto_relative) {
        if(!slot) return {PrimitiveDisposition::goto_false,{}};
        // Door range routing has separate explicit scoring and host validation.
        if(!general_source_slot&&(selected_slots->version!=10 ||
           (!supported_circle_slot_profile(*slot)&&!ranged_slot&&!point_offset_slot&&!source_point_slot&&!source_circle_slot&&
            !(sparse_source_requested&&sparse_standing_route45)))) {
            const auto proximity=slot->proximity?
                std::to_string((*slot->proximity)[0])+","+
                std::to_string((*slot->proximity)[1])+","+
                std::to_string((*slot->proximity)[2]):"none";
            return unsupported("SLOT runtime profile not implemented (version="+
                std::to_string(selected_slots->version)+", index="+std::to_string(slot_index)+
                ", flags="+std::to_string(slot->flags)+", facing="+
                (slot->facing?std::to_string(*slot->facing):"none")+", standing="+
                std::to_string(slot->standing)+", sitting="+std::to_string(slot->sitting)+
                ", proximity="+proximity+")");
        }
        if(slot->sitting!=0&&!child_shy_sitting_fallback&&!general_source_slot)
            return unsupported("sitting-weighted SLOT profile requires an exact supported fallback path");
        if(ranged_slot&&!ranged_slot_goals_&&!continuous_ranged_slot_goals_&&!authenticated_source_slot)
            return unsupported("ranged SLOT requires an explicit scoring provider");
        if(child_shy_sitting_fallback&&!sitting_slot_candidate_preflight_)
            return unsupported("child-shy sitting SLOT requires a nearby-chair preflight");
        if(ranged_slot&&!goal_validator_&&!sparse_source_requested)
            return unsupported("ranged SLOT requires a host room/clearance validator");
        if(bed_circle_slot&&!ranged_slot_goals_&&!continuous_ranged_slot_goals_&&!authenticated_circle_slot)
            return unsupported("bed circle SLOT requires an explicit scoring provider");
        if(bed_circle_slot&&!goal_validator_&&!sparse_source_requested)
            return unsupported("bed circle SLOT requires a host room/clearance validator");
        if(source_circle_slot&&((!continuous_ranged_slot_goals_&&!authenticated_circle_slot)||(!goal_validator_&&!sparse_source_requested)))
            return unsupported("source circle requires continuous ranked goals and host geometry validation");
        if((point_offset_slot||source_point_slot)&&!goal_validator_&&!(source_point_slot&&sparse_source_requested))
            return unsupported("exact point SLOT requires a host bounds/wall/placement validator");
        if((slot->flags&0x0800U)!=0&&!goal_validator_&&!sparse_source_requested)
            return unsupported("IgnoreRooms SLOT requires a host validator for the active single-room grid");
    }
    const auto* actor=world_.frame_entity(frame,VmEntityRole::caller);
    const auto* target=world_.frame_entity(frame,VmEntityRole::stack_object);
    // Execute returns false for a null/out-of-world StackObject before routing.
    if(goto_relative&&(!target||!target->position))return {PrimitiveDisposition::goto_false,{}};
    const bool source_relative_route=goto_relative&&(source_exact_relative(i)||ranged_goto_relative||saved_go_here_relative);
    // VMGotoRoutingSlot.InitRoutes uses the target's room before selecting
    // any local SLOT goal. Only the authenticated SLOT path can replay its
    // original request after a portal changes the actor's floor.
    const bool cross_floor_slot=!goto_relative&&general_source_slot&&!(slot->flags&0x0800U)&&route_grid_refresh_;
    const bool cross_floor_room_route=(source_relative_route||cross_floor_slot)&&room_pair_&&room_portal_step_;
    const bool source_stand_before_walk=general_source_slot&&actor&&actor->person_data.size()>0&&actor->person_data[0]==1;
    const auto initial_attachment=actor?actor->attachment:std::optional<Attachment>{};
    if(!actor || !target || !actor->position || !target->position || actor->kind!=EntityKind::avatar ||
       actor->person_data.size()<=37 || (actor->attachment&&!source_stand_before_walk) ||
       (actor->position->level!=target->position->level&&!cross_floor_room_route)) {
        std::ostringstream detail;detail<<"routing requires positioned free avatar and target on one level";
        detail<<"; opcode="<<i.opcode<<" operands=[";
        for(std::size_t n=0;n<i.operands.size();++n)detail<<(n?",":"")<<unsigned(i.operands[n]);
        detail<<"] caller_ref="<<frame.context.caller<<"/"<<frame.context.caller_generation
              <<" stack_ref="<<frame.context.stack_object<<"/"<<frame.context.stack_object_generation
              <<" bound="<<frame.context.entity_references_bound;
        const auto describe=[&](const char* role,const Entity* entity){
            detail<<"; "<<role<<"={";
            if(!entity){detail<<"unresolved}";return;}
            detail<<"id="<<entity->id<<",gen="<<entity->generation<<",kind="<<(entity->kind==EntityKind::avatar?"avatar":"object")
                  <<",guid="<<(entity->definition?std::to_string(entity->definition->guid):"absent")
                  <<",dead="<<entity->dead<<",ui_ghost="<<entity->ui_ghost<<",words="<<entity->person_data.size()
                  <<",posture="<<(entity->person_data.empty()?"absent":std::to_string(entity->person_data[0]))<<",pose=";
            if(entity->position)detail<<entity->position->x<<","<<entity->position->y<<","<<entity->position->level<<","<<unsigned(entity->position->direction);
            else detail<<"absent";
            detail<<",attachment=";
            if(entity->attachment){const auto& a=*entity->attachment;const auto* container=world_.find(a.container);
                detail<<a.container<<"/"<<(container?container->generation:0)<<",slot="<<a.slot<<",positioned="<<a.positioned;}
            else detail<<"absent";
            detail<<"}";
        };
        describe("actor",actor);describe("target",target);
        detail<<"; source_relative="<<source_relative_route<<",general_slot="<<general_source_slot
              <<",sparse="<<sparse_source_requested<<",stand_before_walk="<<source_stand_before_walk
              <<",cross_floor_room_route="<<cross_floor_room_route;
        return unsupported(detail.str());
    }
    if(nested_request && (actor->id!=job_->actor || actor->generation!=job_->actor_generation))
        return unsupported("nested Route45 changed the active avatar identity");
    if(!sparse_source_requested&&!prepare_dense_grid(frame,i,dispatch))return unsupported(diagnostic_);
    std::optional<std::vector<LiveAttemptWalkIdentity>> captured_sparse_consideration;
    if(sparse_source_requested){
        if(!source_sparse_services_.capture_avatars_to_consider)
            return unsupported("source sparse routing has no Init-time avatars-to-consider capture");
        std::optional<std::vector<LiveAttemptWalkIdentity>> inherited=replay_sparse_consideration_;
        if(!inherited&&nested_request&&job_){
            if(job_->sparse_consideration)inherited=job_->sparse_consideration;
            else if(job_->source_sparse)inherited=job_->source_sparse->avatars_to_consider;
        }
        const auto captured=capture_source_sparse_consideration(frame,i,dispatch,actor->id,actor->generation,
            target->id,target->generation,*actor->position,*target->position,inherited);
        if(!captured)return unsupported(diagnostic_.empty()?"source sparse Init-time avatar state is unknown":diagnostic_);
        captured_sparse_consideration=*captured;
    }
    if(child_shy_sitting_fallback) {
        if(target->kind!=EntityKind::avatar)
            return unsupported("child-shy sitting SLOT requires an avatar target");
        const auto actor_id=actor->id,target_id=target->id;
        const auto actor_generation=actor->generation,target_generation=target->generation;
        const auto actor_position=*actor->position,target_position=*target->position;
        std::optional<std::string> preflight_diagnostic;
        {
            struct Guard {bool& flag;bool previous;explicit Guard(bool& value):flag(value),previous(value){flag=true;}~Guard(){flag=previous;}} guard(executing_);
            preflight_diagnostic=sitting_slot_candidate_preflight_(*selected_slots,slot_index,
                actor_position,target_position);
        }
        actor=world_.find(actor_id);target=world_.resolve_reference({target_id,target_generation});
        if(!actor||!target||actor->generation!=actor_generation||target->generation!=target_generation||
           !actor->position||!target->position||!(*actor->position==actor_position)||
           !(*target->position==target_position)||actor->attachment)
            return unsupported("entity changed during child-shy sitting SLOT preflight");
        if(preflight_diagnostic)return unsupported(*preflight_diagnostic);
    }
    bool same_room_verified=false;
    if((!goto_relative||source_relative_route)&&room_pair_) {
        const auto actor_id=actor->id,target_id=target->id;
        const auto actor_generation=actor->generation,target_generation=target->generation;
        const auto actor_position=*actor->position,target_position=*target->position;
        const auto actor_direction=actor->radian_direction;
        const auto rooms=[&] {
            struct Guard {bool& flag;bool previous;explicit Guard(bool& value):flag(value),previous(value){flag=true;}~Guard(){flag=previous;}} guard(executing_);
            return room_pair_(actor_position,target_position);
        }();
        actor=world_.find(actor_id);target=world_.resolve_reference({target_id,target_generation});
        if(!actor||!target||actor->generation!=actor_generation||target->generation!=target_generation||
           !actor->position||!target->position||!(*actor->position==actor_position)||
           !(*target->position==target_position)||actor->radian_direction!=actor_direction||
           !placement_attachment_equal(actor->attachment,initial_attachment))
            return unsupported("entity changed during Route45 room resolver");
        if(!rooms)return unsupported("Route45 room resolver could not classify actor and target");
        const bool ignore_rooms=!goto_relative&&(slot->flags&0x0800U)!=0;
        const bool floor_transition=cross_floor_room_route&&
            actor->position->level!=target->position->level;
        if((rooms->first!=rooms->second||floor_transition)&&!ignore_rooms) {
            if(!room_portal_step_)
                return unsupported("cross-room Route45 requires a bounded Portal15 step provider");
            RouteFrameContext frame_context;frame_context.state=RouteFrameState::room_portal;
            if(nested_request)suspended_jobs_.push_back(std::move(*job_));
            job_=Job{dispatch.ticket,actor->generation,target->generation,actor->id,target->id,
                     *actor->position,*target->position,actor->radian_direction,frame_context,
                     {},1,0,0};
            job_->run_generation=vm_->run_generation();
            job_->carried_target=capture_carried_target(world_,*actor,*target);
            job_->call_failure_trees=source_failure_trees&&!nested_request;
            if(bounded_goto_relative||ranged_goto_relative||ordinary_go_here_relative)job_->face_anywhere=i.operands[3]==255;
            job_->deferred_room_route=true;
            job_->sparse_consideration=captured_sparse_consideration;
            job_->room_context_open=true;
            job_->replay_frame=frame;
            job_->replay_instruction=i;
            return {PrimitiveDisposition::wait,{}};
        }
        same_room_verified=rooms->first==rooms->second&&
            actor->position->level==target->position->level;
    }
    const RoutePoint start{actor->position->x,actor->position->y};
    std::optional<std::string> execution_callback_error;
    const auto execution_actor_id=actor->id,execution_target_id=target->id;
    const auto execution_actor_generation=actor->generation,execution_target_generation=target->generation;
    const auto execution_actor_position=*actor->position,execution_target_position=*target->position;
    const auto execution_actor_radians=actor->radian_direction,execution_target_radians=target->radian_direction;
    const auto execution_actor_attachment=actor->attachment,execution_target_attachment=target->attachment;
    const auto refresh_execution_participants=[&]()->bool {
        auto* current_actor=world_.find(execution_actor_id);
        // Only the moving actor needs live mutation authority. The target is
        // the same exact source anchor already read by the placement proof.
        const auto* current_target=world_.resolve_reference({execution_target_id,execution_target_generation});
        if(!current_actor||!current_target||current_actor->generation!=execution_actor_generation||
           current_target->generation!=execution_target_generation||!current_actor->position||!current_target->position||
           !(*current_actor->position==execution_actor_position)||!(*current_target->position==execution_target_position)||
           current_actor->radian_direction!=execution_actor_radians||
           (current_target->kind==EntityKind::avatar&&current_target->radian_direction!=execution_target_radians)||
           !placement_attachment_equal(current_actor->attachment,execution_actor_attachment)||
           !placement_attachment_equal(current_target->attachment,execution_target_attachment))return false;
        actor=current_actor;target=current_target;
        return true;
    };
    std::size_t execution_candidate_index=0;
    const auto check_attempt_walk=[&](RoutePoint point)->bool {
        if(execution_callback_error)return false;
        const auto index=execution_candidate_index++;
        if(!attempt_walk_execution_)return true;
        if(auto error=invoke_route_execution_callback(RouteExecutionPhase::attempt_walk,frame,i,dispatch,
            execution_actor_id,execution_actor_generation,execution_target_id,execution_target_generation,
            execution_actor_position,execution_target_position,
            point,index)) {
            execution_callback_error=*error;
            return false;
        }
        if(!refresh_execution_participants()) {
            execution_callback_error="source route attempt callback changed or invalidated its participants";
            return false;
        }
        return true;
    };
    const auto allow_goal=[&](RoutePoint point) {
        if(execution_callback_error)return false;
        if(!check_attempt_walk(point))return false;
        if(!goal_validator_)return true;
        struct Guard {bool& flag;bool previous;explicit Guard(bool& value):flag(value),previous(value){flag=true;}~Guard(){flag=previous;}} guard(executing_);
        const auto accepted=goal_validator_(frame,point);
        if(source_reset_retired_)throw SourceCheckResetRetirement(vm_);
        if(!refresh_execution_participants()) {
            execution_callback_error="route goal validator changed or invalidated its participants";
            return false;
        }
        return accepted;
    };
    if(immediate){immediate->actor={actor->id,actor->generation};immediate->target={target->id,target->generation};
        immediate->actor_position=actor->position;immediate->target_position=target->position;
        immediate->nested_depth=suspended_jobs_.size()+(nested_request?1U:0U);}
    // CurRoute is selected before AttemptWalk. A rejected walk does not erase
    // that source destination: other avatars still consult it during failure398.
    std::optional<SourceSlotChoicesSnapshot> last_attempted_choice;
    const auto remember_attempt=[&](const SourceSlotChoiceGoal& goal){
        // Exact source27 intentionally has no selection `slot` so it follows
        // its direct-point branch. Its already-generated canonical transient
        // SLOT still authenticates CurRoute; do not change branch selection.
        const auto* retained_slot=slot?slot:(goto_relative&&selected_slots?
            slot_of_type(*selected_slots,3,slot_index):nullptr);
        if(!retained_slot||!selected_slots||selected_slots->version<4||selected_slots->version>10||retained_slot->type!=3)return;
        SourceSlotChoicesSnapshot choice;choice.slot_version=selected_slots->version;
        choice.slot_index=slot_index;choice.slot=*retained_slot;choice.target_radian_direction=source_slot_object_radians(*target);
        choice.selected=goal;last_attempted_choice=std::move(choice);
    };
    const auto fail_route=[&](std::int16_t code,const std::string& reason,std::int16_t blocker=0) -> PrimitiveOutcome {
        report_goal_selection("failed",reason);
        diagnostic_=reason;
        if(stair_goto_relative)
            return unsupported("stair GotoRelativePosition failure tree is not implemented: "+reason);
        if(!source_failure_trees||nested_request){
            if(actor->person_data.size()>62&&!world_.set_person_data(actor->id,62,code))return unsupported("source RouteResult write failed");
            if(immediate){immediate->failure_code=code;immediate->blocker.id=blocker;
                if(const auto* current=world_.find(blocker)){immediate->blocker.generation=current->generation;immediate->blocker_generation_is_current_lookup=true;}}
            return {PrimitiveDisposition::goto_false,reason};
        }
        Job failed{};
        failed.ticket=dispatch.ticket;
        failed.actor_generation=actor->generation;failed.target_generation=target->generation;
        failed.actor=actor->id;failed.target=target->id;
        failed.expected=*actor->position;failed.target_position=*target->position;
        failed.expected_radian_direction=actor->radian_direction;
        failed.run_generation=vm_->run_generation();
        failed.call_failure_trees=true;failed.failure_pending=true;
        failed.failure_code=code;failed.failure_blocker=blocker;failed.failure_reason=reason;
        if(last_attempted_choice){
            const auto& choice=last_attempted_choice->selected;
            failed.path={{actor->position->x,actor->position->y}}; // Unstarted, not a motion path.
            failed.entry_flags=choice.entry_flags;failed.facing=choice.facing;failed.face_anywhere=choice.face_anywhere;
            failed.arrival_radian_direction=choice.radian_direction;
            failed.selected_sitting_object=choice.chair?std::optional<std::int16_t>{choice.chair->id}:std::nullopt;
            failed.source_choices=std::move(last_attempted_choice);
        }
        job_=std::move(failed);
        return {PrimitiveDisposition::wait,reason};
    };
    RoutePath path{RouteStatus::invalid_input,{},0,0};
    std::optional<PreRankedSlotGoal> selected;
    std::optional<ReferenceSlotPointCandidate> point_candidate;
    std::optional<SourceSlotPointCandidate> source_point_candidate;
    std::vector<PreRankedSlotGoal> fallback_goals;
    std::optional<SourceSlotChoicesSnapshot> general_choices;
    std::optional<std::size_t> general_selected_index;
    std::optional<SourceSparseRouteSnapshot> sparse_snapshot;
    std::unique_ptr<WalkMotion> sparse_motion;
    bool sparse_source_selected=false;
    std::string invalid_route_input_diagnostic;
    std::size_t route_goal_count=0;
    const auto install_sparse_plan=[&](RoutePoint goal,std::optional<float> outgoing,bool face_anywhere,
        const SourceWalkPlanResult& plan,const WalkMotionProfile& profile,
        std::vector<LiveAttemptWalkIdentity> considered)->bool {
        if(plan.state!=SourceWalkPlanState::ready&&plan.state!=SourceWalkPlanState::turn_only)return false;
        RouteFrameContext begin_frame{};auto* parent_frame=nested_request&&job_?&job_->frame:nullptr;
        const auto* effective=parent_frame?parent_frame:&begin_frame;
        if(plan.state==SourceWalkPlanState::ready&&effective->state==RouteFrameState::room_portal&&
           effective->portal_turns==std::numeric_limits<std::uint32_t>::max())return false;
        const bool hard_start=plan.state==SourceWalkPlanState::turn_only||
            effective->state!=RouteFrameState::room_portal||effective->portal_turns==0;
        SourceSparseRouteSnapshot sparse;sparse.room=plan.room;sparse.profile=profile;
        sparse.avatars_to_consider=std::move(considered);sparse.face_anywhere=face_anywhere;
        sparse.target_radian_direction=outgoing.value_or(0.0f);sparse.hard_start=hard_start;
        if(plan.state==SourceWalkPlanState::ready){
            auto motion=std::make_unique<WalkMotion>();
            const WalkMotionPose pose{{actor->position->x,actor->position->y},actor->position->level,actor->radian_direction};
            if(motion->initialize(plan.segments,pose,profile,false,hard_start?0:8)!=WalkMotionStatus::success)return false;
            const auto captured=motion->snapshot();
            if(captured.status!=WalkMotionStatus::success||!captured.snapshot)return false;
            sparse.motion=*captured.snapshot;
            const auto initial_heading=static_cast<float>(sparse.motion.target_direction);
            const auto selection=source_turn_selection(actor->radian_direction,initial_heading);
            if(hard_start&&selection.index){
                SourceLookTowardsRouteSnapshot turn;turn.start_radian_direction=actor->radian_direction;
                turn.target_radian_direction=initial_heading;turn.walk_style=profile.walk_style;turn.in_pool=profile.in_pool;
                turn.animation_index=selection.index;turn.turn_tweak=selection.residual;sparse.turn=turn;sparse.lifecycle=1;
            }
            if(consume_route_portal_turn(begin_frame,parent_frame)!=hard_start)return false;
            sparse_motion=std::move(motion);
        }
        path.status=RouteStatus::found;path.points={{start.x,start.y}};
        if(!(start==goal))path.points.push_back(goal);
        sparse_snapshot=std::move(sparse);sparse_source_selected=true;route_goal_count=1;
        return true;
    };
    if(goto_relative&&!ranged_goto_relative&&!general_source_slot) {
        if(!goal_validator_&&!sparse_source_requested)
            return unsupported("GotoRelativePosition outside the synthetic route profiles: exact-point goal requires a host bounds/room/clearance validator");
        const RoutePoint goal{target->position->x,target->position->y};
        route_goal_count=1;
        if(sparse_source_requested){
            if(!source_sparse_services_.place||!source_sparse_services_.begin_locomotion||
               !source_sparse_services_.end_locomotion||!source_sparse_services_.publish_motion)
                return unsupported("source sparse routing requires placement, locomotion and feedback services");
            const bool face_anywhere=i.operands[3]==255;
            const auto arrival=face_anywhere?actor->position->direction:
                i.operands[3]==254?std::uint8_t{4}:
                static_cast<std::uint8_t>((target->position->direction+i.operands[3])&7U);
            const auto radians=static_cast<float>(static_cast<double>(arrival)*(source_pi/4.0));
            remember_attempt({goal,0,arrival,radians,face_anywhere,{},actor->position->level});
            if(!check_attempt_walk(goal)){
                if(execution_callback_error)return unsupported(*execution_callback_error);
                return fail_route(7,"GotoRelativePosition target rejected by source AttemptWalk");
            }
            WalkMotionProfile profile{};std::vector<LiveAttemptWalkIdentity> considered;
            auto plan=source_sparse_plan(frame,i,dispatch,actor->id,actor->generation,target->id,target->generation,
                *actor->position,*target->position,goal,0,face_anywhere?std::nullopt:std::optional<float>{radians},
                captured_sparse_consideration,profile,considered);
            if(!plan)return unsupported(diagnostic_.empty()?"source GotoRelative sparse plan is unknown":diagnostic_);
            if(plan->state==SourceWalkPlanState::no_path)return fail_route(3,"GotoRelativePosition has no source route");
            if(plan->state==SourceWalkPlanState::budget_exhausted)
                return unsupported("source GotoRelative planner exhausted a bounded work limit: "+plan->diagnostic);
            if(!install_sparse_plan(goal,face_anywhere?std::nullopt:std::optional<float>{radians},face_anywhere,
                *plan,profile,std::move(considered)))
                return unsupported("source GotoRelative sparse plan could not be installed");
        }else{
            if(!allow_goal(goal)) {
                if(execution_callback_error)return unsupported(*execution_callback_error);
                return fail_route(7,"GotoRelativePosition target rejected by host room/clearance validator");
            }
            if(!grid().contains(goal))
                return fail_route(7,"GotoRelativePosition target lies outside the active route grid");
            if(start==goal) {
                if(!grid().walkable(start)) path={RouteStatus::no_path,{},0,0};
                else path={RouteStatus::found,{start},0,0};
            } else path=find_route(grid(),start,{goal},limits_);
        }
    } else if(general_source_slot||(!point_offset_slot&&!source_point_slot&&(((ranged_slot||bed_circle_slot||source_circle_slot)&&(ranged_slot_goals_||continuous_ranged_slot_goals_||authenticated_source_slot))||
                                   (!ranged_slot&&!bed_circle_slot&&!source_circle_slot&&ranked_goals_)))) {
        const auto actor_id=actor->id,target_id=target->id;
        const auto actor_generation=actor->generation,target_generation=target->generation;
        const auto actor_position=*actor->position,target_position=*target->position;
        const auto target_attachment=target->attachment;
        const auto actor_radian_direction=actor->radian_direction;
        const auto source_run_generation=vm_->run_generation();
        const auto target_radian_direction=source_slot_object_radians(*target);
        const auto relative_angle=(general_source_slot||ranged_goto_relative||source_circle_slot||source_range_slot)?std::optional<float>{target_radian_direction}:std::nullopt;
        if(relative_angle&&!continuous_ranged_slot_goals_&&!authenticated_source_slot&&target->kind==EntityKind::avatar&&
           target_radian_direction!=static_cast<float>(target_position.direction)*0.78539816339744830962f)
            return unsupported("relative target angle requires a continuous-angle scoring provider");
        if(goal_selection_observer_){
            goal_receipt.emplace();auto& receipt=*goal_receipt;
            receipt.actor=actor_id;receipt.target=target_id;receipt.actor_generation=actor_generation;
            receipt.target_generation=target_generation;receipt.ticket=dispatch.ticket;receipt.run_generation=source_run_generation;
            receipt.owner=frame.context.code_owner;receipt.routine=frame.key;receipt.pc=frame.pc;
            receipt.instruction=i;receipt.actor_position=actor_position;receipt.target_position=target_position;
            receipt.slot_index=slot_index;receipt.authenticated=authenticated_source_slot;
            receipt.circle=source_circle_slot||bed_circle_slot||supported_circle_slot_profile(*slot);receipt.range=ranged_slot;
            receipt.relative=ranged_goto_relative;receipt.sparse=sparse_source_requested;
            receipt.backend=authenticated_source_slot?"authenticated_placement":
                (continuous_ranged_slot_goals_&&(ranged_slot||bed_circle_slot||source_circle_slot))?
                    "ordinary_continuous":(ranged_slot||bed_circle_slot||source_circle_slot)?"ordinary_range":"ordinary_ranked";
            receipt.reachability_backend=sparse_source_requested?"sparse_per_goal":"ranked_grid_batch";
        }
        std::optional<AuthenticatedRangeSlotGoals> source_range_result;
        const auto candidates=[&] {
          try {
            return [&] {
            struct Guard {bool& flag;bool previous;explicit Guard(bool& value):flag(value),previous(value){flag=true;}~Guard(){flag=previous;}} guard(executing_);
            if(authenticated_source_slot){
                if(!active_range_lease_.expired()||!placement_source_matches(frame,i,dispatch,true))
                    throw std::logic_error("authenticated SLOT planning lacks an exact live or private replay source");
                auto lease=std::make_shared<RoutePlacementProof::State>();
                lease->vm=vm_;lease->frame=frame;lease->instruction=i;lease->dispatch=dispatch;
                const auto run=vm_->run_generation();const auto attachment=actor->attachment;
                std::weak_ptr<RoutePlacementProof::State> weak=lease;active_range_lease_=lease;
                lease->validate=[this,weak,frame,i,dispatch,run,actor_id,target_id,actor_generation,target_generation,
                    actor_position,target_position,target_radian_direction,attachment,target_attachment](){
                    const auto live=weak.lock(),owned=active_range_lease_.lock();
                    if(!live||!live->active||owned!=live||!executing_||vm_->run_generation()!=run||
                       !placement_source_matches(frame,i,dispatch,true))return false;
                    const auto* a=world_.find(actor_id);const auto* t=world_.resolve_reference({target_id,target_generation});
                    if(!a||!t||a->generation!=actor_generation||t->generation!=target_generation||!a->position||!t->position||
                       a->position->x!=actor_position.x||a->position->y!=actor_position.y||a->position->level!=actor_position.level||
                       bool(a->attachment)!=bool(attachment)||!placement_attachment_equal(t->attachment,target_attachment))return false;
                    if(attachment&&(a->attachment->container!=attachment->container||a->attachment->slot!=attachment->slot||
                        a->attachment->positioned!=attachment->positioned))return false;
                    return actor_id==target_id||(*t->position==target_position&&source_slot_object_radians(*t)==target_radian_direction);
                };
                struct LeaseEnd {std::shared_ptr<RoutePlacementProof::State> lease;~LeaseEnd(){lease->active=false;}} end{lease};
                RoutePlacementContext context{vm_,frame,i,dispatch,RoutePlacementProof(lease)};
                source_range_result=authenticated_range_goals_(*selected_slots,slot_index,actor_position,target_position,target_radian_direction,context);
                return source_range_result->goals;
            }
            if((ranged_slot||bed_circle_slot||source_circle_slot)&&continuous_ranged_slot_goals_)
                return continuous_ranged_slot_goals_(*selected_slots,slot_index,actor_position,target_position,relative_angle);
            const auto& provider=(ranged_slot||bed_circle_slot||source_circle_slot)?ranged_slot_goals_:ranked_goals_;
            return provider(*selected_slots,slot_index,actor_position,target_position);
            }();
          }catch(const std::exception& error){report_goal_selection("provider_exception",error.what());throw;}
          catch(...){report_goal_selection("provider_exception","non-standard provider exception");throw;}
        }();
        if(goal_receipt){goal_receipt->provider_completed=true;goal_receipt->provider_goals=candidates.size();}
        actor=world_.find(actor_id);target=world_.resolve_reference({target_id,target_generation});
        if(!actor || !target || actor->generation!=actor_generation || target->generation!=target_generation ||
           !actor->position || !target->position || !(*actor->position==actor_position) ||
           !(*target->position==target_position) ||
           actor->radian_direction!=actor_radian_direction || !placement_attachment_equal(actor->attachment,initial_attachment) || actor->person_data.size()<=37||
           (relative_angle&&source_slot_object_radians(*target)!=target_radian_direction)||
           (authenticated_source_slot&&!placement_attachment_equal(target->attachment,target_attachment)))
            return unsupported("entity changed during pre-ranked SLOT goal provider");
        if(authenticated_source_slot){
            if(vm_->run_generation()!=source_run_generation||!placement_source_matches(frame,i,dispatch,true))
                return unsupported("authenticated SLOT source VM changed during authenticated placement");
            if(!source_range_result)return unsupported("authenticated SLOT provider omitted its typed result");
            const auto& result=*source_range_result;
            if(candidates.empty()){
                if(result.failure_code!=7&&result.failure_code!=8&&!(general_source_slot&&result.failure_code==10))return unsupported("source SLOT failure code is outside parser profile");
                if(result.failure_code==7&&(result.blocker||result.blocker_generation))return unsupported("source SLOT no-goal failure carries an unexpected blocker");
                if(result.failure_code==8){const auto* blocker=world_.find(result.blocker);
                    if(!blocker||blocker->kind!=EntityKind::object||!result.blocker_generation||blocker->generation!=result.blocker_generation)
                        return unsupported("source SLOT blocked-object identity is stale");}
                return fail_route(result.failure_code,"source SLOT has no verified goals",result.blocker);
            }
            if(result.failure_code||result.blocker||result.blocker_generation)return unsupported("successful source SLOT carries failure metadata");
        }
        // The provider owns ranking and destination validation, but cannot
        // bypass this controller's supported direction mask/facing profile.
        if(!general_source_slot)for(const auto& candidate:candidates)
            if((candidate.entry_flags&static_cast<std::uint8_t>(~slot->flags))!=0 ||
               candidate.entry_flags!=reference_slot_entry_flags(
                   {target_position.x,target_position.y},target_position.direction,
                   candidate.position,static_cast<std::uint8_t>(slot->flags),
                   (source_circle_slot||source_range_slot)&&(slot->flags&512U)?std::optional<float>{0}:relative_angle))
                return unsupported("pre-ranked SLOT goal has unsupported entry flags");
        if(!general_source_slot&&sparse_source_requested&&std::any_of(candidates.begin(),candidates.end(),[](const auto& candidate){
            return candidate.entry_flags==0||candidate.facing>=8||candidate.sitting_object||candidate.sitting_approach;
        }))return unsupported("source sparse standing route received a sitting or invalid ranked goal");
        if(general_source_slot){
            if(!source_range_result||source_range_result->semantic_goals.size()!=candidates.size()||candidates.size()>4096)
                return unsupported("full SLOT provider omitted exact semantic goals or exceeded budget");
            for(std::size_t n=0;n<candidates.size();++n){const auto& goal=candidates[n];const auto& semantic=source_range_result->semantic_goals[n];
                if(!(goal.position==semantic.position)||goal.entry_flags!=semantic.entry_flags||goal.facing!=semantic.facing||
                   goal.sitting_approach||!goal.radian_direction||!same_float_bits(*goal.radian_direction,semantic.radian_direction)||
                   goal.sitting_object!=(semantic.chair?std::optional<std::int16_t>{semantic.chair->id}:std::nullopt)||
                   semantic.facing>7||semantic.level<1||semantic.level>127||!std::isfinite(semantic.radian_direction))return unsupported("full SLOT geometry/semantic results disagree");
                if(semantic.chair){const auto* chair=world_.find(semantic.chair->id);
                    if(slot->sitting<=0||!chair||chair->kind!=EntityKind::object||chair->generation!=semantic.chair->generation||
                       !chair->entry_points||chair->entry_points->size()<=26||!chair->entry_points->at(26).action)
                        return unsupported("full SLOT selected an unproven chair");}
            }
        }
        if(source_circle_slot||source_range_slot){
            const auto geometry=source_range_slot?enumerate_source_slot_range_candidates(*selected_slots,slot_index,target_position,target_radian_direction):
                enumerate_source_slot_circle_candidates(*selected_slots,slot_index,target_position,target_radian_direction);
            std::vector<RoutePoint> seen;
            if(candidates.size()>geometry.size())return unsupported("source circle provider exceeded candidate cardinality");
            for(const auto& candidate:candidates){
                if(candidate.sitting_object||candidate.sitting_approach||!candidate.radian_direction||
                   std::find(seen.begin(),seen.end(),candidate.position)!=seen.end())
                    return unsupported("source circle provider returned unsupported goal identity");
                const auto exact=std::find_if(geometry.begin(),geometry.end(),[&](const auto& value){
                    return value.position==candidate.position&&value.entry_flags==candidate.entry_flags&&value.facing==candidate.facing&&
                        same_float_bits(value.radian_direction,*candidate.radian_direction);
                });
                if(exact==geometry.end())return unsupported("source circle provider altered source geometry or continuous facing");
                seen.push_back(candidate.position);
            }
        }
        std::vector<PreRankedSlotGoal> accepted;
        accepted.reserve(candidates.size());
        for(const auto& candidate:candidates){
            const bool sparse_enabled=sparse_source_requested;
            if(general_source_slot)accepted.push_back(candidate);
            else if(sparse_enabled){
                if(check_attempt_walk(candidate.position))accepted.push_back(candidate);
            }else if(allow_goal(candidate.sitting_approach.value_or(candidate.position)))accepted.push_back(candidate);
            if(authenticated_source_slot){
                actor=world_.find(actor_id);target=world_.resolve_reference({target_id,target_generation});
                if(!actor||!target||actor->generation!=actor_generation||target->generation!=target_generation||
                   !actor->position||!target->position||!(*actor->position==actor_position)||!(*target->position==target_position)||
                   actor->radian_direction!=actor_radian_direction||!placement_attachment_equal(actor->attachment,initial_attachment)||actor->person_data.size()<=37||
                   source_slot_object_radians(*target)!=target_radian_direction||!placement_attachment_equal(target->attachment,target_attachment)||
                   vm_->run_generation()!=source_run_generation||
                   !placement_source_matches(frame,i,dispatch,true))
                    return unsupported("authenticated SLOT participants or VM changed during goal validation");
            }
        }
        if(goal_receipt)goal_receipt->accepted_goals=accepted.size();
        if(execution_callback_error)return unsupported(*execution_callback_error);
        route_goal_count=accepted.size();
        if(sparse_source_requested){
            if(!source_sparse_services_.place||!source_sparse_services_.begin_locomotion||
               !source_sparse_services_.end_locomotion||!source_sparse_services_.publish_motion)
                return unsupported("source sparse routing requires placement, locomotion and feedback services");
            if(accepted.empty())return fail_route(candidates.empty()?7:3,
                "source sparse SLOT has no candidate after source checks");
            std::optional<std::size_t> chosen;
            SourceWalkPlanResult chosen_plan;
            WalkMotionProfile chosen_profile{};
            std::vector<LiveAttemptWalkIdentity> avatars_to_consider;
            std::optional<std::vector<LiveAttemptWalkIdentity>> frozen_considered;
            frozen_considered=captured_sparse_consideration;
            for(std::size_t n=0;n<accepted.size();++n){
                if(general_source_slot&&source_range_result->semantic_goals[n].level!=actor_position.level)
                    return unsupported("source SLOT goal changes floor without an admitted inter-floor route");
                if(general_source_slot&&(accepted[n].sitting_object||source_stand_before_walk)){chosen=n;break;}
                if(general_source_slot)remember_attempt(source_range_result->semantic_goals[n]);
                else if(accepted[n].radian_direction)remember_attempt({accepted[n].position,accepted[n].entry_flags,
                    accepted[n].facing,*accepted[n].radian_direction,slot->facing==-3,{},actor_position.level});
                if(general_source_slot&&!check_attempt_walk(accepted[n].position)){
                    if(execution_callback_error)return unsupported(*execution_callback_error);
                    continue;}
                WalkMotionProfile profile{};
                std::vector<LiveAttemptWalkIdentity> considered;
                const bool any=general_source_slot?source_range_result->semantic_goals[n].face_anywhere:slot->facing==-3;
                const auto angle=any?std::optional<float>{}:accepted[n].radian_direction;
                if(goal_receipt)++goal_receipt->reachable_attempts;
                const auto plan=source_sparse_plan(frame,i,dispatch,actor_id,actor_generation,target_id,
                    target_generation,actor_position,target_position,accepted[n].position,n,angle,
                    frozen_considered,profile,considered);
                if(!plan)return unsupported(diagnostic_.empty()?"source sparse planner has unknown state":diagnostic_);
                if(!frozen_considered)frozen_considered=considered;
                else if(frozen_considered->size()!=considered.size()||!std::equal(frozen_considered->begin(),frozen_considered->end(),
                    considered.begin(),[](const auto& a,const auto& b){return a.id==b.id&&a.generation==b.generation;}))
                    return unsupported("source avatars-to-consider set changed between ranked candidate plans");
                if(plan->state==SourceWalkPlanState::no_path)continue;
                if(plan->state==SourceWalkPlanState::budget_exhausted)
                    return unsupported("source sparse route planning exhausted a bounded work limit: "+plan->diagnostic);
                if(plan->state!=SourceWalkPlanState::ready&&plan->state!=SourceWalkPlanState::turn_only)
                    return unsupported("source sparse route planning is unsupported: "+plan->diagnostic);
                chosen=n;chosen_plan=*plan;chosen_profile=profile;avatars_to_consider=std::move(considered);break;
            }
            if(!chosen)return fail_route(3,"source sparse AttemptWalk found no reachable source goal");
            selected=accepted[*chosen];
            if(general_source_slot)general_selected_index=*chosen;
            fallback_goals.assign(accepted.begin()+static_cast<std::ptrdiff_t>(*chosen+1),accepted.end());
            const RoutePoint goal=selected->position;
            if(general_source_slot&&(selected->sitting_object||source_stand_before_walk)){
                path.status=RouteStatus::found;path.points={start};
            }else{
            path.status=RouteStatus::found;path.points={start};
            if(!(start==goal))path.points.push_back(goal);
            std::optional<float> outgoing;
            const bool selected_any=general_source_slot?source_range_result->semantic_goals[*chosen].face_anywhere:slot->facing==-3;
            if(!selected_any)outgoing=selected->radian_direction.value_or(
                static_cast<float>(selected->facing)*(static_cast<float>(source_pi/4.0)));
            if(!install_sparse_plan(selected->position,outgoing,selected_any,chosen_plan,chosen_profile,
                std::move(avatars_to_consider)))
                return unsupported("source sparse route plan could not be installed");
            }
        }
        if(!sparse_source_selected&&!general_source_slot&& !grid().walkable(start))
            invalid_route_input_diagnostic="pre-ranked SLOT route start is outside the walkable grid";
        else if(!sparse_source_selected&&!general_source_slot&&candidates.empty())
            invalid_route_input_diagnostic="pre-ranked SLOT goal provider returned no candidates (actor="+
                std::to_string(actor_position.x)+","+std::to_string(actor_position.y)+
                "; target="+std::to_string(target_position.x)+","+
                std::to_string(target_position.y)+", direction="+
                std::to_string(target_position.direction)+", SLOT flags="+
                std::to_string(slot->flags)+")";
        else if(!sparse_source_selected&&!general_source_slot&&accepted.empty())
            invalid_route_input_diagnostic="host rejected every pre-ranked SLOT goal as outside the walkable grid";
        else if(!sparse_source_selected&&!general_source_slot)for(const auto& goal:accepted)if(!grid().contains(goal.position)||
            (goal.sitting_approach&&!grid().contains(*goal.sitting_approach))||
            (!general_source_slot&&goal.entry_flags==0)||goal.facing>=8) {
            invalid_route_input_diagnostic="pre-ranked SLOT goal is outside the grid or has invalid facing flags";
            break;
        }
        // A valid relative search may have no eligible destination. Source
        // routing follows its failure branch (and optional failure tree),
        // rather than treating an empty candidate list as malformed metadata.
        if(!sparse_source_selected&&!(general_source_slot&&selected&&(selected->sitting_object||source_stand_before_walk))){
            const bool known_empty=(ranged_goto_relative||source_circle_slot||source_range_slot)&&grid().walkable(start)&&accepted.empty();
            if(goal_receipt&&!known_empty)++goal_receipt->reachable_attempts;
            auto result=known_empty?
                PreRankedSlotRoute{{RouteStatus::no_path,{},0,0},{}}:
                find_pre_ranked_slot_route(grid(),start,accepted,limits_);
            if(result.selected) {
                selected=accepted[*result.selected];
                fallback_goals.reserve(accepted.size()-1);
                for(std::size_t n=0;n<accepted.size();++n)
                    if((source_circle_slot||source_range_slot)?n>*result.selected:n!=*result.selected)fallback_goals.push_back(accepted[n]);
            }
            path=std::move(result.path);
        }
    if(general_source_slot&&selected){
        SourceSlotChoicesSnapshot choices;choices.slot_version=selected_slots->version;choices.slot_index=slot_index;choices.slot=*slot;
        choices.target_radian_direction=source_slot_object_radians(*target);
        if(!source_range_result||!general_selected_index||*general_selected_index>=source_range_result->semantic_goals.size())
            return unsupported("selected source choice was not retained");
        const auto found=source_range_result->semantic_goals.begin()+static_cast<std::ptrdiff_t>(*general_selected_index);
        choices.selected=*found;choices.remaining.assign(found+1,source_range_result->semantic_goals.end());
        general_choices=std::move(choices);
    }
    } else if(source_point_slot) {
        try {source_point_candidate=enumerate_source_slot_point_candidate(*selected_slots,slot_index,
            *target->position,source_slot_object_radians(*target));}
        catch(const SourceCheckResetRetirement&){throw;}
    catch(const std::exception& error){return unsupported(error.what());}
        const RoutePoint goal{source_point_candidate->position.x,source_point_candidate->position.y};
        route_goal_count=1;
        if(sparse_source_requested){
            if(!source_sparse_services_.place||!source_sparse_services_.begin_locomotion||
               !source_sparse_services_.end_locomotion||!source_sparse_services_.publish_motion)
                return unsupported("source sparse routing requires placement, locomotion and feedback services");
            remember_attempt({goal,0,source_point_candidate->facing,source_point_candidate->radian_direction,
                source_point_candidate->face_anywhere,{},source_point_candidate->position.level});
            if(!check_attempt_walk(goal)){
                if(execution_callback_error)return unsupported(*execution_callback_error);
                return fail_route(7,"source exact SLOT point rejected by source AttemptWalk");
            }
            WalkMotionProfile profile{};std::vector<LiveAttemptWalkIdentity> considered;
            const auto outgoing=source_point_candidate->face_anywhere?std::nullopt:
                std::optional<float>{source_point_candidate->radian_direction};
            const auto plan=source_sparse_plan(frame,i,dispatch,actor->id,actor->generation,target->id,target->generation,
                *actor->position,*target->position,goal,0,outgoing,captured_sparse_consideration,profile,considered);
            if(!plan)return unsupported(diagnostic_.empty()?"source point SLOT sparse plan is unknown":diagnostic_);
            if(plan->state==SourceWalkPlanState::no_path)return fail_route(3,"source point SLOT has no AttemptWalk path");
            if(plan->state==SourceWalkPlanState::budget_exhausted)
                return unsupported("source point SLOT planner exhausted a bounded work limit: "+plan->diagnostic);
            if(!install_sparse_plan(goal,outgoing,source_point_candidate->face_anywhere,*plan,profile,std::move(considered)))
                return unsupported("source point SLOT sparse plan could not be installed (state="+std::to_string(static_cast<int>(plan->state))+", diagnostic="+plan->diagnostic+")");
        }else{
            if(!allow_goal(goal)) {
                if(execution_callback_error)return unsupported(*execution_callback_error);
                return fail_route(7,"source exact SLOT point rejected by host bounds/wall/placement validator");
            }
            if(!grid().contains(goal))return fail_route(7,"source exact SLOT point lies outside the active route grid");
            if(start==goal)path=grid().walkable(start)?RoutePath{RouteStatus::found,{start},0,0}:RoutePath{RouteStatus::no_path,{},0,0};
            else path=find_route(grid(),start,{goal},limits_);
        }
    } else if(point_offset_slot) {
        point_candidate=enumerate_reference_slot_point_candidate(*selected_slots,slot_index,
            {target->position->x,target->position->y},target->position->direction);
        const auto goal=point_candidate->position;
        if(!allow_goal(goal)) {
            if(execution_callback_error)return unsupported(*execution_callback_error);
            return {PrimitiveDisposition::goto_false,"point-offset SLOT rejected by host room/clearance validator"};
        }
        if(!grid().contains(goal))
            return {PrimitiveDisposition::goto_false,"point-offset SLOT lies outside the active route grid"};
        route_goal_count=1;
        if(start==goal) {
            if(!grid().walkable(start))path={RouteStatus::no_path,{},0,0};
            else path={RouteStatus::found,{start},0,0};
        } else path=find_route(grid(),start,{goal},limits_);
    } else {
        std::vector<RoutePoint> goals;
        bool direction_compatible_candidate=false;
        for(const auto& candidate:enumerate_reference_slot_circle_candidates(*selected_slots,slot_index,
                {target->position->x,target->position->y},target->position->direction)) {
            if(candidate.entry_flags==0)continue;
            direction_compatible_candidate=true;
            if(allow_goal(candidate.position))goals.push_back(candidate.position);
        }
        if(execution_callback_error)return unsupported(*execution_callback_error);
        route_goal_count=goals.size();
        if(goals.empty()) {
            if(direction_compatible_candidate) {
                diagnostic_="host goal validator rejected every direction-compatible SLOT candidate";
            } else {
                diagnostic_="SLOT direction mask rejects every candidate (type3 index "+
                    std::to_string(slot_index)+", flags "+std::to_string(slot->flags)+")";
            }
            diagnostic_+=" (target "+std::to_string(target->id)+" at "+
                std::to_string(target->position->x)+","+std::to_string(target->position->y)+","+
                std::to_string(target->position->level)+" direction "+
                std::to_string(target->position->direction)+")";
            return {PrimitiveDisposition::goto_false,{}};
        }
        if(std::find(goals.begin(),goals.end(),start)!=goals.end()) {
            // Validate clearance even for the turn-only shortcut.
            if(!grid().contains(start)) path={RouteStatus::invalid_input,{},0,0};
            else if(!grid().walkable(start)) path={RouteStatus::no_path,{},0,0};
            else path={RouteStatus::found,{start},0,0};
        }else path=find_route(grid(),start,goals,limits_);
    }
    if(path.status==RouteStatus::budget_exhausted) return unsupported("routing search budget exhausted");
    if(path.status==RouteStatus::invalid_input) {
        if(invalid_route_input_diagnostic.empty())invalid_route_input_diagnostic=
            "routing inputs invalid: start "+std::to_string(start.x)+","+std::to_string(start.y)+
            " walkable="+std::string(grid().walkable(start)?"yes":"no")+
            ", accepted goals="+std::to_string(route_goal_count);
        return unsupported(invalid_route_input_diagnostic);
    }
    if(path.status==RouteStatus::no_path) return fail_route(3,"no path in supplied clearance grid");
    const auto goal=path.points.back();
    const std::int16_t entry_flags=general_source_slot?static_cast<std::int16_t>(selected->entry_flags):((goto_relative&&!ranged_goto_relative)||source_point_slot)?0:static_cast<std::int16_t>(selected?selected->entry_flags:reference_slot_entry_flags(
        {target->position->x,target->position->y},target->position->direction,goal,
        static_cast<std::uint8_t>(slot->flags)));
    if(!general_source_slot&&(!goto_relative||ranged_goto_relative) && !point_offset_slot && !source_point_slot && entry_flags==0) {diagnostic_="selected route goal no longer matches SLOT direction mask";
        report_goal_selection("failed",diagnostic_);
        return {PrimitiveDisposition::goto_false,{}};}
    // The TS1 direction operand is a relative facing offset from the target:
    // 0 keeps its direction and 4 faces the opposite way. OnTopOf Facing
    // computes GetDirectionTo(p,p): Atan2(+0,-0) = pi in the pinned parser.
    const auto arrival_facing=general_source_slot?selected->facing:(goto_relative&&!ranged_goto_relative)?
        (i.operands[3]==255?actor->position->direction:
         i.operands[3]==254?std::uint8_t{4}:
         static_cast<std::uint8_t>((target->position->direction+i.operands[3])&7U)):
        (source_point_candidate?source_point_candidate->facing:(point_candidate?point_candidate->facing:
         (selected?selected->facing:heading(goal,{target->position->x,target->position->y}))));
    RouteFrameContext frame_context;
    if(sparse_snapshot){
        if(sparse_snapshot->lifecycle==1)frame_context.state=RouteFrameState::begin_turn;
        else if(!sparse_snapshot->motion.current)frame_context.state=RouteFrameState::turn_only;
        else frame_context.state=RouteFrameState::walking;
    }else if(path.points.size()==1) frame_context.state=RouteFrameState::turn_only;
    if(ordinary_go_here_relative){const auto* live_target=world_.frame_entity(frame,VmEntityRole::stack_object);
        if(!live_target||live_target->ui_ghost)return unsupported("ordinary GoHere target changed its source membership during planning");}
    // StartWalkAnimation removes the shared avatar obstacle; only the root
    // PreExit restores it. A child can be admitted before its own first walk
    // tick while its exact source ancestor already owns that obstacle class.
    bool inherited_source_footprint=false;
    if(nested_request&&!sparse_snapshot&&!actor->static_footprint&&job_->walking_started&&
       (job_->source_sparse||job_->source_choices)&&job_->actor==actor->id&&
       job_->actor_generation==actor->generation&&job_->run_generation==vm_->run_generation()&&
       placement_source_matches(frame,i,dispatch,true)){
        std::size_t matches=0;
        const auto owns=[&](const VmPending& wait){return wait.ticket==job_->ticket&&
            wait.context.caller==job_->actor&&(wait.instruction.instruction.opcode==45||wait.instruction.instruction.opcode==27);};
        for(const auto& wait:vm_->suspended_pending())if(owns(wait))++matches;
        for(const auto& continuation:vm_->immediate_continuations())
            if(continuation.parent_pending&&owns(*continuation.parent_pending))++matches;
        inherited_source_footprint=matches==1;
    }
    if(nested_request) suspended_jobs_.push_back(std::move(*job_));
    job_=Job{dispatch.ticket,actor->generation,target->generation,actor->id,target->id,
             *actor->position,*target->position,actor->radian_direction,frame_context,
             std::move(path.points),1,entry_flags,
             arrival_facing,source_point_candidate?source_point_candidate->face_anywhere:(goto_relative?i.operands[3]==255:slot->facing==-3),
             false};
    job_->selected_sitting_object=selected?selected->sitting_object:std::nullopt;
    job_->carried_target=capture_carried_target(world_,*actor,*target);
    job_->fallback_goals=std::move(fallback_goals);
    job_->run_generation=vm_->run_generation();
    job_->call_failure_trees=source_failure_trees&&!nested_request;
    job_->same_room_verified=same_room_verified;
    // BeginWalk can turn before StartWalkAnimation. A nested route inherits
    // the avatar's current obstacle class; its own locomotion is still pending.
    if(sparse_snapshot||inherited_source_footprint)job_->walking_started=!actor->static_footprint;
    job_->source_sparse=std::move(sparse_snapshot);
    job_->source_motion=std::move(sparse_motion);
    job_->sparse_consideration=std::move(captured_sparse_consideration);
    if(source_point_candidate)job_->arrival_radian_direction=source_point_candidate->radian_direction;
    if(source_circle_slot&&selected){
        job_->arrival_radian_direction=selected->radian_direction;
        SourceSlotCircleRouteSnapshot circle;
        circle.slot_version=selected_slots->version;circle.slot_index=slot_index;circle.slot=*slot;
        circle.target_radian_direction=source_slot_object_radians(*target);
        circle.selected={selected->position,selected->entry_flags,selected->facing,*selected->radian_direction};
        for(const auto& goal:job_->fallback_goals)
            circle.fallback_goals.push_back({goal.position,goal.entry_flags,goal.facing,*goal.radian_direction});
        job_->source_circle=std::move(circle);
    }
    if(source_range_slot&&selected){
        job_->arrival_radian_direction=selected->radian_direction;
        SourceSlotRangeRouteSnapshot range;
        range.slot_version=selected_slots->version;range.slot_index=slot_index;range.slot=*slot;
        range.target_radian_direction=source_slot_object_radians(*target);
        range.selected={selected->position,selected->entry_flags,selected->facing,*selected->radian_direction};
        for(const auto& goal:job_->fallback_goals)range.fallback_goals.push_back({goal.position,goal.entry_flags,goal.facing,*goal.radian_direction});
        job_->source_range=std::move(range);
    }
    if(!general_source_slot&&source_generic_sparse_slot&&sparse_source_selected&&job_->source_sparse&&selected){
        const auto selected_angle=selected->radian_direction.value_or(
            static_cast<float>(selected->facing)*static_cast<float>(source_pi/4.0));
        if(!std::isfinite(selected_angle))return unsupported("generic ranked SLOT has no finite selected direction");
        SourceSlotRangeRouteSnapshot choices;
        choices.slot_version=selected_slots->version;choices.slot_index=slot_index;choices.slot=*slot;
        choices.target_radian_direction=source_slot_object_radians(*target);
        choices.selected={selected->position,selected->entry_flags,selected->facing,selected_angle};
        for(const auto& goal:job_->fallback_goals){
            const auto angle=goal.radian_direction.value_or(static_cast<float>(goal.facing)*static_cast<float>(source_pi/4.0));
            if(!std::isfinite(angle))return unsupported("generic ranked SLOT fallback has no finite direction");
            choices.fallback_goals.push_back({goal.position,goal.entry_flags,goal.facing,angle});
        }
        job_->arrival_radian_direction=selected_angle;
        job_->source_sparse->choices=std::move(choices);
    }
    if(general_choices){
        job_->arrival_radian_direction=general_choices->selected.radian_direction;job_->face_anywhere=general_choices->selected.face_anywhere;
        job_->source_choices=std::move(general_choices);
        if(selected&&(selected->sitting_object||source_stand_before_walk)){job_->frame.state=RouteFrameState::initial;job_->same_room_verified=true;}
    }
    if(((!goto_relative||general_source_slot||(ranged_goto_relative&&job_->source_range))&&actor->id==target->id)||source_point_slot)
        job_->target_anchor_radian_direction=source_slot_object_radians(*target);
    report_goal_selection("selected",{});
    return {PrimitiveDisposition::wait,{}};
    }catch(const SourceCheckResetRetirement& retired){
        if(!source_reset_retired_||retired.source()!=vm_)throw;
        if(source_reset_operation_depth_>1)throw;
        return {PrimitiveDisposition::continue_execution,{}};
    }
}
PrimitiveOutcome Routing::request_look_towards(const VmFrame& frame,const Instruction& instruction,
    const VmDispatch& dispatch,bool nested) {
    const auto* actor=world_.frame_entity(frame,VmEntityRole::caller);
    const auto* target=world_.frame_entity(frame,VmEntityRole::stack_object);
    if(!actor||actor->kind!=EntityKind::avatar||!actor->position||!target||!dispatch.ticket||
       !std::isfinite(actor->radian_direction))
        return unsupported("LookTowards requires a live avatar and an exact StackObject identity");
    if(actor->position->x<INT16_MIN||actor->position->x>INT16_MAX||
       actor->position->y<INT16_MIN||actor->position->y>INT16_MAX||
       actor->position->level<1||actor->position->direction>7)
        return unsupported("LookTowards caller is outside the source LotTilePos profile");
    if(!look_towards_services_.profile||!look_towards_services_.start||
       !look_towards_services_.ended||!look_towards_services_.reset)
        return unsupported("LookTowards requires native shared animation services");
    const auto actor_id=actor->id,target_id=target->id;
    const auto actor_generation=actor->generation,target_generation=target->generation;
    const auto actor_position=*actor->position,target_position=source_look_position(*target);
    const auto actor_angle=actor->radian_direction;
    const auto run=vm_->run_generation();
    if(target_position.x<INT16_MIN||target_position.x>INT16_MAX||
       target_position.y<INT16_MIN||target_position.y>INT16_MAX||
       target_position.level<INT8_MIN||target_position.level>INT8_MAX)
        return unsupported("LookTowards StackObject is outside the source LotTilePos profile");
    auto point=target_position;
    std::optional<SourceLookTowardsProfile> profile;
    try {
        struct Guard {bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(executing_);
        if(instruction.operands[0]>=4){
            if(!look_towards_group_)return unsupported("LookTowards average requires an authoritative complete group resolver");
            const auto group=look_towards_group_(frame);
            if(!group||group->empty()||group->size()>4096)
                return unsupported("LookTowards group is missing or exceeds the bounded World profile");
            std::vector<std::int16_t> seen;seen.reserve(group->size());
            std::int32_t x=0,y=0,level=0;bool contains_target=false;
            for(const auto& member:*group){
                const auto* entity=world_.find(member.id);
                if(!entity||!member.generation||entity->generation!=member.generation||
                   !(source_look_position(*entity)==member.position)||
                   std::find(seen.begin(),seen.end(),member.id)!=seen.end()||
                   member.position.x<INT16_MIN||member.position.x>INT16_MAX||
                   member.position.y<INT16_MIN||member.position.y>INT16_MAX||
                   member.position.level<INT8_MIN||member.position.level>INT8_MAX)
                    return unsupported("LookTowards group contains a stale, duplicate or non-LotTilePos member");
                seen.push_back(member.id);contains_target|=member.id==target_id;
                x=source_short_sum(x,member.position.x);y=source_short_sum(y,member.position.y);
                auto level_bits=(static_cast<std::uint32_t>(level)+static_cast<std::uint32_t>(member.position.level))&255U;
                level=level_bits>=128?static_cast<std::int32_t>(level_bits)-256:static_cast<std::int32_t>(level_bits);
            }
            if(!contains_target)return unsupported("LookTowards complete group omits StackObject");
            const auto count=static_cast<std::int32_t>(group->size());
            point={x/count,y/count,static_cast<std::int16_t>(level/count),0};
        }
        profile=look_towards_services_.profile(frame);
    }catch(const std::exception& error){return unsupported(std::string("LookTowards resource planning failed: ")+error.what());}
    if(!profile)return unsupported("LookTowards current pool or walk-style capability is unknown");
    const auto* current_actor=world_.find(actor_id);const auto* current_target=world_.resolve_reference({target_id,target_generation});
    if(!current_actor||!current_target||current_actor->generation!=actor_generation||
       current_target->generation!=target_generation||!current_actor->position||
       !(*current_actor->position==actor_position)||!same_float_bits(current_actor->radian_direction,actor_angle)||
       !(source_look_position(*current_target)==target_position)||vm_->run_generation()!=run)
        return unsupported("LookTowards identity changed during host planning");
    // GetDirectionTo uses integer deltas (including +0 for the negated Y).
    // In particular, looking at oneself is Atan2(0,0), not Atan2(+0,-0).
    auto angle=static_cast<float>(std::atan2(static_cast<double>(point.x-actor_position.x),
        static_cast<double>(-(point.y-actor_position.y))));
    if((instruction.operands[0]&1U)!=0)angle=static_cast<float>(std::fmod(static_cast<double>(angle)+source_pi,source_pi*2));
    SourceLookTowardsRouteSnapshot turn;
    turn.mode=instruction.operands[0];turn.start_radian_direction=actor_angle;turn.target_radian_direction=angle;
    turn.in_pool=profile->in_pool;turn.walk_style=profile->in_pool?0:profile->walk_style;
    Job candidate{};candidate.ticket=dispatch.ticket;candidate.actor_generation=actor_generation;
    candidate.target_generation=actor_generation;candidate.actor=actor_id;candidate.target=actor_id;
    candidate.expected=actor_position;candidate.target_position=actor_position;candidate.expected_radian_direction=actor_angle;
    candidate.frame.state=RouteFrameState::turn_only;candidate.path={{actor_position.x,actor_position.y}};
    candidate.next=1;candidate.entry_flags=1;candidate.facing=source_direction_notch(angle);
    candidate.run_generation=run;candidate.same_room_verified=true;candidate.look_towards=turn;
    if(nested)suspended_jobs_.push_back(std::move(*job_));
    job_=std::move(candidate);
    return {PrimitiveDisposition::wait,{}};
}
std::optional<bool> Routing::start_source_selected_walk(){
    const auto reject=[&](const char* why)->std::optional<bool>{diagnostic_=why;return {};};
    if(!job_||!job_->source_choices||(job_->source_choices->selected.chair&&!job_->source_choices->chair_attempted)||!vm_||!vm_->pending()||
       vm_->pending()->ticket!=job_->ticket||vm_->status()!=VmStatus::waiting||vm_->stack().empty())
        return reject("source selected WalkTo lost its exact routing pending");
    auto* actor=world_.find(job_->actor);const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!target||!actor->position||!target->position||actor->attachment||
       actor->generation!=job_->actor_generation||target->generation!=job_->target_generation)
        return reject("source selected WalkTo requires the actual standing actor pose");
    const auto frame=vm_->stack().back();const auto instruction=vm_->pending()->instruction.instruction;
    const VmDispatch dispatch{vm_,job_->ticket,false};const auto goal=job_->source_choices->selected;
        WalkMotionProfile profile{};std::vector<LiveAttemptWalkIdentity> considered;
        const auto outgoing=goal.face_anywhere?std::nullopt:std::optional<float>{goal.radian_direction};
        const auto plan=source_sparse_plan(frame,instruction,dispatch,job_->actor,job_->actor_generation,job_->target,job_->target_generation,
            *actor->position,*target->position,goal.position,0,outgoing,job_->sparse_consideration,profile,considered);
        if(!plan){diagnostic_="source alternative AttemptWalk is unresolved: "+diagnostic_;return {};}
        if(plan->state==SourceWalkPlanState::no_path)return false;
        if(plan->state!=SourceWalkPlanState::ready&&plan->state!=SourceWalkPlanState::turn_only)return reject("source alternative AttemptWalk exceeded a supported work bound");
        SourceSparseRouteSnapshot sparse;sparse.room=plan->room;sparse.profile=profile;sparse.avatars_to_consider=std::move(considered);
        if(plan->state==SourceWalkPlanState::turn_only&&job_->source_sparse&&job_->source_sparse->walk_to_purpose==SourceWalkToPurpose::architecture_invalidation){
            sparse.motion=job_->source_sparse->motion;sparse.walk_to_pending=true;sparse.walk_to_purpose=SourceWalkToPurpose::architecture_invalidation;
        }
        sparse.face_anywhere=goal.face_anywhere;sparse.target_radian_direction=outgoing.value_or(0);
        auto* parent=suspended_jobs_.empty()?nullptr:&suspended_jobs_.back().frame;
        const auto& effective=parent?*parent:job_->frame;
        const bool hard_start=plan->state==SourceWalkPlanState::turn_only||effective.state!=RouteFrameState::room_portal||effective.portal_turns==0;
        sparse.hard_start=hard_start;
        if(plan->state==SourceWalkPlanState::ready){
            auto motion=std::make_unique<WalkMotion>();
            if(motion->initialize(plan->segments,{{actor->position->x,actor->position->y},actor->position->level,actor->radian_direction},profile,false,hard_start?0:8)!=WalkMotionStatus::success)
                return reject("source alternative motion cannot initialize");
            const auto captured=motion->snapshot();if(!captured.snapshot)return reject("source alternative motion cannot capture");sparse.motion=*captured.snapshot;
            const auto heading=static_cast<float>(sparse.motion.target_direction);const auto turn=source_turn_selection(actor->radian_direction,heading);
            if(hard_start&&turn.index){SourceLookTowardsRouteSnapshot value;value.start_radian_direction=actor->radian_direction;value.target_radian_direction=heading;
                value.in_pool=profile.in_pool;value.walk_style=profile.walk_style;value.animation_index=turn.index;value.turn_tweak=turn.residual;sparse.turn=value;sparse.lifecycle=1;}
            if(consume_route_portal_turn(job_->frame,parent)!=hard_start)return reject("source selected WalkTo parent portal turn bound is exhausted");
            job_->source_motion=std::move(motion);
        }
        if(!(job_->path.front()==goal.position))job_->path.push_back(goal.position);
        job_->walking_started=!actor->static_footprint;
        job_->source_sparse=std::move(sparse);job_->frame.state=job_->source_sparse->lifecycle==1?RouteFrameState::begin_turn:
            job_->source_motion?RouteFrameState::walking:RouteFrameState::turn_only;
        return true;
}
std::optional<bool> Routing::advance_source_slot_choice(){
    const auto reject=[&](const char* why)->std::optional<bool>{diagnostic_=why;return {};};
    if(!job_||!job_->source_choices||!vm_||!vm_->pending()||vm_->pending()->ticket!=job_->ticket||vm_->stack().empty())
        return reject("source choices lost their pending routing frame");
    auto& choices=*job_->source_choices;
    while(!choices.remaining.empty()){
        const auto goal=choices.remaining.front();choices.remaining.erase(choices.remaining.begin());
        if(job_->fallback_goals.empty())return reject("source choice and fallback lists disagree");
        job_->fallback_goals.erase(job_->fallback_goals.begin());choices.selected=goal;choices.chair_attempted=false;
        auto* actor=world_.find(job_->actor);const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
        if(!actor||!target||!actor->position||!target->position||(actor->attachment&&(actor->person_data.empty()||actor->person_data[0]!=1))||actor->generation!=job_->actor_generation||target->generation!=job_->target_generation)
            return reject("source alternative actor or target identity changed");
        job_->expected=*actor->position;job_->expected_radian_direction=actor->radian_direction;
        job_->entry_flags=goal.entry_flags;job_->facing=goal.facing;job_->face_anywhere=goal.face_anywhere;
        job_->arrival_radian_direction=goal.radian_direction;
        job_->selected_sitting_object=goal.chair?std::optional<std::int16_t>{goal.chair->id}:std::nullopt;
        job_->external_frame_sitting=false;job_->external_frame_call.reset();job_->external_frame_owner=0;
        job_->source_motion.reset();job_->source_sparse.reset();job_->frame.state=RouteFrameState::initial;
        job_->path={{actor->position->x,actor->position->y}};job_->next=1;
        if(goal.chair){const auto* chair=world_.find(goal.chair->id);
            if(!chair||chair->generation!=goal.chair->generation||!chair->entry_points||chair->entry_points->size()<=26)return reject("alternative chair identity changed");
            return true;
        }
        if(!actor->person_data.empty()&&actor->person_data[0]==1)return true;
        const auto planned=start_source_selected_walk();
        if(!planned)return {};
        if(*planned)return true;

    }
    return false;
}
std::optional<bool> Routing::source_sparse_attempt_alternative() {
    const auto reject=[&](const char* why)->std::optional<bool>{diagnostic_=why;return {};};
    if(!job_||!job_->source_sparse||!vm_||!vm_->pending()||vm_->pending()->ticket!=job_->ticket||
       vm_->status()!=VmStatus::waiting||vm_->stack().empty())
        return reject("source SoftFail has no owning routing frame");
    if(job_->source_choices&&!job_->source_choices->remaining.empty()&&job_->source_choices->remaining.front().chair)
        return advance_source_slot_choice();
    auto& sparse=*job_->source_sparse;
    auto* semantic=job_->source_choices?&*job_->source_choices:nullptr;
    auto* choices=job_->source_circle?static_cast<SourceSlotRangeRouteSnapshot*>(nullptr):
        (job_->source_range?&*job_->source_range:(sparse.choices?&*sparse.choices:nullptr));
    if(!job_->source_circle&&!choices&&!semantic) {
        if(job_->fallback_goals.empty())return false;
        return reject("source SoftFail has no frozen ranked SLOT manifest");
    }
    const auto& slot=job_->source_circle?job_->source_circle->slot:semantic?semantic->slot:choices->slot;
    const auto frame=vm_->stack().back();const auto pending=*vm_->pending();
    const auto ticket=job_->ticket,run=job_->run_generation;
    while(!job_->fallback_goals.empty()) {
        auto goal=job_->fallback_goals.front();
        SourceSlotCircleGoal frozen;
        if(semantic){
            if(semantic->remaining.empty())return reject("source semantic choices are exhausted before fallback views");
            const auto& next=semantic->remaining.front();
            frozen={next.position,next.entry_flags,next.facing,next.radian_direction};
        }else{
            const auto& remaining=job_->source_circle?job_->source_circle->fallback_goals:choices->fallback_goals;
            if(remaining.empty())return reject("source ranked choice vectors disagree");
            frozen=remaining.front();
        }
        if(!(frozen.position==goal.position)||frozen.entry_flags!=goal.entry_flags||frozen.facing!=goal.facing)
            return reject("source ranked choice vectors disagree");
        // Preserve the originally ranked source angle, never recompute/rerank.
        goal.radian_direction=frozen.radian_direction;
        auto* actor=world_.find(job_->actor);const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
        if(!actor||!target||actor->generation!=job_->actor_generation||target->generation!=job_->target_generation||
           !actor->position||!target->position||!goal.radian_direction||goal.sitting_object||
           goal.facing>7||(!semantic&&!goal.entry_flags)||!std::isfinite(*goal.radian_direction))
            return reject("source AttemptRoute alternative identity or standing goal is unavailable");
        const auto position=*actor->position,target_position=*target->position;const auto radians=actor->radian_direction;
        SourceRoomRoutePlan route;
        try {
            const auto rooms=room_pair_?room_pair_(position,target_position):std::nullopt;
            if((slot.flags&0x800U)!=0||(rooms&&rooms->first==rooms->second))route.found=true;
            else {
                if(!rooms||!source_sparse_services_.room_route)
                    return reject("source cross-room AttemptRoute requires a pure room graph service");
                SourceSparseMotionContext context;context.vm=vm_;context.frame=frame;context.dispatch={vm_,ticket,false};
                context.actor=job_->actor;context.actor_generation=job_->actor_generation;context.run_generation=run;
                context.position=position;context.root_route=suspended_jobs_.empty();context.room_context_open=job_->room_context_open;
                const auto planned=source_sparse_services_.room_route(context,target_position);
                if(!planned)return reject("source AttemptRoute room graph is unresolved");
                route=*planned;
                if((route.found&&route.portals.empty())||(!route.found&&!route.portals.empty())||route.portals.size()>max_portal_retries_)
                    return reject("source AttemptRoute room graph returned inconsistent bounded state");
                for(std::size_t n=0;n<route.portals.size();++n) {
                    const auto& edge=route.portals[n];const auto* portal=world_.find(edge.portal_id);
                    if(!portal||portal->kind!=EntityKind::object||!portal->position||!(*portal->position==edge.position)||edge.source_room==edge.target_room||
                       (n&&route.portals[n-1].target_room!=edge.source_room)||
                       (!n&&edge.source_room!=rooms->first)||
                       (n+1==route.portals.size()&&edge.target_room!=rooms->second))
                        return reject("source AttemptRoute room graph edge is stale or disconnected");
                }
            }
        }catch(...) {return reject("source AttemptRoute pure room graph callback threw");}
        actor=world_.find(job_->actor);target=world_.resolve_reference({job_->target,job_->target_generation});
        if(!job_||job_->ticket!=ticket||job_->run_generation!=run||vm_->run_generation()!=run||
           vm_->status()!=VmStatus::waiting||!vm_->pending()||!route_pending_equal(*vm_->pending(),pending)||
           vm_->stack().empty()||!route_frame_equal(vm_->stack().back(),frame)||!actor||!target||
           actor->generation!=job_->actor_generation||target->generation!=job_->target_generation||actor->attachment||
           !actor->position||!target->position||!(*actor->position==position)||!(*target->position==target_position)||
           actor->radian_direction!=radians)
            return reject("source AttemptRoute room graph changed its participants or VM frame");
        // AttemptRoute replaces CurRoute and resets WalkTo/AttemptedChair/
        // TurnTweak, regardless of graph success. Choices.RemoveAt(0) follows
        // every attempted row, preserving the original ranking and failure.
        SourceSlotCircleGoal selected{goal.position,goal.entry_flags,goal.facing,*goal.radian_direction};
        if(job_->source_circle){
            job_->source_circle->selected=selected;
            if(job_->source_circle->fallback_goals.empty())return reject("source circle choice vectors disagree");
            job_->source_circle->fallback_goals.erase(job_->source_circle->fallback_goals.begin());
        }else if(semantic){
            if(semantic->remaining.empty()||semantic->remaining.front().chair)return reject("source semantic standing choice is inconsistent");
            semantic->selected=semantic->remaining.front();semantic->remaining.erase(semantic->remaining.begin());semantic->chair_attempted=false;
            job_->face_anywhere=semantic->selected.face_anywhere;sparse.face_anywhere=job_->face_anywhere;
        }else{
            choices->selected=selected;
            if(choices->fallback_goals.empty())return reject("source ranked choice vectors disagree");
            choices->fallback_goals.erase(choices->fallback_goals.begin());
        }
        job_->fallback_goals.erase(job_->fallback_goals.begin());
        job_->entry_flags=goal.entry_flags;job_->facing=goal.facing;job_->arrival_radian_direction=*goal.radian_direction;
        job_->selected_sitting_object.reset();
        job_->path={{position.x,position.y},goal.position};job_->next=1;
        sparse.target_radian_direction=*goal.radian_direction;sparse.walk_to_pending=true;
        sparse.walk_to_purpose=SourceWalkToPurpose::ranked_alternative;
        sparse.fallback_room_route=std::move(route.portals);
        if(sparse.turn)sparse.turn->turn_tweak=0;
        // State, CurrentPath, frame counters, velocity and collision history
        // belong to the existing frame; a room graph success does not walk.
        if(route.found)return true;
    }
    return false;
}
bool Routing::source_route_tick_prefix() {
    const auto reject=[&](const char* why){diagnostic_=why;return false;};
    if(!job_||!vm_||vm_->run_generation()!=job_->run_generation||
       vm_->status()!=VmStatus::waiting||!vm_->pending()||vm_->pending()->ticket!=job_->ticket||
       vm_->stack().empty()||vm_->pending()->instruction.depth!=vm_->stack().size())
        return reject("source route tick prefix has no owning pending frame");
    if(!source_sparse_services_.source_tick)return true; // Existing standalone adapters need no RNG host.
    const auto frame=vm_->stack().back();const auto pending=*vm_->pending();
    const auto ticket=job_->ticket,run=job_->run_generation,generation=job_->actor_generation;
    const auto id=job_->actor;const auto* actor=world_.find(id);
    if(!actor||actor->generation!=generation||(!actor->position&&!job_->unplaced_room_failure))
        return reject("source route tick prefix has no authenticated actor");
    SourceSparseMotionContext context;context.vm=vm_;context.frame=frame;
    context.dispatch={vm_,ticket,false};context.actor=id;context.actor_generation=generation;
    context.run_generation=run;context.actor_unplaced=!actor->position;
    context.position=actor->position.value_or(EntityPosition{-32768,-32768,1,0});context.root_route=suspended_jobs_.empty();context.room_context_open=job_->room_context_open;
    const auto radians=actor->radian_direction;
    try {
        if(!source_sparse_services_.source_tick(context))return reject("source route tick prefix was rejected");
    }catch(...) {return reject("source route tick prefix raised an exception");}
    actor=world_.find(id);
    if(!job_||job_->ticket!=ticket||job_->run_generation!=run||job_->actor!=id||
       job_->actor_generation!=generation||vm_->run_generation()!=run||vm_->status()!=VmStatus::waiting||
       !vm_->pending()||!route_pending_equal(*vm_->pending(),pending)||vm_->stack().empty()||
       !route_frame_equal(vm_->stack().back(),frame)||!actor||actor->generation!=generation||
       actor->position.has_value()==context.actor_unplaced||
       (actor->position&&!(*actor->position==context.position))||!same_float_bits(actor->radian_direction,radians))
        return reject("source route tick prefix changed its owning frame or actor");
    return true;
}
bool Routing::tick_source_standing(){
    const auto reject=[&](const char* why){diagnostic_=why;return false;};
    if(!job_||!job_->source_choices||job_->source_sparse||(job_->source_choices->selected.chair&&!job_->source_choices->chair_attempted)||!vm_)
        return reject("source STAND_FUNC lost its selected standing goal");
    const auto* actor=world_.find(job_->actor);
    if(!actor||actor->generation!=job_->actor_generation||actor->person_data.empty()||!actor->position)
        return reject("source STAND_FUNC actor state is unavailable");
    if(actor->person_data[0]==1){
        if(!actor->attachment){
            if(!world_.set_person_data(actor->id,0,0))return reject("source unbound sitting posture reset failed");
        }else{
            const auto chair_id=actor->attachment->container;const auto* chair=world_.find(chair_id);
            if(!chair||!chair->generation||!chair->entry_points||chair->entry_points->size()<=27||
               actor->attachment->slot>=chair->slots.size()||chair->slots[actor->attachment->slot]!=actor->id)
                return reject("source STAND_FUNC physical chair is nonreciprocal");
            const auto generation=chair->generation;const auto action=chair->entry_points->at(27).action;
            if(!standing_action_)return reject("source STAND_FUNC resource provider is unavailable");
            const auto call=standing_action_(actor->id,chair_id);
            actor=world_.find(job_->actor);chair=world_.find(chair_id);
            if(!actor||actor->generation!=job_->actor_generation||!chair||chair->generation!=generation||
               !actor->attachment||actor->attachment->container!=chair_id)
                return reject("source EP27 condition changed its actor or chair identity");
            if(!call){job_->failure_code=6;job_->failure_blocker=0;return finish(false,"source chair has no schedulable EP27");}
            const auto bank=vm_->shared_program_for_owner(call->key.scope==RoutineScope::global?0:call->context.code_owner);
            const auto found=bank?bank->routines().find(call->key):RoutineSet::const_iterator{};
            if(!bank||found==bank->routines().end())return reject("source EP27 immutable function bank is unavailable");
            const auto& routine=found->second;const auto count=routine.metadata?routine.metadata->arguments:
                (routine.version==0x8000||routine.version==0x8001?std::size_t{4}:std::size_t{0});
            if(call->key.id!=action||call->context.caller!=actor->id||call->context.callee!=chair_id||
               call->context.stack_object!=chair_id||!call->exact_arguments||call->arguments!=std::vector<std::int16_t>(count,0))
                return reject("source EP27 differs from exact stand ABI");
            if(!schedule_external_frame(*call,false))return reject("source EP27 could not push its authenticated child");
            job_->frame.state=RouteFrameState::stand_func;job_->external_frame_purpose=RouteExternalFramePurpose::standing;
            job_->external_frame_callee_generation=generation;return true;
        }
    }
    const auto planned=start_source_selected_walk();
    if(!planned)return false;
    if(*planned){source_sparse_continue_=true;return true;}
    job_->failure_code=3;const auto alternative=advance_source_slot_choice();
    if(!alternative)return false;
    if(*alternative){source_sparse_continue_=true;return true;}
    return finish(false,"source standing goals exhausted AttemptWalk");
}
bool Routing::tick_source_slot_chair(){
        auto& choice=*job_->source_choices;
        const auto chair=*choice.selected.chair;const auto* object=world_.find(chair.id);
        if(!object||object->generation!=chair.generation||!object->entry_points||object->entry_points->size()<=26)
            return false;
        if(choice.chair_attempted){diagnostic_="chair attempt lost its external continuation";return false;}
        std::optional<VmCall> call;
        if(sitting_action_)call=sitting_action_(job_->actor,chair.id);
        object=world_.find(chair.id);
        if(!object||object->generation!=chair.generation||!object->entry_points||object->entry_points->size()<=26){
            diagnostic_="chair EP26 condition changed the selected chair identity";return false;
        }
        if(call){
            const auto bank=vm_->shared_program_for_owner(call->key.scope==RoutineScope::global?0:call->context.code_owner);
            const auto found=bank?bank->routines().find(call->key):RoutineSet::const_iterator{};
            const auto* routine=bank&&found!=bank->routines().end()?&found->second:nullptr;
            if(!routine){diagnostic_="chair EP26 owner routine is unavailable";return false;}
            const auto count=routine->metadata?routine->metadata->arguments:
                (routine->version==0x8000||routine->version==0x8001?std::size_t{4}:std::size_t{0});
            if(call->key.id!=object->entry_points->at(26).action||call->context.caller!=job_->actor||
               call->context.callee!=chair.id||call->context.stack_object!=chair.id||!call->exact_arguments||
               call->arguments!=std::vector<std::int16_t>(count,0)){
                diagnostic_="chair EP26 call differs from source ABI";return false;}
            choice.chair_attempted=true;
            if(schedule_external_frame(*call,true))return true;
            choice.chair_attempted=false;return false;
        }
        job_->failure_code=5;
        const auto alternative=advance_source_slot_choice();
        if(!alternative)return false;
        if(*alternative){source_sparse_continue_=true;return true;}
        return finish(false,"source chair has no schedulable EP26 and no remaining goal");
}
bool Routing::tick_source_sparse(std::uint64_t now,const std::function<void(const RouteMovement&)>& moved) {
    // Source CONTINUE repeats the same frame in one VM tick. Use iteration,
    // not native recursion, on memory-constrained targets. Work exhaustion is
    // an implementation bound, never an authored NoPath result.
    for(std::size_t work=0;work<128;++work){
        source_sparse_continue_=false;
        // The first entry was prefixed by tick(). Source CONTINUE requests a
        // distinct routing-frame entry in this same scheduler tick.
        if(work&&!source_route_tick_prefix())return false;
        if(job_->source_choices&&job_->source_choices->selected.chair&&!job_->source_choices->chair_attempted){
            if(!tick_source_slot_chair())return false;
        }else if(job_->source_choices&&!job_->source_sparse){
            if(!tick_source_standing())return false;
        }else if(!tick_source_sparse_step(now,moved))return false;
        if(!source_sparse_continue_)return true;
    }
    diagnostic_="source routing continuation work bound exhausted";return false;
}
bool Routing::tick_source_sparse_step(std::uint64_t now,const std::function<void(const RouteMovement&)>& moved) {
    const auto reject=[&](const std::string& why){diagnostic_=why;return false;};
    if(!job_||!job_->source_sparse||!vm_||!vm_->pending()||vm_->status()!=VmStatus::waiting||
       vm_->pending()->ticket!=job_->ticket||vm_->run_generation()!=job_->run_generation||
       vm_->stack().empty())return reject("source sparse route lost its exact pending frame");
    auto& sparse=*job_->source_sparse;
    const auto frame=vm_->stack().back();
    const auto instruction=vm_->pending()->instruction.instruction;
    const VmDispatch dispatch{vm_,job_->ticket,false};
    const auto still_vm=[&](){return job_&&vm_&&vm_->run_generation()==job_->run_generation&&
        vm_->status()==VmStatus::waiting&&vm_->pending()&&vm_->pending()->ticket==job_->ticket&&
        !vm_->stack().empty()&&placement_frame_equal(vm_->stack().back(),frame)&&
        placement_instruction_equal(vm_->pending()->instruction.instruction,instruction);};
    if((instruction.opcode!=45&&instruction.opcode!=27)||frame.context.caller!=job_->actor||
       frame.context.stack_object!=job_->target)return reject("source sparse pending opcode or identities changed");
    auto* actor=world_.find(job_->actor);
    auto* destination=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!destination||!destination->position||actor->generation!=job_->actor_generation||!actor->position||actor->attachment||
       !(*actor->position==job_->expected))return reject("source sparse actor changed between ticks");
    const auto make_context=[&]() {
        SourceSparseMotionContext context;context.vm=vm_;context.frame=frame;context.dispatch=dispatch;
        context.actor=job_->actor;context.actor_generation=job_->actor_generation;
        context.run_generation=job_->run_generation;context.position=*actor->position;
        context.root_route=suspended_jobs_.empty();context.room_context_open=job_->room_context_open;return context;
    };
    const auto update_expected=[&](){actor=world_.find(job_->actor);if(!actor||!actor->position)return false;
        job_->expected=*actor->position;job_->expected_radian_direction=actor->radian_direction;return true;};
    SourceRouteCollision collision;
    if(sparse.collision){
        if(!collision.restore(*sparse.collision))return reject("source collision state is not a stable continuation");
    }else{
        SourceRouteCollisionSnapshot initial;
        for(const auto identity:sparse.avatars_to_consider)initial.considered.push_back({identity.id,identity.generation});
        if(!collision.restore(initial))return reject("source collision consideration set is invalid");
    }
    const auto publish_collision=[&](){
        sparse.collision=collision.state();
        sparse.avatars_to_consider.clear();
        for(const auto identity:collision.state().considered)sparse.avatars_to_consider.push_back({identity.id,identity.generation});
        job_->sparse_consideration=sparse.avatars_to_consider;
        job_->frame.wait_time=static_cast<std::uint32_t>(collision.state().wait_time);
        if(job_->source_motion){const auto state=job_->source_motion->snapshot();
            if(state.status!=WalkMotionStatus::success||!state.snapshot)return false;
            sparse.motion=*state.snapshot;}
        return true;
    };
    const auto route_ticket=job_->ticket;
    const auto soft_failure=[&](const SourceRouteCollisionDecision& decision,bool timeout_fallthrough)->bool {
        const auto alternative=source_sparse_attempt_alternative();
        if(!alternative)return false; // Unknown graph/authority remains unsupported.
        if(*alternative) {
            if(job_->source_choices&&!job_->source_sparse){
                // A new chair choice owns a new state. The old
                // sparse reference must never be used after replacing CurRoute.
                source_sparse_continue_=true;return true;
            }
            if(collision.phase()!=SourceRouteCollisionPhase::soft_failure) {
                auto state=collision.state();state.phase=SourceRouteCollisionPhase::soft_failure;
                if(!collision.restore(state))return reject("source SoftFail cannot retain collision counters");
            }
            if(!collision.continue_after_soft_failure()||!publish_collision())
                return reject("source SoftFail cannot publish its retained continuation");
            // Collision returns CONTINUE. Timeout's prefix falls through to
            // WALKING in THIS entry; it does not request another prefix yet.
            source_sparse_continue_=!timeout_fallthrough;
            return true;
        }
        job_->failure_code=decision.failure_code;job_->failure_blocker=decision.failure_blocker.id;
        const auto failed_actor=job_->actor;const auto generation=job_->actor_generation;
        const auto result=finish(false,"source collision exhausted the authored route choices");
        const auto* failed=world_.find(failed_actor);
        return failed&&failed->generation==generation&&world_.set_person_data(failed_actor,62,decision.failure_code)&&result;
    };
    if(sparse.collision&&collision.phase()==SourceRouteCollisionPhase::waiting){
        if(!job_->source_motion)return reject("source wait has no retained movement segment");
        const auto waiting=collision.tick_wait(sparse.motion.velocity);
        if(waiting.action==SourceRouteCollisionAction::resume_walking){
            if((!sparse.walk_to_pending&&job_->source_motion->resume_after_collision()!=WalkMotionStatus::success)||!publish_collision())
                return reject("source rejected movement could not resume after waiting");
        }else{
            if(waiting.action!=SourceRouteCollisionAction::wait_next_tick&&waiting.action!=SourceRouteCollisionAction::soft_fail)
                return reject("source wait lifecycle is unsupported");
            if(job_->source_motion->set_velocity(waiting.velocity)!=WalkMotionStatus::success||!publish_collision())
                return reject("source wait could not publish its decelerated movement state");
            std::string error;const auto context=make_context();
            if(!source_sparse_services_.publish_velocity||
               !source_sparse_services_.publish_velocity(context,waiting.animation_weights,&error)||!still_vm())
                return reject(error.empty()?"source wait animation feedback is unavailable":error);
            if(waiting.action!=SourceRouteCollisionAction::soft_fail)return true;
            if(!soft_failure(waiting,true))return false;
            if(!job_||job_->ticket!=route_ticket||job_->external_frame_active||!job_->source_sparse)return true;
            // Source timeout does not replenish Timeout/WaitTime. After a
            // successful SoftFail it zeroes Velocity and executes WALKING
            // immediately, including its WalkTo-null AttemptWalk branch.
            if(!job_->source_motion||job_->source_motion->set_velocity(0)!=WalkMotionStatus::success||!publish_collision())
                return reject("source timeout cannot retain CurrentPath while zeroing velocity");
            job_->frame.state=RouteFrameState::walking;sparse.lifecycle=0;sparse.turn.reset();
        }
    }
    if(job_->room_route_invalid){
        const auto event=process_room_route_invalidation();if(!event)return false;
        if(*event)return true;
    }
    if(job_->frame.state==RouteFrameState::shooed) {
        if(!sparse.walk_to_pending||sparse.walk_to_purpose!=SourceWalkToPurpose::shoo||
           !source_sparse_services_.locomotion_profile||!source_sparse_services_.begin_locomotion)
            return reject("source SHOOED continuation services are unavailable");
        std::optional<WalkMotionProfile> profile;
        try{profile=source_sparse_services_.locomotion_profile(make_context());}
        catch(...){return reject("source SHOOED live locomotion profile threw");}
        if(!profile||!still_vm())return reject("source SHOOED live locomotion profile is unknown");
        if(!world_.set_static_footprint(job_->actor,false))return reject("source SHOOED dynamic footprint was rejected");
        std::string error;
        try{if(!source_sparse_services_.begin_locomotion(make_context(),*profile,&error))
            return reject(error.empty()?"source SHOOED walking animation restart failed":error);}
        catch(...){return reject("source SHOOED walking animation restart threw");}
        if(!still_vm())return reject("source SHOOED animation restart changed its pending frame");
        sparse.profile=*profile;sparse.lifecycle=0;sparse.turn.reset();sparse.locomotion_started=true;
        job_->walking_started=true;job_->frame.state=RouteFrameState::walking;
        source_sparse_continue_=true;return true;
    }
    if(sparse.walk_to_pending&&job_->frame.state==RouteFrameState::walking) {
        WalkMotionProfile profile;std::vector<LiveAttemptWalkIdentity> considered;
        const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
        if(!target||!target->position)return reject("source pending AttemptWalk target disappeared");
        const auto plan=source_sparse_plan(frame,instruction,dispatch,job_->actor,job_->actor_generation,
            job_->target,job_->target_generation,*actor->position,*target->position,job_->path.back(),job_->next,
            sparse.face_anywhere?std::nullopt:std::optional<float>{sparse.target_radian_direction},
            sparse.avatars_to_consider,profile,considered);
        if(!plan||!still_vm())return reject("source pending AttemptWalk context is unavailable: "+diagnostic_);
        if(plan->state==SourceWalkPlanState::no_path) {
            SourceRouteCollisionDecision failed;failed.failure_code=3;
            return soft_failure(failed,false);
        }
        if(plan->state!=SourceWalkPlanState::ready&&plan->state!=SourceWalkPlanState::turn_only)
            return reject("source pending AttemptWalk is incomplete: "+plan->diagnostic);
        if(considered.size()!=sparse.avatars_to_consider.size()||!std::equal(considered.begin(),considered.end(),
            sparse.avatars_to_consider.begin(),[](const auto& a,const auto& b){return a.id==b.id&&a.generation==b.generation;}))
            return reject("source pending AttemptWalk changed its inherited consideration set");
        if(plan->state==SourceWalkPlanState::turn_only) {
            // Source start==end leaves WalkTo null and CurrentPath untouched.
            job_->frame.state=RouteFrameState::turn_only;
        }else {
            auto motion=std::make_unique<WalkMotion>();
            if(motion->initialize(plan->segments,{{actor->position->x,actor->position->y},actor->position->level,
                actor->radian_direction},profile,true,sparse.motion.velocity)!=WalkMotionStatus::success)
                return reject("source pending AttemptWalk replacement motion is unsupported");
            job_->source_motion=std::move(motion);sparse.walk_to_pending=false;
            sparse.walk_to_purpose=SourceWalkToPurpose::ranked_alternative;
            sparse.room=plan->room;sparse.profile=profile;
        }
        if(!publish_collision())return reject("source pending AttemptWalk could not publish its motion continuation");
        source_sparse_continue_=true;return true;
    }
    const auto start_turn=[&]()->bool {
        if(!sparse.turn||!look_towards_services_.start||!look_towards_services_.ended)return false;
        auto& turn=*sparse.turn;
        if(turn.animation_num_frames)return true;
        std::optional<SourceLookTowardsProfile> profile;
        std::optional<std::uint32_t> frames;
        try {
            profile=look_towards_services_.profile?look_towards_services_.profile(frame):std::nullopt;
            if(!profile||profile->in_pool!=sparse.profile.in_pool||profile->walk_style!=sparse.profile.walk_style)return false;
            const auto speed=!profile->in_pool&&profile->walk_style==1?2.0f:1.0f;
            frames=look_towards_services_.start(frame,job_->ticket,turn.animation_index,profile->in_pool,speed);
        } catch(...) {return false;}
        if(!frames||!*frames||*frames>INT32_MAX)return false;
        const auto selection=source_turn_selection(turn.start_radian_direction,turn.target_radian_direction);
        turn.in_pool=profile->in_pool;turn.walk_style=profile->in_pool?0:profile->walk_style;
        turn.animation_speed=!profile->in_pool&&profile->walk_style==1?2.0f:1.0f;turn.animation_num_frames=*frames;
        turn.turn_tweak=selection.residual/(static_cast<float>(*frames)/turn.animation_speed);
        if((turn.animation_index&1U)!=0)turn.turn_tweak=-turn.turn_tweak;
        return true;
    };
    if(sparse.lifecycle==1||sparse.lifecycle==2){
        if(!start_turn())return reject("source sparse turn clip is unavailable or its profile changed");
        std::optional<bool> ended;try{ended=look_towards_services_.ended(frame,job_->ticket);}catch(...){return reject("source sparse shared turn callback threw");}
        if(!ended||!still_vm())return reject("source sparse shared turn-animation state is unavailable or changed the VM frame");
        if(!*ended){
            if(!world_.set_radian_direction(job_->actor,job_->expected_radian_direction+sparse.turn->turn_tweak)||
               !update_expected())return reject("source sparse turn tweak could not be published");
            return true;
        }
        if(sparse.lifecycle==1){
            if(!world_.set_radian_direction(job_->actor,static_cast<float>(sparse.motion.target_direction)))return reject("source sparse BeginWalk direction rejected");
            if(!world_.set_static_footprint(job_->actor,false)||!source_sparse_services_.begin_locomotion)
                return reject("source sparse BeginWalk locomotion service unavailable");
            std::string error;const auto context=make_context();
            try{if(!source_sparse_services_.begin_locomotion(context,sparse.profile,&error))
                return reject(error.empty()?"source sparse BeginWalk locomotion start failed":error);}
            catch(...){return reject("source sparse BeginWalk animation callback threw");}
            if(!still_vm())return reject("source sparse BeginWalk callback changed its pending VM frame");
            sparse.locomotion_started=true;job_->walking_started=true;sparse.lifecycle=0;sparse.turn.reset();
            job_->frame.state=RouteFrameState::walking;
            if(!update_expected())return reject("source sparse BeginWalk actor disappeared");
            source_sparse_continue_=true;return true;
        }else{
            if(!world_.set_radian_direction(job_->actor,sparse.target_radian_direction))
                return reject("source sparse EndWalk final direction rejected");
            const auto reset_profile=look_towards_services_.profile?look_towards_services_.profile(frame):std::nullopt;
            if(!reset_profile||!look_towards_services_.reset||!look_towards_services_.reset(frame,job_->ticket,reset_profile->in_pool))
                return reject("source sparse EndWalk standing-loop reset failed");
            if(!update_expected())return reject("source sparse EndWalk actor disappeared");
            if(!source_sparse_services_.end_locomotion)return reject("source sparse EndWalk animation service unavailable");
            {std::string error;const auto context=make_context();try{if(!source_sparse_services_.end_locomotion(context,sparse.profile,&error))
                return reject(error.empty()?"source sparse EndWalk animation cleanup failed":error);}catch(...){return reject("source sparse EndWalk animation callback threw");}}
            if(!still_vm())return reject("source sparse EndWalk callback changed its pending VM frame");
            return finish(true,{});
        }
    }
    const auto finish_walk=[&]()->bool {
        if(job_->source_motion){
            const auto captured=job_->source_motion->snapshot();
            if(captured.status!=WalkMotionStatus::success||!captured.snapshot)
                return reject("source terminal waypoint motion could not be captured before EndWalk");
            sparse.motion=*captured.snapshot;
        }
        if(!world_.set_person_data(job_->actor,37,job_->entry_flags))return reject("source sparse RouteEntryFlags write failed");
        auto* parent=suspended_jobs_.empty()?nullptr:&suspended_jobs_.back().frame;
        const bool may_turn=!sparse.face_anywhere&&consume_route_portal_turn(job_->frame,parent);
        const auto selection=source_turn_selection(actor->radian_direction,sparse.target_radian_direction);
        if(!sparse.face_anywhere&&may_turn&&selection.index){
            SourceLookTowardsRouteSnapshot turn;turn.start_radian_direction=actor->radian_direction;
            turn.target_radian_direction=sparse.target_radian_direction;turn.walk_style=sparse.profile.walk_style;
            turn.in_pool=sparse.profile.in_pool;turn.animation_index=selection.index;turn.turn_tweak=selection.residual;
            sparse.turn=turn;sparse.lifecycle=2;job_->frame.state=RouteFrameState::end_turn;
            if(!start_turn()||!still_vm())return reject("source sparse EndWalk turn clip could not start on arrival");
            return true;
        }
        if(!sparse.face_anywhere&&!world_.set_radian_direction(job_->actor,sparse.target_radian_direction))
            return reject("source sparse EndWalk final direction rejected");
        if(!update_expected())return reject("source sparse EndWalk actor disappeared");
        if(!source_sparse_services_.end_locomotion)return reject("source sparse EndWalk animation service unavailable");
        {std::string error;const auto context=make_context();try{if(!source_sparse_services_.end_locomotion(context,sparse.profile,&error))
            return reject(error.empty()?"source sparse EndWalk animation cleanup failed":error);}catch(...){return reject("source sparse EndWalk animation callback threw");}}
        if(!still_vm())return reject("source sparse EndWalk callback changed its pending VM frame");
        return finish(true,{});
    };
    if(!job_->source_motion||job_->frame.state==RouteFrameState::turn_only){
        if(sparse.lifecycle==1)return true;
        const auto finishing_ticket=job_->ticket;
        if(!finish_walk())return false; // source TURN_ONLY
        // TURN_ONLY returns CONTINUE into END_TURN in the same VM tick;
        // WALKING arrival, below, deliberately waits until the next tick.
        if(job_&&job_->ticket==finishing_ticket&&job_->source_sparse&&job_->source_sparse->lifecycle==2)
            {source_sparse_continue_=true;return true;}
        return true;
    }
    if(!sparse.locomotion_started){
        const auto initial_heading=static_cast<float>(sparse.motion.target_direction);
        if(!world_.set_radian_direction(job_->actor,initial_heading))
            return reject("source sparse BeginWalk facing update failed");
        if(!world_.set_static_footprint(job_->actor,false)||!source_sparse_services_.begin_locomotion)
            return reject("source sparse BeginWalk locomotion service unavailable");
        std::string error;const auto context=make_context();
        try{if(!source_sparse_services_.begin_locomotion(context,sparse.profile,&error))
            return reject(error.empty()?"source sparse BeginWalk locomotion start failed":error);}
        catch(...){return reject("source sparse BeginWalk animation callback threw");}
        if(!still_vm())return reject("source sparse BeginWalk callback changed its pending VM frame");
        sparse.locomotion_started=true;job_->walking_started=true;
        if(!update_expected())return reject("source sparse BeginWalk actor disappeared");
    }
    actor=world_.find(job_->actor);
    const auto pose=WalkMotionPose{{actor->position->x,actor->position->y},actor->position->level,actor->radian_direction};
    auto* parent=suspended_jobs_.empty()?nullptr:&suspended_jobs_.back().frame;
    auto prepared=job_->source_motion->prepare_step(job_->frame,parent);
    if(prepared.status==WalkMotionStatus::waypoint_required){
        const auto advanced=job_->source_motion->advance_waypoint(pose,sparse.profile,true);
        if(advanced==WalkMotionStatus::finished)return finish_walk();
        if(advanced!=WalkMotionStatus::success)return reject("source sparse waypoint advance is unsupported");
        const auto captured=job_->source_motion->snapshot();
        if(captured.status!=WalkMotionStatus::success||!captured.snapshot)return reject("source sparse waypoint snapshot failed");
        sparse.motion=*captured.snapshot;
        return true;
    }
    if(prepared.status==WalkMotionStatus::finished)return finish_walk();
    if(prepared.status!=WalkMotionStatus::success||!prepared.step)return reject("source sparse movement step is unavailable");
    const auto point=prepared.step->data().sample.position;
    const auto saved_direction=actor->radian_direction;
    auto world_destination=*actor->position;world_destination.x=point.x;world_destination.y=point.y;world_destination.direction=0;
    last_sparse_placement_.reset();
    if(position_publication_){
        std::string error;last_sparse_placement_=publish_route_position(world_destination,{point.x,point.y},job_->next,true,&error);
        if(!last_sparse_placement_)return reject(error);
        if(!world_.set_radian_direction(job_->actor,saved_direction))return reject("source sparse lifecycle facing restoration failed");
    }else if(auto error=invoke_route_execution_callback(RouteExecutionPhase::set_position,frame,instruction,dispatch,
        job_->actor,job_->actor_generation,job_->target,job_->target_generation,*actor->position,
        *world_.resolve_reference({job_->target,job_->target_generation})->position,{point.x,point.y},job_->next))return reject(*error);
    if(!last_sparse_placement_)return reject("source sparse SetPosition preflight returned unknown");
    if(last_sparse_placement_->status!=SourceSparsePlacementStatus::success&&
       last_sparse_placement_->status!=SourceSparsePlacementStatus::cant_be_through_wall){
        if(job_->source_motion->reject_step(*prepared.step)!=WalkMotionStatus::collision_required)
            return reject("source rejected movement lease could not be retained");
        const auto placement=*last_sparse_placement_;
        if(placement.status==SourceSparsePlacementStatus::unsupported)
            return reject(placement.diagnostic.empty()?"source placement is unresolved":placement.diagnostic);
        SourceRouteCollisionInput input;input.caller={job_->actor,job_->actor_generation};
        if(placement.blocker){
            const auto* blocker=world_.find(*placement.blocker);
            if(!blocker||blocker->generation!=placement.blocker_generation)
                return reject("source collision blocker identity changed");
            input.blocker={blocker->id,blocker->generation};
            input.kind=blocker->kind==EntityKind::avatar?SourceRouteCollisionBlocker::avatar:SourceRouteCollisionBlocker::object;
        }
        if(input.kind==SourceRouteCollisionBlocker::avatar){
            const auto known=source_sparse_services_.avatar_collision_state?
                source_sparse_services_.avatar_collision_state(make_context(),{input.blocker.id,input.blocker.generation}):std::nullopt;
            if(!known||!still_vm())return reject("source colliding avatar's routing state is unknown");
            input.avatar_top_route=known->top_route;input.avatar_route_wait=known->wait_time;
        }
        std::optional<RouteBlockedMovementReceipt> blocked_receipt;
        if(outcome_observer_){
            blocked_receipt.emplace();auto& observed=*blocked_receipt;
            observed.tick=now;observed.candidate=world_destination;observed.placement_status=static_cast<std::uint8_t>(placement.status);
            observed.placement_error=placement.placement_error;observed.blocker={input.blocker.id,input.blocker.generation};
            observed.retries_before=collision.state().retries;
        }
        const auto record_blocked=[&]{
            if(!blocked_receipt)return;
            blocked_receipt->retries_after=collision.state().retries;
            job_->last_blocked_movement=*blocked_receipt;
            if(!job_->first_blocked_movement)job_->first_blocked_movement=*blocked_receipt;
        };
        auto decision=collision.begin_collision(input);
        if(blocked_receipt){
            if(job_->blocked_movement_count!=std::numeric_limits<std::uint64_t>::max())++job_->blocked_movement_count;
            blocked_receipt->action=decision.action;record_blocked();
        }
        if(!publish_collision())return reject("source collision counter publication failed");
        if(decision.action==SourceRouteCollisionAction::attempt_walk){
            WalkMotionProfile profile;std::vector<LiveAttemptWalkIdentity> considered;
            const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
            if(!target||!target->position)return reject("source replan target disappeared");
            const auto plan=source_sparse_plan(frame,instruction,dispatch,job_->actor,job_->actor_generation,
                job_->target,job_->target_generation,*actor->position,*target->position,job_->path.back(),job_->next,
                sparse.face_anywhere?std::nullopt:std::optional<float>{sparse.target_radian_direction},
                sparse.avatars_to_consider,profile,considered);
            if(!plan||!still_vm())return reject("source collision AttemptWalk context is unavailable: "+diagnostic_);
            if(blocked_receipt){blocked_receipt->replan=plan->state;
                if(job_->blocked_movement_count==1)job_->first_blocked_movement->replan=plan->state;
                record_blocked();}
            if(plan->state==SourceWalkPlanState::ready||plan->state==SourceWalkPlanState::turn_only){
                const auto velocity=sparse.motion.velocity;
                if(plan->state==SourceWalkPlanState::turn_only){
                    job_->source_motion.reset();sparse.motion={};job_->frame.state=RouteFrameState::turn_only;
                }else{
                    auto motion=std::make_unique<WalkMotion>();
                    if(motion->initialize(plan->segments,{{actor->position->x,actor->position->y},actor->position->level,
                        actor->radian_direction},profile,true,velocity)!=WalkMotionStatus::success)
                        return reject("source collision replacement motion is unsupported");
                    job_->source_motion=std::move(motion);sparse.profile=profile;sparse.room=plan->room;
                }
                decision=collision.finish_reroute(SourceRouteCollisionReplan::ready);
            }else if(plan->state==SourceWalkPlanState::no_path){
                std::optional<bool> can_shoo;
                if(input.kind==SourceRouteCollisionBlocker::avatar&&source_sparse_services_.avatar_collision_state){
                    const auto known=source_sparse_services_.avatar_collision_state(make_context(),{input.blocker.id,input.blocker.generation});
                    if(!known||!still_vm())return reject("source CanShooAvatar state is unresolved");
                    can_shoo=known->can_shoo;
                }
                decision=collision.finish_reroute(SourceRouteCollisionReplan::no_path,can_shoo);
            }else return reject("source collision AttemptWalk is incomplete: "+plan->diagnostic);
            if(!publish_collision())return reject("source collision replacement state could not be retained");
        }
        if(decision.action==SourceRouteCollisionAction::shoo_direct_route||
           decision.action==SourceRouteCollisionAction::shoo_interaction){
            std::string error;
            bool shoo_completed=false;
            {
                auto context=make_context();auto scope=std::make_shared<RouteResourceOwnerProof::State>();
                scope->owner=this;scope->context=&context;scope->thread=&resource_owner_thread_identity;scope->current=still_vm;
                context.owner_binding=RouteResourceOwnerProof(scope);
                struct EndOwnerScope {std::shared_ptr<RouteResourceOwnerProof::State>& state;~EndOwnerScope(){state->active=false;}} end_owner_scope{scope};
                shoo_completed=source_sparse_services_.shoo&&source_sparse_services_.shoo(context,
                    {input.blocker.id,input.blocker.generation},decision.action==SourceRouteCollisionAction::shoo_direct_route,&error);
            }
            if(!shoo_completed||!still_vm())return reject(error.empty()?"source shoo effects are unavailable":error);
            decision=collision.acknowledge_shoo(true);
            if(!publish_collision())return reject("source shoo continuation could not be retained");
        }
        if(blocked_receipt){blocked_receipt->action=decision.action;record_blocked();
            if(job_->blocked_movement_count==1)job_->first_blocked_movement=*blocked_receipt;}
        if(decision.action==SourceRouteCollisionAction::soft_fail)return soft_failure(decision,false);
        if(decision.action==SourceRouteCollisionAction::continue_replanned||decision.action==SourceRouteCollisionAction::wait_next_tick){
            source_sparse_continue_=true;return true;
        }
        return reject("source collision policy requires unavailable live state");
    }
    actor=world_.find(job_->actor);if(!actor||!actor->position||actor->generation!=job_->actor_generation)
        return reject("source sparse actor disappeared before placement");
    if(!position_publication_&&(!world_.set_position(job_->actor,world_destination)||!world_.set_radian_direction(job_->actor,saved_direction)))
        return reject("source sparse SetPosition publication failed");
    const auto committed=job_->source_motion->commit_step(*prepared.step);
    if((committed.status!=WalkMotionStatus::success&&committed.status!=WalkMotionStatus::waypoint_required)||!committed.effects)
        return reject("source sparse motion lease became stale after SetPosition");
    if(!world_.set_radian_direction(job_->actor,committed.effects->radian_direction)||!update_expected())
        return reject("source sparse motion direction feedback failed");
    if(moved)moved(RouteMovement{now,job_->actor,*actor->position});
    if(!source_sparse_services_.publish_motion)return reject("source sparse animation/visual feedback service unavailable");
    {std::string error;const auto context=make_context();
        try{if(!source_sparse_services_.publish_motion(context,*committed.effects,prepared.step->data().animation_weights,&error))
            return reject(error.empty()?"source sparse motion feedback failed":error);}
        catch(...){return reject("source sparse motion feedback callback threw");}}
    if(!still_vm())return reject("source sparse motion feedback changed its pending VM frame");
    if(job_->source_motion->phase()==WalkMotionPhase::waypoint_required){
        const auto next=job_->source_motion->advance_waypoint(
            WalkMotionPose{{actor->position->x,actor->position->y},actor->position->level,actor->radian_direction},
            sparse.profile,true);
        if(next==WalkMotionStatus::finished)return finish_walk();
        if(next!=WalkMotionStatus::success)return reject("source sparse same-tick waypoint advance failed");
    }
    const auto captured=job_->source_motion->snapshot();
    if(captured.status!=WalkMotionStatus::success||!captured.snapshot)return reject("source sparse motion snapshot failed");
    sparse.motion=*captured.snapshot;
    return true;
}
bool Routing::tick_look_towards() {
    const auto reject=[&](const std::string& why){diagnostic_=why;return false;};
    if(!job_||!job_->look_towards||!vm_||!vm_->pending()||vm_->stack().empty()||
       vm_->run_generation()!=job_->run_generation||vm_->pending()->ticket!=job_->ticket||
       vm_->pending()->instruction.instruction.opcode!=22||
       vm_->pending()->instruction.instruction.operands[0]!=job_->look_towards->mode)
        return reject("LookTowards lost its pending source frame");
    const auto frame=vm_->stack().back();const auto ticket=job_->ticket,run=job_->run_generation;
    const auto actor_id=job_->actor;const auto generation=job_->actor_generation;
    auto& turn=*job_->look_towards;
    const auto still_valid=[&](){
        const auto* actor=world_.find(actor_id);
        return job_&&job_->ticket==ticket&&job_->run_generation==run&&vm_->run_generation()==run&&
            vm_->status()==VmStatus::waiting&&vm_->pending()&&vm_->pending()->ticket==ticket&&
            !vm_->stack().empty()&&vm_->stack().back().context.caller==actor_id&&actor&&
            actor->generation==generation&&actor->position&&*actor->position==job_->expected&&
            same_float_bits(actor->radian_direction,job_->expected_radian_direction);
    };
    try {
        if(job_->frame.state==RouteFrameState::turn_only){
            // EndWalk always writes the default NORTH entry flags, even if a
            // portal parent has already used its one animated-turn allowance.
            const auto selection=source_turn_selection(turn.start_radian_direction,turn.target_radian_direction);
            if(!world_.set_person_data(actor_id,37,1))return reject("LookTowards route entry flags rejected");
            auto* parent=suspended_jobs_.empty()?nullptr:&suspended_jobs_.back().frame;
            const bool may_turn=consume_route_portal_turn(job_->frame,parent);
            if(!selection.index||!may_turn){
                if(!world_.set_radian_direction(actor_id,turn.target_radian_direction))
                    return reject("LookTowards final direction or entry flags rejected");
                return finish(true,{});
            }
            const auto profile=look_towards_services_.profile?look_towards_services_.profile(frame):std::nullopt;
            if(!profile||!still_valid())return reject("LookTowards pool/walk-style service changed its source identity");
            const auto speed=!profile->in_pool&&profile->walk_style==1?2.0f:1.0f;
            const auto frames=look_towards_services_.start?
                look_towards_services_.start(frame,ticket,selection.index,profile->in_pool,speed):std::nullopt;
            if(!frames||!*frames||*frames>INT32_MAX||!still_valid())
                return reject("LookTowards native turn animation is unavailable or changed its source identity");
            turn.in_pool=profile->in_pool;turn.walk_style=profile->in_pool?0:profile->walk_style;
            turn.animation_index=selection.index;turn.animation_speed=speed;turn.animation_num_frames=*frames;
            turn.turn_tweak=selection.residual/(static_cast<float>(*frames)/speed);
            if((selection.index&1U)!=0)turn.turn_tweak=-turn.turn_tweak;
            job_->frame.state=RouteFrameState::end_turn;
        }
        if(job_->frame.state!=RouteFrameState::end_turn)return reject("LookTowards frame is outside its turn lifecycle");
        const auto ended=look_towards_services_.ended?look_towards_services_.ended(frame,ticket):std::nullopt;
        if(!ended||!still_valid())return reject("LookTowards shared animation state is unavailable or changed its source identity");
        if(*ended){
            const auto profile=look_towards_services_.profile?look_towards_services_.profile(frame):std::nullopt;
            if(!profile||!still_valid())return reject("LookTowards reset pool capability is unknown");
            // END_TURN resets using the CURRENT room's pool state. It need not
            // be the same animation family used to begin this turn.
            if(!world_.set_radian_direction(actor_id,turn.target_radian_direction))
                return reject("LookTowards final continuous angle rejected");
            const auto* actor=world_.find(actor_id);job_->expected=*actor->position;
            job_->expected_radian_direction=actor->radian_direction;
            if(!look_towards_services_.reset||!look_towards_services_.reset(frame,ticket,profile->in_pool)||!still_valid())
                return reject("LookTowards native standing-loop reset failed");
            return finish(true,{});
        }
        if(!world_.set_radian_direction(actor_id,job_->expected_radian_direction+turn.turn_tweak))
            return reject("LookTowards continuous tweak rejected");
        const auto* actor=world_.find(actor_id);job_->expected=*actor->position;
        job_->expected_radian_direction=actor->radian_direction;
        return true;
    }catch(const std::exception& error){return reject(std::string("LookTowards native animation callback failed: ")+error.what());}
}
void Routing::end_room_context(Job& job) {
    if(!job.room_context_open)return;
    job.room_context_open=false;
    if(room_portal_context_end_)room_portal_context_end_(job.ticket);
}
// Global resources are immutable and can also be inspected on restore. A
// private table is authenticated by its owner at the next scheduler tick;
// capture/restore of a deferred request must not run the host lookup.
bool Routing::cross_floor_slot_matches(const VmFrame& frame,const Instruction& op,bool inspect_private)const{
    if(op.opcode!=45||!general_slot_provider_||!authenticated_range_goals_||!source_sparse_services_.plan_input||
       !source_sparse_services_.capture_avatars_to_consider||(op.operands[4]&0xfeU)||op.operands[5]||op.operands[6]||op.operands[7])return false;
    const auto scope=op.operands[2]|(op.operands[3]<<8);
    std::size_t index=op.operands[0]|(op.operands[1]<<8);
    if(scope==0){
        if(index>=frame.arguments.size()||frame.arguments[index]<0)return false;
        index=static_cast<std::size_t>(frame.arguments[index]);
    }
    const SlotTable* table=&globals_;
    if(scope!=2){
        if((scope!=0&&scope!=1)||!stack_object_slots_)return false;
        if(!inspect_private)return true;
        table=stack_object_slots_(frame);
    }
    const auto* slot=table?slot_of_type(*table,3,index):nullptr;
    return slot&&(slot->flags&0x0800U)==0;
}
bool Routing::advance_room_route(const std::optional<VmCall>& previous_call,
                                 const std::optional<bool>& previous_result) {
    if(!job_||!job_->deferred_room_route||!room_portal_step_||!vm_||
       vm_->run_generation()!=job_->run_generation||vm_->status()!=VmStatus::waiting||
       !vm_->pending()||vm_->pending()->ticket!=job_->ticket)
        return job_?finish(false,"deferred room route lost its root VM ticket"):false;
    const auto* actor=world_.find(job_->actor);const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!target||actor->generation!=job_->actor_generation||
       target->generation!=job_->target_generation||!actor->position||!target->position||
       !target_checkpoint_matches(world_,job_->actor,job_->actor_generation,job_->target,job_->target_generation,job_->carried_target,job_->target_position,(captured_source_goals(*job_)||job_->deferred_room_route))||(actor->attachment&&(actor->person_data.empty()||actor->person_data[0]!=1)))
        return finish(false,"deferred room route participants changed");
    const auto actor_position=*actor->position,target_position=*target->position;
    const auto actor_direction=actor->radian_direction;const auto attachment=actor->attachment;
    const auto step_index=job_->portal_steps;
    if(actor_position.level!=target_position.level&&job_->replay_instruction&&job_->replay_instruction->opcode==45){
        const auto pending=*vm_->pending();
        const auto frame=*job_->replay_frame;
        const auto matched=[&]{
            struct Guard{bool& flag;bool prior;~Guard(){flag=prior;}} guard{executing_,executing_};
            executing_=true;
            return cross_floor_slot_matches(frame,*job_->replay_instruction,true);
        }();
        actor=world_.find(job_->actor);target=world_.resolve_reference({job_->target,job_->target_generation});
        if(!matched||!actor||actor->generation!=job_->actor_generation||!actor->position||!(*actor->position==actor_position)||
           !target||!target->position||!(*target->position==target_position)||actor->radian_direction!=actor_direction||
           !placement_attachment_equal(actor->attachment,attachment)||vm_->run_generation()!=job_->run_generation||
           !vm_->pending()||!route_pending_equal(*vm_->pending(),pending)||vm_->stack().empty()||
           !route_frame_equal(vm_->stack().back(),frame)){
            diagnostic_="deferred cross-floor SLOT resource, participants or ticket changed";return false;
        }
    }
    const auto plan=[&] {
        struct CallbackGuard {bool& flag;bool previous;explicit CallbackGuard(bool& value):flag(value),previous(value){flag=true;}~CallbackGuard(){flag=previous;}} callback_guard(executing_);
        return room_portal_step_(job_->ticket,actor_position,target_position,previous_call,previous_result,step_index);
    }();
    actor=world_.find(job_->actor);target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!target||actor->generation!=job_->actor_generation||
       target->generation!=job_->target_generation||!actor->position||!target->position||
       !(*actor->position==actor_position)||!(*target->position==target_position)||
       actor->radian_direction!=actor_direction||!placement_attachment_equal(actor->attachment,attachment))
        return finish(false,"Route45 portal planner changed route participants");
    if(plan.status==RoutePortalStepStatus::unsupported){
        diagnostic_=plan.diagnostic.empty()?"room portal step could not be authenticated":plan.diagnostic;
        return false;
    }
    if(plan.status==RoutePortalStepStatus::unavailable){
        job_->failure_code=2; // VMRouteFailCode.NoRoomRoute.
        const auto actor_id=job_->actor;const auto generation=job_->actor_generation;
        const bool completed=finish(false,plan.diagnostic.empty()?"no route between rooms":plan.diagnostic);
        const auto* current=world_.find(actor_id);
        return current&&current->generation==generation&&world_.set_person_data(actor_id,62,2)&&completed;
    }
    if(plan.status==RoutePortalStepStatus::call_ready) {
        if(!plan.call||plan.call->return_mode!=VmCallReturnMode::ordinary||
           job_->portal_steps>=max_portal_retries_||!vm_->call_pending(job_->ticket,*plan.call))
            return finish(false,"room portal planner returned an invalid, over-budget, or unschedulable call");
        ++job_->portal_steps;
        job_->external_frame_active=true;
        job_->external_frame_owner=plan.call->context.code_owner;
        job_->external_frame_call=*plan.call;if(job_->external_frame_call)world_.bind_context_live(job_->external_frame_call->context);
        job_->expected=*actor->position;
        job_->expected_radian_direction=actor->radian_direction;
        job_->frame.state=RouteFrameState::room_portal;
        return true;
    }
    if(plan.call||!room_pair_||!job_->replay_frame||!job_->replay_instruction)
        return finish(false,"room portal planner reported arrival without a valid local Route45 request");
    const auto rooms=[&] {
        struct CallbackGuard {bool& flag;bool previous;explicit CallbackGuard(bool& value):flag(value),previous(value){flag=true;}~CallbackGuard(){flag=previous;}} callback_guard(executing_);
        return room_pair_(actor_position,target_position);
    }();
    actor=world_.find(job_->actor);target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!rooms||rooms->first!=rooms->second||actor_position.level!=target_position.level||!actor||!target||
       actor->generation!=job_->actor_generation||target->generation!=job_->target_generation||
       !actor->position||!target->position||!(*actor->position==actor_position)||
       !(*target->position==target_position)||actor->radian_direction!=actor_direction||!placement_attachment_equal(actor->attachment,attachment))
        return finish(false,"portal planner claimed destination before the actor reached the target room");

    auto deferred=std::move(*job_);
    job_.reset();
    struct DeferredResetStorage {Job*& slot;Job* previous;~DeferredResetStorage(){slot=previous;}}
        deferred_reset_storage{source_reset_deferred_,source_reset_deferred_};
    source_reset_deferred_=&deferred;
    const auto previous_sparse_consideration=replay_sparse_consideration_;
    replay_sparse_consideration_=deferred.sparse_consideration;
    struct RestoreSparseConsideration {
        std::optional<std::vector<LiveAttemptWalkIdentity>>& slot;
        std::optional<std::vector<LiveAttemptWalkIdentity>> previous;
        ~RestoreSparseConsideration(){slot=std::move(previous);}
    } restore_sparse{replay_sparse_consideration_,previous_sparse_consideration};
    const auto was_executing=executing_;
    executing_=false;
    PrimitiveOutcome outcome;
    replay_placement_range_=false;
    const auto replay_failure=[&](const std::string& reason,const char* phase,const char* exception){
        std::string detail=reason+" phase="+phase+" actor="+std::to_string(deferred.actor)+"/"+
            std::to_string(deferred.actor_generation)+" target="+std::to_string(deferred.target)+"/"+
            std::to_string(deferred.target_generation)+" run="+std::to_string(deferred.run_generation)+
            " ticket="+std::to_string(deferred.ticket)+" frame_state="+std::to_string(static_cast<unsigned>(deferred.frame.state))+
            " portal_steps="+std::to_string(deferred.portal_steps)+" portal_retries="+std::to_string(deferred.portal_retries)+
            " authenticated_range="+std::to_string(replay_placement_range_);
        if(deferred.replay_frame)detail+=" owner="+std::to_string(deferred.replay_frame->context.code_owner)+
            " routine="+std::to_string(deferred.replay_frame->key.id)+" pc="+std::to_string(deferred.replay_frame->pc);
        if(deferred.replay_instruction)detail+=" opcode="+std::to_string(deferred.replay_instruction->opcode);
        if(exception){
            std::string bounded;for(const char* at=exception;*at&&bounded.size()<512;++at)bounded.push_back(*at);
            detail+=" exception="+bounded;
        }
        return detail;
    };
    try {
        if(!vm_->pending()||vm_->pending()->ticket!=deferred.ticket||vm_->run_generation()!=deferred.run_generation)
            throw std::logic_error("private range replay pending authority is stale");
        replay_placement_authorization_=std::make_pair(deferred.run_generation,deferred.ticket);
        struct ReplayEnd {std::optional<std::pair<std::uint64_t,std::uint64_t>>& value;~ReplayEnd(){value.reset();}} replay_end{replay_placement_authorization_};
        outcome=request(*deferred.replay_frame,*deferred.replay_instruction,
                        VmDispatch{vm_,deferred.ticket});
    } catch(const SourceCheckResetRetirement&) {
        executing_=was_executing;throw;
    } catch(const std::exception& error) {
        executing_=was_executing;
        if(replay_placement_range_){
            const auto reason=replay_failure("authenticated range planning is unsupported after room transition",
                "room_transition_local_replay_exception",error.what());
            job_=std::move(deferred);diagnostic_=reason;return false;
        }
        const auto reason=replay_failure("local Route45 SLOT planning raised an exception after room transition",
            "room_transition_local_replay_exception",error.what());
        job_=std::move(deferred);
        return finish(false,reason);
    } catch(...) {
        executing_=was_executing;
        if(replay_placement_range_){
            job_=std::move(deferred);diagnostic_="authenticated range planning is unsupported after room transition";return false;
        }
        job_=std::move(deferred);
        return finish(false,"local Route45 SLOT planning raised an exception after room transition");
    }
    executing_=was_executing;
    if(outcome.disposition==PrimitiveDisposition::wait&&job_&&job_->ticket==deferred.ticket&&
       !job_->deferred_room_route) {
        if(job_->source_sparse) {
            job_->room_context_open=deferred.room_context_open;
            job_->portal_steps=deferred.portal_steps;job_->portal_retries=deferred.portal_retries;
            job_->external_frame_call=deferred.external_frame_call;job_->external_frame_owner=deferred.external_frame_owner;
            deferred.room_context_open=false;
        }
        end_room_context(deferred);
        return true;
    }
    if(replay_placement_range_&&outcome.disposition==PrimitiveDisposition::unsupported){
        const auto reason=replay_failure(outcome.diagnostic.empty()?"authenticated range planning is unsupported after room transition":outcome.diagnostic,
            "room_transition_local_replay_unsupported",nullptr);
        if(job_)job_.reset();
        job_=std::move(deferred);
        diagnostic_=reason;
        return false;
    }
    const auto reason=diagnostic_.empty()?"local Route45 SLOT planning failed after room transition":diagnostic_;
    if(job_)job_.reset();
    job_=std::move(deferred);
    return finish(false,reason);
}
std::optional<RouteOutcomeReceipt> Routing::outcome_receipt(const Job& job,bool result,
        const std::string& reason,RouteOutcomeStage stage) const noexcept {
    if(!outcome_observer_)return {};
    try{
        RouteOutcomeReceipt row;row.stage=stage;row.ticket=job.ticket;row.run_generation=job.run_generation;
        row.actor={job.actor,job.actor_generation};row.target={job.target,job.target_generation};
        row.result=result;row.reason=reason;row.nested_depth=suspended_jobs_.size();
        row.portal_steps=job.portal_steps;row.portal_retries=job.portal_retries;row.frame_state=job.frame.state;
        row.path_points=job.path.size();row.next_point=job.next;row.failure_tree_invoked=job.failure_tree_invoked;
        row.restored_job=job.diagnostic_restored;row.blocked_movement_count=job.blocked_movement_count;
        row.first_blocked_movement=job.first_blocked_movement;row.last_blocked_movement=job.last_blocked_movement;
        if(!result){row.failure_code=job.failure_code;row.blocker.id=job.failure_blocker;
            if(const auto* blocker=world_.find(job.failure_blocker)){row.blocker.generation=blocker->generation;row.blocker_generation_is_current_lookup=true;}}
        const auto pose=[&](WorldEntityReference reference,std::optional<EntityPosition>& position,std::optional<std::int16_t>& room){
            const auto* entity=world_.resolve_reference(reference);if(!entity)return;
            position=entity->position;
            if(const auto value=entity->object_words.find(29);value!=entity->object_words.end())room=value->second;
        };
        pose(row.actor,row.actor_position,row.actor_object_room);pose(row.target,row.target_position,row.target_object_room);
        if(job.source_choices){const auto& goal=job.source_choices->selected;
            row.selected_goal=EntityPosition{goal.position.x,goal.position.y,goal.level,goal.facing};row.selected_goal_origin="source_choices";
        }else if(job.source_range){const auto& goal=job.source_range->selected;
            row.selected_goal=EntityPosition{goal.position.x,goal.position.y,job.target_position.level,goal.facing};row.selected_goal_origin="source_range";
        }else if(job.source_circle){const auto& goal=job.source_circle->selected;
            row.selected_goal=EntityPosition{goal.position.x,goal.position.y,job.target_position.level,goal.facing};row.selected_goal_origin="source_circle";
        }else if(!job.path.empty()){const auto goal=job.path.back();row.selected_goal=EntityPosition{goal.x,goal.y,job.target_position.level,job.facing};row.selected_goal_origin="path_end";}
        if(job.source_sparse)row.sparse_room=job.source_sparse->room;
        const VmPending* source=nullptr;
        const auto take=[&](const VmPending& pending){if(pending.ticket==job.ticket&&!source)source=&pending;};
        if(vm_){if(vm_->pending())take(*vm_->pending());for(const auto& pending:vm_->suspended_pending())take(pending);
            for(const auto& parked:vm_->immediate_continuations())if(parked.parent_pending)take(*parked.parent_pending);}
        if(source){row.source_available=true;row.source_is_pending=true;row.owner=source->context.code_owner;
            row.routine=source->instruction.key;row.pc=source->instruction.pc;row.source_depth=source->instruction.depth;
            row.instruction=source->instruction.instruction;}
        return row;
    }catch(...){return {};}
}
void Routing::emit_outcome(const RouteOutcomeReceipt& receipt) const noexcept {
    try{if(outcome_observer_)outcome_observer_(receipt);}catch(...){}
}
bool Routing::finish(bool result,std::string reason) {
    if(!result&&!job_->failure_tree_invoked&&!job_->unplaced_room_failure){
        const auto* actor=world_.find(job_->actor);
        if(actor&&actor->generation==job_->actor_generation&&actor->person_data.size()>62)
            if(!world_.set_person_data(actor->id,62,job_->failure_code)){diagnostic_="source RouteResult write failed";return false;}
    }
    if(!result&&!job_->failure_tree_invoked)
        if(const auto receipt=outcome_receipt(*job_,result,reason,RouteOutcomeStage::failure_decision))emit_outcome(*receipt);
    if(!result&&job_->call_failure_trees&&!job_->failure_tree_invoked) {
        const auto pending=vm_->pending();
        if(pending&&pending->ticket==job_->ticket) {
            const auto* actor=world_.find(job_->actor);
            if(actor&&actor->generation==job_->actor_generation)
                world_.set_static_footprint(job_->actor,true);
            VmCall failure{routine_key(398),pending->context,
                           {job_->failure_code,job_->failure_blocker,0,0}};
            if(vm_->call_pending(job_->ticket,failure)) {
                job_->failure_pending=false;
                job_->failure_tree_invoked=true;
                job_->external_frame_purpose.reset();job_->external_frame_callee_generation=0;
                job_->failure_reason=reason;
                job_->external_frame_active=true;
                job_->external_frame_owner=failure.context.code_owner;
                job_->external_frame_call=std::move(failure);if(job_->external_frame_call)world_.bind_context_live(job_->external_frame_call->context);
                job_->frame.state=job_->unplaced_room_failure?RouteFrameState::failed:RouteFrameState::room_portal;
                return true;
            }
            diagnostic_="required source failure tree398 could not be invoked: "+vm_->diagnostic();
            return false;
        }
        diagnostic_="required source failure tree398 has no matching owning pending frame";
        return false;
    }
    if(suspended_jobs_.empty()&&!job_->look_towards&&source_sparse_services_.end_locomotion&&
       vm_&&vm_->pending()&&vm_->pending()->ticket==job_->ticket&&
       vm_->run_generation()==job_->run_generation&&!vm_->stack().empty()){
        const auto* actor=world_.find(job_->actor);
        if(actor&&actor->generation==job_->actor_generation){
            SourceSparseMotionContext context;context.vm=vm_;context.frame=vm_->stack().back();
            context.dispatch={vm_,job_->ticket,false};context.actor=job_->actor;context.actor_generation=job_->actor_generation;
            context.run_generation=job_->run_generation;context.actor_unplaced=!actor->position;
            context.position=actor->position.value_or(EntityPosition{-32768,-32768,1,0});context.root_route=true;
            const auto profile=job_->source_sparse?job_->source_sparse->profile:WalkMotionProfile{};
            const auto ticket=job_->ticket;std::string error;
            if(!source_sparse_services_.end_locomotion(context,profile,&error)||!job_||job_->ticket!=ticket||
               vm_->run_generation()!=context.run_generation){
                diagnostic_=error.empty()?"source root route PreExit animation cleanup failed":error;return false;
            }
        }
    }
    auto terminal=outcome_receipt(*job_,result,reason,RouteOutcomeStage::completion);
    end_room_context(*job_);
    const auto ticket=job_->ticket;
    const auto completed_actor=job_->actor,completed_target=job_->target;
    const auto completed_actor_generation=job_->actor_generation,completed_target_generation=job_->target_generation;
    const auto completed_run=job_->run_generation;
    if(suspended_jobs_.empty()) {
        const auto* actor=world_.find(job_->actor);
        if(actor && actor->generation==job_->actor_generation)
            world_.set_static_footprint(job_->actor,true);
        job_.reset();
        last_tick_.reset();
    } else {
        const bool sparse_child=job_->source_sparse.has_value();
        job_.reset();
        job_=std::move(suspended_jobs_.back());
        suspended_jobs_.pop_back();
        const auto* actor=world_.find(job_->actor);
        if(!sparse_child&&actor && actor->generation==job_->actor_generation)
            world_.set_static_footprint(job_->actor,!job_->walking_started);
    }
    diagnostic_=reason;
    const bool accepted=vm_->complete(ticket,result);
    if(!accepted){
        diagnostic_="route VM completion rejected ticket="+std::to_string(ticket)+" run="+std::to_string(completed_run)+
            " actor="+std::to_string(completed_actor)+"/"+std::to_string(completed_actor_generation)+
            " target="+std::to_string(completed_target)+"/"+std::to_string(completed_target_generation)+
            " result="+std::to_string(result)+" vm_run="+std::to_string(vm_->run_generation())+
            " vm_status="+std::to_string(static_cast<unsigned>(vm_->status()))+
            " pending_ticket="+(vm_->pending()?std::to_string(vm_->pending()->ticket):"none");
        if(job_)diagnostic_+=" parent_ticket="+std::to_string(job_->ticket)+" parent_run="+std::to_string(job_->run_generation)+
            " parent_actor="+std::to_string(job_->actor)+"/"+std::to_string(job_->actor_generation)+
            " parent_target="+std::to_string(job_->target)+"/"+std::to_string(job_->target_generation)+
            " parent_state="+std::to_string(static_cast<unsigned>(job_->frame.state));
        if(!reason.empty())diagnostic_+=" reason="+reason;
    }
    if(terminal){terminal->completion_attempted=true;terminal->completion_accepted=accepted;emit_outcome(*terminal);}
    return accepted;
}
bool Routing::interrupt() {
    if(executing_||!job_||job_->failure_pending||job_->failure_tree_invoked)return false;
    route_mutated();
    if(!suspended_jobs_.empty()){
        const auto parent=std::find_if(suspended_jobs_.rbegin(),suspended_jobs_.rend(),
            [](const Job& candidate){return candidate.call_failure_trees&&!candidate.failure_tree_invoked;});
        if(parent==suspended_jobs_.rend())return false;
        parent->interrupt_pending=true;
        job_->interrupt_pending=true;
        return true;
    }
    if(!job_->call_failure_trees)return false;
    // Simitone names fail code 4 Interrupted. Keep this isolated until the
    // original PC executable's cancellation code is verified independently.
    if(job_->external_frame_active||parked_for_immediate()){
        job_->interrupt_pending=true;
        return true;
    }
    job_->failure_code=4;
    return finish(false,"route interrupted by player");
}
std::vector<WorldEntityReference> Routing::strong_entity_references() const {
    std::vector<WorldEntityReference> refs;
    const auto context=[&](const VmContext& value){for(const auto role:{VmEntityRole::caller,VmEntityRole::callee,VmEntityRole::stack_object}){
        const auto ref=vm_context_reference(value,role);if(ref.id>0&&ref.generation)refs.push_back(ref);
    }};
    const auto job=[&](const Job& value){
        refs.push_back({value.actor,value.actor_generation});refs.push_back({value.target,value.target_generation});
        if(value.replay_frame)context(value.replay_frame->context);
        if(value.external_frame_call)context(value.external_frame_call->context);
        for(const auto& link:value.carried_target){refs.push_back({link.child.id,link.child.generation});refs.push_back({link.parent.id,link.parent.generation});}
    };
    if(job_)job(*job_);for(const auto& value:suspended_jobs_)job(value);
    if(source_reset_deferred_)job(*source_reset_deferred_);
    return refs;
}
bool Routing::discard_for_source_reset(){
    const bool authorized=vm_&&vm_->source_reset_lifecycle_active();
    if(source_reset_detach_failed_||((executing_||(vm_&&vm_->dispatch_active()))&&!authorized))return false;
    if(source_reset_retired_)return authorized;
    if(source_reset_operation_depth_)source_reset_retired_=true;
    struct Guard {bool& flag;bool previous;explicit Guard(bool& value):flag(value),previous(value){flag=true;}~Guard(){flag=previous;}} guard(executing_);
    // Release the real room owner before losing its ticket. Do not complete
    // any old VM pending instruction or erase its action queue.
    const auto release=[&](Job& job){
        end_room_context(job);
        const auto* actor=world_.find(job.actor);
        if(actor&&actor->generation==job.actor_generation&&!job.look_towards&&
           !world_.set_static_footprint(job.actor,true))
            throw std::runtime_error("source reset route footprint release failed");
    };
    try{
        if(job_)release(*job_);
        for(auto& parent:suspended_jobs_)release(parent);
        if(source_reset_deferred_)release(*source_reset_deferred_);
    }catch(...){
        // A throwing host callback may have partially released ownership.
        // Never turn a later retry into a fabricated successful detach.
        source_reset_detach_failed_=true;diagnostic_="source reset route owner release failed";return false;
    }
    route_mutated();last_tick_.reset();
    if(!source_reset_operation_depth_)clear_source_reset_storage();
    diagnostic_.clear();return true;
}
void Routing::cancel_chain(const std::string& reason) {
    if(job_) {
        end_room_context(*job_);
        const auto* actor=world_.find(job_->actor);
        if(actor&&actor->generation==job_->actor_generation&&!job_->look_towards)
            world_.set_static_footprint(job_->actor,true);
    }
    for(auto& parent:suspended_jobs_) {
        end_room_context(parent);
        const auto* actor=world_.find(parent.actor);
        if(actor&&actor->generation==parent.actor_generation&&!parent.look_towards)
            world_.set_static_footprint(parent.actor,true);
    }
    job_.reset();suspended_jobs_.clear();parked_immediate_routes_.clear();diagnostic_=reason;last_tick_.reset();
}
void invalidate_session_room_routes(RouteStackSnapshot& snapshot) noexcept {
    for(auto& job:snapshot.jobs)job.room_route_invalid=true;
}
bool Routing::invalidate_room_routes(){
    if(executing_||!vm_||vm_->dispatch_active())return false;
    for(auto& job:suspended_jobs_)job.room_route_invalid=true;
    if(job_)job_->room_route_invalid=true;
    route_mutated();return true;
}
std::optional<bool> Routing::process_room_route_invalidation(){
    if(!job_||!job_->room_route_invalid)return false;
    const auto state=job_->frame.state;
    if(state==RouteFrameState::begin_turn||state==RouteFrameState::end_turn||
       state==RouteFrameState::failed||state==RouteFrameState::turn_only)return false;
    const auto reject=[&](const char* why)->std::optional<bool>{diagnostic_=why;return {};};
    if(!vm_||!vm_->pending()||vm_->pending()->ticket!=job_->ticket||vm_->status()!=VmStatus::waiting||vm_->stack().empty())
        return reject("architecture route event lost its actual pending frame");
    const SlotRecord* source_slot=job_->source_choices?&job_->source_choices->slot:job_->source_range?&job_->source_range->slot:
        job_->source_circle?&job_->source_circle->slot:job_->source_sparse&&job_->source_sparse->choices?&job_->source_sparse->choices->slot:nullptr;
    const bool ignore_rooms=source_slot&&(source_slot->flags&2048U)!=0;
    if(!room_route_invalidation_||(!source_sparse_services_.room_route&&!ignore_rooms))return reject("architecture room-route event providers are unavailable");
    auto* actor=world_.find(job_->actor);const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!target||!actor->position||!target->position||actor->generation!=job_->actor_generation||target->generation!=job_->target_generation)
        return reject("architecture route event participants are stale");
    const auto frame=vm_->stack().back();const auto pending=*vm_->pending();const auto position=*actor->position,target_position=*target->position;
    const auto direction=actor->radian_direction;const auto attachment=actor->attachment;
    SourceSparseMotionContext context;context.vm=vm_;context.frame=frame;context.dispatch={vm_,job_->ticket,false};
    context.actor=job_->actor;context.actor_generation=job_->actor_generation;context.run_generation=job_->run_generation;
    context.position=position;context.root_route=suspended_jobs_.empty();context.room_context_open=false;
    std::optional<SourceRoomRoutePlan> plan;
    try{
        if(!room_route_invalidation_(job_->ticket))return reject("architecture ignored-room reset is unavailable");
        job_->room_context_open=false;
        plan=ignore_rooms?std::optional<SourceRoomRoutePlan>{{true,{}}}:source_sparse_services_.room_route(context,target_position);
    }catch(...){return reject("architecture room-route event provider threw");}
    actor=world_.find(job_->actor);target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!target||actor->generation!=job_->actor_generation||target->generation!=job_->target_generation||!actor->position||!target->position||
       !(*actor->position==position)||!(*target->position==target_position)||actor->radian_direction!=direction||
       !placement_attachment_equal(actor->attachment,attachment)||!vm_->pending()||!route_pending_equal(*vm_->pending(),pending)||
       vm_->stack().empty()||!route_frame_equal(vm_->stack().back(),frame))return reject("architecture room replanning changed the actual source");
    if(!plan||plan->portals.size()>max_portal_retries_||(!plan->found&&!plan->portals.empty()))return reject("architecture room graph is unknown or malformed");
    if(plan->found){
        if(!room_pair_)return reject("architecture room-pair authority is unavailable");
        const auto rooms=room_pair_(position,target_position);if(!rooms)return reject("architecture current room identity is unknown");
        if(plan->portals.empty()&&rooms->first!=rooms->second){
            if(!ignore_rooms)return reject("architecture graph omitted required room portals");
        }
        for(std::size_t n=0;n<plan->portals.size();++n){const auto&e=plan->portals[n];const auto*p=world_.find(e.portal_id);
            if(!p||!p->position||!(*p->position==e.position)||e.source_room==e.target_room||
               (n&&plan->portals[n-1].target_room!=e.source_room)||(!n&&e.source_room!=rooms->first)||
               (n+1==plan->portals.size()&&e.target_room!=rooms->second))return reject("architecture room portal identity/sequence is stale");
        }
    }
    const bool prior_room_replan=job_->room_route_replan_active;
    job_->room_route_invalid=false;
    // DoRoomRoute keeps CurRoute, CurrentPath, chair attempt, turn tweak and
    // counters. Only the source WalkTo and ignored-room set are reset.
    if(job_->source_sparse){job_->source_sparse->walk_to_pending=true;job_->source_sparse->walk_to_purpose=SourceWalkToPurpose::architecture_invalidation;
        job_->source_sparse->fallback_room_route=plan->portals;}
    if(!plan->found){
        job_->failure_code=2;job_->failure_blocker=0;
        const auto alternative=job_->source_choices?advance_source_slot_choice():job_->source_sparse?source_sparse_attempt_alternative():std::optional<bool>{false};
        if(!alternative)return {};
        if(*alternative)return true;
        return finish(false,"architecture change left no authored room route");
    }
    if(!plan->portals.empty()){
        job_->room_route_replan_active=true;job_->frame.state=RouteFrameState::initial;
        // Only a NEW room traversal supersedes the just-acknowledged child.
        // Same-room invalidation must still dispatch its original EP27/shoo
        // return branch below. The portal has a different source purpose.
        job_->external_frame_sitting=false;job_->external_frame_purpose.reset();
        job_->external_frame_callee_generation=0;
        return advance_invalidated_room_route();
    }
    if(prior_room_replan){
        const auto resumed=resume_invalidated_walk();if(!resumed)return {};
        job_->room_route_replan_active=false;
        if(!*resumed){job_->failure_code=3;return finish(false,"architecture replacement walk has no path");}
        return true;
    }
    return false;
}
std::optional<bool> Routing::resume_invalidated_walk(){
    const auto reject=[&](const std::string& why)->std::optional<bool>{
        diagnostic_="architecture walk resume "+why+" last_tick="+(last_tick_?std::to_string(*last_tick_):"none");
        if(job_)diagnostic_+=" actor="+std::to_string(job_->actor)+"/"+std::to_string(job_->actor_generation)+
            " target="+std::to_string(job_->target)+"/"+std::to_string(job_->target_generation)+
            " state="+std::to_string(static_cast<unsigned>(job_->frame.state));
        return {};
    };
    if(!job_||!vm_)return reject("missing job or VM");
    auto* actor=world_.find(job_->actor);const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!target||!actor->position||!target->position)return reject(
        "participant unavailable actor_live="+std::to_string(bool(actor))+" target_live="+std::to_string(bool(target))+
        " actor_position="+std::to_string(actor&&actor->position.has_value())+" target_position="+std::to_string(target&&target->position.has_value()));
    job_->expected=*actor->position;job_->expected_radian_direction=actor->radian_direction;
    job_->frame.state=RouteFrameState::initial;
    if(job_->source_choices){
        if(job_->source_choices->selected.chair&&!job_->source_choices->chair_attempted){job_->path={{actor->position->x,actor->position->y}};job_->next=1;return true;}
        job_->path={{actor->position->x,actor->position->y}};job_->next=1;
        if(!actor->person_data.empty()&&actor->person_data[0]==1){job_->source_motion.reset();job_->source_sparse.reset();return true;}
        return start_source_selected_walk();
    }
    if(job_->source_sparse){
        const auto frame=vm_->stack().back();const auto instruction=vm_->pending()->instruction.instruction;
        WalkMotionProfile profile;std::vector<LiveAttemptWalkIdentity> considered;
        const auto plan=source_sparse_plan(frame,instruction,{vm_,job_->ticket,false},job_->actor,job_->actor_generation,job_->target,job_->target_generation,
            *actor->position,*target->position,job_->path.back(),job_->next,job_->face_anywhere?std::nullopt:job_->arrival_radian_direction,
            job_->source_sparse->avatars_to_consider,profile,considered);
        if(!plan)return {};
        if(plan->state==SourceWalkPlanState::no_path)return false;
        if(plan->state!=SourceWalkPlanState::ready&&plan->state!=SourceWalkPlanState::turn_only){diagnostic_="architecture AttemptWalk exceeded its work bound";return {};}
        auto&s=*job_->source_sparse;s.fallback_room_route.clear();s.walk_to_pending=false;s.walk_to_purpose=SourceWalkToPurpose::ranked_alternative;
        s.room=plan->room;s.profile=profile;s.lifecycle=0;s.turn.reset();
        job_->walking_started=!actor->static_footprint;
        if(plan->state==SourceWalkPlanState::turn_only){job_->frame.state=RouteFrameState::turn_only;return true;}
        auto motion=std::make_unique<WalkMotion>();
        auto*parent=suspended_jobs_.empty()?nullptr:&suspended_jobs_.back().frame;const auto&effective=parent?*parent:job_->frame;
        const bool hard=effective.state!=RouteFrameState::room_portal||effective.portal_turns==0;
        if(motion->initialize(plan->segments,{{actor->position->x,actor->position->y},actor->position->level,actor->radian_direction},profile,false,hard?0:8)!=WalkMotionStatus::success)return {};
        const auto snapshot=motion->snapshot();if(!snapshot.snapshot)return {};s.motion=*snapshot.snapshot;s.hard_start=hard;
        const auto turn=source_turn_selection(actor->radian_direction,static_cast<float>(s.motion.target_direction));
        if(hard&&turn.index){SourceLookTowardsRouteSnapshot t;t.start_radian_direction=actor->radian_direction;t.target_radian_direction=static_cast<float>(s.motion.target_direction);
            t.in_pool=profile.in_pool;t.walk_style=profile.walk_style;t.animation_index=turn.index;t.turn_tweak=turn.residual;s.turn=t;s.lifecycle=1;}
        consume_route_portal_turn(job_->frame,parent);job_->source_motion=std::move(motion);
        job_->path={{actor->position->x,actor->position->y},job_->path.back()};job_->next=1;
        job_->frame.state=s.lifecycle==1?RouteFrameState::begin_turn:RouteFrameState::walking;return true;
    }
    const auto path=find_route(grid(),{actor->position->x,actor->position->y},{job_->path.back()},limits_);
    if(path.status==RouteStatus::no_path)return false;
    if(path.status!=RouteStatus::found)return {};
    job_->path=path.points;job_->next=1;return true;
}
bool Routing::advance_invalidated_room_route(const std::optional<VmCall>& previous_call,const std::optional<bool>& previous_result){
    const auto reject=[&](const std::string& why){
        diagnostic_="architecture room advance "+why+" last_tick="+(last_tick_?std::to_string(*last_tick_):"none");
        if(job_)diagnostic_+=" actor="+std::to_string(job_->actor)+"/"+std::to_string(job_->actor_generation)+
            " target="+std::to_string(job_->target)+"/"+std::to_string(job_->target_generation)+
            " state="+std::to_string(static_cast<unsigned>(job_->frame.state));
        return false;
    };
    if(!job_||!job_->room_route_replan_active||!room_portal_step_||!vm_||!vm_->pending())return reject("missing replan owner, callback or pending VM");
    const auto*actor=world_.find(job_->actor);const auto*target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!target||!actor->position||!target->position)return reject(
        "participant unavailable actor_live="+std::to_string(bool(actor))+" target_live="+std::to_string(bool(target))+
        " actor_position="+std::to_string(actor&&actor->position.has_value())+" target_position="+std::to_string(target&&target->position.has_value()));
    const auto position=*actor->position,target_position=*target->position;const auto radians=actor->radian_direction;
    const auto pending=*vm_->pending();const auto frame=vm_->stack().back();const auto attachment=actor->attachment;
    const auto plan=room_portal_step_(job_->ticket,position,target_position,previous_call,previous_result,job_->portal_steps);
    actor=world_.find(job_->actor);target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor||!target||actor->generation!=job_->actor_generation||target->generation!=job_->target_generation||!actor->position||!target->position||
       !(*actor->position==position)||!(*target->position==target_position)||actor->radian_direction!=radians||!placement_attachment_equal(actor->attachment,attachment)||
       !vm_->pending()||!route_pending_equal(*vm_->pending(),pending)||vm_->stack().empty()||!route_frame_equal(vm_->stack().back(),frame)){
        diagnostic_="architecture room portal callback changed its participants or pending frame";return false;}
    job_->room_context_open=true;
    if(plan.status==RoutePortalStepStatus::call_ready){
        if(!plan.call||plan.call->return_mode!=VmCallReturnMode::ordinary||job_->portal_steps>=max_portal_retries_||!vm_->call_pending(job_->ticket,*plan.call)){diagnostic_="architecture room portal call cannot be scheduled";return false;}
        ++job_->portal_steps;job_->external_frame_active=true;job_->external_frame_call=plan.call;if(job_->external_frame_call)world_.bind_context_live(job_->external_frame_call->context);job_->external_frame_owner=plan.call->context.code_owner;
        job_->expected=position;job_->expected_radian_direction=radians;job_->frame.state=RouteFrameState::room_portal;return true;
    }
    if(plan.status!=RoutePortalStepStatus::destination_reached||plan.call){diagnostic_=plan.diagnostic.empty()?"architecture room portal step unavailable":plan.diagnostic;return false;}
    const auto rooms=room_pair_?room_pair_(position,target_position):std::nullopt;
    if(!rooms||rooms->first!=rooms->second){diagnostic_="architecture portal arrival did not reach target room";return false;}
    const auto resumed=resume_invalidated_walk();if(!resumed)return false;
    job_->room_route_replan_active=false;
    if(!*resumed){job_->failure_code=3;return finish(false,"architecture replacement walk has no path");}
    return true;
}
bool Routing::tick(std::uint64_t now,const std::function<void(const RouteMovement&)>& moved) {
    SourceResetOperation source_operation(*this);
    try {
    route_mutated();
    if(executing_) throw std::logic_error("routing ticks must not reenter");
    struct Guard {bool& flag;bool previous;explicit Guard(bool& value):flag(value),previous(value){flag=true;}~Guard(){flag=previous;}} guard(executing_);
    if(last_tick_ && (now<=*last_tick_ || now-*last_tick_!=1)) {
        diagnostic_="routing scheduler cadence rejected now="+std::to_string(now)+" last_tick="+std::to_string(*last_tick_);
        if(job_)diagnostic_+=" actor="+std::to_string(job_->actor)+"/"+std::to_string(job_->actor_generation)+
            " target="+std::to_string(job_->target)+"/"+std::to_string(job_->target_generation)+
            " state="+std::to_string(static_cast<unsigned>(job_->frame.state));
        return false;
    }
    last_tick_=now;
    if(!vm_ || !job_) {
        diagnostic_="routing tick missing VM or job now="+std::to_string(now);return false;
    }
    if(parked_for_immediate())return true;
    if(job_->external_frame_active) {
        if(vm_->run_generation()!=job_->run_generation) {
            cancel_chain("route canceled by VM restart during external frame");return false;
        }
        if(const auto returned=vm_->external_frame_return()) {
            const auto result=*returned;
            if(result.ticket!=job_->ticket || result.run_generation!=job_->run_generation ||
               result.code_owner!=job_->external_frame_owner ||
               !vm_->resume_external_frame(job_->ticket)) {
                cancel_chain("external route frame returned with mismatched ownership");return false;
            }
            // The VM parent becomes the source top routing frame only after
            // its authenticated child return is acknowledged. Waiting child
            // BHAV ticks never reach this prefix.
            if(!source_route_tick_prefix())return false;
            job_->external_frame_active=false;
            if(job_->failure_tree_invoked)
                return finish(false,job_->failure_reason);
            if(job_->room_route_replan_active){
                auto* current=world_.find(job_->actor);if(!current||!current->position){
                    diagnostic_="architecture external return lost actor pose now="+std::to_string(now)+
                        " actor="+std::to_string(job_->actor)+"/"+std::to_string(job_->actor_generation)+
                        " target="+std::to_string(job_->target)+"/"+std::to_string(job_->target_generation)+
                        " state="+std::to_string(static_cast<unsigned>(job_->frame.state));return false;
                }
                job_->expected=*current->position;job_->expected_radian_direction=current->radian_direction;
                if(job_->room_route_invalid){const auto event=process_room_route_invalidation();if(!event)return false;if(*event)return true;
                    return advance_invalidated_room_route();}
                return advance_invalidated_room_route(job_->external_frame_call,result.result);
            }
            if(job_->room_route_invalid){const auto event=process_room_route_invalidation();if(!event)return false;if(*event)return true;}
            if(job_->interrupt_pending){
                job_->interrupt_pending=false;
                job_->failure_code=4;
                return finish(false,"route interrupted by player");
            }
            if(job_->external_frame_purpose==RouteExternalFramePurpose::standing){
                auto* standing=world_.find(job_->actor);
                if(!standing||standing->generation!=job_->actor_generation||!standing->position||standing->person_data.empty()){
                    diagnostic_="source EP27 return lost its real actor pose";return false;
                }
                job_->expected=*standing->position;job_->expected_radian_direction=standing->radian_direction;
                job_->path={{standing->position->x,standing->position->y}};job_->next=1;
                job_->external_frame_purpose.reset();job_->external_frame_callee_generation=0;
                job_->external_frame_call.reset();job_->external_frame_owner=0;job_->frame.state=RouteFrameState::initial;
                if(result.result){
                    if(standing->person_data[0]==1&&!world_.set_person_data(standing->id,0,0))return false;
                    if(!source_route_tick_prefix())return false;
                    return tick_source_sparse(now,moved);
                }
                VmFrame actor_frame;actor_frame.context.caller=job_->actor;
                const auto blocker=world_.read(actor_frame,3,54);
                if(!blocker){diagnostic_="source CantStand PrimitiveResultID is unavailable";return false;}
                job_->failure_code=6;job_->failure_blocker=(*blocker&&world_.find(*blocker))?*blocker:0;
                const auto alternative=advance_source_slot_choice();if(!alternative)return false;
                if(*alternative){if(!source_route_tick_prefix())return false;return tick_source_sparse(now,moved);}
                return finish(false,"source EP27 returned false and exhausted source choices");
            }
            if(job_->external_frame_purpose==RouteExternalFramePurpose::shoo) {
                auto* actor=world_.find(job_->actor);const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
                if(!job_->source_sparse||job_->frame.state!=RouteFrameState::shooed||
                   !actor||!target||actor->generation!=job_->actor_generation||target->generation!=job_->target_generation||
                   !actor->position||actor->attachment) {
                    diagnostic_="source shoo return changed its original route participants";return false;
                }
                // The source ignores LastStackExitCode in SHOOED. Its child
                // may have moved the avatar; keep the original CurRoute goal.
                job_->expected=*actor->position;job_->expected_radian_direction=actor->radian_direction;
                job_->external_frame_purpose.reset();job_->external_frame_callee_generation=0;
                job_->external_frame_call.reset();job_->external_frame_owner=0;
                return tick_source_sparse(now,moved);
            }
            if(job_->external_frame_sitting) {
                job_->external_frame_sitting=false;
                if(result.result){if(job_->source_choices&&!world_.set_person_data(job_->actor,37,job_->entry_flags))return false;return finish(true,{});}
                if(job_->source_choices){
                    auto* current=world_.find(job_->actor);
                    if(!current||!current->position||current->attachment){diagnostic_="failed chair returned without a routable actor";return false;}
                    job_->expected=*current->position;job_->expected_radian_direction=current->radian_direction;
                    job_->source_choices->chair_attempted=false;
                    const auto alternative=advance_source_slot_choice();if(!alternative)return false;
                    if(*alternative)return true;
                    return finish(false,"source chair EP26 failed and choices are exhausted");
                }
                auto* actor=world_.find(job_->actor);
                const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
                if(!actor||!target||actor->generation!=job_->actor_generation||
                   target->generation!=job_->target_generation||!actor->position||!target->position||
                   actor->attachment||!grid().contains({actor->position->x,actor->position->y})||
                   !grid().walkable({actor->position->x,actor->position->y})||
                   (job_->target!=job_->actor&&!target_checkpoint_matches(world_,job_->actor,job_->actor_generation,job_->target,job_->target_generation,job_->carried_target,job_->target_position,(captured_source_goals(*job_)||job_->deferred_room_route))))
                    return finish(false,"failed EntryPoint26 changed or invalidated route participants");
                if(route_grid_refresh_&&!route_grid_refresh_(*actor->position))
                    return finish(false,"failed EntryPoint26 could not refresh the route grid");
                job_->failure_code=5; // Simitone VMRouteFailCode.CantSit.
                if(try_next_sitting_goal())return true;
                return finish(false,"EntryPoint26 returned false and no alternate SLOT goal remains");
            }
            job_->frame.state=RouteFrameState::room_portal;
            const auto* actor=world_.find(job_->actor);
            const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
            if(!actor||!target||actor->generation!=job_->actor_generation||
               target->generation!=job_->target_generation||!actor->position||!target->position||
               (job_->target!=job_->actor&&!target_checkpoint_matches(world_,job_->actor,job_->actor_generation,job_->target,job_->target_generation,job_->carried_target,job_->target_position,(captured_source_goals(*job_)||job_->deferred_room_route)))||
               actor->attachment)
                return finish(false,"portal frame changed or invalidated route participants");
            const auto refresh_after_external_move=[&](bool required)->bool {
                const auto actor_position=*actor->position;
                const auto target_position=*target->position;
                const auto actor_direction=actor->radian_direction;
                if(!route_grid_refresh_)return !required;
                if(!route_grid_refresh_(actor_position))return false;
                actor=world_.find(job_->actor);
                target=world_.resolve_reference({job_->target,job_->target_generation});
                return actor&&target&&actor->generation==job_->actor_generation&&
                    target->generation==job_->target_generation&&actor->position&&target->position&&
                    *actor->position==actor_position&&*target->position==target_position&&
                    actor->radian_direction==actor_direction&&!actor->attachment;
            };
            if(job_->deferred_room_route) {
                const auto previous_call=job_->external_frame_call;
                if(!refresh_after_external_move(true))
                    return finish(false,"Portal15 returned but the active room grid could not be refreshed");
                job_->expected=*actor->position;
                job_->expected_radian_direction=actor->radian_direction;
                return advance_room_route(previous_call,result.result);
            }
            if(!result.result) {
                if(!portal_retry_||!job_->external_frame_call||
                   job_->portal_retries>=max_portal_retries_)
                    return finish(false,"external route frame returned false; no bounded portal retry is available");
                if(!(*actor->position==job_->expected)) {
                    if(!refresh_after_external_move(true))
                        return finish(false,"failed portal frame moved avatar but host route-grid refresh is unavailable or failed");
                    job_->expected=*actor->position;
                    job_->expected_radian_direction=actor->radian_direction;
                    job_->portal_replan_required=true;
                }
                const auto failed_call=*job_->external_frame_call;
                const auto retry_index=job_->portal_retries+1;
                const auto actor_position=*actor->position;
                const auto target_position=*target->position;
                const auto next=[&] {
                    struct CallbackGuard {bool& flag;bool previous;explicit CallbackGuard(bool& value):flag(value),previous(value){flag=true;}~CallbackGuard(){flag=previous;}} callback_guard(executing_);
                    return portal_retry_(failed_call,actor_position,target_position,retry_index);
                }();
                const auto* current_actor=world_.find(job_->actor);
                const auto* current_target=world_.resolve_reference({job_->target,job_->target_generation});
                if(!current_actor||!current_target||current_actor->generation!=job_->actor_generation||
                   current_target->generation!=job_->target_generation||!current_actor->position||!current_target->position||
                   !(*current_actor->position==job_->expected)||
                   (job_->target!=job_->actor&&!target_checkpoint_matches(world_,job_->actor,job_->actor_generation,job_->target,job_->target_generation,job_->carried_target,job_->target_position,(captured_source_goals(*job_)||job_->deferred_room_route)))||
                   current_actor->attachment)
                    return finish(false,"portal retry provider changed or invalidated route participants");
                if(!next) return finish(false,"external route frame returned false; no alternate room route remains");
                if(next->return_mode!=VmCallReturnMode::ordinary||!vm_->call_pending(job_->ticket,*next))
                    return finish(false,"portal retry provider returned an invalid or unschedulable VM call");
                job_->portal_retries=retry_index;
                job_->external_frame_owner=next->context.code_owner;
                job_->external_frame_call=*next;if(job_->external_frame_call)world_.bind_context_live(job_->external_frame_call->context);
                job_->external_frame_active=true;
                job_->frame.state=RouteFrameState::room_portal;
                return true;
            }
            if(!(*actor->position==job_->expected)||job_->portal_replan_required) {
                if(!(*actor->position==job_->expected)&&!refresh_after_external_move(false))
                    return finish(false,"portal frame moved avatar but host route-grid refresh failed");
                const auto goal=job_->path.back();
                const auto replanned=find_route(grid(),{actor->position->x,actor->position->y},{goal},limits_);
                if(replanned.status!=RouteStatus::found)
                    return finish(false,"portal frame moved avatar but parent route could not be replanned");
                job_->path=replanned.points;job_->next=1;job_->expected=*actor->position;
                job_->expected_radian_direction=actor->radian_direction;
                job_->portal_replan_required=false;
            }
            // A portal BHAV resumes the suspended Route45 service only at this
            // scheduler boundary. Movement begins on a later tick.
            return true;
        }
        const auto& pending=vm_->pending();
        if((vm_->status()==VmStatus::waiting && pending && pending->ticket!=job_->ticket) ||
           vm_->status()==VmStatus::running || vm_->status()==VmStatus::budget_exhausted)
            return true; // Child BHAV is still executing or waiting on its own service.
        cancel_chain("external route frame ended without a matching return");return false;
    }
    if(vm_->run_generation()!=job_->run_generation||!vm_->pending()||
       vm_->status()!=VmStatus::waiting||vm_->pending()->ticket!=job_->ticket) {
        cancel_chain("route canceled by VM restart or ticket mismatch");return false;
    }
    if(job_->unplaced_room_failure&&job_->failure_pending&&job_->call_failure_trees)
        return finish(false,job_->failure_reason);
    if(!source_route_tick_prefix())return false;
    if(job_->failure_pending)
        return finish(false,job_->failure_reason);
    if(job_->interrupt_pending){
        job_->interrupt_pending=false;
        if(job_->call_failure_trees)job_->failure_code=4;
        return finish(false,"route interrupted by player");
    }
    const auto* actor=world_.find(job_->actor);const auto* target=world_.resolve_reference({job_->target,job_->target_generation});
    if(!actor || !target) return finish(false,"route canceled by missing participant");
    if(actor->generation!=job_->actor_generation) return finish(false,"route canceled by actor generation change");
    if(target->generation!=job_->target_generation) return finish(false,"route canceled by target generation change");
    if(!job_->carried_target.empty()&&!route_carried_target_matches(world_,{job_->actor,job_->actor_generation},
        {job_->target,job_->target_generation},job_->carried_target,job_->target_position))
        return finish(false,"route canceled by carried target ancestry or pose change");
    if(!actor->position || (!job_->source_circle&&!job_->source_range&&!target->position)) return finish(false,"route canceled by missing participant position");
    if(!(*actor->position==job_->expected))
        return finish(false,"route canceled by actor position change: actor="+std::to_string(job_->actor));
    if(!job_->source_circle&&!job_->source_range&&target->id!=actor->id && !target_checkpoint_matches(world_,job_->actor,job_->actor_generation,job_->target,job_->target_generation,job_->carried_target,job_->target_position,(captured_source_goals(*job_)||job_->deferred_room_route)))
        return finish(false,"route canceled by target position change: target="+std::to_string(job_->target)+
            " expected="+std::to_string(job_->target_position.x)+","+std::to_string(job_->target_position.y)+
            " actual="+std::to_string(target->position->x)+","+std::to_string(target->position->y));
    if(actor->radian_direction!=job_->expected_radian_direction)
        return finish(false,"route canceled by continuous actor direction change");
    if(job_->room_route_invalid&&!job_->source_sparse){const auto event=process_room_route_invalidation();if(!event)return false;if(*event)return true;}
    if(job_->room_route_replan_active)return advance_invalidated_room_route();
    if(job_->look_towards)return tick_look_towards();
    if(job_->source_choices&&!job_->source_sparse)return tick_source_sparse(now,moved);
    if(job_->source_sparse)return tick_source_sparse(now,moved);
    if(actor->attachment&&!(job_->deferred_room_route&&!actor->person_data.empty()&&actor->person_data[0]==1))
        return finish(false,"route canceled because actor became attached");
    if(job_->deferred_room_route)return advance_room_route({},{});
    std::optional<RouteMovement> event;
    if(job_->next<job_->path.size()) {
        const auto point=job_->path[job_->next];const RoutePoint from{actor->position->x,actor->position->y};
        if(!grid().can_step(from,point)) {
            if(job_->source_circle||job_->source_range){
                const auto retried=try_next_circle_goal();
                if(!retried)return false;
                if(*retried)return true;
            }
            return finish(false,"next segment blocked during route");
        }
        auto position=*actor->position;position.x=point.x;position.y=point.y;position.direction=heading(from,point);
        if(set_position_execution_&&!position_publication_) {
            if(vm_->stack().empty()||!vm_->pending()) {
                diagnostic_="source route placement lost its pending VM frame";
                return false;
            }
            const auto& source_frame=vm_->stack().back();
            const auto& source_instruction=vm_->pending()->instruction.instruction;
            const VmDispatch source_dispatch{vm_,job_->ticket,false};
            if(!target->position) {
                diagnostic_="source route placement target has no live position";
                return false;
            }
            if(auto error=invoke_route_execution_callback(RouteExecutionPhase::set_position,
                source_frame,source_instruction,source_dispatch,actor->id,actor->generation,
                target->id,target->generation,*actor->position,*target->position,point,job_->next)) {
                diagnostic_=*error;
                return false;
            }
            actor=world_.find(job_->actor);target=world_.resolve_reference({job_->target,job_->target_generation});
            if(!actor||!target||actor->generation!=job_->actor_generation||
               target->generation!=job_->target_generation||!actor->position||!target->position||
               !(*actor->position==job_->expected)||
               actor->radian_direction!=job_->expected_radian_direction) {
                diagnostic_="source route placement participants changed after execution callback";
                return false;
            }
        }
        if(!job_->walking_started) {
            if(!world_.set_static_footprint(actor->id,false))
                return finish(false,"dynamic route footprint update rejected");
            job_->walking_started=true;
        }
        job_->frame.state=RouteFrameState::walking;
        if(position_publication_){
            std::string error;const auto placement=publish_route_position(position,point,job_->next,true,&error);
            if(!placement){diagnostic_=error;return false;}
            if(placement->status==SourceSparsePlacementStatus::unsupported){diagnostic_=placement->diagnostic.empty()?"route position lifecycle is unsupported":placement->diagnostic;return false;}
            if(placement->status!=SourceSparsePlacementStatus::success)return finish(false,"route position placement blocked");
        }else if(!world_.set_position(actor->id,position)) return finish(false,"position update rejected");
        job_->expected=position;
        job_->expected_radian_direction=world_.find(actor->id)->radian_direction;
        ++job_->next;
        event=RouteMovement{now,actor->id,position};
    }
    if(job_->next==job_->path.size()) {
        auto position=job_->expected;
        if(!job_->face_anywhere)position.direction=job_->facing;
        if(position_publication_){
            std::string error;const auto placement=publish_route_position(position,{position.x,position.y},job_->next,false,&error);
            if(!placement){diagnostic_=error;return false;}
            if(placement->status==SourceSparsePlacementStatus::unsupported){diagnostic_=placement->diagnostic.empty()?"route arrival position lifecycle is unsupported":placement->diagnostic;return false;}
            if(placement->status!=SourceSparsePlacementStatus::success)return finish(false,"route arrival placement blocked");
        }else if(!world_.set_position(job_->actor,position))return finish(false,"route arrival position update rejected");
        if(!world_.set_person_data(job_->actor,37,job_->entry_flags))
            return finish(false,"route arrival fields rejected");
        constexpr float pi_over_four=0.78539816339744830962f;
        if(!job_->face_anywhere&&!world_.set_radian_direction(job_->actor,
            job_->arrival_radian_direction.value_or(static_cast<float>(job_->facing)*pi_over_four)))
            return finish(false,"route arrival facing rejected");
        if(event) event->position=position;
        if(job_->selected_sitting_object) {
            job_->failure_code=5; // Simitone VMRouteFailCode.CantSit.
            std::optional<VmCall> call;
            if(sitting_action_) {
                try {call=sitting_action_(job_->actor,*job_->selected_sitting_object);}
                catch(const std::exception& error) {diagnostic_=std::string("EntryPoint26 planning failed: ")+error.what();}
            }
            if(call&&schedule_external_frame(*call,true)) {
                if(event&&moved)moved(*event);
                return true;
            }
            if(call&&diagnostic_.empty())diagnostic_=std::string("EntryPoint26 VM call could not be scheduled: ")+vm_->diagnostic();
            else if(!call&&diagnostic_.empty())diagnostic_="EntryPoint26 provider returned no VM call";
            if(try_next_sitting_goal()) {
                if(event&&moved)moved(*event);
                return true;
            }
            return finish(false,diagnostic_.empty()?
                "selected sitting goal has no schedulable EntryPoint26 or alternate SLOT goal":diagnostic_);
        }
        const auto accepted=finish(true,{});
        if(event && moved) moved(*event);
        return accepted;
    }
    if(event && moved) moved(*event);
    return true;
    }catch(const SourceCheckResetRetirement& retired){
        if(!source_reset_retired_||retired.source()!=vm_)throw;
        return true; // Reset consumed the old route; never complete its ticket.
    }
}
} // namespace odh
