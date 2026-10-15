#include "odh/hit_arguments.hpp"
#include <iostream>
#include <sstream>

namespace {
int checks=0;
void require(bool condition,const char* why){++checks;if(!condition)throw std::runtime_error(why);}
template<class F>void rejects(F action){
    bool rejected=false;try{action();}catch(const odh::FormatError&){rejected=true;}
    require(rejected,"unresolved HOT arguments must reject");
}
odh::Hot parse(const std::string& text){
    std::istringstream stream(text);
    return odh::read_hot(odh::View(stream,text.size()));
}
}
int main(){try{
    using namespace odh;
    const HotEvent ordinary{1,7,{}};
    const HitArguments caller{21,-55,1024};
    require(resolve_hit_arguments(ordinary,caller)==caller &&
            caller==HitArguments({21,-55,1024}),"zero overrides preserve each raw caller word without mutation");
    require(play_sound_arguments(ordinary,-7)==HitArguments({-7,0,0}) &&
            play_sound_arguments(ordinary,0)==HitArguments({0,0,0}) &&
            play_sound_arguments(ordinary,12)==HitArguments({12,0,0}),
            "selected source is sign extended; no source-12 alias or invented zero fallback");
    require(play_sound_arguments(ordinary,-32768)==HitArguments({-32768,0,0}) &&
            play_sound_arguments(ordinary,32767)==HitArguments({32767,0,0}),
            "selected int16 source extremes preserved");
    HotEvent overrides{1,7,{}};
    overrides.argument_overrides={{0,-2147483647-1,2147483647}};
    require(resolve_hit_arguments(overrides,caller)==HitArguments({21,-2147483647-1,2147483647}) &&
            caller==HitArguments({21,-55,1024}),"signed nonzero overrides replace only their own words");
    overrides.argument_overrides={{-4,0,5}};
    require(play_sound_arguments(overrides,-7)==HitArguments({-4,0,5}),
            "first and third overrides apply independently to PlaySound caller");
    overrides.argument_overrides[1].reset();rejects([&]{resolve_hit_arguments(overrides,caller);});
    overrides.argument_overrides[1]=0;
    overrides.unsupported_reason="unresolved symbol";
    rejects([&]{resolve_hit_arguments(overrides,caller);});
    HotEvent generic{};
    require(resolve_hit_arguments(generic,caller)==caller,
            "pure argument resolver does not impose event-type policy");

    const auto parsed=parse("[EventMappingEquate]\nkNeg=-9\n[EventMapping]\n"
                            "first=1,7,kNeg,0,6\nsecond=1,7\n"
                            "bad=1,7,,0,6\n");
    require(resolve_hit_arguments(parsed.events.at("first"),caller)==HitArguments({-9,-55,6}),
            "parsed signed equate and explicit zero resolve per component");
    require(resolve_hit_arguments(parsed.events.at("second"),caller)==caller,
            "omitted trailing HOT argument words are known zero");
    rejects([&]{resolve_hit_arguments(parsed.events.at("bad"),caller);});
    std::cout<<checks<<" pure HOT argument checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
