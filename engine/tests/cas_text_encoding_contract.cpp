/* SPDX-License-Identifier: MPL-2.0 */
// Save through the real CAS writer, then reopen character and neighborhood
// STR-3 chunks. Synthetic resources isolate this test from commercial data.
#include "app/cas/CASFamilyWriter.h"
#include "app/ui/OriginalTextEncoding.h"
#include "files/formats/iff/chunks/NBRS.h"
#include "files/formats/iff/chunks/STR.h"
#include "files/formats/iff/chunks/CTSS.h"
#include "content/TS1NeighbourProvider.h"
#include "content/ContentManager.h"
#include "simantics/utils/VMTS1ResidentReconciliation.h"
#include "core/io.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <chrono>
using namespace simapp;
using namespace simapp::cas;
using namespace FSO::Files::Formats::IFF::Chunks;
namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void le32(std::vector<uint8_t>& b,uint32_t v){for(int i=0;i<4;++i)b.push_back(uint8_t(v>>(i*8)));}
void write(const fs::path& p,const std::vector<uint8_t>& b){require(CASFamilyWriter::WriteFileAtomic(p.string(),b),"fixture write failed");}
std::vector<uint8_t> read(const fs::path& p){std::vector<uint8_t> b;require(CASFamilyWriter::ReadFile(p.string(),b),"reopen failed");return b;}
std::vector<CASRawChunk> reopen(const fs::path& p){std::vector<CASRawChunk> c;require(CASFamilyWriter::ReadIff(read(p),c),"reopened IFF failed parsing");return c;}
const CASRawChunk& find(const std::vector<CASRawChunk>& c,const std::string& type){for(const auto& x:c)if(x.Type==type)return x;throw std::runtime_error("missing saved chunk");}
std::string decode(const CASRawChunk& c,int index,uint8_t language){
    require(c.Data.size()>4 && c.Data[0]==0xfd && c.Data[1]==0xff && c.Data[4]==language,"wrong STR-3 format/language tag");
    STR table;st::Reader input(c.Data,st::Endian::Big);table.Read(nullptr,input);
    return original::LegacyToUTF8(table.GetString(index));
}
void saved_resident_reconciliation(){
    using namespace FSO::SimAntics::Engine::Utils;
    using D=ResidentReconciliation;
    SavedResidentAffiliation removed;removed.Resident=true;removed.SavedFamily=7;
    removed.NeighbourKnown=true;removed.NeighbourFamily=0;
    require(ReconcileSavedResident(removed,7,9)==D::RemoveFormerResident,"reused family ID retained a detached saved resident");
    auto moved=removed;moved.NeighbourFamily=7;moved.CanonicalFamilyKnown=true;moved.CanonicalMember=true;moved.CanonicalHouse=0;
    require(ReconcileSavedResident(moved,8,9)==D::RemoveFormerResident,"evicted household pet retained old lot without deleting family");
    auto current=moved;current.CurrentMember=true;current.CanonicalHouse=9;
    require(ReconcileSavedResident(current,7,9)==D::Keep,"current household human/pet removed");
    auto visitor=moved;visitor.Resident=false;visitor.CanonicalHouse=5;
    require(ReconcileSavedResident(visitor,8,9)==D::Keep,"positioned visitor mistaken for former resident");
    auto missing=removed;missing.NeighbourKnown=false;
    require(ReconcileSavedResident(missing,7,9)==D::UnknownAffiliation,"missing NBRS affiliation guessed");
    auto unassigned=removed;unassigned.SavedFamily=0;
    require(ReconcileSavedResident(unassigned,7,9)==D::UnknownAffiliation,"unassigned NPC guessed to be former resident");
    auto missingFamily=moved;missingFamily.CanonicalFamilyKnown=false;
    require(ReconcileSavedResident(missingFamily,8,9)==D::UnknownAffiliation,"missing canonical FAMI guessed");
    auto inconsistent=moved;inconsistent.CanonicalMember=false;
    require(ReconcileSavedResident(inconsistent,8,9)==D::UnknownAffiliation,"inconsistent NBRS/FAMI membership deleted");
    auto conflict=moved;conflict.CanonicalHouse=9;
    require(ReconcileSavedResident(conflict,8,9)==D::UnknownAffiliation,"conflicting lot ownership silently resolved");
}
void personal_resolution(){
    auto bytes=CASFamilyWriter::MakeSTRFormat3({std::string("Jos\xe9",4),""},14);
    const auto original=bytes;
    CTSS personal;st::Reader input(bytes,st::Endian::Big);personal.Read(nullptr,input);
    for(auto locale:{STRLangCode::EnglishUS,STRLangCode::Japanese,STRLangCode::Portuguese}){
        STR::DefaultLangCode=locale;
        auto text=FSO::Content::TS1::ResolvePersonalText(&personal);
        require(text.SourceLanguage==14 && !text.UTF8 && !text.Ambiguous,"sole personal source provenance lost");
        require(original::PersonalToUTF8(text.Value(0),text.SourceLanguage,text.UTF8)=="José","personal source decoded in UI language");
        require(text.Values.size()==2 && text.Value(1).empty(),"empty authored biography borrowed another entry");
        if(locale!=STRLangCode::Portuguese)require(personal.Length()==0 && personal.GetString(0).empty(),"personal resolver changed generic STR fallback");
        require(bytes==original,"locale resolution changed raw saved bytes");
    }
    STR::DefaultLangCode=STRLangCode::EnglishUS;
    const auto japaneseBytes=std::string("\x82\xa0",2); // CP932 hiragana A
    personal.LanguageSets[13].Strings.clear();personal.LanguageSets[14].Strings={{15,japaneseBytes,""},{15,"",""}};
    auto japanese=FSO::Content::TS1::ResolvePersonalText(&personal);
    require(japanese.SourceLanguage==15 && original::PersonalToUTF8(japanese.Value(0),15,false)=="あ","Japanese identity source decoded as requested English");
    personal.LanguageSets[13].Strings={{14,"PT",""}};
    auto ambiguous=FSO::Content::TS1::ResolvePersonalText(&personal);
    require(ambiguous.Ambiguous && ambiguous.SourceLanguage==0 && ambiguous.Values.empty(),"ambiguous alternative identities chose first set");
    personal.LanguageSets[0].Strings={{1,"US",""},{1,"US bio",""}};
    require(FSO::Content::TS1::ResolvePersonalText(&personal).Value(0)=="US","personal US fallback changed");
    STR::DefaultLangCode=STRLangCode::Portuguese;
    require(FSO::Content::TS1::ResolvePersonalText(&personal).Value(0)=="PT","legitimate selected personal translation ignored");
    // Read-only character parsing must retain a sole author set, including a
    // private Windows-style path with mixed case. Technical STR tables retain
    // their existing memory optimisation and generic fallback semantics.
    STR::DefaultLangCode=STRLangCode::EnglishUS;
    FSO::Files::Formats::IFF::IffFile file;file.SourcePath="C:\\private\\CHARACTERS\\User00001.iff";
    const auto retain=FSO::Files::Formats::IFF::IffFile::RETAIN_CHUNK_DATA;
    FSO::Files::Formats::IFF::IffFile::RETAIN_CHUNK_DATA=false;
    CTSS loaded;st::Reader reader(bytes,st::Endian::Big);loaded.Read(&file,reader);
    require(FSO::Content::TS1::ResolvePersonalText(&loaded).SourceLanguage==14,"readonly character discarded authored language");
    STR technical;st::Reader technicalReader(bytes,st::Endian::Big);technical.Read(&file,technicalReader);
    require(technical.LanguageSets[13].Strings.empty(),"technical STR kept unselected translations");
    file.SourcePath="/private/GameData/Object.iff";CTSS catalogue;st::Reader catalogueReader(bytes,st::Endian::Big);catalogue.Read(&file,catalogueReader);
    require(catalogue.LanguageSets[13].Strings.empty(),"catalogue CTSS kept all translations");
    FSO::Files::Formats::IFF::IffFile::RETAIN_CHUNK_DATA=retain;
    const std::vector<uint8_t> utf8={0xff,0xff,1,0,0xc3,0xa9,0};
    CTSS unicode;st::Reader unicodeReader(utf8,st::Endian::Big);unicode.Read(nullptr,unicodeReader);
    auto text=FSO::Content::TS1::ResolvePersonalText(&unicode);
    require(text.UTF8 && original::PersonalToUTF8(text.Value(0),text.SourceLanguage,text.UTF8)=="é","UTF8 STR provenance lost");
}
void selected_and_fallback(){
    // Unequal language lengths expose the original US-only Length bug even
    // when both sets exist, while retaining the documented whole-set fallback.
    auto bytes=CASFamilyWriter::MakeSTRFormat3({"English", "US biography", "US extra"},1);
    auto portuguese=CASFamilyWriter::MakeSTRFormat3({std::string("A\xe7\xfa" "car",6),"PT biography"},14);
    bytes[2]=5;bytes[3]=0;bytes.insert(bytes.end(),portuguese.begin()+4,portuguese.end());
    STR table;st::Reader input(bytes,st::Endian::Big);table.Read(nullptr,input);
    const auto originalPT=table.LanguageSets[13].Strings[0].Value;
    const auto originalUS=table.LanguageSets[0].Strings[0].Value;
    STR::DefaultLangCode=STRLangCode::Portuguese;
    require(table.Length()==2 && table.GetString(1)=="PT biography", "selected language length diverges from GetString");
    require(table.GetString(2).empty(),"reader invented per-entry fallback into shorter selected set");
    STR::DefaultLangCode=STRLangCode::EnglishUS;
    require(table.Length()==3 && table.GetString(2)=="US extra", "switching to US failed");
    require(table.Length(STRLangCode::Portuguese)==2,"explicit language length ignored");
    STR::DefaultLangCode=STRLangCode::Japanese;
    require(table.Length()==3 && table.GetString(0)=="English", "missing selected set did not fall back to US");
    require(table.GetStringEntry(0)->LanguageCode==1,"fallback rewrote the source language tag");
    STR::DefaultLangCode=STRLangCode::Portuguese;
    require(table.Length()==2 && table.GetString(0)==originalPT,"language flip changed selected bytes");
    require(table.LanguageSets[0].Strings[0].Value==originalUS,"language flip changed fallback bytes");
    table.LanguageSets.resize(1); // TSO-format chunks may have fewer than20sets.
    require(table.Length()==3 && table.GetString(0)=="English","short language table did not safely fall back");
    table.LanguageSets.clear();
    require(table.Length()==0 && table.GetString(0).empty(),"empty language table not safely readable");
}
void unequal_mutation(){
    // Body-string growth must extend the language being read, without using
    // its append index against a shorter independently initialized language.
    for(bool selectedLonger:{false,true}){
        STR table;
        const size_t usCount=selectedLonger?2:3,ptCount=selectedLonger?3:2;
        for(size_t i=0;i<usCount;++i)table.LanguageSets[0].Strings.push_back({1,"US"+std::to_string(i),"US comment"});
        for(size_t i=0;i<ptCount;++i)table.LanguageSets[13].Strings.push_back({14,"PT"+std::to_string(i),"PT comment"});
        STR::DefaultLangCode=STRLangCode::Portuguese;
        STRItem added;added.Value="new body slot";added.Comment="slot comment";added.LanguageCode=20;
        table.AppendString(added);
        require(table.Length()==int(ptCount+1) && table.Length(STRLangCode::EnglishUS)==int(usCount),"selected append changed a different language length");
        require(table.GetStringEntry(int(ptCount))->LanguageCode==14 && table.GetString(int(ptCount))==added.Value,"selected append lost slot language or bytes");
        for(size_t i=0;i<usCount;++i){auto& entry=table.LanguageSets[0].Strings[i];require(entry.Value=="US"+std::to_string(i)&&entry.Comment=="US comment"&&entry.LanguageCode==1,"append modified an unrelated language");}
        for(size_t i=0;i<ptCount;++i){const auto* entry=table.GetStringEntry(int(i));
            require(entry&&entry->Value=="PT"+std::to_string(i)&&entry->Comment=="PT comment"&&entry->LanguageCode==14,"append changed an existing selected slot or source language");}
        // Selected append and SetString both resolve absent JA to US; neither
        // initializes JA nor relabels existing US entries.
        table.AppendString(added,STRLangCode::Japanese);
        require(table.Length(STRLangCode::EnglishUS)==int(usCount+1)&&table.LanguageSets[14].Strings.empty(),"fallback append invented a language set");
        require(table.GetStringEntry(int(usCount),STRLangCode::EnglishUS)->LanguageCode==1,"fallback append used requested rather than actual language tag");
        table.SetString(0,"changed PT");
        require(table.GetString(0)=="changed PT"&&table.GetString(0,STRLangCode::EnglishUS)=="US0","selected setter changed fallback language");
    }
    // Global structural edits require valid indexes in every participating set.
    // Rejection is checked before editing the first set, avoiding partial edits.
    STR table;table.LanguageSets[0].Strings={{1,"US0",""},{1,"US1",""},{1,"US2",""}};
    table.LanguageSets[13].Strings={{14,"PT0",""}};
    auto snapshot=[&](){st::Writer writer(st::Endian::Big);table.Write(nullptr,writer);return writer.ToArray();};
    const auto before=snapshot();
    auto reject=[&](auto edit){bool rejected=false;try{edit();}catch(const std::out_of_range&){rejected=true;}
        require(rejected&&snapshot()==before,"unequal global edit was not rejected before mutation");};
    reject([&]{table.InsertString(2,STRItem{});});
    reject([&]{table.RemoveString(1);});
    reject([&]{table.SwapString(0,2);});
    reject([&]{table.InsertString(-1,STRItem{});});
    table.InsertString(1,STRItem{0,"inserted",""});
    require(table.LanguageSets[0].Strings[1].LanguageCode==1&&table.LanguageSets[13].Strings[1].LanguageCode==14,"valid global insertion lost slot tags");
    table.SwapString(0,1);table.RemoveString(0);
    require(snapshot()==before,"valid structural operations did not preserve original rows");
}
void refresh(const fs::path& neighborhood,const fs::path& character,int16_t id,const CASFamilyDesc& draft){
    // Exercise the public provider refresh path and the exact biography guard
    // used by NhoodModel, instead of bypassing Length via raw GetString calls.
    auto manager=FSO::Content::Content::Get();
    const auto previousRoot=manager->SimsCompleteDir;
    manager->SimsCompleteDir=neighborhood.parent_path().string();
    FSO::Content::TS1::TS1NeighborhoodProvider provider(manager);
    provider.MainResource=std::make_unique<FSO::Files::Formats::IFF::IffFile>(neighborhood.string());
    require(original::LegacyToUTF8(provider.GetFamilyName(id))==draft.LastName,"provider refresh lost localized family name");
    FSO::Files::Formats::IFF::IffFile avatar(character.string());
    auto ctss=avatar.Get<CTSS>(2000);
    require(ctss && ctss->Length()==2,"reopened localized CTSS length lost name/bio");
    const auto bio=ctss->Length()>1?ctss->GetString(1):std::string();
    require(original::LegacyToUTF8(bio)==draft.Members[0].Bio,"model biography length guard lost localized bio");
    manager->SimsCompleteDir=previousRoot;
}
void provider_personal_locale(const fs::path& neighborhood,const fs::path& character,int16_t id,const CASFamilyDesc& draft){
    const auto previous=STR::DefaultLangCode;
    const auto originalNeighborhood=read(neighborhood),originalCharacter=read(character);
    auto manager=FSO::Content::Content::Get();
    FSO::Content::TS1::TS1NeighborhoodProvider provider(manager);
    provider.MainResource=std::make_unique<FSO::Files::Formats::IFF::IffFile>(neighborhood.string());
    for(auto locale:{STRLangCode::EnglishUS,STRLangCode::Japanese,STRLangCode::Portuguese}){
        STR::DefaultLangCode=locale;
        const auto family=provider.GetFamilyText(id);
        require(family.SourceLanguage==14 && !family.Ambiguous,"real provider lost author language after locale change");
        require(original::PersonalLabelUTF8(family.Value(0),family.SourceLanguage,family.UTF8,family.Ambiguous)==draft.LastName,"real provider surname changed with UI language");
        require(provider.GetFamilyName(id)==family.Value(0),"compatibility family query changed original bytes");
        // Reopen under each requested locale: retaining a previously decoded
        // table alone would not detect readonly CTSS filtering on load.
        FSO::Files::Formats::IFF::IffFile avatar(character.string());
        const auto person=FSO::Content::TS1::ResolvePersonalText(avatar.Get<CTSS>(2000));
        require(person.SourceLanguage==14 && person.Values.size()==2,"reopened personal name/bio provenance lost");
        require(original::PersonalLabelUTF8(person.Value(0),person.SourceLanguage,person.UTF8,person.Ambiguous)==draft.Members[0].FirstName,"reopened personal name changed across locale");
        require(original::PersonalToUTF8(person.Value(1),person.SourceLanguage,person.UTF8)==draft.Members[0].Bio,"reopened biography changed across locale");
    }
    auto* names=provider.MainResource->Get<FAMs>(uint16_t(id));
    const auto author=names->LanguageSets[13].Strings[0];
    names->LanguageSets[14].Strings={{15,std::string("\x82\xa0",2),"Japanese source comment"}};
    STR::DefaultLangCode=STRLangCode::EnglishUS;
    const auto ambiguous=provider.GetFamilyText(id);
    require(ambiguous.Ambiguous && ambiguous.SourceLanguage==0 && ambiguous.Values.empty(),"real provider silently chose ambiguous identity");
    require(original::PersonalLabelUTF8(std::string("\xff",1),250,false,ambiguous.Ambiguous)=="[name unavailable]","ambiguous consumer decoded untagged identifier bytes");
    require(original::PersonalLabelUTF8(std::string("\xff",1),0,false,false)=="[name unavailable]","untagged consumer guessed a personal codepage");
    require(names->LanguageSets[13].Strings[0].Value==author.Value && names->LanguageSets[13].Strings[0].LanguageCode==author.LanguageCode,"ambiguous presentation changed author bytes/tags");
    require(read(neighborhood)==originalNeighborhood && read(character)==originalCharacter,"locale/ambiguous presentation rewrote saved personal data");
    STR::DefaultLangCode=previous;
}
void fixture(const fs::path& root){
    fs::create_directories(root/"Characters");
    CASRawChunk obj;obj.Type="OBJD";obj.ID=128;obj.Data.resize(216);
    CASRawChunk body;body.Type="STR#";body.ID=200;body.Data=CASFamilyWriter::MakeSTRFormat3({"adult","body","head"});
    auto tpl=CASFamilyWriter::WriteIff({obj,body});
    const std::string name="People\\TemplatePerson.iff";
    std::vector<uint8_t> far{'F','A','R','!','b','y','A','Z'};le32(far,1);le32(far,uint32_t(16+tpl.size()));
    far.insert(far.end(),tpl.begin(),tpl.end());le32(far,1);le32(far,uint32_t(tpl.size()));le32(far,uint32_t(tpl.size()));le32(far,16);le32(far,uint32_t(name.size()));far.insert(far.end(),name.begin(),name.end());
    write(root/"Objects.far",far);
    NBRS neighbours;st::Writer bytes(st::Endian::Big);neighbours.Write(nullptr,bytes);
    CASRawChunk nbrs;nbrs.Type="NBRS";nbrs.ID=1;nbrs.Data=bytes.ToArray();
    write(root/"Neighborhood.iff",CASFamilyWriter::WriteIff({nbrs}));
}
void row(const fs::path& root,STRLangCode language){
    STR::DefaultLangCode=language;fixture(root);
    CASFamilyDesc draft;draft.LastName="Família";
    CASSimDesc person;person.FirstName="José";person.Bio="João e Ana: café.";draft.Members.push_back(person);
    CASFamilyWriter writer(root.string(),(root/"Objects.far").string());uint32_t next=0x12340000;writer.Random32=[&]{return ++next;};
    auto result=writer.Save(draft,{},CASFamilyWriter::TextEncoding::UTF8);
    require(result.Ok && result.Files.size()==1,"UTF-8 family save failed");
    auto character=reopen(result.Files[0]);auto neighborhood=reopen(root/"Neighborhood.iff");
    const auto code=uint8_t(language);
    require(decode(find(character,"CTSS"),0,code)==person.FirstName,"first name failed save/reopen");
    require(decode(find(character,"CTSS"),1,code)==person.Bio,"biography failed save/reopen");
    require(decode(find(neighborhood,"FAMs"),0,code)==draft.LastName,"surname failed save/reopen");
    refresh(root/"Neighborhood.iff",result.Files[0],result.FamilyID,draft);
    if(language==STRLangCode::Portuguese){
        provider_personal_locale(root/"Neighborhood.iff",result.Files[0],result.FamilyID,draft);
        // A PT-only personal string has no US fallback. Changing locale never
        // relabels it or borrows an unrelated language; returning restores it.
        const auto raw=read(root/"Neighborhood.iff");
        const auto& saved=find(neighborhood,"FAMs");STR table;st::Reader input(saved.Data,st::Endian::Big);table.Read(nullptr,input);
        STR::DefaultLangCode=STRLangCode::EnglishUS;
        require(table.Length()==0 && table.GetString(0).empty(),"PT-only table acquired an invented US fallback");
        STR::DefaultLangCode=language;
        require(table.Length()==1 && original::LegacyToUTF8(table.GetString(0))==draft.LastName,"PT-only name lost after locale flip");
        require(read(root/"Neighborhood.iff")==raw,"reading a different locale changed the save");
        refresh(root/"Neighborhood.iff",result.Files[0],result.FamilyID,draft);
    }
    const auto before=read(root/"Neighborhood.iff");const auto count=std::distance(fs::directory_iterator(root/"Characters"),fs::directory_iterator{});
    draft.Members.push_back(person);draft.Members.back().Bio="unrepresentable 猫";
    const auto rejected=writer.Save(draft,{},CASFamilyWriter::TextEncoding::UTF8);
    require(!rejected.Ok && rejected.Files.empty(),"unrepresentable later member accepted or wrote partial files");
    require(read(root/"Neighborhood.iff")==before,"rejected text changed neighborhood");
    require(std::distance(fs::directory_iterator(root/"Characters"),fs::directory_iterator{})==count,"rejected text left a character file");
    require(original::UIString("José")=="José","UTF-8 UI draft was decoded twice");
    require(original::UIString(std::string("Jos\xe9",4))=="José","legacy UI cache not decoded");
}
}
int main(){
    const auto old=STR::DefaultLangCode;
    fs::path root;
    const auto nonce=std::chrono::steady_clock::now().time_since_epoch().count();
    for(int i=0;i<32;++i){
        auto candidate=fs::temp_directory_path()/("odh-cas-text-"+std::to_string(nonce)+"-"+std::to_string(i));
        if(fs::create_directory(candidate)){root=candidate;break;}
    }
    if(root.empty())return 2;
    try{saved_resident_reconciliation();personal_resolution();selected_and_fallback();unequal_mutation();row(root/"en",STRLangCode::EnglishUS);row(root/"pt",STRLangCode::Portuguese);STR::DefaultLangCode=old;fs::remove_all(root);std::cout<<"CAS text matrix: EN/PT save/reopen/provider refresh, selected-US fallback, unequal language mutation and strict no-write rejection passed\n";return 0;}
    catch(const std::exception& error){STR::DefaultLangCode=old;fs::remove_all(root);std::cerr<<error.what()<<'\n';return 1;}
}
