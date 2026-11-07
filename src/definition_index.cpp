#include "odh/definition_index.hpp"
namespace odh {
DefinitionIndex::DefinitionIndex(const std::vector<DefinitionCandidate>& source,const DefinitionIndexLimits& limits){
    if(source.size()>limits.candidates)throw FormatError("object definition candidate budget exceeded");
    auto available=limits.provenance_bytes;
    for(const auto& candidate:source){
        for(const auto* value:{&candidate.source,&candidate.asset}){
            if(value->size()>available)throw FormatError("object definition provenance byte budget exceeded");
            available-=value->size();
        }
    }
    for(const auto& candidate:source)definitions_[candidate.definition.guid].push_back(candidate);
    candidates_=source.size();
}
std::vector<DefinitionCandidate> DefinitionIndex::snapshot_candidates() const {
    std::vector<DefinitionCandidate> result;result.reserve(candidates_);
    for(const auto& row:definitions_)result.insert(result.end(),row.second.begin(),row.second.end());
    return result;
}
const std::vector<DefinitionCandidate>* DefinitionIndex::candidates(std::uint32_t guid) const{
    const auto found=definitions_.find(guid);return found==definitions_.end()?nullptr:&found->second;
}
const DefinitionCandidate* DefinitionIndex::unique(std::uint32_t guid) const{
    const auto* values=candidates(guid);return values&&values->size()==1?&values->front():nullptr;
}
const DefinitionCandidate* DefinitionIndex::unique_master(const DefinitionCandidate& part) const{
    if(part.definition.master_id==0)return nullptr;
    const DefinitionCandidate* result=nullptr;
    for(const auto& group:definitions_)for(const auto& candidate:group.second){
        if(candidate.source!=part.source||candidate.asset!=part.asset||
           candidate.definition.master_id!=part.definition.master_id||candidate.definition.sub_index!=-1)continue;
        if(result||unique(candidate.definition.guid)!=&candidate)return nullptr;
        result=&candidate;
    }
    return result;
}
}
