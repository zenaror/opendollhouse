// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "files/formats/iff/AbstractIffChunk.h"
#include <stdexcept>

namespace FSO { namespace Files { namespace Formats { namespace IFF { namespace Chunks {
// Optional authority/phase marker. Motive values remain in classic ObjM; ODHI8
// retains its unchanged execution graph for readers which do not know ODMP.
class ODMP : public IffChunk {
public:
    IFF_CHUNK_TYPE("ODMP")
    static constexpr size_t MaxBytes = 20 + 32767 * 16;
    std::vector<uint8_t> Data;
    void Read(IffFile*, st::Reader& stream) override {
        if (stream.size() > MaxBytes) throw std::runtime_error("ODMP marker exceeds limit");
        Data = stream.bytes(stream.size());
    }
    bool Write(IffFile*, st::Writer& stream) override { stream.WriteBytes(Data); return true; }
};
}}}}}
