// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "files/formats/iff/chunks/BHAV.h"
#include "files/Hash.h"
#include <algorithm>

namespace FSO { namespace SimAntics { namespace Engine { namespace Utils {

inline size_t TS1PreviousDecoderArgs(const Files::Formats::IFF::Chunks::BHAV& tree) {
    return tree.SourceFormatVersion < 0x8002 ? 0 : tree.SourceParsedArgs;
}
inline bool TS1HasHistoricalBehaviorMetadata(const Files::Formats::IFF::Chunks::BHAV& tree) {
    return (tree.SourceFormatVersion == 0x8000 || tree.SourceFormatVersion == 0x8001 ||
            tree.SourceFormatVersion == 0x8002) &&
        tree.Args == tree.SourceParsedArgs && tree.Locals == tree.SourceParsedLocals;
}
inline uint32_t TS1BehaviorSignature(const Files::Formats::IFF::Chunks::BHAV& tree,
                                    bool historicalMetadata = false) {
    st::Writer bytes(st::Endian::Little);
    bytes.WriteByte(tree.Type);
    bytes.WriteByte(historicalMetadata ? uint8_t(TS1PreviousDecoderArgs(tree)) : tree.Args);
    bytes.WriteUInt16(historicalMetadata ? tree.PreviousDecoderLocals : tree.Locals);
    for (const auto& op : tree.Instructions) {
        bytes.WriteUInt16(op.Opcode);
        bytes.WriteByte(op.TruePointer);
        bytes.WriteByte(op.FalsePointer);
        bytes.WriteBytes(op.Operand.data(), op.Operand.size());
    }
    return Files::Hash::CalculateHash(bytes.ToArray());
}

inline bool TS1BehaviorSignatureMatches(const Files::Formats::IFF::Chunks::BHAV& tree,
                                        uint32_t saved, bool allowHistoricalArguments) {
    if (saved == TS1BehaviorSignature(tree)) return true;
    // Earlier readers discarded legacy Args/Locals, and read the reserved
    // 8002 byte7 as part of Locals. Reproduce only that parser-owned identity,
    // never edits to live metadata or to code/type/branch/operand bytes.
    // Old files cannot bind declarations their decoder never recorded.
    if (!allowHistoricalArguments || !TS1HasHistoricalBehaviorMetadata(tree)) return false;
    return saved == TS1BehaviorSignature(tree, true);
}

inline size_t TS1LogicalLocals(bool original, size_t locals, size_t args) {
    return original ? locals : std::max(locals, args);
}
inline bool TS1SavedLocalsSizeValid(const Files::Formats::IFF::Chunks::BHAV& tree,
                                  size_t count, bool original, uint32_t version) {
    if (count == TS1LogicalLocals(original,tree.Locals,tree.Args)) return true;
    if (!original || version > 24 || count < tree.Locals) return false;
    // Preserve historical nonzero padding until its frame exits. No arbitrary
    // intermediate size is valid, and new TS1 frames allocate declared locals.
    if (count == std::max<size_t>(tree.Locals,tree.Args)) return true;
    return TS1HasHistoricalBehaviorMetadata(tree) && count ==
        std::max<size_t>(tree.PreviousDecoderLocals,TS1PreviousDecoderArgs(tree));
}
inline bool TS1ExpandHistoricalLocals(const Files::Formats::IFF::Chunks::BHAV& tree,
                                     std::vector<int16_t>& locals, bool authenticated, uint32_t version) {
    if (!authenticated || version > 24 || !TS1HasHistoricalBehaviorMetadata(tree) ||
        locals.size() >= tree.Locals || locals.size() !=
        std::max<size_t>(tree.PreviousDecoderLocals,TS1PreviousDecoderArgs(tree))) return false;
    // Only newly required entries were absent from the old decoder's bank.
    // Preserve its entire known prefix; never trim historical padded values.
    locals.resize(tree.Locals,0);
    return true;
}

}}}}
