// SPDX-License-Identifier: MPL-2.0
#include "career_dialog_text.hpp"
#include "odh/dialog.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
using namespace odh;using namespace odh_host;
namespace {
void require(bool yes,const char* reason){if(!yes)throw std::runtime_error(reason);}
struct Fixture {
    CareerCatalog catalog{std::vector<std::pair<std::uint16_t,Career>>{}};std::map<std::uint16_t,StringTable> tables;CareerDialogBindings bindings;std::string error;
    Fixture(){Career c;c.levels.resize(10);catalog=CareerCatalog({{9,c},{65535,c}});
        for(const auto id:{std::uint16_t{9},std::uint16_t{1009}}){StringTable t{};t.state=StringTableState::decoded;
            for(const auto language:{1,14})for(unsigned index=0;index<33;++index)t.entries.push_back({std::uint8_t(language),"L"+std::to_string(language)+"T"+std::to_string(id)+"I"+std::to_string(index),{}});
            tables.emplace(id,std::move(t));}
        bindings.careers=&catalog;bindings.language=14;bindings.caller=CareerDialogPerson{9,1,false};
        bindings.strings=[this](auto id)->std::optional<const StringTable*>{const auto found=tables.find(id);return found==tables.end()?nullptr:&found->second;};
    }
    auto text(std::string_view token,std::vector<std::uint16_t> indexes,std::vector<std::int16_t> locals={}){return career_dialog_text(token,indexes,locals,bindings,error);}
    void empty(std::uint16_t id,unsigned language,unsigned index){unsigned at=0;for(auto& entry:tables.at(id).entries)if(entry.language==language&&at++==index){entry.value.clear();return;}}
};
void source_matrix(){
    Fixture f;
    for(const auto& token:{"Job:","JobDesc:","JobOffer:"}){
        auto one=f.text(token,{32000});require(bool(one),"one selector uses the actual caller career, not a local/temp/constant job");
        auto two=f.text(token,{1,0},{2,9});require(bool(two),"two selectors read both source locals in the specified order");
        const auto expected=std::string(token)=="Job:"?7U:std::string(token)=="JobDesc:"?8U:1U;
        require(two->index==expected&&two->table==(std::string(token)=="Job:"?1009:9),"source TS1 stride-two title/description and career-wide offer indices");
        require(!f.text(token,{1,0,7},{2,9}),"three selectors are not a supported source helper call; the shared parser retains literal suffix bytes");
    }
    for(const auto language:{0,1,2,14,19,20}){
        f.bindings.language=std::uint8_t(language);
        const bool twelve=language==0||language==1||language==2||language==19;
        for(const int hour:{0,9,12,15,23}){
            const auto text=f.text("TimeLocal:",{0,32767},{std::int16_t(hour)});
            const auto shown=twelve?(hour==0?12:hour>12?hour-12:hour):hour;
            const auto expected=(shown<10?std::string{" "}:std::string{})+std::to_string(shown)+":00"+
                (twelve?(hour>=12?" pm":" am"):"");
            require(text&&text->bytes==expected,"native locale hour formatter preserves field width, zero minutes, noon/midnight and exact suffix policy");
        }
    }
    f.bindings.language=14;
    require(f.text("TimeLocal:",{0,32767},{15})->bytes=="15:00","source ignores a second parsed minute selector rather than reading a missing local");
    require(!f.text("TimeLocal:",{0,1,2},{9,30}),"TimeLocal helper rejects a third selector instead of fabricating a time suffix");
    f.bindings.caller->alternate_title=true;
    require(f.text("Job:",{0})->index==24,"source caller bit selects nonempty alternative title for its actual level");
    require(f.text("JobOffer:",{0})->index==2,"source caller bit selects nonempty career-wide alternative offer");
    f.empty(1009,14,24);f.empty(9,14,2);
    require(f.text("Job:",{0})->index==5&&f.text("JobOffer:",{0})->index==1,"source empty alternative title/offer falls back to its main text");
    f.tables.erase(1009);require(f.text("Job:",{0})->table==9,"known absent optional title bank falls back to the base STR");
    f.bindings.language=20;require(f.text("JobDesc:",{0})->language==1,"absent requested language set falls back to English");
}
void negatives(){
    Fixture f;
    require(!f.text("Job:",{})&&!f.error.empty(),"missing selectors reject rather than fabricate empty text");
    require(!f.text("Job:",{0,1},{9}),"missing local selector is unknown");
    require(!f.text("Job:",{0,1},{8,0}),"missing career refuses without an English placeholder");
    require(!f.text("Job:",{0,1},{9,-1})&&!f.text("Job:",{0,1},{9,10}),"invalid signed levels refuse");
    require(!f.text("Temp:",{0},{9}),"unrelated variable tokens are not reinterpreted");
    require(!f.text("TimeLocal:",{1},{9})&&!f.text("TimeLocal:",{0},{-1})&&!f.text("TimeLocal:",{0},{24}),"missing or out-of-range source TimeLocal input refuses");
    f.bindings.caller.reset();require(!f.text("Job:",{0,1},{9,0}),"two locals do not replace missing caller gender authority");
    f.bindings.caller=CareerDialogPerson{9,0,false};
    const auto source=f.bindings.strings;f.bindings.strings=[source](auto id)->std::optional<const StringTable*>{if(id==1009)return {};return source(id);};
    require(!f.text("Job:",{0}),"unknown optional title-bank ownership does not become absent fallback");
    f.bindings.strings=source;f.tables.erase(9);require(!f.text("JobDesc:",{0}),"missing required STR refuses");
    f.tables[9]=f.tables.at(1009);f.empty(9,14,4);const auto empty=f.text("JobDesc:",{0});require(empty&&empty->bytes.empty()&&empty->language==14,"known empty source text remains distinct from unavailable text and is not replaced with English");
    f.tables[9].entries.clear();f.tables[9].entries.push_back({14,"only entry0",{}});
    require(!f.text("JobDesc:",{0}),"short present language set does not fall back per-entry");
}
void message_family(){Fixture f;std::vector<std::int16_t> locals{155,0,9,15,9,0,0,0,310};
    const auto result=expand_dialog_text("$Job:4:5 | $JobDesc:4:5 | $TimeLocal:2 - $TimeLocal:3",[&](auto token,const auto& indexes)->std::optional<std::string>{
        const auto text=f.text(token,indexes,locals);return text?std::optional<std::string>{text->bytes}:std::nullopt;});
    require(result=="L14T1009I3 | L14T9I4 |  9:00 - 15:00","the complete career/time dependency family expands through the actual shared dialog parser");
}
}
int main(){try{source_matrix();negatives();message_family();std::cout<<"career dialog source-family matrix passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
