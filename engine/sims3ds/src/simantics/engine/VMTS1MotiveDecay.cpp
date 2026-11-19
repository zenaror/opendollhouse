/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Entities.VMTS1MotiveDecay
#include "simantics/engine/VMTS1MotiveDecay.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/VMContext.h"
#include "simantics/VM.h"
#include "simantics/VMClock.h"
#include "content/ContentManager.h"
#include <algorithm>
namespace FSO { namespace SimAntics { namespace Entities {
using namespace FSO::SimAntics::Model;

const float VMTS1MotiveDecay::Constants[17] = {
    180, //energy span 0
    16, //wake hours 1
    7, //wake hour 2
    0.01f, //energy drift 3
    0.3f, //hunger to bladder 4
    0.0021f, //hunger decrement ratio 5
    0.055f, //social decrement base 6
    0.000125f, //social decrement multiplier 7
    0.25f, //ent dec awake 8
    1.0f, //ent mult asleep 9
    0.17f, //hyg decrement awake 10
    0.08f, //hyg decrement asleep 11
    0.3f, //blad decrement awake 12
    0.15f, //blad decrement asleep 13
    0.4f, //comfort decrement active 14
    0.6f, //comfort decrement lazy 15
    0.5f, //comfort decrement 16
};

const VMMotive VMTS1MotiveDecay::DecrementMotives[7] = {
    VMMotive::Hunger, VMMotive::Comfort, VMMotive::Hygiene, VMMotive::Bladder, VMMotive::Energy, VMMotive::Fun, VMMotive::Social
};

void VMTS1MotiveDecay::Tick(VMAvatar* avatar, VMContext* context) {
    const auto now = context->VM && context->VM->TS1
        ? avatar->TS1PersonTickCounter(context->Clock->Ticks) : context->Clock->Ticks;
    if (now < LastTick) { LastTick = now; return; }
    if (now - LastTick < 60) return;
    // The caller stamps even a bypassed update. Resume never catches up a
    // hidden or frozen person's missed intervals in a burst.
    LastTick = now;
    if (avatar->Dead || avatar->GetValue(VMStackObjectVariable::Hidden) != 0 ||
        avatar->GetPersonData(VMPersonDataVariable::Cheats) != 0) return;

    auto globals = Content::Content::Get()->WorldObjectGlobals.get();
    globals->InitCurves();
    const auto gender = avatar->GetPersonData(VMPersonDataVariable::Gender);
    const int species = (gender & 2) ? 1 : ((gender & 4) ? 2 : 0);
    const auto& t = globals->MotiveDecay[species];
    auto read = [&](VMMotive m) { return avatar->GetMotiveDataFloat(m); };
    auto write = [&](VMMotive m, float value) { avatar->SetMotiveDataFloat(m, value); };
    const bool sleeping = read(VMMotive::SleepState) < 0;
    auto energy = read(VMMotive::Energy);
    if (sleeping) {
        energy += species ? float(t.WakeHours) / 30.0f : (t.EnergySpan / float(24 - t.WakeHours)) / 30.0f;
        if (context->Clock->Hours > t.WakeHour &&
            int64_t(context->Clock->Hours) < int64_t(t.WakeHour) + t.WakeHours)
            energy -= t.EnergyDrift;
    } else {
        energy -= species ? t.EnergySpan / 30.0f : (t.EnergySpan / float(t.WakeHours)) / 30.0f;
    }
    write(VMMotive::Energy, energy);
    const float foodGain = read(VMMotive::Hunger) - avatar->GetOldMotiveDataFloat(VMMotive::Hunger);
    if (foodGain > 0) write(VMMotive::Bladder, read(VMMotive::Bladder) - foodGain * t.HungerToBladder);
    write(VMMotive::Comfort, read(VMMotive::Comfort) -
          (float(avatar->GetPersonData(VMPersonDataVariable::ActivePersonality)) *
           ((t.ComfortActive - t.ComfortLazy) * .001f) + t.ComfortLazy));
    write(VMMotive::Hunger, read(VMMotive::Hunger) - (read(VMMotive::Hunger) + 100.0f) * t.HungerRatio);
    if (!sleeping)
        write(VMMotive::Social, read(VMMotive::Social) -
              (float(avatar->GetPersonData(VMPersonDataVariable::OutgoingPersonality)) * t.SocialMultiplier + t.SocialBase));
    write(VMMotive::Fun, sleeping ? t.FunAsleepMultiplier * read(VMMotive::Fun) : read(VMMotive::Fun) - t.FunAwake);
    write(VMMotive::Hygiene, read(VMMotive::Hygiene) - (sleeping ? t.HygieneAsleep : t.HygieneAwake));
    write(VMMotive::Bladder, read(VMMotive::Bladder) - (sleeping ? t.BladderAsleep : t.BladderAwake));
    for (int i = 0; i < 16; ++i) write(VMMotive(i), std::max(-100.0f, std::min(100.0f, read(VMMotive(i)))));
    avatar->SnapshotMotiveData();

    // These are the DVD's HappyWeight record IDs, distinct from the nine
    // action-contribution curves. Preserve its value==13 pet exclusion.
    static const VMMotive moodMotives[] = {VMMotive::Hunger,VMMotive::Comfort,VMMotive::Hygiene,
        VMMotive::Bladder,VMMotive::Fun,VMMotive::Room,VMMotive::Social};
    const auto age = avatar->GetPersonData(VMPersonDataVariable::PersonsAge);
    const auto& curves = age != 0 && age < 18 ? globals->HappyWeightChild : globals->HappyWeight;
    if (curves.size() >= 7) {
        float sum = 0, weightSum = 0;
        for (int i = 0; i < 7; ++i) {
            const auto value = read(moodMotives[i]);
            if (avatar->GetPersonData(VMPersonDataVariable::Gender) >= 2 && value == 13.0f) continue;
            const auto weight = curves[i].GetPoint(value);
            sum += weight * value; weightSum += weight;
        }
        if (weightSum != 0) write(VMMotive::Mood, sum / weightSum);
    }
}
}}}
