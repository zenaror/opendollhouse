// SPDX-License-Identifier: MPL-2.0
// Real parser/provider consumers, with synthetic Work.iff strings only.
#include "content/ContentManager.h"
#include "content/TS1JobProvider.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/chunks/CARR.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/engine/VMDialogHandler.h"
#include "simantics/engine/VMThread.h"
#include "simantics/entities/VMAvatar.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
namespace fs=std::filesystem;
static void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
static std::vector<uint8_t> strings(int id){
    st::Writer w(st::Endian::Little);w.WriteInt16(-3);w.WriteUInt16(66);
    for(int lang:{1,14})for(int index=0;index<33;++index){w.WriteByte(lang);
        w.WriteNullTerminatedString("L"+std::to_string(lang)+"T"+std::to_string(id)+"I"+std::to_string(index));w.WriteByte(0);}
    return w.ToArray();
}
int main(){
 const auto root=fs::temp_directory_path()/("odh-engine-career-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 auto content=FSO::Content::Content::Get();auto savedJobs=std::move(content->Jobs);
 const auto savedPath=content->SimsCompleteDir;const auto savedLanguage=STR::DefaultLangCode;
 try{
    require(!fs::exists(root),"fixture already exists");fs::create_directories(root/"GameData");
    IffRawFile work;work.Header.assign(64,0);const std::string header="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    std::copy(header.begin(),header.end(),work.Header.begin());work.HadRsmp=true;work.RsmpIndex=0;
    work.Set("STR#",9,strings(9));work.Set("STR#",1009,strings(1009));
    const auto bytes=work.Write();std::ofstream file(root/"GameData/Work.iff",std::ios::binary);file.write((const char*)bytes.data(),bytes.size());file.close();
    content->SimsCompleteDir=root.string();content->Jobs=std::make_unique<FSO::Content::TS1::TS1JobProvider>(content);content->Jobs->Init();
    CARR career;career.Name="Wrong English career";std::vector<uint8_t> encoded(4096);st::Reader input(encoded);IffFieldEncode fields(&input);
    for(int i=0;i<10;++i)career.JobLevels.emplace_back(fields);
    career.JobLevels[2].JobName="Wrong English title";content->Jobs->Jobs[9]=&career;
    VM::UseWorld(false);VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;
    IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"career-dialog",true);OBJD definition;FSO::Content::GameObject object;object.OBJ=&definition;object.Resource=&resource;
    VMAvatar actor(&object);actor.Thread=std::make_unique<VMThread>(&context,&actor,16);
    actor.SetPersonData(Model::VMPersonDataVariable::JobType,9);actor.SetPersonData(Model::VMPersonDataVariable::JobPromotionLevel,1);
    actor.SetPersonData(Model::VMPersonDataVariable::Gender,0);
    VMStackFrame frame;frame.Caller=frame.Callee=&actor;frame.Thread=actor.Thread.get();frame.CodeOwner=&object;frame.Locals={9,2,9,15};
    auto parse=[&](const std::string& s){return VMDialogHandler::ParseDialogText(&frame,s,nullptr);};
    STR::DefaultLangCode=STRLangCode::Portuguese;
    const auto tick=context.Clock->Ticks;const auto seed=context.RandomSeed;
    require(parse("$JobOffer:0:1").LegacyBytes()=="L14T9I1","actual offer must use localized career-wide STR, not CARR English");
    require(parse("$Job:0:1 | $JobDesc:0:1").LegacyBytes()=="L14T1009I7 | L14T9I8","actual title/description use distinct source banks and stride two");
    require(parse("$Job:32000").LegacyBytes()=="L14T1009I5","single selector binds actual caller job and level");
    require(parse("$Job:0:1:123").LegacyBytes()=="L14T1009I7:123","third selector remains literal suffix");
    require(parse("$TimeLocal:2:65535 - $TimeLocal:3").LegacyBytes()==" 9:00 - 15:00","TS1 locale formatting and ignored minute selector");
    actor.SetPersonData(Model::VMPersonDataVariable::Gender,1);
    require(parse("$Job:0:1").LegacyBytes()=="L14T1009I25"&&parse("$JobOffer:0:1").LegacyBytes()=="L14T9I2","caller alternate title and offer use source rows");
    content->Jobs->JobStrings(9)->SetString(2,"",STRLangCode::Portuguese);
    require(parse("$JobOffer:0:1").LegacyBytes()=="L14T9I1","empty alternate offer falls back to primary row");
    STR::DefaultLangCode=STRLangCode::Japanese;
    const auto fallback=parse("$JobOffer:0:1");require(fallback.LegacyBytes()=="L1T9I2"&&!fallback.LegacyOnly&&fallback.Parts.front().Language==1,"absent locale preserves English provenance");
    auto table=content->Jobs->JobStrings(9);table->InitLanguageSet(STRLangCode::Japanese);table->SetString(2,u8"求人",STRLangCode::Japanese);table->SourceFormat=-4;
    const auto unicode=parse("$JobOffer:0:1");require(unicode.LegacyBytes()==u8"求人"&&!unicode.LegacyOnly&&unicode.Parts.front().Language==15&&unicode.Parts.front().UTF8,"UTF8 job fragment retains resource encoding");
    require(context.Clock->Ticks==tick&&context.RandomSeed==seed,"formatting changes simulation");
    vm.TS1=false;actor.SetPersonData(Model::VMPersonDataVariable::Gender,0);
    require(parse("$JobOffer:0:1").LegacyBytes()=="Wrong English career career track for a Wrong English title.","non-TS1 reference behavior changed");
    content->Jobs=std::move(savedJobs);content->SimsCompleteDir=savedPath;STR::DefaultLangCode=savedLanguage;fs::remove_all(root);
    std::cout<<"PASS actual career dialog source family\n";return 0;
 }catch(const std::exception& e){content->Jobs=std::move(savedJobs);content->SimsCompleteDir=savedPath;STR::DefaultLangCode=savedLanguage;if(fs::exists(root))fs::remove_all(root);std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
