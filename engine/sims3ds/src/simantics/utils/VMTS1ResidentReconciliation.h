/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <cstdint>

namespace FSO { namespace SimAntics { namespace Engine { namespace Utils {
enum class ResidentReconciliation { Keep, RemoveFormerResident, UnknownAffiliation };
struct SavedResidentAffiliation {
    bool Resident = false, CurrentMember = false;
    int16_t SavedFamily = 0;
    bool NeighbourKnown = false;
    int16_t NeighbourFamily = 0;
    bool CanonicalFamilyKnown = false, CanonicalMember = false;
    int16_t CanonicalHouse = 0;
};
// Species is deliberately absent: former household pets follow the same
// ownership rule, while current pets and visiting people are protected.
inline ResidentReconciliation ReconcileSavedResident(const SavedResidentAffiliation& s,
                                                     int16_t currentFamily, int16_t house) {
    if (!s.Resident || s.CurrentMember) return ResidentReconciliation::Keep;
    if (s.SavedFamily <= 0 || !s.NeighbourKnown || s.NeighbourFamily < 0)
        return ResidentReconciliation::UnknownAffiliation;
    if (s.NeighbourFamily == 0) return ResidentReconciliation::RemoveFormerResident;
    if (!s.CanonicalFamilyKnown || !s.CanonicalMember ||
        s.NeighbourFamily == currentFamily || s.CanonicalHouse < 0 || s.CanonicalHouse == house)
        return ResidentReconciliation::UnknownAffiliation;
    return ResidentReconciliation::RemoveFormerResident;
}
}}}}
