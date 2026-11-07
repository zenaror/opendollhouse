// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/architecture_edit.hpp"
#include "odh/architecture_resource_catalog.hpp"
#include "odh/bitmap.hpp"
#include "architecture_material_resources.hpp"
#include <filesystem>
#include <map>

namespace odh_host {
struct ArchitectureEditMaterialDescriptor {
    odh::ArchitectureEditMaterialKind kind=odh::ArchitectureEditMaterialKind::floor;
    std::uint16_t id=0;
    std::string name,description;
    std::int32_t price=0;
    std::uint16_t default_pattern=0; // Five source UI wall-style pairings.
};
struct ArchitectureEditResourceLimits {
    std::size_t materials=65536,metadata_bytes=64*1024*1024;
    std::uint32_t chunks_per_file=4096;
};
// Explicit selected resource files/catalog. Opens original files read-only ONCE
// at construction; material()/descriptors() never perform I/O. Dynamic IDs are
// exact prepared/catalog IDs; raw FLRm/WALm aliases never enter these maps.
class ArchitectureEditResources {
public:
    ArchitectureEditResources(const std::filesystem::path&build,
        const std::filesystem::path&floors,const std::filesystem::path&walls,
        const odh::ArchitectureResourceCatalog&,std::uint8_t language=1,
        const ArchitectureEditResourceLimits& = {});
    odh::ArchitectureEditMaterial material(odh::ArchitectureEditMaterialKind,std::uint16_t)const;
    const std::vector<ArchitectureEditMaterialDescriptor>&descriptors()const noexcept{return descriptors_;}
    // Load only the visible catalog page; no decoding in a frame callback.
    std::optional<odh::BitmapImage> thumbnail(odh::ArchitectureEditMaterialKind,std::uint16_t)const;
    ArchitectureMaterialCacheStats material_cache_stats()const noexcept{return resource_files_.cache_stats();}
private:
    std::filesystem::path floors_,walls_;
    ArchitectureMaterialResources resource_files_;
    std::map<std::pair<odh::ArchitectureEditMaterialKind,std::uint16_t>,odh::ArchitectureEditMaterial> materials_;
    std::vector<ArchitectureEditMaterialDescriptor>descriptors_;
};
}
