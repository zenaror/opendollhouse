#include "odh/find_best_object_for_function.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <utility>

namespace odh {
namespace {
constexpr std::array<std::uint8_t,15> kEntry{{18,19,20,21,22,23,24,25,26,27,14,28,16,17,29}};
constexpr std::array<std::int16_t,15> kScoreWord{{31,32,33,36,-1,-1,37,38,-1,-1,61,39,64,65,15}};
constexpr std::array<std::uint8_t,4> kSurfaceEntries{{14,18,20,25}};

PrimitiveOutcome branch(bool result) {
    return {result?PrimitiveDisposition::goto_true:PrimitiveDisposition::goto_false,{}};
}
PrimitiveOutcome unsupported(std::string why) {
    return {PrimitiveDisposition::unsupported,std::move(why)};
}

std::uint16_t function_operand(const Instruction& instruction) noexcept {
    return static_cast<std::uint16_t>(instruction.operands[0] |
        (static_cast<std::uint16_t>(instruction.operands[1])<<8));
}

std::int16_t source_short(std::int32_t value) noexcept {
    const auto bits=static_cast<std::uint16_t>(value);
    std::int16_t result;std::memcpy(&result,&bits,sizeof(result));return result;
}
std::int8_t source_sbyte(std::int32_t value) noexcept {
    const auto bits=static_cast<std::uint8_t>(value);
    std::int8_t result;std::memcpy(&result,&bits,sizeof(result));return result;
}

bool active(const FindBestObjectForFunctionRequest& request) noexcept {
    return request.world&&request.source_vm&&request.source_frame&&request.source_instruction&&
        request.source_instruction->opcode==14&&request.source_dispatch.origin==request.source_vm&&
        request.source_vm->matches_active_query(
            *request.source_frame,*request.source_instruction,request.source_dispatch);
}

const Bhav* lookup(const FunctionalTreeResolvedCode& code) {
    if(code.status!=FunctionalTreeCodeStatus::resolved)return nullptr;
    const auto& program=code.key.scope==RoutineScope::global?code.global_program:code.owner_program;
    if(!program)return nullptr;
    const auto found=program->routines().find(code.key);
    return found==program->routines().end()?nullptr:&found->second;
}

bool valid_code(const FunctionalTreeResolvedCode& code,std::uint16_t id) {
    const auto expected=routine_key(id);
    return code.status==FunctionalTreeCodeStatus::resolved&&code.owner!=0&&
        code.owner_program&&code.global_program&&code.key.scope==expected.scope&&code.key.id==expected.id;
}

struct SourcePos { std::int16_t x,y; std::int8_t level; };
SourcePos narrow_position(const Entity* entity) noexcept {
    if(!entity||!entity->position)return {-32768,-32768,1};
    return {source_short(entity->position->x),source_short(entity->position->y),
        source_sbyte(entity->position->level)};
}
bool out_of_world(const Entity* entity) noexcept {
    if(!entity||!entity->position)return true;
    const auto p=narrow_position(entity);
    return p.x==-32768&&p.y==-32768&&p.level==1;
}

std::int32_t wrap_add(std::int32_t a,std::int32_t b) noexcept {
    const auto bits=static_cast<std::uint32_t>(a)+static_cast<std::uint32_t>(b);
    std::int32_t result;std::memcpy(&result,&bits,sizeof(result));return result;
}
std::int32_t wrap_mul(std::int32_t a,std::int32_t b) noexcept {
    const auto bits=static_cast<std::uint32_t>(a)*static_cast<std::uint32_t>(b);
    std::int32_t result;std::memcpy(&result,&bits,sizeof(result));return result;
}
std::int32_t wrap_sub(std::int32_t a,std::int32_t b) noexcept {
    const auto bits=static_cast<std::uint32_t>(a)-static_cast<std::uint32_t>(b);
    std::int32_t result;std::memcpy(&result,&bits,sizeof(result));return result;
}

std::int32_t source_distance_penalty(SourcePos candidate,SourcePos caller) noexcept {
    // LotTilePos operator- narrows each difference to short/sbyte before the
    // primitive's unchecked Int32 square/add expression.
    const auto dx=source_short(static_cast<std::int32_t>(candidate.x)-caller.x);
    const auto dy=source_short(static_cast<std::int32_t>(candidate.y)-caller.y);
    const auto dl=source_sbyte(static_cast<std::int32_t>(candidate.level)-caller.level);
    auto sum=wrap_mul(dx,dx);
    sum=wrap_add(sum,wrap_mul(dy,dy));
    sum=wrap_add(sum,wrap_mul(wrap_mul(dl,dl),900*256));
    std::int32_t root;
    if(sum<0) root=std::numeric_limits<std::int32_t>::min();
    else {
        const auto value=std::sqrt(static_cast<double>(sum));
        root=value>static_cast<double>(std::numeric_limits<std::int32_t>::max())
            ?std::numeric_limits<std::int32_t>::min():static_cast<std::int32_t>(value);
    }
    return root/3;
}

bool is_surface_entry(std::uint8_t entry) noexcept {
    for(auto value:kSurfaceEntries)if(value==entry)return true;
    return false;
}

std::optional<bool> run_condition(const FindBestObjectForFunctionRequest& request,
        const FunctionalTreeResolvedCode& code,std::int16_t target,std::uint64_t generation,std::string& diagnostic) {
    const auto fail=[&](std::string reason)->std::optional<bool>{diagnostic=std::move(reason);return {};};
    if(code.status==FunctionalTreeCodeStatus::target_resource_absent)return true;
    if(code.status==FunctionalTreeCodeStatus::unsupported)return fail("condition resource is unresolved");
    if(!valid_code(code,code.key.id))return fail("condition resource/key is invalid");
    const auto* bhav=lookup(code);
    if(!bhav)return true; // Exact known missing GetRoutineWithOwner skips the condition.
    if(bhav->instructions.empty())return false;
    if((bhav->version!=0x8000&&bhav->version!=0x8001&&
        (bhav->version!=0x8002||!bhav->metadata))||!request.check_instruction_budget)
        return fail("condition metadata or instruction budget unavailable");
    if((request.caller_register_owner&&request.caller_thread_absent_proven)||
       (!request.caller_register_owner&&!request.caller_thread_absent_proven))return fail("actual Caller.Thread ownership unavailable");
    try {
        const auto* candidate=request.world->find(target);
        if(!candidate||candidate->generation!=generation)return fail("candidate identity changed before condition");
        VmLimits limits; // Native configured default budget; source ctor5 is a List capacity.
        SharedRoutineOwners owners;owners.emplace(code.owner,code.owner_program);
        Vm check(code.global_program,ShareVmProgramTag{},code.memory,limits,VmPrimitive{},
            std::move(owners),ShareVmOwnerProgramsTag{});
        if(!check.set_owner_resolver(code.owner_resolver))return fail("condition resource context could not bind");
        if(!check.has_routine(code.key,code.owner))return fail("condition is absent from its installed bank");
        VmContext context{request.source_frame->context.caller,target,target,code.owner};
        if(!request.world->bind_context_live(context))return fail("condition context cannot bind candidate identities");
        if(request.source_frame->context.entity_references_bound&&
           !vm_context_copy_reference(context,VmEntityRole::caller,request.source_frame->context,VmEntityRole::caller))
            return fail("condition Caller pointer cannot propagate its exact source generation");
        if(!vm_context_set_reference(context,VmEntityRole::callee,{target,generation})||
           !vm_context_set_reference(context,VmEntityRole::stack_object,{target,generation}))
            return fail("condition candidate pointer cannot bind its captured generation");
        const std::vector<std::int16_t> args(4,0);
        if(check.start(code.key,context,args,true)!=VmStatus::running)return fail("condition start failed: "+check.diagnostic());
        const auto primitive=request.check_primitive_factory?request.check_primitive_factory(check.effective_memory()):request.check_primitive;
        const auto result=request.caller_register_owner
            ?check.run_synchronous_check_with_registers(*request.caller_register_owner,request.scheduler_running_now,request.check_instruction_budget,primitive)
            :check.run_synchronous_check(request.check_instruction_budget,primitive);
        if(!result.started||(result.status!=VmStatus::returned_true&&result.status!=VmStatus::returned_false))
            return fail("status="+std::string(vm_status_name(result.status))+" detail="+result.diagnostic.substr(0,512));
        return result.status==VmStatus::returned_true;
    }catch(const std::exception& error){return fail("exception="+std::string(error.what()).substr(0,512));}
    catch(...){return fail("unknown condition exception");}
}

} // namespace

PrimitiveOutcome find_best_object_for_function(const FindBestObjectForFunctionRequest& request) {
    if(!active(request))return unsupported("FindBestObjectForFunction requires the exact active opcode-14 dispatch");
    try {
    const auto function=function_operand(*request.source_instruction);
    if(function>=kEntry.size())return unsupported("FindBestObjectForFunction function index is outside the source mapping");
    if(!request.ordered_live_entities||!request.identities_still_match||!request.effective_lockout||
       !request.disabled||!request.score_value||!request.slot_zero_empty||!request.is_in_use||
       !request.resolve_condition)
        return unsupported("FindBestObjectForFunction lacks required live-world services");
    if(!request.identities_still_match())return unsupported("FindBestObjectForFunction source/inventory identity is stale");

    std::optional<std::vector<FindBestObjectIdentity>> inventory;
    try { inventory=request.ordered_live_entities(); }
    catch(...) { return unsupported("FindBestObjectForFunction entity inventory lookup failed"); }
    if(!inventory)return unsupported("FindBestObjectForFunction lacks a complete entity inventory");
    if(inventory->size()>4096)return unsupported("FindBestObjectForFunction entity inventory exceeds the native budget");
    std::set<std::int16_t> seen;
    std::int16_t previous=0;
    for(const auto& item:*inventory) {
        if(item.id<=previous||item.id<=0||!item.generation||!seen.insert(item.id).second)
            return unsupported("FindBestObjectForFunction entity inventory is not unique ascending source order");
        const auto* entity=request.world->find(item.id);
        if(!entity||entity->generation!=item.generation)
            return unsupported("FindBestObjectForFunction inventory identity is not live in World");
        previous=item.id;
    }

    std::int32_t best_score=std::numeric_limits<std::int32_t>::min();
    std::optional<FindBestObjectIdentity> best;
    const auto entry_index=kEntry[function];
    const auto score_index=kScoreWord[function];

    for(const auto& item:*inventory) {
        if(!request.identities_still_match())
            return unsupported("FindBestObjectForFunction entity/source identities changed during scan");
        const auto* entity=request.world->find(item.id);
        if(!entity||entity->generation!=item.generation)
            return unsupported("FindBestObjectForFunction candidate identity changed during scan");
        std::optional<std::int16_t> lockout;
        try {
            lockout=request.effective_lockout(item.id,item.generation);
        } catch(...) { return unsupported("FindBestObjectForFunction filter lookup failed"); }
        if(!lockout)return unsupported("FindBestObjectForFunction lockout evidence is unresolved for entity "+
            std::to_string(item.id)+" generation "+std::to_string(item.generation));
        if(*lockout>0)continue;
        if(entity->kind==EntityKind::object) {
            std::optional<std::uint32_t> disabled;
            try { disabled=request.disabled(item.id,item.generation); }
            catch(...) { return unsupported("FindBestObjectForFunction Disabled lookup failed"); }
            if(!disabled)return unsupported("FindBestObjectForFunction Disabled evidence is unresolved");
            if(*disabled>0)continue;
        }
        if(out_of_world(entity))continue;
        if(!entity->entry_points)
            return unsupported("FindBestObjectForFunction candidate entry-point table is unproven");
        if(entry_index>=entity->entry_points->size())
            return unsupported("FindBestObjectForFunction candidate entry-point table is too short");
        const auto selected=(*entity->entry_points)[entry_index];
        if(!selected.action)continue;

        std::int32_t score=0;
        if(score_index>=0) {
            std::optional<std::int16_t> value;
            try { value=request.score_value(item.id,item.generation,static_cast<std::uint8_t>(score_index)); }
            catch(...) { return unsupported("FindBestObjectForFunction score lookup failed"); }
            if(!value)return unsupported("FindBestObjectForFunction score value is unresolved");
            score=*value;
            if(score<=0)continue;
            if((score_index==39&&score<800)||(score_index==64&&score<15)||(score_index==15&&score<600))continue;
        }

        bool execute=true;
        if(selected.condition) {
            FunctionalTreeResolvedCode condition;
            try { condition=request.resolve_condition(item.id,item.generation,selected.condition); }
            catch(...) { return unsupported("FindBestObjectForFunction condition resolution failed"); }
            if(condition.status==FunctionalTreeCodeStatus::unsupported)
                return unsupported("FindBestObjectForFunction condition resource provenance is unresolved");
            if(condition.status==FunctionalTreeCodeStatus::resolved&&
               !valid_code(condition,selected.condition))
                return unsupported("FindBestObjectForFunction condition bank/key is incomplete");
            if(!request.identities_still_match()||!request.world->find(item.id)||
               request.world->find(item.id)->generation!=item.generation)
                return unsupported("FindBestObjectForFunction target changed before condition execution");
            std::string condition_diagnostic;
            const auto condition_result=run_condition(request,condition,item.id,item.generation,condition_diagnostic);
            if(!condition_result)return unsupported("FindBestObjectForFunction function="+std::to_string(function)+
                " candidate="+std::to_string(item.id)+" generation="+std::to_string(item.generation)+
                " condition="+std::to_string(selected.condition)+" owner="+std::to_string(condition.owner)+" "+condition_diagnostic);
            execute=*condition_result;
            // Check trees can mutate data, positions, entry points, and other
            // World state. Source retains this candidate's pre-check score but
            // rereads later fields and scans from live VM state.
            if(!request.identities_still_match()||!request.world->find(item.id)||
               request.world->find(item.id)->generation!=item.generation)
                return unsupported("FindBestObjectForFunction identity changed during condition effects");
        } else if(is_surface_entry(entry_index)) {
            std::optional<bool> empty;
            try { empty=request.slot_zero_empty(item.id,item.generation); }
            catch(...) { return unsupported("FindBestObjectForFunction slot-zero lookup failed"); }
            if(!empty)return unsupported("FindBestObjectForFunction slot-zero state is unresolved");
            execute=*empty;
        }
        if(!execute)continue;

        std::optional<bool> in_use;
        try { in_use=request.is_in_use(item.id,item.generation); }
        catch(...) { return unsupported("FindBestObjectForFunction IsInUse lookup failed"); }
        if(!in_use)return unsupported("FindBestObjectForFunction multitile/avatar use evidence is incomplete");
        if(*in_use)continue;
        if(!request.identities_still_match())
            return unsupported("FindBestObjectForFunction source/inventory changed before distance scoring");
        const auto* live_candidate=request.world->find(item.id);
        const auto* caller=request.world->frame_entity(*request.source_frame,VmEntityRole::caller);
        if(!live_candidate||live_candidate->generation!=item.generation||!caller)
            return unsupported("FindBestObjectForFunction distance identities are unavailable");
        const auto penalty=source_distance_penalty(narrow_position(live_candidate),narrow_position(caller));
        score=wrap_sub(score,penalty);
        if(score>best_score) { best_score=score;best=item; }
    }

    if(!request.identities_still_match())
        return unsupported("FindBestObjectForFunction source/inventory changed before result publication");
    if(best) {
        const auto* winner=request.world->find(best->id);
        if(!winner||winner->generation!=best->generation)
            return unsupported("FindBestObjectForFunction selected entity was removed or its ID was recycled");
        PrimitiveOutcome outcome{PrimitiveDisposition::goto_true,{}};
        outcome.stack_object_write=VmStackObjectWrite{best->id};
        return outcome;
    }
    return branch(false);
    } catch(const std::exception& error) {
        return unsupported(std::string("FindBestObjectForFunction failed: ")+error.what());
    } catch(...) {
        return unsupported("FindBestObjectForFunction failed with a non-standard exception");
    }
}

} // namespace odh
