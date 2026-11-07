#include "odh/saved_code_compatibility.hpp"

#include <algorithm>
#include <bitset>
#include <utility>

namespace odh {
namespace {
std::uint32_t native_word(const Bhav& routine) {
    std::uint32_t word=0;
    for(unsigned i=0;i<4;++i) word|=std::uint32_t(routine.header[4+i])<<(8*i);
    return word;
}
bool old_objt(std::uint32_t version) {
    return version==0 || version>=0x80000000U;
}
}

SavedCodeSignature compute_saved_code_signature(std::uint16_t root,
    const SavedCodeResolver& resolve,const SavedCodeSignatureLimits& limits) {
    SavedCodeSignature result{};
    if(!resolve) return result;
    // Iterative traversal avoids a native-stack dependency on untrusted graphs.
    std::bitset<65536> seen{};
    std::vector<std::uint16_t> pending;
    auto enqueue=[&](std::uint16_t id) {
        if(seen[id]) return true;
        if(result.visited_ids>=limits.routine_ids) {
            result.state=SavedCodeSignatureState::routine_limit;
            result.problem_id=id;return false;
        }
        seen[id]=true;++result.visited_ids;pending.push_back(id);return true;
    };
    if(!enqueue(root)) return result;
    std::uint32_t sum=0,root_word=0;
    while(!pending.empty()) {
        const auto id=pending.back();pending.pop_back();
        const auto* routine=resolve(id);
        if(!routine) continue;
        if(routine->version<0x8000 || routine->version>0x8002) {
            result.state=SavedCodeSignatureState::unsupported_format;
            result.problem_id=id;result.problem_format=routine->version;return result;
        }
        const auto word=native_word(*routine);
        if(id==root) root_word=word;
        sum+=word;
        for(const auto& instruction:routine->instructions) {
            if(result.examined_instructions>=limits.instructions) {
                result.state=SavedCodeSignatureState::instruction_limit;
                result.problem_id=id;return result;
            }
            ++result.examined_instructions;
            const auto called=static_cast<std::uint16_t>(instruction.opcode&0x7fffU);
            if(called>255 && !enqueue(called)) return result;
        }
    }
    result.state=SavedCodeSignatureState::computed;
    result.value=(root_word<<16)+sum;
    return result;
}

namespace {
SavedCodeTypeComparison compare_type(const Objt& table,std::uint16_t type,
    const SavedCodeResources& resources,bool recovery_enabled,
    const SavedCodeSignatureLimits& limits,
    std::uint64_t* instructions_left=nullptr, std::uint64_t* ids_left=nullptr) {
    SavedCodeTypeComparison result{};result.serialized_type=type;
    const auto* entry=table.entry_for_type(type);
    if(!entry) return result;
    result.guid=entry->guid;
    if(!entry->guid) {result.state=SavedCodeTypeState::sentinel;return result;}
    result.special_guid=entry->guid==0x3c08965fU;
    if(old_objt(table.version)) {
        result.state=SavedCodeTypeState::unsupported_objt_version;return result;
    }
    if(!entry->metadata) {result.state=SavedCodeTypeState::missing_metadata;return result;}
    const auto& metadata=*entry->metadata;
    result.saved={std::uint32_t(metadata.init_tree_version)<<16 | metadata.unknown1a,
                  std::uint32_t(metadata.main_tree_version)<<16 | metadata.unknown2a};
    if(!resources.supported) {result.state=SavedCodeTypeState::unsupported_resources;return result;}
    if((resources.init_root && resources.init_root<256) ||
       (resources.main_root && resources.main_root<256)) {
        result.state=SavedCodeTypeState::invalid_entry_root;return result;
    }
    if(!resources.resolve && (resources.init_root || resources.main_root)) {
        result.state=SavedCodeTypeState::unsupported_resources;return result;
    }
    // An entry explicitly declaring no tree (ID0) has signature zero. A required
    // nonzero root absent from authenticated resources is an incomplete adapter,
    // not evidence that the saved pair matches the current installed scripts.
    if((resources.init_root && !resources.resolve(resources.init_root)) ||
       (resources.main_root && !resources.resolve(resources.main_root))) {
        result.state=SavedCodeTypeState::missing_required_root;return result;
    }
    auto compute=[&](std::uint16_t root) {
        if(!root) {
            SavedCodeSignature absent{};absent.state=SavedCodeSignatureState::computed;
            return absent;
        }
        auto bounded=limits;
        if(instructions_left) bounded.instructions=std::min(bounded.instructions,*instructions_left);
        if(ids_left) bounded.routine_ids=static_cast<std::uint32_t>(
            std::min<std::uint64_t>(bounded.routine_ids,*ids_left));
        auto signature=compute_saved_code_signature(root,resources.resolve,bounded);
        if(instructions_left) *instructions_left-=signature.examined_instructions;
        if(ids_left) *ids_left-=signature.visited_ids;
        return signature;
    };
    result.init=compute(resources.init_root);
    result.main=compute(resources.main_root);
    if(result.init.state!=SavedCodeSignatureState::computed ||
       result.main.state!=SavedCodeSignatureState::computed) {
        result.state=SavedCodeTypeState::unsupported_signature;return result;
    }
    result.current={result.init.value,result.main.value};
    const bool mismatch=result.saved.init!=result.current.init || result.saved.main!=result.current.main;
    result.state=mismatch?SavedCodeTypeState::mismatched:SavedCodeTypeState::matched;
    result.final_init_required=recovery_enabled && mismatch;
    result.recovery_required=result.final_init_required || result.special_guid;
    return result;
}

} // namespace
SavedCodeTypeComparison compare_saved_code_type(const Objt& table,std::uint16_t type,
    const SavedCodeResources& resources,bool recovery_enabled,
    const SavedCodeSignatureLimits& limits) {
    return compare_type(table,type,resources,recovery_enabled,limits);
}

SavedCodePreflight preflight_saved_code_compatibility(const Objt& table,
    const SavedCodeResourceProvider& provide,bool recovery_enabled,
    const SavedCodePreflightLimits& limits) {
    SavedCodePreflight result{};
    if(table.entries.size()>limits.types || table.entries.size()>65535) {
        result.state=SavedCodePreflightState::type_limit;return result;
    }
    result.state=SavedCodePreflightState::complete;
    result.types.reserve(table.entries.size());
    for(std::size_t i=0;i<table.entries.size();++i) {
        const auto type=static_cast<std::uint16_t>(i+1);
        const auto& entry=table.entries[i];
        SavedCodeResources resources{};
        // Sentinels and unsupported table formats need no resource lookup.
        if(entry.guid && entry.metadata && !old_objt(table.version) && provide)
            resources=provide(type,entry.guid);
        auto instructions_left=limits.instructions-result.examined_instructions;
        auto ids_left=limits.routine_ids-result.visited_ids;
        auto comparison=compare_type(table,type,resources,recovery_enabled,
            limits.signature,&instructions_left,&ids_left);
        result.examined_instructions+=comparison.init.examined_instructions+comparison.main.examined_instructions;
        result.visited_ids+=comparison.init.visited_ids+comparison.main.visited_ids;
        if(comparison.state==SavedCodeTypeState::mismatched) ++result.mismatched_types;
        if(comparison.recovery_required) ++result.recovery_types;
        const bool limited=comparison.init.state==SavedCodeSignatureState::routine_limit ||
            comparison.main.state==SavedCodeSignatureState::routine_limit ||
            comparison.init.state==SavedCodeSignatureState::instruction_limit ||
            comparison.main.state==SavedCodeSignatureState::instruction_limit;
        if(comparison.state!=SavedCodeTypeState::matched &&
           comparison.state!=SavedCodeTypeState::mismatched &&
           comparison.state!=SavedCodeTypeState::sentinel)
            result.state=SavedCodePreflightState::unsupported;
        result.types.push_back(std::move(comparison));
        if(limited || result.examined_instructions>limits.instructions || result.visited_ids>limits.routine_ids) {
            result.state=SavedCodePreflightState::work_limit;return result;
        }
    }
    return result;
}
} // namespace odh
