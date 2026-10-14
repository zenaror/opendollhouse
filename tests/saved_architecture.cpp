#include "odh/saved_architecture.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool b,const char* s){++checks;if(!b)throw std::runtime_error(s);}
void bad(const std::function<void()>& f){bool rejected=false;try{f();}catch(const std::runtime_error&){rejected=true;}require(rejected,"invalid architecture accepted");}
odh::SavedArchitectureArray grid(std::uint16_t id,int stride,int w=3,int h=2){
    odh::SavedArchitectureArray r;r.id=id;r.array.width=w;r.array.height=h;r.array.stride=stride;
    r.array.data.resize(static_cast<std::size_t>(w*h*stride));
    for(std::size_t i=0;i<r.array.data.size();++i)r.array.data[i]=static_cast<std::uint8_t>(i);
    r.array.encoded={0xab,0xcd};r.array.trailing_bytes={0xa3};r.array.zero=-3;r.array.unknown=-7;
    return r;
}
std::vector<odh::SavedArchitectureArray> fixture(bool adv=false){
    return {grid(adv?11:1,adv?2:1),grid(adv?111:101,adv?2:1),grid(adv?12:2,adv?14:8),grid(adv?112:102,adv?14:8)};
}
}
int main(){try{
    auto input=fixture();auto a=odh::interpret_saved_architecture(input);
    require(a.width==3&&a.height==2&&!a.advanced,"rectangular extent");
    require(a.levels[0].floors==std::vector<std::uint16_t>({0,2,4,1,3,5}),"column-major to row-major");
    const auto& t=a.levels[0].walls[1];
    require(t.segments==16&&t.unknown==17,"raw diagonal/unknown byte");
    require(t.top_left_style==18&&t.top_right_style==19&&t.top_left_pattern==20&&t.top_right_pattern==21&&t.bottom_left_pattern==22&&t.bottom_right_pattern==23,"legacy fields preserved without clamping");
    require(!a.altitude&&!a.grass&&!a.target_grass&&!a.pools&&!a.water&&!a.levels[0].flags&&!a.levels[0].object_ids,"missing auxiliary remains absent");
    require(a.records[0].array.encoded==input[0].array.encoded&&a.records[0].array.trailing_bytes==input[0].array.trailing_bytes&&a.records[0].array.zero==-3&&a.records[0].array.unknown==-7,"raw record preservation");
    input[0].array.data[0]=99;require(a.levels[0].floors[0]==0&&a.records[0].array.data[0]==0,"owned storage");
    input=fixture(true);a=odh::interpret_saved_architecture(input);
    require(a.advanced&&a.levels[0].floors==std::vector<std::uint16_t>({256,1284,2312,770,1798,2826}),"advanced little endian full-width patterns");
    const auto& u=a.levels[1].walls[1];
    require(u.segments==28&&u.unknown==29&&u.top_left_style==7966&&u.top_right_style==8480&&u.top_left_pattern==8994&&u.top_right_pattern==9508&&u.bottom_left_pattern==10022&&u.bottom_right_pattern==10536,"advanced six words exact");
    for(auto id:{0,3,103,6,7,8,108,9,10})input.push_back(grid(static_cast<std::uint16_t>(id),id==0?4:(id==3||id==103?2:1)));
    input.push_back(grid(999,16));a=odh::interpret_saved_architecture(input);
    require((*a.altitude)[1]==std::array<std::uint8_t,4>{8,9,10,11},"altitude four raw bytes");
    require(*a.grass==std::vector<std::uint8_t>({0,2,4,1,3,5})&&*a.target_grass==*a.grass&&*a.pools==*a.grass&&*a.water==*a.grass,"auxiliary bytes not substituted");
    require(*a.levels[0].flags==*a.grass&&*a.levels[1].flags==*a.grass,"both flags");
    require(*a.levels[0].object_ids==a.levels[0].floors&&*a.levels[1].object_ids==a.levels[0].floors,"object ID words");
    require(a.records.back().id==999&&a.records.back().array.data==input.back().array.data,"unknown grid retained");
    for(std::size_t i=0;i<4;++i){auto x=fixture();x.erase(x.begin()+static_cast<std::ptrdiff_t>(i));bad([&]{odh::interpret_saved_architecture(x);});}
    for(std::size_t i=0;i<4;++i){auto x=fixture(true);x.erase(x.begin()+static_cast<std::ptrdiff_t>(i));bad([&]{odh::interpret_saved_architecture(x);});}
    auto x=fixture();x.push_back(x.front());bad([&]{odh::interpret_saved_architecture(x);});
    for(auto dim:{0,-1,65}){x=fixture();x[0].array.width=dim;bad([&]{odh::interpret_saved_architecture(x);});}
    x=fixture();x[1].array.height=3;bad([&]{odh::interpret_saved_architecture(x);});
    for(auto stride:{0,-1,2,17}){x=fixture();x[0].array.stride=stride;bad([&]{odh::interpret_saved_architecture(x);});}
    for(std::size_t i=0;i<input.size();++i){x=input;x[i].array.data.pop_back();bad([&]{odh::interpret_saved_architecture(x);});}
    for(std::size_t i=4;i<input.size()-1;++i){x=input;++x[i].array.stride;x[i].array.data.resize(static_cast<std::size_t>(6*x[i].array.stride));bad([&]{odh::interpret_saved_architecture(x);});}
    odh::SavedArchitectureLimits lim;lim.arrays=3;bad([&]{odh::interpret_saved_architecture(fixture(),lim);});
    lim={};lim.dimension=2;bad([&]{odh::interpret_saved_architecture(fixture(),lim);});
    lim={};lim.retained_bytes=0;bad([&]{odh::interpret_saved_architecture(fixture(),lim);});
    // Exact cumulative boundary accounts for all copies, metadata and typed grids.
    x=fixture();std::size_t retained=0;
    for(const auto& r:x)retained+=sizeof(odh::SavedArchitectureArray)+r.array.data.size()+r.array.encoded.size()+r.array.trailing_bytes.size();
    retained+=12*(sizeof(std::uint16_t)+sizeof(odh::SavedWallTile));
    lim={};lim.retained_bytes=retained;
    require(odh::interpret_saved_architecture(x,lim).width==3,"exact retained budget accepted");
    --lim.retained_bytes;bad([&]{odh::interpret_saved_architecture(x,lim);});
    x=fixture();x[0].array.encoded.resize(2*1024*1024);bad([&]{odh::interpret_saved_architecture(x);});
    x=fixture();x[0].array.trailing_bytes.resize(2*1024*1024);bad([&]{odh::interpret_saved_architecture(x);});
    bad([&]{odh::interpret_saved_architecture({});});
    // Advanced grids have explicit precedence while legacy copies remain owned.
    x=fixture(true);auto legacy=fixture();x.insert(x.end(),legacy.begin(),legacy.end());
    a=odh::interpret_saved_architecture(x);require(a.advanced&&a.records.size()==8&&a.levels[0].floors[0]==256,"explicit advanced branch");
    x=fixture();x[0].array.width=2147483647;x[0].array.height=2147483647;
    lim={};lim.dimension=0xffffffffU;bad([&]{odh::interpret_saved_architecture(x,lim);});
    std::cout<<checks<<" saved architecture checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
