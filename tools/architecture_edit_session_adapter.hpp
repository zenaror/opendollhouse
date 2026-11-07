// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/architecture_edit.hpp"
#include "odh/architecture_snapshot.hpp"
#include "odh/definition_index.hpp"
#include "linux_family_session_journal.hpp"

namespace odh_host {
struct ArchitectureEditValidationContext {
    const odh::WorldSnapshot* world=nullptr;
    const odh::SavedArchitecture* terrain=nullptr;
    const odh::PreparedArchitecture* prepared=nullptr;
    const odh::GeometricRoomMap* rooms=nullptr;
    const odh::DefinitionIndex* definitions=nullptr;
    odh::ArchitectureEditRectangleClip rectangle_clip;
    // Real category8 SetObjectSupported event state. Absence is unknown, not
    // a grid of false values. Only upper-level support checks require it.
    std::optional<std::vector<std::uint8_t>> object_support;
    std::function<odh::ObjectPositionRegistryServices(odh::World&,const odh::ObjectPositionRegistrySnapshot&)> registry_services;
    std::function<odh::ArchitectureEditMaterial(odh::ArchitectureEditMaterialKind,std::uint16_t)> material;
};
odh::ArchitectureEditServices make_architecture_edit_services(ArchitectureEditValidationContext);
struct ArchitectureEditSessionHooks {
    std::function<odh::SessionBundle()> capture_current;
    // Must perform actual candidate restore/geometry/route compatibility
    // validation, including the source WallsChanged/room-route consumer.
    // It must not clear queues/routes or modify the live simulation.
    std::function<void(const odh::SessionBundle&,const odh::PreparedArchitecture&,
        const odh::GeometricRoomMap&)> validate_candidate;
    std::function<void(const odh::SessionBundle&,const std::vector<std::uint8_t>&)> validate_reopen;
};
enum class ArchitectureEditSessionState {cancelled,rejected,unsupported,published_reload_required};
struct ArchitectureEditSessionResult {
    ArchitectureEditSessionState state=ArchitectureEditSessionState::unsupported;
    std::string diagnostic;
    std::int32_t cost=0;
    std::vector<odh::ArchitectureEditReceipt> receipts;
    std::filesystem::path checkpoint;
    std::vector<std::uint8_t> archive;
    bool durability_uncertain=false;
};
class ArchitectureEditSessionAdapter {
public:
    ArchitectureEditSessionAdapter(FamilySessionJournal&,const odh::ArchitectureResourceCatalog&,
        ArchitectureEditValidationContext,ArchitectureEditSessionHooks);
    // Empty batch cancels with no capture/lookup/I/O. Success is a durable
    // same-house reload boundary: never tick the former World after publish.
    ArchitectureEditSessionResult execute(const std::vector<odh::ArchitectureEditCommand>&);
private:
    FamilySessionJournal& journal_;const odh::ArchitectureResourceCatalog&catalog_;
    ArchitectureEditValidationContext context_;ArchitectureEditSessionHooks hooks_;
    bool executing_=false;
};
}
