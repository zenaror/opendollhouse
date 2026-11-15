// SPDX-License-Identifier: MPL-2.0
#include "files/formats/iff/chunks/OBJM.h"
#include "files/formats/iff/chunks/OBJMWriter.h"
#include "files/formats/iff/chunks/OBJT.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::Files::Formats::IFF::Chunks;
static void Require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
static bool Decode(OBJM& object,const std::vector<uint8_t>& bytes,const std::function<OBJMResource(uint16_t)>& resolve){
    try{st::Reader input(bytes.data(),bytes.size(),st::Endian::Little);object.Read(nullptr,input);return object.Prepare(resolve);}catch(const std::exception&){return false;}
}
static OBJTEntry Type(uint16_t id,OBJDType kind,uint32_t guid){
    st::Writer data(st::Endian::Little);data.WriteUInt32(guid);for(int i=0;i<4;++i)data.WriteUInt16(0);
    data.WriteUInt16(id);data.WriteUInt16(uint16_t(kind));for(char ch:std::string("fixture"))data.WriteByte(ch);data.WriteByte(0);
    auto bytes=data.TakeArray();st::Reader input(bytes);return OBJTEntry(input,2);
}
int main(){try{
    const std::vector<uint8_t> shipped={0,0,0,0,0x33,0,0,0,0x4d,0x6a,0x62,0x4f,1,0,0,0,0,0,0};
    const auto noResources=[](uint16_t)->OBJMResource{throw std::runtime_error("empty table requested a resource");};
    OBJM empty;Require(Decode(empty,shipped,noResources)&&empty.Decoded&&empty.ObjectData.empty(),"shipped empty table rejected");
    const auto minimal=WriteOBJMChunk(0,0x33,{},{});Require(minimal.size()==14,"unexpected minimal empty layout");
    Require(Decode(empty,minimal,noResources)&&empty.Version==0x33&&empty.ObjectData.empty(),"writer empty table cannot reopen");
    for(size_t cut=0;cut<14;++cut){OBJM bad;Require(!Decode(bad,std::vector<uint8_t>(shipped.begin(),shipped.begin()+cut),noResources)&&!bad.Decoded,"mandatory empty bytes truncated but accepted");}
    auto wrong=shipped;wrong[8]^=1;Require(!Decode(empty,wrong,noResources)&&!empty.Decoded,"bad magic inherited valid state");
    wrong=shipped;wrong[12]=2;Require(!Decode(empty,wrong,noResources)&&!empty.Decoded,"bad compression inherited valid state");
    wrong=minimal;wrong[13]=0x80;Require(!Decode(empty,wrong,noResources)&&!empty.Decoded,"truncated nonzero table treated as empty");
    Require(Decode(empty,shipped,noResources)&&empty.DecodeError.empty(),"valid reread did not recover from failed input");
    auto objectType=Type(1,OBJDType::Normal,0x11223344);auto personType=Type(2,OBJDType::Person,0x55667788);
    const auto resources=[&](uint16_t id){OBJMResource r;r.OBJT=id==1?&objectType:id==2?&personType:nullptr;return r;};
    OBJMRecordOut object;object.ObjectID=17;object.TypeID=1;object.HasLinkedSlot=false;
    object.Instance.OBJT=&objectType;object.Instance.X=168;object.Instance.Y=248;object.Instance.Level=1;
    object.Instance.ObjectData.resize(OBJMInstance::ObjectDataCount);object.Instance.ObjectData[11]=17;object.Instance.ObjectData[1]=3;
    object.Instance.Attributes={5,-6};object.Instance.StackFlags=512;
    OBJMRecordOut person;person.ObjectID=18;person.TypeID=2;person.HasLinkedSlot=false;
    person.Instance.OBJT=&personType;person.Instance.X=184;person.Instance.Y=248;person.Instance.Level=1;
    person.Instance.ObjectData.resize(OBJMInstance::ObjectDataCount);person.Instance.ObjectData[11]=18;
    person.Instance.HasPersonData=true;person.Instance.PersonData.PersonData.resize(0x50);person.Instance.PersonData.PersonData[35]=1234;
    person.Instance.PersonData.Body="fixture-body";person.Instance.PersonData.Head="fixture-head";
    const auto populated=WriteOBJMChunk(0,0x33,{object,person},{});OBJM restored;
    Require(Decode(restored,populated,resources)&&restored.Version==0x33&&restored.ObjectData.size()==2,"new version33 records cannot reopen");
    const auto& o=*restored.ObjectData.at(17).Instance;const auto& p=*restored.ObjectData.at(18).Instance;
    Require(o.X==168&&o.Y==248&&o.ObjectData[1]==3&&o.Attributes==object.Instance.Attributes,"new object state changed");
    Require(p.HasPersonData&&p.PersonData.PersonData.size()==0x50&&p.PersonData.PersonData[35]==1234&&p.PersonData.Body=="fixture-body"&&p.PersonData.Head=="fixture-head","new version33 avatar fields changed");
    OBJMRawChunk raw;std::string error;Require(raw.Parse(populated,error)&&raw.Records.size()==2,"writer record table is unreadable");
    auto incomplete=populated;incomplete.resize(12+raw.Records.front().Offset-4);OBJM rejected;
    Require(!Decode(rejected,incomplete,resources)&&!rejected.Decoded,"missing record offset accepted");
    auto invalidOffset=populated;const auto offset=12+raw.Records.front().Offset-4;for(int i=0;i<4;++i)invalidOffset[offset+i]=0xff;
    Require(!Decode(rejected,invalidOffset,resources)&&!rejected.Decoded,"invalid record offset accepted");
    std::puts("empty object snapshot: valid empty, mandatory bounds, reuse and version33 object/avatar roundtrip passed");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"empty object snapshot: %s\n",error.what());return 1;}}
