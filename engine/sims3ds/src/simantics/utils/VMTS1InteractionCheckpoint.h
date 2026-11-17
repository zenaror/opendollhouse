// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <vector>

namespace FSO { namespace Files { namespace Formats { namespace IFF {
class IffFile;
class IffRawFile;
}}}}
namespace FSO { namespace SimAntics {
class VM;
namespace Engine { namespace Utils {
class VMTS1InteractionCheckpoint {
public:
    static void PresentDialogs(VM& vm);
    static void RestoreClassic(VM& vm, Files::Formats::IFF::IffFile& house);
    // Only a freshly synchronized, validated ObjM can carry authoritative
    // manager words. Direct execution overlays retain the v8 compatibility path.
    static void Capture(VM& vm, Files::Formats::IFF::IffRawFile& house, bool departing,
                        bool schedulerWordsBound = false, uint32_t outputVersion = 12);
    // Verify v11 SIMI binding before publishing globals or constructing a lot.
    // Structural preflight only: never proves origin or admits a legacy candidate.
    static bool PreflightLegacyBudget(Files::Formats::IFF::IffFile& house);
    static void ValidateFinanceBinding(Files::Formats::IFF::IffFile& house);
    static bool IsDeparture(Files::Formats::IFF::IffFile& house);
    static void Restore(VM& vm, Files::Formats::IFF::IffFile& house, bool resumeDispatch = false,
                        bool validateOnly = false, int32_t* legacyPurse = nullptr,
                        bool previewUnpublishedPortals = false);
};
}}
}}
