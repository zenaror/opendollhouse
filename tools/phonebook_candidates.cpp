// SPDX-License-Identifier: MPL-2.0
#include "phonebook_candidates.hpp"
#include <algorithm>
#include <set>
namespace odh_host {
PhonebookSelection select_phonebook_choices(const PhonebookSnapshot& snapshot,const PhonebookName& name,
        std::size_t max_choices,std::size_t max_bytes){
    const auto refuse=[](const char* why){return PhonebookSelection{false,{},why};};
    if(max_choices>4096||max_bytes>1048576)return refuse("phonebook budget exceeds dialog bounds");
    if(!snapshot.caller_current)return refuse("phonebook caller differs from current neighbor authority");
    if(!snapshot.relationships_current)return refuse("phonebook caller relationship authority unavailable");
    PhonebookSelection result;std::set<std::int16_t> emitted;std::size_t bytes=0;
    for(auto key:snapshot.relationship_keys){
        const auto word=static_cast<std::uint32_t>(key)&65535U;
        const auto target=static_cast<std::int16_t>(word<=32767U?static_cast<std::int32_t>(word):static_cast<std::int32_t>(word)-65536);
        auto n=std::find_if(snapshot.neighbors.begin(),snapshot.neighbors.end(),[&](const auto& row){return row.id==target;});
        if(n==snapshot.neighbors.end())return refuse("phonebook relationship references a missing current neighbor");
        const auto family=n->person_data_available?n->family:0;
        const auto gender=n->person_data_available?n->gender:0;
        if(!family||gender>=2)continue;
        if(target<=0)return refuse("phonebook eligible neighbor has no positive selectable ID");
        if(!emitted.insert(target).second)return refuse("phonebook relationship keys alias one selectable neighbor");
        if(result.choices.size()>=max_choices)return refuse("phonebook choice budget exhausted");
        if(!name)return refuse("phonebook character display-name provider unavailable");
        const auto label=name(target,n->guid);
        if(!label||label->empty()||label->size()>4096||label->find('\0')!=std::string::npos)
            return refuse("phonebook admitted character display name unavailable");
        if(label->size()>max_bytes-bytes)return refuse("phonebook label byte budget exhausted");
        bytes+=label->size();result.choices.push_back({*label,target});
    }
    result.ready=true;return result;
}
std::optional<PhonebookResponse> phonebook_response(const PhonebookSelection& result,std::optional<std::size_t> selected){
    if(!selected)return PhonebookResponse{0,"-1"};
    if(!result.ready)return {};
    if(*selected>=result.choices.size()||result.choices[*selected].value<=0)return {};
    return PhonebookResponse{1,std::to_string(result.choices[*selected].value)};
}
std::optional<PhonebookResponse> phonebook_button_response(const PhonebookSelection& offered,std::size_t button,std::optional<std::size_t> selected){
    if(button==1)return phonebook_response(offered,{});
    if(button!=0||!selected)return {};
    return phonebook_response(offered,selected);
}

}
