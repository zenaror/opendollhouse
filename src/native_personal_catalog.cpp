// SPDX-License-Identifier: MPL-2.0
#include "odh/native_personal_catalog.hpp"
#include <algorithm>
namespace odh {
namespace {
bool nonzero(const std::array<std::uint8_t,32>& h){return std::any_of(h.begin(),h.end(),[](auto b){return b!=0;});}
void path(const std::string& s,bool entry){
    if(s.empty()||s.size()>4096||s.find('\0')!=std::string::npos||s.front()=='/'||s.front()=='\\'||s.find(':')!=std::string::npos||(!entry&&s.find('\\')!=std::string::npos))throw FormatError("hydrated catalog resource path is not canonical");
    std::size_t start=0;for(std::size_t i=0;i<=s.size();++i)if(i==s.size()||s[i]=='/'||(entry&&s[i]=='\\')){auto c=s.substr(start,i-start);if(c.empty()||c=="."||c=="..")throw FormatError("hydrated catalog resource path has invalid component");start=i+1;}
}
Nbrs derive(const Nbrs& raw,const std::vector<NativePersonalCatalogDefinition>& defs,const std::set<std::int16_t>& reserved,std::vector<NativePersonalCatalogBinding>& bindings){
    if(raw.entries.size()>4096)throw FormatError("hydrated neighbor source exceeds4096");
    auto out=raw;
    for(std::size_t n=0;n<out.entries.size();++n){const auto& r=out.entries[n];if(!r.record){if(r.marker!=0)throw FormatError("hydrated catalog malformed source hole");continue;}
        if(r.marker!=1||r.record->neighbor_id!=static_cast<std::int16_t>(n+1))throw FormatError("hydrated catalog source NID does not match original manager slot");
    }
    for(const auto& d:defs){std::optional<std::size_t> existing;
        for(std::size_t n=0;n<out.entries.size();++n)if(out.entries[n].record&&out.entries[n].record->guid==d.guid){existing=n;break;}
        std::size_t index=0;
        if(existing)index=*existing;
        else {std::optional<std::size_t> hole;for(std::size_t n=0;n<out.entries.size();++n)if(!out.entries[n].record)hole=n;
            index=hole.value_or(out.entries.size());if(index>=4096||reserved.count(static_cast<std::int16_t>(index+1)))throw FormatError("hydrated catalog allocator cannot reuse a protected historical neighbor ID");
            NeighborRecord r{};r.version=10;r.unknown3=0;r.name=native_personal_catalog_name(d.source,d.asset);if(r.name.size()%2==0)r.name_padding=0;r.person_mode=0;r.neighbor_id=static_cast<std::int16_t>(index+1);r.guid=d.guid;r.unknown_neg_one=-1;
            if(index==out.entries.size())out.entries.push_back({1,std::move(r)});else out.entries[index]={1,std::move(r)};
        }
        bindings.push_back({d.guid,static_cast<std::int16_t>(index+1)});
    }
    return out;
}
}
bool native_personal_catalog_type(std::uint16_t t) noexcept {return t==2||t==0x200||t==0x400||t==0x800||t==0x1000;}
std::string native_personal_catalog_name(const std::string& source,const std::string& asset){
    path(source,false);if(!asset.empty())path(asset,true);const auto& s=asset.empty()?source:asset;
    const auto slash=s.find_last_of("/\\");const auto start=slash==std::string::npos?0:slash+1;const auto dot=s.find_last_of('.');const auto end=dot==std::string::npos||dot<start?s.size():dot;
    auto name=s.substr(start,end-start);if(name.empty()||name.size()>256)throw FormatError("hydrated catalog resource basename is empty or oversized");return name;
}
void validate_native_personal_catalog(const NativePersonalCatalog& c){
    if(c.version!=1||!nonzero(c.neighborhood_identity)||c.definitions.size()>4096||c.bindings.size()!=c.definitions.size())throw FormatError("hydrated catalog profile/count unsupported");
    std::set<std::uint32_t> guids;std::set<std::int16_t> nids;std::size_t bytes=0;std::optional<std::uint32_t> previous;
    for(std::size_t n=0;n<c.definitions.size();++n){const auto& d=c.definitions[n];const auto& b=c.bindings[n];
        if(d.provider_ordinal>=8192||!d.guid||!native_personal_catalog_type(d.object_type)||!nonzero(d.fingerprint)||!guids.insert(d.guid).second||(previous&&d.provider_ordinal<=*previous)||b.guid!=d.guid||b.neighbor_id<=0||b.neighbor_id>4096||!nids.insert(b.neighbor_id).second)throw FormatError("hydrated catalog definition/order/binding conflicts");
        previous=d.provider_ordinal;(void)native_personal_catalog_name(d.source,d.asset);bytes+=d.source.size()+d.asset.size();if(bytes>2u*1024u*1024u)throw FormatError("hydrated catalog cumulative provenance exceeds2MiB");
    }
}
NativePersonalCatalog build_native_personal_catalog(const Nbrs& n,const std::array<std::uint8_t,32>& h,const std::vector<NativePersonalCatalogDefinition>& defs,const std::set<std::int16_t>& reserved){
    NativePersonalCatalog c;c.neighborhood_identity=h;c.definitions=defs;
    // Validate provider metadata BEFORE allocating any derived neighbor record.
    for(std::size_t i=0;i<defs.size();++i)c.bindings.push_back({defs[i].guid,static_cast<std::int16_t>(i+1)});
    validate_native_personal_catalog(c);c.bindings.clear();(void)derive(n,defs,reserved,c.bindings);validate_native_personal_catalog(c);return c;
}
Nbrs hydrate_native_personal_neighbors(const Nbrs& raw,const NativePersonalCatalog& c,const std::set<std::int16_t>& reserved){
    validate_native_personal_catalog(c);std::vector<NativePersonalCatalogBinding> actual;auto out=derive(raw,c.definitions,reserved,actual);
    if(actual.size()!=c.bindings.size())throw FormatError("hydrated catalog binding count changed");
    for(std::size_t i=0;i<actual.size();++i)if(actual[i].guid!=c.bindings[i].guid||actual[i].neighbor_id!=c.bindings[i].neighbor_id)throw FormatError("hydrated catalog allocation receipt differs from immutable source");
    return out;
}
std::optional<std::int16_t> native_personal_catalog_lookup(const NativePersonalCatalog& c,std::uint32_t guid){for(std::size_t i=0;i<c.definitions.size();++i)if(c.definitions[i].object_type==2&&c.bindings[i].guid==guid)return c.bindings[i].neighbor_id;return {};}
std::vector<std::uint8_t> encode_native_personal_catalog(const NativePersonalCatalog& c){validate_native_personal_catalog(c);std::vector<std::uint8_t> b;const auto put=[&](std::uint64_t x,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(static_cast<std::uint8_t>(x>>(8*i)));};put(c.version,4);b.insert(b.end(),c.neighborhood_identity.begin(),c.neighborhood_identity.end());put(c.definitions.size(),4);
    for(std::size_t i=0;i<c.definitions.size();++i){const auto& d=c.definitions[i];put(d.provider_ordinal,4);put(d.guid,4);put(d.object_type,2);put(d.definition_chunk_id,2);put(d.source.size(),2);b.insert(b.end(),d.source.begin(),d.source.end());put(d.asset.size(),2);b.insert(b.end(),d.asset.begin(),d.asset.end());b.insert(b.end(),d.fingerprint.begin(),d.fingerprint.end());put(static_cast<std::uint16_t>(c.bindings[i].neighbor_id),2);}return b;
}
NativePersonalCatalog decode_native_personal_catalog(const std::vector<std::uint8_t>& b){if(b.size()>3u*1024u*1024u)throw FormatError("hydrated catalog wire exceeds3MiB");std::size_t at=0;const auto get=[&](unsigned n){if(n>b.size()-at)throw FormatError("truncated hydrated catalog");std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t{b[at++]}<<(8*i);return v;};NativePersonalCatalog c;c.version=static_cast<std::uint32_t>(get(4));for(auto& x:c.neighborhood_identity)x=static_cast<std::uint8_t>(get(1));const auto count=get(4);if(count>4096)throw FormatError("hydrated catalog count exceeds4096");
    const auto string=[&]{const auto n=get(2);if(n>4096||n>b.size()-at)throw FormatError("hydrated catalog string outside bounded wire");std::string s(reinterpret_cast<const char*>(b.data()+at),static_cast<std::size_t>(n));at+=n;return s;};
    for(std::uint64_t i=0;i<count;++i){NativePersonalCatalogDefinition d;d.provider_ordinal=static_cast<std::uint32_t>(get(4));d.guid=static_cast<std::uint32_t>(get(4));d.object_type=static_cast<std::uint16_t>(get(2));d.definition_chunk_id=static_cast<std::uint16_t>(get(2));d.source=string();d.asset=string();for(auto& x:d.fingerprint)x=static_cast<std::uint8_t>(get(1));const auto nid=static_cast<std::int16_t>(get(2));c.bindings.push_back({d.guid,nid});c.definitions.push_back(std::move(d));}if(at!=b.size())throw FormatError("hydrated catalog trailing bytes");validate_native_personal_catalog(c);return c;
}
} //namespace odh
