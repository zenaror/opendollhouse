#include "odh/relationship.hpp"
#include <algorithm>

namespace odh {
namespace {
std::int16_t low16(std::int32_t v) {
    const auto u = static_cast<std::uint16_t>(static_cast<std::uint32_t>(v));
    return static_cast<std::int16_t>(u < 32768 ? static_cast<std::int32_t>(u) : static_cast<std::int32_t>(u)-65536);
}
bool valid(std::int16_t i, std::size_t size) { return i >= 0 && static_cast<std::size_t>(i) < size; }
std::optional<std::int16_t> input(const VmFrame& f, const std::array<std::int16_t,20>& t,
    const VmMemory& m, std::uint8_t scope, std::int16_t index) {
    switch (scope) {
    case 7: return index;
    case 8: return valid(index,t.size()) ? std::optional<std::int16_t>{t[static_cast<std::size_t>(index)]} : std::nullopt;
    case 9: return valid(index,f.arguments.size()) ? std::optional<std::int16_t>{f.arguments[static_cast<std::size_t>(index)]} : std::nullopt;
    case 10: return f.context.stack_object;
    case 25: return valid(index,f.locals.size()) ? std::optional<std::int16_t>{f.locals[static_cast<std::size_t>(index)]} : std::nullopt;
    default:
        if (const auto a=resolve_person_data_address(scope,index,t); a && m.read) return m.read(f,a->scope,a->index);
        return {};
    }
}
}
RelationshipState::RelationshipState(const NeighborContext& context, RelationshipLimits limits):limits_(limits) {
    if (context.active_count() > limits_.sources) throw FormatError("relationship source budget exceeded");
    for (const auto& entry : context.source().entries) {
        if (!entry.record) continue;
        const auto& record=*entry.record;
        auto& pairs=sources_[{RelationshipMatrix::neighbor,record.neighbor_id,0}];
        for (const auto& relation : record.relationships) {
            if (pairs_ >= limits_.pairs || relation.values.size() > limits_.values-values_)
                throw FormatError("relationship snapshot budget exceeded");
            std::vector<std::int16_t> words;
            words.reserve(relation.values.size());
            for (auto value : relation.values) words.push_back(low16(value));
            if (!pairs.emplace(relation.key,std::move(words)).second)
                throw FormatError("duplicate relationship target key");
            ++pairs_; values_+=relation.values.size();
        }
    }
}
std::optional<std::int16_t> RelationshipState::lookup(Source source,std::int32_t target,std::size_t index) const {
    const auto s=sources_.find(source); if (s==sources_.end()) return {};
    const auto p=s->second.find(target); if (p==s->second.end() || index>=p->second.size()) return {};
    return p->second[index];
}
std::optional<std::int16_t> RelationshipState::value(std::int16_t source,std::int32_t target,std::size_t index) const {
    return lookup({RelationshipMatrix::neighbor,source,0},target,index);
}
std::optional<std::int16_t> RelationshipState::object_value(const World& w,std::int16_t source,std::int16_t target,std::size_t index) const {
    const auto entity=w.find(source); if (!entity) return {};
    return lookup({RelationshipMatrix::object,source,entity->generation},static_cast<std::uint16_t>(target),index);
}
VmPrimitive RelationshipState::primitive(const World& world,VmMemory memory,std::function<void(const RelationshipRecord&)> observer) {
    return [this,&world,memory=std::move(memory),observer=std::move(observer)]
        (const VmFrame& frame,const Instruction& instruction,const std::array<std::int16_t,20>& temps,const VmDispatch&)->PrimitiveOutcome {
        const bool legacy=instruction.opcode==24;
        if (!legacy && instruction.opcode!=26) return {};
        const auto& b=instruction.operands;
        const auto index=b[legacy?1:0], mode=b[legacy?3:1], flags=b[legacy?4:2];
        const auto set_mode=legacy?b[0]:static_cast<std::uint8_t>(((flags>>2)&1)|((flags>>4)&2));
        const auto scope16=legacy?std::uint16_t{9}:static_cast<std::uint16_t>(b[4]|(std::uint16_t{b[5]}<<8));
        const auto var=legacy?static_cast<std::int16_t>(b[2]):low16(b[6]|(std::int32_t{b[7]}<<8));
        RelationshipRecord record; record.context=frame.context;record.index=index;record.mode=mode;record.set_mode=set_mode;
        auto finish=[&](PrimitiveDisposition result,const char* diagnostic="")->PrimitiveOutcome {
            record.success=result==PrimitiveDisposition::goto_true;
            if (observer) observer(record);
            return {result,diagnostic};
        };
        if (mode>3) return finish(PrimitiveDisposition::unsupported,"relationship direction unsupported");
        std::int16_t selected=0;
        if (mode>=2) {
            const auto& bank=legacy?frame.arguments:frame.locals;
            const auto selector=legacy?std::size_t{0}:static_cast<std::size_t>(b[3]);
            if (selector>=bank.size()) return finish(PrimitiveDisposition::unsupported,"relationship entity selector unavailable");
            selected=bank[selector];
        }
        Source source{RelationshipMatrix::neighbor,0,0};
        std::int32_t target=0;
        if (flags&2) {
            std::int16_t caller_neighbor=0;
            if (mode<2) {
                const auto caller=world.find(frame.context.caller);
                if (!caller || caller->kind!=EntityKind::avatar || caller->person_data.size()<=31)
                    return finish(PrimitiveDisposition::unsupported,"relationship caller neighbor ID unavailable");
                caller_neighbor=caller->person_data[31];
            }
            source.id=mode==0?caller_neighbor:mode==3?selected:frame.context.stack_object;
            target=mode==0||mode==3?frame.context.stack_object:mode==1?caller_neighbor:selected;
        } else {
            const auto first=mode==0?frame.context.caller:mode==3?selected:frame.context.stack_object;
            const auto second=mode==0||mode==3?frame.context.stack_object:mode==1?frame.context.caller:selected;
            const auto a=world.find(first), c=world.find(second);
            record.matrix=RelationshipMatrix::object;record.source=first;record.target=static_cast<std::uint16_t>(second);
            if (!a || !c) {record.skipped_entities=true;return finish(PrimitiveDisposition::goto_true);}
            const auto nid=[](const Entity& e)->std::int16_t {
                return e.kind==EntityKind::avatar && e.person_data.size()>31?e.person_data[31]:std::int16_t{0};
            };
            if (nid(*a)>0 && nid(*c)>0) {source.id=nid(*a);target=nid(*c);}
            else {source={RelationshipMatrix::object,first,a->generation};target=static_cast<std::uint16_t>(second);}
        }
        record.matrix=source.matrix;record.source=source.id;record.target=target;
        const auto s=sources_.find(source);
        if (s==sources_.end() && source.matrix==RelationshipMatrix::neighbor)
            return finish(PrimitiveDisposition::unsupported,"relationship source neighbor unavailable");
        const auto p=s==sources_.end()?nullptr:[&]()->const std::vector<std::int16_t>* {
            const auto found=s->second.find(target);return found==s->second.end()?nullptr:&found->second;
        }();
        const bool created=!p, grown=!p || p->size()<=index;
        if ((created || grown) && (flags&1)) return finish(PrimitiveDisposition::goto_false);
        const auto growth=grown?static_cast<std::size_t>(index)+1-(p?p->size():0):0;
        if ((s==sources_.end() && sources_.size()>=limits_.sources) || (created && pairs_>=limits_.pairs) || growth>limits_.values-values_)
            return finish(PrimitiveDisposition::unsupported,"relationship storage budget exceeded");
        PrimitiveOutcome outcome{PrimitiveDisposition::goto_true,{}};
        std::optional<VmMemoryAddress> output_address;
        std::int16_t result=0;
        if (set_mode<3) {
            if (scope16>255) return finish(PrimitiveDisposition::unsupported,"relationship variable scope unsupported");
            const auto scope=static_cast<std::uint8_t>(scope16);
            const auto before=p && p->size()>index?(*p)[index]:std::int16_t{0};
            record.value_before=before;
            if (set_mode==0) {
                result=before;
                switch (scope) {
                case 8: if (!valid(var,temps.size())) return finish(PrimitiveDisposition::unsupported,"relationship temp output unavailable");
                    outcome.temp_write=VmTempWrite{static_cast<std::size_t>(var),result};break;
                case 9: if (!valid(var,frame.arguments.size())) return finish(PrimitiveDisposition::unsupported,"relationship argument output unavailable");
                    outcome.write=VmFrameWrite{VmFrameBank::arguments,static_cast<std::size_t>(var),result};break;
                case 25: if (!valid(var,frame.locals.size())) return finish(PrimitiveDisposition::unsupported,"relationship local output unavailable");
                    outcome.write=VmFrameWrite{VmFrameBank::locals,static_cast<std::size_t>(var),result};break;
                case 10: outcome.stack_object_write=VmStackObjectWrite{result};break;
                case 7: return finish(PrimitiveDisposition::unsupported,"relationship literal output is not writable");
                default: output_address=resolve_person_data_address(scope,var,temps);
                    if (!output_address || !memory.write) return finish(PrimitiveDisposition::unsupported,"relationship external output unavailable");
                }
            } else {
                const auto operand=input(frame,temps,memory,scope,var);
                if (!operand) return finish(PrimitiveDisposition::unsupported,"relationship input unavailable");
                result=set_mode==1?*operand:low16(static_cast<std::int32_t>(before)+*operand);
                if (!(flags&64)) result=std::clamp(result,std::int16_t{-100},std::int16_t{100});
            }
            record.value_after=result;
        }
        // Stage only a list for existing pairs. Pair/source creation copies the
        // bounded matrix so every allocation still precedes an external writer.
        // Existing non-growing reads/no-ops need no relationship allocation.
        auto external_write=[&] {
            return !output_address || memory.write(frame,output_address->scope,output_address->index,result);
        };
        if (created) {
            auto candidate=sources_;
            auto& list=candidate[source][target];
            list.resize(static_cast<std::size_t>(index)+1,0);
            if (set_mode==1 || set_mode==2) list[index]=result;
            if (!external_write()) return finish(PrimitiveDisposition::unsupported,"relationship external output rejected write");
            sources_.swap(candidate);
        } else if (grown || set_mode==1 || set_mode==2) {
            auto candidate=*p;
            if (grown) candidate.resize(static_cast<std::size_t>(index)+1,0);
            if (set_mode==1 || set_mode==2) candidate[index]=result;
            if (!external_write()) return finish(PrimitiveDisposition::unsupported,"relationship external output rejected write");
            s->second.find(target)->second.swap(candidate);
        } else if (!external_write()) return finish(PrimitiveDisposition::unsupported,"relationship external output rejected write");
        pairs_+=created?1:0;values_+=growth;
        record.created=created;record.grown=grown;record.success=true;
        if (observer) observer(record);
        return outcome;
    };
}
}
