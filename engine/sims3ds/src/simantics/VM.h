/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.VM — Simantics Virtual Machine.
#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include "simantics/_files_contract.h"
#include "simantics/engine/translator/VMRoutine.h"
#include "simantics/engine/VMTS1Travel.h"
#include "simantics/model/VMDialogInfo.h"
#include "simantics/model/VMInventoryItem.h"
#include "simantics/model/VMTuningCache.h"
#include "simantics/model/DynamicTuning.h"
#include <deque>
#include "simantics/model/VMPlatformState.h"
#include "simantics/model/VMRuntimeHeadline.h"
#include "simantics/engine/TSOGlobalLink/VMTSOGlobalLinkStub.h"

class VMWorldActivator;
namespace FSO { namespace SimAntics {
class VMContext;
class VMEntity;
class VMPieMenuInteraction;
namespace Engine { class VMStackFrame; namespace Utils { class VMTS1InteractionCheckpoint; } }
class VMFreeWill;
namespace Entities { class VMMultitileGroup; }
namespace Model { class VMSetToNextCache; namespace TSOPlatform { class VMTSOLotState; } }

struct VMTS1RemovalRequest {
    int16_t ID;
    VMEntity* Identity;
};

struct VMTS1ObjectIdentity {
    int16_t ID;
    uint32_t GUID;
};

class VMSandboxRestoreState {
public:
    std::vector<VMEntity*> Entities;
    std::unordered_map<int16_t, VMEntity*> ObjectsById;
    int16_t ObjectId = 1;
    std::unique_ptr<Model::VMSetToNextCache> SetToNext;
    std::deque<VMTS1RemovalRequest> PendingRemovals;
    bool TS1ObjectManagerOrderKnown = false;
    std::vector<VMEntity*> TS1ObjectManagerOrder;
    std::vector<VMTS1ObjectIdentity> TS1RemovedObjectIdentities;
};

// TS1 source purse and six ordered daily records: current, then five older.
// Initialized distinguishes a loaded/assigned purse from a fresh VM's legacy mirrors.
struct VMTS1BudgetState {
    using DaysType = std::array<std::array<int32_t, 8>, 6>;
    int32_t Purse = 0;
    DaysType Days{};
    bool Initialized = false;
    // In-memory admission token for a validated old marker-only SIMI. Never
    // serialized as provenance; binds its exact loaded chunk trio for saving.
    bool LegacyCandidateValidated = false;
    bool LegacyOwnerRestricted = false, LegacyPromoted = false;
    int16_t LegacyOwnerID = 0;
    uint32_t LegacySIMIHash = 0, LegacyOBJMHash = 0, LegacyODHIHash = 0;
};

/// Simantics Virtual Machine.
class VM {
public:
    static bool UseWorld();
    static void UseWorld(bool value);

    bool IsServer() const { return GlobalLink() != nullptr; }

    VMContext* Context; // not owned (the caller creates the VMContext and keeps it alive)
    VMWorldActivator* Activator = nullptr;
    std::unique_ptr<VMFreeWill> FreeWill;

    int32_t SpeedMultiplier = 1;
    int32_t LastSpeedMultiplier = 0;

    std::vector<Model::VMInventoryItem> MyInventory;
    std::vector<VMEntity*> Entities;
    // The DVD manager's birth/load list is separate from sorted dispatch.
    // Unknown synthetic/legacy graphs must not infer this order from IDs.
    bool TS1ObjectManagerOrderKnown = false;
    std::vector<VMEntity*> TS1ObjectManagerOrder;
    void ValidateTS1ObjectManagerOrder() const;
    bool EstablishTS1ObjectManagerOrderFromClassic(
        const FSO::Files::Formats::IFF::Chunks::OBJM& objects);
    bool HasTS1ObjectIdentity(int16_t id, uint32_t guid) const;
    std::unordered_set<VMEntity*> SoundEntities;

    std::unique_ptr<Model::DynamicTuning> Tuning;
    std::unique_ptr<Model::VMTuningCache> TuningCache;

    // The DVD keeps a preference byte separate from script global 30.
    // Explicit preference application synchronizes both; unrelated settings do not.
    bool FreeWillEnabled = true;
    void ApplyTS1FreeWillPreference(bool enabled) {
        FreeWillEnabled = enabled;
        if (TS1 && GlobalState.size() > 30) SetGlobalValue(30, enabled ? 1 : 0);
    }

    bool TS1 = true;   // sims: this port runs The Sims 1 content only (FreeSims defaulted to false)

    std::vector<int16_t> GlobalState;
    VMTS1BudgetState TS1Budget;
    std::shared_ptr<Model::VMPlatformState> PlatformState;
    Model::TSOPlatform::VMTSOLotState* TSOState() const;
    std::string LotName;

