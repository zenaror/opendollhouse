#include "animation_catalog.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace {
int checks=0;
void require(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const odh::FormatError&){rejected=true;}require(rejected,"expected explicit format/budget rejection");}
void put(std::string& b,std::uint32_t v){for(int i=0;i<4;++i)b+=static_cast<char>((v>>(8*i))&255);}
void text(std::string& b,const std::string& v){b+=static_cast<char>(v.size());b+=v;}
std::string bcf(const std::string& name,const std::string& skill="Skill"){std::string b;put(b,0);put(b,0);put(b,1);text(b,name);text(b,skill);for(auto v:{0U,0U,0U,1U,1U,0U})put(b,v);return b;}
// Real BCF section layouts, including the mixed skeleton+appearance profile.
std::string prefixed_bcf(bool skeleton,bool appearance,const std::string& animation={}){
    std::string b;put(b,skeleton?1:0);
    if(skeleton){text(b,"rig");b+=char(0);b+=char(0);} // zero bones
    put(b,appearance?1:0);
    if(appearance){text(b,"skin");put(b,0);put(b,0);put(b,0);} // zero bindings
    if(animation.empty())put(b,0);else b+=bcf(animation).substr(8);
    return b;
}
std::string far(const std::vector<std::pair<std::string,std::string>>& entries){std::string b="FAR!byAZ";put(b,1);put(b,0);std::vector<std::uint32_t> offsets;for(const auto& e:entries){offsets.push_back(static_cast<std::uint32_t>(b.size()));b+=e.second;}auto offset=static_cast<std::uint32_t>(b.size());for(int i=0;i<4;++i)b[12+i]=static_cast<char>((offset>>(8*i))&255);put(b,static_cast<std::uint32_t>(entries.size()));for(std::size_t i=0;i<entries.size();++i){const auto& e=entries[i];put(b,static_cast<std::uint32_t>(e.second.size()));put(b,static_cast<std::uint32_t>(e.second.size()));put(b,offsets[i]);put(b,static_cast<std::uint32_t>(e.first.size()));b+=e.first;}return b;}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("odh-catalog-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Temp(){std::filesystem::create_directory(path);}~Temp(){std::filesystem::remove_all(path);}std::string write(const std::string& name,const std::string& bytes){const auto p=path/name;std::ofstream s(p,std::ios::binary);s.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));return p.string();}};
void usage(const odh::host::CatalogUsage& u){std::cout<<"sources="<<u.sources<<" far_entries="<<u.far_entries<<" supported_bcf="<<u.bcf_supported<<" skipped_profile_bcf="<<u.bcf_skipped_profile<<" animations="<<u.animations<<" cfp_entries="<<u.cfp_entries<<" bcf_candidates="<<u.bcf_candidates<<" bcf_overrides="<<u.bcf_overrides<<" animation_overrides="<<u.animation_overrides<<" cfp_candidates="<<u.cfp_candidates<<" cfp_overrides="<<u.cfp_overrides<<" name_bytes="<<u.name_bytes<<" index_bytes="<<u.index_bytes<<" cache_records="<<u.cache_records<<" registry_entries="<<u.registry_entries<<" registry_bytes="<<u.registry_bytes<<" live_headers="<<u.live_headers<<" metadata_bytes="<<u.metadata_bytes<<" cfp_samples="<<u.cfp_samples<<'\n';}
std::string animation_set(const std::vector<std::pair<std::string,std::string>>& names){
    std::string out;put(out,0);put(out,0);put(out,static_cast<std::uint32_t>(names.size()));
    for(const auto& item:names)out+=bcf(item.first,item.second).substr(12);return out;
}
void ts1_projection_budget(Temp& t){
    using namespace odh::host;const auto policy=CatalogNamePolicy::ts1_provider_order;
    constexpr unsigned count=8;
    // Equal source path lengths, filenames and inventory shape isolate the
    // projection credit: neither parser work nor active headers differ here.
    for(const std::string extension:{"bcf","cfp","cmx"}){
        std::vector<std::pair<std::string,std::string>> duplicate,distinct;
        const auto payload=extension=="bcf"?prefixed_bcf(false,false):
            extension=="cfp"?std::string(7,127):std::string("unclassified");
        for(unsigned i=0;i<count;++i){const auto prefix="dir"+std::to_string(i)+"/";
            duplicate.push_back({prefix+"name0000."+extension,payload});
            distinct.push_back({prefix+"name"+std::to_string(1000+i)+"."+extension,payload});
        }
        const auto a=t.write("budget-"+extension+"-a.far",far(duplicate));
        const auto b=t.write("budget-"+extension+"-b.far",far(distinct));
        AnimationCatalog projected({a},{},policy),unprojected({b},{},policy);
        const auto actual=projected.usage(),control=unprojected.usage();
        require(actual.index_bytes==control.index_bytes&&actual.name_bytes==control.name_bytes,
            "BCF/CFP/CMX projection retains every admitted record/key reservation while original buffers remain resident");
        require(actual.far_entries==count&&actual.animations==0&&
            (extension!="bcf"||actual.bcf_overrides==count-1)&&
            (extension!="cfp"||actual.cfp_overrides==count-1),
            "conservative reservation accounting does not change effective provider selection");
    }
    std::vector<std::pair<std::string,std::string>> duplicate,distinct;
    for(unsigned i=0;i<count;++i){duplicate.push_back({"Repeated","Skill"});distinct.push_back({"Clp"+std::to_string(10000+i),"Skill"});}
    const auto a=t.write("budget-internal-a.far",far({{"bundle.bcf",animation_set(duplicate)},{"Skill.cfp",std::string(7,127)}}));
    const auto b=t.write("budget-internal-b.far",far({{"bundle.bcf",animation_set(distinct)},{"Skill.cfp",std::string(7,127)}}));
    AnimationCatalog projected({a},{},policy),unprojected({b},{},policy);
    const auto actual=projected.usage(),control=unprojected.usage();
    require(actual.index_bytes==control.index_bytes&&actual.name_bytes==control.name_bytes&&
        actual.animation_overrides==count-1&&actual.animations==1&&control.animations==count,
        "internal animation projection keeps raw admission charges, independently from effective namespace size");
    auto limits=CatalogLimits{};limits.index_bytes=control.index_bytes;limits.name_bytes=control.name_bytes;
    AnimationCatalog exact({a},limits,policy);
    require(exact.usage().index_bytes==limits.index_bytes&&exact.usage().name_bytes==limits.name_bytes,
        "exact admitted reservation limit constructs projected namespace without fictitious credit");
    rejects([&]{exact.resolve("Repeated");});rejects([&]{exact.resolve_if_present("Repeated");});
    require(exact.usage().registry_entries==0&&exact.usage().live_headers==0&&exact.usage().cfp_samples==0&&
        exact.usage().index_bytes==limits.index_bytes,
        "discarded views cannot finance a later live-header registry record, and failed admission leaves charges intact");
}

void ts1_provider_projection(Temp& t){
    using namespace odh::host;const auto policy=CatalogNamePolicy::ts1_provider_order;
    const std::string planes_a(7,127),planes_b(7,128);
    const auto first=t.write("provider-first.far",far({{"folder/Shared.BCF",bcf("Old","Skill")},{"Skill.cfp",planes_a}}));
    const auto last=t.write("provider-last.far",far({{"another\\SHARED.bcf",bcf("New","Skill")},{"nested/skill.CFP",planes_b}}));
    for(const bool reverse:{false,true}){
        AnimationCatalog c(reverse?std::vector<std::string>{last,first}:std::vector<std::string>{first,last},{},policy);
        const auto winner=c.resolve(reverse?"OLD":"new");
        require(winner->bcf.source==(reverse?first:last)&&winner->cfp->source==(reverse?first:last),
            "TS1 BCF and independent CFP basename overwrite follow admitted order with unequal payloads");
        require(!c.resolve_if_present(reverse?"New":"Old"),"shadowed BCF internal names are not added to TS1 namespace");
        const auto u=c.usage();require(u.bcf_candidates==2&&u.bcf_supported==1&&u.bcf_overrides==1&&
            u.cfp_candidates==2&&u.cfp_entries==1&&u.cfp_overrides==1&&u.animations==1,
            "provider selection reports original candidates and effective selected namespace");
        require(c.decode(winner->header)->animation==winner,"selected immutable header decodes exact provider, without name re-resolution");
    }
    {
        const auto same=t.write("provider-identical.far",far({{"Shared.bcf",bcf("Old","Skill")},{"Skill.cfp",planes_a}}));
        AnimationCatalog c({first,same},{},policy);require(c.resolve("old")->bcf.source==same&&c.decode("OLD")->planes.bits[0].size()==1,
            "identical copies use provider projection and preserve selected provenance rather than special byte deduplication");
        const auto baseline=t.write("provider-slots-a.far",far({{"one.bcf",bcf("Shared","OneOld")},{"two.bcf",bcf("Shared","Two")}}));
        const auto replacement=t.write("provider-slots-b.far",far({{"ONE.BCF",bcf("Shared","OneNew")}}));
        AnimationCatalog slots({baseline,replacement},{},policy);
        require(slots.resolve("shared")->header.xskill=="Two"&&slots.resolve("shared")->bcf.entry=="two.bcf"&&slots.usage().animation_overrides==1,
            "overwriting filename keeps first registration slot before internal-name host scan");
    }
    {
        const auto a=t.write("host-a.far",far({{"alpha.bcf",bcf("Clash","A")},{"Skill.cfp",planes_a}}));
        const auto b=t.write("host-b.far",far({{"beta.bcf",animation_set({{"CLASH","First"},{"clash","Second"}})},{"Skill.cfp",planes_b}}));
        AnimationCatalog forward({a,b},{},policy),reverse({b,a},{},policy);
        require(forward.resolve("clash")->header.xskill=="First"&&forward.resolve("clash")->header.name=="CLASH"&&
            forward.resolve("clash")->bcf.source==b&&forward.usage().animation_overrides==2&&reverse.resolve("clash")->header.xskill=="A",
            "last selected host wins, while Get returns FIRST matching internal record in that host");
        const auto order=t.write("provider-order-inside-far.far",far({{"one/part.bcf",bcf("Earlier")},{"two/PART.BCF",bcf("Later")}}));
        AnimationCatalog intra({order},{},policy);require(intra.resolve("later")->bcf.entry_index==1&&!intra.resolve_if_present("earlier"),
            "distinct full paths sharing basename follow exact FAR entry registration order");
        const auto exact=t.write("provider-exact-far-name.far",far({{"part.bcf",bcf("FirstPayload","Skill")},{"part.bcf",bcf("LaterReference","Skill")},
            {"Skill.cfp",planes_a},{"Skill.cfp",planes_b.substr(0,6)}}));
        AnimationCatalog source_far({exact},{},policy);const auto chosen=source_far.decode("firstpayload");
        require(!source_far.resolve_if_present("LaterReference")&&chosen->animation->bcf.entry_index==0&&chosen->animation->cfp->entry_index==2&&
            chosen->planes.bits[0].size()==1,
            "FAR1 GetEntry reads FIRST exact full filename payload for selected duplicate reference, with actual-byte provenance");
    }
    {
        const auto good=t.write("selected-good.far",far({{"clip.bcf",bcf("Good","Skill")},{"Skill.cfp",planes_a}}));
        const auto bad=t.write("selected-bad.far",far({{"CLIP.BCF",bcf("Broken").substr(0,9)}}));
        rejects([&]{AnimationCatalog selected_bad({good,bad},{},policy);});
        AnimationCatalog shadowed_bad({bad,good},{},policy);
        require(shadowed_bad.resolve("good")->bcf.source==good,"inactive overridden BCF is not decoded, as source ListGeneric contains only selected values");
        const auto cfp_bad=t.write("selected-cfp-bad.far",far({{"x/skill.cfp",planes_a.substr(0,6)}}));
        AnimationCatalog bad_planes({good,cfp_bad},{},policy);const auto selected=bad_planes.resolve("good");
        require(selected->cfp&&selected->cfp->source==cfp_bad,"selected malformed CFP remains selected and is not silently replaced");
        rejects([&]{bad_planes.decode(selected->header);});require(bad_planes.usage().cfp_samples==0,"failed selected CFP leaves no phantom live sample charge");
        AnimationCatalog good_planes({cfp_bad,good},{},policy);require(good_planes.decode("good")->animation->cfp->source==good,
            "explicit reversed provider order selects actual valid CFP instead of a failure-triggered fallback");
        const auto bad_exact=t.write("bad-exact-name.far",far({{"part.bcf","bad"},{"part.bcf",bcf("Good")}}));
        rejects([&]{AnimationCatalog malformed_first_payload({bad_exact},{},policy);});
    }
    {
        const auto unknown=t.write("provider-unknown-cmx.far",far({{"known.bcf",bcf("Known")},{"override.cmx","unclassified"}}));
        AnimationCatalog c({unknown},{},policy);rejects([&]{c.resolve("Known");});rejects([&]{c.resolve_if_present("missing");});
        const auto excluded=t.write("provider-excluded.far",far({{"Globals-bad.bcf","bad"},{"globals-unknown.cmx","unknown"},{"known.bcf",bcf("Known")}}));
        AnimationCatalog skip({excluded},{},policy);require(skip.resolve("known")&&!skip.resolve_if_present("missing")&&skip.usage().bcf_skipped_profile==0,
            "source BuildDictionary excludes globals basenames before selected provider decoding");
        const auto nested=t.write("provider-xskill-key.far",far({{"nested.bcf",bcf("Nested","dir\\Skill")},{"Skill.cfp",planes_a}}));
        AnimationCatalog exact_skill({nested},{},policy);require(!exact_skill.resolve("nested")->cfp,
            "TS1 CFP Get uses lowercased XSkill key without inventing basename normalization for a path-bearing key");
        rejects([&]{exact_skill.decode("nested");});
    }
    {
        auto limits=CatalogLimits{};limits.cache_records=1;
        AnimationCatalog c({first,last},limits,policy);auto held=c.resolve("new");
        const auto absent=t.write("projection-extra.far",far({{"extra.bcf",bcf("Extra","Skill")},{"Skill.cfp",planes_a}}));
        AnimationCatalog pinned({first,last,absent},limits,policy);auto header=pinned.resolve("new");
        rejects([&]{pinned.resolve("extra");});
        require(pinned.resolve("NEW")==header&&pinned.usage().live_headers==1,"projected header identity survives cache eviction under pinned cap");
        auto decoded=pinned.decode(header->header);require(decoded->animation==header&&decoded->animation->bcf.source==last&&decoded->animation->cfp->source==absent,
            "selected BCF and independently overridden CFP provenance remain pinned through registry reuse");
        auto copied=header->header;rejects([&]{pinned.decode(copied);});rejects([&]{pinned.decode(held->header);});
        limits={};limits.index_bytes=1;rejects([&]{AnimationCatalog over({first,last},limits,policy);});
        limits={};limits.name_bytes=1;rejects([&]{AnimationCatalog over({first,last},limits,policy);});
        limits={};limits.metadata_bytes=1;AnimationCatalog tiny({first,last},limits,policy);rejects([&]{tiny.resolve("new");});
        limits={};limits.cfp_samples=6;AnimationCatalog samples({first,last},limits,policy);rejects([&]{samples.decode("new");});
        limits={};limits.parser.text_bytes=2;rejects([&]{AnimationCatalog parser({first,last},limits,policy);});
        std::shared_ptr<const CatalogDecoded> survivor;{AnimationCatalog transient({first,last},{},policy);survivor=transient.decode("new");}
        require(survivor->animation->bcf.source==last&&survivor->animation->cfp->source==last&&survivor->planes.bits[0].size()==1,
            "projected owned handles retain actual selected bytes after catalog destruction");
    }
}

}
int main(int argc,char** argv){try{
    using namespace odh::host;
    if(argc>=4&&std::string(argv[1])=="--ts1-name"){
        std::vector<std::string> admitted;for(int i=3;i<argc;++i)admitted.emplace_back(argv[i]);
        AnimationCatalog native(admitted,{},CatalogNamePolicy::ts1_provider_order);usage(native.usage());
        const auto selected=native.resolve(argv[2]);const auto decoded=native.decode(selected->header);
        require(decoded->animation==selected&&selected->cfp.has_value(),"native selected provider/header/CFP identity");
        std::cout<<selected->header.name<<" BCF="<<selected->bcf.source<<":"<<selected->bcf.entry<<"["<<selected->bcf.entry_index
            <<"] CFP="<<selected->cfp->source<<":"<<selected->cfp->entry<<"["<<selected->cfp->entry_index
            <<"] remaining="<<decoded->planes.remaining_bytes<<'\n';usage(native.usage());return 0;
    }
    if(argc==3){AnimationCatalog native({argv[1],argv[2]});usage(native.usage());auto decoded=native.decode("a2o-idle-neutral-trans-stand-lstand");require(decoded->animation->cfp.has_value(),"native CFP exists");require(decoded->planes.bits[0].size()==90&&decoded->planes.bits[3].size()==1305,"native planes");usage(native.usage());std::cout<<decoded->animation->bcf.source<<":"<<decoded->animation->bcf.entry<<" -> "<<decoded->animation->cfp->entry<<" remaining="<<decoded->planes.remaining_bytes<<'\n';return 0;}
    Temp t;ts1_projection_budget(t);ts1_provider_projection(t);const std::string cfp(7,127);std::string mixed;put(mixed,1);put(mixed,0);
    const auto a=t.write("one.far",far({{"Animation\\NotClip.cmx.bcf",bcf("Clip")},{"rig.cmx",mixed},{"unused.bcf",bcf("Other","OtherSkill")}}));
    const auto b=t.write("two.far",far({{"Nested/Skill.CFP",cfp},{"other.cmx.bcf",bcf("Third","Nested\\OtherSkill")},{"CFP\\OtherSkill.cfp",cfp}}));
    AnimationCatalog catalog({a,b});const auto stats=catalog.usage();require(stats.sources==2&&stats.bcf_supported==3&&stats.bcf_skipped_profile==1&&stats.animations==3,"profiles/counts");require(stats.metadata_bytes==0&&stats.cfp_samples==0,"lazy cache");
    auto exact_limits=CatalogLimits{};exact_limits.name_bytes=stats.name_bytes;exact_limits.index_bytes=stats.index_bytes;AnimationCatalog exact({a,b},exact_limits);require(exact.usage().name_bytes==stats.name_bytes,"cumulative exact name/index budgets");--exact_limits.name_bytes;rejects([&]{AnimationCatalog over({a,b},exact_limits);});exact_limits.name_bytes=stats.name_bytes;--exact_limits.index_bytes;rejects([&]{AnimationCatalog over({a,b},exact_limits);});
    const auto resolved=catalog.resolve("cLiP");require(resolved->header.name=="Clip"&&resolved->bcf.entry=="Animation\\NotClip.cmx.bcf"&&resolved->cfp->source==b&&resolved->cfp->entry=="Nested/Skill.CFP","internal index and full cross-source provenance");require(catalog.usage().cfp_samples==0,"header-only does not decode CFP");
    auto decoded=catalog.decode("CLIP");require(decoded->planes.bits[6].size()==1&&decoded->animation==resolved,"lazy owned CFP");require(catalog.decode("clip")==decoded,"cached decode reuse");require(catalog.resolve("third")->cfp->entry=="CFP\\OtherSkill.cfp","Windows/POSIX skill basename");rejects([&]{catalog.resolve("NotClip.cmx");});
    const auto missing=t.write("missing.far",far({{"anything.bcf",bcf("Missing")}}));AnimationCatalog absent({missing});require(!absent.resolve("Missing")->cfp,"optional missing CFP");rejects([&]{absent.decode("Missing");});
    const auto duplicate=t.write("duplicate.far",far({{"other.bcf",bcf("cLiP")},{"other/Skill.cfp",cfp}}));AnimationCatalog ambiguous_animation({a,duplicate});rejects([&]{ambiguous_animation.resolve("clip");});AnimationCatalog ambiguous_cfp({a,b, t.write("dupcfp.far",far({{"else\\skill.cfp",cfp}}))});rejects([&]{ambiguous_cfp.resolve("clip");});
    auto limits=CatalogLimits{};limits.sources=1;rejects([&]{AnimationCatalog c({a,b},limits);});limits={};limits.far_entries=1;rejects([&]{AnimationCatalog c({a},limits);});limits={};limits.name_bytes=1;rejects([&]{AnimationCatalog c({a},limits);});limits={};limits.index_bytes=1;rejects([&]{AnimationCatalog c({a},limits);});limits={};limits.metadata_bytes=1;AnimationCatalog tiny({a,b},limits);rejects([&]{tiny.resolve("clip");});limits={};limits.cfp_samples=6;AnimationCatalog sample_limit({a,b},limits);rejects([&]{sample_limit.decode("clip");});
    limits={};limits.cache_records=1;AnimationCatalog pinned({a,b},limits);auto held=pinned.resolve("clip");rejects([&]{pinned.resolve("third");});require(pinned.usage().metadata_bytes>0&&pinned.usage().live_headers==1&&pinned.usage().cache_records==0,"evicted pinned header remains charged");require(pinned.resolve("clip")==held,"evicted pinned header identity reused at cap1");held.reset();require(pinned.resolve("third")->header.name=="Third","budget freed with handle");
    limits={};limits.cfp_samples=7;AnimationCatalog pinned_cfp({a,b},limits);auto held_planes=pinned_cfp.decode("clip");rejects([&]{pinned_cfp.decode("third");});require(pinned_cfp.usage().cfp_samples==7,"evicted pinned CFP remains charged");require(pinned_cfp.decode("clip")==held_planes,"evicted pinned CFP handle reused without recharge");held_planes.reset();require(pinned_cfp.decode("third")->planes.bits[0].size()==1,"CFP released budget");
    limits={};limits.cache_records=2;AnimationCatalog identity({a,b},limits);auto identity_a=identity.resolve("clip");identity.resolve("other");identity.resolve("third");require(identity.usage().live_headers==2,"LRU frees only unpinned header");require(identity.resolve("clip")==identity_a&&identity.usage().live_headers==2,"cap2 evicted live header reused without duplicate allocation");
    limits={};limits.cache_records=1;AnimationCatalog cycles({a,b},limits);const auto base_index=cycles.usage().index_bytes;for(int n=0;n<100;++n){cycles.resolve("clip");cycles.resolve("third");const auto u=cycles.usage();require(u.registry_entries==1&&u.registry_bytes==u.index_bytes-base_index,"expired registry bounded across cycles");}require(cycles.usage().live_headers==1,"registry cycles retain one header");
    rejects([&]{exact.resolve("clip");}); // Exact construction index budget has no live-registry room.
    std::vector<std::pair<std::string,std::string>> many;for(int n=0;n<17;++n)many.push_back({std::to_string(n)+".bcf",bcf("clip"+std::to_string(n))});AnimationCatalog sixteen({t.write("many.far",far(many))});std::vector<std::shared_ptr<const CatalogAnimation>> handles;for(int n=0;n<16;++n)handles.push_back(sixteen.resolve("clip"+std::to_string(n)));rejects([&]{sixteen.resolve("clip16");});handles.clear();require(sixteen.resolve("clip16")->header.name=="clip16","16 live metadata budget");
    auto moved=std::move(catalog);rejects([&]{catalog.resolve("clip");});rejects([&]{catalog.usage();});require(moved.resolve("clip")->header.name=="Clip","source/View stable across move");AnimationCatalog assigned({missing});assigned=std::move(moved);rejects([&]{moved.decode("clip");});require(assigned.decode("clip")->planes.bits[0].size()==1,"move assignment keeps owned streams");std::shared_ptr<const CatalogDecoded> survivor;{AnimationCatalog dying({a,b});survivor=dying.decode("clip");}require(survivor->planes.bits[0].size()==1&&survivor->animation->bcf.source==a,"owned handles after catalog destruction");
    const auto fixture=bcf("Truncated");for(std::size_t n=0;n<fixture.size();++n){const auto truncated=t.write("truncated.far",far({{"clip.bcf",fixture.substr(0,n)}}));rejects([&]{AnimationCatalog c({truncated});});}
    const auto broken=t.write("broken.far",far({{"clip.bcf",bcf("Broken")},{"skill.cfp",cfp.substr(0,6)}}));AnimationCatalog trunc_cfp({broken});require(trunc_cfp.resolve("broken")->cfp.has_value(),"header resolve does not decode malformed CFP");rejects([&]{trunc_cfp.decode("broken");});require(trunc_cfp.usage().cfp_samples==0,"failed CFP decode releases ledger");
    {
        const auto prefix=t.write("section-profiles.far",far({
            {"appearance.bcf",prefixed_bcf(false,true)}, {"skeleton.bcf",prefixed_bcf(true,false)},
            {"mixed.bcf",prefixed_bcf(true,true)}, {"animated-mixed.bcf",prefixed_bcf(true,true,"Prefixed")},
            {"Skill.cfp",cfp}}));
        AnimationCatalog complete_prefix({prefix});
        const auto u=complete_prefix.usage();
        require(u.bcf_supported==4&&u.bcf_skipped_profile==0&&u.animations==1,
            "validated skeleton/appearance/mixed zero-animation providers retain complete name authority");
        const auto selected=complete_prefix.resolve_if_present("prefixed");
        require(selected&&selected->header.name=="Prefixed"&&selected->bcf.entry=="animated-mixed.bcf"&&
            complete_prefix.decode("PREFIXED")->animation==selected,
            "shared prefix reader resolves real mixed animation header and CFP with exact identity");
        require(!complete_prefix.resolve_if_present("missing"),"complete prefixed providers prove named absence");
        auto small=CatalogLimits{};small.metadata_bytes=1;AnimationCatalog budget({prefix},small);
        rejects([&]{budget.resolve_if_present("Prefixed");});
        small={};small.parser.text_bytes=2;rejects([&]{AnimationCatalog text_budget({prefix},small);});
        const auto wire=prefixed_bcf(true,true,"Prefixed");
        for(std::size_t n=0;n<wire.size();++n){const auto broken=t.write("prefix-truncated.far",far({{"partial.bcf",wire.substr(0,n)}}));
            rejects([&]{AnimationCatalog bad({broken});});}
        auto bad_count=wire;bad_count[0]=65;
        const auto overflow=t.write("prefix-count.far",far({{"overflow.bcf",bad_count}}));
        rejects([&]{AnimationCatalog bad({overflow});});
        const auto unknown=t.write("unknown-profile.far",far({{"possibly-present.cmx","unknown text profile"},
            {"known.bcf",bcf("Known")}}));
        AnimationCatalog partial({unknown});require(partial.resolve_if_present("Known")->header.name=="Known",
            "known present remains usable alongside an unclassified possible provider");
        rejects([&]{partial.resolve_if_present("missing");});
        const auto trailing=t.write("unknown-trailing.far",far({{"known.bcf",bcf("Known")+"unknown"}}));
        AnimationCatalog unparsed({trailing});require(unparsed.resolve_if_present("Known")->header.name=="Known",
            "known animation survives unknown trailing metadata");
        rejects([&]{unparsed.resolve_if_present("missing");});
        const auto duplicate_prefix=t.write("duplicate-prefix.far",far({{"duplicate.bcf",prefixed_bcf(true,false,"Prefixed")}}));
        AnimationCatalog ambiguity({prefix,duplicate_prefix});rejects([&]{ambiguity.resolve_if_present("prefixed");});
        auto one=CatalogLimits{};one.cache_records=1;AnimationCatalog pinned_absence({prefix,unknown},one);
        auto pinned_header=pinned_absence.resolve_if_present("Prefixed");
        rejects([&]{pinned_absence.resolve_if_present("Known");});
        require(pinned_header->header.name=="Prefixed","pinned-header budget error never becomes absence");
    }
    {const auto clean=t.write("complete-lookup.far",far({{"clip.bcf",bcf("Lookup")}}));AnimationCatalog complete({clean});
     const auto clip=complete.resolve("Lookup");require(complete.resolve_if_present("lookup")==clip,"typed presence retains exact admitted header identity");
     require(!complete.resolve_if_present("missing"),"complete supported index proves named animation absence without synthesizing a clip");
     AnimationCatalog partial({a,b});rejects([&]{partial.resolve_if_present("missing");});
     AnimationCatalog no_sources({});rejects([&]{no_sources.resolve_if_present("missing");});}
    usage(stats);std::cout<<checks<<" animation catalog checks passed (synthetic)\n";return 0;
}catch(const std::exception& e){std::cerr<<"Animation catalog test failed: "<<e.what()<<'\n';return 1;}}
