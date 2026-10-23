#include "odh/session_entry.hpp"
#include "json.hpp"

#include <charconv>
#include <iostream>
#include <sstream>

namespace {
unsigned number(const std::string& text, unsigned maximum) {
    unsigned value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(text.empty()||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||value>maximum)
        throw odh::FormatError("invalid numeric option");
    return value;
}
std::string run(const std::filesystem::path& root,unsigned mode,std::uint16_t house,
                std::uint8_t language) {
    const auto entry=odh::load_session_entry(root,mode,house,language);
    std::ostringstream out;
    out<<"{\"evidence\":\"session_entry_resolved_only\",\"mode\":"<<entry.neighborhood_mode
       <<",\"house_number\":"<<entry.house_number<<",\"house_file\":"<<json_bytes(entry.house_file)
       <<",\"family_chunk_id\":"<<entry.family_chunk_id<<",\"family_number\":"<<entry.family_number
       <<",\"member_count\":"<<entry.members.size()<<",\"members\":[";
    bool first=true;
    for(const auto& member:entry.members){
        if(!first)out<<',';first=false;
        out<<"{\"guid\":"<<member.guid<<",\"neighbor_id\":"<<member.neighbor_id<<",\"character_asset\":"
           <<json_bytes(member.character_asset)<<",\"object_definition_id\":"
           <<member.object_definition_id<<'}';
    }
    out<<"]}\n";
    return out.str();
}
}

int main(int argc,char** argv){
    try{
        std::optional<std::string> user_data,mode_text,house_text,language_text;
        for(int i=1;i<argc;i+=2){
            if(i+1>=argc)throw odh::FormatError("missing option value");
            const std::string key=argv[i],value=argv[i+1];
            if(key=="--user-data"&&!user_data)user_data=value;
            else if(key=="--mode"&&!mode_text)mode_text=value;
            else if(key=="--house"&&!house_text)house_text=value;
            else if(key=="--language"&&!language_text)language_text=value;
            else throw odh::FormatError("unknown or duplicate option");
        }
        if(!user_data||!mode_text||!house_text)
            throw odh::FormatError("Usage: odh-session-entry --user-data DIR --mode 1..7 --house 1..98 [--language 1..20]");
        const auto mode=number(*mode_text,7),house=number(*house_text,98);
        const auto language=language_text?number(*language_text,20):1U;
        if(mode==0||house==0||language==0)throw odh::FormatError("numeric option outside supported range");
        std::cout<<run(*user_data,mode,static_cast<std::uint16_t>(house),
                       static_cast<std::uint8_t>(language));
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"odh-session-entry: "<<error.what()<<'\n';return 2;
    }
}
