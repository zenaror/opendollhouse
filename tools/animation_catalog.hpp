#pragma once
#include "odh/animation.hpp"
#include <memory>
namespace odh::host {
struct CatalogLimits {
    std::uint32_t sources=128, far_entries=8192, cache_records=16;
    std::uint64_t name_bytes=8*1024*1024, index_bytes=16*1024*1024;
    std::uint64_t metadata_bytes=1024*1024, cfp_samples=1048576;
    AnimationLimits parser{}; // Bounds each transient BCF/CFP decode too.
};
struct AnimationProvenance {std::string source,entry;std::uint32_t source_index,entry_index;};
struct CatalogAnimation {
    AnimationHeader header;
    AnimationProvenance bcf;
    std::optional<AnimationProvenance> cfp; // Missing resource is not a synthesized pose.
    std::uint64_t bcf_remaining_bytes;
};
struct CatalogDecoded {std::shared_ptr<const CatalogAnimation> animation;CfpPlanes planes;};
struct CatalogUsage {
    std::uint64_t sources=0,far_entries=0,bcf_supported=0,bcf_skipped_profile=0,animations=0,cfp_entries=0;
    std::uint64_t name_bytes=0,index_bytes=0,cache_records=0,live_headers=0,registry_entries=0,registry_bytes=0,metadata_bytes=0,cfp_samples=0;
};
// HOST, seekable read-only files, not thread-safe. Caller supplies every FAR;
// Lookup of duplicate animation names or CFP basenames rejects, regardless of
// source order; unrelated duplicates do not prevent constructing the index.
// All bounded binary BCF sections are validated, including zero-animation
// skeleton/appearance files. Unknown CMX or trailing profiles keep absence
// authority incomplete; malformed binary resources fail explicitly.
// Accounting is logical bytes (strings + populated record sizeof), not RSS or
// allocator capacity. Live returned handles stay charged after cache eviction;
// budget exhaustion while handles are pinned fails explicitly. Handles may
// outlive catalog destruction. metadata_bytes includes live decoded record
// overhead; cfp_samples counts all seven live plane lengths (4 bytes/sample).
// Resolving a still-live animation reuses its header identity, and decoding
// reuses a still-live decoded handle even after LRU eviction. A weak registry
// preserves these identities. Its logical bytes
// are included in index_bytes and reported separately; expired records are
// pruned on operations, and its population is bounded by live_headers.
class AnimationCatalog {
public:
    explicit AnimationCatalog(const std::vector<std::string>& sources,CatalogLimits={});
    ~AnimationCatalog();
    AnimationCatalog(AnimationCatalog&&) noexcept;
    AnimationCatalog& operator=(AnimationCatalog&&) noexcept;
    AnimationCatalog(const AnimationCatalog&)=delete;
    AnimationCatalog& operator=(const AnimationCatalog&)=delete;
    std::shared_ptr<const CatalogAnimation> resolve(const std::string& internal_name);
    // Null means known absence from the admitted complete supported index.
    // Missing source authority/skipped BCF profiles/ambiguity/load errors throw.
    std::shared_ptr<const CatalogAnimation> resolve_if_present(const std::string& internal_name);
    std::shared_ptr<const CatalogDecoded> decode(const std::string& internal_name);
    // The selected header must be the live immutable object returned by this
    // catalog (including an aliasing Playback handle). Identity maps directly
    // to its recorded BCF/CFP source and entry; no second name lookup occurs.
    // Copies and headers owned by another catalog reject, even with equal names.
    std::shared_ptr<const CatalogDecoded> decode(const AnimationHeader& selected);
    CatalogUsage usage() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
    Impl& state() const;
};
}
