#include "linux_cas_catalog.hpp"
#include "odh/formats.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
void need(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct TempDir {
    std::filesystem::path path;
    TempDir(){auto p=(std::filesystem::temp_directory_path()/"odh-cas-catalog-XXXXXX").string();
        std::vector<char> bytes(p.begin(),p.end());bytes.push_back('\0');
        const auto* result=::mkdtemp(bytes.data());if(!result)throw std::runtime_error("mkdtemp failed");path=result;}
    ~TempDir(){std::error_code error;std::filesystem::remove_all(path,error);}
};
void write(const std::filesystem::path& path,const std::string& data){
    std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);
    out.write(data.data(),static_cast<std::streamsize>(data.size()));if(!out)throw std::runtime_error("fixture write failed");
}
void put32(std::string& out,std::uint32_t value){
    for(unsigned i=0;i<4;++i)out.push_back(static_cast<char>((value>>(8U*i))&0xffU));
}
std::string far1(const std::vector<std::pair<std::string,std::string>>& entries){
    std::string out="FAR!byAZ";put32(out,1);put32(out,0);
    std::vector<std::uint32_t> offsets;
    for(const auto& entry:entries){offsets.push_back(static_cast<std::uint32_t>(out.size()));out+=entry.second;}
    const auto manifest=static_cast<std::uint32_t>(out.size());
    for(unsigned i=0;i<4;++i)out[12+i]=static_cast<char>((manifest>>(8U*i))&0xffU);
    put32(out,static_cast<std::uint32_t>(entries.size()));
    for(std::size_t i=0;i<entries.size();++i){
        put32(out,static_cast<std::uint32_t>(entries[i].second.size()));
        put32(out,static_cast<std::uint32_t>(entries[i].second.size()));
        put32(out,offsets[i]);put32(out,static_cast<std::uint32_t>(entries[i].first.size()));
        out+=entries[i].first;
    }
    return out;
}
std::string cmx(const std::string& appearance){
    return "// Character File.\nversion 300\n0\n1\n"+appearance+"\n0\n0\n0\n0\n";
}
}
int main(){try{
    TempDir temp;
    const auto root=temp.path;
    write(root/"GameData/Skins/B001.cmx",cmx("b001mafat_base"));
    write(root/"GameData/Skins/C001.cmx",cmx("c001ma_base"));
    write(root/"GameData/Skins/not-cas.cmx","not a CMX\n");
    write(root/"GameData/Skins/B001MAFatMed_base.bmp","pixels");
    write(root/"GameData/Skins/B001MAFat_default.bmp","pixels");
    write(root/"GameData/Skins/C001MAMed_base.bmp","pixels");
    write(root/"GameData/Skins/huao_base.bmp","pixels");
    write(root/"ExpansionShared/SkinsBuy/B002MAfitmed_extra.bmp","pixels");
    write(root/"ExpansionShared/SkinsBuy/B002.cmx",cmx("b002mafit_extra"));
    write(root/"ExpansionPack1/Skins/Clothes.far",far1({
        {R"(skins\C003.cmx)",cmx("c003ma_archive")},
        {R"(skins\C003MALgt_archive.bmp)","pixels"},
        {R"(skins\B003.cmx)",cmx("b003mafit_archive")},
        {R"(skins\B003MAFitMed_archive.bmp)","pixels"},
    }));

    const auto catalog=odh_host::scan_linux_cas_catalog(root);
    need(catalog.stats().loose_cmx_files==4&&catalog.stats().skipped_appearance_files==1,
        "catalog statistics include valid and skipped loose CMX files: "+
        std::to_string(catalog.stats().loose_cmx_files)+"/"+
        std::to_string(catalog.stats().skipped_appearance_files));
    need(catalog.stats().texture_names==7,"catalog indexed loose and archived skin textures");
    need(catalog.stats().far_archives==1&&catalog.stats().far_appearance_entries==2,
        "catalog indexed CAS metadata inside FAR archives");
    const auto adult_heads=catalog.heads("ma","med");
    need(adult_heads.size()==1&&adult_heads[0].appearance=="c001ma_base"&&
        adult_heads[0].texture=="c001mamed_base","head selection requires a texture for the chosen skin");
    need(catalog.heads("ma","drk").empty(),"head without selected-skin texture is excluded");
    const auto adult_bodies=catalog.bodies("ma","med");
    need(adult_bodies.size()==3&&adult_bodies[0].appearance=="b001mafat_base"&&
        adult_bodies[0].texture=="b001mafatmed_base"&&adult_bodies[0].body_type=="fat"&&
        adult_bodies[0].handgroup_texture=="_base","body and hand assets are matched to actual filenames");
    const auto body_fallback=catalog.bodies("ma","drk");
    need(body_fallback.size()==3&&body_fallback[0].texture=="b001mafat_default"&&
        body_fallback[1].texture=="b002mafitmed_extra",
        "body uses the Simitone fallback prefix when skin-specific texture is absent");
    need(adult_bodies[1].appearance=="b002mafit_extra"&&adult_bodies[1].body_type=="fit",
        "expansion clothing is included and retains its three-letter body type");
    const auto archived_head=catalog.heads("ma","lgt");
    need(archived_head.size()==1&&archived_head[0].appearance=="c003ma_archive"&&
        archived_head[0].texture=="c003malgt_archive",
        "Windows-style FAR entry paths resolve head textures by basename on Linux");
    const auto archived_body=catalog.bodies("ma","med");
    need(archived_body.size()==3&&archived_body.back().appearance=="b003mafit_archive"&&
        archived_body.back().texture=="b003mafitmed_archive",
        "Windows-style FAR entry paths resolve body textures by basename on Linux");
    bool rejected=false;
    try{(void)catalog.bodies("../", "med");}catch(const std::invalid_argument&){rejected=true;}
    need(rejected,"catalog rejects path-like avatar type input");
    rejected=false;
    try{(void)odh_host::scan_linux_cas_catalog(root/"missing");}catch(const odh::FormatError&){rejected=true;}
    need(rejected,"catalog rejects a missing game directory");
    std::cout<<"Linux CAS clothing catalog checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
