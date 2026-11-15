// SPDX-License-Identifier: MPL-2.0
// Real provider, NBRS, CTSS, dialog primitive and presentation codec; no commercial data.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMDialogHandler.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/model/VMDialogInfo.h"
#include "simantics/primitives/VMDialogPrivateStrings.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/chunks/CTSS.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "app/ui/OriginalDialogText.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
static void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static std::string Present(const Model::VMDialogInfo& info,Model::VMDialogInfo::TextField field){return simapp::original::DialogTextUTF8(info.TextParts[field]);}
static void Register(const std::filesystem::path& dir,int16_t nid,std::vector<std::pair<uint8_t,std::string>> names,bool utf8,NBRS& neighbors){
 const auto guid=uint32_t(0xd1a10000u+uint16_t(nid));OBJD obj;obj.GUID=guid;obj.CatalogStringsID=2000;obj.ObjectType=OBJDType::Person;
 st::Writer ob(st::Endian::Little);obj.Write(nullptr,ob);IffRawFile file;file.Set("OBJD",1,ob.ToArray(),"Synthetic personal identity");
 st::Writer text(st::Endian::Little);
 if(utf8){CTSS ctss;for(auto& n:names){STRItem item;item.LanguageCode=n.first;item.Value=n.second;ctss.LanguageSets[n.first-1].Strings.push_back(item);}ctss.Write(nullptr,text);}
 else {text.WriteInt16(-3);text.WriteUInt16(names.size());for(auto& n:names){text.WriteByte(n.first);text.WriteBytes(reinterpret_cast<const uint8_t*>(n.second.data()),n.second.size());text.WriteByte(0);text.WriteByte(0);}}
 file.Set("CTSS",2000,text.ToArray(),"Synthetic name");const auto path=(dir/"Characters"/(std::to_string(nid)+".iff")).string();std::string error;Require(file.Save(path,&error),error.c_str());
 auto c=FSO::Content::Content::Get();Require(c->WorldObjects->AddStandaloneIff(path,true)==1,"synthetic character did not register");
 auto n=std::make_unique<Neighbour>();n->Unknown1=1;n->NeighbourID=nid;n->GUID=guid;neighbors.NeighbourByID[nid]=n.get();neighbors.Entries.push_back(std::move(n));
}
static void Set(STR& table,int language,const std::vector<std::string>& values){table.LanguageSets[language-1].Strings.clear();for(auto& v:values){STRItem item;item.LanguageCode=uint8_t(language);item.Value=v;table.LanguageSets[language-1].Strings.push_back(item);}}
int main(int argc,char** argv){try{
 Require(argc==2,"pass a private temporary fixture directory");std::filesystem::create_directories(std::filesystem::path(argv[1])/"Characters");
 const auto previous=STR::DefaultLangCode;auto c=FSO::Content::Content::Get();c->Neighborhood=std::make_unique<FSO::Content::TS1::TS1NeighborhoodProvider>(c);NBRS neighbors;c->Neighborhood->Neighbors=&neighbors;
 Register(argv[1],7,{{14,std::string("Efig\xeania")}},false,neighbors);
 Register(argv[1],8,{{1,u8"Aiko 東京 Ø"}},true,neighbors);
 Register(argv[1],9,{{14,"PT only"},{15,"JA only"}},false,neighbors);
 Register(argv[1],10,{{1,"US fallback"}},false,neighbors);
 Register(argv[1],11,{{15,std::string("\x82\xa0")}},false,neighbors);
 Register(argv[1],12,{{14,std::string("\xc3\xa9")}},false,neighbors);
 VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;VMThread thread(&context,nullptr,8);FSO::Content::GameObject owner;owner.GUID=1;VMRoutine routine;routine.ID=100;VMStackFrame frame;frame.Thread=&thread;frame.CodeOwner=&owner;frame.Routine=&routine;frame.Locals.resize(4);frame.StackObjectRawID=7;frame.Locals[0]=8;
 std::vector<Model::VMDialogInfo> raised;vm.OnDialog=[&](Model::VMDialogInfo& info){raised.push_back(info);};
 for(auto lang:{STRLangCode::EnglishUS,STRLangCode::Portuguese}){
  STR::DefaultLangCode=lang;STR source;const bool pt=lang==STRLangCode::Portuguese;
  const auto prefix=pt?std::string("A\xe7\xe3o: "):std::string("Caf\xe9: ");
  Set(source,int(lang),{prefix+"$Neighbor / $NeighborLocal:0!",pt?std::string("Di\xe1logo"):std::string("Dialog"),pt?std::string("Sim, $Neighbor"):std::string("Yes, $Neighbor"),pt?std::string("N\xe3o"):std::string("No"),"Cancel"});
  VMDialogOperand operand;operand.MessageStringID=1;operand.TitleStringID=2;operand.YesStringID=3;operand.NoStringID=4;operand.CancelStringID=5;operand.Type=VMDialogType::YesNoCancel;
  const auto exit=VMDialogPrivateStrings::ExecuteGeneric(&frame,&operand,&source);Require(exit==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,"real dialog primitive did not block");Require(bool(thread.BlockingState),"dialog continuation missing");
  auto info=raised.back();simapp::PendingDialog presented;simapp::original::ApplyDialogTextUTF8(info,presented);Require(presented.TextUTF8,"LotSession boundary lost UTF8 marker");Require(info.HasTextParts,"dialog lost text provenance");
  const auto expected=(pt?u8"Ação: ":u8"Café: ")+std::string(u8"Efigênia / Aiko 東京 Ø!");
  if(Present(info,Model::VMDialogInfo::MessageText)!=expected)std::cerr<<"actual=["<<Present(info,Model::VMDialogInfo::MessageText)<<"] expected=["<<expected<<"] parts="<<info.TextParts[0].Parts.size()<<"\n";
  Require(presented.Message==expected,"LotSession boundary concatenated raw encodings");
  Require(Present(info,Model::VMDialogInfo::MessageText)==expected,"mixed dialog was decoded with the wrong code page");
  Require(Present(info,Model::VMDialogInfo::YesText)==std::string(pt?u8"Sim, Efigênia":u8"Yes, Efigênia"),"authored button name was not decoded");
  Require(Present(info,Model::VMDialogInfo::NoText)==std::string(pt?u8"Não":"No"),"button resource lost encoding");
  Require(presented.Title==std::string(pt?u8"Diálogo":"Dialog")&&presented.Yes==std::string(pt?u8"Sim, Efigênia":u8"Yes, Efigênia"),"boundary title/button did not preserve personal text");
  Require(Present(info,Model::VMDialogInfo::TitleText)==std::string(pt?u8"Diálogo":"Dialog"),"title resource lost encoding");
  Require(simapp::original::DialogStringUTF8(Present(info,Model::VMDialogInfo::MessageText),true)==expected,"marked presentation text was decoded a second time");
  Require(simapp::original::DialogStringUTF8(prefix,false)==std::string(pt?u8"Ação: ":u8"Café: "),"unmarked legacy notice changed contract");
  Require(VMDialogHandler::ParseDialogString(&frame,source.GetString(0),&source)==info.Message,"legacy parser contract changed");
  auto wait=std::static_pointer_cast<VMDialogResult>(thread.BlockingState);wait->Responded=true;wait->ResponseCode=1;Require(VMDialogPrivateStrings::ExecuteGeneric(&frame,&operand,&source)==VMPrimitiveExitCode::GOTO_FALSE&&!thread.BlockingState,"text presentation changed dialog response");
  std::cout<<(pt?"PT":"EN")<<" real dialog: "<<expected<<'\n';
 }
 STR::DefaultLangCode=STRLangCode::EnglishUS;STR source;Set(source,1,{"$Neighbor"});
 const auto name=[&](int16_t id){frame.StackObjectRawID=id;return simapp::original::DialogTextUTF8(VMDialogHandler::ParseDialogText(&frame,"$Neighbor",&source));};
 Require(name(12)==u8"Ã©","legacy bytes that happen to be valid UTF8 were guessed as UTF8");
 Require(name(9)=="Unknown","ambiguous personal identity guessed a language");Require(name(99)=="Unknown","missing neighbor manufactured a name");Require(name(10)=="US fallback","US fallback changed");Require(name(11)==u8"あ","singleton CP932 name decoded as active EN");
 neighbors.NeighbourByID[7]->Unknown1=0;Require(name(7)=="Unknown","invalid NBRS entry manufactured a name");neighbors.NeighbourByID[7]->Unknown1=1;
 STR nested;Set(nested,15,{std::string("\x82\xa0 $Neighbor")});frame.StackObjectRawID=8;STR::DefaultLangCode=STRLangCode::Japanese;
 Require(simapp::original::DialogTextUTF8(VMDialogHandler::ParseDialogText(&frame,nested.GetString(0),&nested))==u8"あ Aiko 東京 Ø","CP932 literal and UTF8 name were mixed");
 // Literal US fallback is decoded as US even while the selected UI is CP932.
 STR fallback;Set(fallback,1,{std::string("Caf\xe9: $Neighbor")});
 Require(simapp::original::DialogTextUTF8(VMDialogHandler::ParseDialogText(&frame,fallback.GetString(0),&fallback))==u8"Café: Aiko 東京 Ø","fallback resource was decoded as current Japanese UI");
 // Recursive dynamic strings preserve the nested table's own source encoding.
 IffFile globalFile;auto* inner=new STR();inner->ChunkID=77;inner->ChunkProcessed=true;Set(*inner,1,{std::string("Caf\xe9: $Neighbor")});globalFile.AddChunk(inner);
 FSO::Content::GameGlobalResource globalResource(&globalFile,nullptr);FSO::Content::GameGlobal global;global.Resource=&globalResource;context.Globals=&global;
 frame.Locals[0]=0;frame.Locals[1]=77;
 const auto dynamic=std::string("\x82\xa0 $DynamicStringLocal:0:1!");
 Require(simapp::original::DialogTextUTF8(VMDialogHandler::ParseDialogText(&frame,dynamic,&nested))==u8"あ Café: Aiko 東京 Ø!","nested fragment lost its source encoding");
 // An untyped dynamic group name keeps the complete dialog legacy. It must
 // neither be guessed as UTF8 nor be partly converted around known names.
 STR::DefaultLangCode=STRLangCode::EnglishUS;
 VMAvatar avatar(c->WorldObjects->Get(uint32_t(0xd1a10008),true));FSO::SimAntics::Entities::VMMultitileGroup group;avatar.MultitileGroup=&group;group.Name=u8"Lia bebê 雪";frame.StackObject=&avatar;frame.Caller=&avatar;frame.Locals[0]=7;
 STR opaque;Set(opaque,1,{"$NeighborLocal:0 / $Object","$NeighborLocal:0"});VMDialogOperand opaqueOperand;opaqueOperand.MessageStringID=1;opaqueOperand.TitleStringID=2;
 VMDialogHandler::ShowDialog(&frame,&opaqueOperand,&opaque);auto legacyInfo=raised.back();Require(!legacyInfo.HasTextParts,"opaque name enabled partial UTF8 conversion");
 simapp::PendingDialog legacy;legacy.Message=legacyInfo.Message;legacy.Title=legacyInfo.Title;simapp::original::ApplyDialogTextUTF8(legacyInfo,legacy);
 Require(!legacy.TextUTF8&&legacy.Message==std::string("Efig\xeania / ")+u8"Lia bebê 雪"&&legacy.Title==std::string("Efig\xeania"),"opaque dialog did not preserve its integral legacy bytes");
 // Exact equality with this avatar's resolved CTSS is a known authority.
 group.Name=u8"Aiko 東京 Ø";VMDialogHandler::ShowDialog(&frame,&opaqueOperand,&opaque);auto knownInfo=raised.back();Require(knownInfo.HasTextParts,"canonical personal name lost proven source");
 simapp::PendingDialog known;simapp::original::ApplyDialogTextUTF8(knownInfo,known);Require(known.TextUTF8&&known.Message==u8"Efigênia / Aiko 東京 Ø","canonical object name lost personal encoding");
 frame.StackObject=nullptr;frame.Caller=nullptr;
 Model::VMDialogText paragraphs;paragraphs.Append("a\r",1,false);paragraphs.Append("\nb",14,false);paragraphs.ExpandParagraphs();Require(paragraphs.LegacyBytes()=="a\r\n\r\nb","paragraph expansion crossed a fragment incorrectly");
 STR::DefaultLangCode=previous;c->Neighborhood->Neighbors=nullptr;
 std::cout<<"PASS singleton/fallback/ambiguous/missing/invalid identity, CP932+UTF8, legacy bytes and real blocking dialog continuation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
