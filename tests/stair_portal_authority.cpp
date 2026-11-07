// SPDX-License-Identifier: MPL-2.0
// Pure authority tests; architectural/resource validation is supplied by the
// same host callback that validates reconstructed portal continuations.
#include "../tools/stair_portal_authority.hpp"
#include <iostream>
#include <stdexcept>
namespace {
using namespace odh;using namespace odh_host;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Fixture {
    World world;RoomPortalSession session;std::vector<VmFrame> stack;
    StairPortalAuthorityServices services;bool architecture=true,resource=true;unsigned validations=0;
    Instruction instruction{27,254,255,{0,0,254,0,0,0,0,0}};
    explicit Fixture(std::int16_t actor=1){
        for(const auto id:{std::int16_t{1},std::int16_t{2},std::int16_t{10},std::int16_t{11},std::int16_t{20},std::int16_t{21}}){
            require(world.create(id,id<10?EntityKind::avatar:EntityKind::object)&&world.define_type(id,1000+id)&&
                world.set_position(id,{24,24,static_cast<std::int16_t>(id==11||id==21?2:1),0}),"create exact actor/stair endpoints");
        }
        VmCall call;call.key=routine_key(4105);call.context={actor,10,10,77};call.arguments={0,0,0,0};
        require(world.bind_context_live(call.context),"bind pending portal references");
        RoomPortalContext context;context.pending_call=call;context.pending_edge=RoomPortalEdge{1,2,10,*world.find(10)->position};
        context.portal_generation=world.find(10)->generation;session.contexts.emplace(91,context);
        session.code_owners.emplace(std::make_tuple("objects.far","stair.iff",4105),77);
        VmFrame external;external.key=call.key;external.context=call.context;
        external.return_mode=VmCallReturnMode::external_frame_next_tick;external.invocation_ticket=91;
        VmFrame current;current.key=routine_key(4112);current.context={actor,10,11,77};
        require(world.bind_context_live(current.context),"bind actual current references");stack={external,current};
        services.endpoints=[this](auto anchor)->std::optional<std::vector<ObjectPositionIdentity>>{
            if(!architecture||anchor.id!=10||anchor.generation!=world.find(10)->generation)return {};
            return std::vector<ObjectPositionIdentity>{{10,world.find(10)->generation},{11,world.find(11)->generation}};
        };
        services.validate_context=[this,actor](auto id,auto ticket,const auto& context){++validations;
            return resource&&id==actor&&ticket==91&&context.pending_edge&&context.pending_edge->portal_id==10;
        };
    }
    bool relative(){return authorize_stair_goto_relative(world,stack,stack.back(),instruction,session,services);}
    bool snap(){const auto* target=world.find(11);return authorize_stair_portal_continuation(world,stack,stack.back(),
        {11,target->generation},session,services).has_value();}
};
template<class Mutation> void reject(Mutation mutation,const char* message){Fixture f;mutation(f);require(!f.relative(),message);}
}
int main(){try{
    for(const std::int16_t actor:{1,2}){Fixture f(actor);const auto before=f.world.snapshot();
        const auto proof=authorize_stair_portal_continuation(f.world,f.stack,f.stack.back(),{11,f.world.find(11)->generation},f.session,f.services);
        require(proof&&proof->ticket==91&&proof->actor.id==actor&&proof->portal.id==10&&proof->target.id==11&&f.relative()&&f.snap(),
            "each resident can resume its exact Portal15 and paired stair endpoint without UI state");
        require(f.world.find(1)->position==before.entities[0].position,"authorization never moves an actor");
        f.instruction.operands[3]=4;require(f.relative(),"both already-supported stair direction forms use same authority");
        f.session.contexts.clear();require(!f.relative(),"retained installed owner alone grants no active continuation");
    }
    reject([](auto& f){f.stack.front().return_mode=VmCallReturnMode::ordinary;},"ordinary script borrowed portal authority");
    reject([](auto& f){f.stack.front().invocation_ticket=92;},"wrong external ticket accepted");
    reject([](auto& f){f.stack.front().key=routine_key(4106);},"another external action accepted");
    reject([](auto& f){f.stack.front().context.code_owner=78;},"another external bank accepted");
    reject([](auto& f){f.stack.back().context.code_owner=78;},"current unrelated bank accepted");
    reject([](auto& f){f.stack.front().context.caller=2;},"another resident external frame accepted");
    reject([](auto& f){f.stack.back().context.callee=11;},"current foreign callee accepted");
    reject([](auto& f){f.stack.back().context.entity_references_bound=false;},"unbound current generation accepted");
    reject([](auto& f){f.session.contexts.at(91).pending_call.reset();},"missing pending call accepted");
    reject([](auto& f){f.session.contexts.at(91).pending_edge.reset();},"missing pending edge accepted");
    reject([](auto& f){++f.session.contexts.at(91).portal_generation;},"stale portal generation accepted");
    reject([](auto& f){f.session.contexts.at(91).pending_call->context.callee=11;},"mismatched pending callee accepted");
    reject([](auto& f){f.session.contexts.at(91).pending_call->return_mode=VmCallReturnMode::immediate;},"unrelated call protocol accepted");
    reject([](auto& f){f.architecture=false;},"unknown reconstructed architecture accepted");
    reject([](auto& f){f.resource=false;},"unverified immutable resource accepted");
    reject([](auto& f){f.services.validate_context={};},"absent resource validator accepted");
    reject([](auto& f){f.services.endpoints={};},"absent stair membership validator accepted");
    reject([](auto& f){f.services.endpoints=[&f](auto)->std::optional<std::vector<ObjectPositionIdentity>>{
        return std::vector<ObjectPositionIdentity>{{20,f.world.find(20)->generation},{21,f.world.find(21)->generation}};};},
        "same resource owner borrowed another stair group");
    reject([](auto& f){f.services.endpoints=[&f](auto)->std::optional<std::vector<ObjectPositionIdentity>>{
        return std::vector<ObjectPositionIdentity>{{10,f.world.find(10)->generation},{11,f.world.find(11)->generation+1}};};},
        "stale opposite endpoint accepted");
    reject([](auto& f){require(f.world.set_position(11,{24,24,1,0}),"change upper floor");},"non-paired endpoint levels accepted");
    reject([](auto& f){require(f.world.set_position(10,{40,24,1,0}),"change portal pose");},"changed architectural edge pose accepted");
    reject([](auto& f){require(f.world.remove(1)&&f.world.create(1,EntityKind::avatar)&&f.world.set_position(1,{24,24,1,0}),"reuse actor ID");},
        "actor ID reuse inherited a previous continuation");
    reject([](auto& f){auto nested=f.stack.front();nested.invocation_ticket=92;f.stack.insert(f.stack.end()-1,nested);},
        "nested external action borrowed an older portal ticket");
    reject([](auto& f){f.instruction.operands[3]=5;},"unsupported source operand shape accepted");
    reject([](auto& f){f.stack.back().key=routine_key(4113);},"arbitrary same-bank routine admitted as GotoRelative stair profile");
    {Fixture f;auto wrong=f.stack.back();++wrong.pc;
        require(!authorize_stair_portal_continuation(f.world,f.stack,wrong,{11,f.world.find(11)->generation},f.session,f.services),
            "fabricated current frame accepted");}
    reject([](auto& f){f.services.validate_context=[&f](auto,auto,const auto&){
        require(f.world.set_position(11,{40,24,2,0}),"mutate endpoint during validator");return true;};},
        "endpoint mutation during proof accepted");
    reject([](auto& f){f.services.validate_context=[&f](auto,auto,const auto&){f.session.contexts.clear();return true;};},
        "context removal during proof accepted");
    reject([](auto& f){f.services.validate_context=[&f](auto,auto,const auto&){
        require(f.world.set_position(1,{40,24,1,0}),"mutate actor during validator");return true;};},
        "actor mutation during proof accepted");
    reject([](auto& f){f.services.validate_context=[&f](auto,auto,const auto&){++f.stack.front().invocation_ticket;return true;};},
        "external ticket mutation during proof accepted");
    std::cout<<"stair_portal_authority: PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<"stair_portal_authority: "<<e.what()<<'\n';return 1;}}
