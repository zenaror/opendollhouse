// SPDX-License-Identifier: MPL-2.0
#include "files/formats/iff/chunks/SLOT.h"
#include "simantics/engine/TS1SlotMetadata.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace FSO::Files::Formats::IFF::Chunks;
using namespace FSO::SimAntics::Engine;
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void U16(std::vector<uint8_t>&b,uint16_t x){b.push_back(uint8_t(x));b.push_back(uint8_t(x>>8));}
void U32(std::vector<uint8_t>&b,uint32_t x){for(unsigned k=0;k<4;++k)b.push_back(uint8_t(x>>(8*k)));}
void F32(std::vector<uint8_t>&b,float f){uint32_t x;std::memcpy(&x,&f,4);U32(b,x);}
std::vector<uint8_t> Header(unsigned version,unsigned count){
    std::vector<uint8_t>b;U32(b,0);U32(b,version);U32(b,0x544f4c53);U32(b,count);return b;
}
void Row(std::vector<uint8_t>&b,unsigned version,uint16_t type,int height=0,float z=0){
    U16(b,type);F32(b,1.25f);F32(b,-2.5f);F32(b,z);
    //Deliberately distinguish routing Standing/Sitting/Ground from physical
    //i9/i10/Height. Incorrect field aliases must fail through the real reader.
    U32(b,101);U32(b,202);U32(b,303);U32(b,0);U32(b,uint32_t(-1));
    if(version>=5){U32(b,2);U32(b,4);U32(b,3);U32(b,404);U32(b,505);}
    if(version>=7)F32(b,.125f);
    if(version>=8)U32(b,uint32_t(height));
    if(version>=9)U32(b,uint32_t(-3));
    if(version>=10)U32(b,16);
}
SLOT Read(const std::vector<uint8_t>&bytes){SLOT slot;st::Reader in(bytes);slot.Read(nullptr,in);Need(in.pos()==bytes.size(),"SLOT version consumed wrong byte extent");return slot;}
void VersionsAndOrdinals(){
    for(unsigned version:{4u,5u,6u,7u,8u,9u,10u}){
        auto bytes=Header(version,3);Row(bytes,version,0,0);Row(bytes,version,3,0);Row(bytes,version,0,3);
        auto resource=Read(bytes);const auto& rows=resource.Slots.at(0);
        Need(rows.size()==2&&rows[0].SourceRowIndex==1&&rows[1].SourceRowIndex==3,"physical rows lost global file ordinal");
        Need(rows[0].Standing==101&&rows[0].Sitting==202&&rows[0].Ground==303,"routing weights changed");
        Need(rows[0].Height==5&&rows[0].RawHeight==0,"legacy routing height overwrote literal source height0");
        auto got=TS1BuildSlotMetadata(rows,false);
        Need(got.size()==3&&got[0].Height==0&&got[0].Capacity==50&&got[0].ResourceRow==-1,"source0 lost runtime-constructor defaults");
        Need(got[1].Capacity==(version>=5?404:0)&&got[1].Flags==(version>=5?505:0),"physical fields aliased Standing/Sitting or skipped version5 extras");
        Need(got[1].Height==1&&got[1].Offset.Z==0,"resource rawheight0 floor normalization differs");
        Need(got[2].ResourceRow==3&&got[2].Height==(version>=8?3:1)&&got[2].Offset.Z==(version>=8?4.f:0.f),"version8 raw height did not reach physical setter");
    }
}
void HeightAndPeople(){
    for(int h=0;h<=9;++h){SLOTItem r;r.Type=0;r.RawHeight=h;r.Offset=st::Vector3(3,-7,9);r.SourceRowIndex=11;
        auto m=TS1BuildSlotMetadata({r},false,5);float z=9;int normalized=h;
        switch(h){case 0:normalized=5;break;case 1:z=0;break;case 2:z=2.5f;break;case 3:case 8:z=4;break;case 4:{uint32_t bits=0x4081999a;std::memcpy(&z,&bits,4);break;}}
        Need(m[1].Height==normalized&&m[1].Offset.X==3&&m[1].Offset.Y==-7&&m[1].Offset.Z==z,"DVD46e014 height switch differs");
        Need(m.size()==5&&m[4].Capacity==50&&m[4].Height==0&&m[4].ResourceRow==-1,"saved growth did not use default runtime records");
    }
    auto person=TS1BuildSlotMetadata({},true,2);
    const int codes[6]={0,6,0,1,1,1};Need(person.size()==6,"saved prefix shrank constructor/person suffix");
    for(size_t i=0;i<person.size();++i)Need(person[i].Height==codes[i],"person append used resource height normalization");
    Need(person[2].Flags==2&&person[2].BoneName=="HEAD"&&person[2].Offset.Z==0,"person second physical extra lost HEAD metadata");
    Need(person[1].BoneName.empty()&&person[3].BoneName.empty(),"HEAD binding leaked to other slots");
}
void Rejections(){
    auto bytes=Header(5,2);Row(bytes,5,0);bool rejected=false;
    try{Read(bytes);}catch(const std::runtime_error&){rejected=true;}Need(rejected,"truncated version5 rows accepted");
    bytes=Header(10,0xffffffffu);rejected=false;
    try{Read(bytes);}catch(const std::runtime_error&){rejected=true;}Need(rejected,"unbounded resource ordinal accepted");
    rejected=false;try{TS1BuildSlotMetadata({},false,32768);}catch(const std::runtime_error&){rejected=true;}Need(rejected,"source slot extent overflow accepted");
    SLOTItem r;r.Type=0;r.Offset.Z=std::numeric_limits<float>::infinity();rejected=false;
    try{TS1BuildSlotMetadata({r},false);}catch(const std::runtime_error&){rejected=true;}Need(rejected,"non-finite physical metadata accepted");
}
}
int main(){try{VersionsAndOrdinals();HeightAndPeople();Rejections();std::cout<<"TS1 slot metadata contracts passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
