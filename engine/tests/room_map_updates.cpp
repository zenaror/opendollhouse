// SPDX-License-Identifier: MPL-2.0
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include <iostream>
#include <stdexcept>

using namespace FSO::SimAntics;
using namespace FSO::LotView::Model;

static void Check(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}

int main() {
    constexpr int width = 12, height = 8;
    VMContext context(nullptr);
    VM vm(&context, nullptr);
    context.Architecture(std::make_unique<VMArchitecture>(width, height, nullptr, &context));
    auto& arch = *context.Architecture();
    for (auto& floor : arch.Floors[1]) floor.Pattern = 1;
    arch.Tick();

    // The ordinary flood fill leaves the seed's top half unset. Each empty
    // story still gets its own outside room, including stories with flooring.
    for (int level = 0; level < arch.Stories; level++) {
        const uint32_t id = level + 1;
        const auto& map = arch.Rooms[level].Map;
        Check(map.front() == id, "outside room seed");
        for (size_t i = 1; i < map.size(); i++)
            Check(map[i] == (id | (id << 16)), "outside room ID");
        const auto& room = arch.RoomData.at(id);
        Check(room.IsOutside && !room.IsPool && room.Area == width * height, "outside room area");
        Check(room.RoomObs.empty() && room.WallObs.empty(), "unobstructed story");
    }

    // Compare the very next support query with a whole-story calculation:
    // alternating placements and removals include edges and overlapping ranges.
    for (bool placed : {true, false}) {
        for (auto pos : {st::Point(0, 0), st::Point(5, 4), st::Point(6, 3), st::Point(11, 7)}) {
            arch.SetObjectSupported(pos.X, pos.Y, 1, placed);
            const auto incremental = arch.Supported[0];
            arch.RegenerateSupported(2);
            Check(incremental == arch.Supported[0], "column support changed on full recalculation");
        }
    }

    arch.SetObjectSupported(2, 2, 1, true);
    FloorTile empty;
    for (int y = 0; y <= 2; y++) for (int x = 0; x <= 2; x++)
        arch.SetFloor(x, y, 2, empty, true);
    arch.SetObjectSupported(11, 7, 1, true);
    const auto withPendingFloor = arch.Supported[0];
    arch.RegenerateSupported(2);
    Check(withPendingFloor == arch.Supported[0], "pending distant floor edit was skipped");

    FloorTile pool;
    pool.Pattern = 65535;
    arch.SetFloor(3, 5, 1, pool, true);
    arch.Tick();
    const auto poolRoom = arch.Rooms.front().Map[5 * width + 3] & 0xffffu;
    Check(arch.RoomData.at(poolRoom).IsPool && arch.RoomData.at(poolRoom).Area == 1,
          "pool beside outside room");
    std::cout << "Room maps and immediate column support passed\n";
}
