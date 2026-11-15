/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
#pragma once
#include "files/formats/iff/AbstractIffChunk.h"
#include <set>
#include <stdexcept>

namespace FSO { namespace Files { namespace Formats { namespace IFF { namespace Chunks {
// Private, optional names for persistent object instances. OBJT describes a type,
// not an instance; two cradles of the same type may have different names.
class ODHN : public IffChunk {
public:
    IFF_CHUNK_TYPE("ODHN")
    static constexpr size_t MaxBytes = 4 * 1024 * 1024;
    static constexpr uint32_t MaxGroups = 32767, MaxNameBytes = 4096;
    struct Part {
        int16_t ID = 0;
        uint32_t GUID = 0;
        bool operator==(const Part& rhs) const { return ID == rhs.ID && GUID == rhs.GUID; }
    };
    struct Entry {
        int16_t AnchorID = 0;
        uint32_t MasterGUID = 0;
        std::vector<Part> Parts;
        std::string Name;
        bool CanonicalPersonalName = false;
    };
    uint32_t FormatVersion = 2;
    uint32_t ObjectSignature = 0;
    std::vector<Entry> Entries;

    static void Validate(const std::vector<Entry>& entries, uint32_t version = 2) {
        if (version != 1 && version != 2) throw std::runtime_error("ODHN: unsupported format");
        if (entries.size() > MaxGroups) throw std::runtime_error("ODHN: too many groups");
        std::set<int16_t> allIDs;
        size_t bytes = 16;
        for (const auto& e : entries) {
            if (e.AnchorID <= 0 || !e.MasterGUID || e.Parts.empty() || e.Parts.size() > MaxGroups || e.Name.size() > MaxNameBytes)
                throw std::runtime_error("ODHN: invalid group identity or name length");
            int16_t previous = 0;
            for (const auto& p : e.Parts) {
                if (p.ID <= previous || !p.GUID || !allIDs.insert(p.ID).second)
                    throw std::runtime_error("ODHN: overlapping or unsorted object identities");
                previous = p.ID;
            }
            if (e.AnchorID != e.Parts.front().ID) throw std::runtime_error("ODHN: anchor is not the canonical first part");
            bytes += (version == 2 ? 13 : 12) + e.Parts.size() * 6 + e.Name.size();
            if (bytes > MaxBytes) throw std::runtime_error("ODHN: names exceed size limit");
        }
    }
    void Read(IffFile*, st::Reader& input) override {
        if (input.size() > MaxBytes) throw std::runtime_error("ODHN: chunk exceeds size limit");
        // Read into locals. Truncation, unsupported versions, and trailing bytes
        // reject the entire supplement before any names are applied.
        st::Reader io(input.raw(input.size()), input.size(), st::Endian::Little);
        if (io.u32() != 0x4E48444Fu) throw std::runtime_error("ODHN: unsupported format");
        auto version = io.u32();
        if (version != 1 && version != 2) throw std::runtime_error("ODHN: unsupported format");
        auto signature = io.u32();
        auto count = io.u32();
        if (count > MaxGroups) throw std::runtime_error("ODHN: group count exceeds limit");
        std::vector<Entry> entries;
        for (uint32_t i = 0; i < count; ++i) {
            Entry e;
            e.AnchorID = io.s16(); e.MasterGUID = io.u32();
            auto parts = io.u16(); auto length = io.u32();
            if (!parts || parts > MaxGroups || length > MaxNameBytes) throw std::runtime_error("ODHN: invalid entry size");
            for (uint32_t j = 0; j < parts; ++j) e.Parts.push_back({io.s16(), io.u32()});
            auto bytes = io.bytes(length);
            e.Name.assign(bytes.begin(), bytes.end());
            if (version == 2) {
                auto canonical = io.u8();
                if (canonical > 1) throw std::runtime_error("ODHN: invalid name provenance");
                e.CanonicalPersonalName = canonical != 0;
            }
            entries.push_back(std::move(e));
        }
        if (io.pos() != io.size()) throw std::runtime_error("ODHN: trailing bytes");
        Validate(entries, version);
        FormatVersion = version;
        ObjectSignature = signature;
        Entries = std::move(entries);
    }
    bool Write(IffFile*, st::Writer& io) override {
        Validate(Entries);
        io.ByteOrder(st::Endian::Little);
        io.WriteUInt32(0x4E48444Fu); io.WriteUInt32(2);
        io.WriteUInt32(ObjectSignature); io.WriteUInt32((uint32_t)Entries.size());
        for (const auto& e : Entries) {
            io.WriteInt16(e.AnchorID); io.WriteUInt32(e.MasterGUID);
            io.WriteUInt16((uint16_t)e.Parts.size()); io.WriteUInt32((uint32_t)e.Name.size());
            for (const auto& p : e.Parts) { io.WriteInt16(p.ID); io.WriteUInt32(p.GUID); }
            io.WriteBytes((const uint8_t*)e.Name.data(), e.Name.size());
            io.WriteByte(e.CanonicalPersonalName ? 1 : 0);
        }
        return true;
    }
};
}}}}}
