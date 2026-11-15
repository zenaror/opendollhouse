// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "files/formats/iff/AbstractIffChunk.h"
#include <stdexcept>

namespace FSO { namespace Files { namespace Formats { namespace IFF { namespace Chunks {
// OpenDollhouse's execution supplement to the classic ObjM records.
class ODHI : public IffChunk {
public:
    IFF_CHUNK_TYPE("ODHI")
    static constexpr size_t MaxBytes = 16 * 1024 * 1024;
    std::vector<uint8_t> Data;
    void Read(IffFile*, st::Reader& stream) override {
        if (stream.size() > MaxBytes) throw std::runtime_error("ODHI checkpoint exceeds limit");
        Data = stream.bytes(stream.size());
    }
    bool Write(IffFile*, st::Writer& stream) override {
        stream.WriteBytes(Data);
        return true;
    }
};
}}}}}
