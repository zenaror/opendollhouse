// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace FSO { namespace Common { namespace TS1 {

using RoofRGB = std::array<float,3>;
// Mode is supplied from the actual mesh material state, independently of the
// inherited ambient. An unbound input preserves the existing consumer path.
struct RoofLightingInput {
    int Mode=-1;
    RoofRGB Ambient{{0,0,0}};
    bool Available() const {
        if(Mode==0) return true; // texture-only does not consume ambient
        return Mode>=1 && Mode<=2 && std::isfinite(Ambient[0]) &&
            std::isfinite(Ambient[1]) && std::isfinite(Ambient[2]);
    }
    bool SameMaterialAs(const RoofLightingInput& other) const {
        if(Available()!=other.Available()) return false;
        if(!Available()) return true;
        return Mode==other.Mode && (Mode==0 || Ambient==other.Ambient);
    }
};

// Bounded uniform snapshot policy. This is not a per-prototype cache: new
// canonical kinds created after a Medium apply may have mixed source modes.
struct RoofUniformMaterialState {
    int RawDetail=-1, Mode=-1;
    bool HasMaterials=false;
    void SetCharacterDetail(int detail,bool applyExisting) {
        const bool initial=RawDetail<0;
        RawDetail=detail>=0 && detail<=2 ? detail : -1;
        if(RawDetail<0) Mode=-1;
        else if(initial || !HasMaterials) Mode=RawDetail;
        else if(applyExisting) Mode=RawDetail==2 ? 2 : 0;
    }
    void ResetFactory() {HasMaterials=false;Mode=RawDetail;}
};
inline float ClampRoofPitch(float pitch) {
    if(!(pitch>0)) return 0; // ordered source comparison includes NaN
    return pitch>2 ? 2 : pitch;
}
inline bool RoofPitchReplacesFactory(float previous,float clampedPitch) {
    // Source epsilon bits0x358637bd; subtract float arguments at extended
    // precision rather than rounding the difference back to float first.
    const double difference=std::fabs(double(clampedPitch)-double(previous));
    return std::isnan(difference) || double(1.0e-6f)<difference;
}

// Native world frame is (tileX, up, tileY). Source 004ca6b6 maps architectural
// tileY to D3D X and tileX to D3D Z. Keep the frame conversion explicit.
// These are flat native hip face normals; camera yaw is not a light input.
inline RoofRGB RoofFaceNormal(int face,float pitch) {
    const float inverse=1.0f/std::sqrt(1.0f+pitch*pitch);
    switch(face) {
        case 0:return {{0,inverse,-pitch*inverse}};
        case 1:return {{pitch*inverse,inverse,0}};
        case 2:return {{0,inverse,pitch*inverse}};
        default:return {{-pitch*inverse,inverse,0}};
    }
}

inline constexpr RoofRGB RoofSourceLightToSurface() {
    // Exact positive counterparts of the captured D3DLIGHT2 Direction fields.
    // No further normalization is established before the source SetLight call.
    return {{.24913644790172577f,.8304547667503357f,.49827289580345154f}};
}
inline RoofRGB RoofNormalToSource(const RoofRGB& nativeNormal) {
    return {{nativeNormal[2],nativeNormal[1],nativeNormal[0]}};
}

// Ideal fixed-function white material / white roof-owned directional light.
// This models API RGB arithmetic, not an original driver byte-rounding rule.
inline RoofRGB RoofMaterialFactors(const RoofRGB& unitWorldNormal,const RoofLightingInput& input) {
    if(!input.Available() || input.Mode==0) return {{1,1,1}};
    const auto normal=RoofNormalToSource(unitWorldNormal);
    constexpr auto toLight=RoofSourceLightToSurface();
    const float diffuse=std::max(0.0f,normal[0]*toLight[0]+
        normal[1]*toLight[1]+normal[2]*toLight[2]);
    return {{std::clamp(input.Ambient[0]+diffuse,0.0f,1.0f),
        std::clamp(input.Ambient[1]+diffuse,0.0f,1.0f),
        std::clamp(input.Ambient[2]+diffuse,0.0f,1.0f)}};
}
inline RoofRGB RoofMaterialRGB(const RoofRGB& texture,const RoofRGB& unitWorldNormal,
                               const RoofLightingInput& input) {
    const auto light=RoofMaterialFactors(unitWorldNormal,input);
    return {{texture[0]*light[0],texture[1]*light[1],texture[2]*light[2]}};
}

}}}
