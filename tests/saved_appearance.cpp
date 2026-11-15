#include "saved_appearance.hpp"
#include "saved_appearance_far_index.hpp"
#include "odh/sha256.hpp"
#include <chrono>
#include <cmath>
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
std::string mixed_avatar(const std::string& species,const std::string& outfit,const std::vector<std::string>& meshes){
 auto skeleton=rig(species);skeleton.resize(skeleton.size()-8);
 const auto skin=appearance(outfit,meshes);
 return skeleton+skin.substr(4); // Preserve skeleton and appearance sections plus zero animation count.
}
std::string clip(){std::string b;word(b,0);word(b,0);word(b,1);text(b,"clip");text(b,"pose");for(int i=0;i<5;++i)word(b,0);word(b,1);text(b,"ROOT");word(b,10);for(int i=0;i<7;++i)word(b,0);return b;}
using Entry=std::pair<std::string,std::string>;
void far(const std::filesystem::path& p,const std::vector<Entry>& entries){std::string b="FAR!byAZ";word(b,1);word(b,0);std::vector<std::uint32_t> offsets;for(const auto& e:entries){offsets.push_back(static_cast<std::uint32_t>(b.size()));b+=e.second;}auto offset=static_cast<std::uint32_t>(b.size());for(int i=0;i<4;++i)b[12+i]=static_cast<char>((offset>>(i*8))&255);word(b,static_cast<std::uint32_t>(entries.size()));for(std::size_t i=0;i<entries.size();++i){auto size=static_cast<std::uint32_t>(entries[i].second.size());word(b,size);word(b,size);word(b,offsets[i]);word(b,static_cast<std::uint32_t>(entries[i].first.size()));b+=entries[i].first;}std::ofstream out(p,std::ios::binary);out.write(b.data(),static_cast<std::streamsize>(b.size()));}
void save(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));}
struct Temp {std::filesystem::path path;Temp(){path=std::filesystem::temp_directory_path()/("odh-saved-appearance-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(path);}~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
// Independent old implementation is the compatibility oracle. Test equality,
// exact chosen entry identity and exact error message, not a wall-clock limit.
const odh::FarEntry& legacy_select(const std::vector<odh::FarEntry>& entries,const std::string& name){
 const auto fold=[](std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c+'a'-'A');return value;};
 const auto key=fold(name);const odh::FarEntry* out=nullptr;
 for(const auto& entry:entries)if(fold(entry.name)==key){
  if(out)throw odh::FormatError("saved appearance ambiguous FAR entry: "+name);out=&entry;
 }
 if(!out)throw odh::FormatError("saved appearance missing FAR entry: "+name);
 return *out;
}
void full_name_index_matrix(){
 using namespace odh_host::appearance_detail;
 auto matrix=[&](const std::vector<std::string>& names,const std::vector<std::string>& queries){
  std::vector<odh::FarEntry> entries;
  for(const auto& name:names){odh::FarEntry entry{};entry.name=name;entry.offset=static_cast<std::uint32_t>(entries.size());entries.push_back(entry);}
  const auto order=index_full_names(entries);
  check(order.size()==entries.size(),"full-name index stores one index per entry");
  std::vector<bool> seen(entries.size(),false);
  for(const auto index:order){check(index<entries.size()&&!seen[index],"full-name index is a permutation");seen[index]=true;}
  for(std::size_t n=0;n<entries.size();++n)check(entries[n].name==names[n]&&entries[n].offset==n,"index reordered or rewrote FAR entries");
  for(const auto& query:queries){
   const odh::FarEntry* expected=nullptr,*actual=nullptr;std::string expected_error,actual_error;
   try{expected=&legacy_select(entries,query);}catch(const odh::FormatError& error){expected_error=error.what();}
   try{actual=&select_full_name(entries,order,query);}catch(const odh::FormatError& error){actual_error=error.what();}
   check(actual==expected&&actual_error==expected_error,"indexed full-name selection differs from old strict scan");
  }
 };
 matrix({}, {"","absent"});
 matrix({"Folder/Rig.BCF","rig.bcf","Folder\\Rig.BCF","prefix","prefix-more","x.bcf"},
        {"FOLDER/RIG.bcf","RIG.BCF","FOLDER\\RIG.bcf","Rig","Folder/Rig","prefix","PREFIX-MORE","prefix-","z.bcf"});
 matrix({"same.BCF","SAME.bcf","unique.bcf","repeat","repeat"},
        {"SaMe.BcF","UNIQUE.BCF","repeat","missing"});
 const std::string high1=std::string(1,static_cast<char>(0xc4))+".BCF";
 const std::string high2=std::string(1,static_cast<char>(0xe4))+".bcf";
 const std::string nul("A\0Z",3);
 matrix({high1,high2,nul,"a"},{high1,high2,nul,std::string("a\0z",3),"a"});
 // Deterministic unsorted metadata cohort: every exact/case query plus misses
 // is compared to the old allocating scan. No randomness or timing assertion.
 std::vector<std::string> names,queries;
 for(unsigned n=0;n<512;++n){const auto key="Dir/Resource-"+std::to_string((n*73)%512)+".BCF";names.push_back(key);}
 for(const auto& name:names){queries.push_back(name);auto upper=name;for(auto& c:upper)if(c>='a'&&c<='z')c=static_cast<char>(c+'A'-'a');queries.push_back(upper);}
 queries.push_back("resource-0.bcf");queries.push_back("Dir/resource-512.bcf");matrix(names,queries);
 // Existing Archive maximum; selected boundary/middle keys plus misses only,
 // so the legacy reference scan does not make this a quadratic benchmark.
 names.clear();queries.clear();
 for(unsigned n=0;n<8192;++n)names.push_back("Bound/Entry-"+std::to_string(8191-n)+".bcf");
 for(const auto n:{0,1,4095,4096,8190,8191})queries.push_back("BOUND/ENTRY-"+std::to_string(n)+".BCF");
 queries.push_back("Bound/Entry-8192.bcf");matrix(names,queries);
}
}
int main(int argc,char** argv){try{
 if(argc>=8&&std::string(argv[1])=="--commercial-animal-body"){
  std::vector<std::string> sources;for(int i=8;i<argc;++i)sources.emplace_back(argv[i]);
  const auto game_root=std::filesystem::path(argv[2]).parent_path().parent_path().parent_path();
  std::vector<std::string> extra_skins;
  for(const auto* relative:{"ExpansionShared/SkinsBuy","Downloads"}){
   const auto path=game_root/relative;std::error_code error;
   if(std::filesystem::is_directory(path,error)&&!error)extra_skins.push_back(path.string());
  }
  SavedAppearanceResources resources(argv[2],argv[3],"adult-skeleton.cmx.bcf","adult",argv[4],sources,extra_skins);
  odh::AvatarBodyStrings body{};body.avatar_type=argv[5];body.body=argv[6];body.head=argv[7];
  const auto scene=resources.resolve_avatar_body(body);SceneRenderer renderer(scene);
  odh::WorldProjection camera;camera.width=320;camera.height=240;
  const auto frame=renderer.render(SceneState{},camera);
  std::cout<<"commercial animal body decoded: type="<<body.avatar_type<<" rig="<<scene.skeleton_name
      <<" rig_far="<<scene.skeleton_far<<" rig_entry="<<scene.skeleton_entry<<" parts="<<scene.parts.size()
      <<" vertices="<<renderer.vertices()<<" triangles="<<renderer.triangles()<<" texture_bytes="<<renderer.texture_bytes()
      <<" raster_samples="<<frame.image.shaded_samples<<" catalog_malformed="<<resources.diagnostics().malformed_resources
      <<" extra_skin_roots="<<extra_skins.size();
  for(const auto& root:extra_skins)std::cout<<" ["<<root<<"]";
  std::cout<<'\n';
  return 0;
 }
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
 full_name_index_matrix();
 odh::AnimationHeader h{};h.name="clip";odh::BoneMotion motion{};motion.frames=121;h.motions.push_back(motion);
 auto parsed=saved_animation_appearance("clip;6;1000;25;1000;0;7;8",h);
 check(parsed.frame==static_cast<double>(121.0f*(25.0f/1000.0f))&&parsed.priority==6&&parsed.weight_milli==1000&&parsed.speed_milli==1000);check(!parsed.loop&&!parsed.backwards&&!parsed.end_reached&&parsed.unknown1==7&&parsed.unknown2==8);
 parsed=saved_animation_appearance("CLIP;-10;-1000;0;500;1;1;1",h);check(parsed.loop&&parsed.backwards&&parsed.end_reached&&parsed.weight_milli==500);
 for(const auto* s:{"clip;1;2;3;4","clip;1;x;3;4;0","clip;1;+-1;3;4;0","clip;1;++1;3;4;0","other;1;2;3;4;0","clip;1;2147483648;3;4;0","clip;1;-2147483648;3;4;0"})rejects([&]{saved_animation_appearance(s,h);});
 parsed=saved_animation_appearance("clip;ignored;0;1001;-500;01;ignored;ignored;extra",h);
 check(parsed.speed_milli==0&&parsed.weight_milli==-500&&!parsed.loop&&parsed.frame>121&&parsed.unknown1==0);
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
 {
  const auto indexed=dir.path/"full-name-index.far";auto original=entries;
  original[0].first="Folder/MiXeD-Rig.BCF";
  original.push_back({"unused.bin","first"});original.push_back({"UNUSED.BIN","second"});far(indexed,original);
  SavedAppearanceResources selected(indexed.string(),t.string(),"fOLDER/mIXEd-rIG.bcf","adult",skins.string());
  odh::AvatarBodyStrings sample{};sample.body=sample.head="b123mafat";
  const auto scene=selected.resolve_body(sample);
  check(scene.skeleton_entry=="Folder/MiXeD-Rig.BCF"&&scene.parts.size()==2,
        "Archive consumer preserves exact original full name despite unrelated ambiguous keys");
  rejects([&]{SavedAppearanceResources missing(indexed.string(),t.string(),"MiXeD-Rig.BCF","adult",skins.string());});
  original.push_back({"folder/mixed-rig.bcf",rig()});far(indexed,original);
  rejects([&]{SavedAppearanceResources duplicate(indexed.string(),t.string(),"Folder/Mixed-Rig.BCF","adult",skins.string());});
 }
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
 {SceneState ghost;ghost.display_flags=1;ghost.is_ghost=true;
  const auto ordinary=loose_renderer.render(SceneState{},loose_camera).image;
  const auto colored=loose_renderer.render(ghost,loose_camera).image;
  check(colored.depth==ordinary.depth,"display tint preserves actual geometry and depth");
  for(std::size_t i=0;i<ordinary.depth.size();++i)if(std::isfinite(ordinary.depth[i])){
   const auto tint=odh::avatar_display_color(1,true);
   for(unsigned c=0;c<3;++c)check(colored.pixels[i][c]==(unsigned(ordinary.pixels[i][c])*tint[c]+127)/255,"source ShowAsGhost tint precedes actual alpha composition");
  }
  check(odh::avatar_display_color(0,true)[3]==64&&odh::avatar_display_color(1,true)[3]==168,
      "ShowAsGhost has source precedence over IsDead alpha");
  odh::World display_world;check(display_world.create(7,odh::EntityKind::avatar,{0,88,0})&&display_world.declare_object_word(7,34,1),"display actual World actor");
  apply_avatar_display(ghost,*display_world.find(7));check(!ghost.avatar_visible&&loose_renderer.render(ghost,loose_camera).image.shaded_samples==0,
      "live Hidden bank suppresses actual avatar geometry");
  check(display_world.declare_object_word(7,34,0)&&display_world.set_person_data(7,68,1)&&display_world.set_person_data(7,74,1),"live ghost/display banks");
  ghost=SceneState{};apply_avatar_display(ghost,*display_world.find(7));check(ghost.avatar_visible&&ghost.is_ghost&&ghost.display_flags==1,"visual flags are derived from current World banks");
 }

 // TS1 hands are independent optional appearances. TragicClown authors only
 // idle hands, but this contract applies to either side and every gesture.
 // Render each absent selection and compare it with explicit no-hand state:
 // body/head remain visible and no idle or opposite-side fallback is emitted.
 for(std::size_t absent_index=0;absent_index<6;++absent_index){
  auto partial=loose_body;partial.hands[absent_index].clear();const auto before=partial.hands;
  const auto scene=loose_resources.resolve_body(partial);check(scene.parts.size()==7);
  SceneRenderer renderer(scene);SceneState selected{};
  selected.hands.left=selected.hands.right=odh::HandGesture::none;
  const auto baseline=renderer.render(selected,loose_camera);
  if(absent_index%2)selected.hands.right=static_cast<odh::HandGesture>(absent_index/2);
  else selected.hands.left=static_cast<odh::HandGesture>(absent_index/2);
  const auto absent_frame=renderer.render(selected,loose_camera);
  check(baseline.image.shaded_samples>0&&absent_frame.image.tested_samples==baseline.image.tested_samples,
      "an empty selected hand emits no fallback geometry while body/head render");
  check(partial.hands==before,"resolving optional hands leaves authored slots unchanged");
 }
 auto idle_only=loose_body;for(std::size_t i=2;i<6;++i)idle_only.hands[i].clear();
 const auto idle_scene=loose_resources.resolve_body(idle_only);
 check(idle_scene.parts.size()==4&&idle_scene.parts[2].hand&&idle_scene.parts[3].hand);
 SceneRenderer idle_renderer(idle_scene);SceneState idle_state{};
 const auto idle_frame=idle_renderer.render(idle_state,loose_camera);
 idle_state.hands.left=idle_state.hands.right=odh::HandGesture::pointing;
 const auto pointing_frame=idle_renderer.render(idle_state,loose_camera);
 idle_state.hands.left=idle_state.hands.right=odh::HandGesture::fist;
 const auto fist_frame=idle_renderer.render(idle_state,loose_camera);
 check(idle_frame.image.tested_samples>pointing_frame.image.tested_samples&&
       pointing_frame.image.tested_samples==fist_frame.image.tested_samples&&pointing_frame.image.shaded_samples>0,
       "idle-only authored handgroup renders idle hands and removes both on absent pointing/fist");
 idle_state.hands.left=idle_state.hands.right=odh::HandGesture::idle;
 check(idle_renderer.render(idle_state,loose_camera).image.tested_samples==idle_frame.image.tested_samples,
       "returning from an absent gesture restores only the authored idle geometry");
 auto no_hands=loose_body;for(auto& hand:no_hands.hands)hand.clear();
 check(loose_resources.resolve_body(no_hands).parts.size()==2,
       "explicitly absent hand slots do not suppress required body/head");
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
 // Gameplay animals share body/mesh/texture providers, but their rig has
 // independent archive provenance and they do not receive human handgroups.
 {
  const auto base=dir.path/"species-base.far",animals=dir.path/"species-rigs.far";
  far(base,{{"legacy-explicit.bcf",rig("adult")},{"adult-skeleton.cmx.bcf",rig("adult")},
      {"child-skeleton.cmx.bcf",rig("child")}});
  auto mixed_kat=mixed_avatar("kat","embedded-kat-body",{"xskin-loose"});
  mixed_kat.resize(mixed_kat.size()-4);mixed_kat+=clip().substr(8);
  far(animals,{{"Skeletons\\UnrelatedFilename.bcf",mixed_avatar("dog","embedded-dog-body",{"xskin-loose"})},
      {"Skeletons\\kat_skeleton.cmx.bcf",mixed_kat}});
  const std::vector<std::string> sources{base.string(),animals.string()};
  SavedAppearanceResources species(base.string(),t.string(),"legacy-explicit.bcf","adult",skins.string(),sources);
  auto human=loose_body;human.avatar_type="adult";
  auto dog=loose_body;dog.avatar_type="dog";dog.age=27;dog.hands={};
  auto kat=dog;kat.avatar_type="kat";
  const auto dog_scene=species.resolve_avatar_body(dog),kat_scene=species.resolve_avatar_body(kat);
  check(dog_scene.animation_far==base.string()&&dog_scene.skeleton_far==animals.string()&&
      dog_scene.skeleton_entry=="Skeletons\\UnrelatedFilename.bcf"&&dog_scene.skeleton_name=="dog"&&
      kat_scene.skeleton_name=="kat"&&kat_scene.skeleton_far==animals.string()&&
      dog_scene.parts.size()==2&&kat_scene.parts.size()==2,
      "animal rig resolves by installed internal name independently of filename and base CFP archive");
  for(const auto& part:dog_scene.parts)check(!part.hand,"animals never receive tagged human hand geometry");
  SceneRenderer dog_renderer(dog_scene),kat_renderer(kat_scene);
  check(dog_renderer.vertices()==6&&kat_renderer.vertices()==6&&
      dog_renderer.render(SceneState{},loose_camera).image.shaded_samples>0&&
      kat_renderer.render(SceneState{},loose_camera).image.shaded_samples>0,
      "animal rig provenance reaches actual scene decode, skinning and rasterization");
  auto embedded_dog=dog;embedded_dog.body="embedded-dog-body";embedded_dog.head.clear();
  auto embedded_kat=kat;embedded_kat.body="embedded-kat-body";embedded_kat.head.clear();
  const auto embedded_dog_scene=species.resolve_avatar_body(embedded_dog),embedded_kat_scene=species.resolve_avatar_body(embedded_kat);
  check(embedded_dog_scene.parts.size()==1&&embedded_dog_scene.parts[0].appearance_far==animals.string()&&
      embedded_dog_scene.parts[0].appearance_entry=="Skeletons\\UnrelatedFilename.bcf"&&
      embedded_kat_scene.parts[0].appearance_entry=="Skeletons\\kat_skeleton.cmx.bcf",
      "mixed containers publish appearance names as well as their exact rigs");
  SceneRenderer embedded_dog_renderer(embedded_dog_scene),embedded_kat_renderer(embedded_kat_scene);
  check(embedded_dog_renderer.vertices()==3&&embedded_kat_renderer.vertices()==3&&
      embedded_dog_renderer.render(SceneState{},loose_camera).image.shaded_samples>0&&
      embedded_kat_renderer.render(SceneState{},loose_camera).image.shaded_samples>0,
      "complete catalog/part/renderer pipeline consumes mixed1/1/0 and mixed1/1/1 resources");
  auto without_head=dog;without_head.head.clear();
  check(species.resolve_avatar_body(without_head).parts.size()==1,"explicit empty animal head has no separate source appearance");
  auto ignored_hands=dog;for(auto& hand:ignored_hands.hands)hand="unparsed;animal-hand-field";
  check(species.resolve_avatar_body(ignored_hands).parts.size()==dog_scene.parts.size(),
      "source animal type ignores hand strings even when populated");
  // A large custom host has an unrelated filename and still overrides the
  // embedded appearance. The animation-only record has a malformed tail:
  // its zero prefix is safe for known visuals, never for a full diagnostic.
  {
   const auto patch=dir.path/"visual-patch.far";
   std::string long_mesh(220,'v');
   const auto custom=appearance("embedded-dog-body",{long_mesh,long_mesh,long_mesh});
   check(custom.size()>512);
   auto broken_clip=clip();broken_clip.append(32*1024,'x');
   const auto mesh_bytes=mesh(long_mesh,"loose-texture");
   const std::string first="unexpected.bcf",alias="shared.bcf";
   far(patch,{{first,custom},{alias,custom},{"clip-only.bcf",broken_clip},{long_mesh+".bmf",mesh_bytes}});
   // Two different logical names share a byte range in this FAR.
   // Make the second directory entry point at the first's physical bytes.
   {
    const auto table=16+2*custom.size()+broken_clip.size()+mesh_bytes.size();
    std::fstream edit(patch,std::ios::binary|std::ios::in|std::ios::out);
    edit.seekp(static_cast<std::streamoff>(table+4+16+first.size()+8));
    std::string offset;word(offset,16);edit.write(offset.data(),4);check(bool(edit));
   }
   auto patched=sources;patched.push_back(patch.string());
   SavedAppearanceResources full(base.string(),t.string(),"legacy-explicit.bcf","adult",skins.string(),patched);
   SavedAppearanceResources visual(base.string(),t.string(),"legacy-explicit.bcf","adult",skins.string(),patched,
       {},nullptr,{},SavedAppearanceResources::Declarations::VisualFirst);
   const auto scene=visual.resolve_avatar_body(embedded_dog);
   check(scene.parts.size()==3 && scene.parts[0].appearance_far==patch.string() &&
         scene.parts[0].appearance_entry==alias && scene.parts[0].bmf_entry==full.resolve_avatar_body(embedded_dog).parts[0].bmf_entry);
   auto absent=dog;absent.head="not-in-any-file";
   rejects([&]{visual.resolve_avatar_body(absent);});
   check(visual.diagnostics().malformed_resources==1 &&
         visual.diagnostics().resource_errors==full.diagnostics().resource_errors);
   check(visual.resolve_avatar_body(embedded_dog).parts.size()==3);
  }
  {
   // Portraits fit, but a later full scan exceeds the catalog name budget.
   // Keep the usable visual index after that failed upgrade.
   const auto clip_archive=dir.path/"many-animations.far";
   std::string clips(8,'\0');word(clips,3000);
   const auto declaration=clip().substr(12);
   for(int i=0;i<3000;++i)clips+=declaration;
   far(clip_archive,{{"rig.bcf",rig()},{"one.bcf",clips},{"two.bcf",clips},{"three.bcf",clips}});
   SavedAppearanceResources visual(clip_archive.string(),t.string(),"rig.bcf","adult",skins.string(),
       {animals.string()},{},nullptr,{},SavedAppearanceResources::Declarations::VisualFirst);
   check(visual.resolve_avatar_body(embedded_dog).parts.size()==1);
   rejects([&]{visual.diagnostics();});
   check(visual.resolve_avatar_body(embedded_dog).parts.size()==1);
  }
  const auto human_after=species.resolve_avatar_body(human),legacy_after=species.resolve_body(human);
  check(human_after.parts.size()==8&&human_after.skeleton_entry=="adult-skeleton.cmx.bcf"&&human_after.skeleton_far.empty()&&
      legacy_after.skeleton_entry=="legacy-explicit.bcf"&&legacy_after.skeleton_far.empty(),
      "animal resolution never changes human gameplay or explicitly configured tool rig state");
  auto missing_human_hands=human;missing_human_hands.hands={};
  const auto human_without_hands=species.resolve_avatar_body(missing_human_hands);
  check(human_without_hands.parts.size()==2&&
        !human_without_hands.parts[0].hand&&!human_without_hands.parts[1].hand,
        "gameplay human with explicit absent gestures retains exactly body/head without hand fallback");
  SceneRenderer human_without_hands_renderer(human_without_hands);
  check(human_without_hands_renderer.render(SceneState{},loose_camera).image.shaded_samples>0,
        "gameplay human body/head remain renderable with every hand slot absent");
  auto required_human_body=human;required_human_body.body.clear();
  rejects([&]{species.resolve_avatar_body(required_human_body);});
  auto required_human_head=human;required_human_head.head.clear();
  rejects([&]{species.resolve_avatar_body(required_human_head);});
  auto missing_body=dog;missing_body.body.clear();rejects([&]{species.resolve_avatar_body(missing_body);});
  auto missing_head=dog;missing_head.head="missing-animal-head";
  check(species.resolve_avatar_body(missing_head).parts.size()==1&&species.diagnostics().malformed_resources==0,
      "source-proved absent optional animal head adds no geometry");
  auto missing_human_head=human;missing_human_head.head="missing-animal-head";
  rejects([&]{species.resolve_avatar_body(missing_human_head);});
  auto malformed_head=dog;malformed_head.head="missing-animal-head,BAD";rejects([&]{species.resolve_avatar_body(malformed_head);});
  auto absent_head_texture=dog;absent_head_texture.head="b123mafat,HEAD=missing-texture";
  rejects([&]{species.resolve_avatar_body(absent_head_texture);});
  const auto unknown_head_far=dir.path/"unparsed-head-source.far";
  far(unknown_head_far,{{"maybe-head.bcf","bad"}});
  auto incomplete_sources=sources;incomplete_sources.push_back(unknown_head_far.string());
  SavedAppearanceResources incomplete(base.string(),t.string(),"legacy-explicit.bcf","adult",skins.string(),incomplete_sources);
  check(incomplete.diagnostics().malformed_resources==1&&incomplete.resolve_avatar_body(dog).parts.size()==2,
      "known present animal head remains usable despite unrelated incomplete metadata");
  rejects([&]{incomplete.resolve_avatar_body(missing_head);});
  const auto bad_cmx_root=dir.path/"unparsed-cmx";std::filesystem::create_directory(bad_cmx_root);
  save(bad_cmx_root/"maybe-head.cmx","unsupported CMX metadata");
  SavedAppearanceResources incomplete_cmx(base.string(),t.string(),"legacy-explicit.bcf","adult",skins.string(),sources,{bad_cmx_root.string()});
  check(incomplete_cmx.diagnostics().malformed_resources==1,"unparsed selected CMX also prevents absence authority");
  rejects([&]{incomplete_cmx.resolve_avatar_body(missing_head);});
  const auto legacy_rigs=dir.path/"legacy-animal-rigs.far",legacy_skins=dir.path/"legacy-animal-skins";
  far(legacy_rigs,{{"rig.bcf",mixed_avatar("dog","unused-body",{"xskin-loose"})},
      {"head1.bcf",appearance("ambiguous-animal-head",{"xskin-loose"})},
      {"head2.bcf",appearance("ambiguous-animal-head",{"xskin-loose"})}});
  std::filesystem::copy(skins,legacy_skins,std::filesystem::copy_options::recursive);
  save(legacy_skins/"maybe-head.cmx","unsupported CMX metadata");
  SavedAppearanceResources legacy_incomplete(legacy_rigs.string(),t.string(),"rig.bcf","dog",legacy_skins.string());
  check(legacy_incomplete.diagnostics().malformed_resources==1,"legacy loose CMX failure also prevents absence authority");
  rejects([&]{legacy_incomplete.resolve_avatar_body(missing_head);});
  auto ambiguous_head=dog;ambiguous_head.head="ambiguous-animal-head";
  try{legacy_incomplete.resolve_avatar_body(ambiguous_head);check(false);}
  catch(const odh::FormatError& error){check(std::string(error.what()).find("named resource ambiguous")!=std::string::npos,
      "present ambiguous animal head is never treated as absence");}
  const auto missing_mesh_far=dir.path/"head-with-missing-mesh.far";
  far(missing_mesh_far,{{"head.cmx.bcf",appearance("present-animal-head",{"not-installed-mesh"})}});
  auto missing_mesh_sources=sources;missing_mesh_sources.push_back(missing_mesh_far.string());
  SavedAppearanceResources missing_mesh(base.string(),t.string(),"legacy-explicit.bcf","adult",skins.string(),missing_mesh_sources);
  auto present_broken_head=dog;present_broken_head.head="present-animal-head";
  rejects([&]{missing_mesh.resolve_avatar_body(present_broken_head);});
  for(const auto& type:std::vector<std::string>{"","DOG","unrecognized",std::string("dog\0junk",8)}){
   auto invalid=dog;invalid.avatar_type=type;rejects([&]{species.resolve_avatar_body(invalid);});
  }
  auto cat=dog;cat.avatar_type="cat";rejects([&]{species.resolve_avatar_body(cat);});
  const auto cat_file=dir.path/"literal-cat-rig.far";far(cat_file,{{"CatRig.bcf",rig("cat")}});
  auto cat_sources=sources;cat_sources.push_back(cat_file.string());
  SavedAppearanceResources literal_cat(base.string(),t.string(),"legacy-explicit.bcf","adult",skins.string(),cat_sources);
  check(literal_cat.resolve_avatar_body(cat).skeleton_name=="cat"&&literal_cat.resolve_avatar_body(kat).skeleton_name=="kat",
      "cat and kat share classification but require their own literal source skeleton names");
  SavedAppearanceResources absent(base.string(),t.string(),"legacy-explicit.bcf","adult",skins.string(),{base.string()});
  rejects([&]{absent.resolve_avatar_body(dog);});
  try{absent.resolve_avatar_body(dog);check(false);}
  catch(const odh::FormatError& error){const std::string why=error.what();
   check(why.find("species=dog requested_rig=dog stage=rig")!=std::string::npos&&
       why.find("selected_rig=unselected")!=std::string::npos,"missing animal rig names the actual type and lookup stage");}
  const auto duplicate=dir.path/"duplicate-dog-rig.far";far(duplicate,{{"DifferentDogHost.bcf",rig("dog")}});
  auto duplicates=sources;duplicates.push_back(duplicate.string());
  SavedAppearanceResources ambiguous(base.string(),t.string(),"legacy-explicit.bcf","adult",skins.string(),duplicates);
  rejects([&]{ambiguous.resolve_avatar_body(dog);});
  try{ambiguous.resolve_avatar_body(dog);check(false);}
  catch(const odh::FormatError& error){const std::string why=error.what();
   check(why.find("selected_rig=ambiguous")!=std::string::npos&&why.find(animals.string())!=std::string::npos&&
       why.find(duplicate.string())!=std::string::npos,"ambiguous animal rig retains both selected host provenances");}
  const auto replacement=dir.path/"replaced-dog-file.far";
  far(replacement,{{"Later\\UNRELATEDFILENAME.BCF",rig("other")}});
  auto replaced=sources;replaced.push_back(replacement.string());
  SavedAppearanceResources selected_missing(base.string(),t.string(),"legacy-explicit.bcf","adult",skins.string(),replaced);
  rejects([&]{selected_missing.resolve_avatar_body(dog);});
  check(selected_missing.resolve_avatar_body(kat).skeleton_name=="kat",
      "later filename provider wins before rig indexing without resurrecting an earlier dog host");
  auto broken_rig=rig("dog");broken_rig.push_back('x');const auto malformed=dir.path/"malformed-dog-rig.far";
  far(malformed,{{"dog.bcf",broken_rig}});
  SavedAppearanceResources bad_rig(base.string(),t.string(),"legacy-explicit.bcf","adult",skins.string(),{base.string(),malformed.string()});
  rejects([&]{bad_rig.resolve_avatar_body(dog);});
  check(bad_rig.resolve_avatar_body(human).skeleton_name=="adult","rejected animal rig never changes human selection");
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
 // An exported mesh's internal label is not its resource identity. A
 // renamed BMF/SKN remains selected by the appearance's exact file binding.
 {
  auto aliased_mesh=loose_mesh();aliased_mesh.replace(0,aliased_mesh.find('\n'),"exporter-original-label");
  const auto alias_far=dir.path/"mesh-internal-alias.far";
  far(alias_far,{{"Skins\\XSKIN-LOOSE.SKN",aliased_mesh}});
  auto alias_sources=all_fars;alias_sources.push_back(alias_far.string());
  SavedAppearanceResources alias_resources(a.string(),t.string(),"rig.bcf","adult",skins.string(),alias_sources);
  const auto alias_scene=alias_resources.resolve_body(expansion_body);
  check(alias_scene.parts.front().mesh_far==alias_far.string(),"alias retains exact selected mesh provenance");
  SceneRenderer alias_renderer(alias_scene);
  check(alias_renderer.render(SceneState{},loose_camera).image.pixels==expansion_renderer.render(SceneState{},loose_camera).image.pixels,
        "internal mesh label does not change decoded geometry or rendering");
  auto wrong_binding=alias_scene;wrong_binding.parts.front().bmf_entry="Skins\\unbound.skn";
  rejects([&]{SceneRenderer rejected(wrong_binding);});
  const auto truncated_far=dir.path/"mesh-alias-truncated.far";
  far(truncated_far,{{"Skins\\XSKIN-LOOSE.SKN","exporter-original-label\n"}});
  auto truncated_sources=all_fars;truncated_sources.push_back(truncated_far.string());
  SavedAppearanceResources truncated(a.string(),t.string(),"rig.bcf","adult",skins.string(),truncated_sources);
  rejects([&]{truncated.resolve_body(expansion_body);});
 }
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
 // Character selection consumes first logical-key insertion order, separately
 // from the legacy alphabetic wardrobe export and later-host resource winners.
 const auto ordered_far=dir.path/"character-order.far";
 const std::string ordered_cmx="// synthetic CMX\nversion 300\n0\n1\nc900mc_order\n1\n0\n1\nROOT\nxskin-loose\n0\n0\n0\n";
 far(ordered_far,{{"first.cmx",ordered_cmx},{"second.bcf",appearance("b900mcchd_order",{"xskin-loose"})},
    {"z-last-texture.bmp",loose_bitmap()},{"a-next-texture.bmp",loose_bitmap()}});
 const auto overwrite_far=dir.path/"character-order-overwrite.far";
 far(overwrite_far,{{"third.bcf",appearance("c900mc_order",{"xskin-loose"})}});
 auto character_sources=all_fars;character_sources.push_back(ordered_far.string());character_sources.push_back(overwrite_far.string());
 const auto character_catalog=SavedAppearanceResources::resource_catalog(dir.path,a.string(),t.string(),skins.string(),character_sources,{});
 const auto order_index=[&](const auto& order,const std::string& name){
  for(std::size_t i=0;i<order.size();++i){check(order[i]<character_catalog.assets.size(),"ordered catalog index is bounded");if(character_catalog.assets[order[i]].name==name)return i;}
  throw std::runtime_error("ordered catalog key absent");
 };
 const auto head_position=order_index(character_catalog.appearance_order,"c900mc_order");
 check(head_position<order_index(character_catalog.appearance_order,"b900mcchd_order"),"mixed CMX before BCF remains first-key clothing order");
 check(character_catalog.assets[character_catalog.appearance_order[head_position]].asset=="third.bcf","later internal host wins without moving first-key insertion order");
 check(order_index(character_catalog.texture_order,"z-last-texture")<order_index(character_catalog.texture_order,"a-next-texture"),"texture provider order is preserved independently from alphabetic export");
 const auto legacy_order=SavedAppearanceResources::wardrobe_catalog(dir.path,a.string(),t.string(),skins.string(),character_sources,{});
 check(legacy_order.size()==character_catalog.assets.size(),"legacy resource export retains one selected receipt per key");
 for(std::size_t i=0;i<legacy_order.size();++i)check(legacy_order[i].name==character_catalog.assets[i].name&&legacy_order[i].asset==character_catalog.assets[i].asset,"ordered export does not change old wardrobe canonical receipts");
 // Wardrobe receipts come from the renderer's actual selected mapping,
 // not an independent filename census. Exercise both FAR-vs-loose texture
 // aliases and a later container whose internal appearance name is unchanged.
 const auto alias_far=dir.path/"logical-alias.far";
 const auto alias_bytes=appearance("ExpansionBody",{"xskin-loose"});
 far(alias_far,{{"DifferentFilename.cmx.bcf",alias_bytes}});
 auto alias_sources=with_override;alias_sources.push_back(alias_far.string());
 SavedAppearanceResources aliased(a.string(),t.string(),"rig.bcf","adult",skins.string(),alias_sources);
 const auto alias_scene=aliased.resolve_body(expansion_body);
 const auto wardrobe=SavedAppearanceResources::wardrobe_catalog(dir.path,a.string(),t.string(),skins.string(),alias_sources,{});
 const auto receipt=[&](odh::WardrobeAssetKind kind,const std::string& name)->const odh::WardrobeCatalogAsset&{
  const odh::WardrobeCatalogAsset* found=nullptr;
  for(const auto& value:wardrobe)if(value.kind==kind&&value.name==name){check(!found,"selected logical wardrobe name has one receipt");found=&value;}
  check(found!=nullptr,"selected renderer resource appears in wardrobe export");return *found;
 };
 const auto& outfit_receipt=receipt(odh::WardrobeAssetKind::appearance,"ExpansionBody");
 const auto& texture_receipt=receipt(odh::WardrobeAssetKind::texture,"loose-texture");
 check((dir.path/outfit_receipt.source).string()==alias_scene.parts.front().appearance_far&&
       outfit_receipt.asset==alias_scene.parts.front().appearance_entry&&outfit_receipt.asset=="DifferentFilename.cmx.bcf"&&
       outfit_receipt.fingerprint==odh::sha256(alias_bytes.data(),alias_bytes.size()),
       "internal-name alias receipt authenticates exact renderer-selected later container");
 // The SD schedule reads archive extents physically. The later declaration
 // still wins across containers and cache generations.
 for(int pass=0;pass<2;++pass){
  SavedAppearanceResources grouped(a.string(),t.string(),"rig.bcf","adult",skins.string(),
                                   alias_sources,{},nullptr,dir.path/"portrait-names.cache");
  const auto part=grouped.resolve_body(expansion_body).parts.front();
  check(part.appearance_entry==alias_scene.parts.front().appearance_entry &&
        part.appearance_far==alias_scene.parts.front().appearance_far &&
        part.texture_entry==alias_scene.parts.front().texture_entry &&
        part.texture_far==alias_scene.parts.front().texture_far,
        "physical declaration schedule changed logical appearance/texture selection");
 }
 const auto texture_bytes=loose_bitmap();
 check((dir.path/texture_receipt.source).string()==alias_scene.parts.front().texture_far&&
       texture_receipt.asset==alias_scene.parts.front().texture_entry&&texture_receipt.asset=="Later/LOOSE-TEXTURE.BMP"&&
       texture_receipt.fingerprint==odh::sha256(texture_bytes.data(),texture_bytes.size()),
       "FAR-over-loose texture receipt authenticates same exact bytes and path as renderer");
 for(std::size_t index=1;index<wardrobe.size();++index)check(wardrobe[index-1].producer_ordinal<wardrobe[index].producer_ordinal,
       "native provider exports strict candidate ordinals rather than one ordinal per container");
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
 p.carry_animation.bytes="clip;1;0;500;250;1;0;0";p.animation.bytes="clip;6;1000;250;500;0;7;8";
 auto imported=gestures.resolve_gestures(p,body);check(imported.current->weight_milli==500&&imported.carry&&
   imported.carry->frame==5&&imported.carry->speed_milli==0&&!imported.base_executed);
 p.carry_animation.bytes="";p.animation.bytes="clip;6;1000;250;1000;0;7;8";
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
 for(std::size_t selected=0;selected<8;++selected){
  auto absent_body=constructed;
  if(selected==0)absent_body.body.clear();else if(selected==1)absent_body.head.clear();else absent_body.hands[selected-2].clear();
  if(selected<2)rejects([&]{gestures.resolve_body(absent_body);});
  else {const auto scene=gestures.resolve_body(absent_body);check(scene.parts.size()==9);
   for(const auto& part:scene.parts)check(!part.hand||part.hand->right!=((selected-2)%2==1)||
       part.hand->gesture!=static_cast<odh::HandGesture>((selected-2)/2));}
 }
 for(const auto& raw:std::vector<std::string>{",BODY=override","body,","body,BODY=","body,=override","body,BODY=override;other","body,BODY=override,extra","body=override","body;other",std::string(4097,'x'),std::string("body\0junk",9)}){
     auto bad=constructed;bad.body=raw;rejects([&]{gestures.resolve_body(bad);});bad=constructed;bad.head=raw;rejects([&]{gestures.resolve_body(bad);});}
 auto absent_body=constructed;absent_body.body="missing-body,BODY=override";rejects([&]{gestures.resolve_body(absent_body);});
 absent_body=constructed;absent_body.head="missing-head";rejects([&]{gestures.resolve_body(absent_body);});
 absent_body=constructed;absent_body.body="body,BODY=missing-texture";rejects([&]{gestures.resolve_body(absent_body);});
 absent_body=constructed;absent_body.hands[2]="leftPoint,HAND=missing-texture";rejects([&]{gestures.resolve_body(absent_body);});
 absent_body=constructed;absent_body.hands[2]="leftPoint,HAND=bad=second";rejects([&]{gestures.resolve_body(absent_body);});
 for(int i=0;i<20;++i)check(gestures.resolve_body(constructed).parts[7].texture_entry=="skinmed.bmp");
 for(const auto& raw:std::vector<std::string>{",HAND=override","leftIdle,","leftIdle,HAND=","leftIdle,=override","leftIdle,HAND=override;other","leftIdle,HAND=override,extra","leftIdle=override","leftIdle;other",std::string(4097,'x'),std::string("leftIdle\0junk",13)}){auto bad=body;bad.hands[0]=raw;rejects([&]{gestures.resolve_gestures(p,bad);});}
 auto optional_gestures=body;for(auto& hand:optional_gestures.hands)hand.clear();
 const auto no_gestures=gestures.resolve_gestures(p,optional_gestures);
 check(no_gestures.scene.parts.size()==4&&no_gestures.current->raw==p.animation.bytes&&
       no_gestures.base->raw==p.base_animation.bytes&&p.left_hand.bytes=="unresolved-raw-left",
       "saved gesture resolution accepts explicit empty hand slots without altering raw animations/hands");
 auto invalid_hand=body;invalid_hand.hands[2]=std::string("bad\0hand",8);
 bool hand_context=false;try{gestures.resolve_gestures(p,invalid_hand);}
 catch(const odh::FormatError& error){const std::string message=error.what();
  hand_context=message.find("BodySTR hand19 reference length=8 empty=0 embedded_nul=1")!=std::string::npos&&
      message.find("saved appearance empty, oversized or embedded-NUL name")!=std::string::npos;}
 check(hand_context,"invalid nonempty hand remains rejected with field and original cause");
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
