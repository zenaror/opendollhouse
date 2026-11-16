// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include "common/TS1/TS1Environment.h"

namespace FSO { namespace Common { namespace TS1 {

struct EnvironmentGeometryTile {
    uint8_t Segments = 0;
    uint16_t TopLeftStyle = 0, TopRightStyle = 0;
    uint16_t TopLeftPattern = 0, TopRightPattern = 0;
    uint16_t BottomLeftPattern = 0, BottomRightPattern = 0;
    bool Floor = false, Pool = false;
};
struct EnvironmentGeometryRoom {
    EnvironmentInputs Inputs;
    bool Outside = false;
    // Deliberately retains repeated visits. The PC flood's pending entries
    // contribute to area, wall coverage and objects more than once.
    std::vector<int32_t> Visits;
};
struct EnvironmentGeometryResult {
    std::vector<uint32_t> Map;
    std::vector<EnvironmentGeometryRoom> Rooms;
};

namespace EnvironmentGeometryDetail {
constexpr int Unseen = -1, PendingA = -2, PendingB = -3, PendingFull = -4, Split = -5;
constexpr int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};
constexpr uint8_t NeighborMask[4] = {8, 2, 4, 1};
constexpr int OppositeSide[5] = {0, 3, 4, 1, 2};
inline bool SideAllows(int side, int direction) {
    switch (side) {
    case 1: return direction == 1 || direction == 2; // southwest
    case 2: return direction == 1 || direction == 3; // southeast
    case 3: return direction == 0 || direction == 3; // northeast
    case 4: return direction == 0 || direction == 2; // northwest
    default: return true;
    }
}
inline int EntrySide(int diagonal, int direction) {
    return diagonal == 16 ? (direction == 0 || direction == 2 ? 2 : 4)
                          : (direction == 0 || direction == 3 ? 1 : 3);
}
inline bool WallScored(uint16_t style) {
    switch (style) {
    case 1: case 3: case 5: case 6: case 7: case 8: case 9: case 15: case 22: case 23:
        return true;
    default: return false;
    }
}
inline bool BlocksOutside(uint16_t style) {
    return style != 2 && style != 12 && style != 13 && style != 14;
}
struct ExpandedTile {
    uint8_t Segments = 0;
    std::array<uint16_t, 6> Styles{};
    std::array<uint16_t, 8> Patterns{};
};
inline ExpandedTile Expand(const std::vector<EnvironmentGeometryTile>& tiles,
                           int width, int height, int x, int y) {
    const auto& tile = tiles[y * width + x];
    ExpandedTile result;
    result.Segments = tile.Segments;
    result.Styles[0] = tile.TopLeftStyle;
    result.Styles[1] = tile.TopRightStyle;
    result.Styles[4] = result.Styles[5] = tile.TopRightStyle;
    result.Patterns[0] = tile.TopLeftPattern;
    result.Patterns[1] = tile.TopRightPattern;
    result.Patterns[2] = tile.BottomRightPattern;
    result.Patterns[3] = tile.BottomLeftPattern;
    if (tile.Segments & 16) {
        result.Patterns[4] = tile.BottomLeftPattern;
        result.Patterns[5] = tile.BottomRightPattern;
    }
    if (tile.Segments & 32) {
        result.Patterns[6] = tile.BottomLeftPattern;
        result.Patterns[7] = tile.BottomRightPattern;
    }
    if (x + 1 < width && (tiles[y * width + x + 1].Segments & 1))
        result.Styles[2] = tiles[y * width + x + 1].TopLeftStyle;
    if (y + 1 < height && (tiles[(y + 1) * width + x].Segments & 2))
        result.Styles[3] = tiles[(y + 1) * width + x].TopRightStyle;
    return result;
}
} // namespace EnvironmentGeometryDetail

// Builds logical room geometry independently of the renderer/routing graph.
// First-floor pools and upper-story floor presence divide flood regions as in
// the original PC game. Room numbers here are local to this story.
inline EnvironmentGeometryResult BuildEnvironmentGeometry(
    const std::vector<EnvironmentGeometryTile>& tiles, int width, int height, int story) {
    using namespace EnvironmentGeometryDetail;
    EnvironmentGeometryResult result;
    if (width <= 0 || height <= 0 || tiles.size() != size_t(width * height)) return result;
    const int count = width * height;
    result.Map.resize(size_t(count));
    std::vector<int> states(size_t(count), Unseen);
    // first room/side and remaining room/side of a diagonal tile
    std::unordered_map<int, std::array<int, 4>> halves;
    std::vector<int> work;
    work.reserve(size_t(count));
    const auto valid = [=](int x, int y) { return x >= 0 && y >= 0 && x < width && y < height; };
    auto setHalf = [&](int index, int room, int side) {
        const auto found = halves.find(index);
        if (found == halves.end()) {
            halves.emplace(index, std::array<int, 4>{room, side, Unseen, OppositeSide[side]});
            return Split;
        }
        auto& half = found->second;
        if (half[0] == room) { halves.erase(found); return room; }
        half[2] = room; half[3] = side;
        return Split;
    };
    // The PC scans columns first. Discovery order affects pending visits.
    for (int seedX = 0; seedX < width; ++seedX) for (int seedY = 0; seedY < height; ++seedY) {
        const int seed = seedY * width + seedX, state = states[seed];
        if (state != Unseen && state != PendingA && state != PendingB && state != Split) continue;
        if (state == Split && halves.at(seed)[2] != Unseen) continue;
        const int room = int(result.Rooms.size());
        result.Rooms.emplace_back();
        auto& visits = result.Rooms.back().Visits;
        const bool floor = tiles[seed].Floor;
        const bool pool = story == 1 && tiles[seed].Pool;
        work.push_back(seed);
        while (!work.empty()) {
            const int index = work.back(); work.pop_back();
            const int x = index % width, y = index / width;
            const int current = states[index];
            const int diagonal = (tiles[index].Segments & 16) ? 16 : (tiles[index].Segments & 32);
            if (current == room) continue;
            int side = 0;
            if (current == PendingFull) states[index] = room;
            else if (current == Unseen) {
                if (diagonal == 16) { states[index] = PendingA; side = 4; }
                else if (diagonal == 32) { states[index] = PendingB; side = 3; }
                else states[index] = PendingFull;
                work.push_back(index); visits.push_back(index);
            } else if (current == PendingA || current == PendingB) {
                side = diagonal == 16 ? (current == PendingA ? 4 : 2)
                                      : (current == PendingA ? 1 : 3);
                states[index] = setHalf(index, room, side);
            } else if (current == Split) {
                const auto half = halves.at(index);
                if (half[2] == room) continue;
                if (half[2] == Unseen) { work.push_back(index); visits.push_back(index); }
                side = half[3];
                states[index] = setHalf(index, room, side);
            } else continue;
            for (int direction = 0; direction < 4; ++direction) {
                if (!SideAllows(side, direction)) continue;
                const int nx = x + DX[direction], ny = y + DY[direction];
                if (!valid(nx, ny)) continue;
                const int neighbor = ny * width + nx, old = states[neighbor];
                if (old >= 0 || (story != 1 && tiles[neighbor].Floor != floor) ||
                    ((story == 1 && tiles[neighbor].Pool) != pool) ||
                    (tiles[neighbor].Segments & NeighborMask[direction])) continue;
                const int nextDiagonal = (tiles[neighbor].Segments & 16) ? 16 : (tiles[neighbor].Segments & 32);
                int next = PendingFull;
                if (nextDiagonal) {
                    const int targetSide = EntrySide(nextDiagonal, direction);
                    const int pending = targetSide == 4 || targetSide == 1 ? PendingA : PendingB;
                    if (old == Unseen) next = pending;
                    else if (old == PendingA || old == PendingB) {
                        if (old == pending) continue;
                    } else if (old == Split) {
                        const auto& half = halves.at(neighbor);
                        if (!((targetSide == half[1] && half[0] == Unseen) ||
                              (targetSide == half[3] && half[2] == Unseen))) continue;
                        next = Split;
                    } else continue;
                }
                states[neighbor] = next;
                work.push_back(neighbor); visits.push_back(neighbor);
            }
        }
    }
    for (int index = 0; index < count; ++index) {
        if (states[index] >= 0) {
            const auto id = uint32_t(states[index]); result.Map[index] = id | (id << 16);
        } else if (states[index] == Split) {
            const auto& half = halves.at(index);
            const int low = half[1] == 4 || half[1] == 1 ? half[0] : half[2];
            const int high = half[1] == 4 || half[1] == 1 ? half[2] : half[0];
            result.Map[index] = uint16_t(low) | (uint32_t(uint16_t(high)) << 16);
        }
    }

    // Connectivity to the exterior passes through fence styles, but not full
    // walls or closed diagonal wall faces. Two nodes retain diagonal sides.
    std::vector<uint8_t> outside(size_t(count) * 2);
    work.push_back(0); outside[0] = 1;
    while (!work.empty()) {
        const int node = work.back(); work.pop_back();
        const int index = node / 2, half = node & 1;
        const int x = index % width, y = index / width;
        const auto expanded = Expand(tiles, width, height, x, y);
        const int diagonal = (tiles[index].Segments & 16) ? 16 : (tiles[index].Segments & 32);
        const int side = diagonal == 16 ? (half == 0 ? 4 : 2) : (half == 0 ? 1 : 3);
        const int diagonalStyle = diagonal == 16 ? 4 : 5;
        const auto add = [&](int target) {
            if (!outside[target]) { outside[target] = 1; work.push_back(target); }
        };
        if (!diagonal || !BlocksOutside(expanded.Styles[diagonalStyle])) add(node ^ 1);
        for (int direction = 0; direction < 4; ++direction) {
            if (diagonal && !SideAllows(side, direction)) continue;
            const int nx = x + DX[direction], ny = y + DY[direction];
            if (!valid(nx, ny)) continue;
            const int bit = direction == 0 ? 2 : direction == 1 ? 8 : direction == 2 ? 1 : 4;
            int styleIndex = direction == 0 ? 1 : direction == 1 ? 3 : direction == 2 ? 0 : 2;
            if ((expanded.Segments & bit) && BlocksOutside(expanded.Styles[styleIndex])) continue;
            const int target = ny * width + nx;
            const int targetDiagonal = (tiles[target].Segments & 16) ? 16 : (tiles[target].Segments & 32);
            int targetHalf = 0;
            if (targetDiagonal) {
                const int entry = EntrySide(targetDiagonal, direction);
                targetHalf = entry == 4 || entry == 1 ? 0 : 1;
            }
            add(target * 2 + targetHalf);
        }
    }
    for (int room = 0; room < int(result.Rooms.size()); ++room) {
        auto& out = result.Rooms[room];
        bool found = false;
        for (int index : out.Visits) if (states[index] == room) {
            out.Outside = outside[index * 2] != 0 || outside[index * 2 + 1] != 0;
            found = true; break;
        }
        if (!found) out.Outside = out.Visits.size() == 1;
        auto& in = out.Inputs;
        for (int index : out.Visits) {
            if (tiles[index].Pool) ++in.GroundGoodCount;
            if (out.Outside) { in.HalfTiles += 2; continue; }
            const auto expanded = Expand(tiles, width, height, index % width, index / width);
            if (!(expanded.Segments & 48)) {
                in.HalfTiles += 2;
                if (tiles[index].Floor) in.CoveredHalfTiles += 2;
                for (int direction = 0; direction < 4; ++direction) {
                    if ((expanded.Segments & (1 << direction)) && WallScored(expanded.Styles[direction])) {
                        ++in.WallCount;
                        if (expanded.Patterns[direction]) ++in.CoveredWallCount;
                    }
                }
            } else {
                ++in.HalfTiles;
                const bool horizontal = expanded.Segments & 16;
                const int style = horizontal ? 4 : 5;
                const int first = horizontal ? 4 : 6, second = first + 1;
                if (WallScored(expanded.Styles[style])) {
                    ++in.WallCount;
                    if (expanded.Patterns[first] || expanded.Patterns[second]) ++in.CoveredWallCount;
                }
                // The original diagonal collector reads these two fields;
                // floor coverage is not the rectangular tile's sentinel.
                if (expanded.Styles[0] || expanded.Patterns[0])
                    ++in.CoveredHalfTiles;
            }
        }
    }
    return result;
}
}}}