    Model::VMHeadlineRendererProvider* Headline;

    bool Ready = false;
    bool BHAVDirty = false;
    uint32_t MyUID = 0; //UID of this client in the VM

    // C# events
    std::function<void(Model::VMDialogInfo&)> OnDialog;
    // ADDED(sims): diagnostic hook, called for every SimAntics exception a thread catches (before the
    // object reset), including the ones whose dialog is suppressed by VMThread::DialogCooldown.
    std::function<void(VMEntity* entity, VMEntity* callee, const std::string& message)> OnSimanticsException;
    // ADDED(sims): diagnostic hook, called when TS1 Find Best Interaction queues an action (the top candidates
    // considered, best first, and the one chosen by the weighted random pick).
    std::function<void(VMEntity* caller, const std::vector<VMPieMenuInteraction>& top, const VMPieMenuInteraction& chosen)> OnAutonomyChoice;
    // ADDED(sims): diagnostic hook, called after every primitive a (non-check) thread executes (result code), and
    // with result 255 when it calls a subroutine. Null by default (no cost).
    std::function<void(Engine::VMStackFrame* frame, uint16_t opcode, int result)> OnTraceInstruction;
    // Diagnostic hook called before VMThread retires a frame; result is the exact Pop input.
    // Null by default so frame-return observation adds no work when unused.
    std::function<void(const Engine::VMStackFrame* frame, int result)> OnTraceFrameReturn;
    // TS1(sims): FreeSO VM.OnRequestLotSwitch — Generic Sims Call 17 "Change To Lot In Temp 0" (-1 = the family's
    // home lot) asks the front-end to save this lot and load another. See engine/VMTS1Travel.h for the whole API.
    std::function<void(int16_t lotId)> OnRequestLotSwitch;
    Engine::Utils::TS1Departure Departure;
    // TS1(sims): UI events raised by expansion pack calls (VMTS1Travel::Event*: build/buy enabled, selected Sim
    // changed, cancel pie menu, head flush). FreeSO VM.SignalGenericVMEvt.
    std::function<void(int32_t evt, int32_t value)> OnTS1Event;
    bool TS1BuildBuyEnabled = true; // TS1(sims): Generic Sims Call 10/11 (FreeSO Architecture.BuildBuyEnabled)

    // TS1(sims): saved object ids. VMWorldActivator restores The Sims 1 house state (OBJM) with the ids the
    // objects had when the house was saved, because attributes, relationships and containers refer to those ids.
    // AddEntity gives an entity whose GUID is in PendingIDsByGUID that id; NextObjID never hands out a reserved id.
    std::unordered_map<uint32_t, int16_t> PendingIDsByGUID;
    std::unordered_set<int16_t> ReservedObjectIDs;
    std::function<void()> OnFullRefresh;
    std::function<void(VMEntity*)> OnBreakpoint;

    /// STUB(3ds): IVMTSOGlobalLink comes from the (removed) net driver; always null on 3DS.
    Engine::TSOTransaction::IVMTSOGlobalLink* GlobalLink() const { return nullptr; }
    Engine::TSOTransaction::VMTSOGlobalLinkStub CheckGlobalLink;

    /// Constructs a new Virtual Machine instance.
    VM(VMContext* context, Model::VMHeadlineRendererProvider* headline);
    ~VM();

    /// Gets an entity from this VM.
    VMEntity* GetObjectById(int16_t id);
    VMEntity* GetObjectByPersist(uint32_t id);

    /// Initializes this Virtual Machine.
    void Init();
    void Reset();
    void UpdateTuning();
    void Update();
    void InternalTick();
    // FreeSO Scheduler.RunningNow: only entity execution shares check temporaries.
    bool EntityTickRunning() const { return EntityTickActive; }

