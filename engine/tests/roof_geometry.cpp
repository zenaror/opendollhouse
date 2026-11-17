// SPDX-License-Identifier: MPL-2.0
#include "common/TS1/TS1RoofGeometry.h"
#include "common/TS1/TS1RoofLighting.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <cstring>
#include <cmath>
using namespace FSO::Common::TS1;
static void Check(bool pass,const char* message) {
    if(!pass) { std::fprintf(stderr,"Original roof geometry: %s\n",message); std::abort(); }
}
struct Rect {int X0,Y0,X1,Y1;};
static RoofAdmissionLevel Admission(int width,std::initializer_list<Rect> rects) {
    RoofAdmissionLevel input;input.Width=width;input.Story=1;
    input.Eligible.resize(size_t(width)*width);
    input.Blocked.resize(input.Eligible.size());
    for(const auto& r:rects) for(int y=r.Y0;y<r.Y1;++y) for(int x=r.X0;x<r.X1;++x)
        input.Eligible[size_t(y)*width+x]=1;
    return input;
}
static uint64_t CellHash(const RoofClassification& grid) {
    uint64_t hash=14695981039346656037ull;
    for(const auto& cell:grid.Cells) {
        const int fields[]={cell.X,cell.Y,cell.Flags,cell.Height,cell.Prototype.Kind,cell.Prototype.Rotation};
        for(auto field:fields) for(int shift:{0,8}) {
            hash^=uint8_t(field>>shift); hash*=1099511628211ull;
        }
    }
    return hash;
}
static void SelectorGoldens() {
    // Independent source table dump, including all32 exceptional/family cases.
    const uint16_t keys[2][16]={
        {256,264,264,266,264,266,265,270,264,265,266,270,266,270,270,271},
        {255,4,4,3,4,3,9,13,4,9,3,13,3,13,13,255}};
    const uint8_t rotations[2][16]={
        {0,2,1,1,3,2,1,2,0,0,0,1,3,3,0,0},
        {255,1,2,0,0,3,1,1,3,0,1,2,2,0,3,255}};
    for(int family=0;family<2;++family) for(int mask=0;mask<16;++mask) {
        const auto selected=RoofPrototypeForFlags(uint8_t((mask<<4)|(family?2:0)));
        Check(selected.Kind==keys[family][mask] && selected.Rotation==rotations[family][mask],
              "all32 primary kind/rotation selectors");
    }
}
static void ClassifierGoldens() {
    // Golden streams transcribed independently from the original A/B/C model.
    // They cover concavity, both key families, non-cascading halo and early retry.
    const auto one=ClassifyRoof(Admission(12,{{4,4,6,6}}));
    Check(one.Completed && one.Attempts==1 && one.Cells.size()==16,"one-tile admission");
    Check(CellHash(one)==0x33d4a6e9e08d6745ull,"one-tile full topology stream");
    Check(one.Cells.front().X==3 && one.Cells.front().Y==3 &&
          one.Cells.front().Flags==23 && one.Cells.front().Prototype.Kind==4 &&
          one.Cells.front().Prototype.Rotation==1,"independent northwest eave corner");
    const auto rectangle=ClassifyRoof(Admission(16,{{4,4,10,8}}));
    Check(rectangle.Completed && rectangle.Cells.size()==48 &&
          CellHash(rectangle)==0x379d2b2befaa5355ull,"rectangle full topology stream");
    const auto elbow=ClassifyRoof(Admission(18,{{4,4,12,8},{4,8,8,12}}));
    Check(elbow.Completed && elbow.Cells.size()==84 &&
          CellHash(elbow)==0x11b41d3bc0e668a7ull,"concave elbow full topology stream");
    auto blocked=Admission(12,{{4,4,6,6}});
    blocked.Blocked[4*12+3]=blocked.Blocked[5*12+3]=1;
    const auto trimmed=ClassifyRoof(blocked);
    Check(trimmed.Completed && trimmed.Attempts==3 && trimmed.Cells.size()==12 &&
          CellHash(trimmed)==0x88e1708f20a1c961ull,"blocked halo immediate-return retries");
    Check(trimmed.Flags[4*12+3]==8,"blocked half cell is retained and never admitted");
    Check(RoofPrototypeForFlags(2).Kind==255 && RoofPrototypeForFlags(2).Rotation==255 &&
          RoofPrototypeForFlags(242).Kind==255,"exceptional raw table entries remain observable");
    Check(RoofPrototypeForFlags(0).Kind==256,"second family key zero is canonical256");
    auto capped=Admission(24,{{4,4,20,20}});
    for(int y=4;y<20;y+=2)capped.Blocked[size_t(y)*24+3]=1;
    for(int x=4;x<20;++x)capped.Blocked[3*24+x]=1;
    const auto partial=ClassifyRoof(capped);
    Check(!partial.Completed && partial.Attempts==9 && partial.Cells.size()==291 &&
          CellHash(partial)==0x89b3989f04aef204ull,
          "ninth failed retry preserves partially mutated drawable grid");
    auto full=Admission(128,{{2,2,126,126}});
    const auto large=ClassifyRoof(full);
    Check(large.Width==128 && large.Flags.size()==16384 && large.Heights.size()==16384,
          "original full storage domain");
    bool rejected=false;
    full.Eligible[0]=1;
    try { ClassifyRoof(full); } catch(const std::invalid_argument&) {rejected=true;}
    Check(rejected,"unsupported active border is explicit, not clamped");
}
static uint64_t AttributeHash(const RoofPrototype& prototype) {
    uint64_t hash=14695981039346656037ull;
    for(const auto& triangle:prototype) for(const auto& vertex:triangle.Vertices) {
        const float fields[]={vertex.Position[0],vertex.Position[1],vertex.Position[2],vertex.UV[0],vertex.UV[1]};
        for(auto value:fields) {
            uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));
            for(int shift=0;shift<32;shift+=8) {hash^=uint8_t(bits>>shift);hash*=1099511628211ull;}
        }
    }
    return hash;
}
static void LayerGoldens() {
    auto lower=Admission(16,{{4,4,12,12}});
    auto upper=Admission(16,{{6,6,10,10}});upper.Story=2;
    lower.Blocked[6*16+6]=1;
    const auto levels=ClassifyRoofLevels({lower,upper});
    Check(levels.size()==2 && CellHash(levels[1])==0xe3cd833029c99ec5ull &&
          CellHash(levels[0])==0xc9536ce5e5bbcdc0ull,
          "upper post-classifier active/nonhalo suppresses lower raw admission before halo");
    Check(levels[0].Flags[6*16+6]==8,"upper suppression preserves lower blocker8");
    Check((levels[1].Flags[5*16+5]&4) && !(levels[0].Flags[5*16+5]&4),
          "upper halo does not suppress original lower eligibility");
    Check(lower.Eligible[6*16+6]==1,"raw admission input stays unchanged");
    const auto reversed=ClassifyRoofLevels({upper,lower});
    Check(CellHash(reversed[0])==CellHash(levels[1]) && CellHash(reversed[1])==CellHash(levels[0]),
          "source classification order is upper-first regardless publisher vector order");
}
static void PrototypeGoldens() {
    // Complete authored position/UV streams from00570888 and0056f8ab, atpitch1.
    const int keys[]={3,4,9,13,256,265,264,266,270,271};
    const size_t counts[]={2,2,2,2,4,4,5,6,6,8};
    const uint64_t hashes[]={0x9a86acb3e60a8bd8ull,0x635915018618bb68ull,
        0xe7e1bcfe36f96175ull,0x49bd83477f50ab88ull,0x5b5bae9e874cd605ull,
        0xbe54b631545b6828ull,0xa643598231d65b85ull,0x59703166cf60fd55ull,
        0x90a70cd69fbf6055ull,0x129f925b8de1a375ull};
    for(size_t i=0;i<10;++i) {
        const auto prototype=CreateRoofPrototype(uint16_t(keys[i]),1);
        Check(prototype.size()==counts[i] && AttributeHash(prototype)==hashes[i],
              "all-family complete source vertex/UV streams");
        for(const auto& triangle:prototype) {
            float length=0;for(auto n:triangle.Normal)length+=n*n;
            Check(std::fabs(length-1)<2e-6f,"source flat normals are unit vectors");
        }
    }
    const auto diagonal=CreateRoofPrototype(9,1);
    const float anchor=.5773502691896258f;
    Check(std::fabs(diagonal[0].Normal[0]+anchor)<2e-6f &&
          std::fabs(diagonal[0].Normal[1]-anchor)<2e-6f &&
          std::fabs(diagonal[0].Normal[2]-anchor)<2e-6f,
          "key9 first diagonal normal has both horizontal components");
    const RoofLightingInput lit{2,{{0,0,0}}};
    const auto diagonal0=RoofSourceMaterialFactors(diagonal[0].Normal,lit);
    const auto diagonal1=RoofSourceMaterialFactors(diagonal[1].Normal,lit);
    const auto cardinal=RoofSourceMaterialFactors(CreateRoofPrototype(3,1)[0].Normal,lit);
    Check(std::fabs(diagonal0[0]-.6233022783941228f)<2e-6f &&
          std::fabs(diagonal1[0]-.3356242878721057f)<2e-6f &&
          std::fabs(cardinal[0]-.4110541252858157f)<2e-6f,
          "source prototype normals consume captured light without another X/Z swap");
    Check(AttributeHash(CreateRoofPrototype(255,1))==hashes[3],"unsigned255 invokes common fallback13");
    Check(AttributeHash(CreateRoofPrototype(257,1))==hashes[9],"unknown second-family shape invokes fallback15");
}
static void PlacementGoldens() {
    std::vector<uint8_t> heights(64*64*4,0);
    const size_t own=(4*64+3)*4;
    heights[own]=10;heights[own+1]=20;heights[own+2]=30;heights[own+3]=40;
    heights[(5*64+4)*4+1]=51;
    heights[(5*64+2)*4+1]=52;
    heights[(3*64+4)*4+1]=54;
    heights[(3*64+2)*4+1]=58;
    RoofHalfCell cell{6,8,3,2,{3,0}};
    for(int mask:{1,2,4,8}) {
        cell.Flags=uint8_t((mask<<4)|3);
        const auto selection=SelectRoofGround(cell,heights,64);
        Check(selection.Supported && selection.Draw && selection.Height==50+mask,
              "nonflat source mask selects signed diagonal neighbor TOP byte");
    }
    cell.Flags=uint8_t(3<<4|3);
    Check(SelectRoofGround(cell,heights,64).Height==20,"other topology masks retain own TOP byte");
    cell.Flags=1;
    const auto skipped=SelectRoofGround(cell,heights,64);
    Check(skipped.Supported && !skipped.Draw,"nonflat nonbit2 cell skips mesh submission");
    heights[own]=heights[own+2]=heights[own+3]=20;
    Check(SelectRoofGround(cell,heights,64).Draw,"flat nonbit2 cell remains drawable");
    cell.Flags=3;
    const auto prototype=CreateRoofPrototype(3,1);
    const std::array<float,3> unit{{.5f,9.300511360168457f,.5f}};
    const float x[4]={12.5f,11.5f,11.5f,12.5f},z[4]={9.5f,9.5f,8.5f,8.5f};
    for(int camera=0;camera<4;++camera) {
        const auto placed=PlaceRoofPrototype(prototype,cell,7,1,camera,1,unit);
        const auto& position=placed[0].Vertices[0].Position;
        Check(std::fabs(position[0]-x[camera])<2e-6f &&
              std::fabs(position[2]-z[camera])<2e-6f &&
              std::fabs(position[1]-16.58711814880371f)<2e-6f,
              "original ground/band/story placement and inverse camera story vector");
        Check(placed[0].Normal==prototype[0].Normal,"camera does not rotate physical roof normals");
    }
    cell.Prototype.Rotation=1;
    const auto turn=PlaceRoofPrototype(prototype,cell,7,1,0,1,unit);
    Check(std::fabs(turn[0].Vertices[0].Position[0]-12.5f)<2e-6f &&
          std::fabs(turn[0].Vertices[0].Position[2]-11.0f)<2e-6f,
          "prototype quarter turn uses source pivot and source row-matrix sign");
    const float angle=RoofQuarterTurnAngle(255);
    uint32_t bits;std::memcpy(&bits,&angle,sizeof(bits));
    Check(bits==0x43c846cb,"exceptional255 retains unreduced source final-float angle");
}
static void FactoryHistory() {
    RoofPrototypeFactory factory;
    factory.SetCharacterDetail(2,false);
    Check(factory.GetOrCreate(3,.6f).Mode==2,"first prototype inherits rawHigh");
    Check(factory.SetCharacterDetail(1,true),"explicitMedium updates existing cache");
    Check(factory.GetOrCreate(3,.6f).Mode==0,"existing kind remains unlit afterMedium");
    Check(factory.GetOrCreate(9,.6f).Mode==1,"new kind inherits current rawMedium");
    Check(factory.GetOrCreate(3,1).Mode==0,"reuse does not recreate or reset mode");
    Check(factory.Prototypes().size()==2,"cache is keyed by canonicalkind alone");
    factory.SetCharacterDetail(0,false);
    Check(factory.GetOrCreate(9,.6f).Mode==1,"binding rawdetail alone does not replay callback");
    factory.SetCharacterDetail(1,true);
    Check(factory.GetOrCreate(9,.6f).Mode==0,"sameMedium reapply updates previously new kind");
    factory.ResetFactory();
    Check(factory.Prototypes().empty() && factory.GetOrCreate(3,.6f).Mode==1,
          "actual factory replacement retains rawdetail and resets material history");
    Check(factory.GetOrCreate(255,.6f).Geometry.size()==2 &&
          factory.Prototypes().count(255)==1,"exceptional key preserves its cache identity");
}
int main() {SelectorGoldens();ClassifierGoldens();LayerGoldens();PrototypeGoldens();PlacementGoldens();FactoryHistory();std::puts("Original roof geometry checks passed");}
