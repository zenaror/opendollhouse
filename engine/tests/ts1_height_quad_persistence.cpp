// SPDX-License-Identifier: MPL-2.0
// The VM's side of the confirmed-save handoff. SaveLot supplies the decoded
// committed ARRY; this fixture does not pretend to execute the app save path.
#include "simantics/model/VMArchitectureTerrain.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
using FSO::SimAntics::Model::VMArchitectureTerrain;
static void need(bool value, const char* what) { if (!value) throw std::runtime_error(what); }
int main() { try {
    VMArchitectureTerrain terrain(48,48);
    std::array<int,4> q{};
    need(!terrain.GetTS1HeightQuad(2,2,q), "marshal without ARRY has no authored source");
    std::vector<uint8_t> raw(64*64*4, 10);
    const auto at=[](int x,int y) { return size_t(y*64+x)*4; };
    raw[at(2,2)]=9; raw[at(2,2)+2]=11;
    terrain.Heights.assign(48*48,100);
    need(terrain.SetTS1HeightSource(raw), "authored source");
    need(terrain.GetTS1HeightQuad(2,2,q) && q==std::array<int,4>{9,10,11,10}, "retain packed corners");
    terrain.Heights[2*48+2]=200;
    need(terrain.GetTS1HeightQuad(2,2,q) && q==std::array<int,4>{10,20,10,10}, "edited vertices");
    // Synthetic bytes represent the result after a successful first save.
    auto committed=raw;
    committed[at(2,2)]=10; committed[at(2,2)+1]=20;
    committed[at(2,2)+2]=10; committed[at(2,2)+3]=10;
    need(terrain.SetTS1HeightSource(committed), "confirmed first save rebase");
    terrain.Heights[2*48+2]=100;
    need(terrain.GetTS1HeightQuad(2,2,q) && q==std::array<int,4>{10,10,10,10}, "edit back must not revive old packed corners");
    const auto before=q; // Expected committed corners for the following model checks.
    auto second=committed; second[at(2,2)+1]=10;
    need(terrain.SetTS1HeightSource(second), "confirmed second save rebase");
    VMArchitectureTerrain reopened(48,48); reopened.Heights.assign(48*48,100);
    need(reopened.SetTS1HeightSource(second) && reopened.GetTS1HeightQuad(2,2,q) && q==before, "reopen same quad");
    terrain.EnterVis(); terrain.Heights[2*48+2]=300;
    need(terrain.GetTS1HeightQuad(2,2,q) && q[1]==30, "preview reads live vertices");
    terrain.ExitVis(); need(terrain.GetTS1HeightQuad(2,2,q) && q==before, "undo restores vertices");
    raw[at(47,47)] = 17; raw[at(47,48)+1] = 12;
    need(terrain.SetTS1HeightSource(raw), "boundary source");
    need(terrain.GetTS1HeightQuad(47,47,q) && q==std::array<int,4>{17,10,10,10}, "cropped boundary preserves authored corner");
    need(!terrain.GetTS1HeightQuad(48,47,q), "outside lot rejected");
    need(!terrain.SetTS1HeightSource({1,2}) && !terrain.GetTS1HeightQuad(2,2,q), "invalid source clears authority");
    std::cout << "TS1 terrain height source and confirmed rebase: PASS\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
