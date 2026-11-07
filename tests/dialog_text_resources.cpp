#include "../tools/dialog_text_resources.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace odh_host;
namespace {
unsigned checks=0;void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void le16(std::string& out,std::size_t at,std::uint16_t n){out.at(at)=char(n);out.at(at+1)=char(n>>8);}
void le32(std::string& out,std::size_t at,std::uint32_t n){le16(out,at,std::uint16_t(n));le16(out,at+2,std::uint16_t(n>>16));}
std::string chunk(const char* type,std::uint16_t id,const std::string& payload){std::string out(type,4);const auto size=std::uint32_t(76+payload.size());for(int shift:{24,16,8,0})out.push_back(char(size>>shift));out.push_back(char(id>>8));out.push_back(char(id));out.append(66,'\0');return out+payload;}
std::string table(std::initializer_list<std::pair<std::uint8_t,std::string>> entries){std::string out(4,'\0');le16(out,0,0xfffd);le16(out,2,std::uint16_t(entries.size()));for(const auto& entry:entries){out.push_back(char(entry.first));out+=entry.second;out.append(2,'\0');}return out;}
DialogTextResources::Frozen pin(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);auto bytes=std::make_shared<const std::string>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());return [path,bytes](const DialogTextTable& key)->std::shared_ptr<const std::string>{return key.archive==path.string()&&key.entry.empty()?bytes:nullptr;};}
void write(const std::filesystem::path& path,const std::string& chunks){std::string out(64,'\0');const std::string signature="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";out.replace(0,signature.size(),signature);out+=chunks;std::ofstream file(path,std::ios::binary);file.write(out.data(),std::streamsize(out.size()));require(bool(file),"write owned resource fixture");}
struct Temp{std::filesystem::path path=std::filesystem::temp_directory_path()/("odh-dialog-resources-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}};
}
int main(){try{
    Temp temp;const auto file=temp.path/"character.iff";std::string objd(84,'\0');le32(objd,0,138);le16(objd,18,2);le16(objd,22,65535);le32(objd,28,909);le16(objd,82,300);
    write(file,chunk("OBJD",128,objd)+chunk("CTSS",300,table({{1,"English name"},{1,"English catalog"},{14,"Nome real"},{14,""}}))+chunk("STR#",301,table({{1,"Nested $Neighbor"},{14,"Texto $Neighbor"}})));
    odh::ObjectDefinition definition{};definition.version=138;definition.guid=909;definition.object_type=2;definition.sub_index=-1;
    odh::DefinitionIndex index({{file.string(),{},128,definition}});
    const auto frozen=pin(file);
    DialogTextResources resources(index,14,[](const auto& text,auto){return text;},frozen);
    const auto name=resources.catalog(909,0),description=resources.catalog(909,1);
    require(name.availability==DialogTextAvailability::value&&name.utf8=="Nome real","catalog display name uses exact admitted CTSS and selected language");
    require(description.availability==DialogTextAvailability::value&&description.utf8.empty(),"known empty localized catalog text never becomes an English fallback");
    require(resources.catalog(999,0).availability==DialogTextAvailability::unavailable,"unknown GUID is not invented known-absent catalog");
    const auto message=resources.string({file.string(),{},301},0);
    require(message.availability==DialogTextAvailability::value&&message.utf8=="Texto $Neighbor","dynamic STR lookup retains resource language and commands for common expansion");
    require(resources.string({file.string(),{},999},0).availability==DialogTextAvailability::absent,"known absent STR table is an explicit fallback candidate");
    require(resources.string({file.string(),{},301},999).utf8=="Missing String!!!","known STR missing entry uses existing source literal");
    require(resources.string({(temp.path/"absent.iff").string(),{},301},0).availability==DialogTextAvailability::unavailable,"failed I/O remains unknown, never absent table");
    DialogTextResources no_codec(index,14,{},frozen);require(no_codec.catalog(909,0).availability==DialogTextAvailability::unavailable,"source text needs an explicit decoding authority");
    const auto duplicate=temp.path/"duplicate.iff";write(duplicate,chunk("OBJD",128,objd)+chunk("CTSS",300,table({{1,"First"}}))+chunk("CTSS",300,table({{1,"Second"}})));
    odh::DefinitionIndex duplicates({{duplicate.string(),{},128,definition}});DialogTextResources ambiguous(duplicates,1,[](const auto& text,auto){return text;},pin(duplicate));
    require(ambiguous.catalog(909,0).availability==DialogTextAvailability::unavailable,"duplicate CTSS refuses entire lookup rather than leaking first result");
    write(file,chunk("OBJD",128,objd)+chunk("CTSS",300,table({{14,"Altered keeping the same GUID"}})));
    DialogTextResources fresh_reader(index,14,[](const auto& text,auto){return text;},frozen);
    require(fresh_reader.catalog(909,0).utf8=="Nome real","first catalog use reads admitted frozen bytes despite later same-GUID CTSS path mutation");
    DialogTextResources no_pin(index,14,[](const auto& text,auto){return text;},{});
    require(no_pin.catalog(909,0).availability==DialogTextAvailability::unavailable,"existing path never substitutes for missing frozen resource authority");
    const auto multi=temp.path/"multi.iff";auto master=objd,child=objd;le16(master,18,4);le16(child,18,4);
    le16(master,20,1);le16(master,22,65535);le16(master,82,310);le16(child,20,1);le16(child,22,0);le32(child,28,910);
    write(multi,chunk("OBJD",128,master)+chunk("OBJD",129,child)+chunk("CTSS",310,table({{1,"Master name"},{1,"Master description"}}))+chunk("CTSS",300,table({{1,"Own name"},{1,"Own description"}})));
    auto master_definition=definition;master_definition.object_type=4;master_definition.master_id=1;master_definition.sub_index=-1;master_definition.is_multitile=true;
    auto child_definition=master_definition;child_definition.guid=910;child_definition.sub_index=0;
    odh::DefinitionIndex multis({{multi.string(),{},128,master_definition},{multi.string(),{},129,child_definition}});
    DialogTextResources multipart(multis,1,[](const auto& text,auto){return text;},pin(multi));
    require(multipart.display(910,DialogEntityCatalogPolicy::prefer_master).utf8=="Master name"&&
        multipart.display(910,DialogEntityCatalogPolicy::own).utf8=="Own name"&&multipart.catalog(910,1).utf8=="Own description",
        "object master display differs from own ToString and own CatalogLocal description");
    const auto empty_master=temp.path/"empty-master.iff";
    write(empty_master,chunk("OBJD",128,master)+chunk("OBJD",129,child)+chunk("CTSS",300,table({{1,"Own fallback"}})));
    odh::DefinitionIndex fallback_index({{empty_master.string(),{},128,master_definition},{empty_master.string(),{},129,child_definition}});
    DialogTextResources fallback(fallback_index,1,[](const auto& text,auto){return text;},pin(empty_master));
    require(fallback.display(910,DialogEntityCatalogPolicy::prefer_master).utf8=="Own fallback","known missing master CTSS falls back to own ToString catalog");
    const auto absent=temp.path/"absent-catalog.iff";le16(objd,82,0);write(absent,chunk("OBJD",128,objd));
    odh::DefinitionIndex absent_index({{absent.string(),{},128,definition}});DialogTextResources guid_fallback(absent_index,1,[](const auto& text,auto){return text;},pin(absent));
    require(guid_fallback.display(909,DialogEntityCatalogPolicy::own).utf8=="38D","known missing own CTSS and empty label use source ToString uppercase GUID, never invented empty name");
    const auto legacy=temp.path/"legacy-cas.iff";le16(objd,80,2000);
    write(legacy,chunk("OBJD",128,objd)+chunk("CTSS",2000,table({{1,"Admitted CAS name"}})));
    odh::DefinitionIndex legacy_index({{legacy.string(),{},128,definition}});const auto legacy_bytes=pin(legacy);
    DialogTextResources imported(legacy_index,1,[](const auto& text,auto){return text;},legacy_bytes);
    require(imported.catalog(909,0).availability==DialogTextAvailability::absent,"Catalog0 import with CTSS2000 never self-authorizes native CAS provenance");
    unsigned authorization_calls=0;
    DialogTextResources admitted(legacy_index,1,[](const auto& text,auto){return text;},legacy_bytes,
        [&](const odh::DefinitionCandidate& exact,const std::shared_ptr<const std::string>& bytes){++authorization_calls;
            return exact.definition.guid==909&&exact.chunk_id==128&&bytes==legacy_bytes({legacy.string(),{},0});});
    require(admitted.catalog(909,0).utf8=="Admitted CAS name"&&authorization_calls==1,"explicit exact-definition immutable-byte authority preserves legacy CAS names");
    DialogTextResources denied(legacy_index,1,[](const auto& text,auto){return text;},legacy_bytes,
        [](const auto&,const auto&){return false;});
    require(denied.catalog(909,0).availability==DialogTextAvailability::absent,"failed CAS provenance never overrides raw catalog metadata");
    std::cout<<checks<<" exact dialog string-resource checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
