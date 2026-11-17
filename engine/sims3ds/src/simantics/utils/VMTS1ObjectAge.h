// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "simantics/entities/VMEntity.h"
#include "simantics/VMClock.h"
#include "simantics/model/VMStackObjectVariable.h"
namespace FSO { namespace SimAntics { namespace Utils {
inline int16_t TS1AgeShort(int32_t value) {
    const uint16_t bits=uint16_t(value);
    return bits<=0x7fff?int16_t(bits):int16_t(int32_t(bits)-65536);
}
// DVD432396: these are object calendar fields, not a person's life-stage data.
inline void RefreshTS1ObjectAge(VMEntity& entity,const VMClock& clock) {
    using V=Model::VMStackObjectVariable;
    int16_t years=TS1AgeShort(clock.Year-entity.GetValue(V::BirthYear));
    int16_t months=TS1AgeShort(clock.Month-entity.GetValue(V::BirthMonth));
    if(TS1AgeShort(clock.DayOfMonth-entity.GetValue(V::BirthDay))<0)months=TS1AgeShort(int(months)-1);
    if(months<0){months=TS1AgeShort(int(months)+12);years=TS1AgeShort(int(years)-1);}
    entity.SetValue(V::SimAge,years);entity.SetValue(V::AgeInMonths,TS1AgeShort(years*12+months));
}
// DVD431ea2 is for fresh creation, including objects outside the world.
// Deserialized instances must retain the birth fields supplied by their file.
inline void StampTS1ObjectCreation(VMEntity& entity,const VMClock& clock) {
    using V=Model::VMStackObjectVariable;
    entity.SetValue(V::BirthYear,TS1AgeShort(clock.Year));
    entity.SetValue(V::BirthMonth,TS1AgeShort(clock.Month));
    entity.SetValue(V::BirthDay,TS1AgeShort(clock.DayOfMonth));
    entity.SetValue(V::BirthHour,TS1AgeShort(clock.Hours));
    entity.SetValue(V::BirthMinutes,TS1AgeShort(clock.Minutes));
}
}}}
