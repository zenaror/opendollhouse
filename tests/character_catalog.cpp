#include "odh/character_catalog.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
template<class F> void rejects(F fn, const char* reason) {
    try { fn(); } catch (const odh::FormatError&) { return; }
    throw std::runtime_error(reason);
}
void be32(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>(value >> 24));
    out.push_back(static_cast<char>(value >> 16));
    out.push_back(static_cast<char>(value >> 8));
    out.push_back(static_cast<char>(value));
}
void le16(std::string& out, std::size_t offset, std::uint16_t value) {
    out.at(offset)=static_cast<char>(value);
    out.at(offset+1)=static_cast<char>(value>>8);
}
void le32(std::string& out, std::size_t offset, std::uint32_t value) {
    le16(out,offset,static_cast<std::uint16_t>(value));
    le16(out,offset+2,static_cast<std::uint16_t>(value>>16));
}
std::string objd(std::uint32_t guid, std::uint16_t type=2,
                 std::uint32_t version=138) {
    std::string value(32,'\0');
    le32(value,0,version); le16(value,18,type); le32(value,28,guid);
    return value;
}
std::string chunk(const char* type, std::uint16_t id, const std::string& payload) {
    std::string out(type,4); be32(out,static_cast<std::uint32_t>(76+payload.size()));
    out.push_back(static_cast<char>(id>>8));out.push_back(static_cast<char>(id));
    out.append(2,'\0');out.append(64,'\0');out+=payload;
    return out;
}
std::string iff(std::initializer_list<std::string> chunks) {
    std::string out(64,'\0');
    const std::string signature="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    out.replace(0,signature.size(),signature);
    for (const auto& value : chunks) out+=value;
    return out;
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("fixture write failed");
}
struct Temp {
    std::filesystem::path path;
    Temp() {
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        path=std::filesystem::temp_directory_path()/("odh-character-catalog-"+std::to_string(stamp));
        std::filesystem::create_directories(path/"Characters");
    }
    ~Temp() { std::error_code ec;std::filesystem::remove_all(path,ec); }
};
}

int main() {
    try {
        Temp temp;const auto dir=temp.path/"Characters";
        write(dir/"User00009.iff",iff({chunk("OBJD",128,objd(0x9009))}));
        write(dir/"User00002.IFF",iff({chunk("OBJD",128,objd(0x2002)),
                                       chunk("OBJD",3,objd(0x3333,1)),
                                       chunk("OBJD",9,objd(0x9999,2,136))}));
        write(dir/"README.txt","ignored");
        std::filesystem::create_directory(dir/"folder.iff");

        auto catalog=odh::read_character_catalog(dir);
        require(catalog.size()==2,"only supported person definitions are indexed");
        require(catalog[0].asset=="User00002.IFF"&&catalog[0].chunk_id==128&&
                catalog[0].definition.guid==0x2002&&catalog[1].asset=="User00009.iff"&&
                catalog[1].definition.guid==0x9009,"IFF file order is deterministic and metadata retained");
        const odh::DefinitionIndex index(catalog);
        require(index.unique(0x2002)&&!index.unique(0x3333),
                "catalog exposes only supported character definitions to household resolution");

        odh::CharacterCatalogLimits limits;limits.files=1;
        rejects([&]{(void)odh::read_character_catalog(dir,limits);},
                "file count limit applies before reading");
        limits={};limits.file_bytes=1;
        rejects([&]{(void)odh::read_character_catalog(dir,limits);},
                "per-file size limit applies before parsing");
        limits={};limits.directory_entries=2;
        rejects([&]{(void)odh::read_character_catalog(dir,limits);},
                "all directory entries count toward the scan bound");
        limits={};limits.total_file_bytes=1;
        rejects([&]{(void)odh::read_character_catalog(dir,limits);},
                "total size limit applies before parsing");
        const auto malformed=temp.path/"Malformed";std::filesystem::create_directory(malformed);
        write(malformed/"broken.iff","not an iff");
        rejects([&]{(void)odh::read_character_catalog(malformed);},
                "malformed character archive is surfaced instead of silently skipped");
        rejects([&]{(void)odh::read_character_catalog(temp.path/"missing");},
                "missing character directory rejected");

        std::cout<<"PASS deterministic bounded Character IFF scan and OBJD person filtering\n";
        std::cout<<"PASS malformed files and missing folders fail explicitly\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
