#include "animation_catalog.hpp"
#include "scene_renderer.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

namespace {
int checks=0,cases=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F> void rejects(F f){bool caught=false;try{f();}catch(const odh::FormatError&){caught=true;}check(caught,"expected explicit rejection");}
template<class F> void run(const char* name,F f){f();++cases;std::cout<<"PASS "<<name<<'\n';}
void word(std::string& b,std::uint32_t n){for(int i=0;i<4;++i)b.push_back(static_cast<char>((n>>(8*i))&255));}
void scalar(std::string& b,float n){std::uint32_t bits;std::memcpy(&bits,&n,4);word(b,bits);}
void text(std::string& b,const std::string& s){b.push_back(static_cast<char>(s.size()));b+=s;}
using Entry=std::pair<std::string,std::string>;
std::string far(const std::vector<Entry>& entries){
    std::string b="FAR!byAZ";word(b,1);word(b,0);std::vector<std::uint32_t> offsets;
    for(const auto& e:entries){offsets.push_back(static_cast<std::uint32_t>(b.size()));b+=e.second;}
    const auto offset=static_cast<std::uint32_t>(b.size());for(int i=0;i<4;++i)b[12+i]=static_cast<char>((offset>>(8*i))&255);
    word(b,static_cast<std::uint32_t>(entries.size()));
    for(std::size_t i=0;i<entries.size();++i){const auto size=static_cast<std::uint32_t>(entries[i].second.size());word(b,size);word(b,size);word(b,offsets[i]);word(b,static_cast<std::uint32_t>(entries[i].first.size()));b+=entries[i].first;}
    return b;
}
std::string rig(){
    std::string b;word(b,1);text(b,"rig");b.push_back(1);b.push_back(0);text(b,"ROOT");text(b,"NULL");word(b,0);
    for(float n:{0.f,0.f,0.f,0.f,0.f,0.f,1.f})scalar(b,n);
    for(auto n:{1U,1U,0U})word(b,n);scalar(b,0);scalar(b,0);word(b,0);word(b,0);return b;
}
std::string appearance(){std::string b;word(b,0);word(b,1);text(b,"app");word(b,0);word(b,0);word(b,1);text(b,"ROOT");text(b,"mesh");word(b,0);word(b,0);word(b,0);return b;}
std::string mesh(){
    std::string b;text(b,"mesh");text(b,"x");word(b,1);text(b,"ROOT");
    for(auto n:{1U,0U,1U,2U,1U,0U,0U,3U,0U,0U,3U})word(b,n);
    for(int i=0;i<6;++i)scalar(b,.25f);word(b,0);word(b,3);
    for(const auto& p:std::array<std::array<float,3>,3>{{{-.7f,2,0},{.7f,2,0},{0,4,0}}}){for(float n:p)scalar(b,n);for(float n:{0.f,1.f,0.f})scalar(b,n);}
    return b;
}
std::string bitmap(){
    std::string b="BM";word(b,58);word(b,0);word(b,54);word(b,40);word(b,1);word(b,1);
    b.push_back(1);b.push_back(0);b.push_back(24);b.push_back(0);word(b,0);word(b,4);for(int i=0;i<4;++i)word(b,0);
    b.push_back(20);b.push_back(40);b.push_back(static_cast<char>(200));b.push_back(0);return b;
}
std::string animation(const std::string& name="clip"){
    std::string b;word(b,0);word(b,0);word(b,1);text(b,name);text(b,"pose");scalar(b,100);
    for(auto n:{0U,0U,2U,2U,1U})word(b,n);
    text(b,"ROOT");word(b,2);scalar(b,100);for(auto n:{1U,1U,0U,0U,0U,0U})word(b,n);return b;
}
std::string cfp(){
    std::string b;for(const auto& plane:std::array<std::array<float,2>,7>{{{0,1},{0,0},{0,0},{0,0},{0,0},{0,0},{1,1}}})
        for(float value:plane){b.push_back(static_cast<char>(255));scalar(b,value);}return b;
}
struct Fixture {
    std::filesystem::path directory=std::filesystem::temp_directory_path()/("odh-scene-cfp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string base,combined,header,planes,textures;
    Fixture(){std::filesystem::create_directory(directory);const std::vector<Entry> assets{{"rig.bcf",rig()},{"app.bcf",appearance()},{"mesh.bmf",mesh()}};
        base=write("base.far",far(assets));auto all=assets;all.push_back({"clip.bcf",animation()});all.push_back({"POSE.cfp",cfp()});combined=write("combined.far",far(all));
        header=write("headers.far",far({{"nested/clip.bcf",animation()},{"other.bcf",animation("other")}}));
        planes=write("planes.far",far({{"remote/POSE.CFP",cfp()}}));textures=write("textures.far",far({{"red.bmp",bitmap()}}));}
    ~Fixture(){std::error_code error;std::filesystem::remove_all(directory,error);}
    std::string write(const char* name,const std::string& bytes){auto p=directory/name;std::ofstream out(p,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();if(!out)throw std::runtime_error("fixture write failed");return p.string();}
    SceneConfig config(const std::string& source)const {SceneConfig c;c.animation_far=source;c.skeleton_entry="rig.bcf";c.skeleton_name="rig";c.textures_far=textures;c.parts.push_back({"app.bcf","app","mesh.bmf","red.bmp"});return c;}
};
SceneCfpResolver provider(odh::host::AnimationCatalog& catalog){return [&](const auto& selected){auto owned=catalog.decode(selected);return std::shared_ptr<const odh::CfpPlanes>(owned,&owned->planes);};}
odh::WorldProjection camera(){odh::WorldProjection c;c.width=160;c.height=160;return c;}
}

int main(){try{
    Fixture f;
    run("legacy-and-multifar-pixels-depth",[&]{
        odh::host::AnimationCatalog catalog({f.header,f.planes});const auto selected=catalog.resolve("clip");
        check(selected->bcf.source==f.header&&selected->bcf.entry=="nested/clip.bcf"&&selected->cfp->source==f.planes&&selected->cfp->entry=="remote/POSE.CFP","exact cross-FAR provenance");
        SceneRenderer legacy(f.config(f.combined)),remote(f.config(f.base),provider(catalog));SceneState state;state.animation=&selected->header;
        auto before=legacy.render(state,camera());
        for(double frame:{0.,.5,1.}){state.frame=frame;const auto expected=legacy.render(state,camera()),actual=remote.render(state,camera());
            check(actual.image.pixels==expected.image.pixels&&actual.image.depth==expected.image.depth,"provider pixels/depth differ from legacy");
            check(actual.image.tested_samples==expected.image.tested_samples&&actual.image.shaded_samples==expected.image.shaded_samples,"provider raster counts differ");}
        check(before.image.pixels!=legacy.render(state,camera()).image.pixels&&before.image.shaded_samples>0,"animated fixture must change visible pixels");
        check(legacy.cache_samples()==14&&legacy.cache_clips()==1&&remote.cache_samples()==0&&remote.cache_clips()==0,"only legacy renderer owns a CFP cache");
        auto decoded=catalog.decode(selected->header);check(decoded==catalog.decode("clip"),"selected decode duplicated cached record");
        check(catalog.usage().cfp_samples==14,"catalog must charge one decoded set of planes");
    });
    run("selected-header-identity",[&]{
        odh::host::AnimationCatalog catalog({f.header,f.planes}),foreign({f.header,f.planes});const auto selected=catalog.resolve("clip"),other=foreign.resolve("clip");
        const auto copy=selected->header;rejects([&]{catalog.decode(copy);});rejects([&]{catalog.decode(other->header);});
        check(catalog.usage().cfp_samples==0&&catalog.usage().cache_records==1,"identity refusals decoded or cached a foreign header");
        const auto owned=catalog.decode(selected->header);check(owned->animation==selected&&owned->animation->bcf.source_index==0&&owned->animation->cfp->source_index==1,"identity decode lost exact catalog selection");
    });
    run("missing-cfp-no-legacy-fallback",[&]{
        odh::host::AnimationCatalog catalog({f.header});auto selected=catalog.resolve("clip");SceneRenderer renderer(f.config(f.combined),provider(catalog));SceneState state;state.animation=&selected->header;
        rejects([&]{renderer.render(state,camera());});check(catalog.usage().cfp_samples==0&&renderer.cache_clips()==0,"missing external CFP silently used legacy archive");
    });
    run("ambiguous-source-refused",[&]{
        const auto duplicate=f.write("duplicate.far",far({{"other/pose.cfp",cfp()}}));odh::host::AnimationCatalog catalog({f.header,f.planes,duplicate});
        rejects([&]{catalog.resolve("clip");});check(catalog.usage().cfp_samples==0&&catalog.usage().cache_records==0&&catalog.usage().live_headers==0,"ambiguous CFP published cache state");
    });
    run("malformed-cfp-rolls-back-ledger",[&]{
        const auto broken=f.write("broken.far",far({{"pose.cfp",cfp().substr(0,7)}}));odh::host::AnimationCatalog catalog({f.header,broken});auto selected=catalog.resolve("clip");
        SceneRenderer renderer(f.config(f.base),provider(catalog));SceneState state;state.animation=&selected->header;const auto metadata=catalog.usage().metadata_bytes;
        rejects([&]{renderer.render(state,camera());});check(catalog.usage().cfp_samples==0&&catalog.usage().metadata_bytes==metadata&&renderer.cache_samples()==0,"failed decode retained samples or decoded metadata");
    });
    run("sample-budget-and-shared-handles",[&]{
        odh::host::CatalogLimits limits;limits.cfp_samples=13;odh::host::AnimationCatalog small({f.header,f.planes},limits);auto selected=small.resolve("clip");SceneRenderer rejected(f.config(f.base),provider(small));SceneState state;state.animation=&selected->header;
        rejects([&]{rejected.render(state,camera());});check(small.usage().cfp_samples==0,"sample budget failure charged planes");
        limits.cfp_samples=14;limits.cache_records=1;odh::host::AnimationCatalog exact({f.header,f.planes},limits);selected=exact.resolve("clip");state.animation=&selected->header;
        auto decoded=exact.decode(selected->header);auto alias=std::shared_ptr<const odh::CfpPlanes>(decoded,&decoded->planes);SceneRenderer renderer(f.config(f.base),provider(exact));
        for(int n=0;n<20;++n){state.frame=double(n%3)/2;renderer.render(state,camera());check(exact.decode(selected->header).get()==decoded.get()&&exact.usage().cfp_samples==14,"repeat draws duplicated CFP decode/budget");}
        check(alias.get()==&decoded->planes&&renderer.cache_samples()==0&&renderer.cache_clips()==0,"external planes copied into renderer cache");
        rejects([&]{exact.resolve("other");});check(exact.usage().cfp_samples==14&&alias->bits[0].size()==2,"pinned budget refusal invalidated shared planes");
    });
    run("null-and-mismatched-provider-refused",[&]{
        odh::host::AnimationCatalog catalog({f.header,f.planes});auto selected=catalog.resolve("clip");SceneState state;state.animation=&selected->header;
        SceneRenderer absent(f.config(f.combined),[](const auto&){return std::shared_ptr<const odh::CfpPlanes>{};});rejects([&]{absent.render(state,camera());});
        auto bad=std::make_shared<odh::CfpPlanes>();SceneRenderer invalid(f.config(f.combined),[&](const auto&){return bad;});rejects([&]{invalid.render(state,camera());});
        check(absent.cache_clips()==0&&invalid.cache_samples()==0,"invalid provider populated legacy cache");
    });
    run("validation-before-provider-and-bind-pose",[&]{
        unsigned calls=0;SceneRenderer renderer(f.config(f.base),[&](const auto&){++calls;return std::shared_ptr<const odh::CfpPlanes>{};});SceneState state;
        renderer.render(state,camera());check(calls==0,"bind pose called CFP provider");odh::AnimationHeader header{};state.animation=&header;state.frame=std::numeric_limits<double>::quiet_NaN();
        rejects([&]{renderer.render(state,camera());});check(calls==0,"invalid frame called CFP provider");
    });
    std::cout<<cases<<"/"<<cases<<" scene CFP provider cases passed; "<<checks<<" checks (synthetic Linux host)\n";return 0;
}catch(const std::exception& e){std::cerr<<"scene CFP provider test: "<<e.what()<<'\n';return 1;}}
