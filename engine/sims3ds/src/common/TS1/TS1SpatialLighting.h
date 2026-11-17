// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace FSO { namespace Common { namespace TS1 {

struct SpatialOffset {
    int8_t Dx = 127, Dy = 127;
    bool operator==(const SpatialOffset& other) const { return Dx == other.Dx && Dy == other.Dy; }
    bool operator!=(const SpatialOffset& other) const { return !(*this == other); }
    int DistanceSquared() const { return int(Dx) * Dx + int(Dy) * Dy; }
};
struct SpatialCell {
    std::array<SpatialOffset, 2> Slots{};
    bool operator==(const SpatialCell& other) const { return Slots == other.Slots; }
    bool operator!=(const SpatialCell& other) const { return !(*this == other); }
    // DVD 004b523c: ordered slots, with no sort, deduplication or promotion of
    // an overwritten first slot. A third source can be permanently lost.
    void Insert(SpatialOffset value) {
        if (Slots[0].Dx == 127) Slots[0] = value;
        else if (Slots[1].Dx == 127) Slots[1] = value;
        else if (value.DistanceSquared() < Slots[0].DistanceSquared()) Slots[0] = value;
        else if (Slots[1].DistanceSquared() > value.DistanceSquared()) Slots[1] = value;
    }
    // DVD 004b53b3: remove only one matching pair, then compact.
    void Erase(SpatialOffset value) {
        if (Slots[0].Dx == 127) return;
        if (Slots[0] == value) { Slots[0] = Slots[1]; Slots[1] = {}; }
        else if (Slots[1].Dx != 127 && Slots[1] == value) Slots[1] = {};
    }
};
static_assert(sizeof(SpatialOffset) == 2 && sizeof(SpatialCell) == 4, "Original spatial cell is four signed bytes");

struct SpatialPosition { int X = 0, Y = 0, Story = 1; };
struct SpatialEmitter {
    SpatialPosition Position;
    int16_t ObjectRoom = -1, SourceMode = 0, Contribution = 0, Hidden = 0;
    uint16_t Flags2 = 0;
};

// Borrowed for the duration of an operation, never retained. Packed low/high
// cells use native environment indices; CanonicalIds explicitly maps those
// indices to original record IDs (index zero must be -1, not exterior room 0).
struct SpatialRoomMapView {
    int Width = 0, Height = 0;
    const std::vector<std::vector<uint32_t>>* Maps = nullptr;
    const std::vector<int16_t>* CanonicalIds = nullptr;
    std::array<int16_t, 2> RoomsAt(int x, int y, int story) const {
        if (!Maps || !CanonicalIds || x < 0 || y < 0 || x >= Width || y >= Height ||
            story < 1 || story > 2 || size_t(story) > Maps->size()) return {{-1,-1}};
        const auto& map = (*Maps)[story - 1];
        const size_t index = size_t(y) * Width + x;
        if (index >= map.size()) return {{-1,-1}};
        const auto cell = map[index];
        const auto id = [&](uint16_t value) { return value < CanonicalIds->size() ? (*CanonicalIds)[value] : int16_t(-1); };
        return {{id(uint16_t(cell)), id(uint16_t(cell >> 16))}};
    }
};

// Mutable original source grid. Event ordering belongs to the VM room manager:
// CollectVisit is called for EVERY ordered room-tile/object-iterator visit, not
// once per unique source or arbitrary VM entity pass. No renderer dependency.
class SpatialPublisher {
public:
    int Width = 0, Height = 0, Radius = 4;
    std::vector<std::vector<SpatialCell>> Maps;

    void Initialize(int width, int height, int radius = 4) {
        if (width < 3 || height < 3 || width > 127 || height > 127 || radius < 0 || radius >= 127)
            throw std::invalid_argument("TS1 spatial grid dimensions/radius");
        std::vector<std::vector<SpatialCell>> maps(2, std::vector<SpatialCell>(size_t(width) * height));
        Maps.swap(maps); Width = width; Height = height; Radius = radius;
    }
    bool Interior(SpatialPosition point) const {
        return point.Story >= 1 && point.Story <= 2 && point.X > 0 && point.Y > 0 &&
            point.X < Width - 1 && point.Y < Height - 1;
    }
    void Erase(SpatialPosition source) {
        if (!Interior(source)) return;
        for (int dy = -Radius; dy <= Radius; ++dy) for (int dx = -Radius; dx <= Radius; ++dx) {
            const SpatialPosition target{source.X + dx, source.Y + dy, source.Story};
            if (Interior(target)) Maps[size_t(source.Story - 1)][size_t(target.Y) * Width + target.X].Erase({int8_t(dx), int8_t(dy)});
        }
    }
    void Insert(SpatialPosition source, const SpatialRoomMapView& rooms) {
        if (!Interior(source) || rooms.Width != Width || rooms.Height != Height) return;
        const auto sourcePair = rooms.RoomsAt(source.X, source.Y, source.Story);
        // 004b5090 reads the raw tile room. A split source is -5; do not
        // replace it with the room resolved from an object's subtile position.
        const int sourceRoom = sourcePair[0] == sourcePair[1] ? sourcePair[0] : -5;
        if (sourceRoom < 0 && sourceRoom != -5) return;
        for (int dy = -Radius; dy <= Radius; ++dy) for (int dx = -Radius; dx <= Radius; ++dx) {
            const SpatialPosition target{source.X + dx, source.Y + dy, source.Story};
            if (!Interior(target)) continue;
            const auto pair = rooms.RoomsAt(target.X, target.Y, target.Story);
            const int targetRoom = pair[0] == pair[1] ? pair[0] : -5;
            if (targetRoom == sourceRoom || (targetRoom == -5 && (pair[0] == sourceRoom || pair[1] == sourceRoom)))
                Maps[size_t(source.Story - 1)][size_t(target.Y) * Width + target.X].Insert({int8_t(dx), int8_t(dy)});
        }
    }
    void CollectVisit(const SpatialEmitter& source, const SpatialRoomMapView& rooms) {
        if (source.Hidden != 0 || source.ObjectRoom < 0 || !(source.Flags2 & 0x400) || !Interior(source.Position)) return;
        if (source.Contribution == 0) Erase(source.Position);
        else if (source.SourceMode == 1) { Erase(source.Position); Insert(source.Position, rooms); }
    }
    void Unregister(const SpatialEmitter& source) {
        // 004331d4: source mode only; no hidden/flag/contribution admission.
        if (source.SourceMode == 1) Erase(source.Position);
    }
};

// 00466bdf/00466c2d and 0046716c/00466d14. A synchronous F1 during
// collection can dirty a record again, while the final story-mask clear leaves
// it dormant. Do not replace these two pieces of state with one dirty boolean.
struct SpatialRoomSchedule {
    std::array<uint8_t, 2048> Dirty{};
    uint8_t StoryMask = 0;
    void Mark(int canonicalID) {
        if (canonicalID < 0 || canonicalID >= 2048) return;
        Dirty[size_t(canonicalID)] = 1;
        StoryMask |= uint8_t(1u << (unsigned(canonicalID) >> 10));
    }
    template<class Collect> void Flush(const std::vector<uint16_t>& activeIDs, Collect collect) {
        const uint8_t selected = StoryMask;
        if (!selected) return;
        // The caller supplies the original numeric record order.
        for (const auto id : activeIDs) if (id < 2048 && Dirty[id] && (selected & (1u << (id >> 10)))) {
            Dirty[id] = 0;
            collect(id);
        }
        StoryMask &= uint8_t(~selected);
    }
};
}}}
