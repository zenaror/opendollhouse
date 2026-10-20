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
std::string clip(){std::string b;word(b,0);word(b,0);word(b,1);text(b,"clip");text(b,"pose");for(int i=0;i<5;++i)word(b,0);word(b,1);text(b,"ROOT");word(b,10);for(int i=0;i<7;++i)word(b,0);return b;}
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
 entries.pop_back();far(a,entries);const auto duplicate_textures=dir.path/"duplicate-textures.far";
 far(duplicate_textures,{{"unmatched.bmp",""},{"skinlgt.bmp",""},{"override.bmp",""},{"OVERRIDE.bmp",""},{"skinmed.bmp",""},{"x.bmp",""}});
 SavedAppearanceResources ambiguous_texture(a.string(),duplicate_textures.string(),"rig.bcf","adult");rejects([&]{ambiguous_texture.resolve_body(constructed);});
 SavedAppearanceResources moved_body(std::move(gestures));rejects([&]{gestures.resolve_body(constructed);});
 std::cout<<checks<<" saved appearance checks passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
