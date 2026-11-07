// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/dialog.hpp"
#include "odh/neighbor_context.hpp"
namespace odh_host {
enum class DialogTextAvailability { value, absent, unavailable };
struct DialogTextValue {
    DialogTextAvailability availability=DialogTextAvailability::unavailable;
    std::string utf8;
};
// Exact immutable resource identity; no search or operating-system path is
// inferred by the value service. Host readers authenticate these descriptors.
struct DialogTextTable {std::string archive,entry;std::uint16_t id=301;};
enum class DialogEntityCatalogPolicy { own, prefer_master };
struct DialogTextBudget {unsigned expansions=0,lookups=0;};
struct DialogTextProviders {
    // Actual source Name owner (for avatars, the already selected BodySTR11
    // name). Absent means known empty group name; unavailable is not fallback.
    std::function<DialogTextValue(const odh::Entity&)> entity_runtime_name;
    // CTSS0=name, CTSS1=description. Entity binding chooses exact master/private
    // identity; neighbor binding uses its NBRS GUID even without a World body.
    std::function<DialogTextValue(const odh::Entity&,unsigned,DialogEntityCatalogPolicy)> entity_catalog;
    std::function<DialogTextValue(std::uint32_t,unsigned)> neighbor_catalog;
    // absent means a known absent TABLE. Present-but-missing string uses the
    // existing source literal Missing String!!!, not an absent table fallback.
    std::function<DialogTextValue(const DialogTextTable&,std::uint16_t)> string;
    std::function<std::optional<DialogTextTable>(std::uint16_t)> global_table;
    std::function<std::optional<std::vector<DialogTextTable>>(const odh::Entity&,std::uint16_t)> entity_tables;
    // Format/list owners are explicit. Absence of a persisted MyList or proven
    // locale formatter never becomes an invented empty list/date/currency.
    std::function<std::optional<std::string>(const odh::Entity&)> list_utf8;
    std::function<std::optional<std::string>(std::int16_t)> fixed;
    std::function<std::optional<std::string>(std::int32_t)> money;
    std::function<std::optional<std::string>(std::int16_t,std::int16_t,std::int16_t)> date;
    // Original Job/JobDesc/JobOffer/TimeLocal helpers retain their existing
    // EXE-derived policies instead of being replaced by managed-reference ones.
    odh::DialogSubstitution career_time;
};
struct DialogTextContext {
    const odh::World& world;
    const odh::VmFrame& frame;
    const odh::Vm* source_vm=nullptr;
    const odh::NeighborContext* neighbors=nullptr;
    std::optional<DialogTextTable> current_table;
    DialogTextBudget* budget=nullptr; // One lease shared by all six prepared fields.
};
// Input and provider values are already decoded using each resource's actual
// language. No World/VM mutation. Output/recursion/selector bounds are explicit.
std::optional<std::string> expand_ts1_dialog_text(const std::string& utf8,
    const DialogTextContext&,const DialogTextProviders&,std::string& diagnostic);
}
