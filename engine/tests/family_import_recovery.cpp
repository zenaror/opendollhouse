/* SPDX-License-Identifier: MPL-2.0 */
// Real IFF/FAM readers and filesystem writes; no commercial fixtures or IO stubs.
#include "content/ContentManager.h"
#include "content/TS1FamilyImport.h"
#include "files/formats/iff/chunks/NBRS.h"
#include "files/formats/iff/chunks/OBJT.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace fs=std::filesystem;
using namespace FSO::Content;
using namespace FSO::Content::TS1;
using namespace FSO::Files::Formats::IFF::Chunks;
namespace {
constexpr std::uint32_t person=0xd3a79911;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void word(std::vector<std::uint8_t>& bytes,std::size_t pos,std::uint32_t value){
    for(unsigned i=0;i<4;++i)bytes.at(pos+i)=std::uint8_t(value>>(i*8));
}
std::vector<std::uint8_t> read(const fs::path& path){
    std::vector<std::uint8_t> bytes;require(RawIff::ReadFile(path.string(),bytes),"cannot read fixture/result");return bytes;
}
void write(const fs::path& path,const std::vector<RawIffChunk>& chunks){
    require(RawIff::WriteFileAtomic(path.string(),RawIff::Build(chunks)),"cannot write fixture");
}
RawIffChunk neighbors(bool populated){
    NBRS records;
    if(populated){
        auto n=std::make_unique<Neighbour>();n->Version=4;n->Name="user00003";n->NeighbourID=3;
        n->GUID=person;n->PersonMode=5;n->PersonData.assign(88,0);
        records.NeighbourByID[3]=n.get();records.DefaultNeighbourByGUID[person]=3;
        records.Entries.push_back(std::move(n));
    }
    st::Writer out(st::Endian::Little);records.Write(nullptr,out);
    RawIffChunk result;result.Type="NBRS";result.ID=1;result.Data=out.ToArray();return result;
}
RawIffChunk strings(const char* type,unsigned id,const std::vector<std::string>& values){
    RawIffChunk result;result.Type=type;result.ID=id;result.Data=RawIff::MakeSTRFormat3(values);return result;
}
// A retail-imported lot retains its old objt type and stores the new GUID in Gtab #1.
void verifyImportedLotTable() {
    constexpr uint32_t guest = 0xc0ffee01, unrelated = 0x13579bdf;
    RawIffChunk table; table.Type = "objt"; table.ID = 0;
    st::Writer entries(st::Endian::Little);
    entries.WriteUInt32(0); entries.WriteUInt32(2); entries.WriteUInt32(0x6f626a74);
    uint16_t id = 0;
    for (uint32_t guid : {person, unrelated}) {
        entries.WriteUInt32(guid);
        for (int i = 0; i < 4; ++i) entries.WriteUInt16(0);
        entries.WriteUInt16(++id); entries.WriteUInt16(uint16_t(OBJDType::Person));
        entries.WriteUInt16(0); // empty name and word padding
    }
    table.Data = entries.ToArray();
    RawIffChunk mapping; mapping.Type = "Gtab"; mapping.ID = 1; mapping.Data.assign(32, 0);
    word(mapping.Data,8,0x47746162); word(mapping.Data,12,2);
    word(mapping.Data,16,person); word(mapping.Data,20,guest);
    word(mapping.Data,24,guest); word(mapping.Data,28,0xc0ffee02);
    auto decoded = [&](const std::vector<RawIffChunk>& chunks) {
        FSO::Files::Formats::IFF::IffFile file;
        file.Read(RawIff::Build(chunks));
        return file.Get<OBJT>(0)->Entries;
    };
    auto original = decoded({table});
    require(original.at(0).GUID == person, "plain lot changed a resident GUID");
    auto moved = decoded({table,mapping});
    require(moved.size() == 2 && moved[0].GUID == guest && moved[0].TypeID == 1 &&
            moved[1].GUID == unrelated && moved[1].TypeID == 2, "imported lot lost resident or object identity");
    mapping.Data.pop_back();
    bool rejected = false;
    try { decoded({table,mapping}); } catch (const std::exception&) { rejected = true; }
    require(rejected, "truncated Gtab accepted");
}
struct Fixture {
    fs::path root;
    TS1FamFile family;
    explicit Fixture(fs::path path):root(std::move(path)){
        fs::create_directories(root/"Characters");fs::create_directories(root/"Houses");
        auto content=Content::Get();content->WorldObjects=std::make_unique<WorldObjectProvider>(content);
        RawIffChunk object;object.Type="OBJD";object.ID=1;object.Data.assign(256,0);
        word(object.Data,0,138);word(object.Data,28,TS1CharacterFiles::TEMPLATE_PERSON_GUID);
        object.Data[18]=2;
        write(root/"person-template.iff",{object});
        GameObjectReference reference;reference.ID=TS1CharacterFiles::TEMPLATE_PERSON_GUID;
        reference.FileName=(root/"person-template.iff").string();reference.Source=GameObjectSource::Standalone;
        content->WorldObjects->Entries[reference.ID]=reference;
        write(root/"Neighborhood.iff",{neighbors(false)});
        RawIffChunk fami;fami.Type="FAMI";fami.ID=10;fami.Data.assign(44,0);
        word(fami.Data,12,56);word(fami.Data,20,20000);word(fami.Data,36,1);word(fami.Data,40,person);
        RawIffChunk house;house.Type="HOUS";house.ID=1;house.Data={11,22,33,44};
        write(root/"synthetic.FAM",{fami,neighbors(true),strings("FAMs",10,{"Test household"}),
            strings("uChr",3,{"adult","authored-body","authored-head"}),
            strings("CTSS",3,{"Test resident","Authored biography"}),house});
        require(TS1FamilyImport::Read((root/"synthetic.FAM").string(),family),"real FAM reader rejected fixture");
        require(family.Members.size()==1&&family.Members[0].GUID==person,"FAM lost resident identity");
    }
    auto import(bool recovery=false){TS1FamilyImport::Options options;options.BootstrapRecovery=recovery;
        return TS1FamilyImport::Import(root.string(),family,options);}
    std::size_t characters() const {std::size_t count=0;for(const auto& item:fs::directory_iterator(root/"Characters"))
        if(item.path().extension()==".iff")++count;return count;}
    fs::path templates() const {
        const auto data=root/"installed";
        fs::create_directories(data/"TemplateFamilyUnleashed");fs::create_directories(data/"TemplateCommunity");
        fs::copy_file(root/"synthetic.FAM",data/"TemplateFamilyUnleashed/synthetic.FAM",fs::copy_options::overwrite_existing);
        RawIffChunk old;old.Type="HOUS";old.ID=1;old.Data={8,7,6,5};
        write(data/"TemplateCommunity/House56.iff",{old});return data;
    }
    void oldReceipt(const fs::path& data) {
        require(import(true).Ok,"prepare already committed family");
        fs::copy_file(data/"TemplateCommunity/House56.iff",root/"Houses/House56.iff",fs::copy_options::overwrite_existing);
        fs::create_directories(root/"Patch");
        fs::copy_file(root/"synthetic.FAM",root/"Patch/synthetic.FAM.applied");
    }
    void verify() const {
        std::vector<RawIffChunk> chunks;require(RawIff::Parse(read(root/"Neighborhood.iff"),chunks),"bad committed neighborhood");
        auto nbrs=RawIff::Find(chunks,"NBRS",1);require(nbrs,"committed neighborhood has no residents");
        NBRS records;st::Reader in(nbrs->Data,st::Endian::Little);records.Read(nullptr,in);
        require(records.NeighbourByID.size()==1&&records.DefaultNeighbourByGUID.count(person)==1,
            "retry duplicated or changed the resident GUID");
        unsigned families=0;for(const auto& c:chunks)if(c.Type=="FAMI")++families;
        require(families==1&&characters()==1,"retry duplicated family or character file");
        require(fs::is_regular_file(root/"Houses/House56.iff"),"completed family has no house");
    }
};
}
int main(){
    const auto root=fs::temp_directory_path()/("odh-family-import-recovery-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{fs::path path;~Cleanup(){Content::Get()->WorldObjects.reset();std::error_code ec;fs::remove_all(path,ec);}}cleanup{root};
    try{
        verifyImportedLotTable();
        {
            Fixture f(root/"normal");require(f.import().Ok,"normal real family import failed");f.verify();
            const auto old=read(f.root/"Neighborhood.iff");fs::remove(f.root/"Houses/House56.iff");
            const auto skipped=f.import();require(skipped.Ok&&skipped.Skipped,"explicit duplicate import no longer skips");
            require(!fs::exists(f.root/"Houses/House56.iff"),"ordinary duplicate import recreated deliberately deleted house");
            require(f.import(true).Ok,"authorized recovery of previously committed family failed");f.verify();
            require(read(f.root/"Neighborhood.iff")==old,"house-only recovery changed family or resident data");
        }
        {
            Fixture f(root/"house-failure");const auto before=read(f.root/"Neighborhood.iff");
            fs::remove(f.root/"Houses");{std::ofstream block(f.root/"Houses");block<<"blocked directory";}
            require(!f.import(true).Ok,"failed house write reported completed family");
            require(read(f.root/"Neighborhood.iff")==before,"failed house write committed residents before their house");
            fs::remove(f.root/"Houses");fs::create_directory(f.root/"Houses");
            require(f.import(true).Ok,"authorized retry failed after removing IO obstruction");f.verify();
        }
        {
            Fixture f(root/"custom-house");RawIffChunk custom;custom.Type="HOUS";custom.ID=1;custom.Data={9,8,7,6};
            write(f.root/"Houses/House56.iff",{custom});const auto before=read(f.root/"Houses/House56.iff");
            require(f.import(true).Ok,"family import with existing house failed");f.verify();
            require(read(f.root/"Houses/House56.iff")==before,"bootstrap overwrote existing house");
        }
        {
            Fixture f(root/"pending-patch");fs::create_directory(f.root/"Patch");
            fs::copy_file(f.root/"synthetic.FAM", f.root/"Patch/installer.FAM");
            auto results=TS1FamilyImport::ApplyPendingPatches(f.root.string());
            require(results.size()==1&&results[0].Ok,"explicit installer queue failed");f.verify();
            require(!fs::exists(f.root/"Patch/installer.FAM")&&fs::exists(f.root/"Patch/installer.FAM.applied"),
                    "committed installer source was not preserved/consumed");
            const auto neighborhood=read(f.root/"Neighborhood.iff");
            require(TS1FamilyImport::ApplyPendingPatches(f.root.string()).empty(),"consumed queue replayed");
            // Simulate interruption after the atomic neighbourhood commit and
            // before queue acknowledgement. The exact family must be reused.
            fs::rename(f.root/"Patch/installer.FAM.applied",f.root/"Patch/installer.FAM");
            results=TS1FamilyImport::ApplyPendingPatches(f.root.string());
            require(results.size()==1&&results[0].Ok&&results[0].Skipped,"committed installer retry duplicated family");
            f.verify();require(read(f.root/"Neighborhood.iff")==neighborhood,"installer retry rewrote committed residents");
            // Consuming the source prevents a later intentionally absent family
            // from being repopulated on load, without an absence-based heuristic.
            write(f.root/"Neighborhood.iff",{neighbors(false)});
            require(TS1FamilyImport::ApplyPendingPatches(f.root.string()).empty(),"deleted family resurrected from global defaults");
        }
        {
            Fixture f(root/"pending-installer-house");const auto data=f.templates();
            fs::copy_file(data/"TemplateCommunity/House56.iff",f.root/"Houses/House56.iff");
            const auto old=read(f.root/"Houses/House56.iff");fs::create_directory(f.root/"Patch");
            fs::copy_file(f.root/"synthetic.FAM",f.root/"Patch/synthetic.FAM");
            const auto result=TS1FamilyImport::ApplyPendingPatches(f.root.string(),data.string());
            require(result.size()==1&&result[0].Ok,"pending installer house was not completed");f.verify();
            require(read(f.root/"Houses/House56.iff")==RawIff::Build(f.family.HouseChunks),"pending family retained incompatible template house");
            require(read(f.root/"Houses/House56.iff.pre-family-template")==old,"pending installer lost permanent preimage");
        }
        {
            Fixture f(root/"applied-installer-house");const auto data=f.templates();f.oldReceipt(data);
            const auto old=read(f.root/"Houses/House56.iff"),nbh=read(f.root/"Neighborhood.iff");
            std::map<fs::path,std::vector<std::uint8_t>> chars;
            for(const auto& file:fs::directory_iterator(f.root/"Characters"))chars[file.path()]=read(file.path());
            const auto result=TS1FamilyImport::ApplyPendingPatches(f.root.string(),data.string());
            require(result.size()==1&&result[0].Ok&&!result[0].Skipped,"applied installer house was not repaired");
            require(read(f.root/"Houses/House56.iff")==RawIff::Build(f.family.HouseChunks),"repair did not install corresponding FAM house");
            require(read(f.root/"Houses/House56.iff.pre-family-template")==old,"repair preimage differs");
            require(read(f.root/"Neighborhood.iff")==nbh,"house repair rewrote roster");
            for(const auto& file:chars)require(read(file.first)==file.second,"house repair reset imported character");
            require(TS1FamilyImport::ApplyPendingPatches(f.root.string(),data.string()).empty(),"repair was not idempotent");
        }
        for(int mode=0;mode<8;++mode){
            Fixture f(root/("preserve-applied-"+std::to_string(mode)));const auto data=f.templates();f.oldReceipt(data);
            auto house=f.root/"Houses/House56.iff";
            if(mode==0){RawIffChunk edit;edit.Type="HOUS";edit.ID=1;edit.Data={1,2,3,4};write(house,{edit});}
            if(mode==1)fs::remove(house);
            if(mode==2)write(f.root/"Neighborhood.iff",{neighbors(false)}); // deleted household
            if(mode==3){ // moved household
                std::vector<RawIffChunk> chunks;require(RawIff::Parse(read(f.root/"Neighborhood.iff"),chunks),"parse fixture");
                for(auto& chunk:chunks)if(chunk.Type=="FAMI")word(chunk.Data,12,57);
                write(f.root/"Neighborhood.iff",chunks);
            }
            if(mode==4){std::ofstream changed(f.root/"Patch/synthetic.FAM.applied",std::ios::app|std::ios::binary);changed.put('x');}
            if(mode==5)for(const auto& file:fs::directory_iterator(f.root/"Characters"))fs::remove(file.path());
            if(mode==6){ // changed family member, despite same lot and resident still present
                std::vector<RawIffChunk> chunks;require(RawIff::Parse(read(f.root/"Neighborhood.iff"),chunks),"parse fixture");
                for(auto& chunk:chunks)if(chunk.Type=="FAMI")word(chunk.Data,40,person+1);
                write(f.root/"Neighborhood.iff",chunks);
            }
            const auto before=fs::exists(house)?read(house):std::vector<std::uint8_t>{};
            const auto roster=read(f.root/"Neighborhood.iff");
            const auto result=TS1FamilyImport::ApplyPendingPatches(f.root.string(),mode==7?std::string():data.string());
            require(result.empty(),"consumed receipt replayed absent, edited, untrusted, moved or incomplete family");
            require((fs::exists(house)?read(house):std::vector<std::uint8_t>{})==before&&read(f.root/"Neighborhood.iff")==roster,
                "ineligible repair changed house or roster");
            require(!fs::exists(house.string()+".pre-family-template"),"ineligible repair wrote preimage");
        }
        {
            Fixture f(root/"conflicting-preimage");const auto data=f.templates();f.oldReceipt(data);
            const auto house=f.root/"Houses/House56.iff";const auto before=read(house);
            {std::ofstream bad(house.string()+".pre-family-template");bad<<"preexisting unrelated bytes";}
            const auto result=TS1FamilyImport::ApplyPendingPatches(f.root.string(),data.string());
            require(result.size()==1&&!result[0].Ok&&read(house)==before,"conflicting preimage was overwritten or ignored");
        }
#if defined(__linux__)
        {
            Fixture f(root/"external-patch");
            fs::create_directory(root/"outside-queue");
            fs::copy_file(f.root/"synthetic.FAM",root/"outside-queue/installer.FAM");
            fs::create_directory_symlink(root/"outside-queue",f.root/"Patch");
            const auto before=read(f.root/"Neighborhood.iff");
            const auto results=TS1FamilyImport::ApplyPendingPatches(f.root.string());
            require(results.size()==1&&!results[0].Ok,"external symlink queue admitted");
            require(fs::exists(root/"outside-queue/installer.FAM")&&read(f.root/"Neighborhood.iff")==before,
                    "rejected external queue changed files");
        }
#endif
        std::cout<<"real FAM: import, house failure/retry, committed-family recovery and existing-house preservation passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
