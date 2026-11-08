// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/native_personal_state.hpp"
#include "odh/original_lot_classification.hpp"
#include <memory>
namespace odh {
// Original46dae0 indexes Globals at this+10:32 is simless, not lot-mode2.
// The host invokes the original load-time reset only for a new frontend load,
// before/after import; never normalize a cold continuation or an old archive.
constexpr std::int16_t original_frontend_playing_simless=0;
// Exact inputs of original4f277c and its five source predicates. Family number
// is the CURRENT selected family identity/Global9 (including synthetic0),
// not home/lot and NOT the distinct serialized FAMI FamilyNumber field.
using OriginalFrontendLotClass=OriginalLotClass;
struct OriginalFrontendSaveBoundary {
    std::int16_t house=0,simless=0;
    std::optional<std::int32_t> family_number;
    std::optional<OriginalFrontendLotClass> zoning;
};
// true: capture live people via425b05; false: OTHER original lot-save branch,
// never permission to omit saving; nullopt: missing authority affects decision.
// This classifies a boundary, not destination availability or travel policy.
std::optional<bool> original_frontend_personal_save_required(const OriginalFrontendSaveBoundary&) noexcept;

class OriginalFrontendPersonalSource {
public:
    const Nbrs& neighbors() const;
    OriginalFrontendPersonalSource with_catalog(const NativePersonalCatalog&) const;
    const NativePersonalCatalog* catalog() const;
    const std::array<std::uint8_t,32>& neighborhood_identity() const;
private:
    struct Data;
    explicit OriginalFrontendPersonalSource(std::shared_ptr<const Data> data):data_(std::move(data)){}
    std::shared_ptr<const Data> data_;
    friend OriginalFrontendPersonalSource read_original_frontend_personal_source(const View&);
};
// Hash and parse the same bounded owned immutable bytes. Source prefix order and
// null records remain significant; legacy last-GUID maps are never consulted.
OriginalFrontendPersonalSource read_original_frontend_personal_source(const View&);
// Optional externally authenticated original44-byte GUID index adapter. Absent
// accepts only a unique immutable GUID/NID mapping; never the legacy LAST map.
using OriginalFrontendGuidLookup=std::function<std::optional<std::int16_t>(std::uint32_t)>;
class PreparedOriginalFrontendPersonalSave {
public:
    PreparedOriginalFrontendPersonalSave()=default;
    const NativePersonalSnapshot& preview_snapshot() const;
    const std::vector<NativePersonalSource>& sources() const {return sources_;}
    bool source_current(const World&,const NativePersonalState&) const;
    // Single-use publication. Host authenticates resources/current head and CAS
    // immediately before this call, with no simulation between prepare/CAS/commit.
    bool commit(NativePersonalState&,const World&);
private:
    const NativePersonalState* owner_=nullptr;
    std::optional<OriginalFrontendPersonalSource> source_;
    NativePersonalSourceResolver resolve_;
    OriginalFrontendGuidLookup lookup_;
    std::vector<NativePersonalSource> sources_;
    std::optional<PreparedNativePersonalCommit> prepared_;
    NativePersonalSnapshot preview_;
    std::array<std::uint8_t,32> before_{};
    bool consumed_=false;
    friend PreparedOriginalFrontendPersonalSave prepare_original_frontend_personal_save(
        const OriginalFrontendPersonalSource&,const World&,const NativePersonalState&,
        std::int16_t,const NativePersonalSourceResolver&,const OriginalFrontendGuidLookup&);
};
// The caller admits the actual original frontend transition phase and Global32.
// Resource resolver must authenticate immutable GUID/NID/OBJD/content and exact
// live generation; missing bodies are skipped, conflicting identities rejected.
PreparedOriginalFrontendPersonalSave prepare_original_frontend_personal_save(
    const OriginalFrontendPersonalSource&,const World&,const NativePersonalState&,
    std::int16_t simless,const NativePersonalSourceResolver&,const OriginalFrontendGuidLookup& = {});
} // namespace odh
