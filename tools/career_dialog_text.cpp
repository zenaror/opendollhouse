// SPDX-License-Identifier: MPL-2.0
#include "career_dialog_text.hpp"
#include <limits>
namespace odh_host {
namespace {
std::optional<CareerDialogText> fail(std::string& error,const char* why){error=why;return {};}
}
std::optional<CareerDialogText> career_dialog_text(std::string_view token,
    const std::vector<std::uint16_t>& indexes,const std::vector<std::int16_t>& locals,
    const CareerDialogBindings& bindings,std::string& error){
    error.clear();
    const bool time=token=="TimeLocal:";
    const auto language=bindings.language?bindings.language:std::uint8_t{1};
    if(!time&&token!="Job:"&&token!="JobDesc:"&&token!="JobOffer:")return fail(error,"career dialog token is outside this source family");
    if(indexes.empty()||indexes.size()>2)return fail(error,"career dialog requires one or two source selectors");
    if(time){
        if(indexes[0]>=locals.size())return fail(error,"TimeLocal source local is unavailable");
        int hour=locals[indexes[0]];
        if(hour<0||hour>=24)return fail(error,"TimeLocal source hour is outside 0..23");
        // Original 0044c904 -> 004a6cc9 -> locale vtable+0x80/005fe2e5.
        // Minute and formatting flags passed by this caller are all zero.
        const bool twelve=language==1||language==2||language==19;
        const bool afternoon=hour>=12;
        if(twelve){if(hour==0)hour=12;else if(hour>12)hour-=12;}
        auto text=(hour<10?std::string{" "}:std::string{})+std::to_string(hour)+":00";
        if(twelve)text+=afternoon?" pm":" am";
        return CareerDialogText{std::move(text),1,{},{}};
    }
    if(!bindings.caller||!bindings.careers||!bindings.strings)return fail(error,"career dialog caller/catalog/string ownership unavailable");
    auto person=*bindings.caller;
    if(indexes.size()>=2){
        if(indexes[0]>=locals.size()||indexes[1]>=locals.size())return fail(error,"career dialog source locals are unavailable");
        person.job=locals[indexes[0]];person.level=locals[indexes[1]];
    }
    const auto career=bindings.careers->lookup(person.job,person.level);
    if(!career.level)return fail(error,"career dialog job or level is unavailable");
    const auto job=static_cast<std::uint16_t>(person.job);
    std::uint16_t table_id=job;
    const auto base=bindings.strings(job);
    if(!base)return fail(error,"career dialog base STR ownership is unknown");
    if(!*base)return fail(error,"career dialog base STR is absent");
    const odh::StringTable* table=*base;
    const auto level=static_cast<std::size_t>(person.level);
    std::size_t index=0;
    if(token=="Job:"){
        // Native job loader selects the optional job+1000 STR, otherwise copies
        // the base table into both title banks. Addition narrows as a WORD.
        const auto alternate_id=static_cast<std::uint16_t>(static_cast<unsigned>(job)+1000U);
        const auto alternate=bindings.strings(alternate_id);
        if(!alternate)return fail(error,"career dialog alternate STR ownership is unknown");
        if(*alternate){table=*alternate;table_id=alternate_id;}
        index=3+2*level;
        if(person.alternate_title){
            const auto* gender=odh::string_in_ts1_language_set(*table,language,23+level);
            if(gender&&!gender->value.empty())index=23+level;
        }
    }else if(token=="JobDesc:")index=4+2*level;
    else{
        // Offer is a localized career-wide string, independent of job level.
        index=1;
        if(person.alternate_title){const auto* gender=odh::string_in_ts1_language_set(*table,language,2);
            if(gender&&!gender->value.empty())index=2;}
    }
    const auto* selected=odh::string_in_ts1_language_set(*table,language,index);
    if(!selected)return fail(error,"career dialog selected source text is absent");
    if(selected->value.size()>4096)return fail(error,"career dialog source text exceeds budget");
    if(selected->value.find('\0')!=std::string::npos)return fail(error,"career dialog source text contains an embedded terminator");
    return CareerDialogText{selected->value,selected->language?selected->language:std::uint8_t{1},table_id,index};
}
}
