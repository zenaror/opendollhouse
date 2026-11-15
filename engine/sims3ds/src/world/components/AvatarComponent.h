/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.LotView.Components.AvatarComponent (World/components/AvatarComponent.cs)
// The animated Vitaboy skeleton supplies the same live hand/head attachment
// anchors used by the avatar mesh and the previous desktop renderer.
#pragma once
#include <cstdint>
#include "world/components/EntityComponent.h"
#include "vitaboy/Appearance.h"
#include "vitaboy/SimAvatar.h"

namespace FSO { namespace LotView { namespace Components {

enum class AvatarDisplayFlags : int { Normal = 0, ShowAsGhost = 1 };

class AvatarComponent : public EntityComponent {
public:
    FSO::Vitaboy::SimAvatar* Avatar = nullptr;
    float RadianDirection = 0;
    AvatarDisplayFlags DisplayFlags = AvatarDisplayFlags::Normal;
    bool IsPet = false;
    /// TS1(sims): person data CensorshipFlags (FreeSO VMCensorshipFlags: 1 pelvis, 2 spine if female, 4 head,
    /// 8/16 left/right hand, 32/64 left/right foot, 128 full body), set by the VM; World draws the TS1 mosaic.
    /// The VM clears bit 2 for male Sims.
    int32_t CensorFlags = 0;

    st::Vector3 DrawPosition() override {
        if (Container == nullptr) {
            // FreeSO AvatarComponent: AltitudeOff = InterpAltitude(_Position) (avatar positions are tile centres)
            if (AltitudeSource == nullptr) return Position;
            return st::Vector3(Position.X, Position.Y, Position.Z + AltitudeAt(Position, 0.0f));
        }
        return Container->GetSLOTPosition(ContainerSlot) + st::Vector3(0.5f, 0.5f, IsPet ? 0.0f : -1.4f);
    }
    std::optional<st::Vector3> GetLookTarget() override {
        auto head = Avatar && Avatar->Skeleton ? Avatar->Skeleton->GetBone("HEAD") : nullptr;
        if (!head || !Avatar->Skeleton->GetBone(head->ParentName)) return {};
        // Both pins derive the actual head offset, including child/pet rigs,
        // rather than assuming a fixed adult height. Swap axes before Z turn.
        const auto& h = head->AbsolutePosition;
        const auto offset = FSO::Vitaboy::Transform(st::Vector3(h.X/3,h.Z/3,h.Y/3),
            FSO::Vitaboy::Matrix::CreateRotationZ(RadianDirection + 3.14159265358979323846f));
        const auto p = DrawPosition();
        return st::Vector3(p.X+offset.X,p.Z+offset.Z,p.Y+offset.Y);
    }
    // TS1 cXPerson slots 2..4 have zero offsets and no bone binding. The
    // hand and head use the current animated pose, as in both FreeSO pins.
    st::Vector3 GetSLOTPosition(int32_t slot) override {
        const auto position = DrawPosition();
        if (slot >= 2 && slot <= 4) return position;
        const char* name = slot == 0 ? "R_FINGER0" : slot == 1 ? "HEAD" : nullptr;
        const auto bone = name && Avatar && Avatar->Skeleton ? Avatar->Skeleton->GetBone(name) : nullptr;
        if (!bone) return position;
        const auto& p = bone->AbsolutePosition;
        const auto offset = FSO::Vitaboy::Transform(st::Vector3(p.X/3,p.Z/3,p.Y/3),
            FSO::Vitaboy::Matrix::CreateRotationZ(RadianDirection + 3.14159265358979323846f));
        return position + offset - st::Vector3(0.5f,0.5f,0);
    }
};

}}}
