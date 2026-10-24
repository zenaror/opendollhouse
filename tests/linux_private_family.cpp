#include "linux_family_overlay_store.hpp"
#include "linux_private_family.hpp"

#include "odh/character_catalog.hpp"
#include "odh/family.hpp"
#include "odh/neighborhood.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void add16le(std::vector<std::uint8_t>& b,std::uint16_t v){b.push_back(v);b.push_back(v>>8);}
void add16be(std::vector<std::uint8_t>& b,std::uint16_t v){b.push_back(v>>8);b.push_back(v);}
void add32le(std::vector<std::uint8_t>& b,std::uint32_t v){for(unsigned i=0;i<4;++i)b.push_back(v>>(8*i));}
void add32be(std::vector<std::uint8_t>& b,std::uint32_t v){for(unsigned i=0;i<4;++i)b.push_back(v>>(8*(3-i)));}
void set16le(std::vector<std::uint8_t>& b,std::size_t p,std::uint16_t v){b.at(p)=v;b.at(p+1)=v>>8;}
void set32le(std::vector<std::uint8_t>& b,std::size_t p,std::uint32_t v){for(unsigned i=0;i<4;++i)b.at(p+i)=v>>(8*i);}
void chunk(std::vector<std::uint8_t>& b,const char* type,std::uint16_t id,const std::string& label,const std::vector<std::uint8_t>& data){
    b.insert(b.end(),type,type+4);add32be(b,static_cast<std::uint32_t>(76+data.size()));add16be(b,id);add16be(b,0x10);
    std::vector<std::uint8_t> name(64,0);std::copy(label.begin(),label.end(),name.begin());b.insert(b.end(),name.begin(),name.end());b.insert(b.end(),data.begin(),data.end());
}
std::vector<std::uint8_t> character(std::uint32_t guid,bool body){
    const std::string signature="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    std::vector<std::uint8_t> b(signature.begin(),signature.end());b.resize(64,0);
    std::vector<std::uint8_t> objd(216,0);set32le(objd,0,138);set16le(objd,18,2);set32le(objd,28,guid);chunk(b,"OBJD",128,"Person",objd);
    if(body){std::vector<std::uint8_t> str{0xFD,0xFF};add16le(str,3);for(const auto* text:{"skin","body","head"}){str.push_back(1);str.insert(str.end(),text,text+std::strlen(text));str.push_back(0);str.push_back(0);}str.insert(str.end(),3,0xA3);chunk(b,"STR#",200,"bodystring",str);}
    return b;
}
struct TempDir{std::filesystem::path path;TempDir(){std::string p=(std::filesystem::temp_directory_path()/"odh-private-family-XXXXXX").string();std::vector<char>b(p.begin(),p.end());b.push_back('\0');const auto* d=::mkdtemp(b.data());if(!d)throw std::runtime_error("mkdtemp failed");path=d;}~TempDir(){std::error_code e;std::filesystem::remove_all(path,e);}};
void write(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes){std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!out)throw std::runtime_error("fixture write failed");}
std::vector<std::uint8_t> neighborhood(){
    const std::string signature="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";std::vector<std::uint8_t>b(signature.begin(),signature.end());b.resize(64,0);
    odh::Family family{};family.version=9;family.family_number=1;family.budget=1000;family.unknown=24;family.member_guids={0x11112222};
    chunk(b,"FAMI",7,"",odh::encode_fami(family));
    odh::NeighborRecord n{};n.version=10;n.unknown3=17;n.name="Eve";n.mystery_zero=0;n.person_mode=9;n.person_data=std::vector<std::int16_t>(256,0);n.neighbor_id=1;n.guid=0x11112222;n.unknown_neg_one=-1;
    chunk(b,"NBRS",1,"",odh::encode_nbrs(odh::Nbrs{55,63,{{1,n}},0}));return b;
}
}
int main(){try{
    TempDir temp;const auto root=temp.path;const auto installed=root/"game/UserData/Characters";const auto private_dir=root/"player/Characters";
    const auto neighborhood_path=root/"game/UserData/Neighborhood.iff";const auto overlay_path=root/"player/overlay.odhp";
    std::filesystem::create_directories(installed);write(installed/"Existing.iff",character(0x11112222,false));
    const auto original_neighborhood=neighborhood();write(neighborhood_path,original_neighborhood);
    std::vector<odh_host::LinuxPrivateFamilyMemberDraft> drafts(1);drafts[0].name_utf8="Olá";drafts[0].biography_utf8="Bio";drafts[0].language=14;drafts[0].body_strings={{1,"body"},{2,"head"}};drafts[0].person_data=std::vector<std::int16_t>(88,0);drafts[0].person_data[2]=500;
    const auto template_bytes=character(0x7FD96B54,true);std::uint32_t next_guid=0x22334455;
    const auto family=odh_host::create_linux_private_family(template_bytes,installed,private_dir,neighborhood_path,overlay_path,"Família",drafts,20000,[&]{return next_guid++;});
    need(family.family_chunk_id==0&&family.family_number==2&&family.members.size()==1,"family creation used wrong free family identity");
    need(std::filesystem::exists(family.members[0].path),"private character file is missing");
    const auto catalog=odh::read_character_catalog(private_dir);need(catalog.size()==1&&catalog[0].definition.guid==family.members[0].guid,"private family member is not discoverable");
    const auto saved=odh_host::read_linux_family_overlay(overlay_path);need(saved&&saved->creations.size()==1,"family sidecar was not persisted");
    need(saved->creations[0].family_name==std::string("Fam\xedlia",7)&&saved->creations[0].members[0].neighbor_id==2,"family/neighbor records were not linked");
    need(saved->creations[0].members[0].guid==family.members[0].guid&&(*saved->creations[0].members[0].person_data)[61]==0,"private GUID or FAMI link is incorrect");
    need(std::filesystem::file_size(neighborhood_path)==original_neighborhood.size(),"source neighborhood size changed");
    {std::ifstream in(neighborhood_path,std::ios::binary);std::vector<std::uint8_t> after(original_neighborhood.size());in.read(reinterpret_cast<char*>(after.data()),static_cast<std::streamsize>(after.size()));need(after==original_neighborhood,"source neighborhood bytes changed");}

    auto invalid=drafts;invalid[0].name_utf8="猫";const auto before=std::filesystem::file_size(family.members[0].path);bool rejected=false;
    try{(void)odh_host::create_linux_private_family(template_bytes,installed,private_dir,neighborhood_path,overlay_path,"Second",invalid,20000,[]{return 0x33445566;});}
    catch(const std::invalid_argument&){rejected=true;}
    need(rejected&&std::filesystem::file_size(family.members[0].path)==before,"unrepresentable name changed existing private data");

    auto two=drafts;two.push_back(drafts[0]);std::size_t guid_calls=0;
    rejected=false;try{(void)odh_host::create_linux_private_family(template_bytes,installed,private_dir,neighborhood_path,overlay_path,"Rollback",two,20000,[&]{++guid_calls;return guid_calls==1?0x44556677U:0x44556677U;});}
    catch(const std::runtime_error&){rejected=true;}
    need(rejected,"repeated GUID exhaustion did not fail the family transaction");
    const auto after_rollback=odh::read_character_catalog(private_dir);need(after_rollback.size()==1,"failed family left an orphan private character");
    const auto unchanged=odh_host::read_linux_family_overlay(overlay_path);need(unchanged&&odh::encode_family_overlay(*unchanged)==odh::encode_family_overlay(*saved),"failed family changed the saved overlay");
    std::cout<<"linux private family transaction checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
