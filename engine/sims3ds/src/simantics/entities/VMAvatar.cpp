/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.VMAvatar (entities/VMAvatar.cs)
#include "simantics/entities/VMAvatar.h"
#include "hit/HITThread.h"
#include "hit/HITVM.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMTS1MotiveDecay.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/engine/Routing/VMTS1Locomotion.h"
#include "simantics/engine/VMTS1Neighbourhood.h"
#include "files/formats/iff/chunks/CTSS.h"
#include "content/TS1PersonalText.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "common/utils/DirectionUtils.h"
#include "content/ContentManager.h"
#include "vitaboy/content/TS1AvatarProvider.h" // HOOK(vitaboy)
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <algorithm>
#include <set>

#if __has_include("simantics/primitives/VMFindLocationFor.h")
#include "simantics/primitives/VMFindLocationFor.h"
#define ST_HAVE_FINDLOCATIONFOR 1
#endif

namespace FSO { namespace SimAntics {
using namespace FSO::Files::Formats::IFF::Chunks;
using namespace FSO::SimAntics::Model;
using namespace FSO::LotView::Model;
using FSO::LotView::Components::AvatarComponent;
using FSO::LotView::Components::ObjectComponent;
using FSO::Common::Utils::DirectionUtils;
using FSO::Vitaboy::AppearanceType;

uint32_t VMAvatar::TEMPLATE_PERSON = 0x7FD96B54;
uint32_t VMAvatar::TEMPLATE_NPC = 0x7FD96B56;
uint32_t VMAvatar::NPC_MAID = 0xC61931DC;
uint32_t VMAvatar::NPC_GARDENER = 0xD62B91B4;
uint32_t VMAvatar::DOG_TEMPLATE = 0x3AC5D780;
uint32_t VMAvatar::CAT_TEMPLATE = 0x3762D78D;
const int32_t VMAvatar::FORCE_DELETE_TIMEOUT = 60 * 30;

static bool EqualsIgnoreCase(const std::string& a, const char* b) {
    return FSO::Content::ToLowerInvariant(a) == FSO::Content::ToLowerInvariant(b);
}

static std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(sep, start);
        out.push_back(s.substr(start, (p == std::string::npos) ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return out;
}

// C# Convert.ToUInt64(str, 16): throws on bad input.
static uint64_t ParseHex64(const std::string& s) {
    if (s.empty()) throw std::invalid_argument("empty");
    char* end = nullptr;
    uint64_t v = strtoull(s.c_str(), &end, 16);
    if (end == nullptr || *end != '\0') throw std::invalid_argument("bad hex");
    return v;
}

void VMAvatar::Message(const std::string& value) {
    m_Message = value;
    SetPersonData(VMPersonDataVariable::ChatBaloonOn, 1);
    MessageTimeout = 150 + (int32_t)value.size() / 2;
}

void VMAvatar::BodyOutfit(uint64_t value) {
    _BodyOutfit = value;
    Avatar->Body = FSO::Content::Content::Get()->AvatarOutfits->Get(value, false);
    if (AvatarType == VMAvatarType::Adult || AvatarType == VMAvatarType::Child) Avatar->Handgroup = Avatar->Body;
}

void VMAvatar::HeadOutfit(uint64_t value) {
    _HeadOutfit = value;
    Avatar->Head = (_HeadOutfit == 0) ? nullptr : FSO::Content::Content::Get()->AvatarOutfits->Get(value, false);
}

void VMAvatar::SkinTone(AppearanceType value) {
    _SkinTone = value;
    Avatar->Appearance = value;
}

st::Vector3 VMAvatar::VisualPosition() const { return (UseWorld && WorldUI) ? WorldUI->Position : st::Vector3(); }
void VMAvatar::VisualPosition(const st::Vector3& value) { if (UseWorld && WorldUI) WorldUI->Position = value; }

void VMAvatar::RadianDirection(float value) {
    _RadianDirection = value;
    if (UseWorld && WorldUI) AvaUI()->RadianDirection = value;
}

VMEntity::DirectionT VMAvatar::Direction() const {
    int midPointDir = (int)DirectionUtils::PosMod(std::nearbyint(_RadianDirection / (M_PI / 4.0)), 8);
    return (DirectionT)(1 << (midPointDir));
}
void VMAvatar::Direction(DirectionT value) {
    RadianDirection(((int)std::nearbyint(std::log2((double)(int)value))) * (float)(M_PI / 4.0));
}

// FIX(sims): The Sims 1 itself stores the species in person data 65 as 0 male, 1 female, 2/3 dog (male/female),
// 4/5 cat (male/female): that is what the shipped NBRS records and every saved house's OBJM hold for the Unleashed pets.
// FreeSO's flags (dog 8, cat 16) are accepted too. Testing only the flags made every pet loaded from a house or a
// neighbour record a person (Old Town cats cooking, dogs flushing toilets, pets eligible for every human interaction).
bool VMAvatar::IsPet() {
    return IsDog() || IsCat();
}
bool VMAvatar::IsDog() {
    auto gender = GetPersonData(VMPersonDataVariable::Gender);
    return (gender & 8) > 0 || gender == 2 || gender == 3;
}
bool VMAvatar::IsCat() {
    auto gender = GetPersonData(VMPersonDataVariable::Gender);
    return (gender & 16) > 0 || gender == 4 || gender == 5;
}

VMAvatar::VMAvatar(FSO::Content::GameObject* obj) : VMEntity(obj) {
    PersonData.fill(0);
    MotiveData.fill(0);
    auto state = std::make_shared<TSOPlatform::VMTSOAvatarState>(); //todo: ts1 switch
    AvatarState = state.get();
    PlatformState = state;
    BodyStrings = Object->Resource->Get<STR>(Object->OBJ->BodyStringID);
    Visitor = false;
    SetAvatarType(BodyStrings);
    SkinTone(AppearanceType::Light);

    if (UseWorld) {
        WorldUI.reset(new AvatarComponent());
        auto avatarc = AvaUI();
        avatarc->Avatar = Avatar.get();
    }

    MotiveDecay.reset(new Entities::VMTS1MotiveDecay());
    for (int i = 0; i < 16; i++) {
        MotiveChanges[i] = VMMotiveChange();
        MotiveChanges[i].Motive = (VMMotive)i;
    }
}

VMAvatar::~VMAvatar() {}

void VMAvatar::SetAvatarType(STR* data) {
    if (data == nullptr) {
        AvatarType = VMAvatarType::Adult;
    } else {
        auto type = data->GetString(0);
        if (type == "adult") AvatarType = VMAvatarType::Adult;
        else if (type == "child") AvatarType = VMAvatarType::Child;
        else if (type == "cat" || type == "kat") AvatarType = VMAvatarType::Cat; // TS1(sims): Unleashed cats are "kat"
        else if (type == "dog") AvatarType = VMAvatarType::Dog;
    }

    auto content = FSO::Content::Content::Get();
    switch (AvatarType) {
        case VMAvatarType::Adult:
        case VMAvatarType::Child: // C#: Child falls through to no case, leaving Avatar null; we keep an adult skeleton so outfits can be bound.
            // HOOK(vitaboy): TS1 children use the child skeleton (same bone layout, child proportions).
            Avatar.reset(new FSO::Vitaboy::SimAvatar(content->AvatarSkeletons->Get(AvatarType == VMAvatarType::Child ? "child.skel" : "adult.skel")));
            Avatar->Head = content->AvatarOutfits->Get(SetAvatarRandomOutfit(1), false);
            Avatar->Body = content->AvatarOutfits->Get(SetAvatarRandomOutfit(0), false);
            Avatar->Handgroup = Avatar->Body;
            break;
        case VMAvatarType::Cat: {
            auto skel = content->AvatarSkeletons->Get("kat.skel"); // TS1(sims): kat_skeleton.cmx (FreeSims "cat.skel")
            if (skel == nullptr || skel->Bones.empty()) skel = content->AvatarSkeletons->Get("cat.skel");
            Avatar.reset(new FSO::Vitaboy::SimAvatar(skel));
            Avatar->Body = content->AvatarOutfits->Get("uaa002cat_calico.oft");
            break;
        }
        case VMAvatarType::Dog:
            Avatar.reset(new FSO::Vitaboy::SimAvatar(content->AvatarSkeletons->Get("dog.skel")));
            Avatar->Body = content->AvatarOutfits->Get("uaa012dog_scottish.oft"); //;)
            break;
    }
}

void VMAvatar::SetAvatarBodyStrings(STR* data, VMContext* context) {
    if (data == nullptr) return;

    auto body = data->GetString(1);
    auto randBody = data->GetString(10);

    auto gender = data->GetString(12);
    auto genVar = (int)VMPersonDataVariable::Gender;

    try {
        if (randBody != "") {
            auto bodySpl = Split(randBody, ';');
            DefaultSuits.Daywear = ParseHex64(bodySpl[context->NextRandom((uint64_t)bodySpl.size() - 1)]);
        } else if (body != "") {
            DefaultSuits.Daywear = ParseHex64(body);
        }

        BodyOutfit(DefaultSuits.Daywear);

        auto head = data->GetString(2);
        auto randHead = data->GetString(9);

        if (randHead != "") {
            auto headSpl = Split(randHead, ';');
            HeadOutfit(ParseHex64(headSpl[context->NextRandom((uint64_t)headSpl.size() - 1)]));
        } else if (head != "") {
            HeadOutfit(ParseHex64(head));
        }
    } catch (...) {
        // HOOK(vitaboy): TS1 body strings are "suit,BODY=texture" (not TSO hex outfit ids). Register them as
        // TS1 outfits (FreeSO: new VMOutfitReference(data, head)) so the Sim gets its real body/head/hands.
        auto vbContent = FSO::Content::TS1::TS1AvatarContent::Get();
        auto ts1Body = vbContent->RegisterOutfitFromBodyStrings(data, false);
        auto ts1Head = vbContent->RegisterOutfitFromBodyStrings(data, true);
        if (ts1Body != 0) {
            DefaultSuits.Daywear = ts1Body;
            BodyOutfit(ts1Body);
            HeadOutfit(ts1Head);
        } else {
        //head or body invalid, resort to default.
        HeadOutfit(SetAvatarRandomOutfit(1));
        BodyOutfit(SetAvatarRandomOutfit(0));
        }
    }

    if (EqualsIgnoreCase(gender, "male")) PersonData[genVar] = 0;
    else if (EqualsIgnoreCase(gender, "female")) PersonData[genVar] = 1;
    // FIX(sims): TS1 encodes pets as 2..5 (see IsPet); FreeSO's 8/9/16/17 never reach a TS1 BHAV or a saved file.
    else if (EqualsIgnoreCase(gender, "dogmale")) PersonData[genVar] = context->VM->TS1 ? 2 : 8;
    else if (EqualsIgnoreCase(gender, "dogfemale")) PersonData[genVar] = context->VM->TS1 ? 3 : 9;
    else if (EqualsIgnoreCase(gender, "catmale")) PersonData[genVar] = context->VM->TS1 ? 4 : 16;
    else if (EqualsIgnoreCase(gender, "catfemale")) PersonData[genVar] = context->VM->TS1 ? 5 : 17;

    auto names = data->GetString(11);
    if (names != "") {
        auto nameSpl = Split(names, ';');
        Name(nameSpl[context->NextRandom((uint64_t)nameSpl.size())]);
    }

    PersonData[(int)VMPersonDataVariable::PersonsAge] = (int16_t)atoi(data->GetString(13).c_str());

    auto skinTone = data->GetString(14);
    if (EqualsIgnoreCase(skinTone, "lgt")) SkinTone(AppearanceType::Light);
    else if (EqualsIgnoreCase(skinTone, "med")) SkinTone(AppearanceType::Medium);
    else if (EqualsIgnoreCase(skinTone, "drk")) SkinTone(AppearanceType::Dark);
}

uint64_t VMAvatar::SetAvatarRandomOutfit(int32_t Type) {
    uint64_t id = 0x3A00000000DULL;
    // C#: new Random().Next(1, 4) (non-deterministic, values 1..3). Uses rand() here; cosmetic only.
    auto value = 1 + (rand() % 3);

    if (Type == 1) {
        switch (value) {
            case 1: id = 0x3A10000000DULL; break;
            case 2: id = 0x3A20000000DULL; break;
            case 3: id = 0x1C50000000DULL; break;
            case 4: id = 0x1C60000000DULL; break;
        }
    } else {
        switch (value) {
            case 1: id = 0x25B0000000DULL; break;
            case 2: id = 0x2600000000DULL; break;
            case 3: id = 0x50000000DULL; break;
            case 4: id = 0x70000000DULL; break;
        }
    }
    return id;
}

void VMAvatar::SetAvatarData(XmlCharacter* charInfo) {
    auto content = FSO::Content::Content::Get();
    if (charInfo != nullptr) {
        PersistID = (uint16_t)atoi(charInfo->Id.c_str());
        AppearanceType type = AppearanceType::Light;
        if (charInfo->Appearance == "Medium") type = AppearanceType::Medium;
        else if (charInfo->Appearance == "Dark") type = AppearanceType::Dark;

        uint64_t headId = 0, bodyId = 0;
        try { headId = ParseHex64(charInfo->Head); } catch (...) {}
        try { bodyId = ParseHex64(charInfo->Body); } catch (...) {}
        auto headPurchasable = content->AvatarPurchasables->Get(headId, false);
        auto bodyPurchasable = content->AvatarPurchasables->Get(bodyId, false);

        Name(charInfo->Name);
        SkinTone(type);
        AvatarType = VMAvatarType::Adult;

        auto Gender = charInfo->Gender;
        if (Gender == "Male")
            SetPersonData(VMPersonDataVariable::Gender, 0);
        else if (Gender == "Female")
            SetPersonData(VMPersonDataVariable::Gender, 1);

        auto HeadOutfitP = content->AvatarOutfits->Get(headPurchasable != nullptr ? headPurchasable->OutfitID : headId, false);
        auto BodyOutfitP = content->AvatarOutfits->Get(bodyPurchasable != nullptr ? bodyPurchasable->OutfitID : bodyId, false);
        Avatar->Head = HeadOutfitP;
        Avatar->Body = BodyOutfitP;
        Avatar->Handgroup = Avatar->Body;
    } else {
        Avatar->Head = content->AvatarOutfits->Get("mah010_baldbeard01.oft"); //default to bob newbie, why not
        Avatar->Body = content->AvatarOutfits->Get("mab002_slob.oft");
        Avatar->Handgroup = Avatar->Body;
    }

    for (size_t i = 0; i < MotiveData.size(); i++) {
        MotiveData[i] = 75;
    }
    SnapshotMotiveData();

    if (charInfo != nullptr && charInfo->Data.size() > 1) {
        SetPersonData(VMPersonDataVariable::CookingSkill, (int16_t)(charInfo->Data[0].Value));
        SetPersonData(VMPersonDataVariable::CharismaSkill, (int16_t)(charInfo->Data[1].Value));
    }
}

void VMAvatar::InitBodyData(VMContext* context) {
    //init walking strings
    // TS1(sims): children use the global 'StdChild' (151) / 'PanicChild' (160) tables (c2o-* walks/idles).
    const bool ts1Child = context->VM->TS1 && TS1Appearance.AgeType == "child";
    auto GlobWalk = context->Globals->Resource->Get<STR>(150);
    if (GlobWalk != nullptr)
        for (int i = 0; i < GlobWalk->Length() && i < 50; i++) {
            WalkAnimations[i] = GlobWalk->GetString(i);
        }

    auto GlobSwim = context->Globals->Resource->Get<STR>(158);
    if (GlobSwim != nullptr)
        for (int i = 0; i < GlobSwim->Length() && i < 50; i++) {
            SwimAnimations[i] = GlobSwim->GetString(i);
        }

    if (ts1Child) {
        // the child tables only fill the entries a child has (empty entries keep the adult one)
        auto ChildWalk = context->Globals->Resource->Get<STR>(151);
        if (ChildWalk != nullptr)
            for (int i = 0; i < ChildWalk->Length() && i < 50; i++) { auto str = ChildWalk->GetString(i); if (str != "") WalkAnimations[i] = str; }
        auto ChildSwim = context->Globals->Resource->Get<STR>(160);
        if (ChildSwim != nullptr)
            for (int i = 0; i < ChildSwim->Length() && i < 50; i++) { auto str = ChildSwim->GetString(i); if (str != "") SwimAnimations[i] = str; }
    }

    auto SpecialWalk = Object->Resource->Get<STR>(150);
    if (SpecialWalk != nullptr) {
        for (int i = 0; i < SpecialWalk->Length() && i < 50; i++) {
            auto str = SpecialWalk->GetString(i);
            if (str != "") WalkAnimations[i] = str;
        }
    }
}

void VMAvatar::PrepareForRestore(VMContext* context) {
    if (UseWorld && WorldUI) WorldUI->ObjectID = ObjectID;
    VMEntity::PrepareForRestore(context);
}

void VMAvatar::RestorePersonData(const std::vector<int16_t>& person, const float (&motives)[16], const float* oldMotives) {
    // Classic ObjM stores 80 words through version 0x45, then 256. Only
    // the supported prefix belongs to the runtime bank. The decoded snapshot
    // retains its extra words for the saver; do not reject or overwrite them.
    std::array<int16_t,101> restoredPerson{};
    std::copy_n(person.begin(), std::min(person.size(), restoredPerson.size()), restoredPerson.begin());
    std::array<float,16> restoredMotives{}, restoredOld{};
    for (size_t i=0; i<restoredMotives.size(); ++i) {
        if (!std::isfinite(motives[i]) || motives[i] < -32768.0f || motives[i] > 32767.0f)
            throw std::runtime_error("ObjM: invalid saved motive");
        restoredMotives[i] = motives[i];
        const auto oldValue = oldMotives ? oldMotives[i] : motives[i];
        if (!std::isfinite(oldValue) || oldValue < -32768.0f || oldValue > 32767.0f)
            throw std::runtime_error("ObjM: invalid old motive");
        restoredOld[i] = oldValue;
    }
    PersonData=restoredPerson;
    MotiveData=restoredMotives; OldMotiveData=restoredOld;
}

void VMAvatar::FinishRestore(VMContext* context) {
    VMEntity::FinishRestore(context);
    // Saved person fields are already installed. Do not InheritNeighbor, run
    // Init person, default motives, or randomize names/outfits during a load.
    if (context->VM->TS1) {
        auto strings = Object->Resource->Get<STR>(Object->OBJ->BodyStringID);
        TS1Appearance = Model::VMTS1Appearance::FromBodyStrings(strings);
        if (strings) {
            auto content = FSO::Content::TS1::TS1AvatarContent::Get();
            const auto body = content->RegisterOutfitFromBodyStrings(strings, false);
            const auto head = content->RegisterOutfitFromBodyStrings(strings, true);
            if (body) { DefaultSuits.Daywear = body; BodyOutfit(body); }
            if (head) HeadOutfit(head);
            if (EqualsIgnoreCase(TS1Appearance.SkinTone, "med")) SkinTone(AppearanceType::Medium);
            else if (EqualsIgnoreCase(TS1Appearance.SkinTone, "drk")) SkinTone(AppearanceType::Dark);
            else SkinTone(AppearanceType::Light);
        }
        auto ctss = Object->Resource->Get<FSO::Files::Formats::IFF::Chunks::CTSS>(Object->OBJ->CatalogStringsID);
        const auto personal = FSO::Content::TS1::ResolvePersonalText(ctss);
        PersonalTextLanguage=personal.SourceLanguage; PersonalTextUTF8=personal.UTF8; PersonalTextAmbiguous=personal.Ambiguous;
        if (!personal.Value(0).empty()) Name(personal.Value(0));
    }
    const auto person = PersonData;
    InitBodyData(context);
    PersonData = person;
    SetPersonData(VMPersonDataVariable::CensorshipFlags, person[int(VMPersonDataVariable::CensorshipFlags)]);
    SetPersonData(VMPersonDataVariable::RenderDisplayFlags, person[int(VMPersonDataVariable::RenderDisplayFlags)]);
}

void VMAvatar::Init(VMContext* context) {
    if (UseWorld && WorldUI) WorldUI->ObjectID = ObjectID;

    if (context->VM->TS1) {
        // TS1(sims): The Sims 1 person init tree ('Init person', PersonGlobals #8192) reads the person data
        // (age, job, family number...) and sets the motives ('init motives'). So, unlike the FreeSims order below:
        //  1. body strings (gender/age/skin) and the motive defaults are set *before* the init tree runs,
        //  2. a neighbour's persistent person data is inherited from the neighbourhood (NBRS) record of its
        //     GUID, as the game does when it spawns a neighbour (FreeSO: InheritNeighbor),
        //  3. the FreeSims TSO test values (skills 1000, person data 61 = 100, ...) are not applied: in TS1
        //     person data 61 is the family number and 2-18 are the personality/skills of the character.
        Animations.clear();
        SetAvatarBodyStrings(Object->Resource->Get<STR>(Object->OBJ->BodyStringID), context);
        TS1Appearance = Model::VMTS1Appearance::FromBodyStrings(Object->Resource->Get<STR>(Object->OBJ->BodyStringID));
        { // the person's name is its catalog string 0 (CTSS #2000 in User*.iff: "Bob")
            auto ctss = Object->Resource->Get<FSO::Files::Formats::IFF::Chunks::CTSS>(Object->OBJ->CatalogStringsID);
            const auto personal = FSO::Content::TS1::ResolvePersonalText(ctss);
            PersonalTextLanguage = personal.SourceLanguage;
            PersonalTextUTF8 = personal.UTF8;
            PersonalTextAmbiguous = personal.Ambiguous;
            if (!personal.Value(0).empty()) Name(personal.Value(0));
            if (personal.Ambiguous) printf("[identity] ambiguous personal CTSS language for GUID %08x\n", (unsigned)Object->OBJ->GUID);
        }
        InitBodyData(context);
        for (size_t i = 0; i < MotiveData.size(); i++) {
            MotiveData[i] = 75;
        }
        SetMotiveData(VMMotive::SleepState, 0); //max all motives except sleep state
        SnapshotMotiveData();
        FSO::SimAntics::Engine::Utils::VMTS1Neighbourhood::InheritNeighbor(this, context->VM);
        VMEntity::Init(context);
        return;
    }

    VMEntity::Init(context);

    Animations.clear();

    SetAvatarBodyStrings(Object->Resource->Get<STR>(Object->OBJ->BodyStringID), context);
    InitBodyData(context);

    for (size_t i = 0; i < MotiveData.size(); i++) {
        MotiveData[i] = 75;
    }

    SetMotiveData(VMMotive::SleepState, 0); //max all motives except sleep state
    SnapshotMotiveData();

    SetPersonData(VMPersonDataVariable::NeatPersonality, 1000); //for testing wash hands after toilet
    SetPersonData(VMPersonDataVariable::OnlineJobID, 1); //for testing wash hands after toilet

    SetPersonData(VMPersonDataVariable::CreativitySkill, 1000);
    SetPersonData(VMPersonDataVariable::CookingSkill, 1000);
    SetPersonData(VMPersonDataVariable::CharismaSkill, 1000);
    SetPersonData(VMPersonDataVariable::LogicSkill, 1000);
    SetPersonData(VMPersonDataVariable::BodySkill, 1000);

    SetPersonData(VMPersonDataVariable::NumOutgoingFriends, 100);
    SetPersonData(VMPersonDataVariable::IncomingFriends, 100);
}

void VMAvatar::Reset(VMContext* context) {
    VMEntity::Reset(context);
    SetPersonData(VMPersonDataVariable::Priority, 0); // TS1(sims): FreeSO
    Animations.clear();
    if (Headline != nullptr && HeadlineRenderer != nullptr) {
        HeadlineRenderer->Dispose();
        Headline.reset();
        HeadlineRenderer.reset();
    }
    for (auto& aprName : BoundAppearances) {
        //remove all appearances, so we don't have stuff stuck to us.
        auto apr = FSO::Content::Content::Get()->AvatarAppearances->Get(aprName);
        Avatar->RemoveAccessory(apr);
    }
    BoundAppearances.clear();
    // STUB(3ds): context.VM.EODHost.ForceDisconnect(this) — no EOD host.
}

void VMAvatar::HandleTimePropsEvent(FSO::Vitaboy::TimePropertyListItem& tp) {
    HandleTimePropsEvent(tp, true);
}

void VMAvatar::HandleTimePropsEvent(FSO::Vitaboy::TimePropertyListItem& tp, bool allowSound) {
    HandleTimePropsEvent(tp, allowSound, CurrentAnimationState());
}

void VMAvatar::HandleTimePropsEvent(FSO::Vitaboy::TimePropertyListItem& tp, bool allowSound, VMAnimationState* eventOwner) {
    VMAvatar* avatar = this;
    auto evt = tp.Properties.Get("xevt");
    if (evt != nullptr && eventOwner != nullptr) {
        auto eventValue = (int16_t)atoi(evt->c_str());
        eventOwner->EventQueue.push_back(eventValue);
        // TS1(sims): FreeSO — only events below 100 count towards the operand's expected event count. Events
        // >= 100 (101 = 'sound' cue) are extra; when the animation ends short of its count, Animate Sim queues the
        // missing ones (0, 1, ...). E.g. 'A2O-tvsmall-turnon' carries only xevt 101 and its tree switches the TV on
        // on the forced event 0: counting 101 meant small TVs never turned on and 'Watch TV' never ran.
        if (eventValue < 100) eventOwner->EventsRun++;
    }
    auto rhevt = tp.Properties.Get("righthand");
    if (rhevt != nullptr) {
        auto eventValue = (int16_t)atoi(rhevt->c_str());
        avatar->Avatar->RightHandGesture = (FSO::Vitaboy::SimHandGesture)eventValue;
    }
    auto lhevt = tp.Properties.Get("lefthand");
    if (lhevt != nullptr) {
        auto eventValue = (int16_t)atoi(lhevt->c_str());
        avatar->Avatar->LeftHandGesture = (FSO::Vitaboy::SimHandGesture)eventValue;
    }

    auto soundevt = tp.Properties.Get("sound");
    auto owner = this;
    if (allowSound && UseWorld && soundevt != nullptr && FSO::HIT::HITVM::Get() != nullptr) {
        bool playing = false;
        for (auto& x : owner->SoundThreads) if (x.Name == *soundevt) { playing = true; break; }
        if (!playing) {
            auto thread = FSO::HIT::HITVM::Get()->PlaySoundEvent(*soundevt);
            if (thread != nullptr) {
                auto hthread = std::dynamic_pointer_cast<FSO::HIT::HITThread>(thread);
                if (hthread != nullptr) SubmitHITVars(hthread.get());

                if (!thread->AlreadyOwns(owner->ObjectID)) thread->AddOwner(owner->ObjectID);

                Model::Sound::VMSoundEntry entry;
                entry.Sound = thread;
                entry.Pan = true;
                entry.Zoom = true;
                entry.Name = *soundevt; // (C# left Name empty, so the same event could stack up)
                owner->SoundThreads.push_back(entry);
                owner->TickSounds();
            }
        }
    }

    // TS1: animation time properties mount/unmount authored accessories.
    // Both pinned FreeSO consumers retain the literal name even without a
    // world renderer; the existing appearance resolver owns .apr/case lookup.
    auto dress = tp.Properties.Get("dress");
    if (dress != nullptr) {
        avatar->BoundAppearances.insert(*dress);
        if (UseWorld && avatar->Avatar != nullptr) {
            auto apr = FSO::Content::Content::Get()->AvatarAppearances->Get(*dress + ".apr");
            if (apr != nullptr) avatar->Avatar->AddAccessory(apr);
        }
    }
    auto undress = tp.Properties.Get("undress");
    if (undress != nullptr) {
        avatar->BoundAppearances.erase(*undress);
        if (UseWorld && avatar->Avatar != nullptr) {
            auto apr = FSO::Content::Content::Get()->AvatarAppearances->Get(*undress + ".apr");
            if (apr != nullptr) avatar->Avatar->RemoveAccessory(apr);
        }
    }

}

void VMAvatar::SubmitHITVars(FSO::HIT::HITThread* thread) {
    if (thread == nullptr || thread->ObjectVar.empty()) return;
    // C#: thread.ObjectVar[12] = GetPersonData(VMPersonDataVariable.Gender);  (TSO's field numbering)
    // TS1(sims): The Sims' HIT code reads field 0 with getsrcdatafield and picks the male (0), female (1) or
    // child (anything else) hitlist (generic_fm_smartlists), so Simlish voices match the Sim.
    int16_t gender = GetPersonData(Model::VMPersonDataVariable::Gender);
    int field0 = (AvatarType == VMAvatarType::Child) ? 2 : (gender & 1);
    thread->ObjectVar[0] = field0;
    thread->ObjectVar[1] = GetPersonData(Model::VMPersonDataVariable::PersonsAge);
    thread->ObjectVar[12] = gender;
    SubmitSourceVars(thread, ObjectID);
    thread->WriteVar(2, field0); // arg2 = Gender (SimsGlobals.txt)
}

void VMAvatar::SubmitSourceVars(FSO::HIT::HITThread* thread, int16_t objectID) {
    if (thread == nullptr || thread->ObjectVar.empty()) return;
    // arg1 = Instance (SimsGlobals.txt). Offset so an actor's sequence group never equals GroupMusic (1),
    // GroupDialogMain (2) or GroupDialogOverlay (3).
    thread->WriteVar(1, 0x1000 + objectID);
}

void VMAvatar::Tick() {
    if (Thread && Thread->Context) Thread->Context->ObserveVisualActor(this);
    Velocity = st::Vector3(0, 0, 0);
    VMEntity::Tick();

    if (m_Message != "") {
        if (MessageTimeout-- > 0) {
            if (MessageTimeout == 0) {
                SetPersonData(VMPersonDataVariable::ChatBaloonOn, 0);
                m_Message = "";
            }
        }
    }

    if (Thread != nullptr && Thread->ThreadBreak == Engine::VMThreadBreakMode::Pause) return;

    if (PersonData[(int)VMPersonDataVariable::OnlineJobStatusFlags] == 0) PersonData[(int)VMPersonDataVariable::OnlineJobStatusFlags] = 1;
    if (Thread != nullptr) {
        MotiveDecay->Tick(this, Thread->Context);
        SetPersonData(VMPersonDataVariable::OnlineJobGrade, std::max((int16_t)0, Thread->Context->VM->GetGlobalValue(11))); //force job grade to what we expect
        // TS1 scripts deliberately move Sims out of world for work, travel and
        // service departure. Only the online-game recovery may place them at
        // the mailbox; doing that in TS1 undoes the authored departure.
        if (!Thread->Context->VM->TS1 && Position() == LotTilePos::OUT_OF_WORLD) {
            // Online-game recovery.
            VMEntity* mailbox = nullptr;
            for (auto x : Thread->Context->VM->Entities) {
                if (x->Object->OBJ->GUID == 0xEF121974 || x->Object->OBJ->GUID == 0x1D95C9B0) { mailbox = x; break; }
            }
#ifdef ST_HAVE_FINDLOCATIONFOR
            if (mailbox != nullptr) Primitives::VMFindLocationFor::FindLocationFor(this, mailbox, Thread->Context);
#else
            (void)mailbox; // primitives/VMFindLocationFor not translated yet
#endif
        }
    }

    VMAvatar* avatar = this;
    if (Thread && Thread->Context->VM->TS1 && !TS1ExternalAnimationClock) {
        int modelState = CurrentAnimationState() && CurrentAnimationState()->TS1ScriptAnimation ? 0 : 3;
        for (auto frame = Thread->Stack.rbegin(); frame != Thread->Stack.rend(); ++frame) {
            if (auto route = dynamic_cast<Engine::VMRoutingFrame*>(frame->get())) {
                if (route->UsesTS1SourceLocomotion()) {
                    modelState = route->TS1MotionModelState();
                    break;
                }
            }
        }
        UpdateTS1MotionRate(modelState);
        if (modelState == 0 && TS1HurryableAnimation && CurrentAnimationState() && CurrentAnimationState()->TS1Clock)
            CurrentAnimationState()->Speed = TS1MotionRate;
    }
    AdvanceAnimations();

    UpdateHeadSeek();

    if (avatar->CarryAnimationState != nullptr) {
        auto& carry = avatar->CarryAnimationState;
        const float pose = carry->TS1Clock ? carry->TS1PoseFrame() : carry->CurrentFrame;
        if (carry->TS1Clock) FSO::Vitaboy::Animator::RenderFrameTS1(avatar->Avatar.get(), carry->Anim,
            int(pose), std::fmod(pose, 1.f), 1.f, carry->Loop);
        else FSO::Vitaboy::Animator::RenderFrame(avatar->Avatar.get(), carry->Anim, int(carry->CurrentFrame), 0.f, 1.f);
    }

    if (Thread && Thread->Context->VM->TS1) {
        // Original SAnimator update precedes room assignment and ordered deltas.
        if (GetValue(VMStackObjectVariable::Room) != -5 &&
            Thread->Context->GetEnvironmentRoomAt(Position()) != -5)
            SetMotiveDataFloat(VMMotive::Room, Thread->Context->GetEnvironmentScore(Position()));
        for (const auto& change : TS1MotiveChanges) change.Tick(this);
    } else {
        for (auto& change : MotiveChanges) change.Tick(this);
    }

    // Native VM.Update publishes this full-tick pose directly to Draw. Apply
    // only the overlay here: FractionalAnim also interpolates world position.
    ApplyHeadSeek(0.f);
    avatar->Avatar->ReloadSkeleton();

    PersonData[(int)VMPersonDataVariable::TickCounter]++;
    if (KillTimeout > -1) {
        if (++KillTimeout > FORCE_DELETE_TIMEOUT) Delete(true, Thread->Context);
        else {
            SetPersonData(VMPersonDataVariable::RenderDisplayFlags, 1);
            SetValue(VMStackObjectVariable::Hidden, (int16_t)((KillTimeout % 30) / 15));
            if (Thread->BlockingState != nullptr) Thread->BlockingState->WaitTime = std::max<int32_t>(Thread->BlockingState->WaitTime, 1000000); //make most things time out
            UserLeaveLot(); //keep forcing
        }
    }
}

int32_t VMAvatar::TS1AnimationTimestamp() const {
    const uint32_t counter = Thread && Thread->Context && Thread->Context->Clock
        ? uint32_t(Thread->Context->Clock->Ticks) : 0;
    const uint32_t bits = counter * (TS1RawAnimationClock ? 42u : 50u);
    return bits <= 0x7fffffffu ? int32_t(bits) : int32_t(int64_t(bits) - 0x100000000LL);
}

void VMAvatar::UpdateTS1MotionRate(int modelState) {
    if (!Thread || !Thread->Context || !Thread->Context->Clock || !Thread->Context->VM->TS1) return;
    const auto tick = Thread->Context->Clock->Ticks;
    if (TS1MotionRateTick == tick) return;
    TS1MotionRateTick = tick;
    TS1MotionRate = Engine::Routing::TS1AdvanceMotionRate(TS1MotionRate,
        Engine::Routing::TS1TargetMotionRate(GetValue(Model::VMStackObjectVariable::WalkStyle),
            modelState, GetPersonData(Model::VMPersonDataVariable::Gender)));
}

void VMAvatar::StartTS1Animation(const std::shared_ptr<VMAnimationState>& state, float rate) {
    if (state && !TS1ExternalAnimationClock && Thread && Thread->Context && Thread->Context->VM->TS1)
        state->StartTS1Clock(TS1AnimationTimestamp(), rate);
}

void VMAvatar::AdvanceAnimations(bool allowSound) {
    //animation update for avatars
    VMAvatar* avatar = this;
    float totalWeight = 0.0f;
    for (size_t channel = 0; channel <= Animations.size(); ++channel) {
        const bool carry = channel == Animations.size();
        auto state = carry ? CarryAnimationState.get() : Animations[channel].get();
        if (carry && (!state || !state->TS1Clock)) break;
        if (carry) totalWeight = 1.f;
        else totalWeight += state->Weight;
        if (state->TS1Clock) {
            if (!state->EndReached || state->Loop) {
                const int32_t previousEventTime = state->TS1EventTime();
                state->AdvanceTS1Clock(TS1AnimationTimestamp());
                const int32_t eventTime = state->TS1EventTime();
                const bool eventWrap = state->PlayingBackwards ? eventTime > previousEventTime : eventTime < previousEventTime;
                auto& props = state->TimePropertyLists;
                auto dispatch = [&](size_t index) {
                    // The source advances its cursor over invalid property
                    // times too, but only calls back inside the authored clip.
                    const auto id = props[index].ID;
                    if (id <= uint32_t(INT32_MAX) && double(id) <= std::trunc(double(state->Anim->Duration)))
                        HandleTimePropsEvent(props[index], allowSound, state);
                };
                auto emit = [&](int32_t limit, bool toEnd) {
                    auto& cursor = state->TS1EventCursor;
                    if (state->PlayingBackwards) {
                        while (cursor >= 0 && (toEnd || int64_t(props[size_t(cursor)].ID) >= limit))
                            dispatch(size_t(cursor--));
                    } else {
                        while (cursor < int32_t(props.size()) && (toEnd || int64_t(props[size_t(cursor)].ID) <= limit))
                            dispatch(size_t(cursor++));
                    }
                };
                if (state->Weight > 0.f && state->Anim->NumFrames() != 0 && !state->Anim->Motions.empty()) {
                    if (eventWrap) {
                        emit(0, true);
                        state->TS1EventCursor = state->PlayingBackwards ? int32_t(props.size()) - 1 : 0;
                    }
                    emit(eventTime, false);
                }
            }
            const float poseFrame = state->TS1PoseFrame();
            FSO::Vitaboy::Animator::RenderFrameTS1(avatar->Avatar.get(), state->Anim,
                int(poseFrame), std::fmod(poseFrame, 1.f),
                carry ? 1.f : state->Weight, state->Loop);
            continue;
        }
        if (!state->EndReached && state->Weight != 0) {
            if (state->PlayingBackwards) state->CurrentFrame -= state->Speed;
            else state->CurrentFrame += state->Speed;
            auto currentFrame = state->CurrentFrame;
            auto currentTime = currentFrame * 33.33f;
            auto& timeProps = state->TimePropertyLists;
            if (!state->PlayingBackwards) {
                for (int i = 0; i < (int)timeProps.size(); i++) {
                    auto tp = timeProps[i];
                    if (tp.ID > currentTime) {
                        break;
                    }

                    timeProps.erase(timeProps.begin());
                    i--;

                    HandleTimePropsEvent(tp, allowSound);
                }
            } else {
                for (int i = (int)timeProps.size() - 1; i >= 0; i--) {
                    auto tp = timeProps[i];
                    if (tp.ID < currentTime) {
                        break;
                    }

                    timeProps.pop_back();
                    HandleTimePropsEvent(tp, allowSound);
                }
            }

            auto status = FSO::Vitaboy::Animator::RenderFrame(avatar->Avatar.get(), state->Anim, (int)state->CurrentFrame, std::fmod(state->CurrentFrame, 1.0f), state->Weight / totalWeight);
            if (status != FSO::Vitaboy::AnimationStatus::IN_PROGRESS) {
                if (state->Loop) {
                    if (state->PlayingBackwards) state->CurrentFrame += state->Anim->NumFrames();
                    else state->CurrentFrame -= state->Anim->NumFrames();
                }
                else
                    state->EndReached = true;
            }
        }
    }

}

void VMAvatar::UserLeaveLot() {
    // STUB(3ds): Thread.Context.VM.EODHost.ForceDisconnect(this) — no EOD host.
    for (size_t i = 0; i < Thread->Queue.size(); i++) {
        auto& x = Thread->Queue[i];
        if (x->ActionRoutine != nullptr && x->ActionRoutine->ID == LEAVE_LOT_TREE && (int)i <= Thread->ActiveQueueBlock + 1) return; //we're already leaving
    }
    auto actions = Thread->Queue;
    for (auto& action : actions) {
        Thread->CancelAction(action->UID);
    }

    auto tree = GetBHAVWithOwner(LEAVE_LOT_TREE, Thread->Context);
    if (tree == nullptr) return;
    auto routine = Thread->Context->VM->Assemble(tree->bhav);

    auto action = std::make_shared<Engine::VMQueuedAction>();
    action->Callee = this;
    action->CodeOwner = tree->owner;
    action->ActionRoutine = routine;
    action->Name = "Leave Lot";
    action->StackObject = this;
    action->Args.assign(4, 0);
    action->InteractionNumber = -1;
    action->Priority = INT16_MAX;
    action->Flags = TTABFlags::Leapfrog | TTABFlags::MustRun;
    Thread->EnqueueAction(action);
    if (KillTimeout == -1) KillTimeout = 0;
}

void VMAvatar::UpdateHeadSeek() {
    if (!Avatar || !Thread) return;
    auto& seek = PersonData[(int)VMPersonDataVariable::HeadSeekState];
    if (seek <= 0 || seek >= 8) return;
    auto target = Thread->Context->VM->GetObjectById(PersonData[(int)VMPersonDataVariable::HeadSeekObject]);
    std::optional<odh::HeadSeekQuaternion> visual;
    if (seek == 1 && target && UseWorld) {
        Avatar->HeadSeekPoseValid = false;
        if (WorldUI && target->WorldUI) if (auto lookedAt = target->WorldUI->GetLookTarget()) {
            const auto p = WorldUI->DrawPosition();
            auto difference = (*lookedAt - st::Vector3(p.X,p.Z,p.Y))*3.f;
            difference.Y -= 1.f;
            if (auto q = FSO::Vitaboy::Animator::CalculateHeadSeek(Avatar.get(),difference,RadianDirection())) {
                visual = odh::HeadSeekQuaternion{q->X,q->Y,q->Z,q->W};
                Avatar->HeadSeekPoseValid = true;
            }
        }
    }
    // Pure value service only: no previous Playback/World/VM is linked.
    odh::step_head_seek(Avatar->HeadSeek,seek,
        PersonData[(int)VMPersonDataVariable::HeadSeekTimeout],
        PersonData[(int)VMPersonDataVariable::HeadSeekFinishAction],target != nullptr,visual);
}

void VMAvatar::ApplyHeadSeek(float fraction) {
    if (!Avatar || Avatar->HeadSeek.weight <= 0) return;
    auto& head = Avatar->HeadSeek;
    odh::slide_head_seek(head,fraction-head.last_fraction);
    head.last_fraction = fraction;
    const float effect = PersonData[(int)VMPersonDataVariable::HeadSeekState] == 4 ? -fraction : fraction;
    if (Avatar->HeadSeekPoseValid) FSO::Vitaboy::Animator::ApplyHeadSeek(Avatar.get(),
        FSO::Vitaboy::Quaternion(head.current[0],head.current[1],head.current[2],head.current[3]),
        std::min(1.f,(head.weight+effect)/15.f));
}

void VMAvatar::FractionalAnim(float fraction) {
    auto avatar = this;
    float totalWeight = 0.0f;
    for (auto& state : Animations) {
        totalWeight += state->Weight;
        if (!state->EndReached) {
            float visualFrame = state->CurrentFrame;
            if (state->TS1Clock) {
                float phase = state->TS1Phase;
                if (state->Anim->Duration > 0.f) {
                    float change = fraction * float(TS1RawAnimationClock ? 42 : 50) * state->Speed / state->Anim->Duration;
                    phase += state->PlayingBackwards ? -change : change;
                    if (state->Loop && (phase < 0.f || phase > 1.f)) phase -= std::floor(phase);
                    else phase = std::clamp(phase, 0.f, 1.f);
                }
                visualFrame = state->BoundTS1PoseFrame(phase * state->Anim->NumFrames());
                FSO::Vitaboy::Animator::RenderFrameTS1(avatar->Avatar.get(), state->Anim, int(visualFrame),
                    std::fmod(visualFrame, 1.f), state->Weight, state->Loop);
                continue;
            }
            if (state->PlayingBackwards) visualFrame -= state->Speed / 2;
            else visualFrame += state->Speed / 2;

            FSO::Vitaboy::Animator::RenderFrame(avatar->Avatar.get(), state->Anim, (int)visualFrame, std::fmod(visualFrame, 1.0f), state->Weight / totalWeight);
        }
    }
    if (avatar->CarryAnimationState != nullptr) {
        const auto& carry = avatar->CarryAnimationState;
        const float pose = carry->TS1Clock ? carry->TS1PoseFrame() : carry->CurrentFrame;
        if (carry->TS1Clock) FSO::Vitaboy::Animator::RenderFrameTS1(avatar->Avatar.get(), carry->Anim,
            int(pose), std::fmod(pose,1.f), 1.f, carry->Loop);
        else FSO::Vitaboy::Animator::RenderFrame(avatar->Avatar.get(), carry->Anim, int(carry->CurrentFrame), 0.f, 1.f);
    }

    ApplyHeadSeek(fraction); // after all ordinary/carry channels, before absolute matrices

    //TODO: if this gets changed to run at variable framerate need to "remember" visual position
    avatar->Avatar->ReloadSkeleton();
    VisualPosition(VisualPosition() + fraction * Velocity);
}

int16_t VMAvatar::GetPersonData(VMPersonDataVariable variable) {
    if ((uint16_t)variable > 100) throw std::runtime_error("Person Data out of bounds!");
    switch (variable) {
        // TS1(sims): FreeSO — Priority is plain person data (set when an interaction starts/ends, read by the
        // queue). FreeSims mapped it onto Queue[0].Priority.
        case VMPersonDataVariable::IsHousemate: {
            auto level = ((TSOPlatform::VMTSOAvatarState*)TSOState())->Permissions;
            return (int16_t)((level >= TSOPlatform::VMTSOAvatarPermissions::BuildBuyRoommate) ? 2 : ((level >= TSOPlatform::VMTSOAvatarPermissions::Roommate) ? 1 : 0));
        }
        default: break;
    }
    return PersonData[(uint16_t)variable];
}

void VMAvatar::SetMotiveChange(VMMotive motive, int16_t perHourChange, int16_t maxValue) {
    if (Thread && Thread->Context->VM->TS1) {
        const int id = int(motive);
        if (!((id >= 5 && id <= 9) || id == 14 || id == 15))
            throw std::runtime_error("Set motive change: unsupported classic motive");
        TS1MotiveChanges.push_back({motive, float(perHourChange) * (1.0f / 1800.0f), float(maxValue)});
        return;
    }
    auto& temp = MotiveChanges[int(motive)];
    if (temp.Ticked) {
        temp.PerHourChange = perHourChange; temp.MaxValue = maxValue; temp.Ticked = false;
    }
}

void VMAvatar::RestoreTS1MotiveChanges(const std::vector<Model::VMTS1MotiveChange>& changes) {
    for (const auto& change : changes) {
        const int id = int(change.Motive);
        if (id < 0 || id >= 16 || !std::isfinite(change.TickDelta) || !std::isfinite(change.StopAt))
            throw std::runtime_error("Invalid classic motive change");
    }
    TS1MotiveChanges = changes;
}

Model::VMMotiveChange VMAvatar::GetMotiveChange(int index) const {
    const auto slot = size_t(index & 15);
    if (!Thread || !Thread->Context->VM->TS1) return MotiveChanges[slot];
    Model::VMMotiveChange result; result.Motive = VMMotive(slot);
    for (const auto& change : TS1MotiveChanges) if (size_t(change.Motive) == slot) {
        result.PerHourChange = int16_t(std::lround(std::clamp(double(change.TickDelta) * 1800.0, -32768.0, 32767.0)));
        result.MaxValue = int16_t(std::lround(std::clamp(double(change.StopAt), -32768.0, 32767.0)));
    }
    return result;
}

bool VMAvatar::HasMotiveChange(VMMotive motive) {
    if (Thread && Thread->Context->VM->TS1) {
        for (const auto& change : TS1MotiveChanges)
            if (change.Motive == motive && change.TickDelta != 0) return true;
        return false;
    }
    return MotiveChanges[int(motive)].PerHourChange != 0;
}

void VMAvatar::ClearMotiveChanges() {
    TS1MotiveChanges.clear();
    for (auto& change : MotiveChanges) change.Clear();
}

bool VMAvatar::SetPersonData(VMPersonDataVariable variable, int16_t value) {
    if (variable == VMPersonDataVariable::Gender && Thread && Thread->Context &&
        PersonData[int(variable)] != value) Thread->Context->VisualLightingDirty = true;
    if ((uint16_t)variable > 100) throw std::runtime_error("Person Data out of bounds!");
    switch (variable) {
        case VMPersonDataVariable::Priority:
            // TS1(sims): FreeSO — stored person data; changing it re-evaluates the queue.
            if (Thread != nullptr) Thread->QueueDirty = true;
            break;
        case VMPersonDataVariable::CensorshipFlags: // TS1(sims): the mosaic over the Sim (World::DrawCensor)
            if (WorldUI != nullptr) {
                bool female = (GetPersonData(VMPersonDataVariable::Gender) & 1) != 0;
                AvaUI()->CensorFlags = female ? value : (value & ~2);
            }
            break;
        case VMPersonDataVariable::MoneyAmmountOverHead: // FreeSO
            if (value != -32768) ShowMoneyHeadline(value);
            break;
        case VMPersonDataVariable::RenderDisplayFlags:
            if (WorldUI != nullptr) AvaUI()->DisplayFlags = (FSO::LotView::Components::AvatarDisplayFlags)value;
            return true;
        default: break;
    }
    PersonData[(uint16_t)variable] = value;
    return true;
}

float VMAvatar::GetMotiveDataFloat(VMMotive variable) const {
    if (uint32_t(variable) >= MotiveData.size()) throw std::runtime_error("Motive Data out of bounds!");
    return MotiveData[size_t(variable)];
}

float VMAvatar::GetOldMotiveDataFloat(VMMotive variable) const {
    if (uint32_t(variable) >= OldMotiveData.size()) throw std::runtime_error("Motive Data out of bounds!");
    return OldMotiveData[size_t(variable)];
}

void VMAvatar::SetMotiveDataFloat(VMMotive variable, float value) {
    if (uint32_t(variable) >= MotiveData.size() || !std::isfinite(value))
        throw std::runtime_error("Invalid motive value");
    MotiveData[size_t(variable)] = value;
}

void VMAvatar::SnapshotMotiveData() { OldMotiveData = MotiveData; }

int16_t VMAvatar::GetMotiveData(VMMotive variable) {
    const auto value = GetMotiveDataFloat(variable);
    if (!std::isfinite(value) || value <= -32768.5f || value >= 32767.5f)
        throw std::runtime_error("Motive cannot be represented by a script integer");
    return int16_t(std::lround(value));
}

bool VMAvatar::SetMotiveData(VMMotive variable, int16_t value) {
    SetMotiveDataFloat(variable, float(value));
    return true;
}

std::string VMAvatar::ToString() {
    return Name();
}

std::unique_ptr<Routing::VMObstacle> VMAvatar::GetObstacle(const LotTilePos& pos, DirectionT dir) {
    if (!(Thread && Thread->Context && Thread->Context->VM && Thread->Context->VM->TS1))
        return std::make_unique<Routing::VMObstacle>(pos.x-3, pos.y-3, pos.x+3, pos.y+3);
    // Directions in the engine are one-hot; the original formula uses ordinals.
    unsigned bits = static_cast<unsigned>(dir), d = 0;
    if (bits && !(bits & (bits-1))) while ((bits >>= 1) != 0) ++d;
    d %= 8;
    const int h = ((PersonData[68] & 1) || (PersonData[84] & 1)) ? 0 : 3;
    const uint16_t mask = Object && Object->OBJ ? uint16_t(Object->OBJ->FootprintMask) : 0;
    const uint16_t extension = (d & 1) ? 0 : uint16_t(PersonData[73]);
    int e[4];
    for (int i=0; i<4; ++i) e[i] = ((mask >> (4*i)) & 15) - ((extension >> (4*i)) & 15);
    const unsigned q = ((8-d)/2)%4;
    // Do not normalize: even zero-width and inverted numeric results are source state.
    auto result = std::make_unique<Routing::VMObstacle>();
    result->x1=pos.x-h+e[(q+3)%4]; result->y1=pos.y-h+e[q];
    result->x2=pos.x+h-e[(q+1)%4]; result->y2=pos.y+h-e[(q+2)%4];
    return result;
}

void VMAvatar::RefreshTS1Footprint() {
    if (Container || Position() == LotTilePos::OUT_OF_WORLD) { Footprint.reset(); return; }
    Footprint = GetObstacle(Position(), Direction());
}

void VMAvatar::ClearTS1InteractionFootprint() {
    if (GetPersonData(Model::VMPersonDataVariable::FootprintExtension) != 0) {
        SetPersonData(Model::VMPersonDataVariable::FootprintExtension, 0);
        RefreshTS1Footprint();
    }
    SetPersonData(Model::VMPersonDataVariable::RouteFootprintMask, 0);
    SetPersonData(Model::VMPersonDataVariable::NonInterruptable, 0);
}

void VMAvatar::RestoreTS1Footprint(int minX, int minY, int maxX, int maxY) {
    if (Container || Position() == LotTilePos::OUT_OF_WORLD) { Footprint.reset(); return; }
    Footprint = std::make_unique<Routing::VMObstacle>();
    Footprint->x1=minX; Footprint->y1=minY; Footprint->x2=maxX; Footprint->y2=maxY;
}

void VMAvatar::PositionChange(VMContext* context, bool noEntryPoint) {
    if (GhostImage) return;

    auto room = context->GetObjectRoom(this);
    SetRoom(room);

    for (auto child : Contained) if (child != nullptr) {
        context->UnregisterObjectPos(child);
        child->Position(Position());
        child->PositionChange(context, noEntryPoint);
    }

    context->RegisterObjectPos(this);
    if (Container != nullptr) return;
    if (Position() == LotTilePos::OUT_OF_WORLD) return;

    VMEntity::PositionChange(context, noEntryPoint);
}

void VMAvatar::PrePositionChange(VMContext* context) {
    Footprint.reset();
    if (GhostImage && UseWorld) {
        if (WorldUI && WorldUI->Container != nullptr) {
            WorldUI->Container = nullptr;
            WorldUI->ContainerSlot = 0;
        }
        return;
    }

    context->UnregisterObjectPos(this);
    if (Container != nullptr) {
        Container->ClearSlot(ContainerSlot);
        return;
    }
    if (Position() == LotTilePos::OUT_OF_WORLD) return;
    VMEntity::PrePositionChange(context);
}

// Begin Container SLOTs interface

int32_t VMAvatar::TotalSlots() {
    return (int32_t)Contained.size();
}

bool VMAvatar::PlaceInSlot(VMEntity* obj, int32_t slot, bool cleanOld, VMContext* context) {
    if (!obj || obj->Dead || obj == this || slot < 0 || slot >= TotalSlots()) return false;
    std::set<VMEntity*> ancestors;
    for (auto parent = this->Container; parent != nullptr; parent = parent->Container)
        if (parent == obj || !ancestors.insert(parent).second) return false;
    if (!cleanOld && obj->Container && (obj->Container != this || obj->ContainerSlot != slot)) return false;
    if (GetSlot(slot) == obj) return true; //already in slot
    if (GetSlot(slot) != nullptr) return false;
    if (cleanOld) obj->PrePositionChange(context);

    if (!obj->GhostImage) {
        Contained[(size_t)slot] = obj;

        if (slot == 0) {
            CarryAnimationState = std::make_shared<VMAnimationState>(FSO::Content::Content::Get()->AvatarAnimations->Get("a2o-rarm-carry-loop.anim"), false);
            StartTS1Animation(CarryAnimationState);
            if (CarryAnimationState->TS1Clock) CarryAnimationState->Loop = true;
        }

        obj->Container = this;
        obj->ContainerSlot = (int16_t)slot;
    }
    if (UseWorld && obj->WorldUI) {
        obj->WorldUI->Container = this->WorldUI.get();
        obj->WorldUI->ContainerSlot = slot;
        auto objC = dynamic_cast<ObjectComponent*>(obj->WorldUI.get());
        if (objC != nullptr) {
            objC->ForceDynamic = true;
        }
    }
    obj->Position(Position()); //TODO: is physical position the same as the slot offset position?
    if (context && !obj->GhostImage) context->InvalidateRoofObject(obj);
    if (cleanOld) obj->PositionChange(context, false);
    return true;
}

int32_t VMAvatar::GetSlotHeight(int32_t /*slot*/) {
    return 5; //in hand
    //TODO: verify
}

VMEntity* VMAvatar::GetSlot(int32_t slot) {
    return slot >= 0 && slot < TotalSlots() ? Contained[(size_t)slot] : nullptr;
}

void VMAvatar::ClearSlot(int32_t slot) {
    auto child = GetSlot(slot);
    if (child == nullptr) return;
    if (Thread && Thread->Context) Thread->Context->InvalidateRoofObject(child);
    child->Container = nullptr;
    child->ContainerSlot = -1;
    if (slot == 0) CarryAnimationState = nullptr; // independent effect slots do not own the hand pose

    if (UseWorld && child->WorldUI) {
        child->WorldUI->Container = nullptr;
        child->WorldUI->ContainerSlot = 0;
        auto objC = dynamic_cast<ObjectComponent*>(child->WorldUI.get());
        if (objC != nullptr) objC->ForceDynamic = false;
    }
    Contained[(size_t)slot] = nullptr;
}

// End Container SLOTs interface

void VMAvatar::SetRoom(uint16_t room) {
    VMEntity::SetRoom(room);
    if (VM::UseWorld() && WorldUI) WorldUI->Room = (uint16_t)GetValue(VMStackObjectVariable::Room);
}

void* VMAvatar::GetIcon(void* /*gd*/, int32_t /*store*/) {
    // STUB(3ds): returned the head/body outfit thumbnail (or pet icon) as a Texture2D.
    return nullptr;
}

}}
