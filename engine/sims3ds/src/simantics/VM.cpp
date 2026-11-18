#include "simantics/primitives/VMTS1Budget.h"
/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.VM
#include "simantics/VM.h"
#include "simantics/utils/VMTS1ObjectAge.h"
#include "simantics/model/VMStackObjectVariable.h"
#include "hit/HITSound.h"
#include "simantics/VMContext.h"
#include "simantics/VMAI.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/engine/VMTS1Travel.h"
#include "simantics/engine/translator/VMTranslator.h"
#include "simantics/model/TSOPlatform/VMTSOLotState.h"
#include "content/ContentManager.h"
#include <algorithm>
#include <stdexcept>
#include "json.hpp"

namespace FSO { namespace SimAntics {
using namespace FSO::SimAntics::Model;
using FSO::Files::Formats::IFF::Chunks::BHAV;

bool VM::IsTS1TextInputValid(const std::string& text) {
    return text.size() <= TS1TextInputMaxBytes && text.find('\0') == std::string::npos
        && odh_json_detail::valid_utf8(text);
}

static bool _UseWorld = true;
bool VM::UseWorld() { return _UseWorld; }
void VM::UseWorld(bool value) {
    _UseWorld = value;
    VMContext::UseWorld = value;
    VMEntity::UseWorld = value;
}

TSOPlatform::VMTSOLotState* VM::TSOState() const {
    return (PlatformState != nullptr && PlatformState->IsTSOLotState()) ? (TSOPlatform::VMTSOLotState*)PlatformState.get() : nullptr;
}

std::unordered_map<BHAV*, std::unique_ptr<VMRoutine>>& VM::_Assembled() {
    static std::unordered_map<BHAV*, std::unique_ptr<VMRoutine>> map;
    return map;
}
std::vector<VM*>& VM::LiveVMs() { // subscribers of the static OnBHAVChange event
    static std::vector<VM*> list;
    return list;
}

VM::VM(VMContext* context, VMHeadlineRendererProvider* headline) {
    context->VM = this;
    this->Context = context;
    Headline = headline;
    TuningCache.reset(new VMTuningCache());
    FreeWill.reset(new VMFreeWill(this));
    LiveVMs().push_back(this); // OnBHAVChange += VM_OnBHAVChange;
}

VM::~VM() {
    auto& l = LiveVMs();
    l.erase(std::remove(l.begin(), l.end(), this), l.end());
    for (auto& e : EntityStore) e->Thread.reset(); // threads reference entities; drop them first
}

VMEntity* VM::OwnEntity(VMEntity* entity) {
    EntityStore.emplace_back(entity);
    return entity;
}
Entities::VMMultitileGroup* VM::OwnGroup(Entities::VMMultitileGroup* group) {
    GroupStore.emplace_back(group);
    return group;
}

/// Gets an entity from this VM.
VMEntity* VM::GetObjectById(int16_t id) {
    auto it = ObjectsById.find(id);
    if (it != ObjectsById.end()) return it->second;
    return nullptr;
}

VMEntity* VM::GetObjectByPersist(uint32_t id) {
    for (auto x : Entities) if (x->PersistID == id) return x;
    return nullptr;
}

/// Initializes this Virtual Machine.
void VM::Init() {
    Context->Globals = FSO::Content::Content::Get()->WorldObjectGlobals->Get("global", false);
    PlatformState = std::make_shared<TSOPlatform::VMTSOLotState>();
    PlatformState->ActivateValidator(this);
    GlobalState.assign(38, 0); // FIX(sims): FreeSO `new short[38]` (TS1 globals 0-37)
    GlobalState[20] = 255; //Game Edition. Basically, what "expansion packs" are running. Let's just say all of them.
    GlobalState[25] = 4; //as seen in EA-Land edith's simulator globals, this needs to be set for people to do their idle interactions.
    GlobalState[17] = 4; //Runtime Code Version, is this in EA-Land.
    // STUB(3ds): if (Driver is VMServerDriver) EODHost = new VMEODHost();
}

void VM::Reset() {
    std::vector<VMEntity*> avatars;
    for (auto x : Entities) if (dynamic_cast<VMAvatar*>(x) != nullptr && x->PersistID > 65535) avatars.push_back(x);
    //TODO: all avatars with persist ID are not npcs in TSO. right now though everything has a persist ID...
    for (auto avatar : avatars) avatar->Delete(true, Context);

    auto ents = Entities;
    for (auto ent : ents) {
        if (ent->Thread != nullptr) {
            if (ent->Thread->BlockingState != nullptr) ent->Thread->BlockingState = nullptr;
            if (ent->Thread->EODConnection != nullptr) ent->Thread->EODConnection = nullptr;
        }
        if (ent->Object->OBJ->GUID == 0x3929AADC) ent->Delete(true, Context);
    }
    //foreach (var ent in ents) ent.Reset(Context); duplicates dogs apparently?? tf
}

void VM::UpdateTuning() {
    TuningCache->UpdateTuning(this);
}

void VM::Update() {
    // The primitive returns to its true edge within the dispatching tick.
    // Additional updates at high speed must yield to the pending lot switch.
    if (Departure.caller) return;
    if (LastFrameSpeed != SpeedMultiplier) {
        // C# collected the sounds of SoundEntities, a set FreeSims never fills (so its pause never happened).
        // TS1(sims): every entity's sounds: TVs, radios and loops pause with the game, as in The Sims.
        std::vector<std::shared_ptr<FSO::HIT::HITSound>> allSounds;
        for (auto ent : Entities) {
            for (auto& x : ent->SoundThreads) if (x.Sound != nullptr) allSounds.push_back(x.Sound);
        }

        if (SpeedMultiplier < 1 && LastFrameSpeed >= 1) for (auto& x : allSounds) x->Pause();
        else if (SpeedMultiplier >= 1 && LastFrameSpeed < 1) for (auto& x : allSounds) x->Resume();
        LastFrameSpeed = SpeedMultiplier;
    }

    if (!Ready || AlternateTick) {
        Tick();
    } else {
        //fractional animation for avatars
        for (auto obj : Entities) {
            auto ava = dynamic_cast<VMAvatar*>(obj);
            if (ava != nullptr) ava->FractionalAnim(0.5f);
        }
    }
    AlternateTick = !AlternateTick;
}

void VM::Tick() {
    if (BHAVDirty) {
        for (auto ent : Entities) if (ent->Thread != nullptr) ent->Thread->RoutineDirty = true;
        BHAVDirty = false;
    }

    //Tick for the Free Will control
    // TS1(sims): FreeSims' random visitor "free will" is not TS1 behaviour (FreeSO removed it): TS1 Sims and
    // visitors choose through their own main trees ('try autonomy' -> Find Best Interaction).
    if (Context->Blueprint != nullptr && !TS1)
        FreeWill->Tick(this);

    // STUB(3ds): C# ran `Driver.Tick(this)` (VMNetDriver: local/client/server) which applied queued net
    // commands and then called InternalTick. Single player on 3DS: tick directly.
    InternalTick();
    Ready = true;
}

// DVD daily callback4322f2: calendar age precedes value depreciation.
static void TS1DailyObjects(VM& vm) {
    using V=FSO::SimAntics::Model::VMStackObjectVariable;
    auto narrow=[](int32_t n) { uint16_t b=uint16_t(n);return b<=0x7fff?int16_t(b):int16_t(int32_t(b)-0x10000); };
    for (auto e:vm.Entities) {
        if (!e || e->Dead || e->GhostImage) continue;
        Utils::RefreshTS1ObjectAge(*e,*vm.Context->Clock);
        auto def=e->Object?e->Object->OBJ:nullptr;
        if (!def) continue;
        if (def->MasterID!=0) {
            if (!dynamic_cast<VMGameObject*>(e) || !e->MultitileGroup ||
                e->MultitileGroup->BaseObject()!=e || !e->MasterDefinition) continue;
            def=e->MasterDefinition;
        }
        if (def->SelfDepreciating) continue;
        const int value=e->GetValue(V::CurrentValue);
        const int loss=(value==narrow(def->Price) && def->InitialDepreciation)
            ? narrow(def->InitialDepreciation):narrow(def->DailyDepreciation);
        const auto next=std::max<int16_t>(narrow(value-loss),narrow(def->DepreciationLimit));
        e->SetValue(V::CurrentValue,next);
        if (e->MultitileGroup && e->MultitileGroup->BaseObject()==e) e->MultitileGroup->Price=next;
    }
}

void VM::InternalTick() {
    if (TS1 && TS1DrainingRemovals)
        throw std::runtime_error("Object removal did not finish; reload the last saved lot");
    // STUB(3ds): GlobalLink.Tick / EODHost.Tick — no TSO server.
    auto publishClock = [&]() {
        GlobalState[6] = (int16_t)Context->Clock->Seconds();
        GlobalState[5] = (int16_t)Context->Clock->Minutes;
        GlobalState[0] = (int16_t)Context->Clock->Hours;
        // TS1(sims): FreeSO GetGlobalValue reads Day of Month / Month / Year (globals 1, 7, 8) from the clock.
        GlobalState[1] = (int16_t)Context->Clock->DayOfMonth;
        GlobalState[7] = (int16_t)Context->Clock->Month;
        GlobalState[8] = (int16_t)Context->Clock->Year;
        GlobalState[4] = (int16_t)Context->Clock->TimeOfDay();
        Context->ObserveSpatialPeriod();
    };
    auto advanceClock = [&]() {
        const auto previousDay=Context->Clock->DayOfMonth;
        Context->Clock->Tick(TS1);
        publishClock();
        if (TS1 && Context->Clock->DayOfMonth!=previousDay) {
            // The original increments this short once at the ordinary day carry.
            const uint16_t bits=uint16_t(GlobalState[28])+uint16_t(1);
            GlobalState[28]=bits<=0x7fff ? int16_t(bits) : int16_t(int32_t(bits)-0x10000);
            TS1DailyObjects(*this);
            // FAMh statistical-history append is a separate source callback;
            // it is still unimplemented, not a second financial transaction.
            Primitives::VMTS1Budget::RolloverDay(this);
            Context->Clock->FinishCalendarCarry();
            GlobalState[1]=int16_t(Context->Clock->DayOfMonth);
            GlobalState[7]=int16_t(Context->Clock->Month);
            GlobalState[8]=int16_t(Context->Clock->Year);
        }
    };
    // TS1 dispatch observes the old time; the clock advances only after the
    // completed ordinary sweep. Keep the independent TSO order unchanged.
    if (TS1) publishClock();
    else advanceClock();

    Context->Architecture()->Tick();

    {
        // Match the scheduler boundary, not the whole VM update: architecture
        // work and frontend queries outside this region clone check registers.
        struct EntityTickScope {
            bool& active;
            bool previous;
            explicit EntityTickScope(bool& value) : active(value), previous(value) { active = true; }
            ~EntityTickScope() { active = previous; }
        } running(EntityTickActive);
        auto entCpy = Entities;
        for (auto obj : entCpy) {
            // TSO advances its stream per entity. The original TS1 dispatcher
            // performs no such draw, including for skipped/disabled entries.
            if (!TS1) Context->NextRandom(1);
            // The DVD gates the whole virtual Tick, including person motive
            // work. Direct checks/Init/callbacks do not enter this dispatch.
            // Its conditional bit17 context has no native binding yet.
            if (TS1 && (obj->Dead || !obj->TS1AdmitSchedulerTick())) continue;
            obj->Tick(); //run object specific tick behaviors, like lockout count decrement
        }
    }
    if (TS1) advanceClock();
    if (TS1) DrainTS1Removals();
    if (TS1) Engine::Utils::VMTS1Travel::KeepSelectedSim(this); // FIX(sims) H1: global 3 names a living Sim
    //Context.SetToNextCache.VerifyPositions(); use only for debug!
}

/// Adds an entity to this Virtual Machine.
void VM::AddEntity(VMEntity* entity) {
    // TS1(sims): a saved object being restored takes its saved id (see PendingIDsByGUID).
    bool pending = false;
    if (!PendingIDsByGUID.empty() && entity->Object != nullptr && entity->Object->OBJ != nullptr) {
        auto pit = PendingIDsByGUID.find(entity->Object->OBJ->GUID);
        if (pit != PendingIDsByGUID.end() && pit->second > 0 && ObjectsById.find(pit->second) == ObjectsById.end()) {
            entity->ObjectID = pit->second;
            ReservedObjectIDs.erase(pit->second);
            PendingIDsByGUID.erase(pit);
            pending = true;
        }
    }
    if (!pending) {
        if (ObjectsById.find(ObjectId) != ObjectsById.end() || ReservedObjectIDs.count(ObjectId)) ObjectId = NextObjID();
        entity->ObjectID = ObjectId;
    }
    ObjectsById[entity->ObjectID] = entity;
    AddToObjList(this->Entities, entity);
    if (!entity->GhostImage) Context->SetToNextCache->NewObject(entity);
    ObjectId = NextObjID();
}

void VM::AddToObjList(std::vector<VMEntity*>& list, VMEntity* entity) {
    if (list.size() == 0) { list.push_back(entity); return; }
    int id = entity->ObjectID - 1;
    int max = (int)list.size() - 1;
    int min = 0;
    while (max - 1 > min) {
        int mid = (max + min) / 2;
        int nid = list[mid]->ObjectID;
        if (id < nid) max = mid;
        else min = mid;
    }
    int index = (list[min]->ObjectID > id) ? min : ((list[max]->ObjectID > id) ? max : max + 1);
    list.insert(list.begin() + index, entity);
}

/// Removes an entity from this Virtual Machine.
bool VM::QueueTS1Removal(VMEntity* entity, bool scriptRequest) {
    if (!TS1 || !entity || entity->GhostImage ||
        (!EntityTickActive && !TS1DrainingRemovals && !scriptRequest)) return false;
    if (entity->Dead || entity == TS1RemovingNow) return true;
    if (GetObjectById(entity->ObjectID) != entity) return false;
    for (const auto& pending : TS1PendingRemovals)
        if (pending.ID == entity->ObjectID && pending.Identity == entity) return true;
    TS1PendingRemovals.push_back({entity->ObjectID,entity});
    return true;
}

bool VM::CanCaptureTS1RemovalState() const {
    if (EntityTickActive || TS1DrainingRemovals || TS1InterpreterCalls!=0) return false;
    return std::none_of(Entities.begin(),Entities.end(),[](const VMEntity* entity) {
        return entity && entity->Thread && entity->Thread->TS1SynchronousCallRunning();
    });
}

bool VM::TS1ScriptImmediateMode() const {
    if (!EntityTickActive) return true;
    // The DVD's immediate wrapper temporarily clears the execution-mode byte,
    // including calls nested inside an ordinary object's dispatch.
    return std::any_of(Entities.begin(),Entities.end(),[](const VMEntity* entity) {
        return entity && entity->Thread && entity->Thread->TS1InitializerRunning();
    });
}

std::vector<int16_t> VM::GetTS1PendingRemovalIDs() const {
    if (!CanCaptureTS1RemovalState())
        throw std::runtime_error("Object-removal dispatch is incomplete");
    std::vector<int16_t> ids;
    ids.reserve(TS1PendingRemovals.size());
    std::unordered_set<int16_t> seen;
    for (const auto& request:TS1PendingRemovals) {
        const auto found=ObjectsById.find(request.ID);
        if (!TS1 || request.ID<=0 || !request.Identity || found==ObjectsById.end() ||
            found->second!=request.Identity || request.Identity->ObjectID!=request.ID ||
            request.Identity->Dead || request.Identity->GhostImage || !request.Identity->Object ||
            !request.Identity->Object->OBJ || !request.Identity->Thread ||
            std::find(Entities.begin(),Entities.end(),request.Identity)==Entities.end() ||
            !seen.insert(request.ID).second)
            throw std::runtime_error("Invalid pending object-removal identity");
        ids.push_back(request.ID);
    }
    return ids;
}

std::deque<VMTS1RemovalRequest> VM::PrepareTS1PendingRemovalRestore(const std::vector<int16_t>& ids) const {
    if (!CanCaptureTS1RemovalState())
        throw std::runtime_error("Object-removal dispatch is incomplete");
    std::deque<VMTS1RemovalRequest> prepared;
    std::unordered_set<int16_t> seen;
    for (const auto id:ids) {
        const auto found=ObjectsById.find(id);
        const auto entity=found==ObjectsById.end()?nullptr:found->second;
        if (!TS1 || id<=0 || !entity || entity->ObjectID!=id || entity->Dead || entity->GhostImage ||
            !entity->Object || !entity->Object->OBJ || !entity->Thread ||
            std::find(Entities.begin(),Entities.end(),entity)==Entities.end() || !seen.insert(id).second)
            throw std::runtime_error("Invalid restored object-removal identity");
        prepared.push_back({id,entity});
    }
    return prepared;
}

void VM::CommitTS1PendingRemovalRestore(std::deque<VMTS1RemovalRequest>& prepared) noexcept {
    TS1PendingRemovals.swap(prepared);
}

void VM::DrainTS1Removals() {
    if (TS1DrainingRemovals || TS1PendingRemovals.empty()) return;
    struct Scope {
        VM& Owner;
        bool Complete = false;
        explicit Scope(VM& vm) : Owner(vm) { Owner.TS1DrainingRemovals=true; }
        // On failure retain the incomplete-drain barrier: no further sweep or
        // save may publish a partly removed group. A fresh lot load recovers.
        ~Scope() { if (Complete) { Owner.TS1RemovingNow=nullptr;Owner.TS1DrainingRemovals=false; } }
    } draining(*this);
    while (!TS1PendingRemovals.empty()) {
        const auto request=TS1PendingRemovals.front();
        TS1PendingRemovals.pop_front();
        auto* entity=GetObjectById(request.ID);
        if (entity!=request.Identity || !entity || entity->Dead) continue;
        TS1RemovingNow=entity;
        entity->Delete(true,Context);
        TS1RemovingNow=nullptr;
    }
    draining.Complete=true;
}

void VM::RemoveEntity(VMEntity* entity) {
    Context->RemoveSpatialEntity(entity);
    if (TS1) for (auto other : Entities)
        if (other && other!=entity && other->Thread) other->Thread->TS1ObjectUseTargetRemoved(entity);
    if (TS1 && entity && entity->Thread) entity->Thread->DiscardTS1ObjectUses();
    if (TS1) if (auto avatar = dynamic_cast<VMAvatar*>(entity)) {
        for (auto other : Entities) if (other && other->Thread)
            for (const auto& frame : other->Thread->Stack)
                if (frame && frame->IsRoutingFrame())
                    static_cast<Engine::VMRoutingFrame*>(frame.get())->ForgetAvatar(avatar);
    }
    auto it = std::find(Entities.begin(), Entities.end(), entity);
    if (it != Entities.end()) {
        Context->SetToNextCache->RemoveObject(entity);
        Entities.erase(it);
        ObjectsById.erase(entity->ObjectID);
        if (entity->ObjectID < ObjectId) ObjectId = entity->ObjectID; //this id is now the smallest free object id.
    }
    entity->Dead = true;
    Context->RemoveVisualActor(entity);
}

void VM::DeleteFromObjList(std::vector<VMEntity*>& list, VMEntity* entity) {
    if (list.size() == 0) { return; }
    int id = entity->ObjectID;
    int max = (int)list.size();
    int min = 0;
    while (max > min) {
        int mid = (max + min) / 2;
        int nid = list[mid]->ObjectID;
        if (id < nid) max = mid;
        else if (id == nid) {
            list.erase(list.begin() + mid); //found it
            return;
        }
        else min = mid + 1;
    }
    //list.RemoveAt(min);
}

/// Finds the next free object ID and remembers it for use when making another object.
int16_t VM::NextObjID() {
    for (int16_t i = ObjectId; i > 0; i++)
        if (ObjectsById.find(i) == ObjectsById.end() && ReservedObjectIDs.count(i) == 0) return i; // TS1(sims): reserved ids
    return 0;
}

int16_t VM::GetGlobalValue(uint16_t var) {
    // should this be in VMContext?
    // FIX(sims): FreeSO bounds globals by the array (TS1 has 38: Behavior.iff STR#129, 33 "Machine Level" ..
    // 37 "Filter Flags for Transit Screen" are read by the expansion packs' controllers). FreeSims capped at 32.
    if (var >= GlobalState.size()) throw std::runtime_error("Global Access out of bounds!");
    return GlobalState[var];
}

bool VM::SetGlobalValue(uint16_t var, int16_t value) {
    if (var >= GlobalState.size()) throw std::runtime_error("Global Access out of bounds!"); // FIX(sims): see Get
    GlobalState[var] = value;
    return true;
}

VMRoutine* VM::Assemble(BHAV* bhav) {
    auto& assembled = _Assembled();
    auto it = assembled.find(bhav);
    if (it != assembled.end()) return it->second.get();
    auto routine = Engine::VMTranslator::Assemble(this, bhav);
    auto result = routine.get();
    assembled[bhav] = std::move(routine);
    return result;
}

bool VM::ReleaseAssembled() { // TS1(sims): see VM.h
    if (!LiveVMs().empty()) return false;
    auto& assembled = _Assembled();
    assembled.clear();
    std::unordered_map<BHAV*, std::unique_ptr<VMRoutine>>().swap(assembled);
    return true;
}

void VM::BHAVChanged(BHAV* bhav) {
    bhav->RuntimeVer++;
    // NOTE: C# drops the cached routine; frames may still point at it, so we keep the old one alive
    // by leaking it into a graveyard instead of freeing it.
    static std::vector<std::unique_ptr<VMRoutine>> graveyard;
    auto& assembled = _Assembled();
    auto it = assembled.find(bhav);
    if (it != assembled.end()) { graveyard.push_back(std::move(it->second)); assembled.erase(it); }
    for (auto vm : LiveVMs()) vm->VM_OnBHAVChange();
}

void VM::SignalDialog(VMDialogInfo& info) {
    if (OnDialog) OnDialog(info);
}

std::unique_ptr<VMSandboxRestoreState> VM::Sandbox() {
    std::unique_ptr<VMSandboxRestoreState> state(new VMSandboxRestoreState());
    state->Entities = Entities;
    state->ObjectId = ObjectId;
    state->ObjectsById = ObjectsById;
    state->SetToNext = std::move(Context->SetToNextCache);
    state->PendingRemovals = std::move(TS1PendingRemovals);
    TS1PendingRemovals.clear();

    Context->SetToNextCache.reset(new VMSetToNextCache(Context));
    Entities.clear();
    ObjectsById.clear();
    ObjectId = 1;

    return state;
}

void VM::SandboxRestore(std::unique_ptr<VMSandboxRestoreState> state) {
    Entities = state->Entities;
    ObjectsById = state->ObjectsById;
    ObjectId = state->ObjectId;
    Context->SetToNextCache = std::move(state->SetToNext);
    TS1PendingRemovals = std::move(state->PendingRemovals);
}

void VM::BreakpointHit(VMEntity* entity) {
    if (!OnBreakpoint) entity->Thread->ThreadBreak = Engine::VMThreadBreakMode::Active; //no handler..
    else OnBreakpoint(entity);
}

}}
