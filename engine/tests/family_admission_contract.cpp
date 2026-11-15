// SPDX-License-Identifier: MPL-2.0
// Exercise the actual generic family primitives against synthetic NBRS/FAMI.
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "files/formats/iff/IffFile.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMTS1Neighbourhood.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/primitives/VMGenericTSOCall.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() { try {
    auto content = FSO::Content::Content::Get();
    content->Neighborhood = std::make_unique<FSO::Content::TS1::TS1NeighborhoodProvider>(content);
    auto& provider = *content->Neighborhood;
    provider.MainResource = std::make_unique<IffFile>();
    NBRS neighbors;
    provider.Neighbors = &neighbors;
    const auto family = [&](int id) {
        auto* f = new FAMI(); f->ChunkID = id; f->ChunkType = "FAMI"; f->ChunkProcessed = true;
        provider.MainResource->AddChunk(f); return f;
    };
    auto* home = family(1); auto* other = family(2);
    constexpr uint32_t guest = 0xf0123499;
    auto n = std::make_unique<Neighbour>(); n->NeighbourID = 99; n->GUID = guest;
    n->PersonData.assign(80, 0); n->PersonData[61] = 2;
    auto* person = n.get(); neighbors.NeighbourByID[99] = person; neighbors.Entries.push_back(std::move(n));
    VMContext context(nullptr); VM vm(&context, nullptr); vm.TS1 = true; vm.GlobalState.assign(38, 0);
    VMThread thread(&context, nullptr, 0); VMStackFrame frame; frame.Thread = &thread; frame.StackObjectRawID = 99;
    VMGenericTSOCall primitive; VMGenericTSOCallOperand operand;
    const auto call = [&](int mode) { operand.Call = static_cast<Model::VMGenericTSOCallMode>(mode); return primitive.Execute(&frame, &operand); };
    provider.GameState.ActiveFamily = home;
    for (uint32_t i = 1; i <= 8; ++i) home->FamilyGUIDs.push_back(i);
    home->RuntimeSubset = {1}; other->FamilyGUIDs = {guest}; other->RuntimeSubset = {guest};
    auto full = home->FamilyGUIDs;
    require(call(4) == VMPrimitiveExitCode::GOTO_FALSE, "ninth member was accepted");
    require(home->FamilyGUIDs == full && home->RuntimeSubset == std::vector<uint32_t>{1}
        && other->FamilyGUIDs == std::vector<uint32_t>{guest} && other->RuntimeSubset == std::vector<uint32_t>{guest}
        && person->PersonData[61] == 2 && provider.DirtyAvatars.empty(), "refused admission changed family state");
    home->FamilyGUIDs.pop_back();
    require(call(4) == VMPrimitiveExitCode::GOTO_TRUE && home->FamilyGUIDs.size() == 8
        && other->FamilyGUIDs.empty() && other->RuntimeSubset.empty() && person->PersonData[61] == 1,
        "valid eighth-member transfer failed");
    const auto admitted = home->FamilyGUIDs; const auto runtime = home->RuntimeSubset;
    require(call(4) == VMPrimitiveExitCode::GOTO_TRUE && home->FamilyGUIDs == admitted && home->RuntimeSubset == runtime,
        "replayed admission duplicated a member");
    provider.DirtyAvatars.clear(); thread.TempRegisters[0] = 42;
    require(call(33) == VMPrimitiveExitCode::GOTO_FALSE && home->FamilyGUIDs == admitted
        && person->PersonData[61] == 1 && provider.DirtyAvatars.empty(), "missing destination removed a resident");
    other->FamilyGUIDs = {10,11,12,13,14,15,16,17}; thread.TempRegisters[0] = 2;
    require(call(33) == VMPrimitiveExitCode::GOTO_FALSE && home->FamilyGUIDs == admitted
        && other->FamilyGUIDs.size() == 8 && person->PersonData[61] == 1, "full destination modified membership");
    other->FamilyGUIDs.pop_back();
    require(call(33) == VMPrimitiveExitCode::GOTO_TRUE && other->FamilyGUIDs.size() == 8
        && std::find(other->FamilyGUIDs.begin(),other->FamilyGUIDs.end(),guest) != other->FamilyGUIDs.end()
        && std::find(home->FamilyGUIDs.begin(),home->FamilyGUIDs.end(),guest) == home->FamilyGUIDs.end()
        && person->PersonData[61] == 2, "transfer did not use the actual destination");
    const auto moved = other->FamilyGUIDs;
    require(call(33) == VMPrimitiveExitCode::GOTO_TRUE && other->FamilyGUIDs == moved, "destination replay duplicated member");
    frame.StackObjectRawID = 98;
    require(call(6) == VMPrimitiveExitCode::GOTO_FALSE && other->FamilyGUIDs == moved, "missing resident was reported removed");
    frame.StackObjectRawID = 99;
    require(call(6) == VMPrimitiveExitCode::GOTO_TRUE && person->PersonData[61] == 0 && other->FamilyGUIDs.size() == 7,
        "normal removal failed");
    provider.GameState.ActiveFamily = nullptr; vm.SetGlobalValue(Utils::VMTS1Neighbourhood::GLOBAL_CURRENT_FAMILY,0);
    require(call(4) == VMPrimitiveExitCode::GOTO_FALSE, "missing current family accepted admission");
    other->FamilyGUIDs.push_back(guest); person->PersonData[61] = 2;
    provider.DirtyAvatars.clear();
    require(call(6) == VMPrimitiveExitCode::GOTO_FALSE && person->PersonData[61] == 2
        && other->FamilyGUIDs == moved && provider.DirtyAvatars.empty(),
        "missing current family accepted removal or changed state");
    std::cout << "family membership: capacity, rejected-state preservation, transfers, replay and removal passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
