// SPDX-License-Identifier: MPL-2.0
// TS1Curve / WorldGlobalProvider, pinned FreeSO7e76c1960252ddd033d8937d5edb47d326f5b69c.
#include "odh/ts1_curve.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
namespace odh {
namespace {
float fsub(float a,float b){volatile float value=a-b;return value;}
float fmul(float a,float b){volatile float value=a*b;return value;}
float fadd(float a,float b){volatile float value=a+b;return value;}
float fdiv(float a,float b){volatile float value=a/b;return value;}
std::int32_t integer(std::string_view text){
    const auto whitespace=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'||c=='\v';};
    while(!text.empty()&&whitespace(text.front()))text.remove_prefix(1);
    while(!text.empty()&&whitespace(text.back()))text.remove_suffix(1);
    if(!text.empty()&&text.front()=='+'){text.remove_prefix(1);
        if(!text.empty()&&(text.front()=='+'||text.front()=='-'))throw FormatError("TS1 curve integer sign invalid");}
    if(text.empty())throw FormatError("TS1 curve integer absent");
    std::int32_t result=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw FormatError("TS1 curve integer invalid");
    return result;
}
}
Ts1Curve::Ts1Curve(std::vector<Ts1CurvePoint> points):points_(std::move(points)){
    if(points_.empty()||points_.size()>4096)throw FormatError("TS1 curve point count outside bounded source profile");
    std::stable_sort(points_.begin(),points_.end(),[](const auto& a,const auto& b){return a.x<b.x;});
}
float Ts1Curve::point(float input) const {
    if(std::isnan(input))throw FormatError("TS1 curve cannot index a NaN input");
    if(input<static_cast<float>(points_.front().x))return static_cast<float>(points_.front().y);
    auto index=points_.size()-1;
    while(input<static_cast<float>(points_[index].x))--index;
    if(index+1==points_.size())return static_cast<float>(points_[index].y);
    const auto& start=points_[index];const auto& end=points_[index+1];
    // C# subtracts the two Int32 coordinates before converting to float.
    const auto difference=static_cast<std::uint32_t>(end.x)-static_cast<std::uint32_t>(start.x);
    const auto signed_difference=difference<=0x7fffffffU?static_cast<std::int64_t>(difference):
        static_cast<std::int64_t>(difference)-0x100000000LL;
    const auto fraction=fdiv(fsub(input,static_cast<float>(start.x)),static_cast<float>(signed_difference));
    return fadd(fmul(fsub(1.0f,fraction),static_cast<float>(start.y)),fmul(fraction,static_cast<float>(end.y)));
}
Ts1Curve parse_ts1_curve(std::string_view input,const Ts1CurveLimits& limits){
    if(input.size()>limits.text_bytes||!limits.points||limits.points>4096)throw FormatError("TS1 curve input budget exceeded");
    std::string text(input);
    for(std::size_t at=0;(at=text.find(");(",at))!=std::string::npos;at+=3)text.replace(at,3,") (");
    std::vector<Ts1CurvePoint> points;
    std::size_t begin=0;
    for(;;){
        const auto end=text.find(' ',begin);
        const auto token=std::string_view(text).substr(begin,end==std::string::npos?end:end-begin);
        if(token.size()<2||points.size()>=limits.points)throw FormatError("TS1 curve token or point budget invalid");
        // Match Substring(1,Length-2) and the first two semicolon fields;
        // the reference does not validate delimiters or consume extra fields.
        const auto clean=token.substr(1,token.size()-2);const auto separator=clean.find(';');
        if(separator==std::string_view::npos)throw FormatError("TS1 curve point separator absent");
        const auto rest=clean.substr(separator+1);const auto next=rest.find(';');
        points.push_back({integer(clean.substr(0,separator)),integer(rest.substr(0,next))});
        if(end==std::string::npos)break;
        begin=end+1;
    }
    return Ts1Curve(std::move(points));
}
Ts1InteractionCurves read_ts1_interaction_curves(const StringTable& table,const Ts1CurveLimits& limits){
    if(table.state!=StringTableState::decoded)throw FormatError("TS1 interaction curve STR is not decoded");
    std::vector<Ts1Curve> curves;std::size_t bytes=0;
    for(const auto& entry:table.entries){
        if(entry.language>1)continue;
        if(curves.size()>=256||entry.value.size()>limits.text_bytes-bytes)throw FormatError("TS1 interaction curve STR budget exceeded");
        bytes+=entry.value.size();curves.push_back(parse_ts1_curve(entry.value,limits));
    }
    if(curves.size()<9)throw FormatError("TS1 interaction curve STR needs nine motives");
    return {std::move(curves[0]),std::move(curves[1]),std::move(curves[2]),std::move(curves[3]),std::move(curves[4]),
        std::move(curves[5]),std::move(curves[6]),std::move(curves[7]),std::move(curves[8])};
}
}
