#include "odh/formats.hpp"
#include <iostream>
#include <sstream>
int main(){try{
    unsigned checks=0;const auto check=[&](bool value){++checks;if(!value)throw std::runtime_error("OBJD startup assertion");};
    std::string bytes(158,'\0');bytes[0]=static_cast<char>(138);
    const auto put=[&](std::size_t p,std::uint16_t value){bytes[p]=static_cast<char>(value&255);bytes[p+1]=static_cast<char>(value>>8);};
    put(38,200);put(86,4097);put(94,4098);put(156,8210);
    std::istringstream stream(bytes);auto result=odh::read_objd_startup(odh::View(stream,bytes.size()));
    check(result.version==138&&result.body_strings==200&&result.init==4097&&result.dynamic_multitile==8210);
    for(std::size_t size=0;size<158;++size){std::istringstream short_stream(bytes.substr(0,size));
        bool rejected=false;try{odh::read_objd_startup(odh::View(short_stream,size));}catch(const odh::FormatError&){rejected=true;}check(rejected);}
    bytes[0]=static_cast<char>(139);std::istringstream unsupported(bytes);bool rejected=false;
    try{odh::read_objd_startup(odh::View(unsupported,bytes.size()));}catch(const odh::FormatError&){rejected=true;}check(rejected);
    bytes[0]=static_cast<char>(138);put(86,0);put(156,255);std::istringstream skipped(bytes);
    result=odh::read_objd_startup(odh::View(skipped,bytes.size()));check(result.init==0&&result.dynamic_multitile==255);
    std::istringstream parent("padding"+bytes+"tail");result=odh::read_objd_startup(odh::View(parent,7+bytes.size()+4).subview(7,bytes.size()));
    check(result.body_strings==200&&result.dynamic_multitile==255);
    std::cout<<checks<<" OBJD startup checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
