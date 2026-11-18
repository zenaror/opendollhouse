/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.VMAvatar (entities/VMAvatar.cs)
// Motives, person data, relationships, routing/animation state: translated.
// STUB(3ds): body mesh / outfit / head rendering, HIT sound events, thumbnails (see STUBS.md).
#pragma once
namespace FSO { namespace SimAntics { namespace Engine { namespace Utils { class InteractionCheckpointCodec; }}}}
#include <array>
#include <unordered_set>
#include "simantics/entities/VMEntity.h"
#include "simantics/model/VMMotive.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "simantics/model/VMMotiveChange.h"
#include "simantics/model/VMAnimationState.h"
#include "simantics/model/TSOPlatform/VMTSOAvatarState.h"
#include "simantics/model/VMTS1Appearance.h"
#include "vitaboy/Appearance.h"

namespace FSO { namespace HIT { class HITThread; } }
namespace FSO { namespace SimAntics {
namespace Entities { class VMTS1MotiveDecay; }

enum class VMAvatarType : uint8_t { Adult, Child, Cat, Dog };

class VMAvatarDefaultSuits {
public:
    uint64_t Daywear;
    uint64_t Swimwear;
    uint64_t Sleepwear;
    explicit VMAvatarDefaultSuits(bool female) {
        Daywear = 0x24C0000000DULL;
        Swimwear = (uint64_t)((female) ? 0x620000000DULL : 0x5470000000DULL);
        Sleepwear = (uint64_t)((female) ? 0x5150000000DULL : 0x5440000000DULL);
    }
};

// STUB(3ds): FSO.LotView.Model.XmlCharacter (FreeSims character xml). Only the fields SetAvatarData reads.
class XmlCharacter {
public:
    struct DataEntry { int32_t Value = 0; };
    std::string Id = "0";
    std::string Name;
    std::string Appearance = "Light";
    std::string Gender = "Male";
    std::string Head = "0";
    std::string Body = "0";
    std::vector<DataEntry> Data;
};

class VMAvatar : public VMEntity {
public:
    // CTSS identity provenance; Name remains source bytes in the engine.
    uint8_t PersonalTextLanguage = 1;
    bool PersonalTextUTF8 = false, PersonalTextAmbiguous = false;
    static uint32_t TEMPLATE_PERSON;
    static uint32_t TEMPLATE_NPC;
    static uint32_t NPC_MAID;
    static uint32_t NPC_GARDENER;
    static uint32_t DOG_TEMPLATE;
    static uint32_t CAT_TEMPLATE;

    std::unique_ptr<FSO::Vitaboy::SimAvatar> Avatar;

    /** Animation vars **/
    Model::TSOPlatform::VMTSOAvatarState* AvatarState; // owned by PlatformState

    std::vector<std::shared_ptr<Model::VMAnimationState>> Animations;
    Model::VMAnimationState* CurrentAnimationState() const { return (Animations.size() == 0) ? nullptr : Animations[0].get(); }
    std::shared_ptr<Model::VMAnimationState> CarryAnimationState;
    bool TS1RawAnimationClock = false;
    bool TS1HurryableAnimation = false;
    // The isolated CAS preview owns a separate renderer clock/controller.
    bool TS1ExternalAnimationClock = false;
    float TS1MotionX = 0, TS1MotionY = 0;
    // The original publishes a path-constrained integer position while the
    // model retains its unconstrained floating-point proposal.
    int16_t TS1MotionPublishedX = 0, TS1MotionPublishedY = 0;
    int8_t TS1MotionLevel = 0;
    bool TS1MotionPositionKnown = false;
    float TS1MotionRate = 1.f;
    int64_t TS1MotionRateTick = -1;
    int32_t TS1AnimationTimestamp() const;
    void StartTS1Animation(const std::shared_ptr<Model::VMAnimationState>& state, float rate = 1.f);
    void UpdateTS1MotionRate(int modelState);

    const std::string& Message() const { return m_Message; }
    void Message(const std::string& value);

