/* SPDX-License-Identifier: MPL-2.0 */
// Synthetic contract only. No installed resources, real saves or live NPC spawn.
#include "content/TS1NeighbourReconcile.h"
#include "content/TS1NeighbourProvider.h"
#include "content/TS1NeighbourhoodWriter.h"
#include "content/TS1CharacterFiles.h"
#include "content/ContentManager.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "core/io.h"
#include "files/formats/iff/chunks/BHAV.h"
#include "files/formats/iff/chunks/STR.h"
#include "simantics/primitives/VMGenericTSOCall.h"
#include "files/formats/iff/chunks/FAMI.h"
#include "files/formats/iff/chunks/NGBH.h"
#include "files/formats/iff/chunks/TTAT.h"
#include "simantics/primitives/VMExpression.h"
#include "simantics/primitives/VMTS1InventoryOperations.h"
#include "simantics/primitives/VMTS1MakeNewCharacter.h"
#include "simantics/primitives/VMTransferFunds.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include <map>
#include <filesystem>
#include <iostream>
#include <chrono>
using namespace FSO::Content::TS1;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
namespace fs=std::filesystem;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void u16(std::vector<uint8_t>& v,uint16_t x){v.push_back(x);v.push_back(x>>8);}
static void u32(std::vector<uint8_t>& v,uint32_t x){for(int i=0;i<4;++i)v.push_back(x>>(8*i));}
using namespace FSO::SimAntics::Primitives;
using Scope=FSO::SimAntics::Engine::Scopes::VMVariableScope;
static std::map<std::string,std::vector<uint8_t>> filesIn(const fs::path& root){
    std::map<std::string,std::vector<uint8_t>> result;
    for(const auto& f:fs::recursive_directory_iterator(root))if(f.is_regular_file()){
        std::vector<uint8_t> data;check(RawIff::ReadFile(f.path().string(),data),"snapshot read failed");
        result[f.path().lexically_relative(root).string()]=std::move(data);
    }return result;
}
static void budgetCodecContract(){
    // Explicit authored wire examples, independent of the native Write body.
    const std::array<std::array<uint8_t,8>,3> cases={{{{0,0,1,0,2,0,0,0}},
        {{3,25,0xef,0xbe,7,0,72,24}},{{1,6,0x34,0x12,0,0,3,10}}}};
    for(const auto& expected:cases){
        VMTransferFundsOperand operand;
        operand.OldAmountOwner=VMTransferFundsOldOwner(expected[0]);operand.AmountOwner=Scope(expected[1]);
        operand.AmountData=uint16_t(expected[2])|(uint16_t(expected[3])<<8);
        operand.Flags=VMTransferFundsFlags(expected[4]);operand.ExpenseType=VMTransferFundsExpenseType(expected[6]);
        operand.TransferType=VMTransferFundsType(expected[7]);
        std::array<uint8_t,8> encoded;encoded.fill(0xa5);operand.Write(encoded.data());
        check(encoded==expected,"budget codec changed an explicit authored eight-byte payload");
        VMTransferFundsOperand decoded;decoded.Read(encoded.data());
        check(decoded.OldAmountOwner==operand.OldAmountOwner&&decoded.AmountOwner==operand.AmountOwner
            &&decoded.AmountData==operand.AmountData&&decoded.Flags==operand.Flags
            &&decoded.ExpenseType==operand.ExpenseType&&decoded.TransferType==operand.TransferType,
            "budget codec Write/Read lost a nontrivial field");
        std::array<uint8_t,8> rewritten;rewritten.fill(0x5a);decoded.Write(rewritten.data());
        check(rewritten==expected,"budget codec Read/Write changed authored bytes");
    }
}
static RawIffChunk authoredPerson(uint32_t guid,bool failInit){
    OBJD object;object.GUID=guid;object.ObjectType=OBJDType::Person;object.SubIndex=-1;
    object.UsesFnTable=0;object.BHAV_Init=4096;object.NumAttributes=1;object.StackSize=32;object.BodyStringID=200;
    st::Writer encoded(st::Endian::Little);object.Write(nullptr,encoded);
    RawIffChunk chunk;chunk.Type="OBJD";chunk.ID=1;chunk.Data=encoded.ToArray();return chunk;
}
static RawIffChunk bodyStrings(){
    st::Writer encoded(st::Endian::Little);encoded.WriteInt16(-3);encoded.WriteUInt16(14);
    for(int i=0;i<14;++i){encoded.WriteByte(1);encoded.WriteNullTerminatedString(i==0?"dog":i==13?"1":"");encoded.WriteByte(0);}
    RawIffChunk chunk;chunk.Type="STR#";chunk.ID=200;chunk.Data=encoded.ToArray();return chunk;
}
static RawIffChunk authoredInit(bool failInit){
    BHAV tree;tree.ChunkID=4096;tree.Locals=3;
    auto add=[&](uint16_t opcode,FSO::SimAntics::Engine::VMPrimitiveOperand& operand){
        BHAVInstruction instruction;instruction.Opcode=opcode;operand.Write(instruction.Operand.data());
        instruction.TruePointer=instruction.FalsePointer=uint8_t(tree.Instructions.size()+1);
        tree.Instructions.push_back(instruction);
    };
    auto assign=[&](int scope,int index,int rhsScope,int rhs){VMExpressionOperand operand;
        operand.Operator=VMExpressionOperator::Assign;operand.LhsOwner=Scope(scope);operand.LhsData=int16_t(index);
        operand.RhsOwner=Scope(rhsScope);operand.RhsData=int16_t(rhs);add(2,operand);};
    assign(18,31,7,1);assign(18,61,7,1);assign(10,0,7,1);
    assign(32,0,7,444); // selected neighbour's person data through singleton
    assign(34,0,7,222); // NGBH through singleton
    assign(36,0,7,333); // TATT through singleton
    assign(6,9,7,1);assign(6,13,7,500);assign(6,14,7,0);
    VMTransferFundsOperand budget;budget.OldAmountOwner=VMTransferFundsOldOwner::Literal;
    budget.AmountData=1;budget.Flags=VMTransferFundsFlags::Subtract;add(25,budget);
    // Assert the materialized payload before executing Init. A serializer error
    // must not be mistaken for an isolation failure or change the intended credit.
    const std::array<uint8_t,8> expectedBudget={{0,0,1,0,2,0,0,0}};
    const auto& budgetBytes=tree.Instructions.back().Operand;
    check(budgetBytes==expectedBudget,"budget operand did not encode literal1/credit flags in the authored layout");
    VMTransferFundsOperand decodedBudget;decodedBudget.Read(budgetBytes.data());
    check(decodedBudget.OldAmountOwner==budget.OldAmountOwner&&decodedBudget.AmountOwner==budget.AmountOwner
        &&decodedBudget.AmountData==1&&decodedBudget.Flags==VMTransferFundsFlags::Subtract
        &&decodedBudget.ExpenseType==budget.ExpenseType&&decodedBudget.TransferType==budget.TransferType,
        "budget operand did not round-trip before authored Init");
    VMTS1InventoryOperationsOperand inventory;inventory.Mode=VMTS1InventoryMode::AddToken;
    inventory.TokenType=7;inventory.Flags=1;inventory.GUID=0x1234abcd;add(51,inventory);
    assign(18,80,34,0);assign(18,81,36,0);assign(18,82,32,0);assign(18,83,6,13);
    inventory.Mode=VMTS1InventoryMode::FindToken;add(51,inventory);assign(18,84,8,0);
    // PetToAdult succeeds but its SaveBodyStrings must not mutate the global
    // cached STR# or its resource file during detached template preparation.
    assign(18,65,7,8);assign(8,0,3,11);
    VMGenericTSOCallOperand grow;grow.Call=FSO::SimAntics::Model::VMGenericTSOCallMode(40);add(1,grow);
    tree.Instructions.back().FalsePointer=255;assign(18,87,8,1);
    // Clone path avoids outfit/catalogue prerequisites and reaches the real
    // WriteCharacter service; a materialized TemplatePerson exists in the test.
    assign(25,0,7,-1);VMTS1MakeNewCharacterOperand clone;clone.AgeLocal=0;clone.ColorLocal=1;clone.GenderLocal=2;
    const auto cloneIndex=tree.Instructions.size();add(19,clone);
    tree.Instructions[cloneIndex].TruePointer=255; // unexpected write success fails Init
    assign(18,85,7,123); // reached the primitive's refusal branch
    if(failInit)assign(33,0,7,1); // unsupported write: VMThread reports suppressed exception
    assign(18,86,7,321);tree.Instructions.back().TruePointer=254;tree.Instructions.back().FalsePointer=255;
    st::Writer encoded(st::Endian::Little);tree.Write(nullptr,encoded);
    RawIffChunk chunk;chunk.Type="BHAV";chunk.ID=4096;chunk.Label="authored side-effect init";chunk.Data=encoded.ToArray();return chunk;
}
static std::vector<uint8_t> registryBytes(){
    std::vector<uint8_t> v;u32(v,0);u32(v,0x49);v.insert(v.end(),{'S','R','B','N'});u32(v,2);
    u32(v,1);u32(v,10);u32(v,9);v.insert(v.end(),{'o','l','d',0});u32(v,123);u32(v,0);
    u16(v,1);u32(v,0x100);u32(v,0xffffffff);u32(v,1);
    u32(v,1);u32(v,99);u32(v,1);u32(v,0x0001ffff); // deliberately outside int16 range
    u32(v,0);return v;
}
int main(){fs::path dir;try{
    budgetCodecContract();
    NBRS n;auto old=std::make_unique<Neighbour>();old->NeighbourID=1;old->GUID=0x100;
    old->Name="old";old->PersonMode=5;old->PersonData.assign(88,-12);old->Relationships[9]={4,5};
    auto before=old.get();n.NeighbourByID[1]=before;n.DefaultNeighbourByGUID[0x100]=1;n.Entries.push_back(std::move(old));
    auto prepare=[](uint32_t id,std::vector<int16_t>& out,std::string&){out.assign(88,int16_t(id));return true;};
    std::vector<NeighbourTemplate> candidates={{0x300,"loose",true},{0x200,"archive",true},{0x300,"duplicate",true},{0x999,"foreign",false},{0x100,"inactive",true}};
    size_t added=0;std::string error;
    check(ReconcileMissingNeighbors(n,candidates,prepare,added,error)&&added==2,"reconciliation failed");
    check(n.NeighbourByID[2]->GUID==0x200&&n.NeighbourByID[3]->GUID==0x300,"allocation order changed");
    check(n.NeighbourByID[1]==before&&before->PersonData[0]==-12&&before->Relationships[9][1]==5,"existing fields changed");
    check(n.NeighbourByID[2]->PersonMode==9&&n.NeighbourByID[2]->PersonData.size()==88,"template fields missing");
    check(ReconcileMissingNeighbors(n,candidates,prepare,added,error)&&added==0,"not idempotent");
    NBRS failed;int calls=0;auto fail=[&](uint32_t,std::vector<int16_t>& d,std::string&){d.assign(88,1);return ++calls<2;};
    check(!ReconcileMissingNeighbors(failed,candidates,fail,added,error)&&failed.Entries.empty()&&failed.NeighbourByID.empty(),"partial publication on preparation failure");
    auto tombstone=std::make_unique<Neighbour>();tombstone->Unknown1=2;failed.Entries.push_back(std::move(tombstone));
    check(!ReconcileMissingNeighbors(failed,candidates,prepare,added,error),"unknown occupied marker reused");

    auto content=FSO::Content::Content::Get();auto world=content->WorldObjects.get();
    // A character introduced after the preload scan is removed by its actual
    // directory provenance; an unrelated loose download remains eligible.
    FSO::Content::GameObjectReference resident;resident.ID=0x400;resident.Person=true;resident.FileName="/selected/Characters/new-cas.iff";
    world->Entries[0x400]=resident;auto download=resident;download.ID=0x500;download.FileName="/install/Downloads/person.iff";world->Entries[0x500]=download;
    world->RemoveObjectsFromFiles({resident.FileName});
    check(!world->Entries.count(0x400)&&world->PersonGUIDs.count(0x500),"scope cleanup discarded legitimate loose or retained foreign CAS");

    dir=fs::temp_directory_path()/("odh-roster-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir/"install");fs::create_directories(dir/"save/UserData/Characters");
    content->SimsCompleteDir=(dir/"install").string();
    auto provider=std::make_unique<TS1NeighborhoodProvider>(content);
    provider->UserDataRoot=(dir/"save").string();provider->UserPath=(dir/"save/UserData").string();
    std::string path=provider->UserPath+"/Neighborhood.iff";
    const auto source=registryBytes();RawIffChunk raw;raw.Type="NBRS";raw.ID=1;raw.Data=source;
    RawIffChunk opaque;opaque.Type="UNKN";opaque.ID=7;opaque.Data={9,8,7,6};
    check(RawIff::WriteFileAtomic(path,RawIff::Build({raw,opaque})),"fixture write failed");
    provider->MainResource=std::make_unique<IffFile>(path);provider->Neighbors=provider->MainResource->Get<NBRS>(1);
    check(ReconcileMissingNeighbors(*provider->Neighbors,{{0x200,"person",true}},prepare,added,error)&&added==1,"fresh roster reconcile failed");
    check(TS1NeighbourhoodWriter::SaveRoster(provider.get(),path,&error),error.c_str());
    IffRawFile stored;check(stored.Load(path,&error),"stored roster unreadable");
    const auto& bytes=stored.Find("NBRS",1)->Data;
    check(bytes.size()>source.size()&&std::equal(source.begin()+16,source.end()-4,bytes.begin()+16),"existing raw relationship/padding changed");
    check(stored.Find("UNKN",7)->Data==opaque.Data,"unrelated payload changed");
    auto first=stored.Write();provider->MainResource=std::make_unique<IffFile>(path);provider->Neighbors=provider->MainResource->Get<NBRS>(1);
    check(ReconcileMissingNeighbors(*provider->Neighbors,{{0x200,"person",true}},prepare,added,error)&&added==0,"fresh idempotence failed");
    check(TS1NeighbourhoodWriter::SaveRoster(provider.get(),path,&error),"second publication failed");
    check(stored.Load(path,&error)&&stored.Write()==first,"idempotent publication changed bytes");
    // Exercise the actual provider, including loose templates and an orphan
    // current Character outside NBRS, not just boolean candidate labels.
    auto personFile=[&](const std::string& file,uint32_t guid){
        fs::create_directories(fs::path(file).parent_path());
        OBJD object;object.GUID=guid;object.ObjectType=OBJDType::Person;object.SubIndex=-1;
        st::Writer encoded(st::Endian::Little);object.Write(nullptr,encoded);
        RawIffChunk chunk;chunk.Type="OBJD";chunk.ID=1;chunk.Data=encoded.ToArray();
        check(RawIff::WriteFileAtomic(file,RawIff::Build({chunk})),"person fixture write failed");
    };
    const auto downloadPath=(dir/"install/Downloads/person.iff").string();
    personFile(downloadPath,0x700);check(world->AddStandaloneIff(downloadPath,true)>0,"download registration failed");
    // Decode the global winner before a scoped character with identical GUID.
    auto globalObject=world->Get(uint32_t(0x700),true);check(globalObject!=nullptr,"global decode failed");
    const auto charPath=provider->UserPath+"/Characters/current.iff";
    personFile(charPath,0x700);personFile(provider->UserPath+"/Characters/orphan.iff",0x701);
    personFile((dir/"install/TemplateNPCs/template.iff").string(),0x702);
    check(provider->LoadCharacters()>=2,"selected character enumeration missed loose files");
    check(world->Entries[0x700].FileName==charPath&&world->Get(uint32_t(0x700),true)->Resource->MainIff()->RuntimeInfo.Path==charPath,"current loose winner not decoded");
    NBRS scoped;auto savedRegistry=provider->Neighbors;provider->Neighbors=&scoped;
    check(provider->AddMissingNeighbors(prepare,added,error),"provider candidate enumeration failed");
    check(scoped.DefaultNeighbourByGUID.count(0x700)&&scoped.DefaultNeighbourByGUID.count(0x701)&&scoped.DefaultNeighbourByGUID.count(0x702),"legitimate loose person omitted");
    provider->Neighbors=savedRegistry;
    // Created after the preload list was formed: equivalent registration point
    // to WriteCharacter, which marks these definitions neighbourhood-scoped.
    auto casPath=provider->UserPath+"/Characters/later-cas.iff";
    personFile(casPath,0x703);world->AddStandaloneIff(casPath,true,true);
    fs::create_directories(dir/"save/UserData2/Characters");
    provider->UserPath=(dir/"save/UserData2").string(); // InitSpecific's scope change
    provider->LoadCharacters();
    check(!world->Entries.count(0x701)&&!world->Entries.count(0x703),"late CAS or foreign resident leaked across neighbourhoods");
    check(world->Entries[0x700].FileName==downloadPath&&world->PersonGUIDs.count(0x700)&&world->Get(uint32_t(0x700),true)->Resource->MainIff()->RuntimeInfo.Path==downloadPath,"global loose winner was not restored");
    provider->UserPath=(dir/"save/UserData").string();
    provider->PreparingPersonTemplate=true;std::vector<uint8_t> beforeFile;RawIff::ReadFile(path,beforeFile);
    check(!provider->SaveNeighbourhood(true)&&!TS1NeighbourhoodWriter::SaveRoster(provider.get(),path,&error),"detached provider wrote files");
    std::vector<uint8_t> afterFile;RawIff::ReadFile(path,afterFile);check(beforeFile==afterFile,"blocked preparation changed disk");
    // Actual AddMissingNeighbors -> real out-of-world person -> materialized
    // OBJD/BHAV Init -> live primitives. No callback stub in this gate.
    provider->PreparingPersonTemplate=false;world->Entries.clear();world->RebuildIndexes();
    content->BasePath=(dir/"install").string();
    fs::create_directories(dir/"install/objectdata/globals");
    RawIffChunk globalChunk;globalChunk.Type="UNKN";globalChunk.ID=1;globalChunk.Data={0};
    check(RawIff::WriteFileAtomic((dir/"install/objectdata/globals/global.iff").string(),RawIff::Build({globalChunk})),"synthetic Global resource missing");
    content->WorldObjectGlobals->Init();
    check(content->WorldObjectGlobals->Get("global",false)!=nullptr,"synthetic Global resource not selected");
    provider->MainResource=std::make_unique<IffFile>(path);provider->Neighbors=provider->MainResource->Get<NBRS>(1);
    // A valid FAMI is present in the FILE cloned by the preparation service.
    FAMI family;family.FamilyNumber=1;family.HouseNumber=9;family.Budget=777;
    st::Writer familyData(st::Endian::Little);family.Write(nullptr,familyData);
    IffRawFile withFamily;check(withFamily.Load(path,&error),"family source unreadable");
    // This Init gate has genuine writable neighbour person data. The earlier
    // lossless fixture deliberately retains the separate Mode0/raw32 case.
    auto mutableNeighbor=provider->Neighbors->NeighbourByID.at(1);
    mutableNeighbor->PersonMode=9;mutableNeighbor->PersonData.assign(88,0);
    st::Writer registryData(st::Endian::Little);provider->Neighbors->Write(nullptr,registryData);
    withFamily.Set("NBRS",1,registryData.ToArray());
    withFamily.Set("FAMI",1,familyData.ToArray());check(withFamily.Save(path,&error),"family fixture save failed");
    provider->MainResource=std::make_unique<IffFile>(path);provider->Neighbors=provider->MainResource->Get<NBRS>(1);
    auto ngbh=new NGBH();ngbh->ChunkID=1;ngbh->ChunkType="NGBH";ngbh->ChunkProcessed=true;ngbh->NeighborhoodData[0]=55;
    provider->MainResource->AddChunk(ngbh);provider->Neighborhood=ngbh;
    auto tatt=new TATT();tatt->ChunkID=1;tatt->ChunkType="TATT";tatt->ChunkProcessed=true;tatt->TypeAttributesByGUID[0x810]={66};
    provider->MainResource->AddChunk(tatt);provider->TypeAttributes=tatt;
    auto selectedFamily=provider->GetFamily(1);check(selectedFamily&&selectedFamily->Budget==777,"family fixture not materialized");
    auto oldNeighbor=provider->Neighbors->NeighbourByID.at(1);const auto oldWords=oldNeighbor->PersonData;
    const auto originalRecords=provider->Neighbors->Entries.size();
    const auto authoredPath=(dir/"install/Downloads/init-person.iff").string();
    check(RawIff::WriteFileAtomic(authoredPath,RawIff::Build({authoredPerson(0x810,false),authoredInit(false),bodyStrings()})),"authored Init file write failed");
    check(world->AddStandaloneIff(authoredPath,true)>0,"authored Init registration failed");
    const auto templatePath=(dir/"install/Downloads/template-person.iff").string();
    // Template is a resource, not a second reconciliation candidate.
    auto templateObj=authoredPerson(TS1CharacterFiles::TEMPLATE_PERSON_GUID,false);
    check(RawIff::WriteFileAtomic(templatePath,RawIff::Build({templateObj})),"template materialization failed");
    world->AddStandaloneIff(templatePath,true);world->Entries[TS1CharacterFiles::TEMPLATE_PERSON_GUID].Person=false;world->RebuildIndexes();
    auto cachedAuthored=world->Get(uint32_t(0x810),true);check(cachedAuthored!=nullptr,"authored resource not decoded");
    auto cachedStrings=cachedAuthored->Resource->Get<STR>(200);check(cachedStrings&&cachedStrings->GetString(13)=="1","juvenile body strings missing");
    auto expectedEntries=world->Entries;auto beforeFiles=filesIn(dir);
    auto selected=provider.get();content->Neighborhood=std::move(provider);
    const auto debugSeed=FSO::SimAntics::VMContext::DebugSeed;const auto useWorld=FSO::SimAntics::VM::UseWorld();
    check(selected->AddMissingNeighbors(PreparePersonTemplate,added,error)&&added==1,error.c_str());
    check(content->Neighborhood.get()==selected,"preparation did not restore selected provider");
    auto prepared=selected->Neighbors->NeighbourByID.at(selected->Neighbors->DefaultNeighbourByGUID.at(0x810));
    const bool markersMatch=prepared->PersonData[80]==222&&prepared->PersonData[81]==333&&prepared->PersonData[82]==444
        &&prepared->PersonData[83]==501&&prepared->PersonData[84]==1&&prepared->PersonData[85]==123&&prepared->PersonData[86]==321&&prepared->PersonData[87]>0;
    if(!markersMatch){
        const int expected[]={222,333,444,501,1,123,321};
        const char* meaning[]={"NGBH","TATT","neighbor data","budget credit","inventory count","clone write refusal","Init completion"};
        for(int i=0;i<7;++i)std::cerr<<"Init marker PD"<<(80+i)<<" ("<<meaning[i]<<"): actual="
            <<prepared->PersonData[80+i]<<" expected="<<expected[i]<<'\n';
        std::cerr<<"Init marker PD87 (PetToAdult target): actual="<<prepared->PersonData[87]<<" expected>0\n";
    }
    check(markersMatch,"authored Init did not execute every mutation and refused write path");
    check(selected->Neighborhood==ngbh&&ngbh->NeighborhoodData[0]==55&&ngbh->InventoryByID.empty(),"Init escaped to real NGBH/inventory");
    check(selected->TypeAttributes==tatt&&tatt->TypeAttributesByGUID.at(0x810)==std::vector<int16_t>{66},"Init escaped to real TATT");
    check(selectedFamily->Budget==777&&oldNeighbor->PersonData==oldWords,"Init escaped to real family or neighbour data");
    check(selected->Neighbors->Entries.size()==originalRecords+1&&selected->DirtyAvatars.empty(),"prototype published unrelated records or dirty identities");
    check(filesIn(dir)==beforeFiles&&cachedStrings->GetString(13)=="1","authored Init changed player/resource files or cached strings");
    check(FSO::SimAntics::VMContext::DebugSeed==debugSeed&&FSO::SimAntics::VM::UseWorld()==useWorld,"preparation leaked VM global boundary state");
    check(world->Entries.size()==expectedEntries.size(),"Init changed resource registry size");
    for(const auto& entry:expectedEntries){auto actual=world->Entries.find(entry.first);
        check(actual!=world->Entries.end()&&actual->second.FileName==entry.second.FileName&&actual->second.Person==entry.second.Person,"Init changed resource winner");}
    // A genuinely failing authored Init must fail the complete reconciliation,
    // even though VMThread suppresses its exception internally.
    const auto badPath=(dir/"install/Downloads/bad-init-person.iff").string();
    check(RawIff::WriteFileAtomic(badPath,RawIff::Build({authoredPerson(0x811,true),authoredInit(true),bodyStrings()})),"bad Init file write failed");
    world->AddStandaloneIff(badPath,true);beforeFiles=filesIn(dir);const auto countBefore=selected->Neighbors->Entries.size();
    check(!selected->AddMissingNeighbors(PreparePersonTemplate,added,error)&&added==0&&!error.empty(),"suppressed authored Init exception was accepted");
    check(content->Neighborhood.get()==selected&&selected->Neighbors->Entries.size()==countBefore
        &&!selected->Neighbors->DefaultNeighbourByGUID.count(0x811),"failed Init published a neighbour or lost singleton");
    check(ngbh->NeighborhoodData[0]==55&&ngbh->InventoryByID.empty()&&selectedFamily->Budget==777
        &&tatt->TypeAttributesByGUID.at(0x810)==std::vector<int16_t>{66}&&oldNeighbor->PersonData==oldWords&&filesIn(dir)==beforeFiles&&cachedStrings->GetString(13)=="1",
        "failed Init leaked original state or files");
    fs::remove_all(dir);std::cout<<"neighbour reconciliation contracts passed\n";return 0;
}catch(const std::exception& e){if(!dir.empty())fs::remove_all(dir);std::cerr<<e.what()<<'\n';return 1;}}
