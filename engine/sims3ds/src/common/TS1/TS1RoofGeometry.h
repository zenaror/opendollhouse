// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <map>

namespace FSO { namespace Common { namespace TS1 {

// Inputs are the original admission result, before the one-half-cell halo.
// Row-major half cells; neither this type nor the classifier infers rooms.
struct RoofAdmissionLevel {
    int Story = 0; // Original layer/caller index, not native old mesh level.
    std::array<float,3> StoryUnit{{.5f,9.300511360168457f,.5f}};
    int Width = 128;
    std::vector<uint8_t> Eligible, Blocked;
};
struct RoofKindRotation { uint16_t Kind; uint8_t Rotation; };
inline RoofKindRotation RoofPrototypeForFlags(uint8_t flags) {
    static constexpr RoofKindRotation non2[16] = {
        {256,0},{264,2},{264,1},{266,1},{264,3},{266,2},{265,1},{270,2},
        {264,0},{265,0},{266,0},{270,1},{266,3},{270,3},{270,0},{271,0}};
    static constexpr RoofKindRotation bit2[16] = {
        {255,255},{4,1},{4,2},{3,0},{4,0},{3,3},{9,1},{13,1},
        {4,3},{9,0},{3,1},{13,2},{3,2},{13,0},{13,3},{255,255}};
    if(flags & 2) return bit2[flags >> 4];
    return non2[flags >> 4];
}
struct RoofHalfCell {
    int X, Y;
    uint8_t Flags, Height;
    RoofKindRotation Prototype;
};
struct RoofClassification {
    int Width = 0, Attempts = 0;
    bool Completed = false;
    // Preserve every byte, including inactive cells and exceptional table keys.
    std::vector<uint8_t> Flags, Heights;
    std::vector<RoofHalfCell> Cells;
};

// Original 0056ea87: halo, square-ring depth, in-place retry, topology table.
// Eligible border cells are rejected explicitly rather than read out of bounds.
RoofClassification ClassifyRoof(const RoofAdmissionLevel& input);
// Source initializes upper story first. Suppression consumes its post-classifier
// active/non-halo flags; returned order and caller raw admissions are preserved.
std::vector<RoofClassification> ClassifyRoofLevels(const std::vector<RoofAdmissionLevel>& input);

// Prototype coordinates/normals use the original D3D frame, not native tile
// coordinates: source X is tileY, source Z is tileX. UVs are normalized.
struct RoofPrototypeVertex {
    std::array<float,3> Position;
    std::array<float,2> UV;
};
struct RoofPrototypeTriangle {
    std::array<RoofPrototypeVertex,3> Vertices;
    std::array<float,3> Normal;
};
using RoofPrototype = std::vector<RoofPrototypeTriangle>;
RoofPrototype CreateRoofPrototype(uint16_t canonicalKind,float pitch);
// Raw source quads are row-major, four canonical LEFT/TOP/RIGHT/BOTTOM bytes.
// Unsupported boundary inputs remain observable; they are never clamped.
struct RoofGroundSelection { bool Supported=false, Draw=false; uint8_t Height=0; };
RoofGroundSelection SelectRoofGround(const RoofHalfCell& cell,
    const std::vector<uint8_t>& heightQuads,int sourceWidth);
float RoofQuarterTurnAngle(int rotation);
RoofPrototype PlaceRoofPrototype(const RoofPrototype& prototype,const RoofHalfCell& cell,
    uint8_t groundHeight,int story,int cameraRotation,float pitch,
    const std::array<float,3>& storyUnit);

struct RoofCachedPrototype { RoofPrototype Geometry; int Mode=-1; };
// Factory lifetime is independent of classified cells. Geometry/visibility
// changes retain this map; actual style/pitch replacement calls ResetFactory.
class RoofPrototypeFactory {
public:
    int RawDetail=-1;
    bool SetCharacterDetail(int detail,bool applyExisting) {
        RawDetail=detail>=0 && detail<=2 ? detail : -1;
        bool changed=false;
        if(applyExisting) for(auto& entry:Entries) {
            const int mode=RawDetail<0 ? -1 : RawDetail==2 ? 2 : 0;
            changed=changed || entry.second.Mode!=mode;
            entry.second.Mode=mode;
        }
        return changed;
    }
    void ResetFactory() { Entries.clear(); }
    RoofCachedPrototype& GetOrCreate(uint16_t canonicalKind,float pitch) {
        auto found=Entries.find(canonicalKind);
        if(found!=Entries.end()) return found->second;
        return Entries.emplace(canonicalKind,RoofCachedPrototype{
            CreateRoofPrototype(canonicalKind,pitch),RawDetail}).first->second;
    }
    const std::map<uint16_t,RoofCachedPrototype>& Prototypes() const {return Entries;}
private:
    std::map<uint16_t,RoofCachedPrototype> Entries;
};


}}}