    int32_t MessageTimeout = 0;
    st::Vector3 Velocity; //used for 60 fps walking animation
    bool Visitor = false;

    /*
        APPEARANCE DATA
    */
    VMAvatarDefaultSuits DefaultSuits = VMAvatarDefaultSuits(false);
    std::unordered_set<std::string> BoundAppearances;
    /// TS1(sims): The Sims 1 body/head/hand mesh + texture names from this person's body strings
    /// (character iff STR# 200), filled in Init. FreeSims' BodyOutfit/HeadOutfit are TSO outfit ids and are
    /// random placeholders for TS1 people; the body (Vitaboy) code should read this instead.
    Model::VMTS1Appearance TS1Appearance;

    uint64_t BodyOutfit() const { return _BodyOutfit; }
    void BodyOutfit(uint64_t value);
    uint64_t HeadOutfit() const { return _HeadOutfit; }
    void HeadOutfit(uint64_t value);
    FSO::Vitaboy::AppearanceType SkinTone() const { return _SkinTone; }
    void SkinTone(FSO::Vitaboy::AppearanceType value);

    st::Vector3 VisualPosition() const override;
    void VisualPosition(const st::Vector3& value) override;
    float RadianDirection() const override { return _RadianDirection; }
    void RadianDirection(float value) override;
    DirectionT Direction() const override;
    void Direction(DirectionT value) override;

    //inferred properties
    std::array<std::string, 50> WalkAnimations;
    std::array<std::string, 50> SwimAnimations;

    /// FreeSims VMAvatar.SubmitHITVars: gives a HIT thread this Sim's data fields (TS1: field 0 = gender, 0 male,
    /// 1 female, 2 child; arg1 = the speaking instance, arg2 = gender).
    void SubmitHITVars(FSO::HIT::HITThread* thread);
    /// TS1(sims): the source fields any object passes (arg1 = instance).
    static void SubmitSourceVars(FSO::HIT::HITThread* thread, int16_t objectID);

    bool IsPet();
    /// FreeSO VMAvatar.ShowMoneyHeadline: the amount floating over the Sim's head (person data MoneyAmmountOverHead,
    /// expression scope MoneyOverHead32Bit). STUB(3ds): instead of a Money headline renderer the UI polls these
    /// (MoneyHeadlineSerial changes on every call) and draws the TS1 "+§n" text over the head.
    void ShowMoneyHeadline(int32_t value) { MoneyHeadlineValue = value; MoneyHeadlineSerial++; }
    int32_t MoneyHeadlineValue = 0;
    uint32_t MoneyHeadlineSerial = 0;
    bool IsDog();
    bool IsCat();

    explicit VMAvatar(FSO::Content::GameObject* obj);
    ~VMAvatar() override;

    void SetAvatarType(FSO::Files::Formats::IFF::Chunks::STR* data);
    void SetAvatarBodyStrings(FSO::Files::Formats::IFF::Chunks::STR* data, VMContext* context);
    uint64_t SetAvatarRandomOutfit(int32_t Type);
    void SetAvatarData(XmlCharacter* charInfo);
    void InitBodyData(VMContext* context);
    void Init(VMContext* context) override;
    void PrepareForRestore(VMContext* context) override;
    void FinishRestore(VMContext* context) override;
    /// Saved data installation without action/UI callbacks; FinishRestore publishes visuals.
    void RestorePersonData(const std::vector<int16_t>& person, const float (&motives)[16], const float* oldMotives = nullptr);
    void Reset(VMContext* context) override;
    void Tick() override;
    /// Advance authored animation channels/events without a household tick.
    /// World Tick calls this same block; isolated UI previews can reuse it.
    void AdvanceAnimations(bool allowSound = true);
    void HandleTimePropsEvent(FSO::Vitaboy::TimePropertyListItem& tp, bool allowSound, Model::VMAnimationState* eventOwner);
    void UserLeaveLot();
    void FractionalAnim(float fraction);

