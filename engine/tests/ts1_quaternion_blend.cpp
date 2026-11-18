// SPDX-License-Identifier: MPL-2.0
// Synthetic controls for original TS1 quaternion interpolation, without assets.
#include "vitaboy/TS1Quaternion.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace FSO::Vitaboy;
namespace {
int checks = 0;
void check(bool ok, const char* name) {
    if (!ok) throw std::runtime_error(name);
    ++checks;
}
bool equal(const Quaternion& a, const Quaternion& b) {
    return a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
}
bool near(float a, float b) { return std::fabs(a-b) < 0.000001f; }
float value(uint32_t bits) { float result; std::memcpy(&result,&bits,4); return result; }
uint32_t bits(float value) { uint32_t result; std::memcpy(&result,&value,4); return result; }
}
int main() {
    try {
        const Quaternion identity, turn(0,0,1,0), sentinel(7,8,9,10);
        Quaternion out = sentinel;
        TS1BlendQuaternion(identity,turn,0.25f,out);
        check(near(out.Z,0.948683298f) && near(out.W,0.316227766f),
            "first quaternion weight uses normalized linear interpolation");
        const auto spherical = Quaternion::Slerp(identity,turn,0.75f);
        check(std::fabs(out.Z-spherical.Z)>0.01f,
            "unequal weights distinguish original interpolation from spherical interpolation");
        TS1BlendQuaternion(identity,Quaternion(0,0,-1,0),0.25f,out);
        check(near(out.Z,-0.948683298f) && near(out.W,0.316227766f),
            "zero dot keeps the supplied quaternion sign");
        TS1BlendQuaternion(Quaternion(0,0,0,-1),Quaternion(0,0,0,1),0.3f,out);
        check(equal(out,Quaternion(0,0,0,-1)), "negative dot takes the shortest quaternion sign");
        TS1BlendQuaternion(Quaternion(0,0,0,0),Quaternion(0,0,0,0),0.5f,out);
        check(equal(out,identity), "zero blend length selects identity");
        for (float w : {0.0f,0.01f,-0.01f}) {
            out=sentinel; TS1BlendQuaternion(identity,turn,w,out);
            check(equal(out,turn), "second endpoint window copies rather than normalizing");
        }
        for (float w : {0.99f,1.0f,1.005f}) {
            out=sentinel; TS1BlendQuaternion(identity,turn,w,out);
            check(equal(out,identity), "first endpoint window copies rather than normalizing");
        }
        for (float w : {-0.1f,1.1f}) {
            out=sentinel; TS1BlendQuaternion(identity,turn,w,out);
            check(equal(out,sentinel), "out of range weight retains caller destination");
        }
        const Quaternion almostUnit(0,0,0,1.000001f);
        TS1BlendQuaternion(almostUnit,almostUnit,0.5f,out);
        check(equal(out,almostUnit), "near-unit norm tolerance retains authored float components");
        Quaternion alias=turn;
        TS1BlendQuaternion(identity,alias,0.25f,alias);
        check(near(alias.Z,0.948683298f) && near(alias.W,0.316227766f),
            "second quaternion may alias the destination");
        alias=identity; TS1BlendQuaternion(alias,turn,0.25f,alias);
        check(near(alias.Z,0.948683298f) && near(alias.W,0.316227766f),
            "first quaternion may alias the destination");
        // Literal synthetic inputs and outputs of the DVD's original x86
        // blend under CW007f. These catch arithmetic order/rounding changes;
        // their expected values are not computed by the helper being tested.
        constexpr uint32_t literal[][13]={
            {0x3f0737a0,0xbfcc686c,0x3fd94f88,0xbd9eac83,0xbe0c35bb,
             0x3fe66b6c,0xbff9e30b,0xbe998ea0,0xbefafa74,0xbf2df71f,0x3f3acfa5,0x3d256150,0x3d82ae87},
            {0x3f3ef77c,0xbf0d9042,0xbfbd87c1,0x3fd5cdc6,0x3e15bb79,
             0xbf92ee65,0x3d833359,0xbf47ee24,0xbfbb5e97,0xbd82b429,0xbf1777eb,0x3f433297,0x3e81fd1e},
            {0x3f7b3068,0xbf863484,0xba13496a,0x3c832b04,0x3fcacddf,
             0xbfa1f0fd,0x3fd8f0f0,0x3f8c71c8,0x3eb16821,0xbf0f1080,0x3c882308,0x3c9e04fc,0x3f543189}
        };
        for (const auto& row:literal) {
            const Quaternion a(value(row[1]),value(row[2]),value(row[3]),value(row[4]));
            const Quaternion b(value(row[5]),value(row[6]),value(row[7]),value(row[8]));
            TS1BlendQuaternion(a,b,value(row[0]),out);
            check(bits(out.X)==row[9]&&bits(out.Y)==row[10]&&bits(out.Z)==row[11]&&bits(out.W)==row[12],
                "literal original x86 quaternion result matches every component bit");
        }
        std::printf("PASS %d TS1 quaternion blend controls\n",checks);
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; }
}
