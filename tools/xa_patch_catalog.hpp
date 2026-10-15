#pragma once
#include "odh/hit_resources.hpp"
#include "odh/xa_audio.hpp"

namespace odh_host {
enum class XaSourceKind { far, loose };
struct XaPatchSource { std::string path; XaSourceKind kind; };
struct XaPatchCatalogLimits {
    std::uint32_t sources=16, entries=65536, name_length=4096;
    std::uint64_t name_bytes=4*1024*1024;
    odh::XaAudioLimits audio{};
};
struct XaPatchCatalogUsage { std::uint64_t sources=0, entries=0, xa_entries=0, name_bytes=0; };
struct XaPatchAsset {
    std::uint32_t patch_id;
    std::string key;
    XaPatchSource source;
    std::string entry; // FAR name, or empty for a loose file.
    std::uint64_t offset,length;
    odh::XaAudio audio;
};
// Host seekable read-only sources, supplied explicitly; no directory scan or
// precedence guesses. Matches ASCII-folded Windows/POSIX basenames as in the
// consulted TS1 provider. Source paths become absolute at construction.
// Duplicate keys reject lookup, in either source order.
// Inputs must remain immutable. Size changes are rejected, not all mutations
// can be detected. Decodes only the selected bounded view; no extraction/cache.
// Returned PCM is owned by the caller; no aggregate PCM/RSS budget is implied.
class XaPatchCatalog {
public:
    explicit XaPatchCatalog(const std::vector<XaPatchSource>&, XaPatchCatalogLimits = {});
    XaPatchAsset load(const odh::Hot&,std::uint32_t patch_id) const;
    XaPatchCatalogUsage usage() const {return usage_;}
private:
    struct Source {XaPatchSource source;std::uint64_t size;};
    struct Candidate {std::size_t source;std::uint64_t offset,length;std::string entry;std::uint32_t count=1;};
    std::vector<Source> sources_;
    std::map<std::string,Candidate> index_;
    XaPatchCatalogLimits limits_;
    XaPatchCatalogUsage usage_;
    std::string key(const std::string&) const;
    void charge(std::uint64_t);
    void add(std::size_t,std::string,std::uint64_t,std::uint64_t);
};
}
