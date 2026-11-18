/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// Translated from FreeSims SimsVille/SimsAntics/primitives/VMFindBestAction.cs
#include "simantics/primitives/VMFindBestAction.h"
#include "simantics/primitives/VMTS1AutonomyPolicy.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMSimanticsException.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/model/VMMotive.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "files/formats/iff/chunks/TTAB.h"
#include "core/io.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMQueuedAction.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "simantics/model/VMStackObjectVariable.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"

namespace FSO { namespace SimAntics { namespace Primitives {
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Model;
using FSO::Files::Formats::IFF::Chunks::TTAB;
using FSO::LotView::Model::LotTilePos;

// ===================================================================================================================
// TS1(sims): The Sims 1 autonomy, translated from FreeSO tso.simantics/Primitives/VMFindBestAction.cs
// (FreeSims' version below is a TSO-style placeholder). FreeSO's notes:
//
// How autonomy works in The Sims:
//
// Each interaction has "motive advertisements" which determine how many autonomy points are given for each interaction.
//
// First, each motive is converted into an "effective motive" using the interaction contribution curves.
// These curves generally make motive changes more evident at lower values, and cap them at a specific value.
// This prevents people from considering sleeping at 50% energy, but also allows other motives like fun to use a much higher cap.
// "Happy" is calculated as an average of all "effective motive"s, including mood.
// The selected Complete Collection DVD averages the nine curves equally.
//
// For each interaction, we only evaluate it if it has the following properties:
// - positive autonomy threshold; a check tree may supply initially empty advertisements
// - object is not burning
// - advertisement minima contribute a fixed amount; personality scales only the delta
// Interaction check trees are evaluated to see if they are usable. "Auto" is passed to the tree as 1 in param 0, indicating this is an autonomous check.
// When evaluating an interaction's check tree, it may change the motive ad range and minimum.
//
// For each interaction a new Happy score is calculated based on the motive deltas applied to the sim's current motives.
// DVD action scoring uses (minimum + delta * personality / 1000) / 1000.
// Without a personality selector it uses (minimum + delta) / 1000.
// Remember to re-apply the interaction curves to the new motives here.
//
// Attenuation is very simple: Score / (1 + (Attenuation* Distance)). It is applied to the *distance from base happy*, which results in the final score.
// Individual motive scores can be negative to discourage use of interactions at high motives.These are combined with positives from other motives.
//
// Final scores below certain values are ignored:
// 1E-07 for visitors and family, 1E-06 for sims who are sitting. These constants are in FCNS 2 in global.iff.
// That means negative scores are ignored. Motive ads when you're in the flat part of the curve are ignored.
//
// sims additions (marked below): objects whose "lockout count" (object data 25) is non-zero are skipped — see
// STUBS_autonomy.md; FreeSO left this as a TODO.
// ===================================================================================================================

const VMPersonDataVariable VMFindBestAction::VaryByTypes[23] = {
    VMPersonDataVariable::UnusedAndDoNotUse, //0
    VMPersonDataVariable::NicePersonality,
    VMPersonDataVariable::NicePersonality, //inverted
    VMPersonDataVariable::ActivePersonality,
    VMPersonDataVariable::ActivePersonality, //inverted
    VMPersonDataVariable::GenerousPersonality,
    VMPersonDataVariable::GenerousPersonality, //inverted
    VMPersonDataVariable::PlayfulPersonality,
    VMPersonDataVariable::PlayfulPersonality, //inverted
    VMPersonDataVariable::OutgoingPersonality,
    VMPersonDataVariable::OutgoingPersonality, //inverted
    VMPersonDataVariable::NeatPersonality,
    VMPersonDataVariable::NeatPersonality, //inverted

    VMPersonDataVariable::SkillEfficiency, // original selector 13
    VMPersonDataVariable::CookingSkill, // cooking skill
    VMPersonDataVariable::CharismaSkill, // social skill *
    VMPersonDataVariable::MechanicalSkill, // repair skill *
    VMPersonDataVariable::UnusedAndDoNotUse3, // original selector 17
    VMPersonDataVariable::UnusedAndDoNotUse3, // original selector 18
    VMPersonDataVariable::CreativitySkill, // creativity skill
    VMPersonDataVariable::UnusedAndDoNotUse3, // original selector 20
    VMPersonDataVariable::BodySkill, // physical skill *
    VMPersonDataVariable::LogicSkill // logic skill
};

const VMMotive VMFindBestAction::WeightMotives[9] = {
    VMMotive::Energy,
    VMMotive::Comfort,
    VMMotive::Hunger,
    VMMotive::Hygiene,
    VMMotive::Bladder,
    VMMotive::Mood,
    VMMotive::Room,
    VMMotive::Social,
    VMMotive::Fun
};

std::function<void(VMEntity*, VMEntity*, const std::string&, bool, float)> VMFindBestAction::DebugCandidate;

int VMFindBestAction::MotiveToWeight(int motive) {
    for (int i = 0; i < 9; i++) if ((int)WeightMotives[i] == motive) return i;
    return -1;
}

VMPrimitiveExitCode VMFindBestAction::ExecuteTS1(VMStackFrame* context) {
    auto callerAvatar = dynamic_cast<VMAvatar*>(context->Caller);
    if (callerAvatar == nullptr) throw VMSimanticsException("Find Best Action: caller is not an avatar (invalid cast).", context);
    // DVD opcode3 selects an interaction; pending player commands are handled
    // by Idle For Input, not substituted for an autonomous selection here.
    const auto personType = callerAvatar->GetPersonData(VMPersonDataVariable::PersonType);
    const bool visitor = personType != 0;
    if (!context->VM()->FreeWillEnabled && personType != 1 && !callerAvatar->IsPet())
        return VMPrimitiveExitCode::GOTO_FALSE;

    auto ents = std::vector<VMEntity*>(context->VM()->Entities); // C#: ObjectQueries.WithAutonomy (objects with auto interactions)
    std::unordered_set<int16_t> processed;
    auto caller = callerAvatar;
    auto pos1 = caller->Position();

    const auto age = caller->GetPersonData(VMPersonDataVariable::PersonsAge);
    const bool child = age > 0 && age < 18;
    auto attenTable = visitor ? TTAB::VisitorAttenuationValues : TTAB::TS1AttenuationValues;
    auto global = FSO::Content::Content::Get()->WorldObjectGlobals.get();
    global->InitCurves();
    auto& interactionCurve = child ? global->InteractionScoreChild : global->InteractionScore;
    if (interactionCurve.size() < 9) return VMPrimitiveExitCode::GOTO_FALSE; // no Global.iff curves

    auto isStray = caller->IsPet() && context->VM()->TS1 && caller->GetPersonData(VMPersonDataVariable::GreetStatus) == 0 && caller->GetPersonData(VMPersonDataVariable::PersonType) == 1;

    // === HAPPY CALCULATION ===

    // The selected DVD scorer averages these nine curves equally.
    float weights[9] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    float totalWeight = 0;
    for (int i = 0; i < 9; i++) totalWeight += weights[i];
    for (int i = 0; i < 9; i++) weights[i] /= totalWeight;
    const auto& tuning = global->Autonomy;
    const double minScore = visitor ? tuning.VisitorMinimum : tuning.FamilyMinimum;
    // DVD 004074bc tests the current lot's registered zoning for residential.
    // Its lookup treats lot zero as residential and an unknown nonzero lot as
    // undefined. The native cache encodes residential/community as 0/1.
    const auto lot = context->VM()->GetGlobalValue(10);
    bool residential = lot == 0;
    if (lot != 0) {
        const auto hood = FSO::Content::Content::Get()->Neighborhood.get();
        if (hood) {
            const auto zone = hood->ZoningDictionary.find(lot);
            residential = zone != hood->ZoningDictionary.end() && zone->second == 0;
        }
    }

    float happyParts[9];
    float baseHappy = 0;
    for (int i = 0; i < 9; i++) {
        happyParts[i] = interactionCurve[i].GetPoint(caller->GetMotiveDataFloat(WeightMotives[i])) * weights[i];
        baseHappy += happyParts[i];
    }

    std::vector<VMPieMenuInteraction> validActions;
    for (auto iobj : ents) {
        if (iobj->Dead || iobj->Position() == LotTilePos::OUT_OF_WORLD) continue;
        auto obj = iobj->MultitileGroup->GetInteractionGroupLeader(iobj);
        // DVD 0045dea5 uses person data65 (sex/species), independently of
        // resident/visitor person type, to exclude human self-interactions.
        if (obj == caller && caller->GetPersonData(VMPersonDataVariable::Gender) < 2) continue;
        auto gobj = dynamic_cast<VMGameObject*>(obj);
        if (processed.count(obj->ObjectID) || (gobj != nullptr && (int)gobj->Disabled > 0)) continue;
        processed.insert(obj->ObjectID);

        // TS1(sims): lockout count. The Sims 1 trees set the used/failed object's "lockout count" (object data 25,
        // e.g. global 'Standard Exit' := param, 'fail food chain object' := 200) and the engine counts it down every
        // tick (VMEntity::Tick); FreeSO's Find Best Object For Function skips locked out objects, its Find Best Action
        // has it as a TODO. Without it Sims re-choose the interaction they just finished or failed.
        if (obj->GetValue(VMStackObjectVariable::LockoutCount) > 0) continue;

        if (isStray) {
            if (gobj != nullptr) continue;
            //determine if the object is indoors
            auto roomID = context->VM()->Context->GetObjectRoom(obj);
            roomID = (uint16_t)std::max(0, std::min((int)context->VM()->Context->RoomInfo.size() - 1, (int)roomID));
            auto& room = context->VM()->Context->RoomInfo[roomID];
            auto outside = room.Room.IsOutside;
            if (!outside) continue;
        }
        auto pos2 = obj->Position();
        auto distance = (float)std::sqrt(std::pow(pos1.x - pos2.x, 2) + std::pow(pos1.y - pos2.y, 2) + std::pow((pos1.Level - pos2.Level) * 320, 2.0)) / 16.0f;
        // DVD 004584fa tests bit9 of object data8 (Burning), not bit5
        // (Occupied). Occupancy/join eligibility belongs to the authored check.
        if (obj->GetFlag(VMEntityFlags::Burning)) continue;

        if (obj->TreeTable == nullptr) continue;
        // DVD 00458992 visits the TTAB from its last row to its first. Checks
        // can change advertisements and consume random numbers, so this order
        // matters before candidate scoring as well as when scores tie.
        const auto& autoEntries = obj->TreeTable->AutoInteractions;
        for (auto entryIt = autoEntries.rbegin(); entryIt != autoEntries.rend(); ++entryIt) {
            auto entry = *entryIt;
            auto id = entry->TTAIndex;
            // The original compares the signed TTAB threshold against the
            // Sim's script-controlled autonomy level, including equality.
            const auto threshold = static_cast<int16_t>(entry->AutonomyThreshold);
            if (threshold <= 0 || threshold > caller->GetPersonData(VMPersonDataVariable::AutonomyLevel)) continue;

            auto action = obj->GetAction((int32_t)id, caller, context->VM()->Context);
            auto ttas = obj->TreeTableStrings;

            caller->ObjectData[(int)VMStackObjectVariable::HideInteraction] = 0;
            auto actionStrings = caller->Thread->CheckAction(action, true);

            std::vector<VMPieMenuInteraction> pie;
            if (actionStrings != nullptr) {
                if (actionStrings->size() > 0) {
                    for (auto& actionS : *actionStrings) {
                        if (actionS.Name.empty()) actionS.Name = (ttas != nullptr) ? ttas->GetString((int32_t)id) : std::string("***MISSING***");
                        actionS.ID = (uint8_t)id;
                        actionS.Entry = entry;
                        actionS.Global = false;
                        pie.push_back(actionS);
                    }
                } else {
                    if (ttas != nullptr) {
                        VMPieMenuInteraction item;
                        item.Name = ttas->GetString((int32_t)id);
                        item.ID = (uint8_t)id;
                        item.Entry = entry;
                        item.Global = false;
                        pie.push_back(item);
                    }
                }
            }
            if (pie.size() == 0 && DebugCandidate) DebugCandidate(caller, obj, (ttas != nullptr) ? ttas->GetString((int32_t)id) : std::string("?"), false, 0);
            if (pie.size() > 0) {
                auto& first = pie[0];
                // calculate score for this tree.
                // start with the base happy value, and modify it for each motive changed.
                float score = baseHappy;
                for (int weightInd = 0; weightInd < 9; ++weightInd) {
                    const auto motiveI = int(WeightMotives[weightInd]);
                    int16_t min = 0, max = 0, personality = 0;
                    if (motiveI < int(entry->MotiveEntries.size())) {
                        const auto& motiveScore = entry->MotiveEntries[motiveI];
                        min = motiveScore.EffectRangeMinimum;
                        max = motiveScore.EffectRangeMaximum;
                        personality = int16_t(motiveScore.PersonalityModifier);
                    }
                    if (first.MotiveAdChanges != nullptr) {
                        // FIX(sims): C# TryGetValue(key, out min) zeroes the value when the key is missing, so a
                        // check tree that changed one ad wiped all the others. Only replace what the tree set.
                        auto& ads = *first.MotiveAdChanges;
                        auto f = ads.find((0 << 16) | motiveI); if (f != ads.end()) min = f->second;
                        f = ads.find((1 << 16) | motiveI); if (f != ads.end()) max = f->second;
                        f = ads.find((2 << 16) | motiveI); if (f != ads.end()) personality = f->second;
                    }

                    // DVD 0045869b..004586ba adjusts the advertisement after
                    // the check tree: another room replaces only Room's delta
                    // with the 16-bit negation of its (possibly changed) minimum.
                    // Keep the personality selector and all other motives.
                    if (motiveI == int(VMMotive::Room) &&
                        caller->GetValue(VMStackObjectVariable::Room) !=
                            obj->GetValue(VMStackObjectVariable::Room)) {
                        const int negated = -int(min);
                        max = negated == 32768 ? int16_t(-32768) : int16_t(negated);
                    }

                    if (min == 0 && max == 0) continue;
                    auto myMotive = caller->GetMotiveDataFloat((VMMotive)motiveI);

                    // Only changed contributions need replacing in the average.
                    score -= happyParts[weightInd];

                    float personalityMul = 1;
                    if (personality > 0 && personality < 23) {
                        personalityMul = caller->GetPersonData(VaryByTypes[personality]);
                        personalityMul /= 1000.0f;
                        if (personality < 13) {
                            if ((personality & 1) == 0) {
                                personalityMul = 1 - personalityMul;
                            }
                        }
                    }

                    // then add the new contribution for this motive.
                    score += interactionCurve[weightInd].GetPoint(myMotive + (min + max * personalityMul) / 1000.0f) * weights[weightInd];
                }

                // score relative to base
                score -= baseHappy;
                // modify score using attenuation
                float atten = (entry->AttenuationCode == 0 || entry->AttenuationCode >= 5) ?
                    entry->AttenuationValue : attenTable[entry->AttenuationCode];

                score = score / (1 + atten * distance);
                if (DebugCandidate) DebugCandidate(caller, obj, first.Name, true, score);

                if (score >= minScore) {
                    for (auto& item : pie) {
                        item.Score = score;
                        item.Callee = obj;
                        // FIX(sims): C# adds `first` once per action string (so only the first sub-option of a
                        // social could ever be chosen); add each string, carrying its own name/param.
                        validActions.push_back(item);
                    }
                }
            }
            //TODO: Lockout interactions that have been used before for a few sim hours (in ts1 ticks. same # of ticks for tso probably)
            //TODO: special logic for socials?
        }
    }
    TS1Autonomy::SortAutonomyRecords(validActions, [](const VMPieMenuInteraction& a, const VMPieMenuInteraction& b) {
        // DVD 0045e467 subtracts float operands in x87 precision and compares
        // against the single-precision 1e-7 constant, without secondary keys.
        const double difference = double(b.Score) - double(a.Score);
        const double tolerance = double(1e-7f);
        return difference > tolerance ? 1 : (difference < -tolerance ? -1 : 0);
    });
    // DVD 0045dea5 uses a uniform choice among the best N records. Logic
    // reduces N by one for each 2000 skill points; the selected Global sets N.
    const auto count = static_cast<int>(std::min<int64_t>(validActions.size(),
        std::max<int64_t>(1, int64_t(tuning.RandomSelectionCount) -
            caller->GetPersonData(VMPersonDataVariable::LogicSkill) / 2000)));
    auto sorted = std::vector<VMPieMenuInteraction>(validActions.begin(), validActions.begin() + count);
    if (sorted.empty()) return VMPrimitiveExitCode::GOTO_FALSE;
    auto selection = sorted[0];
    // On residential lots, visitors take the first sorted record. Family Sims
    // do so only when that record has AutoFirst. Community lots always draw.
    if (!residential || (!visitor && !selection.Entry->AutoFirst()))
        selection = sorted[context->VM()->Context->NextRandom(static_cast<int>(sorted.size()))];
    // Sitting is checked after the lottery in the DVD. A weak chosen action
    // leaves the Sim idle; filtering it before drawing changes the lottery.
    if (caller->GetPersonData(VMPersonDataVariable::Posture) != 0 &&
        selection.Score < tuning.SittingMinimum)
        return VMPrimitiveExitCode::GOTO_FALSE;


    // DVD 0045e3e8 stores only the TTAB index and frame target. Scripts can
    // reset the idle pose before opcode30 resolves and executes the action.
    // Both fields are already part of the classic and native saved state.
    caller->SetValue(VMStackObjectVariable::TreeTableEntry, static_cast<int16_t>(selection.ID));
    context->StackObject = selection.Callee;
    context->StackObjectRawID = selection.Callee->ObjectID;
    if (context->VM()->OnAutonomyChoice) context->VM()->OnAutonomyChoice(context->Caller, sorted, selection);
    return VMPrimitiveExitCode::GOTO_TRUE;
}

VMPrimitiveExitCode VMFindBestAction::Execute(VMStackFrame* context, VMPrimitiveOperand* /*args*/) {
    if (context->VM()->TS1) return ExecuteTS1(context); // TS1(sims)
    auto ents = std::vector<VMEntity*>(context->VM()->Entities);
    auto processed = std::unordered_set<int16_t>();
    auto pos1 = context->Caller->Position();

    // C#: ((VMAvatar)context.Caller) — an invalid cast throws.
    auto callerAvatar = dynamic_cast<VMAvatar*>(context->Caller);

    VMEntity* bestActor = nullptr;
    VMPieMenuInteraction bestAction;
    bool hasBestAction = false; // C#: bestAction != null
    int32_t bestScore = 0;
    for (auto iobj : ents) {
        auto obj = iobj->MultitileGroup->GetInteractionGroupLeader(iobj);
        if (processed.count(obj->ObjectID)) continue;
        processed.insert(obj->ObjectID);

        auto pos2 = obj->Position();
        auto distance = (int16_t)std::floor(std::sqrt(std::pow(pos1.x - pos2.x, 2) + std::pow(pos1.y - pos2.y, 2)) / 16.0);

        auto pie = obj->GetPieMenu(context->VM(), context->Caller, true);
        int32_t bestMyScore = 0;
        const VMPieMenuInteraction* bestMyAction = nullptr;
        for (auto& item : pie) {
            //motive scores must be above their threshold.
            //pick maximum motive score as our base score
            int32_t baseScore = 0;
            for (size_t i = 0; i < item.Entry->MotiveEntries.size(); i++) {
                auto& motiveScore = item.Entry->MotiveEntries[i];
                if (motiveScore.EffectRangeMaximum == 0 && motiveScore.EffectRangeMinimum == 0) continue;
                //LINEAR INTERPOLATE MIN SCORE TO MAX, using motive data of caller
                //MAX is when motive is the lowest.
                //can be in reverse too!
                if (callerAvatar == nullptr) throw VMSimanticsException("Find Best Action: caller is not an avatar (invalid cast).", context);
                auto myMotive = std::max((int16_t)0, callerAvatar->GetMotiveData((VMMotive)i)); //below mid motives causes max atten right now.

                int32_t interpScore = (myMotive * motiveScore.EffectRangeMinimum + (100 - myMotive) * (motiveScore.EffectRangeMinimum + motiveScore.EffectRangeMaximum)) / 100;
                if (interpScore > baseScore) baseScore = interpScore;
            }
            if (item.Entry->AttenuationCode != 0 && item.Entry->AttenuationCode >= 5) throw VMSimanticsException("Find Best Action: attenuation code out of range.", context); // C# IndexOutOfRangeException
            float atten = (item.Entry->AttenuationCode == 0) ? item.Entry->AttenuationValue : TTAB::AttenuationValues[item.Entry->AttenuationCode];
            int32_t attenScore = (int32_t)std::max(0.0f, baseScore * (1.0f - (distance * atten)));
            if (attenScore != 0) attenScore += (int32_t)context->VM()->Context->NextRandom(31) - 15;
            // C# compares int with uint as long.
            if ((int64_t)attenScore > (int64_t)item.Entry->AutonomyThreshold && attenScore > bestMyScore) {
                bestMyScore = attenScore;
                bestMyAction = &item;
            }
        }

        if (bestMyAction != nullptr && bestMyScore > bestScore) {
            bestActor = obj;
            bestAction = *bestMyAction;
            hasBestAction = true;
            bestScore = bestMyScore;
        }
    }

    if (bestActor != nullptr && hasBestAction) {
        std::vector<int16_t> pushArgs = { bestAction.Param0, 0, 0, 0 };
        bestActor->PushUserInteraction(bestAction.ID, context->Caller, context->VM()->Context, &pushArgs);
        return VMPrimitiveExitCode::GOTO_TRUE;
    }

    return VMPrimitiveExitCode::GOTO_FALSE; //we couldn't find anything... because we didn't check! TODO!!
}

void VMFindBestActionOperand::Read(const uint8_t* bytes) {
    st::Reader io(bytes, 8, st::Endian::Little);
    (void)io;
}

void VMFindBestActionOperand::Write(uint8_t* /*bytes*/) { }

}}}
