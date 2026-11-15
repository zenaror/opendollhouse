// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <filesystem>
#include <string>
namespace odh_host {
struct ReferencePatchIdentity { std::string name; std::size_t bytes; std::string sha256; };
// Fixed four-file contract. Injectable identities support synthetic fixtures;
// production always uses the pinned identities below, never user metadata.
using ReferencePatchIdentities=std::array<ReferencePatchIdentity,4>;
const ReferencePatchIdentities& reference_patch_identities();
// Read-only preflight, also called before the frontend creates a new save root.
std::filesystem::path validate_reference_patch_source(const std::filesystem::path& save_root,
    const std::filesystem::path& source,
    const ReferencePatchIdentities& identities=reference_patch_identities());
struct ReferencePatchSelection { std::filesystem::path root; std::string diagnostic; };
// Explicit override is read-only and does not consult/change persisted choice.
// Import + override is an error. No selection is a valid explicit unpatched state.
ReferencePatchSelection prepare_reference_patches(const std::filesystem::path& save_root,
    const std::filesystem::path& import_source={},const std::filesystem::path& override_root={},
    const ReferencePatchIdentities& identities=reference_patch_identities());
}
