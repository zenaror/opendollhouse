/* SPDX-License-Identifier: MPL-2.0 */
// Synthetic neighbourhood records; no game installation or proprietary fixtures.
#include "content/TS1NeighbourProvider.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourhoodWriter.h"
#include "files/formats/iff/IffRawFile.h"
#include <filesystem>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace FSO::Content::TS1;
using namespace FSO::Files::Formats::IFF;
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Fixture {
    std::filesystem::path Root;
    TS1NeighborhoodProvider Provider;
    static FSO::Content::Content* Manager(const std::filesystem::path& root) {
        auto content=FSO::Content::Content::Get();content->SimsCompleteDir=root.string();return content;
    }
    Fixture() : Root(std::filesystem::temp_directory_path()/("odh-visit-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))), Provider(Manager(Root)) {
        Require(std::filesystem::create_directory(Root),"temporary fixture directory already exists");
        Provider.MainResource=std::make_unique<IffFile>();
        IffRawFile file;
        for(int i=1;i<=2;i++){
            auto* family=new Chunks::FAMI();family->ChunkID=(uint16_t)i;family->ChunkProcessed=true;family->HouseNumber=i*5;
            family->FamilyGUIDs={(uint32_t)(100+i)};family->RuntimeSubset=family->FamilyGUIDs;
            Provider.MainResource->AddChunk(family);Provider.FamilyForHouse[(int16_t)family->HouseNumber]=family;
            st::Writer writer;family->Write(nullptr,writer);file.Set("FAMI",(uint16_t)i,writer.ToArray());
            TS1SavedVisit visit;visit.Home=(int16_t)family->HouseNumber;visit.House=21;visit.Caller=100+i;visit.Generation=i;visit.Party=family->FamilyGUIDs;
            Provider.GameState.SavedVisits[(int16_t)i]=visit;
        }
        Require(file.Save(Path()),"could not write initial synthetic IFF");
    }
    ~Fixture(){std::error_code error;std::filesystem::remove_all(Root,error);}
    std::string Path()const{return (Root/"Neighborhood.iff").string();}
};
int main(){try{
    Fixture f;std::string error;
    Require(TS1NeighbourhoodWriter::Save(&f.Provider,f.Path(),{},&error),"visit writer failed");
    f.Provider.GameState.SavedVisits.clear();TS1NeighbourhoodWriter::LoadFamilySpells(&f.Provider,f.Path());
    Require(f.Provider.GameState.SavedVisits.size()==2,"one family's visit replaced the other");
    const auto& first=f.Provider.GameState.SavedVisits.at(1);const auto& second=f.Provider.GameState.SavedVisits.at(2);
    Require(first.Caller==101&&second.Caller==102&&first.House==21&&second.House==21,"visit identities did not survive");
    Require(first.SnapshotPath("profile",1)!=second.SnapshotPath("profile",2),"families share a writable destination snapshot");
    IffRawFile valid;Require(valid.Load(f.Path()),"cannot reload synthetic IFF");
    for(int mutation=0;mutation<5;mutation++){
        auto file=valid;auto* c=file.Find("ODHV",1);Require(c!=nullptr,"writer omitted visit chunk");
        if(mutation==0)c->Data[0]=2; // unsupported version
        if(mutation==1)c->Data[30]=9; // over-limit party count
        if(mutation==2)c->Data[32]=255; // unknown family member
        if(mutation==3)c->Data[6]=5; // destination equals home
        if(mutation==4)c->Data[26]=0; // missing immutable generation
        Require(file.Save(f.Path()),"cannot write corrupt fixture");
        bool rejected=false;try{TS1NeighbourhoodWriter::LoadFamilySpells(&f.Provider,f.Path());}catch(const std::exception&){rejected=true;}
        Require(rejected,"corrupt visit record was silently discarded");
        Require(f.Provider.GameState.SavedVisits.size()==2,"failed read partially replaced current visit state");
    }
    Require(valid.Save(f.Path()),"cannot restore synthetic IFF");
    f.Provider.GameState.SavedVisits.erase(1);
    Require(TS1NeighbourhoodWriter::Save(&f.Provider,f.Path(),{},&error),"return-home writer failed");
    f.Provider.GameState.SavedVisits.clear();TS1NeighbourhoodWriter::LoadFamilySpells(&f.Provider,f.Path());
    Require(f.Provider.GameState.SavedVisits.size()==1&&f.Provider.GameState.SavedVisits.count(2),"returning family deleted another visit");
    std::cout<<"visit persistence contract: per-family records and corruption boundaries passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
