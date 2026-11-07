#include "odh/ts1_curve.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F&& f){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error("invalid curve accepted");}
}
int main(){try{
    const auto curve=odh::parse_ts1_curve("(10;100);(0;0);(10;200);(20;300)");
    require(curve.point(-1)==0&&curve.point(5)==50&&curve.point(10)==200&&curve.point(15)==250&&curve.point(30)==300,
        "stable duplicate-X ordering, reverse lookup, linear interpolation and clamping");
    require(curve.point(-std::numeric_limits<float>::infinity())==0&&
        curve.point(std::numeric_limits<float>::infinity())==300,"source infinities clamp normally");
    rejects([&]{curve.point(std::numeric_limits<float>::quiet_NaN());});
    require(odh::parse_ts1_curve("[+1;2;unused]").point(99)==2,"source strips outer characters and ignores extra semicolon fields");
    require(odh::parse_ts1_curve("(\t+1;\t-2)").point(1)==-2,"source Int32 parser trims non-space whitespace inside tokens");
    require(odh::parse_ts1_curve("(-2147483648;0) (2147483647;1)").point(0)==-2147483648.0f,
        "source unchecked Int32 X difference precedes float division");
    for(const auto* input:{"", "(0;0) ", " (0;0)", "(0;0)  (1;1)", "(1)", "(x;1)", "(2147483648;0)", "(+-1;0)","(0;0)(1;1)"})
        rejects([&]{odh::parse_ts1_curve(input);});
    rejects([&]{odh::parse_ts1_curve("(0;0) (1;1)",{65536,1});});
    rejects([&]{odh::parse_ts1_curve("(0;0)",{4,4096});});
    rejects([&]{odh::Ts1Curve(std::vector<odh::Ts1CurvePoint>{});});
    odh::StringTable table{odh::StringTableState::decoded,-3,{},0};
    for(unsigned i=0;i<9;++i)table.entries.push_back({1,"(0;0);(100;100)",""});
    table.entries.push_back({2,"non-default-language-is-not-a-curve",""});
    const auto curves=odh::read_ts1_interaction_curves(table);
    require(curves[8].point(25)==25,"nine default-language motive curves are independently loaded");
    table.entries.erase(table.entries.begin());rejects([&]{odh::read_ts1_interaction_curves(table);});
    table.state=odh::StringTableState::header_only;rejects([&]{odh::read_ts1_interaction_curves(table);});
    std::cout<<"TS1 curve source semantics passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
