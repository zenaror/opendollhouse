#include "saved_appearance.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
namespace {
int checks=0;
void check(bool ok,const char* why="saved appearance assertion failed"){++checks;if(!ok)throw std::runtime_error(why);}
template<class F> void rejects(F f){bool caught=false;try{f();}catch(const odh::FormatError&){caught=true;}check(caught);}
void word(std::string& b,std::uint32_t n){for(int i=0;i<4;++i)b.push_back(static_cast<char>((n>>(i*8))&255));}
void scalar(std::string& b,float n){std::uint32_t bits;std::memcpy(&bits,&n,4);word(b,bits);}
void text(std::string& b,const std::string& s){b.push_back(static_cast<char>(s.size()));b+=s;}
std::string rig(const std::string& name="adult"){std::string b;word(b,1);text(b,name);b.push_back(1);b.push_back(0);text(b,"ROOT");text(b,"NULL");word(b,0);
 for(float n:{0.f,0.f,0.f,0.f,0.f,0.f,1.f})scalar(b,n);for(auto n:{1U,1U,0U})word(b,n);scalar(b,0);scalar(b,0);word(b,0);word(b,0);return b;}
std::string loose_mesh(){return
 "xskin-loose\nloose-texture\n1\nROOT\n1\n0 1 2\n1\n0 0 3 0 0\n"
 "3\n0.25 0.25\n0.25 0.25\n0.25 0.25\n0\n3\n"
 "-0.7 2 0 0 1 0\n0.7 2 0 0 1 0\n0 4 0 0 1 0\n";}
std::string loose_bitmap(){std::string b="BM";word(b,58);word(b,0);word(b,54);word(b,40);word(b,1);word(b,1);
 b.push_back(1);b.push_back(0);b.push_back(24);b.push_back(0);word(b,0);word(b,4);for(int i=0;i<4;++i)word(b,0);
 b.push_back(20);b.push_back(40);b.push_back(static_cast<char>(200));b.push_back(0);return b;}
std::string appearance(const std::string& name,const std::vector<std::string>& meshes,const std::string& bone="ROOT"){std::string b;word(b,0);word(b,1);text(b,name);word(b,0);word(b,0);word(b,static_cast<std::uint32_t>(meshes.size()));for(const auto& m:meshes){text(b,bone);text(b,m);word(b,0);word(b,0);}word(b,0);return b;}
std::string mesh(const std::string& name,const std::string& tex){std::string b;text(b,name);text(b,tex);for(int i=0;i<6;++i)word(b,0);return b;}
std::string clip(){std::string b;word(b,0);word(b,0);word(b,1);text(b,"clip");text(b,"pose");for(int i=0;i<5;++i)word(b,0);word(b,1);text(b,"ROOT");word(b,10);for(int i=0;i<7;++i)word(b,0);return b;}
using Entry=std::pair<std::string,std::string>;
void far(const std::filesystem::path& p,const std::vector<Entry>& entries){std::string b="FAR!byAZ";word(b,1);word(b,0);std::vector<std::uint32_t> offsets;for(const auto& e:entries){offsets.push_back(static_cast<std::uint32_t>(b.size()));b+=e.second;}auto offset=static_cast<std::uint32_t>(b.size());for(int i=0;i<4;++i)b[12+i]=static_cast<char>((offset>>(i*8))&255);word(b,static_cast<std::uint32_t>(entries.size()));for(std::size_t i=0;i<entries.size();++i){auto size=static_cast<std::uint32_t>(entries[i].second.size());word(b,size);word(b,size);word(b,offsets[i]);word(b,static_cast<std::uint32_t>(entries[i].first.size()));b+=entries[i].first;}std::ofstream out(p,std::ios::binary);out.write(b.data(),static_cast<std::streamsize>(b.size()));}
void save(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));}
struct Temp {std::filesystem::path path;Temp(){path=std::filesystem::temp_directory_path()/("odh-saved-appearance-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(path);}~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
}
int main(int argc,char** argv){try{
 if(argc==5&&std::string(argv[1])=="--commercial-skin"){
  SavedAppearanceResources resources(argv[2],argv[3],"adult-skeleton.cmx.bcf","adult",argv[4]);
  odh::AvatarBodyStrings body{};body.body="b010fafit_01";body.head=body.body;
  for(auto& hand:body.hands)hand=body.body;
  SceneRenderer renderer(resources.resolve_body(body));
  SceneState state{};
  odh::WorldProjection camera;camera.width=320;camera.height=240;
  const auto frame=renderer.render(state,camera);
  std::cout<<"commercial loose-skin resources loaded: vertices="<<renderer.vertices()
      <<" triangles="<<renderer.triangles()<<" texture_bytes="<<renderer.texture_bytes()
      <<" raster_samples="<<frame.image.shaded_samples<<'\n';
  return 0;
 }
 odh::AnimationHeader h{};h.name="clip";odh::BoneMotion motion{};motion.frames=121;h.motions.push_back(motion);
 auto parsed=saved_animation_appearance("clip;6;1000;25;1000;0;7;8",h);
 check(parsed.frame==3.025&&parsed.priority==6&&parsed.weight_milli==1000&&parsed.speed_milli==1000);check(!parsed.loop&&!parsed.backwards&&!parsed.end_reached&&parsed.unknown1==7&&parsed.unknown2==8);
 parsed=saved_animation_appearance("CLIP;-10;-1000;0;500;1;1;1",h);check(parsed.loop&&parsed.backwards&&parsed.end_reached&&parsed.weight_milli==500);
 for(const auto* s:{"clip;1;2;3;4;0;0","clip;1;2;3;4;0;0;0;0","clip;1;2;1001;4;0;0;0","clip;1;2;-1;4;0;0;0","clip;1;2;3;1001;0;0;0","clip;1;2;3;4;2;0;0","clip;1;2;3;4;01;0;0","clip;1;x;3;4;0;0;0","clip;1;2;3;4;0;0;","other;1;2;3;4;0;0;0","clip;2147483648;2;3;4;0;0;0"})rejects([&]{saved_animation_appearance(s,h);});
 auto empty=h;empty.motions.clear();rejects([&]{saved_animation_appearance("clip;1;2;3;4;0;0;0",empty);});
 Temp dir;const auto a=dir.path/"animations.far",t=dir.path/"textures.far";
 std::vector<Entry> entries={{"rig.bcf","rig"},{"unexpected-host.bcf",appearance("body",{"mesh0","mesh1","mesh2"})},{"head-host.bcf",appearance("head",{"headmesh"})},{"hand-host.bcf",appearance("hand",{"handmesh"})},{"mesh0.bmf",mesh("mesh0","unmatched")},{"mesh1.bmf",mesh("mesh1","skinlgt")},{"mesh2.bmf",mesh("mesh2","x")},{"headmesh.bmf",mesh("headmesh","x")},{"handmesh.bmf",mesh("handmesh","x")},
 {"accessory-toothbrush.cmx.bcf",appearance("suit-toothbrush",{"toothbrush"},"R_HAND")},
 {"toothbrush.bmf",mesh("toothbrush","x")}};
 // The rig is caller-selected; scanner skips its nonzero skeleton count.
 entries[0].second=rig();
 far(a,entries);far(t,{{"unmatched.bmp",""},{"skinlgt.bmp",""},{"override.bmp",""},{"skinmed.bmp",""},{"x.bmp",""}});
 const auto skins=dir.path/"loose-skins";std::filesystem::create_directory(skins);
 save(skins/"loose.cmx","// synthetic CMX\nversion 300\n0\n1\nb123mafat\n1\n0\n1\nROOT\nxskin-loose\n0\n0\n0\n");
 save(skins/"xskin-loose.skn",loose_mesh());
 save(skins/"loose-texture.bmp",loose_bitmap());
 SavedAppearanceResources loose_resources(a.string(),t.string(),"rig.bcf","adult",skins.string());
 odh::AvatarBodyStrings loose_body{};loose_body.body="b123mafat";loose_body.head="b123mafat";
 for(auto& hand:loose_body.hands)hand="b123mafat";
 const auto loose_scene=loose_resources.resolve_body(loose_body);
 check(loose_scene.parts.size()==8&&loose_scene.parts.front().loose_skin_assets&&
       loose_scene.parts.front().appearance_entry==(skins/"loose.cmx").string()&&
       loose_scene.parts.front().bmf_entry==(skins/"xskin-loose.skn").string()&&
       loose_scene.parts.front().texture_entry==(skins/"loose-texture.bmp").string());
 SceneRenderer loose_renderer(loose_scene);
 check(loose_renderer.vertices()==24&&loose_renderer.triangles()==8&&loose_renderer.texture_bytes()==32);
 odh::WorldProjection loose_camera;loose_camera.width=160;loose_camera.height=160;
 check(loose_renderer.render(SceneState{},loose_camera).image.shaded_samples>0);
 // Gameplay selects the actual human rig from BodySTR[0]. Age is independent
 // simulation data; a child profile must never borrow the configured adult rig.
 {
  const auto humans_far=dir.path/"human-rigs.far";
  const std::vector<Entry> human_rigs{{"legacy-explicit.bcf",rig()},
      {"adult-skeleton.cmx.bcf",rig("adult")},{"child-skeleton.cmx.bcf",rig("child")}};
  far(humans_far,human_rigs);
  SavedAppearanceResources humans(humans_far.string(),t.string(),"legacy-explicit.bcf","adult",skins.string());
  auto adult=loose_body;adult.avatar_type="adult";adult.age=3;
  auto child=loose_body;child.avatar_type="child";child.age=35;
  const auto adult_source=adult,child_source=child;
  const auto first_adult=humans.resolve_human_body(adult);
  const auto selected_child=humans.resolve_human_body(child);
  const auto second_adult=humans.resolve_human_body(adult);
  check(first_adult.animation_far==humans_far.string()&&first_adult.skeleton_entry=="adult-skeleton.cmx.bcf"&&
      first_adult.skeleton_name=="adult"&&selected_child.animation_far==humans_far.string()&&
      selected_child.skeleton_entry=="child-skeleton.cmx.bcf"&&selected_child.skeleton_name=="child",
      "human BodySTR type selects its exact adult/child rig despite contradictory age");
  check(second_adult.skeleton_entry==first_adult.skeleton_entry&&second_adult.skeleton_name==first_adult.skeleton_name&&
      second_adult.parts.size()==first_adult.parts.size()&&selected_child.parts.size()==first_adult.parts.size(),
      "adult-child-adult resolution does not leak the preceding profile into the shared catalog");
  SceneRenderer adult_renderer(first_adult),child_renderer(selected_child);
  check(adult_renderer.vertices()==24&&child_renderer.vertices()==24&&
      adult_renderer.render(SceneState{},loose_camera).image.shaded_samples>0&&
      child_renderer.render(SceneState{},loose_camera).image.shaded_samples>0,
      "both selected human rigs are valid renderable synthetic resources");
  check(adult.age==adult_source.age&&child.age==child_source.age,
      "visual human resolution preserves original BodySTR age values");
  for(const auto age:std::vector<std::optional<std::int16_t>>{std::nullopt,0,9,35,100}){
   adult.age=age;child.age=age;
   check(humans.resolve_human_body(adult).skeleton_name=="adult"&&
       humans.resolve_human_body(child).skeleton_name=="child");
  }
  const auto explicit_child=humans.resolve_body(child),explicit_adult=humans.resolve_body(adult);
  check(explicit_child.skeleton_entry=="legacy-explicit.bcf"&&explicit_child.skeleton_name=="adult"&&
      explicit_adult.skeleton_entry=="legacy-explicit.bcf"&&explicit_adult.skeleton_name=="adult",
      "legacy resolve_body retains the explicitly configured rig after both gameplay profiles");
  SavedAppearanceResources explicit_child_rig(humans_far.string(),t.string(),"child-skeleton.cmx.bcf","child",skins.string());
  check(explicit_child_rig.resolve_human_body(adult).skeleton_name=="adult"&&
      explicit_child_rig.resolve_body(adult).skeleton_name=="child"&&
      explicit_child_rig.resolve_human_body(child).skeleton_name=="child",
      "gameplay rig selection also leaves an explicitly configured child tool rig unchanged");
  check(adult_source.avatar_type=="adult"&&child_source.avatar_type=="child"&&adult.avatar_type==adult_source.avatar_type&&
      child.avatar_type==child_source.avatar_type&&adult.body==adult_source.body&&child.body==child_source.body&&
      adult.head==adult_source.head&&child.head==child_source.head&&adult.hands==adult_source.hands&&child.hands==child_source.hands);
  for(const auto& type:std::vector<std::string>{"","dog","cat","unrecognized",std::string("child\0junk",10)}){
   auto invalid=child;invalid.avatar_type=type;rejects([&]{humans.resolve_human_body(invalid);});
  }
  check(humans.resolve_human_body(adult).skeleton_name=="adult"&&
      humans.resolve_body(child).skeleton_entry=="legacy-explicit.bcf");
  const auto missing_far=dir.path/"human-missing-child.far";
  far(missing_far,{{"legacy-explicit.bcf",rig()},{"adult-skeleton.cmx.bcf",rig()}});
  SavedAppearanceResources missing(missing_far.string(),t.string(),"legacy-explicit.bcf","adult",skins.string());
  check(missing.resolve_human_body(adult).skeleton_name=="adult");
  rejects([&]{missing.resolve_human_body(child);});
  check(missing.resolve_body(child).skeleton_entry=="legacy-explicit.bcf"&&
      missing.resolve_human_body(adult).skeleton_entry=="adult-skeleton.cmx.bcf");
  const auto ambiguous_far=dir.path/"human-ambiguous-child.far";
  auto duplicate_rigs=human_rigs;duplicate_rigs.push_back({"CHILD-SKELETON.CMX.BCF",rig("child")});
  far(ambiguous_far,duplicate_rigs);
  SavedAppearanceResources ambiguous_human(ambiguous_far.string(),t.string(),"legacy-explicit.bcf","adult",skins.string());
  rejects([&]{ambiguous_human.resolve_human_body(child);});
  check(ambiguous_human.resolve_human_body(adult).skeleton_entry=="adult-skeleton.cmx.bcf"&&
      ambiguous_human.resolve_body(child).skeleton_entry=="legacy-explicit.bcf");
 }
 // Expansion FAR names use Windows directory prefixes, while BCF bindings
 // reference global basenames. Resolve every component with exact provenance.
 const auto expansion=dir.path/"expansion.far",assets=dir.path/"skin-assets.far",override_far=dir.path/"skin-override.far";
 far(expansion,{{"Skins\\Outfit.cmx.bcf",appearance("ExpansionBody",{"xskin-loose"})}});
 far(assets,{{"Skins\\XSKIN-LOOSE.SKN",loose_mesh()},{"Skins\\loose-texture.bmp",loose_bitmap()}});
 const std::vector<std::string> all_fars{a.string(),expansion.string(),assets.string()};
 SavedAppearanceResources expanded(a.string(),t.string(),"rig.bcf","adult",skins.string(),all_fars);
 auto expansion_body=loose_body;expansion_body.body=expansion_body.head="expansionbody";
 for(auto& hand:expansion_body.hands)hand="EXPANSIONBODY";
 const auto expansion_scene=expanded.resolve_body(expansion_body);
 check(expansion_scene.parts.size()==8&&expansion_scene.parts.front().appearance_entry=="Skins\\Outfit.cmx.bcf"&&
       expansion_scene.parts.front().appearance_far==expansion.string()&&
       expansion_scene.parts.front().mesh_far==assets.string()&&
       expansion_scene.parts.front().texture_far==assets.string()&&
       expansion_scene.parts.front().bmf_entry=="Skins\\XSKIN-LOOSE.SKN");
 SceneRenderer expansion_renderer(expansion_scene);
 check(expansion_renderer.vertices()==24&&expansion_renderer.render(SceneState{},loose_camera).image.shaded_samples>0);
 // Loose CMX can consume globally selected FAR meshes/textures. FAR BMPs
 // precede same-named loose images, as TS1Provider.BuildDictionary specifies.
 const auto mixed_scene=expanded.resolve_body(loose_body);
 check(mixed_scene.parts.front().loose_skin_assets&&mixed_scene.parts.front().appearance_far.empty()&&
       mixed_scene.parts.front().mesh_far==assets.string()&&mixed_scene.parts.front().texture_far==assets.string());
 SceneRenderer mixed_renderer(mixed_scene);check(mixed_renderer.render(SceneState{},loose_camera).image.shaded_samples>0);
 far(override_far,{{"Later/LOOSE-TEXTURE.BMP",loose_bitmap()}});
 auto with_override=all_fars;with_override.push_back(override_far.string());
 SavedAppearanceResources overrides(a.string(),t.string(),"rig.bcf","adult",skins.string(),with_override);
 check(overrides.resolve_body(expansion_body).parts.front().texture_far==override_far.string()&&
       overrides.resolve_body(expansion_body).parts.front().texture_entry=="Later/LOOSE-TEXTURE.BMP");
 // Compiled BMF is preferred to SKN across sources, not only siblings.
 const auto compiled=dir.path/"compiled-mesh.far";
 far(compiled,{{"Elsewhere/xskin-loose.bmf",mesh("xskin-loose","loose-texture")}});
 with_override.push_back(compiled.string());
 SavedAppearanceResources compiled_preferred(a.string(),t.string(),"rig.bcf","adult",skins.string(),with_override);
 check(compiled_preferred.resolve_body(expansion_body).parts.front().mesh_far==compiled.string()&&
       compiled_preferred.resolve_body(expansion_body).parts.front().bmf_entry=="Elsewhere/xskin-loose.bmf");
 const auto extra_skins=dir.path/"extra-skins";std::filesystem::create_directory(extra_skins);
 save(extra_skins/"custom.cmx","version 300\n0\n1\ncustom-extra\n1\n0\n1\nROOT\nxskin-loose\n0\n0\n0\n");
 SavedAppearanceResources extra_loose(a.string(),t.string(),"rig.bcf","adult",skins.string(),all_fars,{extra_skins.string()});
 auto custom_body=loose_body;custom_body.body=custom_body.head="custom-extra";for(auto& hand:custom_body.hands)hand="custom-extra";
 const auto custom_scene=extra_loose.resolve_body(custom_body);
 check(custom_scene.parts.front().appearance_entry==(extra_skins/"custom.cmx").string()&&custom_scene.parts.front().mesh_far==assets.string());
 SceneRenderer custom_renderer(custom_scene);check(custom_renderer.render(SceneState{},loose_camera).image.shaded_samples>0);
 rejects([&]{SavedAppearanceResources too_many(a.string(),t.string(),"rig.bcf","adult",{},std::vector<std::string>(129,a.string()));});
 // A damaged unrelated BCF is a source-catalog diagnostic, not permission
 // to substitute a model when that damaged/missing appearance is requested.
 const auto damaged=dir.path/"unrelated-damaged.far";
 far(damaged,{{"Skins/broken.bcf","bad"},{"Skins/good.cmx.bcf",appearance("GoodStillDraws",{"xskin-loose"})}});
 auto damaged_sources=all_fars;damaged_sources.push_back(damaged.string());
 SavedAppearanceResources isolated(a.string(),t.string(),"rig.bcf","adult",skins.string(),damaged_sources);
 auto good_body=loose_body;good_body.body=good_body.head="GoodStillDraws";for(auto& hand:good_body.hands)hand="GoodStillDraws";
 SceneRenderer good_renderer(isolated.resolve_body(good_body));
 check(good_renderer.render(SceneState{},loose_camera).image.shaded_samples>0&&
       isolated.diagnostics().malformed_resources==1&&isolated.diagnostics().resource_errors.size()==1&&
       isolated.diagnostics().resource_errors.front().find("broken.bcf")!=std::string::npos);
 auto bad_body=good_body;bad_body.body="BrokenMissingBody";rejects([&]{isolated.resolve_body(bad_body);});
 // Legacy explicit-archive construction retains its strict parser contract.
 const auto damaged_base=dir.path/"damaged-base.far";
 far(damaged_base,{{"rig.bcf",rig()},{"broken.bcf","bad"}});
 rejects([&]{SavedAppearanceResources strict(damaged_base.string(),t.string(),"rig.bcf","adult");});
 const auto excessive=dir.path/"too-many-archive-entries.far";std::vector<Entry> excessive_entries;
 for(unsigned i=0;i<8193;++i)excessive_entries.push_back({std::to_string(i)+".bcf","bad"});
 far(excessive,excessive_entries);auto excessive_sources=all_fars;excessive_sources.push_back(excessive.string());
 rejects([&]{SavedAppearanceResources strict_budget(a.string(),t.string(),"rig.bcf","adult",skins.string(),excessive_sources);});
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
 p.carry_animation.bytes="clip;1;2;3;4;0;0;0";rejects([&]{no_match.resolve(p);});p.carry_animation.bytes="";
 odh::ObjmSavedAccessory toothbrush{};toothbrush.name.bytes="suit-toothbrush.apr";
 p.accessory_count=1;p.accessories={toothbrush};
 auto with_accessory=no_match.resolve(p);check(with_accessory.scene.parts.size()==7);
 check(with_accessory.scene.parts[4].appearance_name=="suit-toothbrush"&&
       with_accessory.scene.parts[4].bmf_entry=="toothbrush.bmf"&&
       with_accessory.scene.parts[4].texture_entry=="x.bmp"&&!with_accessory.scene.parts[4].hand);
 auto malformed_accessory=toothbrush;malformed_accessory.name.bytes="suit-toothbrush";
 p.accessories={malformed_accessory};rejects([&]{no_match.resolve(p);});
 malformed_accessory=toothbrush;malformed_accessory.binding.bytes="unknown";
 p.accessories={malformed_accessory};rejects([&]{no_match.resolve(p);});
 p.accessories={toothbrush};p.accessory_count=2;rejects([&]{no_match.resolve(p);});
 p.accessories.clear();p.accessory_count=0;
 // Runtime content misses are source visual no-ops, while saved imports and
 // selected malformed resources retain strict diagnostics.
 {auto runtime=no_match.resolve(p).scene;const auto before=runtime.parts.size();
  no_match.append_accessories(runtime,{".apr","not-installed.apr"});check(runtime.parts.size()==before);
  no_match.append_accessories(runtime,{"suit-toothbrush.apr"});check(runtime.parts.size()==before+1);
  rejects([&]{no_match.append_accessories(runtime,{"suit-toothbrush"});});
  auto missing_saved=p;missing_saved.accessory_count=1;missing_saved.accessories={toothbrush};
  missing_saved.accessories[0].name.bytes="not-installed.apr";rejects([&]{no_match.resolve(missing_saved);});
  auto broken_entries=entries;broken_entries.push_back({"broken-accessory.bcf",appearance("broken-accessory",{"missing-accessory-mesh"})});
  const auto broken_file=dir.path/"runtime-broken.far";far(broken_file,broken_entries);
  SavedAppearanceResources broken(broken_file.string(),t.string(),"rig.bcf","adult");
  rejects([&]{broken.append_accessories(runtime,{"broken-accessory.apr"});});}
 p.body.bytes="notthere";rejects([&]{no_match.resolve(p);});p.body.bytes="body";
 // BodySTR variants replace only the visual hand source; saved raw fields and
 // current/base animation evidence survive the opt-in resolution unchanged.
 const std::array<std::string,6> names{"leftIdle","rightIdle","leftPoint","rightPoint","leftFist","rightFist"};
 for(const auto& name:names){entries.push_back({name+".bcf",appearance(name,{"handmesh"})});}
 entries.push_back({"clip.bcf",clip()});entries.push_back({"pose.cfp",""});far(a,entries);
 odh::AvatarBodyStrings body{};
 for(std::size_t i=0;i<names.size();++i)body.hands[i]=names[i]+",HAND="+(i==0?"override":i==1?"skinmed":i==2?"skinlgt":i==3?"missing-right-point":i==4?"unmatched":"missing-right-fist");
 SavedAppearanceResources gestures(a.string(),t.string(),"rig.bcf","adult");
 p.animation.bytes="clip;6;1000;250;1000;0;7;8";p.base_animation.bytes="clip;2;-500;0;500;1;4;5";
 p.left_hand.bytes="unresolved-raw-left";p.right_hand.bytes="unresolved-raw-right";
 auto gesture_result=gestures.resolve_gestures(p,body);check(gesture_result.scene.parts.size()==10);
 check(gesture_result.current->raw==p.animation.bytes&&gesture_result.base->raw==p.base_animation.bytes&&!gesture_result.base_executed);
 check(gesture_result.current->frame==2.5&&gesture_result.base->weight_milli==500);
 for(std::size_t i=0;i<6;++i){const auto& part=gesture_result.scene.parts[i+4];check(part.appearance_name==names[i]);check(part.hand&&part.hand->right==(i%2==1)&&part.hand->gesture==static_cast<odh::HandGesture>(i/2));}
 check(!gesture_result.scene.parts[0].hand&&!gesture_result.scene.parts[3].hand);
 check(gesture_result.scene.parts[4].texture_entry=="override.bmp"&&gesture_result.scene.parts[6].texture_entry=="skinlgt.bmp"&&gesture_result.scene.parts[8].texture_entry=="unmatched.bmp");
 for(const auto i:{5,7,9})check(gesture_result.scene.parts[static_cast<std::size_t>(i)].texture_entry=="skinmed.bmp");
 check(p.left_hand.bytes=="unresolved-raw-left"&&p.right_hand.bytes=="unresolved-raw-right");
 // The constructed BodySTR path needs no fabricated ObjmPersonState and does
 // not resolve saved current/base animations or infer appearance defaults.
 auto constructed=body;constructed.body="BODY,BODY=skinmed";constructed.head="head,HEAD-HEAD=override";
 const auto body_before=constructed.body,head_before=constructed.head;const auto hands_before=constructed.hands;
 auto direct=gestures.resolve_body(constructed);check(direct.parts.size()==10);
 auto live_scene=gestures.resolve_body(constructed);
 gestures.append_accessories(live_scene,{"suit-toothbrush.apr"});
 check(live_scene.parts.size()==11&&live_scene.parts.back().appearance_name=="suit-toothbrush"&&
       live_scene.parts.back().bmf_entry=="toothbrush.bmf");
 gestures.append_accessories(live_scene,{"unknown-accessory.apr"});
 check(live_scene.parts.size()==11&&live_scene.parts.back().appearance_name=="suit-toothbrush");
 rejects([&]{gestures.append_accessories(live_scene,std::vector<std::string>(17,"suit-toothbrush.apr"));});
 check(direct.animation_far==a.string()&&direct.textures_far==t.string()&&direct.skeleton_entry=="rig.bcf"&&direct.skeleton_name=="adult");
 check(direct.object_far.empty()&&direct.object_entry.empty()&&direct.object_id==0);
 check(direct.parts[0].appearance_entry=="unexpected-host.bcf"&&direct.parts[1].texture_entry=="skinmed.bmp"&&direct.parts[2].texture_entry=="unmatched.bmp"&&direct.parts[3].texture_entry=="override.bmp");
 for(std::size_t i=0;i<6;++i){const auto& part=direct.parts[i+4];check(part.appearance_name==names[i]&&part.hand&&part.hand->right==(i%2==1)&&part.hand->gesture==static_cast<odh::HandGesture>(i/2));}
 check(direct.parts[4].texture_entry=="override.bmp"&&direct.parts[6].texture_entry=="skinlgt.bmp"&&direct.parts[8].texture_entry=="unmatched.bmp");
 for(const auto i:{5,7,9})check(direct.parts[static_cast<std::size_t>(i)].texture_entry=="skinmed.bmp");
 check(constructed.body==body_before&&constructed.head==head_before&&constructed.hands==hands_before);
 auto defaults=constructed;defaults.body="body";defaults.head="head";auto default_parts=gestures.resolve_body(defaults);
 check(default_parts.parts[0].texture_entry=="unmatched.bmp"&&default_parts.parts[1].texture_entry=="skinlgt.bmp"&&default_parts.parts[2].texture_entry=="unmatched.bmp"&&default_parts.parts[3].texture_entry=="x.bmp");
 for(std::size_t selected=0;selected<8;++selected){auto absent_body=constructed;if(selected==0)absent_body.body.clear();else if(selected==1)absent_body.head.clear();else absent_body.hands[selected-2].clear();rejects([&]{gestures.resolve_body(absent_body);});}
 for(const auto& raw:std::vector<std::string>{",BODY=override","body,","body,BODY=","body,=override","body,BODY=override;other","body,BODY=override,extra","body=override","body;other",std::string(4097,'x'),std::string("body\0junk",9)}){
     auto bad=constructed;bad.body=raw;rejects([&]{gestures.resolve_body(bad);});bad=constructed;bad.head=raw;rejects([&]{gestures.resolve_body(bad);});}
 auto absent_body=constructed;absent_body.body="missing-body,BODY=override";rejects([&]{gestures.resolve_body(absent_body);});
 absent_body=constructed;absent_body.head="missing-head";rejects([&]{gestures.resolve_body(absent_body);});
 absent_body=constructed;absent_body.body="body,BODY=missing-texture";rejects([&]{gestures.resolve_body(absent_body);});
 absent_body=constructed;absent_body.hands[2]="leftPoint,HAND=missing-texture";rejects([&]{gestures.resolve_body(absent_body);});
 absent_body=constructed;absent_body.hands[2]="leftPoint,HAND=bad=second";rejects([&]{gestures.resolve_body(absent_body);});
 for(int i=0;i<20;++i)check(gestures.resolve_body(constructed).parts[7].texture_entry=="skinmed.bmp");
 for(const auto& raw:std::vector<std::string>{"",",HAND=override","leftIdle,","leftIdle,HAND=","leftIdle,=override","leftIdle,HAND=override;other","leftIdle,HAND=override,extra","leftIdle=override","leftIdle;other",std::string(4097,'x'),std::string("leftIdle\0junk",13)}){auto bad=body;bad.hands[0]=raw;rejects([&]{gestures.resolve_gestures(p,bad);});}
 auto absent=body;absent.hands[2]="missing-gesture,HAND=override";rejects([&]{gestures.resolve_gestures(p,absent);});
 absent=body;absent.hands[2]="leftPoint,HAND=missing";rejects([&]{gestures.resolve_gestures(p,absent);});
 // No explicit texture map means use the appearance mesh's default texture.
 absent=body;absent.hands[0]="leftIdle";check(gestures.resolve_gestures(p,absent).scene.parts[4].texture_entry=="x.bmp");
 absent=body;absent.hands[3]="rightPoint,HAND=bad=second";rejects([&]{gestures.resolve_gestures(p,absent);});
 // Repeated resolves keep identities stable; six variants share the same
 // cumulative 16-part ceiling as the old path.
 for(int i=0;i<20;++i)check(gestures.resolve_gestures(p,body).scene.parts[7].texture_entry=="skinmed.bmp");
 auto exact_parts=entries;for(auto& entry:exact_parts)if(entry.first=="leftFist.bcf")entry.second=appearance("leftFist",std::vector<std::string>(7,"handmesh"));far(a,exact_parts);
 SavedAppearanceResources at_limit(a.string(),t.string(),"rig.bcf","adult");check(at_limit.resolve_body(constructed).parts.size()==16);
 auto crowded=entries;for(auto& entry:crowded)if(entry.first=="leftFist.bcf")entry.second=appearance("leftFist",std::vector<std::string>(8,"handmesh"));far(a,crowded);
 SavedAppearanceResources bounded(a.string(),t.string(),"rig.bcf","adult");rejects([&]{bounded.resolve_gestures(p,body);});rejects([&]{bounded.resolve_body(constructed);});
 far(a,entries);p.left_hand.bytes=p.right_hand.bytes="hand";p.animation.bytes=p.base_animation.bytes="";
 entries.push_back({"duplicate.bcf",appearance("BODY",{"mesh0"})});far(a,entries);SavedAppearanceResources ambiguous(a.string(),t.string(),"rig.bcf","adult");rejects([&]{ambiguous.resolve(p);});rejects([&]{ambiguous.resolve_body(constructed);});
 {SceneConfig runtime;rejects([&]{ambiguous.append_accessories(runtime,{"BODY.apr"});});}
 entries.pop_back();far(a,entries);const auto duplicate_textures=dir.path/"duplicate-textures.far";
 far(duplicate_textures,{{"unmatched.bmp",""},{"skinlgt.bmp",""},{"override.bmp",""},{"OVERRIDE.bmp",""},{"skinmed.bmp",""},{"x.bmp",""}});
 SavedAppearanceResources ambiguous_texture(a.string(),duplicate_textures.string(),"rig.bcf","adult");rejects([&]{ambiguous_texture.resolve_body(constructed);});
 SavedAppearanceResources moved_body(std::move(gestures));rejects([&]{gestures.resolve_body(constructed);});
 std::cout<<checks<<" saved appearance checks passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
