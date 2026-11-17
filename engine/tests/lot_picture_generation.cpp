// SPDX-License-Identifier: MPL-2.0
#include "common/TS1/TS1LotPictures.h"
#include <cstdio>
#include <cstdlib>
using namespace FSO::Common::TS1;
static void Check(bool pass,const char* message) {
    if(!pass){std::fprintf(stderr,"Lot picture generation: %s\n",message);std::abort();}
}
static void ResizeGoldens() {
    // Independent transcription vectors from the original cubic/word kernel,
    // not native image captures or commercial image data.
    const std::array<std::array<uint8_t,3>,8> colors{{
        {{248,0,0}},{{0,252,0}},{{0,0,248}},{{248,252,248}},
        {{248,252,0}},{{0,252,248}},{{248,0,248}},{{0,0,0}}}};
    struct Case {int Size;float Scale;LotPictureFormat Format;bool Keyed;int Blank;std::array<uint16_t,4> Expected;};
    const Case cases[]={
        {8,.25f,LotPictureFormat::RGB565,true,0,{{15727,19407,25576,23726}}},
        {8,.25f,LotPictureFormat::RGB565,false,0,{{17616,31505,29515,27695}}},
        {16,.125f,LotPictureFormat::RGB565,true,7,{{63519,29837,63519,23629}}},
        {8,.25f,LotPictureFormat::RGB555,true,2,{{31775,9679,31775,11854}}}};
    for(const auto& c:cases) {
        LotPictureSurface source{c.Size,c.Size,c.Format,uint16_t(c.Format==LotPictureFormat::RGB565?0xf81f:0x7c1f),c.Keyed,{}};
        for(int y=0;y<c.Size;++y)for(int x=0;x<c.Size;++x) {
            const auto rgb=colors[size_t((x*3+y*5+(x*y)%3)%8)];
            source.Pixels.push_back(x<c.Blank?source.Key:PackLotPictureRGB(rgb,c.Format));
        }
        const auto output=ResizeLotPicture(source,c.Scale,c.Scale);
        Check(output.Width==2&&output.Height==2,"both original picture scales use their own resize extent");
        Check(output.Pixels==std::vector<uint16_t>(c.Expected.begin(),c.Expected.end()),"asymmetric reflected cubic/key/quantization source golden");
    }
    LotPictureSurface odd{3,3,LotPictureFormat::RGB565,0xf81f,false,std::vector<uint16_t>(9,0x07e0)};
    const auto ceil=ResizeLotPicture(odd,.5f,.5f);
    Check(ceil.Width==2&&ceil.Height==2,"destination dimensions use CEIL rather than floor");
    for(auto pixel:ceil.Pixels)Check(pixel==0x07e0,"constant source survives both quantized passes");
    Check(!ResizeLotPicture(odd,0,.5f).Valid(),"invalid scale does not enter reflection or allocation");
}
static void WordAndTrimCases() {
    Check(ExpandLotPictureRGB(0xffff,LotPictureFormat::RGB565)==std::array<uint8_t,3>{{248,252,248}},"source565 expansion has no bit replication");
    Check(LotPictureBMPColor(0xffff,LotPictureFormat::RGB565)==std::array<uint8_t,3>{{248,248,248}},"BMP converts565 to555 before zero expansion");
    Check(LotPictureBMPColor(0xf81f,LotPictureFormat::RGB565)==std::array<uint8_t,3>{{248,0,248}},"exact saved key color");
    LotPictureSurface source{7,6,LotPictureFormat::RGB565,0xf81f,true,std::vector<uint16_t>(42,0xf81f)};
    source.Pixels[2*7+3]=0x0000;source.Pixels[3*7+1]=0x07e0;source.Pixels[4*7+5]=0x001f;
    const auto trim=TrimLotPicture(source);
    Check(trim.Left==1&&trim.Top==2&&trim.Right==1&&trim.Bottom==1&&trim.WidthAfter(7)==5&&trim.HeightAfter(6)==3,"literal top/bottom then left/right crop");
    source.Pixels.assign(42,source.Key);const auto empty=TrimLotPicture(source);
    Check(empty.Left==7&&empty.Right==7&&empty.Top==6&&empty.Bottom==0&&empty.WidthAfter(7)==-7&&empty.HeightAfter(6)==0,
          "all-key literal margins remain observable without invented successful zero-size output");
    const LotPictureMetadata anchor{90,73,-19,0,0};
    const auto placed=PlaceLotPictures(anchor,{7,11,2,3},{3,16,1,2});
    Check(placed.Words()==std::array<int32_t,5>{{83,62,-19,-4,5}},"THMB anchors and signed roof-relative crop offsets");
}
static void ScenePlanCases() {
    // Flat authored H4=6, 48 active tiles in64 storage. Independently applying
    // source004aa1b9 at zoom2: A=-704,B=1536,C=-904,D=264.
    const std::vector<uint8_t> heights(64*64*4,6);
    const auto plan=PlanLotPictures(heights,48,800,600,2,0,116);
    Check(plan.MinX==-704&&plan.MaxX==1536&&plan.MinY==-904&&plan.MaxY==264,"source terrain profile/viewport bounds");
    Check(plan.RequestedWidth==380&&plan.RequestedHeight==221&&plan.OffsetX==768&&plan.OffsetY==132&&plan.BaseYOff==-32,
        "source requested dimensions precede half-size integer division");
    const auto render=PlanLotPictures(heights,48,1520,884,1,0,58);
    // This temporary viewport exceeds (48-2)*32=1472. The literal
    // source ELSE branch uses half the viewport,1520/2=760, rather than
    // the narrow-view B=(48-2)*16+32=768 used for the original request.
    Check(render.MinX==760&&render.MaxX==760&&render.MinY==132&&render.MaxY==132&&render.BaseYOff==-16,
        "temporary Far canvas bounds and metadata base offset recompute independently");
    const auto equalSpan=PlanLotPictures(heights,48,1472,884,1,0,58);
    const auto belowSpan=PlanLotPictures(heights,48,1471,884,1,0,58);
    Check(equalSpan.MinX==736&&equalSpan.MaxX==736&&belowSpan.MinX==719&&belowSpan.MaxX==768,
        "source horizontal span equality selects viewport-half branch");
    const auto half=PlanLotPictures(heights,48,1520,880,1,0,58);
    Check(half.MinY==128&&half.MaxY==132,"odd requested height halves before canvas allocation");
    LotPictureSurface surface{4,3,LotPictureFormat::RGB565,0xf81f,true,std::vector<uint16_t>(12,0xf81f)};
    surface.Pixels[5]=0xffff;surface.Pixels[6]=0x07e0;
    const auto crop=CropLotPicture(surface,TrimLotPicture(surface));
    Check(crop.Width==2&&crop.Height==1&&crop.Pixels==std::vector<uint16_t>({0xffff,0x07e0}),"crop preserves raw source WORDs before BMP conversion");
    Check(LotPictureRGB(crop)==std::vector<uint8_t>({248,248,248,0,248,0}),"cropped BMP channels use actual555 encoder quantization");
}
int main(){ResizeGoldens();WordAndTrimCases();ScenePlanCases();std::puts("Lot picture generation checks passed");}
