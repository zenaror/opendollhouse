// SPDX-License-Identifier: MPL-2.0
#include "common/TS1/TS1LotPictures.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>

namespace FSO { namespace Common { namespace TS1 {
uint16_t PackLotPictureRGB(const std::array<uint8_t,3>& rgb,LotPictureFormat format) {
    if(format==LotPictureFormat::RGB555)
        return uint16_t((uint16_t(rgb[0]>>3)<<10)|(uint16_t(rgb[1]>>3)<<5)|(rgb[2]>>3));
    return uint16_t((uint16_t(rgb[0]>>3)<<11)|(uint16_t(rgb[1]>>2)<<5)|(rgb[2]>>3));
}
std::array<uint8_t,3> ExpandLotPictureRGB(uint16_t word,LotPictureFormat format) {
    if(format==LotPictureFormat::RGB555)
        return {{uint8_t(((word>>10)&31)<<3),uint8_t(((word>>5)&31)<<3),uint8_t((word&31)<<3)}};
    return {{uint8_t((word>>11)<<3),uint8_t(((word>>5)&63)<<2),uint8_t((word&31)<<3)}};
}
std::array<uint8_t,3> LotPictureBMPColor(uint16_t word,LotPictureFormat format) {
    auto rgb=ExpandLotPictureRGB(word,format);
    if(format==LotPictureFormat::RGB565) rgb[1]&=248;
    return rgb;
}
namespace {
float F32(long double value) {volatile float stored=float(value);return stored;}
struct Tap {int Index;float Weight;};
using Axis=std::vector<std::vector<Tap>>;
int Reflect(int index,int size) {
    if(size==1) return 0; // Ordinary source captures exceed1; keep degenerate inputs finite.
    while(index<0 || index>=size) {
        if(index<0) index=-index;
        if(index>=size) index=2*size-index-2;
    }
    return index;
}
Axis Plan(int source,int destination) {
    const float factor=F32(static_cast<long double>(destination)/source);
    const float support=factor>=1 ? 1.0f : F32(1.0L/factor);
    const float inverse=F32(1.0L/support);
    Axis result(static_cast<size_t>(destination));
    for(int j=0;j<destination;++j) {
        const float center=F32(static_cast<long double>(j)/factor);
        const int first=int(std::ceil(static_cast<long double>(center)-support));
        const int last=int(std::floor(static_cast<long double>(center)+support));
        long double sum=0;
        auto& taps=result[size_t(j)];taps.reserve(size_t(last-first+1));
        for(int i=first;i<=last;++i) {
            const long double d=std::fabs((static_cast<long double>(center)-i)*inverse);
            const long double weight=d>=1 ? 0.0L : (((d+d)-3)*d*d+1)*inverse;
            // Original x87 keeps the unrounded raw total in a register, but
            // stores each raw tap as float before its final normalization.
            sum+=weight;
            taps.push_back({Reflect(i,source),F32(weight)});
        }
        const long double normalize=sum>1e-12L ? 1.0L/sum : sum;
        for(auto& tap:taps) tap.Weight=F32(static_cast<long double>(tap.Weight)*normalize);
    }
    return result;
}
uint16_t Filter(const LotPictureSurface& source,const std::vector<Tap>& taps,int fixed,bool horizontal) {
    std::array<float,3> sums{{0,0,0}};float mass=0;
    const auto keyColor=ExpandLotPictureRGB(source.Key,source.Format);
    for(const auto& tap:taps) {
        const size_t at=horizontal ? size_t(fixed)*source.Width+tap.Index : size_t(tap.Index)*source.Width+fixed;
        const auto rgb=ExpandLotPictureRGB(source.Pixels[at],source.Format);
        if(source.Keyed && rgb==keyColor) continue;
        for(size_t c=0;c<3;++c)sums[c]=F32(static_cast<long double>(rgb[c])*tap.Weight+sums[c]);
        mass=F32(static_cast<long double>(mass)+tap.Weight);
    }
    if(mass<.46875f) return source.Key;
    const long double inverse=1.0L/mass;
    std::array<uint8_t,3> rgb;
    for(size_t c=0;c<3;++c)rgb[c]=uint8_t(std::clamp(int(inverse*sums[c]+.5L),0,255));
    return PackLotPictureRGB(rgb,source.Format);
}
}
LotPictureSurface ResizeLotPicture(const LotPictureSurface& source,float scaleX,float scaleY) {
    if(!source.Valid() || !(scaleX>=1e-6f) || !(scaleY>=1e-6f) ||
       !std::isfinite(scaleX) || !std::isfinite(scaleY)) return {};
    const auto scaledWidth=std::ceil(static_cast<long double>(source.Width)*scaleX);
    const auto scaledHeight=std::ceil(static_cast<long double>(source.Height)*scaleY);
    if(scaledWidth<1 || scaledHeight<1 || scaledWidth>std::numeric_limits<int>::max() || scaledHeight>std::numeric_limits<int>::max())
        throw std::invalid_argument("Invalid lot-picture resize extent");
    const int width=int(scaledWidth),height=int(scaledHeight);
    LotPictureSurface middle{width,source.Height,source.Format,source.Key,source.Keyed,{}};
    middle.Pixels.resize(size_t(width)*source.Height);
    const auto horizontal=Plan(source.Width,width);
    for(int y=0;y<source.Height;++y)for(int x=0;x<width;++x)
        middle.Pixels[size_t(y)*width+x]=Filter(source,horizontal[size_t(x)],y,true);
    LotPictureSurface result{width,height,source.Format,source.Key,source.Keyed,{}};
    result.Pixels.resize(size_t(width)*height);
    const auto vertical=Plan(source.Height,height);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        result.Pixels[size_t(y)*width+x]=Filter(middle,vertical[size_t(y)],x,false);
    return result;
}
LotPictureTrim TrimLotPicture(const LotPictureSurface& source) {
    if(!source.Valid()) throw std::invalid_argument("Invalid lot-picture trim surface");
    LotPictureTrim trim;
    auto rowKey=[&](int y){for(int x=0;x<source.Width;++x)if(source.Pixels[size_t(y)*source.Width+x]!=source.Key)return false;return true;};
    while(trim.Top<source.Height && rowKey(trim.Top))++trim.Top;
    while(trim.Bottom<source.Height-trim.Top && rowKey(source.Height-1-trim.Bottom))++trim.Bottom;
    trim.Left=trim.Right=source.Width;
    for(int y=trim.Top;y<source.Height-trim.Bottom;++y) {
        int left=0;while(left<source.Width && source.Pixels[size_t(y)*source.Width+left]==source.Key)++left;
        trim.Left=std::min(trim.Left,left);
        int x=source.Width-1;while(x>=trim.Left && source.Pixels[size_t(y)*source.Width+x]==source.Key)--x;
        trim.Right=std::min(trim.Right,source.Width-x-1);
    }
    return trim;
}
LotPicturePlan PlanLotPictures(const std::vector<uint8_t>& heights,int width,int viewW,int viewH,int zoom,int camera,int storyStep) {
    if(heights.size()!=64*64*4 || width<3 || width>64 || zoom<1 || zoom>3 || camera<0 || camera>3)
        throw std::invalid_argument("Invalid source lot-picture geometry");
    std::array<int,128> maximum{},minimum;minimum.fill(64);
    const int ox=(camera==1||camera==2)?64-width:0;
    const int oy=(camera==2||camera==3)?64-width:0;
    auto canonical=[&](int x,int y) {
        switch(camera) {case 1:return std::array<int,2>{{y,63-x}};
            case 2:return std::array<int,2>{{63-x,63-y}};
            case 3:return std::array<int,2>{{63-y,x}};
            default:return std::array<int,2>{{x,y}};}
    };
    for(int y=0;y<width;++y)for(int x=0;x<width;++x) {
        const auto p=canonical(x+ox,y+oy);const size_t at=(size_t(p[1])*64+p[0])*4;
        for(int c=0;c<4;++c) {maximum[size_t(x+y+ox+oy)]=std::max(maximum[size_t(x+y+ox+oy)],int(heights[at+c]));
            minimum[size_t(x+y+ox+oy)]=std::min(minimum[size_t(x+y+ox+oy)],int(heights[at+c]));}
    }
    const int tileW=16<<zoom,halfW=tileW/2,tileH=8<<zoom,halfH=tileH/2;
    int a,b;if(viewW<(width-2)*tileW) {a=viewW-(width-2)*tileW/2-halfW;b=(width-2)*tileW/2+tileW;}
    else a=b=viewW/2;
    int top=0,bottom=std::numeric_limits<int>::max();
    for(int i=1;i<width-1;++i) {
        top=std::max(top,(maximum[size_t(2*i+ox+oy)]<<(zoom+1))-halfH*i);
        bottom=std::min(bottom,halfH*i-(minimum[size_t(2*(width-1-i)+ox+oy)]<<(zoom+1)));
    }
    int d=top+storyStep*2,c=viewH-tileH*2-(width-2)*tileH-bottom;
    const int dx=(ox-oy)*halfW,dy=(ox+oy)*halfH;
    a-=dx;b-=dx;c-=dy;d-=dy;
    return {((viewW-a)+b)>>(zoom+1),((viewH-c)+d)>>(zoom+1),b/(1<<(zoom-1)),d/(1<<(zoom-1)),bottom,a,b,c,d};
}
LotPictureSurface CropLotPicture(const LotPictureSurface& source,const LotPictureTrim& trim) {
    const int width=trim.WidthAfter(source.Width),height=trim.HeightAfter(source.Height);
    if(!source.Valid() || !trim.HasContent(source.Width,source.Height) || trim.Left<0 || trim.Top<0 || trim.Right<0 || trim.Bottom<0)
        throw std::invalid_argument("Lot-picture capture contains no nonkey rectangle");
    LotPictureSurface result{width,height,source.Format,source.Key,source.Keyed,{}};result.Pixels.reserve(size_t(width)*height);
    for(int y=0;y<height;++y) {auto first=source.Pixels.begin()+size_t(y+trim.Top)*source.Width+trim.Left;
        result.Pixels.insert(result.Pixels.end(),first,first+width);}
    return result;
}
std::vector<uint8_t> LotPictureRGB(const LotPictureSurface& source) {
    if(!source.Valid())throw std::invalid_argument("Invalid lot-picture color surface");
    std::vector<uint8_t> rgb;rgb.reserve(source.Pixels.size()*3);
    for(auto word:source.Pixels){const auto c=LotPictureBMPColor(word,source.Format);rgb.insert(rgb.end(),c.begin(),c.end());}
    return rgb;
}
LotPictureMetadata PlaceLotPictures(const LotPictureMetadata& uncropped,const LotPictureTrim& open,const LotPictureTrim& roofed) {
    auto result=uncropped;
    result.AnchorX-=open.Left;result.AnchorY-=open.Top;
    result.RoofXOff=roofed.Left-open.Left;result.RoofYOff=roofed.Top-open.Top;
    return result;
}
}}}
