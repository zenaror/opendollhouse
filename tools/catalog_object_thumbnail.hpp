// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "saved_object_renderer.hpp"
#include "odh/bitmap.hpp"
#include <string>

namespace odh_host {
enum class CatalogObjectThumbnailStatus { ready, unknown_definition, no_graphics, failed };
struct CatalogObjectThumbnailResult {
    CatalogObjectThumbnailStatus status=CatalogObjectThumbnailStatus::no_graphics;
    odh::BitmapImage image{};
    std::string detail;
};
// Host-only resource presentation. Does not create a World, run Init, or modify
// caller visuals. The caller authenticates the parts and their graphics state;
// this class never guesses a multitile group from catalog metadata.
// Definitions must outlive this cache. select replaces the previous result even
// on failure. image/result references remain valid only until select or clear.
// One isolated 1024x1024 camera, zoom3, <=64 parts. Actual geometry extending
// outside that canvas fails explicitly rather than returning clipped artwork.
class CatalogObjectThumbnail {
public:
    explicit CatalogObjectThumbnail(const odh::DefinitionIndex&);
    const CatalogObjectThumbnailResult& select(const std::vector<SavedObjectVisual>&,
                                               std::uint8_t rotation=0);
    const CatalogObjectThumbnailResult& result()const{return result_;}
    void clear();
private:
    const odh::DefinitionIndex& definitions_;
    SavedObjectRenderer renderer_;
    CatalogObjectThumbnailResult result_;
};
}
