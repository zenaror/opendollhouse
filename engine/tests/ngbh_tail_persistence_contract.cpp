/* SPDX-License-Identifier: MPL-2.0 */
// Synthetic NGBH payloads; no original saves or game installation required.
#include "content/TS1NeighbourProvider.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourhoodWriter.h"
#include "files/formats/iff/IffRawFile.h"
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
using namespace FSO::Content::TS1;
using namespace FSO::Files::Formats::IFF;
static void Require(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
static void Put16(std::vector<uint8_t>& v,uint16_t x){v.push_back(uint8_t(x));v.push_back(uint8_t(x>>8));}
static void Put32(std::vector<uint8_t>& v,uint32_t x){for(int i=0;i<4;++i)v.push_back(uint8_t(x>>(8*i)));}
static void Set32(std::vector<uint8_t>& v,size_t at,uint32_t x){for(int i=0;i<4;++i)v.at(at+i)=uint8_t(x>>(8*i));}
static void Row(std::vector<uint8_t>& v,int16_t id,uint32_t marker,uint16_t count=3){
    Put32(v,marker);Put16(v,uint16_t(id));Put32(v,1);Put32(v,2);Put32(v,0x12345678);Put16(v,count);
}
static std::vector<uint8_t> Payload(uint32_t version,const std::vector<uint8_t>& tail,bool changed=false){
    std::vector<uint8_t> v;Put32(v,0xA1B2C3D4);Put32(v,version);v.insert(v.end(),{'H','B','G','N'});
    for(int i=0;i<16;++i)Put16(v,uint16_t(changed&&i==3?912:100+i));
    if(version>=0x40){Put32(v,changed?3:2);Row(v,9,0x77,changed?12:3);Row(v,2,1);if(changed)Row(v,4,1,7);}
    v.insert(v.end(),tail.begin(),tail.end());return v;
}
struct Fixture{
    std::filesystem::path Root;
    TS1NeighborhoodProvider Provider;
    static FSO::Content::Content* Manager(const std::filesystem::path& p){auto c=FSO::Content::Content::Get();c->SimsCompleteDir=p.string();return c;}
    Fixture():Root(std::filesystem::temp_directory_path()/("odh-ngbh-tail-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),Provider(Manager(Root)){
        Require(std::filesystem::create_directory(Root),"fixture directory collision");
        Provider.MainResource=std::make_unique<IffFile>();auto ngbh=new Chunks::NGBH();ngbh->ChunkID=1;ngbh->ChunkProcessed=true;
        Provider.MainResource->AddChunk(ngbh);Provider.Neighborhood=ngbh;
        for(int i=0;i<16;++i)ngbh->NeighborhoodData[i]=int16_t(100+i);
    }
    ~Fixture(){std::error_code ec;std::filesystem::remove_all(Root,ec);}
    std::string Path()const{return (Root/"Neighborhood.iff").string();}
    void Install(const std::vector<uint8_t>& bytes){IffRawFile f;f.Set("NGBH",1,bytes);f.Set("TEST",7,{3,1,4,1,5});Require(f.Save(Path()),"fixture save failed");}
    std::vector<uint8_t> FileBytes()const{std::ifstream f(Path(),std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
    std::vector<uint8_t> Read(){IffRawFile f;Require(f.Load(Path()),"fixture reload failed");Require(f.Find("TEST",7)&&f.Find("TEST",7)->Data==std::vector<uint8_t>({3,1,4,1,5}),"unrelated chunk changed");return f.Find("NGBH",1)->Data;}
    void Inventories(){Chunks::InventoryItem item;item.Type=2;item.GUID=0x12345678;item.Count=3;Provider.Neighborhood->InventoryByID={{9,{item}},{2,{item}}};}
    void Save(){std::string error;if(!TS1NeighbourhoodWriter::Save(&Provider,Path(),{},&error))throw std::runtime_error(error);}
    void Reopen(){Provider.MainResource=std::make_unique<IffFile>(Path());Provider.Neighborhood=Provider.MainResource->Get<Chunks::NGBH>(1);Require(Provider.Neighborhood!=nullptr,"native reopen omitted NGBH");}
    void Reject(){const auto before=FileBytes();std::string error;Require(!TS1NeighbourhoodWriter::Save(&Provider,Path(),{},&error),"malformed NGBH accepted");Require(error.find("NGBH not saved:")==0,"save rejection omitted reason");Require(FileBytes()==before,"failed save changed file bytes");}
};
int main(){try{
    unsigned cases=0;
    // Run the targeted regression first: canonical markers ensure the old
    // writer fails specifically because it drops the eleven opaque bytes.
    {Fixture f;auto bytes=Payload(0x49,{0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B});
        Set32(bytes,48,1);f.Install(bytes);f.Inventories();f.Save();
        Require(f.Read()==bytes,"opaque suffix dropped");++cases;}
    for(size_t length:{size_t(0),size_t(1),size_t(11),size_t(37)}){
        Fixture f;std::vector<uint8_t> tail;for(size_t i=0;i<length;++i)tail.push_back(uint8_t(0x81+i));
        f.Install(Payload(0x49,tail));f.Inventories();f.Save();Require(f.Read()==Payload(0x49,tail),"unchanged NGBH not lossless");
        f.Provider.Neighborhood->NeighborhoodData[3]=912;f.Provider.Neighborhood->InventoryByID.at(9)[0].Count=12;
        Chunks::InventoryItem item;item.Type=2;item.GUID=0x12345678;item.Count=7;f.Provider.Neighborhood->InventoryByID[4]={item};
        f.Save();Require(f.Read()==Payload(0x49,tail,true),"inventory change lost suffix/header/owner order/marker");
        f.Save();Require(f.Read()==Payload(0x49,tail,true),"second save changed opaque suffix");++cases;
    }
    for(uint32_t version:{0x3Eu,0x3Fu,0x40u}){
        Fixture f;f.Install(Payload(version,{}));if(version>=0x40)f.Inventories();
        f.Save();Require(f.Read()==Payload(version,{}),"legacy no-tail roundtrip changed layout");
        f.Provider.Neighborhood->NeighborhoodData[3]=912;f.Save();auto expected=Payload(version,{});expected[18]=0x90;expected[19]=3;
        Require(f.Read()==expected,"legacy neighbourhood update changed header/version/layout");++cases;
    }
    for(unsigned mutation=0;mutation<7;++mutation){
        Fixture f;auto bytes=Payload(0x49,{1,2,3,4,5,6,7,8,9,10,11});f.Inventories();
        if(mutation==0)bytes.resize(43);
        if(mutation==1)bytes.resize(44);
        if(mutation==2)Set32(bytes,44,0xFFFFFFFF);
        if(mutation==3)Set32(bytes,44,0x7FFFFFFF);
        if(mutation==4)Set32(bytes,54,0xFFFFFFFF);
        if(mutation==5)Set32(bytes,54,0x7FFFFFFF);
        if(mutation==6)bytes.resize(57);
        f.Install(bytes);f.Reject();++cases;
    }
    {Fixture f;auto bytes=Payload(0x49,{9,8,7});bytes[72]=9;bytes[73]=0;
        f.Install(bytes);f.Inventories();f.Provider.Neighborhood->InventoryByID.erase(2);
        f.Save();Require(f.Read()==bytes,"accepted duplicate owner occurrences changed order/markers");++cases;}
    // Fresh original templates use3E/3F with exactly44bytes. First item must
    // save with the minimal inventory version, then survive a native reopen.
    for(uint32_t version:{0x3Eu,0x3Fu,0x2Au}){
        Fixture f;f.Install(Payload(version,{}));Chunks::InventoryItem item;
        item.Type=2;item.GUID=0x12345678;item.Count=3;f.Provider.Neighborhood->InventoryByID[9]={item};
        auto expected=Payload(version,{});Set32(expected,4,0x40);Put32(expected,1);Row(expected,9,1);
        f.Save();Require(f.Read()==expected,"first legacy inventory did not minimally migrate");
        f.Reopen();Require(f.Provider.Neighborhood->Version==0x40&&f.Provider.Neighborhood->InventoryByID.at(9).at(0).Count==3,"first item lost on native reopen");
        f.Provider.Neighborhood->InventoryByID.at(9).at(0).Count=7;expected[66]=7;expected[67]=0;
        f.Save();Require(f.Read()==expected,"second legacy inventory save changed normalized format");
        f.Reopen();Require(f.Provider.Neighborhood->InventoryByID.at(9).at(0).Count==7,"second item count lost on reopen");++cases;
    }
    // Previously accepted native inventory bodies may retain a pre40 header.
    // Normalize only that version word, preserving records, markers and suffix.
    for(uint32_t version:{0x3Eu,0x3Fu,0x13u}){
        Fixture f;auto bytes=Payload(0x40,{0xE1,0xE2,0xE3});Set32(bytes,4,version);f.Install(bytes);
        f.Reopen();Require(f.Provider.Neighborhood->InventoryByID.at(9).at(0).Count==3,"old native inventory no longer readable");
        f.Save();Set32(bytes,4,0x40);Require(f.Read()==bytes,"old native inventory migration lost suffix/order/header");
        f.Reopen();f.Save();Require(f.Read()==bytes,"normalized old native inventory changed on second save");++cases;
    }
    for(unsigned mutation=0;mutation<3;++mutation){
        Fixture f;auto bytes=Payload(0x40,{1,2,3});Set32(bytes,4,0x3F);f.Inventories();
        if(mutation==0)bytes.resize(46);
        if(mutation==1)Set32(bytes,44,0xFFFFFFFF);
        if(mutation==2)Set32(bytes,54,0x7FFFFFFF);
        f.Install(bytes);f.Reject();++cases;
    }
    std::cout<<"NGBH tail persistence: "<<cases<<" cases passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
