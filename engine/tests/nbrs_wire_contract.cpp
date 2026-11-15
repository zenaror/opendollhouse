/* SPDX-License-Identifier: MPL-2.0 */
// Synthetic original-DVD wire invariant, plus optional isolated real-file rewrite.
#include "content/TS1NeighbourhoodWriter.h"
#include "content/TS1NeighbourProvider.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/NBRS.h"
#include "core/io.h"
#include <filesystem>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using FSO::Content::TS1::TS1NeighborhoodProvider;
using FSO::Content::TS1::TS1NeighbourhoodWriter;
static void need(bool x,const char* why){if(!x)throw std::runtime_error(why);}
static void u16(std::vector<uint8_t>& d,uint16_t x){d.push_back(x);d.push_back(x>>8);}
static void u32(std::vector<uint8_t>& d,uint32_t x){for(int k=0;k<4;k++)d.push_back(x>>(8*k));}
static uint32_t l32(const std::vector<uint8_t>&d,size_t p){need(p+4<=d.size(),"bounded original-layout read");return uint32_t(d[p])|(uint32_t(d[p+1])<<8)|(uint32_t(d[p+2])<<16)|(uint32_t(d[p+3])<<24);}
static void record(std::vector<uint8_t>& d,int version,int id,bool person){
    u32(d,1);u32(d,version);if(version==10)u32(d,9);
    const std::string name=id==1?"Alpha":"Beta";d.insert(d.end(),name.begin(),name.end());d.push_back(0);if(!(name.size()&1))d.push_back(0xa3);
    u32(d,0);u32(d,person?5:0);
    if(person)for(int i=0;i<(version==4?80:256);i++)u16(d,i==150?2877:uint16_t(i+100));
    u16(d,id);u32(d,0x12340000+id);u32(d,0xffffffff);u32(d,1);u32(d,1);u32(d,id==1?2:1);u32(d,2);u32(d,17);u32(d,uint32_t(-13));
}
static std::vector<uint8_t> payload(int global,bool mixed){std::vector<uint8_t>d;u32(d,0);u32(d,global);d.insert(d.end(),{'S','R','B','N'});u32(d,4);record(d,4,1,true);record(d,mixed?10:4,2,true);u32(d,0);record(d,4,4,false);d.insert(d.end(),{0xd1,0xe2,0xf3});return d;}
struct Observed{int id;uint32_t guid;std::string name;std::vector<int16_t> person;std::vector<uint32_t> rel;};
static std::vector<Observed> originalLayout(const std::vector<uint8_t>&d,bool modern){
    // Original 00423e3b branches on the GLOBAL version before reading version words.
    const uint32_t global=l32(d,4);need((global>=0x49)==modern,"coherent global version");size_t p=16;std::vector<Observed>out;
    for(uint32_t slot=0;slot<l32(d,12);++slot){auto active=l32(d,p);p+=4;if(!active)continue;need(active==1,"active marker preserved");
        const uint32_t first=l32(d,p);p+=4;uint32_t format=first;if(global>=0x49){need(first==10,"modern outer requires version pair");format=l32(d,p);p+=4;need(format==9,"modern inner version9");}
        else need(first==4,"legacy record remains format4");
        Observed o;size_t begin=p;while(p<d.size()&&d[p])o.name.push_back(char(d[p++]));need(p<d.size(),"name terminates");++p;if(!((p-begin)&1)){}else ++p;
        need(l32(d,p)==0,"mystery word preserved");p+=4;const auto mode=l32(d,p);p+=4;if(mode){size_t words=format>=8?256:80;for(size_t i=0;i<words;++i){need(p+2<=d.size(),"person data bound");o.person.push_back(int16_t(uint16_t(d[p])|(uint16_t(d[p+1])<<8)));p+=2;}}
        need(p+2<=d.size(),"neighbor id bound");o.id=int16_t(uint16_t(d[p])|(uint16_t(d[p+1])<<8));p+=2;o.guid=l32(d,p);p+=4;need(l32(d,p)==0xffffffff,"relationship marker preserved");p+=4;const auto n=l32(d,p);p+=4;
        for(uint32_t i=0;i<n;++i){need(l32(d,p)==1,"relationship key count");p+=4;o.rel.push_back(l32(d,p));p+=4;auto count=l32(d,p);p+=4;for(uint32_t j=0;j<count;++j){o.rel.push_back(l32(d,p));p+=4;}}
        out.push_back(o);
    }need(p+3==d.size()&&d[p]==0xd1&&d[p+1]==0xe2&&d[p+2]==0xf3,"opaque tail and free slots preserved");return out;
}
static void save(const std::filesystem::path& path,bool promote,bool edit){
    IffRawFile raw;std::string error;need(raw.Load(path.string(),&error),error.c_str());auto chunk=raw.Find("NBRS",1);need(chunk,"NBRS fixture");NBRS registry;st::Reader input(chunk->Data,st::Endian::Little);registry.Read(nullptr,input);
    if(edit){auto n=registry.NeighbourByID.at(1);n->PersonData[0]=1234;n->Relationships.at(2)[0]=77;n->GUID=0xabcdef01;
        if(promote){n->Version=10;n->Unknown3=9;n->PersonData[80]=4321;}}
    TS1NeighborhoodProvider provider(nullptr,false);provider.MainResource=std::make_unique<IffFile>();provider.Neighbors=&registry;provider.UserPath=path.parent_path().string();
    need(TS1NeighbourhoodWriter::Save(&provider,path.string(),{},&error,nullptr),error.c_str());
}
int main(int argc,char**argv){try{
    if(argc==2){save(argv[1],false,false);std::cout<<"Production raw-preserving NBRS writer completed isolated file\n";return 0;}
    auto root=std::filesystem::temp_directory_path()/("odh-nbrs-wire-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(root);auto path=root/"Neighborhood.iff";
    for(int variant=0;variant<3;++variant){IffRawFile raw;raw.Set("NBRS",1,payload(0x3f,variant==0));std::string error;need(raw.Save(path.string(),&error),error.c_str());save(path,variant==1,true);IffRawFile out;need(out.Load(path.string(),&error),error.c_str());const auto bytes=out.Find("NBRS",1)->Data;bool modern=variant!=2;auto rows=originalLayout(bytes,modern);need(rows.size()==3,"all identities retained");need(rows[0].id==1&&rows[1].id==2&&rows[0].guid==0xabcdef01&&rows[1].guid==0x12340002,"current IDs/GUIDs survive normalization");need(rows[2].id==4&&rows[2].guid==0x12340004&&rows[2].person.empty(),"non-person prefix promoted without a person bank");need(rows[0].name=="Alpha"&&rows[1].name=="Beta","raw names survive");need(rows[0].person[0]==1234&&rows[0].rel==std::vector<uint32_t>({2,77,uint32_t(-13)}),"current person/relationship edits survive normalization");
        if(modern){need(rows[0].person.size()==256&&rows[0].person[79]==179,"old person prefix preserved");need(rows[0].person[80]==(variant==1?4321:0),"promoted fame retained; legacy suffix zero initialized");need(rows[1].person[150]==(variant==0?2877:0),"modern opaque person suffix retained");}
        NBRS parsed;st::Reader reader(bytes,st::Endian::Little);parsed.Read(nullptr,reader);need(parsed.NeighbourByID.at(1)->PersonData[0]==1234,"native reader consumes corrected wire");save(path,false,false);IffRawFile again;need(again.Load(path.string(),&error),error.c_str());need(again.Find("NBRS",1)->Data==bytes,"native production save idempotent");
    }std::filesystem::remove_all(root);std::cout<<"PASS mixed legacy/expanded roster, first in-memory promotion, legacy-only stability, original-layout fields and native save idempotence\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
