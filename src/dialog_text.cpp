// SPDX-License-Identifier: MPL-2.0
#include "odh/dialog.hpp"
#include <charconv>
#include <stdexcept>
namespace odh {
std::string expand_dialog_text(std::string_view input,const DialogSubstitution& substitute){
    if(input.size()>4096)throw std::invalid_argument("dialog source text exceeds budget");
    static const std::vector<std::string_view> tokens={"DynamicStringLocal:","DynamicObjectName","CatalogLocal:","ObjectLocal:","FixedLocal:","TimeLocal:","NameLocal:","DateLocal:","Attribute:","JobOffer:","JobDesc:","MoneyXL:","TempXL:","Local:","Param:","Temp:","Job:","ListObject","Neighbor","Object","Me"};
    std::string out;
    for(std::size_t at=0;at<input.size();){
        if(input[at]!='$'){out.push_back(input[at++]);continue;}
        ++at;
        std::string_view token;for(auto t:tokens)if(input.substr(at,t.size())==t){token=t;break;}
        if(token.empty()){
            if(input.substr(at,2)=="\\n"){out.push_back('\n');at+=2;continue;}
            // Native TS1 replaces known prefixes only. Currency and other
            // literal dollars remain text; leave the following byte untouched
            // so $$Local, $$Temp and other doubled prefixes expand uniformly.
            out.push_back('$');continue;
        }
        at+=token.size();std::vector<std::uint16_t> indexes;
        if(token.back()==':'){
            // Original TS1 career/time commands consume at most two selectors.
            // A third colon suffix remains literal, including its leading zeros.
            const bool career=token=="JobOffer:"||token=="Job:"||token=="JobDesc:"||token=="TimeLocal:";
            const auto max=career?2U:(token=="DynamicStringLocal:"||token=="DateLocal:")?3U:1U;
            for(unsigned i=0;i<max;++i){
                const auto begin=at;while(at<input.size()&&input[at]>='0'&&input[at]<='9')++at;
                unsigned index=0;if(begin==at)throw std::invalid_argument("dialog substitution index missing");
                const auto parsed=std::from_chars(input.data()+begin,input.data()+at,index);
                if(parsed.ec!=std::errc{}||index>32767)throw std::invalid_argument("dialog substitution index exceeds source short");
                indexes.push_back(static_cast<std::uint16_t>(index));
                if(i+1==max||at==input.size()||input[at]!=':')break;
                ++at;
            }
        }
        const auto text=substitute?substitute(token,indexes):std::nullopt;
        if(!text)throw std::invalid_argument("dialog string substitution backend unavailable: "+std::string(token));
        if(out.size()>4096||text->size()>4096-out.size())throw std::invalid_argument("expanded dialog text exceeds budget");
        out+=*text;
    }
    if(out.size()>4096)throw std::invalid_argument("expanded dialog text exceeds budget");
    return out;
}
bool dialog_selector_type(std::uint8_t type) noexcept {
    return (type>=5&&type<=11) || type==13;
}
}
