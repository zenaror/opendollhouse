#pragma once
#include "odh/definition_index.hpp"
#include <functional>
#include <tuple>

namespace odh_host {
using DefinitionCanonicalSource=std::function<std::string(const std::string&)>;
// Metadata lookup only. The caller's catalogues remain unchanged: this must
// never replace the base Build/Buy catalogue or admit additional Main threads.
// The first original spelling of each physical source represents all aliases
// in the returned copy. This preserves provenance while allowing unique_master
// to match chunks supplied under different aliases of the same physical file.
inline std::vector<odh::DefinitionCandidate> combine_definition_lookup(
        const std::vector<odh::DefinitionCandidate>& base,
        const std::vector<odh::DefinitionCandidate>& supplemental,
        const DefinitionCanonicalSource& canonical_source,
        const odh::DefinitionIndexLimits& limits={}) {
    if(!canonical_source)throw odh::FormatError("definition lookup requires a canonical source resolver");
    using PhysicalKey=std::tuple<std::string,std::string,std::uint16_t>;
    std::map<PhysicalKey,std::uint32_t> physical;
    std::map<std::string,std::string> representatives;
    std::vector<odh::DefinitionCandidate> combined;
    auto remaining=limits.provenance_bytes;
    const auto append=[&](const std::vector<odh::DefinitionCandidate>& source){
        for(const auto& candidate:source){
            auto canonical=canonical_source(candidate.source);
            if(canonical.empty())throw odh::FormatError("definition lookup source canonicalization returned empty identity");
            if(canonical.size()>limits.provenance_bytes||candidate.asset.size()>limits.provenance_bytes-canonical.size())
                throw odh::FormatError("definition lookup physical identity exceeds provenance budget");
            const PhysicalKey key{canonical,candidate.asset,candidate.chunk_id};
            const auto existing=physical.find(key);
            if(existing!=physical.end()){
                if(existing->second!=candidate.definition.guid)
                    throw odh::FormatError("definition lookup physical OBJD has conflicting GUIDs");
                continue;
            }
            if(combined.size()>=limits.candidates)
                throw odh::FormatError("definition lookup candidate budget exceeded");
            const auto representative=representatives.find(canonical);
            const auto& spelling=representative==representatives.end()?candidate.source:representative->second;
            if(spelling.size()>remaining||candidate.asset.size()>remaining-spelling.size())
                throw odh::FormatError("definition lookup provenance byte budget exceeded");
            remaining-=spelling.size()+candidate.asset.size();
            auto copy=candidate;copy.source=spelling;
            if(representative==representatives.end())representatives.emplace(canonical,spelling);
            combined.push_back(std::move(copy));physical.emplace(key,candidate.definition.guid);
        }
    };
    append(base);append(supplemental);
    return combined;
}
} // namespace odh_host
