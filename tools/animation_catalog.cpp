#include "animation_catalog.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
namespace odh::host {
namespace {
std::string fold(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);return s;}
std::string basename(std::string s){for(auto& c:s)if(c=='\\')c='/';const auto p=s.find_last_of('/');return fold(p==s.npos?s:s.substr(p+1));}
bool suffix(const std::string& s,const std::string& end){return s.size()>=end.size()&&s.compare(s.size()-end.size(),end.size(),end)==0;}
void charge(std::uint64_t& used,std::uint64_t amount,std::uint64_t limit,const char* what){if(used>limit||amount>limit-used)throw FormatError(what);used+=amount;}
std::uint64_t properties_bytes(const AnimationProperties& p){std::uint64_t n=p.size()*sizeof(AnimationProperties::value_type);for(const auto& v:p)n+=v.first.size()+v.second.size();return n;}
std::uint64_t header_bytes(const CatalogAnimation& a){std::uint64_t n=sizeof(CatalogAnimation)+a.header.name.size()+a.header.xskill.size()+a.bcf.source.size()+a.bcf.entry.size();
    if(a.cfp)n+=a.cfp->source.size()+a.cfp->entry.size();
    n+=a.header.motions.size()*sizeof(BoneMotion);for(const auto& m:a.header.motions){n+=m.bone.size()+m.properties.size()*sizeof(AnimationProperties)+m.time_properties.size()*sizeof(std::vector<AnimationTimeProperty>);
        for(const auto& p:m.properties)n+=properties_bytes(p);
        for(const auto& g:m.time_properties){n+=g.size()*sizeof(AnimationTimeProperty);for(const auto& t:g)n+=properties_bytes(t.pairs);}}return n;}
}
struct AnimationCatalog::Impl {
    struct Source {std::string path;std::ifstream stream;std::unique_ptr<View> view;std::vector<FarEntry> entries;};
    struct Index {std::string key;std::uint32_t source,entry,animation;};
    struct Cached {std::size_t id;std::uint64_t metadata,samples;std::shared_ptr<const CatalogAnimation> header;std::shared_ptr<const CatalogDecoded> decoded;};
    struct LiveRef {std::size_t id;std::weak_ptr<const CatalogAnimation> header;std::weak_ptr<const CatalogDecoded> decoded;};
    struct Live {std::uint64_t headers=0,metadata=0,samples=0;};
    std::shared_ptr<Live> live=std::make_shared<Live>();
    CatalogLimits limits;CatalogUsage stats;std::vector<std::unique_ptr<Source>> sources;std::vector<Index> animations,cfps;std::vector<Cached> cache;std::vector<LiveRef> registry; // LRU front, newest back.
    explicit Impl(CatalogLimits l):limits(l){}
    void index(std::vector<Index>& into,std::string name,std::uint32_t s,std::uint32_t e,std::uint32_t a){charge(stats.name_bytes,name.size(),limits.name_bytes,"catalog cumulative names exceed budget");charge(stats.index_bytes,sizeof(Index),limits.index_bytes,"catalog index exceeds budget");into.push_back({std::move(name),s,e,a});}
    AnimationProvenance provenance(const Index& i)const {const auto& s=*sources.at(i.source);return {s.path,s.entries.at(i.entry).name,i.source,i.entry};}
    View payload(const Index& i)const {const auto& s=*sources.at(i.source);const auto& e=s.entries.at(i.entry);return s.view->subview(e.offset,e.length);}
    const Index* unique(const std::vector<Index>& items,const std::string& key)const {auto it=std::lower_bound(items.begin(),items.end(),key,[](const Index& i,const auto& k){return i.key<k;});if(it==items.end()||it->key!=key)return nullptr;if(it+1!=items.end()&&(it+1)->key==key)throw FormatError("ambiguous catalog resource name");return &*it;}
    void clean_registry(){
        const auto before=registry.size();registry.erase(std::remove_if(registry.begin(),registry.end(),[](const auto& r){return r.header.expired();}),registry.end());
        stats.index_bytes-=(before-registry.size())*sizeof(LiveRef);stats.registry_entries=registry.size();stats.registry_bytes=registry.size()*sizeof(LiveRef);
    }
    void evict(){cache.erase(cache.begin());stats.cache_records=cache.size();clean_registry();}
    std::size_t load(const std::string& name){const auto* i=unique(animations,fold(name));if(!i)throw FormatError("animation internal name not found");return load(static_cast<std::size_t>(i-animations.data()));}
    std::size_t load(std::size_t id){clean_registry();const auto& i=animations.at(id);
        for(std::size_t n=0;n<cache.size();++n)if(cache[n].id==id){auto c=std::move(cache[n]);cache.erase(cache.begin()+static_cast<std::ptrdiff_t>(n));cache.push_back(std::move(c));return cache.size()-1;}
        for(const auto& r:registry)if(r.id==id){auto header=r.header.lock();auto decoded=r.decoded.lock();
            while(cache.size()>=limits.cache_records)evict();
            cache.push_back({id,0,0,std::move(header),std::move(decoded)});stats.cache_records=cache.size();return cache.size()-1;}
        auto collection=read_bcf_animations(payload(i),limits.parser);auto a=std::make_unique<CatalogAnimation>();a->header=std::move(collection.animations.at(i.animation));a->bcf=provenance(i);a->bcf_remaining_bytes=collection.remaining_bytes;
        if(const auto* c=unique(cfps,basename(a->header.xskill+".cfp")))a->cfp=provenance(*c);
        const auto bytes=header_bytes(*a);if(!limits.cache_records||bytes>limits.metadata_bytes)throw FormatError("animation metadata exceeds cache budget");
        while(live->headers>=limits.cache_records||bytes>limits.metadata_bytes-live->metadata){if(cache.empty())throw FormatError("animation cache budget pinned by live handles");evict();}
        if(sizeof(LiveRef)>limits.index_bytes-stats.index_bytes)throw FormatError("live animation registry exceeds index budget");
        auto ledger=live;++live->headers;live->metadata+=bytes;std::shared_ptr<const CatalogAnimation> owned(a.release(),[ledger,bytes](const CatalogAnimation* value){delete value;--ledger->headers;ledger->metadata-=bytes;});
        registry.push_back({id,owned,{}});stats.index_bytes+=sizeof(LiveRef);stats.registry_entries=registry.size();stats.registry_bytes=registry.size()*sizeof(LiveRef);
        cache.push_back({id,bytes,0,std::move(owned),{}});stats.cache_records=cache.size();return cache.size()-1;
    }
    std::size_t selected(const AnimationHeader& header){
        clean_registry();
        for(const auto& r:registry){const auto owned=r.header.lock();if(owned && &owned->header==&header)return r.id;}
        throw FormatError("animation header is not a live selection from this catalog");
    }
    std::shared_ptr<const CatalogDecoded> decode(std::size_t pos){if(cache[pos].decoded)return cache[pos].decoded;const auto header=cache[pos].header;if(!header->cfp)throw FormatError("CFP resource missing for animation");
        const auto samples=static_cast<std::uint64_t>(header->header.translations)*3+static_cast<std::uint64_t>(header->header.rotations)*4;if(samples>limits.cfp_samples)throw FormatError("CFP samples exceed cumulative cache budget");
        while(samples>limits.cfp_samples-live->samples||sizeof(CatalogDecoded)>limits.metadata_bytes-live->metadata){if(cache.size()<=1)throw FormatError("CFP cache budget pinned by live handles");evict();--pos;}
        const auto& provenance=*header->cfp;Index i{{},provenance.source_index,provenance.entry_index,0};auto pending=std::make_unique<CatalogDecoded>();auto ledger=live;live->samples+=samples;live->metadata+=sizeof(CatalogDecoded);std::shared_ptr<CatalogDecoded> decoded(pending.release(),[ledger,samples](const CatalogDecoded* value){delete value;ledger->samples-=samples;ledger->metadata-=sizeof(CatalogDecoded);});decoded->animation=header;decoded->planes=read_cfp(payload(i),header->header.translations,header->header.rotations,limits.parser);cache[pos].decoded=decoded;cache[pos].samples=samples;for(auto& r:registry)if(r.id==cache[pos].id){r.decoded=decoded;break;}return decoded;
    }
};
AnimationCatalog::AnimationCatalog(const std::vector<std::string>& paths,CatalogLimits limits):impl_(std::make_unique<Impl>(limits)){
    auto& p=*impl_;if(paths.size()>limits.sources)throw FormatError("catalog source count exceeds budget");
    for(const auto& path:paths){auto s=std::make_unique<Impl::Source>();s->path=path;s->stream.open(path,std::ios::binary);if(!s->stream)throw FormatError("cannot open catalog FAR");s->view=std::make_unique<View>(s->stream,std::filesystem::file_size(path));
        Limits far;far.far_entries=std::min(limits.far_entries,8192U);far.far_name_bytes=limits.name_bytes-p.stats.name_bytes;s->entries=far1_index(*s->view,far);
        charge(p.stats.name_bytes,path.size(),limits.name_bytes,"catalog source names exceed budget");charge(p.stats.index_bytes,sizeof(Impl::Source),limits.index_bytes,"catalog index exceeds budget");
        const auto si=static_cast<std::uint32_t>(p.sources.size());p.sources.push_back(std::move(s));const auto& source=*p.sources.back();p.stats.far_entries+=source.entries.size();
        for(std::size_t ei=0;ei<source.entries.size();++ei){const auto& e=source.entries[ei];charge(p.stats.name_bytes,e.name.size(),limits.name_bytes,"catalog cumulative names exceed budget");charge(p.stats.index_bytes,sizeof(FarEntry),limits.index_bytes,"catalog index exceeds budget");const auto key=basename(e.name);
            if(suffix(key,".cfp")){p.index(p.cfps,key,si,static_cast<std::uint32_t>(ei),0);++p.stats.cfp_entries;}
            else if(suffix(key,".bcf")){const auto v=source.view->subview(e.offset,e.length);if(v.le32(0)!=0||v.le32(4)!=0){++p.stats.bcf_skipped_profile;continue;}
                const auto table=read_bcf_animations(v,limits.parser);++p.stats.bcf_supported;for(std::size_t ai=0;ai<table.animations.size();++ai)p.index(p.animations,fold(table.animations[ai].name),si,static_cast<std::uint32_t>(ei),static_cast<std::uint32_t>(ai));}
        }
    }
    const auto sort=[](auto& items){std::sort(items.begin(),items.end(),[](const auto& a,const auto& b){return a.key<b.key;});};sort(p.animations);sort(p.cfps);p.stats.sources=p.sources.size();p.stats.animations=p.animations.size();
}
AnimationCatalog::~AnimationCatalog()=default;
AnimationCatalog::AnimationCatalog(AnimationCatalog&&) noexcept=default;
AnimationCatalog& AnimationCatalog::operator=(AnimationCatalog&&) noexcept=default;
AnimationCatalog::Impl& AnimationCatalog::state()const {if(!impl_)throw FormatError("moved-from animation catalog");return *impl_;}
std::shared_ptr<const CatalogAnimation> AnimationCatalog::resolve(const std::string& name){auto& p=state();return p.cache[p.load(name)].header;}
std::shared_ptr<const CatalogDecoded> AnimationCatalog::decode(const std::string& name){auto& p=state();return p.decode(p.load(name));}
std::shared_ptr<const CatalogDecoded> AnimationCatalog::decode(const AnimationHeader& selected){auto& p=state();return p.decode(p.load(p.selected(selected)));}
CatalogUsage AnimationCatalog::usage()const {auto& p=state();p.clean_registry();auto stats=p.stats;stats.live_headers=p.live->headers;stats.metadata_bytes=p.live->metadata;stats.cfp_samples=p.live->samples;return stats;}
}
