#include "linux_private_character_store.hpp"

#include "odh/character_catalog.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
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
void add32be(std::vector<std::uint8_t>& b,std::uint32_t v){for(unsigned i=0;i<4;++i)b.push_back(v>>(8*(3-i)));}
void set16le(std::vector<std::uint8_t>& b,std::size_t p,std::uint16_t v){b.at(p)=v;b.at(p+1)=v>>8;}
void set32le(std::vector<std::uint8_t>& b,std::size_t p,std::uint32_t v){for(unsigned i=0;i<4;++i)b.at(p+i)=v>>(8*i);}
void chunk(std::vector<std::uint8_t>& b,const char* type,std::uint16_t id,const std::string& label,const std::vector<std::uint8_t>& data){
    b.insert(b.end(),type,type+4);add32be(b,static_cast<std::uint32_t>(76+data.size()));add16be(b,id);add16be(b,0x10);
    std::vector<std::uint8_t> name(64,0);std::copy(label.begin(),label.end(),name.begin());b.insert(b.end(),name.begin(),name.end());b.insert(b.end(),data.begin(),data.end());
}
std::vector<std::uint8_t> character(std::uint32_t guid,bool with_body){
    const std::string signature="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    std::vector<std::uint8_t> b(signature.begin(),signature.end());b.resize(64,0);
    std::vector<std::uint8_t> objd(216,0);set32le(objd,0,138);set16le(objd,18,2);set32le(objd,28,guid);chunk(b,"OBJD",128,"Person",objd);
    if(with_body){std::vector<std::uint8_t> str{0xFD,0xFF};add16le(str,3);
        for(const auto* text:{"skin","body","head"}){str.push_back(1);str.insert(str.end(),text,text+std::strlen(text));str.push_back(0);str.push_back(0);}str.insert(str.end(),3,0xA3);chunk(b,"STR#",200,"bodystring",str);}
    return b;
}
struct TempDir{
    std::filesystem::path path;
    TempDir(){std::string pattern=(std::filesystem::temp_directory_path()/"odh-private-character-XXXXXX").string();std::vector<char> writable(pattern.begin(),pattern.end());writable.push_back('\0');const auto* made=::mkdtemp(writable.data());if(!made)throw std::runtime_error("mkdtemp failed");path=made;}
    ~TempDir(){std::error_code ec;std::filesystem::remove_all(path,ec);}
};
void write(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes){std::filesystem::create_directories(path.parent_path());FILE* f=std::fopen(path.c_str(),"wb");if(!f)throw std::runtime_error("cannot create fixture");const auto n=std::fwrite(bytes.data(),1,bytes.size(),f);const int close=std::fclose(f);if(n!=bytes.size()||close)throw std::runtime_error("cannot finish fixture");}
}
int main(){try{
    TempDir temp;const auto installed=temp.path/"game/UserData/Characters";const auto private_dir=temp.path/"player/Characters";
    constexpr std::uint32_t installed_guid=0x10203040,private_guid=0x50607080,chosen_guid=0x90A0B0C0;
    write(installed/"Existing.iff",character(installed_guid,false));
    write(private_dir/"User00001.iff",character(private_guid,false));
    const auto template_bytes=character(0x7FD96B54,true);
    odh::TemplatePersonRequest request{};request.language=14;request.name="Name";request.biography="Bio";
    const std::vector<std::uint32_t> supplied{installed_guid,private_guid,chosen_guid};std::size_t next=0;
    const auto created=odh_host::create_linux_private_character(template_bytes,installed,private_dir,request,[&]{return supplied.at(next++);});
    need(created.guid==chosen_guid&&next==3,"GUID allocation did not reject installed and private collisions");
    need(created.path.filename()=="User00002.iff","private character filename did not avoid existing file");
    struct stat mode{};need(::stat(created.path.c_str(),&mode)==0&&S_ISREG(mode.st_mode),"private character file was not created");
    need((mode.st_mode&0777)==0600,"private character file is not owner-only");
    const auto catalog=odh::read_character_catalog(private_dir);
    need(catalog.size()==2,"created person was not discovered by the private catalog");
    need(std::any_of(catalog.begin(),catalog.end(),[&](const auto& c){return c.definition.guid==chosen_guid;}),"private catalog did not expose created GUID");

    const auto target=temp.path/"symlink-target";std::filesystem::create_directories(target);
    const auto link=temp.path/"private-link";std::filesystem::create_directory_symlink(target,link);
    bool rejected=false;try{(void)odh_host::create_linux_private_character(template_bytes,installed,link,request,[]{return 0xABCDEF01;});}
    catch(const odh::FormatError&){rejected=true;}
    need(rejected,"symlinked private character directory was accepted");
    need(std::filesystem::is_empty(target),"symlink rejection wrote through to target");
    std::cout<<"linux private character store checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
