/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Model.VMRoomMap
#include "simantics/model/VMRoomMap.h"
#include <utility>
#include <string>
#include <cstdio>
#include <algorithm>
namespace FSO { namespace SimAntics { namespace Model {
using namespace FSO::LotView::Model;
using Routing::VMObstacle;

void VMRoomMap::GenerateMap(const std::vector<WallTile>& Walls, const std::vector<FloorTile>& Floors, int32_t width, int32_t height, std::vector<VMRoom>& rooms) {
    Map.assign((size_t)(width * height), 0); //although 0 is the base of the array, room 1 is known to simantics as room 0.
    //values of 0 indicate the room has not been chosen in that location yet.
    bool noFloorBad = (rooms.size() > 1);
    this->Width = width;
    this->Height = height;

    // Unbuilt stories form one unobstructed outside room. Its seed tile has
    // only the low half set, exactly as when visited by the normal flood fill.
    if (!Map.empty() && std::none_of(Walls.begin(), Walls.begin() + Map.size(),
            [](const WallTile& w) { return w.Segments != 0; }) &&
        std::none_of(Floors.begin(), Floors.begin() + Map.size(),
            [](const FloorTile& f) { return f.Pattern >= 65534; })) {
        const uint32_t id = static_cast<uint16_t>(rooms.size());
        std::fill(Map.begin(), Map.end(), id | (id << 16));
        Map.front() = id;
        VMRoom room;
        room.Bounds = st::Rectangle(0, 0, width, height);
        room.Area = static_cast<uint16_t>(Map.size());
        room.IsOutside = true;
        rooms.push_back(std::move(room));
        return;
    }

    // Room IDs follow the first unassigned tile in row order. Flood fill only
    // marks tiles, so the next search can resume where the last one stopped.
    std::stack<st::Point> spread;
    bool outside = true;
    for (size_t i = 0; i < Map.size(); i++) {
        if (Map[i] != 0) continue;
        ExpectedTile = Floors[i].Pattern;
        if (ExpectedTile < 65534) ExpectedTile = 0;
        Map[i] = (uint16_t)rooms.size();
        spread.emplace((int32_t)(i % width), (int32_t)(i / width));
        int32_t rminX = spread.top().X;
        int32_t rmaxX = rminX;
        int32_t rminY = spread.top().Y;
        int32_t rmaxY = rminY;
        std::vector<VMObstacle> wallObs;
        uint16_t area = 0;
        while (spread.size() > 0) {
            area++;
            auto item = spread.top(); spread.pop();

            if (item.X > rmaxX) rmaxX = item.X;
            if (item.X < rminX) rminX = item.X;
            if (item.Y > rmaxY) rmaxY = item.Y;
            if (item.Y < rminY) rminY = item.Y;

            auto plusX = item.X + 1 < width ? item.X + 1 : 0;
            auto minX = item.X > 0 ? item.X - 1 : width - 1;
            auto plusY = item.Y + 1 < height ? item.Y + 1 : 0;
            auto minY = item.Y > 0 ? item.Y - 1 : height - 1;

            const WallTile& mainWalls = Walls[item.X + item.Y * width];
            int32_t obsX = item.X << 4;
            int32_t obsY = item.Y << 4;

            if ((mainWalls.Segments & WallSegments::HorizontalDiag) > 0) {
                wallObs.push_back(VMObstacle(obsX + 9, obsY - 1, obsX + 17, obsY + 7));
                wallObs.push_back(VMObstacle(obsX + 4, obsY + 4, obsX + 12, obsY + 12));
                wallObs.push_back(VMObstacle(obsX - 1, obsY + 9, obsX + 7, obsY + 17));
            }

            if ((mainWalls.Segments & WallSegments::VerticalDiag) > 0) {
                wallObs.push_back(VMObstacle(obsX - 1, obsY - 1, obsX + 7, obsY + 7));
                wallObs.push_back(VMObstacle(obsX + 4, obsY + 4, obsX + 12, obsY + 12));
                wallObs.push_back(VMObstacle(obsX + 9, obsY + 9, obsX + 17, obsY + 17));
            }

            if ((uint8_t)mainWalls.Segments > 15) continue; //don't spread on diagonals for now

            const WallTile& PXWalls = Walls[plusX + item.Y * width];
            const WallTile& PYWalls = Walls[item.X + plusY * width];

            if ((mainWalls.Segments & WallSegments::TopLeft) > 0 && !mainWalls.TopLeftDoor) wallObs.push_back(VMObstacle(obsX - 3, obsY - 3, obsX + 6, obsY + 19));
            if ((mainWalls.Segments & WallSegments::TopRight) > 0 && !mainWalls.TopRightDoor) wallObs.push_back(VMObstacle(obsX - 3, obsY - 3, obsX + 19, obsY + 6));
            if ((mainWalls.Segments & WallSegments::BottomLeft) > 0 && !PYWalls.TopRightDoor) wallObs.push_back(VMObstacle(obsX - 3, obsY + 13, obsX + 19, obsY + 19));
            if ((mainWalls.Segments & WallSegments::BottomRight) > 0 && !PXWalls.TopLeftDoor) wallObs.push_back(VMObstacle(obsX + 13, obsY - 3, obsX + 19, obsY + 19));
            //
            if (((PXWalls.Segments & WallSegments::TopLeft) == 0 || PXWalls.TopLeftStyle != 1))
                SpreadOnto(Walls, Floors, plusX, item.Y, 0, Map, width, height, spread, (uint16_t)rooms.size(), ExpectedTile, noFloorBad);

            if (((mainWalls.Segments & WallSegments::TopLeft) == 0 || mainWalls.TopLeftStyle != 1))
                SpreadOnto(Walls, Floors, minX, item.Y, 2, Map, width, height, spread, (uint16_t)rooms.size(), ExpectedTile, noFloorBad);

            if (((PYWalls.Segments & WallSegments::TopRight) == 0 || PYWalls.TopRightStyle != 1))
                SpreadOnto(Walls, Floors, item.X, plusY, 1, Map, width, height, spread, (uint16_t)rooms.size(), ExpectedTile, noFloorBad);

            if (((mainWalls.Segments & WallSegments::TopRight) == 0 || mainWalls.TopRightStyle != 1))
                SpreadOnto(Walls, Floors, item.X, minY, 3, Map, width, height, spread, (uint16_t)rooms.size(), ExpectedTile, noFloorBad);
        }

        auto bounds = st::Rectangle(rminX, rminY, (rmaxX - rminX) + 1, (rmaxY - rminY) + 1);
        auto roomObs = GenerateRoomObs((uint16_t)rooms.size(), bounds);
        OptimizeObstacles(wallObs);
        OptimizeObstacles(roomObs);
        VMRoom room;
        room.IsOutside = outside || ExpectedTile > 65533;
        room.IsPool = ExpectedTile > 65533;
        room.Bounds = bounds;
        room.WallObs = std::move(wallObs);
        room.RoomObs = std::move(roomObs);
        room.Area = area;
        rooms.push_back(std::move(room));
        outside = false;
    }
}

void VMRoomMap::SpreadOnto(const std::vector<WallTile>& walls, const std::vector<FloorTile>& floors, int32_t x, int32_t y, int32_t inDir, std::vector<uint32_t>& map, int32_t width, int32_t /*height*/, std::stack<st::Point>& spread, uint16_t room, uint16_t expectedTile, bool /*noAir*/) {
    const WallTile& wall = walls[x + y * width];
    auto floor = floors[x + y * width].Pattern;
    if ((expectedTile > 0 && expectedTile != floor) || (expectedTile == 0 && floor > 65533)) {
        return;
    }
    if ((wall.Segments & WallSegments::HorizontalDiag) > 0) {
        if (inDir < 2) {
            //bottom (bottom right pattern)
            if ((map[x + y * width] & (uint32_t)0x0000FFFF) > 0) return; //don't spread onto
            map[x + y * width] |= room;
        } else {
            //top (bottom left pattern)
            if ((map[x + y * width] & (uint32_t)0xFFFF0000) > 0) return; //don't spread onto
            map[x + y * width] |= (uint32_t)room << 16;
        }
        //if (!floorMode) walls[x + y * width] = wall;
    } else if ((wall.Segments & WallSegments::VerticalDiag) > 0) {
        if (inDir > 0 && inDir < 3) {
            //left
            if ((map[x + y * width] & (uint32_t)0x0000FFFF) > 0) return; //don't spread onto
            map[x + y * width] |= room;
        } else {
            //right
            if ((map[x + y * width] & (uint32_t)0xFFFF0000) > 0) return; //don't spread onto
            map[x + y * width] |= (uint32_t)room << 16;
        }
        //if (!floorMode) walls[x + y * width] = wall;
    } else {
        if (map[x + y * width] > 0) return;
        map[x + y * width] = (uint32_t)(room | (room << 16));
    }
    spread.emplace(x, y);
}

std::vector<VMObstacle> VMRoomMap::GenerateRoomObs(uint16_t room, const st::Rectangle& bounds) {
    std::vector<VMObstacle> result;
    auto x1 = std::max<int32_t>(0, bounds.X - 1);
    auto x2 = std::min(Width, bounds.Right() + 1);
    auto y1 = std::max<int32_t>(0, bounds.Y - 1);
    auto y2 = std::min(Height, bounds.Bottom() + 1);
    for (int32_t y = y1; y < y2; y++) {
        int next = -1; //index into result (C# uses a reference to the last added obstacle)
        for (int32_t x = x1; x < x2; x++) {
            int32_t tRoom = (uint16_t)Map[x + y * Width];
            if (tRoom != room) {
                if (next != -1) result[next].x2 += 16;
                else {
                    result.push_back(VMObstacle((x << 4) - 3, (y << 4) - 3, (x << 4) + 19, (y << 4) + 19));
                    next = (int)result.size() - 1;
                }
            } else {
                if (next != -1) next = -1;
            }
        }
    }
    return result;
}

void VMRoomMap::OptimizeObstacles(std::vector<VMObstacle>& obs) {
    if (obs.size() < 2) return;
    // Merge in the original visit order. Links skip consumed rectangles without
    // allocating a list node per wall, and keep surviving obstacles in order.
    const size_t end = obs.size();
    std::vector<size_t> next(end);
    for (size_t i = 0; i < end; i++) next[i] = i + 1;
    size_t head = 0;
    for (size_t i = head; i != end; i = next[i]) {
        auto& a = obs[i];
        size_t* link = &head;
        while (*link != end) {
            const size_t j = *link;
            const auto& b = obs[j];
            if (i != j && a.x1 == b.x1 && a.x2 == b.x2 && a.y1 <= b.y2 && a.y2 >= b.y1) {
                a.y1 = std::min(a.y1, b.y1);
                a.y2 = std::max(a.y2, b.y2);
            } else if (i != j && a.y1 == b.y1 && a.y2 == b.y2 && a.x1 <= b.x2 && a.x2 >= b.x1) {
                a.x1 = std::min(a.x1, b.x1);
                a.x2 = std::max(a.x2, b.x2);
            } else {
                link = &next[j];
                continue;
            }
            *link = next[j];
        }
    }
    size_t count = 0;
    for (size_t i = head; i != end; i = next[i]) obs[count++] = obs[i];
    obs.resize(count);
}

void VMRoomMap::PrintRoomMap() {
    int off = 0;
    for (int32_t y = 0; y < Height; y++) {
        std::string sb;
        for (int32_t x = 0; x < Width; x++) {
            sb += std::to_string(Map[off++]);
            if (Map[off - 1] < 10) sb += " ";
            sb += " ";
        }
        fprintf(stderr, "%s\n", sb.c_str());
    }
}
}}}
