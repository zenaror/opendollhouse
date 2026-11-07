// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "community_lot_store.hpp"
#include "odh/architecture_resource_catalog.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace odh_host {
class LinuxWindow;
struct CommunityLotEditorOptions : CommunityLotStoreOptions {
    std::uint8_t language=1;
    std::filesystem::path floors,walls,build;
    std::vector<std::filesystem::path> definition_resources;
    std::vector<odh::ArchitectureResourceSource> architecture_sources;
    odh::ArchitecturePreparationOptions preparation;
};
struct CommunityLotEditorResult {
    bool changed=false; // At least one explicit save published.
    bool exit_application=false;
    std::size_t accepted_commands=0;
    std::filesystem::path checkpoint;
};
// No household actor or VM is invented; saved entities remain frozen. Draft
// edits stay in memory until Save. Exit offers Save, Discard and Cancel.
CommunityLotEditorResult show_community_lot_editor(LinuxWindow&,const CommunityLotEditorOptions&,
    const std::function<void(const std::string&)>& log={});
}
