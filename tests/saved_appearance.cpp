#include "saved_appearance.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
namespace {
int checks=0;
void check(bool ok){++checks;if(!ok)throw std::runtime_error("saved appearance assertion failed");}
template<class F> void rejects(F f){bool caught=false;try{f();}catch(const odh::FormatError&){caught=true;}check(caught);}
void word(std::string& b,std::uint32_t n){for(int i=0;i<4;++i)b.push_back(static_cast<char>((n>>(i*8))&255));}
void text(std::string& b,const std::string& s){b.push_back(static_cast<char>(s.size()));b+=s;}
std::string appearance(const std::string& name,const std::vector<std::string>& meshes){std::string b;word(b,0);word(b,1);text(b,name);word(b,0);word(b,0);word(b,static_cast<std::uint32_t>(meshes.size()));for(const auto& m:meshes){text(b,"ROOT");text(b,m);word(b,0);word(b,0);}word(b,0);return b;}
std::string mesh(const std::string& name,const std::string& tex){std::string b;text(b,name);text(b,tex);for(int i=0;i<6;++i)word(b,0);return b;}
using Entry=std::pair<std::string,std::string>;
void far(const std::filesystem::path& p,const std::vector<Entry>& entries){std::string b="FAR!byAZ";word(b,1);word(b,0);std::vector<std::uint32_t> offsets;for(const auto& e:entries){offsets.push_back(static_cast<std::uint32_t>(b.size()));b+=e.second;}auto offset=static_cast<std::uint32_t>(b.size());for(int i=0;i<4;++i)b[12+i]=static_cast<char>((offset>>(i*8))&255);word(b,static_cast<std::uint32_t>(entries.size()));for(std::size_t i=0;i<entries.size();++i){auto size=static_cast<std::uint32_t>(entries[i].second.size());word(b,size);word(b,size);word(b,offsets[i]);word(b,static_cast<std::uint32_t>(entries[i].first.size()));b+=entries[i].first;}std::ofstream out(p,std::ios::binary);out.write(b.data(),static_cast<std::streamsize>(b.size()));}
struct Temp {std::filesystem::path path;Temp(){path=std::filesystem::temp_directory_path()/("odh-saved-appearance-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(path);}~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
}
int main(){try{
 odh::AnimationHeader h{};h.name="clip";odh::BoneMotion motion{};motion.frames=121;h.motions.push_back(motion);
 auto parsed=saved_animation_appearance("clip;6;1000;25;1000;0;7;8",h);
 check(parsed.frame==3.025&&parsed.priority==6&&parsed.weight_milli==1000&&parsed.speed_milli==1000);check(!parsed.loop&&!parsed.backwards&&!parsed.end_reached&&parsed.unknown1==7&&parsed.unknown2==8);
 parsed=saved_animation_appearance("CLIP;-10;-1000;0;500;1;1;1",h);check(parsed.loop&&parsed.backwards&&parsed.end_reached&&parsed.weight_milli==500);
 for(const auto* s:{"clip;1;2;3;4;0;0","clip;1;2;3;4;0;0;0;0","clip;1;2;1001;4;0;0;0","clip;1;2;-1;4;0;0;0","clip;1;2;3;1001;0;0;0","clip;1;2;3;4;2;0;0","clip;1;2;3;4;01;0;0","clip;1;x;3;4;0;0;0","clip;1;2;3;4;0;0;","other;1;2;3;4;0;0;0","clip;2147483648;2;3;4;0;0;0"})rejects([&]{saved_animation_appearance(s,h);});
 auto empty=h;empty.motions.clear();rejects([&]{saved_animation_appearance("clip;1;2;3;4;0;0;0",empty);});
 Temp dir;const auto a=dir.path/"animations.far",t=dir.path/"textures.far";
 std::vector<Entry> entries={{"rig.bcf","rig"},{"unexpected-host.bcf",appearance("body",{"mesh0","mesh1","mesh2"})},{"head-host.bcf",appearance("head",{"headmesh"})},{"hand-host.bcf",appearance("hand",{"handmesh"})},{"mesh0.bmf",mesh("mesh0","unmatched")},{"mesh1.bmf",mesh("mesh1","skinlgt")},{"mesh2.bmf",mesh("mesh2","x")},{"headmesh.bmf",mesh("headmesh","x")},{"handmesh.bmf",mesh("handmesh","x")}};
 // The rig is caller-selected; scanner skips its nonzero skeleton count.
 entries[0].second=std::string(4,'\0');entries[0].second[0]=1;
 far(a,entries);far(t,{{"unmatched.bmp",""},{"skinlgt.bmp",""},{"override.bmp",""},{"skinmed.bmp",""},{"x.bmp",""}});
 SavedAppearanceResources resources(a.string(),t.string(),"rig.bcf","adult");odh::ObjmPersonState p{};p.body.bytes="BODY";p.body_texture.bytes="BODY=skinmed";p.head.bytes="head";p.head_texture.bytes="HEAD-HEAD=override";p.left_hand.bytes=p.right_hand.bytes="hand";p.left_hand_texture.bytes=p.right_hand_texture.bytes="HAND=override";
 auto result=resources.resolve(p);check(result.scene.parts.size()==6);check(result.scene.parts[0].appearance_entry=="unexpected-host.bcf");check(result.scene.parts[0].texture_entry=="unmatched.bmp"&&result.scene.parts[1].texture_entry=="skinlgt.bmp"&&result.scene.parts[2].texture_entry=="skinmed.bmp");check(!result.current&&!result.base&&!result.base_executed);
 SavedAppearanceResources moved(std::move(resources));rejects([&]{resources.resolve(p);});
 check(moved.resolve(p).scene.parts[2].texture_entry=="skinmed.bmp");
 // Last matching binding wins. With no match, reference code overrides binding zero.
 p.body_texture.bytes="BODY=override";entries[6].second=mesh("mesh2","unmatched");far(a,entries);
 SavedAppearanceResources no_match(a.string(),t.string(),"rig.bcf","adult");result=no_match.resolve(p);check(result.scene.parts[0].texture_entry=="override.bmp"&&result.scene.parts[1].texture_entry=="skinlgt.bmp"&&result.scene.parts[2].texture_entry=="unmatched.bmp");
 p.body_texture.bytes="";result=no_match.resolve(p);check(result.scene.parts[0].texture_entry=="unmatched.bmp");
 for(const auto* map:{"missing-equals","=override","BODY=","BODY=override;ignored","BODY=override=second"}){p.body_texture.bytes=map;rejects([&]{no_match.resolve(p);});}
 p.body_texture.bytes="BODY=missing";rejects([&]{no_match.resolve(p);});p.body_texture.bytes="BODY=override";
 p.carry_animation.bytes="clip;1;2;3;4;0;0;0";rejects([&]{no_match.resolve(p);});p.carry_animation.bytes="";p.accessory_count=1;rejects([&]{no_match.resolve(p);});p.accessory_count=0;
 p.body.bytes="notthere";rejects([&]{no_match.resolve(p);});p.body.bytes="body";
 entries.push_back({"duplicate.bcf",appearance("BODY",{"mesh0"})});far(a,entries);SavedAppearanceResources ambiguous(a.string(),t.string(),"rig.bcf","adult");rejects([&]{ambiguous.resolve(p);});
 std::cout<<checks<<" saved appearance checks passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
