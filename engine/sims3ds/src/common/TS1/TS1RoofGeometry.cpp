// SPDX-License-Identifier: MPL-2.0
#include "common/TS1/TS1RoofGeometry.h"
#include <utility>
#include <cmath>

namespace FSO { namespace Common { namespace TS1 {
RoofClassification ClassifyRoof(const RoofAdmissionLevel& input) {
    const int n=input.Width;
    if(n<4 || n>128 || input.Eligible.size()!=size_t(n)*n ||
       (!input.Blocked.empty() && input.Blocked.size()!=input.Eligible.size()))
        throw std::invalid_argument("Invalid original roof admission grid");
    RoofClassification result;
    result.Width=n;
    result.Flags.resize(size_t(n)*n);
    result.Heights.resize(size_t(n)*n);
    auto at=[n](int x,int y) { return size_t(y)*n+x; };
    for(int y=0;y<n;++y) for(int x=0;x<n;++x) {
        const auto i=at(x,y);
        if(input.Eligible[i] && (x==0 || y==0 || x==n-1 || y==n-1))
            throw std::invalid_argument("Original roof admission requires an inactive border");
        result.Flags[i]=(input.Eligible[i]?1:0) |
            ((!input.Blocked.empty() && input.Blocked[i])?8:0);
    }
    auto& flags=result.Flags;
    auto& heights=result.Heights;
    for(int y=1;y<n-1;++y) for(int x=1;x<n-1;++x) {
        auto& f=flags[at(x,y)];
        if(f&9) continue;
        bool neighbor=false;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            if(!dx && !dy) continue;
            const auto other=flags[at(x+dx,y+dy)];
            neighbor=neighbor || ((other&1) && !(other&4));
        }
        if(neighbor) f|=5;
    }
    static constexpr int bx[8]={-1,0,1,0,-1,1,1,-1};
    static constexpr int by[8]={0,1,0,-1,-1,-1,1,1};
    static constexpr uint8_t equalMasks[8]={0x2c,0x46,0x83,0x19,0x6e,0xc7,0x9b,0x3d};
    static constexpr int cx[8]={0,1,-1,0,-1,1,1,-1};
    static constexpr int cy[8]={1,0,0,-1,-1,-1,1,1};
    static constexpr uint8_t keepMasks[8]={0xc,0xa,0x5,0x3,0x7,0xb,0xe,0xd};
    for(int attempt=0;attempt<9;++attempt) {
        ++result.Attempts;
        for(int y=1;y<n-1;++y) for(int x=1;x<n-1;++x) {
            if(!(flags[at(x,y)]&1)) continue;
            int radius=1;
            for(;;++radius) {
                int xx=x-radius,yy=y-radius;
                bool failed=false;
                static constexpr int rx[4]={1,0,-1,0},ry[4]={0,1,0,-1};
                for(int side=0;side<4 && !failed;++side)
                    for(int step=0;step<2*radius;++step) {
                        if(xx<0 || yy<0 || xx>=n || yy>=n || !(flags[at(xx,yy)]&1)) {
                            failed=true;break;
                        }
                        xx+=rx[side]; yy+=ry[side];
                    }
                if(failed) { heights[at(x,y)]=uint8_t(radius-1);break; }
            }
        }
        bool rejected=false;
        for(int y=1;y<n-1 && !rejected;++y) for(int x=1;x<n-1;++x) {
            auto& f=flags[at(x,y)];
            if(!(f&1)) continue;
            const auto h=heights[at(x,y)];
            uint8_t equal=0;bool retain=false;
            for(int i=0;i<8;++i) {
                const auto other=at(x+bx[i],y+by[i]);
                if(!(flags[other]&1)) continue;
                if(heights[other]==h) equal|=uint8_t(1<<i);
                else if(heights[other]>h) { retain=true;break; }
            }
            if(equal && !retain) for(auto mask:equalMasks)
                if((equal&mask)==mask) { retain=true;break; }
            if(retain) f|=2;
            else if(f&4) { f&=uint8_t(~1);rejected=true;break; }
            else f&=uint8_t(~2);
        }
        if(rejected) continue;
        for(int y=1;y<n-1;++y) for(int x=1;x<n-1;++x) {
            auto& f=flags[at(x,y)];
            if(!(f&1)) continue;
            const auto h=heights[at(x,y)];
            uint8_t mask=(f&2)?15:0;
            for(int i=0;i<((f&2)?8:4);++i) {
                const auto other=at(x+cx[i],y+cy[i]);
                if(f&2) {
                    if(!(flags[other]&1) || heights[other]<h) mask&=keepMasks[i];
                } else if((flags[other]&1) && !(flags[other]&2) && heights[other]==h)
                    mask|=uint8_t(1<<i);
            }
            f=uint8_t((f&15)|(mask<<4));
        }
        result.Completed=true;
        break;
    }
    for(int y=1;y<n-1;++y) for(int x=1;x<n-1;++x) {
        const auto i=at(x,y);
        if(flags[i]&1) result.Cells.push_back({x,y,flags[i],heights[i],RoofPrototypeForFlags(flags[i])});
    }
    return result;
}
std::vector<RoofClassification> ClassifyRoofLevels(const std::vector<RoofAdmissionLevel>& input) {
    std::vector<RoofClassification> result(input.size());
    int index[3]={-1,-1,-1};
    for(size_t i=0;i<input.size();++i) {
        const int story=input[i].Story;
        if(story<1 || story>2 || index[story]>=0)
            throw std::invalid_argument("Original roof stories must be unique1 or2");
        index[story]=int(i);
    }
    for(int story=2;story>=1;--story) {
        const int current=index[story];
        if(current<0) continue;
        auto effective=input[size_t(current)];
        if(story==1 && index[2]>=0) {
            const auto& upper=result[size_t(index[2])];
            if(effective.Width!=upper.Width || effective.Eligible.size()!=upper.Flags.size())
                throw std::invalid_argument("Original roof story domains must agree");
            for(size_t i=0;i<effective.Eligible.size();++i)
                if((upper.Flags[i]&1) && !(upper.Flags[i]&4)) effective.Eligible[i]=0;
        }
        result[size_t(current)]=ClassifyRoof(effective);
    }
    return result;
}
namespace {
struct PrototypeRecord {float X,Y,Z,U,V;};
static constexpr PrototypeRecord Kind3[][3] = {
    {{0.0f,0.0f,0.0f,1.0f,1.0f},{1.5f,1.5f,1.5f,0.0f,0.0f},{1.5f,1.5f,0.0f,1.0f,0.0f}},
    {{0.0f,0.0f,0.0f,1.0f,1.0f},{0.0f,0.0f,1.5f,0.0f,1.0f},{1.5f,1.5f,1.5f,0.0f,0.0f}},
};
static constexpr PrototypeRecord Kind4[][3] = {
    {{0.0f,0.0f,0.0f,0.0f,1.0f},{0.0f,1.5f,1.5f,0.0f,0.0f},{1.5f,0.0f,0.0f,1.0f,1.0f}},
    {{1.5f,0.0f,0.0f,0.0f,1.0f},{0.0f,1.5f,1.5f,1.0f,0.0f},{1.5f,0.0f,1.5f,1.0f,1.0f}},
};
static constexpr PrototypeRecord Kind9[][3] = {
    {{0.0f,1.5f,0.0f,0.0f,1.0f},{0.0f,0.0f,1.5f,1.0f,0.5f},{1.5f,1.5f,1.5f,0.0f,0.0f}},
    {{0.0f,1.5f,0.0f,0.0f,0.0f},{1.5f,1.5f,1.5f,0.0f,1.0f},{1.5f,0.0f,0.0f,1.0f,0.5f}},
};
static constexpr PrototypeRecord Kind13[][3] = {
    {{0.0f,1.5f,0.0f,0.0f,0.0f},{0.0f,1.5f,1.5f,1.0f,0.0f},{1.5f,0.0f,0.0f,0.0f,1.0f}},
    {{0.0f,1.5f,1.5f,0.0f,0.0f},{1.5f,1.5f,1.5f,1.0f,0.0f},{1.5f,0.0f,0.0f,1.0f,1.0f}},
};
static constexpr PrototypeRecord Kind256[][3] = {
    {{0.0f,0.0f,0.0f,1.0f,1.0f},{0.0f,0.0f,1.5f,1.0f,0.0f},{0.75f,0.75f,0.75f,0.5f,0.5f}},
    {{0.0f,0.0f,0.0f,0.0f,1.0f},{0.75f,0.75f,0.75f,0.5f,0.5f},{1.5f,0.0f,0.0f,1.0f,1.0f}},
    {{1.5f,0.0f,0.0f,0.0f,1.0f},{0.75f,0.75f,0.75f,0.5f,0.5f},{1.5f,0.0f,1.5f,1.0f,1.0f}},
    {{0.0f,0.0f,1.5f,1.0f,1.0f},{1.5f,0.0f,1.5f,0.0f,1.0f},{0.75f,0.75f,0.75f,0.5f,0.5f}},
};
static constexpr PrototypeRecord Kind265[][3] = {
    {{0.0f,0.0f,0.0f,0.0f,1.0f},{0.0f,0.75f,0.75f,0.0f,0.5f},{1.5f,0.75f,0.75f,1.0f,0.5f}},
    {{0.0f,0.0f,0.0f,0.0f,1.0f},{1.5f,0.75f,0.75f,1.0f,0.5f},{1.5f,0.0f,0.0f,1.0f,1.0f}},
    {{0.0f,0.75f,0.75f,1.0f,0.5f},{0.0f,0.0f,1.5f,1.0f,1.0f},{1.5f,0.75f,0.75f,0.0f,0.5f}},
    {{0.0f,0.0f,1.5f,1.0f,1.0f},{1.5f,0.0f,1.5f,0.0f,1.0f},{1.5f,0.75f,0.75f,0.0f,0.5f}},
};
static constexpr PrototypeRecord Kind264[][3] = {
    {{0.0f,0.0f,0.0f,0.0f,1.0f},{0.0f,0.75f,0.75f,0.0f,0.5f},{0.75f,0.75f,0.75f,0.5f,0.5f}},
    {{0.0f,0.0f,0.0f,0.0f,1.0f},{0.75f,0.75f,0.75f,0.5f,0.5f},{1.5f,0.0f,0.0f,1.0f,1.0f}},
    {{1.5f,0.0f,0.0f,0.0f,1.0f},{0.75f,0.75f,0.75f,0.5f,0.5f},{1.5f,0.0f,1.5f,1.0f,1.0f}},
    {{0.0f,0.0f,1.5f,1.0f,1.0f},{0.75f,0.75f,0.75f,0.5f,0.5f},{0.0f,0.75f,0.75f,1.0f,0.5f}},
    {{0.0f,0.0f,1.5f,1.0f,1.0f},{1.5f,0.0f,1.5f,0.0f,1.0f},{0.75f,0.75f,0.75f,0.5f,0.5f}},
};
static constexpr PrototypeRecord Kind266[][3] = {
    {{0.0f,0.0f,0.0f,0.0f,1.0f},{0.0f,0.75f,0.75f,0.0f,0.5f},{1.5f,0.0f,0.0f,1.0f,1.0f}},
    {{1.5f,0.0f,0.0f,1.0f,1.0f},{0.0f,0.75f,0.75f,0.0f,0.5f},{0.75f,0.75f,0.75f,0.5f,0.5f}},
    {{1.5f,0.0f,0.0f,0.0f,1.0f},{0.75f,0.75f,0.75f,0.5f,0.5f},{1.5f,0.0f,1.5f,1.0f,1.0f}},
    {{0.75f,0.75f,0.75f,0.5f,0.5f},{0.0f,0.75f,0.75f,1.0f,0.5f},{0.0f,0.0f,1.5f,1.0f,1.0f}},
    {{0.75f,0.75f,0.75f,0.5f,0.5f},{0.0f,0.0f,1.5f,0.0f,1.0f},{0.75f,0.75f,1.5f,0.0f,0.5f}},
    {{0.75f,0.75f,0.75f,0.5f,0.5f},{0.75f,0.75f,1.5f,1.0f,0.5f},{1.5f,0.0f,1.5f,1.0f,1.0f}},
};
static constexpr PrototypeRecord Kind270[][3] = {
    {{0.0f,0.0f,0.0f,1.0f,1.0f},{0.75f,0.75f,0.75f,0.5f,0.5f},{0.75f,0.75f,0.0f,1.0f,0.5f}},
    {{0.0f,0.0f,0.0f,0.0f,1.0f},{0.0f,0.75f,0.75f,0.0f,0.5f},{0.75f,0.75f,0.75f,0.5f,0.5f}},
    {{0.0f,0.75f,0.75f,1.0f,0.5f},{0.0f,0.0f,1.5f,1.0f,1.0f},{0.75f,0.75f,0.75f,0.5f,0.5f}},
    {{0.0f,0.0f,1.5f,0.0f,1.0f},{0.75f,0.75f,1.5f,0.0f,0.5f},{0.75f,0.75f,0.75f,0.5f,0.5f}},
    {{0.75f,0.75f,0.0f,0.0f,0.5f},{0.75f,0.75f,1.5f,1.0f,0.5f},{1.5f,0.0f,1.5f,1.0f,1.0f}},
    {{0.75f,0.75f,0.0f,0.0f,0.5f},{1.5f,0.0f,1.5f,1.0f,1.0f},{1.5f,0.0f,0.0f,0.0f,1.0f}},
};
static constexpr PrototypeRecord Kind271[][3] = {
    {{0.0f,0.0f,0.0f,0.0f,1.0f},{0.0f,0.75f,0.75f,0.0f,0.5f},{0.75f,0.75f,0.75f,0.5f,0.5f}},
    {{0.0f,0.75f,0.75f,1.0f,0.5f},{0.0f,0.0f,1.5f,1.0f,1.0f},{0.75f,0.75f,0.75f,0.5f,0.5f}},
    {{0.75f,0.75f,0.75f,0.5f,0.5f},{0.0f,0.0f,1.5f,0.0f,1.0f},{0.75f,0.75f,1.5f,0.0f,0.5f}},
    {{0.75f,0.75f,0.75f,0.5f,0.5f},{0.75f,0.75f,1.5f,1.0f,0.5f},{1.5f,0.0f,1.5f,1.0f,1.0f}},
    {{0.75f,0.75f,0.75f,0.5f,0.5f},{1.5f,0.0f,1.5f,0.0f,1.0f},{1.5f,0.75f,0.75f,0.0f,0.5f}},
    {{0.75f,0.75f,0.75f,0.5f,0.5f},{1.5f,0.75f,0.75f,1.0f,0.5f},{1.5f,0.0f,0.0f,1.0f,1.0f}},
    {{0.75f,0.75f,0.0f,0.0f,0.5f},{0.75f,0.75f,0.75f,0.5f,0.5f},{1.5f,0.0f,0.0f,0.0f,1.0f}},
    {{0.0f,0.0f,0.0f,1.0f,1.0f},{0.75f,0.75f,0.75f,0.5f,0.5f},{0.75f,0.75f,0.0f,1.0f,0.5f}},
};
}
RoofPrototype CreateRoofPrototype(uint16_t canonicalKind,float pitch) {
    const PrototypeRecord (*records)[3]=nullptr;
    size_t count=0;
    switch(canonicalKind) {
        case 3: records=Kind3;count=sizeof(Kind3)/sizeof(Kind3[0]);break;
        case 4: records=Kind4;count=sizeof(Kind4)/sizeof(Kind4[0]);break;
        case 9: records=Kind9;count=sizeof(Kind9)/sizeof(Kind9[0]);break;
        case 13: records=Kind13;count=sizeof(Kind13)/sizeof(Kind13[0]);break;
        case 256: records=Kind256;count=sizeof(Kind256)/sizeof(Kind256[0]);break;
        case 265: records=Kind265;count=sizeof(Kind265)/sizeof(Kind265[0]);break;
        case 264: records=Kind264;count=sizeof(Kind264)/sizeof(Kind264[0]);break;
        case 266: records=Kind266;count=sizeof(Kind266)/sizeof(Kind266[0]);break;
        case 270: records=Kind270;count=sizeof(Kind270)/sizeof(Kind270[0]);break;
        case 271: records=Kind271;count=sizeof(Kind271)/sizeof(Kind271[0]);break;
        default:
            // Source selectors preserve unsigned keys.00570888 defaults to13;
            //0056f8ab defaults to15 after subtracting the family bit0x100.
            if(canonicalKind<=255) {records=Kind13;count=2;}
            else {records=Kind271;count=8;}
            break;
    }
    RoofPrototype result(count);
    for(size_t t=0;t<count;++t) {
        auto& triangle=result[t];
        for(int v=0;v<3;++v) {
            const auto& r=records[t][v];
            triangle.Vertices[size_t(v)]={{{r.X,r.Y*pitch,r.Z}},{{r.U,r.V}}};
        }
        const auto& a=triangle.Vertices[0].Position;
        const auto& b=triangle.Vertices[1].Position;
        const auto& c=triangle.Vertices[2].Position;
        const float ux=c[0]-b[0],uy=c[1]-b[1],uz=c[2]-b[2];
        const float vx=a[0]-b[0],vy=a[1]-b[1],vz=a[2]-b[2];
        auto& normal=triangle.Normal;
        normal={{uy*vz-uz*vy,uz*vx-ux*vz,ux*vy-uy*vx}};
        const float length=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
        if(length>0) for(auto& component:normal) component/=length;
    }
    return result;
}
RoofGroundSelection SelectRoofGround(const RoofHalfCell& cell,
    const std::vector<uint8_t>& heightQuads,int sourceWidth) {
    if(sourceWidth<=0 || heightQuads.size()!=size_t(sourceWidth)*sourceWidth*4) return {};
    int x=cell.X/2,y=cell.Y/2;
    auto valid=[sourceWidth](int xx,int yy){return xx>=0 && yy>=0 && xx<sourceWidth && yy<sourceWidth;};
    if(!valid(x,y)) return {};
    const size_t base=(size_t(y)*sourceWidth+x)*4;
    uint8_t height=heightQuads[base+1];
    const bool flat=heightQuads[base]==height && heightQuads[base+2]==height && heightQuads[base+3]==height;
    if(!flat) {
        if(!(cell.Flags&2)) return {true,false,height};
        switch(cell.Flags>>4) {
            case 1:++x;++y;break;
            case 2:--x;++y;break;
            case 4:++x;--y;break;
            case 8:--x;--y;break;
            default:break;
        }
        if(!valid(x,y)) return {};
        height=heightQuads[(size_t(y)*sourceWidth+x)*4+1];
    }
    return {true,true,height};
}
float RoofQuarterTurnAngle(int rotation) {
    // FILD, FMUL piF(0x40490fdb), FMUL .5, one final float store.
    return float(double(rotation)*double(3.1415927410125732421875f)*.5);
}
RoofPrototype PlaceRoofPrototype(const RoofPrototype& prototype,const RoofHalfCell& cell,
    uint8_t groundHeight,int story,int cameraRotation,float pitch,
    const std::array<float,3>& storyUnit) {
    const float angle=RoofQuarterTurnAngle(cell.Prototype.Rotation);
    const float c=float(std::cos(double(angle))),s=float(std::sin(double(angle)));
    const float storyAngle=RoofQuarterTurnAngle(-cameraRotation);
    const float cc=float(std::cos(double(storyAngle))),ss=float(std::sin(double(storyAngle)));
    const float sx=float(story)*storyUnit[0],sy=float(story)*storyUnit[1],sz=float(story)*storyUnit[2];
    const float dx=sx*cc+sz*ss, dz=-sx*ss+sz*cc;
    //00454f69 cached float bits0x3e5105ec. Both products remain extended
    // until the caller stores the combined ground/band translation as float.
    constexpr float groundScale=.204124152660369873046875f;
    const float band=1.5f*pitch;
    const float dy=float(static_cast<long double>(cell.Height)*band+
        static_cast<long double>(groundHeight)*groundScale*3.0L)+sy;
    RoofPrototype result=prototype;
    for(auto& triangle:result) {
        const auto normal=triangle.Normal;
        triangle.Normal={{normal[0]*c+normal[2]*s,normal[1],-normal[0]*s+normal[2]*c}};
        for(auto& vertex:triangle.Vertices) {
            const auto p=vertex.Position;
            const float x=p[0]-.75f,z=p[2]-.75f;
            vertex.Position={{x*c+z*s+.75f+float(cell.Y)*1.5f+dx,
                p[1]+dy,-x*s+z*c+.75f+float(cell.X)*1.5f+dz}};
        }
    }
    return result;
}
}}}
