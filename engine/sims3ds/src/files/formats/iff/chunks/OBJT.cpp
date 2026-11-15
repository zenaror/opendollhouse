/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// Translated from FreeSims sims.files/formats/iff/chunks/OBJT.cs
#include "files/formats/iff/chunks/OBJT.h"
#include "files/formats/iff/IffFile.h"
#include <algorithm>

namespace FSO { namespace Files { namespace Formats { namespace IFF { namespace Chunks {

void GTAB::Read(IffFile*, st::Reader& stream) {
    stream.ByteOrder(st::Endian::Little);
    if (stream.size() < 16 || stream.ReadUInt32() != 0 || stream.ReadUInt32() != 0 ||
        stream.ReadUInt32() != 0x47746162) throw std::runtime_error("invalid Gtab header");
    const uint32_t count = stream.ReadUInt32();
    if (count > 65535 || uint64_t(count) * 8 != stream.size() - stream.pos())
        throw std::runtime_error("invalid Gtab record count");
    std::map<uint32_t,uint32_t> decoded;
    for (uint32_t i = 0; i < count; ++i) {
        const auto from = stream.ReadUInt32();
        const auto to = stream.ReadUInt32();
        if (!from || !to || !decoded.emplace(from,to).second)
            throw std::runtime_error("invalid or duplicate Gtab character mapping");
    }
    replacements.swap(decoded);
}

uint32_t GTAB::Resolve(uint32_t savedGUID) const {
    const auto found = replacements.find(savedGUID);
    return found == replacements.end() ? savedGUID : found->second;
}

bool GTAB::HasTarget(uint32_t guid) const {
    return std::any_of(replacements.begin(), replacements.end(),
        [guid](const auto& pair) { return pair.second == guid; });
}

void OBJT::Read(IffFile* iff, st::Reader& stream)
{
    const auto* guids = iff ? iff->Get<GTAB>(1) : nullptr;
    st::Reader& io = stream;
    io.ByteOrder(st::Endian::Little);
    {
        auto zero = io.ReadInt32();
        (void)zero;
        auto version = io.ReadInt32(); //should be 2/3
        auto objt = io.ReadInt32(); //tjbo
        (void)objt;

        Entries.clear();
        //single tile objects are named. multitile objects arent.

        while (io.HasMore())
        {
            auto& entry = Entries.emplace_back(io, version);
            if (guids) entry.GUID = guids->Resolve(entry.GUID);
        }
    }
}

OBJTEntry::OBJTEntry(st::Reader& io, int32_t version)
{
    //16 bytes of data
    GUID = io.ReadUInt32();
    if (GUID == 0) return;
    Unknown1a = io.ReadUInt16(); //likely number of attributes
    InitTreeVersion = io.ReadUInt16();
    Unknown2a = io.ReadUInt16(); //objd version?
    MainTreeVersion = io.ReadUInt16();
    //increases by one each time, one based, essentially an ID for this loaded type. Mostly matches index in array, but I guess it can possibly be different.
    TypeID = io.ReadUInt16();
    OBJDType = (Chunks::OBJDType)io.ReadUInt16();
    //then the name, null terminated
    Name = io.ReadNullTerminatedString();
    if (Name.size() % 2 == 0) io.ReadByte(); //pad to short width
    if (version > 2) io.ReadInt32(); //not sure what this is
}

}}}}} // namespace FSO::Files::Formats::IFF::Chunks
