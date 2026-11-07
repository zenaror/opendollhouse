// SPDX-License-Identifier: MPL-2.0
// Synthetic IFF metadata/sprite presence. Not a decoded-image/rendering proof.
#include "architecture_edit_resources.hpp"
#include "odh/formats.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace odh;using namespace odh_host;
namespace {
void need(bool b,const char*s){if(!b)throw std::runtime_error(s);}
void put(std::string&s,std::uint32_t v,int n){for(int i=0;i<n;i++)s.push_back(char((v>>(8*i))&255));}
void at(std::string&s,std::size_t o,std::uint32_t v,int n){for(int i=0;i<n;i++)s[o+i]=char((v>>(8*(n-i-1)))&255);}
std::string chunk(const char*type,std::uint16_t id,const std::string&payload){std::string s(76,0);s.replace(0,4,type);at(s,4,76+payload.size(),4);at(s,8,id,2);return s+payload;}
std::string iff(const std::vector<std::string>&cs){std::string s="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";s.resize(64,0);for(auto&c:cs)s+=c;return s;}
std::string strings(const std::vector<std::string>&v){std::string s;put(s,65535,2);put(s,v.size(),2);for(const auto&x:v){s+=x;s.push_back(0);}return s;}
void write(const std::filesystem::path&p,const std::string&s){std::ofstream f(p,std::ios::binary);f.write(s.data(),s.size());need(bool(f),"fixture write");}
struct Temp{std::filesystem::path p=std::filesystem::temp_directory_path()/("odh-edit-resources-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Temp(){std::filesystem::create_directory(p);}~Temp(){std::error_code e;std::filesystem::remove_all(p,e);}};
template<class F>void rejects(F f,const char*why){bool rejected=false;try{f();}catch(const FormatError&){rejected=true;}need(rejected,why);}
}
int main(){
 Temp t;auto build=t.p/"Build.iff",floors=t.p/"floors.iff",walls=t.p/"walls.iff";
 auto b=iff({chunk("STR#",0x81,strings({"70","Wall","wall description","10","Picket","picket description","20","Iron","iron description","30","Privacy","privacy description","40","Rail","rail description"})),chunk("STR#",0x82,strings({"5","Floor1","floor one","9","Floor2","floor two","12","LastExcluded","unused"})),chunk("STR#",0x83,strings({"3","Paper1","paper one","7","Paper2","paper two"})),chunk("STR#",0x85,strings({"75","Pool","pool desc","15","Water","water desc"}))});
 write(build,b);std::vector<std::string>spr;for(auto id:{1,2,257,258,513,514})spr.push_back(chunk("SPR2",id,""));write(floors,iff(spr));write(walls,iff({}));
 auto flr=t.p/"custom.flr",wll=t.p/"custom.wll";write(flr,iff({chunk("STR#",0,strings({"Custom floor","37","real floor metadata"}))}));write(wll,iff({chunk("STR#",0,strings({"Custom paper","41","real paper metadata"}))}));
 auto catalog=discover_architecture_resources({{flr.string(),ArchitectureResourceSourceKind::loose_file},{wll.string(),ArchitectureResourceSourceKind::loose_file}});
 ArchitectureEditResources r(build,floors,walls,catalog,14); // Missing language chooses entire English set.
 auto material=[&](auto kind,auto id){return r.material(kind,id);};
 need(material(ArchitectureEditMaterialKind::floor,1).price==5&&material(ArchitectureEditMaterialKind::floor,2).price==9,"source global floors use Build triples");
 need(material(ArchitectureEditMaterialKind::floor,3).status==ArchitectureEditLookup::absent,"last global floor triple is intentionally excluded");
 need(material(ArchitectureEditMaterialKind::wallpaper,1).price==3&&material(ArchitectureEditMaterialKind::wallpaper,2).price==7,"wallpaper metadata entries exist even with known absent sprites");
 need(material(ArchitectureEditMaterialKind::floor,65535).price==75&&material(ArchitectureEditMaterialKind::floor,65534).price==15,"pool and water source entries");
 need(material(ArchitectureEditMaterialKind::floor,256).price==37&&material(ArchitectureEditMaterialKind::wallpaper,256).price==41,"dynamic material IDs are exact prepared catalog IDs, never raw aliases");
 need(material(ArchitectureEditMaterialKind::wall_style,2).price==10&&material(ArchitectureEditMaterialKind::wall_style,13).price==20,"named structural styles use source WallStyleToIndex ordering");
 need(material(ArchitectureEditMaterialKind::wall_style,3).price==70&&material(ArchitectureEditMaterialKind::wall_style,3).purchasable,"TryGetValue miss defaults0: nonUI loaded style inherits first triple and command admission");
 need(material(ArchitectureEditMaterialKind::wall_style,0).status==ArchitectureEditLookup::unknown&&material(ArchitectureEditMaterialKind::wall_style,256).status==ArchitectureEditLookup::unknown,"missing or live object-authored structural styles remain explicit unknown");
 std::size_t style_count=0;for(const auto&d:r.descriptors())if(d.kind==ArchitectureEditMaterialKind::wall_style){++style_count;if(d.id==2)need(d.name=="Picket"&&d.default_pattern==248,"UI descriptor uses source name and fence pattern pairing");}
 need(style_count==5,"source UI selects five styles, distinct from command out-parameter quirk");
 write(build,"invalid after cache construction");write(flr,"invalid after cache construction");need(material(ArchitectureEditMaterialKind::floor,256).price==37&&material(ArchitectureEditMaterialKind::wall_style,1).price==70,"lookup does no I/O or reread after initialization");write(build,b);
 ArchitectureEditResourceLimits tiny;tiny.metadata_bytes=1;rejects([&]{ArchitectureEditResources bad(build,floors,walls,catalog,1,tiny);},"metadata budget rejects atomically");
 write(flr,iff({chunk("STR#",0,strings({"Bad","not money","bad"}))}));rejects([&]{ArchitectureEditResources bad(build,floors,walls,catalog);},"malformed actual catalog price is not zero");
 write(flr,iff({chunk("STR#",0,strings({"Good","37","good"}))}));write(floors,iff({chunk("SPR2",1,"")}));rejects([&]{ArchitectureEditResources bad(build,floors,walls,catalog);},"global floor initialization requires all source sprites");
 std::cout<<"architecture prepared-domain resource provider passed\n";
}
