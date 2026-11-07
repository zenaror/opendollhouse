#pragma once
#include "odh/object_definition.hpp"
#include <map>

namespace odh {
struct DefinitionCandidate {
    std::string source,asset;
    std::uint16_t chunk_id;
    ObjectDefinition definition;
};
struct DefinitionIndexLimits {
    std::uint32_t candidates=8192;
    // Objects + ExpansionShared + ExpansionPack1..7 in the Legacy install use
    // 583,522 bytes of source/asset text. Keep a bounded margin for installed
    // data paths without excluding the expansion definitions.
    std::uint64_t provenance_bytes=1024*1024;
};
// Explicit resource-set snapshot. Duplicate GUIDs remain ambiguous: no guessed
// archive precedence. Candidates and provenance are owned and budgeted before
// copying. Returned pointers survive while this immutable index exists.
class DefinitionIndex {
public:
    explicit DefinitionIndex(const std::vector<DefinitionCandidate>&,const DefinitionIndexLimits& = {});
    const std::vector<DefinitionCandidate>* candidates(std::uint32_t guid) const;
    const DefinitionCandidate* unique(std::uint32_t guid) const;
    // Resolve a multitile master within the same explicit source/asset and
    // MasterID, SubIndex=-1. Missing/duplicate globally ambiguous GUIDs fail.
    const DefinitionCandidate* unique_master(const DefinitionCandidate&) const;
    std::size_t guid_count() const{return definitions_.size();}
    // Bounded owned snapshot for catalog consumers; ambiguities are retained.
    std::vector<DefinitionCandidate> snapshot_candidates() const;
    std::size_t candidate_count() const{return candidates_;}
private:
    std::map<std::uint32_t,std::vector<DefinitionCandidate>> definitions_;
    std::size_t candidates_=0;
};
}
