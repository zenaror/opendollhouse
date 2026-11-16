// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "files/formats/iff/AbstractIffChunk.h"
#include "odh/named_float_constants.hpp"

namespace FSO { namespace Files { namespace Formats { namespace IFF { namespace Chunks {
// Keep named tunings in the selected IFF so admitted resource patches use the
// same ownership and lazy decoding as STR, BCON and the other Global chunks.
class FCNS : public IffChunk {
public:
    IFF_CHUNK_TYPE("FCNS")
    odh::NamedFloatConstants Values;
    void Read(IffFile*, st::Reader& stream) override {
        const auto bytes = stream.ReadBytes(stream.Length() - stream.Position());
        Values = odh::read_named_float_constants(odh::View(bytes.data(), bytes.size()));
    }
};
}}}}}