    virtual int16_t GetPersonData(Model::VMPersonDataVariable variable);
    virtual void SetMotiveChange(Model::VMMotive motive, int16_t PerHourChange, int16_t MaxValue);
    bool HasMotiveChange(Model::VMMotive motive);
    const std::vector<Model::VMTS1MotiveChange>& GetTS1MotiveChanges() const { return TS1MotiveChanges; }
    void RestoreTS1MotiveChanges(const std::vector<Model::VMTS1MotiveChange>& changes);
    /// Compatibility summary (last matching record); TS1 persistence uses the ordered vector.
    Model::VMMotiveChange GetMotiveChange(int index) const;
    virtual void ClearMotiveChanges();
    virtual bool SetPersonData(Model::VMPersonDataVariable variable, int16_t value);
    virtual int16_t GetMotiveData(Model::VMMotive variable);
    float GetMotiveDataFloat(Model::VMMotive variable) const;
    float GetOldMotiveDataFloat(Model::VMMotive variable) const;
    void SetMotiveDataFloat(Model::VMMotive variable, float value);
    void SnapshotMotiveData();
    virtual bool SetMotiveData(Model::VMMotive variable, int16_t value);

    std::string ToString() override;
    std::unique_ptr<Model::Routing::VMObstacle> GetObstacle(const LotTilePos& pos, DirectionT dir) override;
    // Rebuild the occupied TS1 rectangle without executing placement trees.
    void RefreshTS1Footprint();
    // DVD 0045e839: reset the person interaction's route footprint state.
    void ClearTS1InteractionFootprint();
    void RestoreTS1Footprint(int minX, int minY, int maxX, int maxY);
    void PositionChange(VMContext* context, bool noEntryPoint) override;
    void PrePositionChange(VMContext* context) override;

    // Begin Container SLOTs interface
    int32_t TotalSlots() override;
    bool PlaceInSlot(VMEntity* obj, int32_t slot, bool cleanOld, VMContext* context) override;
    int32_t GetSlotHeight(int32_t slot) override;
    VMEntity* GetSlot(int32_t slot) override;
    void ClearSlot(int32_t slot) override;
    // End Container SLOTs interface

    void SetRoom(uint16_t room) override;
    void* GetIcon(void* gd, int32_t store) override; // STUB(3ds)

private:
    void UpdateHeadSeek();
    void ApplyHeadSeek(float fraction);
    friend class FSO::SimAntics::Engine::Utils::InteractionCheckpointCodec;
    std::string m_Message = "";
    std::array<Model::VMMotiveChange, 16> MotiveChanges;
    std::unique_ptr<Entities::VMTS1MotiveDecay> MotiveDecay;
    std::array<int16_t, 101> PersonData; // C# allocates 100 but allows index 100 through the bounds check
    std::array<float, 16> MotiveData;
    std::array<float, 16> OldMotiveData{};
    std::vector<Model::VMTS1MotiveChange> TS1MotiveChanges;
    // TS1 cXPerson initializes five independent containment slots.
    std::array<VMEntity*, 5> Contained{};
    float _RadianDirection = 0;

    int32_t KillTimeout = -1;
    static const int32_t FORCE_DELETE_TIMEOUT;
    const uint16_t LEAVE_LOT_TREE = 8373;

    uint64_t _BodyOutfit = 0;
    uint64_t _HeadOutfit = 0;
    FSO::Vitaboy::AppearanceType _SkinTone = FSO::Vitaboy::AppearanceType::Light;
    FSO::Files::Formats::IFF::Chunks::STR* BodyStrings = nullptr;
    VMAvatarType AvatarType = VMAvatarType::Adult;

    void HandleTimePropsEvent(FSO::Vitaboy::TimePropertyListItem& tp);
    void HandleTimePropsEvent(FSO::Vitaboy::TimePropertyListItem& tp, bool allowSound);
    FSO::LotView::Components::AvatarComponent* AvaUI() const { return (FSO::LotView::Components::AvatarComponent*)WorldUI.get(); }
};
}}