    /// Adds an entity to this Virtual Machine.
    void AddEntity(VMEntity* entity);
    static void AddToObjList(std::vector<VMEntity*>& list, VMEntity* entity);
    /// Removes an entity from this Virtual Machine.
    void RemoveEntity(VMEntity* entity);
    // Script removal during ordinary TS1 dispatch is committed after the
    // completed sweep and clock step. Internal rollback/UI deletion stays immediate.
    // Script requests also queue outside ordinary dispatch: the DVD request
    // helper enqueues before testing its interpreter execution mode.
    bool QueueTS1Removal(VMEntity* entity, bool scriptRequest = false);
    bool HasTS1PendingRemovals() const { return !TS1PendingRemovals.empty() || TS1DrainingRemovals; }
    // A completed script request may remain queued while the game is paused.
    // Snapshotting must neither execute it nor interrupt its dispatch/drain.
    bool CanCaptureTS1RemovalState() const;
    bool TS1ScriptImmediateMode() const;
    // Includes temporary check machines, which are not Entity::Thread.
    class TS1InterpreterScope {
        VM& Owner;
        bool Tracked;
    public:
        explicit TS1InterpreterScope(VM& owner):Owner(owner),Tracked(owner.TS1) {
            if (Tracked) ++Owner.TS1InterpreterCalls;
        }
        ~TS1InterpreterScope() noexcept { if (Tracked) --Owner.TS1InterpreterCalls; }
        TS1InterpreterScope(const TS1InterpreterScope&) = delete;
        TS1InterpreterScope& operator=(const TS1InterpreterScope&) = delete;
    };
    std::vector<int16_t> GetTS1PendingRemovalIDs() const;
    std::deque<VMTS1RemovalRequest> PrepareTS1PendingRemovalRestore(const std::vector<int16_t>& ids) const;
    // Only commit a validated queue prepared against this VM's live objects.
    void CommitTS1PendingRemovalRestore(std::deque<VMTS1RemovalRequest>& prepared) noexcept;
    static void DeleteFromObjList(std::vector<VMEntity*>& list, VMEntity* entity);

    /// Gets a global value set for this Virtual Machine.
    int16_t GetGlobalValue(uint16_t var);
    /// Sets a global value for this Virtual Machine.
    bool SetGlobalValue(uint16_t var, int16_t value);

    /// Assembles a set of instructions. (routines are cached per BHAV and live for the program lifetime)
    VMRoutine* Assemble(FSO::Files::Formats::IFF::Chunks::BHAV* bhav);
    static void BHAVChanged(FSO::Files::Formats::IFF::Chunks::BHAV* bhav);
    /// TS1(sims): drops every assembled routine (the cache is keyed by BHAV*; call before the content manager
    /// frees a lot's object files). Only when no VM is alive. Returns false (nothing done) while a VM exists.
    static bool ReleaseAssembled();

    /// Signals a Dialog to all listeners. (usually a UI)
    void SignalDialog(Model::VMDialogInfo& info);
    // ADDED(primitives): C# SignalDialog(null) (dialog dismissed, used by VMDialogPrivateStrings). OnDialog takes a
    // reference, so the "null" signal goes to OnDialogClosed instead.
    std::function<void()> OnDialogClosed;
    void SignalDialog(std::nullptr_t) { if (OnDialogClosed) OnDialogClosed(); }

    std::unique_ptr<VMSandboxRestoreState> Sandbox();
    void SandboxRestore(std::unique_ptr<VMSandboxRestoreState> state);

    void BreakpointHit(VMEntity* entity);

    // ---- ownership (C# relied on the GC) ----
    /// Takes ownership of an entity allocated with new; returns it. Entities live until the VM is destroyed
    /// (stack frames, queues and caches may still reference removed entities, as in C#).
    VMEntity* OwnEntity(VMEntity* entity);
    Entities::VMMultitileGroup* OwnGroup(Entities::VMMultitileGroup* group);

private:
    std::unordered_map<int16_t, VMEntity*> ObjectsById;
    int16_t ObjectId = 1;
    int32_t LastFrameSpeed = 1;
    friend class Engine::Utils::VMTS1InteractionCheckpoint;
    bool AlternateTick = false;
    bool EntityTickActive = false; // transient execution context; never saved
    uint32_t TS1InterpreterCalls = 0; // transient; snapshots require zero
    std::deque<VMTS1RemovalRequest> TS1PendingRemovals;
    bool TS1DrainingRemovals = false;
    VMEntity* TS1RemovingNow = nullptr;
    void DrainTS1Removals();
    std::vector<std::unique_ptr<VMEntity>> EntityStore;
    // Actual registered removals, not ownership/cache entries. This local
    // history distinguishes deletion from unavailable historical content.
    std::vector<VMTS1ObjectIdentity> TS1RemovedObjectIdentities;
    std::vector<std::unique_ptr<Entities::VMMultitileGroup>> GroupStore;
    static std::unordered_map<FSO::Files::Formats::IFF::Chunks::BHAV*, std::unique_ptr<VMRoutine>>& _Assembled();
    static std::vector<VM*>& LiveVMs();
    void Tick();
    int16_t NextObjID();
    void VM_OnBHAVChange() { BHAVDirty = true; }
public:
    // Original TS1 dialog Type3 -> global string006e4a70 -> Make19.
    // The submitted input is UTF8; it never renames an existing entity.
    // Native checkpoints preserve this last-input channel (including empty).
    static constexpr size_t TS1TextInputMaxBytes = 4096;
    static bool IsTS1TextInputValid(const std::string& text);
    std::string TS1TextInput;

};
}}
