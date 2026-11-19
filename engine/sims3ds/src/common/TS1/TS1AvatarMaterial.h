// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>

namespace FSO { namespace Common { namespace TS1 {

// The three arguments of the DVD's 005a1917. Keep their source identities
// separate: they are not interchangeable with the host's display-mode enum.
struct AvatarMaterialFlags {
    bool A=false, B=false, C=false;
    bool Any() const { return A || B || C; }
    bool operator==(const AvatarMaterialFlags& rhs) const {
        return A==rhs.A && B==rhs.B && C==rhs.C;
    }
};

inline AvatarMaterialFlags AvatarFlagsFromPersonWords(int source614,int source620,int source634) {
    // 00435294 and 00590c09; cXPerson's short array starts at+58c.
    return {((source614|source634)&1)!=0,(source620&1)!=0,(source620&2)!=0};
}

// A material belongs to a binding instance, never the shared content Mesh.
// Mode is RenderMesh+110; the texture sampler is separate render-context state.
struct AvatarPartMaterial {
    int Mode=-1;
    bool SpecialBlend=false; // RenderMesh+c4 bit1
    AvatarMaterialFlags Flags;
    std::array<float,4> Diffuse{{1,1,1,1}}, Ambient{{1,1,1,1}};
    std::array<float,4> Specular{{0,0,0,0}}, Emissive{{0,0,0,0}};

    // 005a1917 restores the current global on the all-clear path. Callers
    // must supply the global at this exact command boundary, not the UI value.
    void ApplyFlags(AvatarMaterialFlags flags,int currentGlobal) {
        Flags=flags;
        if(!flags.Any()) Mode=currentGlobal;
        else if(Mode==0) Mode=1;
        SpecialBlend=Mode==2 && (flags.A || flags.B);
        if(flags.A) Diffuse={{17.f/255.f,1,241.f/255.f,128.f/255.f}};
        else if(flags.C) Diffuse={{17.f/255.f,1,128.f/255.f,128.f/255.f}};
        else if(flags.B) Diffuse={{248.f/255.f,248.f/255.f,1,128.f/255.f}};
        else Diffuse={{1,1,1,1}};
        Ambient=Diffuse;
        Specular=Emissive=flags.Any()?Diffuse:std::array<float,4>{{0,0,0,0}};
    }
    void Initialize(int currentGlobal,AvatarMaterialFlags flags={}) {
        Mode=currentGlobal; // 005d0c35: a newly constructed mesh copies global.
        ApplyFlags(flags,currentGlobal);
    }
    void ApplyDetail(int requested,int previousGlobal,AvatarMaterialFlags flags) {
        // 004e61c6:005a18a4(new),005a1917(flags),THEN005d14fd(new).
        // The first setter observes the instance's previous A/C flags.
        Mode=requested==0 && (Flags.A || Flags.C) ? 1 : requested;
        ApplyFlags(flags,previousGlobal);
    }
};

}}}
