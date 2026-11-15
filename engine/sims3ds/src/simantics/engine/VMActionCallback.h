/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Engine.VMActionCallback
#pragma once
namespace FSO { namespace SimAntics { namespace Engine { namespace Utils { class InteractionCheckpointCodec; }}}}
#include <cstdint>
namespace FSO { namespace SimAntics {
class VM;
class VMEntity;
namespace Engine {
class VMActionCallback {
public:
    VMActionCallback(FSO::SimAntics::VM* vm, int16_t interactionNumber, VMEntity* target, VMEntity* stackObj, VMEntity* caller, bool paramAsObjectID, bool isTree); //type 1: interaction callback
    //type 2 will be function callback.
    void Run(VMEntity* cbOwner);
private:
    friend class FSO::SimAntics::Engine::Utils::InteractionCheckpointCodec;
    int32_t type;
    //type 1 variables
    VMEntity* Target;
    int16_t Interaction;
    bool SetParam;
    FSO::SimAntics::VM* vm;
    VMEntity* StackObject;
    VMEntity* Caller;
    bool IsTree; //true if we're calling a tree instead of an interaction number
};
}}}
