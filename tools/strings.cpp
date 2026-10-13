#include "odh/animation.hpp"
#include "resource.hpp"
#include "json.hpp"
#include <iostream>
int main(int argc,char** argv){if(argc!=4 && argc!=6){std::cerr<<"Usage: odh-strings FAR ENTRY CHUNK_ID [LANGUAGE INDEX]\n";return 2;}
    try{const auto id=selector(argv[3]);std::optional<std::uint16_t> lang,index;
        if(argc==6){lang=selector(argv[4]);index=selector(argv[5]);if(*lang>255)throw odh::FormatError("language outside byte range");}
        std::optional<odh::StringTable> table;
        with_far_entry(argv[1],argv[2],[&](const auto& view){odh::iff_chunks(view,[&](const odh::Chunk& c){
            if(c.type!="STR#"||c.id!=id)return;if(table)throw odh::FormatError("ambiguous STR# chunk");table=odh::read_strings(c.payload);});});
        if(!table)throw odh::FormatError("STR# chunk not found");const auto* selected=lang?odh::string_in_language(*table,static_cast<std::uint8_t>(*lang),*index):nullptr;
        if(lang && !selected)throw odh::FormatError("language/index not found");
        std::cout<<"{\"evidence\":\"decoded_fields_only\",\"format\":";
        if(table->format)std::cout<<*table->format;else std::cout<<"null";
        std::cout<<",\"state\":"<<json_bytes(table->state==odh::StringTableState::decoded?"decoded":table->state==odh::StringTableState::header_only?"header_only":"absent")
                 <<",\"remaining_bytes\":"<<table->remaining_bytes<<",\"entries\":[";
        bool first=true;for(const auto& e:table->entries){if(selected && &e!=selected)continue;if(!first)std::cout<<',';first=false;
            std::cout<<"{\"language\":"<<static_cast<unsigned>(e.language)<<",\"value\":"<<json_bytes(e.value)<<",\"comment\":"<<json_bytes(e.comment)<<'}';}
        std::cout<<"]}\n";return 0;
    }catch(const std::exception& e){std::cerr<<"odh-strings: "<<e.what()<<'\n';return 2;}}
