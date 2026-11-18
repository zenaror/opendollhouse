// SPDX-License-Identifier: MPL-2.0
#include "simantics/utils/VMTS1InteractionCheckpoint.h"
#include "simantics/utils/VMTS1ActionIcon.h"
#include "simantics/engine/VMTS1Neighbourhood.h"
#include "files/formats/iff/chunks/ODHI.h"
#include "files/formats/iff/chunks/ODMP.h"
#include "simantics/utils/VMTS1MotivePersistence.h"
#include "simantics/utils/VMTS1SchedulerState.h"
#include "simantics/utils/VMTS1BudgetPersistence.h"
#include "simantics/primitives/VMTS1Budget.h"
#include "files/formats/iff/chunks/SIMI.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/IffFile.h"
#include "files/Hash.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "content/TS1NeighbourProvider.h"
#include "files/formats/iff/chunks/FAMI.h"
#include "simantics/VMClock.h"
#include "vitaboy/content/TS1AvatarProvider.h"
#include "simantics/model/VMStackObjectVariable.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/engine/VMTS1RelativePosition.h"
#include "simantics/engine/VMActionCallback.h"
#include "simantics/engine/VMTS1MotiveDecay.h"
#include "simantics/primitives/VMDialogPrivateStrings.h"
#include "simantics/engine/VMDialogHandler.h"
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <sstream>
#include <limits>
#include <cstdlib>
#include "files/formats/iff/chunks/OBJT.h"

namespace FSO { namespace SimAntics { namespace Engine { namespace Utils {
using namespace Files::Formats::IFF;
using namespace Files::Formats::IFF::Chunks;
using Model::VMAnimationState;

namespace {
void Require(bool ok, const std::string& why) {
    if (!ok) throw std::runtime_error("Interaction checkpoint: " + why);
}

// One field order for capture and restore. Only prepare ever writes a field.
// Counts precede bounded allocations. Integers and IEEE floats are little endian.
class Archive {
public:
    st::Writer* out = nullptr;
    st::Reader* in = nullptr;
    explicit Archive(st::Writer& writer) : out(&writer) {}
    explicit Archive(st::Reader& reader) : in(&reader) {}

    template<class T> void value(T& data) {
        static_assert(std::is_arithmetic<T>::value || std::is_enum<T>::value, "scalar field");
        if constexpr (std::is_same<T, bool>::value) {
            if (out) out->WriteByte(data ? 1 : 0);
            else { auto b = in->u8(); Require(b <= 1, "invalid boolean"); data = b != 0; }
        } else {
            using Bits = std::conditional_t<sizeof(T)==8, uint64_t,
                std::conditional_t<sizeof(T)==4, uint32_t, std::conditional_t<sizeof(T)==2, uint16_t, uint8_t>>>;
            Bits bits{};
            if (out) {
                std::memcpy(&bits, &data, sizeof(data));
                if constexpr (sizeof(T)==8) { out->WriteUInt32(static_cast<uint32_t>(bits)); out->WriteUInt32(bits >> 32); }
                if constexpr (sizeof(T)==4) out->WriteUInt32(bits);
                if constexpr (sizeof(T)==2) out->WriteUInt16(bits);
                if constexpr (sizeof(T)==1) out->WriteByte(bits);
            } else {
                if constexpr (sizeof(T)==8) { bits = in->u32(); bits |= uint64_t(in->u32()) << 32; }
                if constexpr (sizeof(T)==4) bits = in->u32();
                if constexpr (sizeof(T)==2) bits = in->u16();
                if constexpr (sizeof(T)==1) bits = in->u8();
                std::memcpy(&data, &bits, sizeof(data));
            }
            if constexpr (std::is_floating_point<T>::value) Require(std::isfinite(data), "non-finite value");
        }
    }
    uint32_t count(size_t size, uint32_t limit) {
        Require(size <= limit, "collection exceeds limit");
        uint32_t n = static_cast<uint32_t>(size);
        value(n);
        Require(n <= limit, "invalid collection size");
        return n;
    }
    void string(std::string& s, uint32_t limit = 65535) {
        auto n = count(s.size(), limit);
        if (out) out->WriteBytes(reinterpret_cast<const uint8_t*>(s.data()), n);
        else s = in->cstr(n, false);
    }
    template<class C> void scalars(C& list) { for (auto& x : list) value(x); }
    template<class T> void vector(std::vector<T>& list, uint32_t limit) {
        auto n = count(list.size(), limit);
        if (in) list.resize(n);
        scalars(list);
    }
};

uint32_t BehaviorSignature(BHAV& tree) {
    st::Writer bytes(st::Endian::Little);
    bytes.WriteByte(tree.Type);
    bytes.WriteByte(tree.Args);
    bytes.WriteUInt16(tree.Locals);
    for (auto& op : tree.Instructions) {
        bytes.WriteUInt16(op.Opcode);
        bytes.WriteByte(op.TruePointer);
        bytes.WriteByte(op.FalsePointer);
        bytes.WriteBytes(op.Operand.data(), op.Operand.size());
    }
    return Files::Hash::CalculateHash(bytes.ToArray());
}

uint32_t AnimationSignature(const Vitaboy::Animation& anim, const VMAnimationState& full,
                            uint32_t version = VMTS1InteractionCheckpoint::CurrentVersion) {
    st::Writer bytes(st::Endian::Little);
    bytes.WriteInt32(anim.NumFrames());
    bytes.WriteFloat(anim.Duration);
    // A resumed source-time walk also depends on the asset's displacement.
    // Older checkpoints retain their original signature algorithm.
    if (version >= 15) bytes.WriteFloat(anim.Distance);
    // Include every property's order, timestamp and payload, including non-xevt
    // sounds, gestures, suits and accessories which the avatar itself consumes.
    for (const auto& tp : full.TimePropertyLists) {
        bytes.WriteUInt32(tp.ID);
        bytes.WriteUInt32(tp.Properties.Items.size());
        for (const auto& item : tp.Properties.Items) {
            bytes.WriteUInt32(item.KeyPairs.size());
            for (const auto& kv : item.KeyPairs) {
                bytes.WriteVariableLengthPascalString(kv.first);
                bytes.WriteVariableLengthPascalString(kv.second);
            }
        }
    }
    return Files::Hash::CalculateHash(bytes.ToArray());
}

struct AvatarRecord {
    std::vector<std::shared_ptr<VMAnimationState>> animations;
    std::shared_ptr<VMAnimationState> carry;
    bool rawAnimationClock = false, hurryableAnimation = false, motionPositionKnown = false;
    float motionX = 0, motionY = 0, motionRate = 1;
    int8_t motionLevel = 0;
    int64_t motionRateTick = -1;
    std::array<int16_t,101> person{};
    std::array<int16_t,16> motives{};
    std::array<MotivePersistence::LegacyChange,16> changes{};
    MotivePersistence::State precision;
    std::array<int16_t,7> decayFractions{};
    int32_t decayMinute = 0, killTimeout = -1;
    int32_t leftHand = 0, rightHand = 0;
    float direction = 0;
    st::Vector3 velocity;
    uint64_t body = 0, head = 0;
    VMAvatarDefaultSuits suits{false};
    Model::VMTS1BodyPart bodyPart, headPart;
    std::vector<std::string> accessories;
    int16_t renderFlags = 0;
    bool hasFootprint = false;
    int32_t minX=0, minY=0, maxX=0, maxY=0;
};
struct Record {
    VMEntity* entity = nullptr;
    std::unique_ptr<VMThread> preparedThread;
    std::vector<int16_t> attributes, objectData, list;
    uint64_t sprites = 0, sprites2 = 0;
    int16_t mainParam = 0, mainStack = 0, slot = -1;
    uint32_t schedulerState = 0x0001ffffu;
    LotView::Model::LotTilePos position;
    VMEntity* container = nullptr;
    std::vector<VMEntity*> slots;
    std::unique_ptr<AvatarRecord> avatar;
};
}


namespace {
// Classic TS1 animation records use thousandths of their complete frame range.
// Retain the unconsumed event properties only: replaying an old xevt could
// charge money, consume an item or award a skill for a second time.
std::shared_ptr<VMAnimationState> ClassicAnimation(Vitaboy::Animation* resource,
                                                  const std::string& encoded, int events) {
    Require(resource, "classic animation resource is unavailable");
    std::vector<std::string> parts;
    std::istringstream input(encoded); std::string part;
    while (std::getline(input, part, ';')) parts.push_back(part);
    Require(parts.size() == 8, "invalid classic animation record");
    auto number = [&](size_t i) {
        size_t used = 0; int result;
        try { result = std::stoi(parts[i], &used); }
        catch (...) { throw std::runtime_error("Interaction checkpoint: invalid classic animation number"); }
        Require(used == parts[i].size(), "invalid classic animation number");
        return result;
    };
    const int speed = number(2), frame = number(3), weight = number(4), loop = number(5);
    Require(frame >= 0 && frame <= 1000 && weight >= 0 && (loop == 0 || loop == 1) &&
            events >= 0 && events <= 255, "classic animation state is out of range");
    auto state = std::make_shared<VMAnimationState>(resource, speed < 0);
    state->CurrentFrame = resource->NumFrames() * (frame / 1000.f);
    state->Speed = std::abs(double(speed)) / 1000.;
    state->Weight = weight / 1000.f; state->Loop = loop != 0;
    state->EndReached = !state->Loop && frame == (speed < 0 ? 0 : 1000);
    state->EventsRun = uint8_t(events);
    const float time = state->CurrentFrame * 33.33f;
    auto& properties = state->TimePropertyLists;
    // Frame thousandths are quantized. The saved consumed-event counter also
    // suppresses xevts just beyond that rounded boundary, in playback order.
    // This matters for events whose BHAV consumer has already applied effects.
    std::set<size_t> consumed;
    int event = 0;
    for (size_t ordinal = 0; ordinal < properties.size(); ++ordinal) {
        const size_t index = state->PlayingBackwards ? properties.size() - ordinal - 1 : ordinal;
        const auto& item = properties[index];
        const auto value = item.Properties.Get("xevt");
        if (value && std::atoi(value->c_str()) < 100 && event++ < events) consumed.insert(index);
    }
    size_t index = 0;
    properties.erase(std::remove_if(properties.begin(), properties.end(), [&](const auto& item) {
        const bool crossed = state->PlayingBackwards ? item.ID >= time : item.ID <= time;
        return consumed.count(index++) || crossed;
    }), properties.end());
    return state;
}
}

class InteractionCheckpointCodec {
public:
    InteractionCheckpointCodec(VM& vm, Archive& archive, uint32_t version) : vm(vm), io(archive), version(version) {}
    bool PreviewUnpublishedPortals = false;

    static int64_t motivePhase(const VMAvatar& avatar) { return avatar.MotiveDecay->LastTick; }

    // ObjM already stores the original per-frame primitive state. ODHI8 is
    // bound to those exact ObjM bytes; restore this existing companion field
    // without adding bytes to the native graph or breaking older readers.
    void preparePrimitiveStates(std::map<int16_t, Record>& records, const OBJM& classic, const OBJT* types) {
        std::map<int16_t, uint32_t> owners;
        if (types) for (const auto& type : types->Entries) owners[type.TypeID] = type.GUID;
        for (auto& entry : records) {
            auto& thread = *entry.second.preparedThread;
            const auto found = classic.ObjectData.find(entry.first);
            if (found != classic.ObjectData.end() && found->second.Instance) {
                const auto& saved = found->second.Instance->Stack;
                std::vector<std::shared_ptr<VMStackFrame>> frames;
                for (const auto& frame : thread.Stack) if (!frame->IsRoutingFrame()) frames.push_back(frame);
                bool matched = saved.size() == frames.size();
                for (size_t i = 0; matched && i < saved.size(); ++i) {
                    const auto& source = saved[i]; const auto& frame = frames[i];
                    const auto owner = owners.find(source.CodeOwnerObjType);
                    matched = owner != owners.end() && frame->CodeOwner && frame->CodeOwner->OBJ &&
                        frame->CodeOwner->OBJ->GUID == owner->second && frame->Routine->ID == uint16_t(source.TreeID) &&
                        frame->InstructionPointer == uint16_t(source.NodeID) && frame->StackObjectID() == source.StackObjectID &&
                        source.Parameters.size() <= frame->Args.size() && source.Locals.size() <= frame->Locals.size() &&
                        std::equal(source.Parameters.begin(), source.Parameters.end(), frame->Args.begin()) &&
                        std::equal(source.Locals.begin(), source.Locals.end(), frame->Locals.begin());
                }
                // Some legacy internal/social records deliberately retain an
                // older classic projection while ODHI owns their live graph.
                // Never apply state from mismatched frames to that graph.
                if (matched) for (size_t i = 0; i < frames.size(); ++i) frames[i]->PrimitiveState = saved[i].PrimitiveState;
            }
            // Older native writers projected every primitive state to zero.
            // An existing non-immediate child owned by opcode17 proves that
            // this parent successfully handed over and must retry. A top-frame
            // historical retry after child return cannot be reconstructed.
            size_t action = 0;
            for (size_t i = 0; i < thread.Stack.size(); ++i) {
                const auto& child = thread.Stack[i];
                if (child->SpecialResult != VMSpecialResult::Interaction) continue;
                Require(action < thread.Queue.size(), "primitive state action boundary is unavailable");
                const auto record = thread.Queue[action++];
                if (i && !thread.Stack[i-1]->IsRoutingFrame() &&
                    thread.Stack[i-1]->GetCurrentInstruction().Opcode == 17 &&
                    (record->Flags & TTABFlags::RunImmediately) == 0 && thread.Stack[i-1]->PrimitiveState == 0)
                    thread.Stack[i-1]->PrimitiveState = 1;
            }
        }
    }


    static MotivePersistence::State classicMotives(const OBJMPerson& person, int64_t lastTick,
                                                  bool legacyNativeOrder = false) {
        MotivePersistence::State state;
        for (size_t i = 0; i < 16; ++i) {
            // Historical ODMP1 writers emitted old/current in the opposite
            // order. Only their fully validated, bound marker grants this
            // interpretation; ordinary classic files use the DVD order.
            const auto current = legacyNativeOrder ? person.MotiveDataOld[i] : person.MotiveData[i];
            const auto old = legacyNativeOrder ? person.MotiveData[i] : person.MotiveDataOld[i];
            MotivePersistence::ValidateCurrent(current);
            MotivePersistence::ValidateFloat(old);
            state.current[i] = current; state.old[i] = old;
        }
        Require(person.MotiveDeltas.size() <= 32767, "too many classic motive changes");
        for (const auto& delta : person.MotiveDeltas) {
            Model::VMTS1MotiveChange change{Model::VMMotive(delta.Motive), delta.TickDelta, delta.StopAt};
            MotivePersistence::ValidateChange(change);
            state.changes.push_back(change);
        }
        state.lastTick = lastTick;
        return state;
    }

    void prepareMotives(std::map<int16_t, Record>& records, IffFile& house,
                        OBJM& objm, ODHI& native, int64_t savedTick) {
        // ODHI is the only legacy authority. In particular, never add stale
        // classic float residues to the native fractions decoded above.
        for (auto& item : records) if (auto& avatar = item.second.avatar)
            avatar->precision = MotivePersistence::Migrate(avatar->motives, avatar->changes,
                                                          avatar->decayFractions, savedTick);
        auto markers = house.List<ODMP>();
        if (markers.empty()) return;
        Require(markers.size() == 1 && markers.front()->ChunkID == 1, "ambiguous ODMP");
        const auto marker = MotivePersistence::ReadMarker(markers.front()->Data, objm.SourceSignature,
            Files::Hash::CalculateHash(native.Data), native.Data.size(), savedTick);
        if (!marker.matching) return;
        size_t avatarCount = 0;
        for (const auto& item : records) if (item.second.avatar) ++avatarCount;
        Require(marker.actors.size() == avatarCount, "motive marker participant count changed");
        constexpr int neighbourIndex = int(Model::VMPersonDataVariable::NeighborId);
        for (const auto& actor : marker.actors) {
            const auto id = actor.first;
            const auto guid = actor.second.guid;
            const auto neighbour = actor.second.neighbour;
            const auto phase = actor.second.lastTick;
            auto record = records.find(id); auto classic = objm.ObjectData.find(id);
            Require(record != records.end() &&
                    record->second.avatar && classic != objm.ObjectData.end() && classic->second.Instance,
                    "unmatched motive marker participant");
            auto& saved = *record->second.avatar;
            auto live = static_cast<VMAvatar*>(record->second.entity);
            auto& instance = *classic->second.Instance;
            Require(instance.HasPersonData && instance.OBJT && instance.OBJT->GUID == guid &&
                    live->Object->OBJ->GUID == guid && saved.person[neighbourIndex] == neighbour &&
                    live->GetPersonData(Model::VMPersonDataVariable::NeighborId) == neighbour &&
                    instance.PersonData.PersonData.size() > size_t(neighbourIndex) &&
                    instance.PersonData.PersonData[neighbourIndex] == neighbour,
                    "motive marker actor identity changed");
            Require(phase >= 0 && phase <= savedTick, "invalid motive marker phase");
            saved.precision = classicMotives(instance.PersonData, phase, marker.schema == 1);
        }
    }

    // FreeSO VMTS1ActivatorNew.ConvertThread/ConvertAvatar, adapted to the
    // integrated VM. This consumes the already decoded classic records; it
    // neither runs authored Reset/Init nor restarts a valid interaction.
    void classic(OBJM& objm, OBJT& objt) {
        Require(objm.Decoded, "classic object records were not decoded");
        std::map<uint16_t, Content::GameObject*> owners;
        for (auto& type : objt.Entries) {
            if (!type.GUID) continue;
            Require(type.TypeID > 0 && !owners.count(type.TypeID), "ambiguous classic code owner type");
            Content::GameObject* owner = nullptr;
            // Definitions already materialized by the activator are the same
            // resource providers used for these saved instances.
            for (auto entity : vm.Entities) if (entity && entity->Object && entity->Object->OBJ &&
                    entity->Object->OBJ->GUID == type.GUID) { owner = entity->Object; break; }
            if (!owner) owner = Content::Content::Get()->WorldObjects->Get(type.GUID, false);
            owners.emplace(type.TypeID, owner);
        }
        auto entity = [&](int16_t id) {
            auto e = id > 0 ? vm.GetObjectById(id) : nullptr;
            Require(e && !e->Dead && !e->GhostImage, "unresolved classic instance " + std::to_string(id));
            return e;
        };
        auto routine = [&](Content::GameObject* owner, uint16_t id) {
            if (!owner || !owner->Resource) return static_cast<VMRoutine*>(nullptr);
            auto tree = id < 4096 ? (vm.Context->Globals ? vm.Context->Globals->Resource->Get<BHAV>(id) : nullptr) :
                id < 8192 ? owner->Resource->Get<BHAV>(id) :
                owner->Resource->SemiGlobal ? owner->Resource->SemiGlobal->Get<BHAV>(id) : nullptr;
            return tree && !tree->Instructions.empty() ? vm.Assemble(tree) : nullptr;
        };
        auto animation = [&](const std::string& encoded, int events) -> std::shared_ptr<VMAnimationState> {
            if (encoded.empty()) return nullptr;
            auto name = encoded.substr(0, encoded.find(';'));
            return ClassicAnimation(Content::Content::Get()->AvatarAnimations->Get(name + ".anim"), encoded, events);
        };
        struct Candidate {
            VMEntity* entity;
            std::unique_ptr<VMThread> thread;
            uint32_t schedulerState = 0x0001ffffu;
            VMAvatar* avatar = nullptr;
            bool reset = false;
            std::vector<std::shared_ptr<VMAnimationState>> animations;
            std::shared_ptr<VMAnimationState> carry;
            MotivePersistence::State precision;
        };
        std::vector<Candidate> candidates;
        for (const auto& saved : objm.ObjectData) {
            try {
                Require(saved.first > 0 && saved.first <= 32767 && saved.second.Instance,
                        "invalid classic instance identity");
                const auto& inst = *saved.second.Instance;
                auto live = entity(int16_t(saved.first));
                Require(inst.OBJT && live->Object->OBJ->GUID == inst.OBJT->GUID,
                        "classic instance definition changed");
                Candidate candidate; candidate.entity = live;
                candidate.schedulerState = TS1SavedSchedulerState(inst, objm.Version);
                candidate.thread = std::make_unique<VMThread>(vm.Context, live, 16);
                auto& thread = *candidate.thread;
                Require(inst.TempRegisters.size() <= thread.TempRegisters.size(), "classic temp bank too large");
                std::copy(inst.TempRegisters.begin(), inst.TempRegisters.end(), thread.TempRegisters.begin());
                // Selected Alex pin CanResume/FramesMatch: expansion patches
                // can retain a valid PC while changing the caller's meaning.
                // Reset only the incompatible instance, after all candidates
                // validate; never run a mismatched parent on a child's return.
                std::vector<VMRoutine*> routines;
                for (const auto& source : inst.Stack) {
                    auto owner = owners.find(source.CodeOwnerObjType);
                    Require(owner != owners.end(), "missing classic frame code owner");
                    auto resolved = routine(owner->second, uint16_t(source.TreeID));
                    routines.push_back(resolved);
                    if (!resolved || source.NodeID < 0 || size_t(source.NodeID) >= resolved->Instructions.size())
                        candidate.reset = true;
                }
                if (!candidate.reset) for (size_t i = 0; i + 1 < inst.Stack.size(); ++i) {
                    const auto opcode = routines[i]->Instructions[inst.Stack[i].NodeID].Opcode;
                    const bool treePrimitive = opcode == 17 || opcode == 20 || opcode == 22 || opcode == 27 ||
                                               opcode == 28 || opcode == 30 || opcode == 45;
                    if (opcode != uint16_t(inst.Stack[i+1].TreeID) && !treePrimitive) candidate.reset = true;
                }
                for (size_t i = 0; !candidate.reset && i < inst.Stack.size(); ++i) {
                    const auto& source = inst.Stack[i];
                    auto owner = owners.find(source.CodeOwnerObjType);
                    Require(owner != owners.end(), "missing classic frame code owner");
                    auto frame = std::make_shared<VMStackFrame>();
                    frame->Thread = &thread; frame->CodeOwner = owner->second;
                    frame->Routine = routine(frame->CodeOwner, uint16_t(source.TreeID));
                    Require(source.NodeID >= 0 && size_t(source.NodeID) < frame->Routine->Instructions.size(),
                            "classic frame " + std::to_string(i) + " has invalid PC");
                    frame->InstructionPointer = uint16_t(source.NodeID);
                    frame->Caller = frame->Callee = live;
                    frame->PrimitiveState = source.PrimitiveState;
                    frame->StackObjectRawID = source.StackObjectID;
                    frame->StackObject = vm.GetObjectById(source.StackObjectID);
                    frame->Args = source.Parameters;
                    frame->Args.resize(std::max(size_t(4), frame->Args.size()), 0);
                    const size_t localCount = std::max(frame->Routine->Locals, frame->Routine->Arguments);
                    Require(source.Locals.size() <= localCount, "classic frame local bank changed");
                    frame->Locals = source.Locals; frame->Locals.resize(localCount, 0);
                    thread.Stack.push_back(std::move(frame));
                }
                if (inst.HasPersonData) {
                    candidate.avatar = dynamic_cast<VMAvatar*>(live);
                    Require(candidate.avatar, "classic person is not an avatar");
                    const auto& person = inst.PersonData;
                    auto action = [&](const OBJMInteraction& source) {
                        Require(source.IsValid() && source.UID >= 0 && source.UID <= 65535 &&
                                source.Priority >= -32768 && source.Priority <= 32767,
                                "invalid classic queued interaction");
                        auto result = std::make_shared<VMQueuedAction>();
                        result->Callee = result->StackObject = entity(source.TargetID);
                        result->CodeOwner = result->Callee->Object;
                        result->ActionRoutine = routine(result->CodeOwner, uint16_t(source.ActionTreeID));
                        Require(result->ActionRoutine, "unresolved classic queued BHAV");
                        result->UID = uint16_t(source.UID); result->InteractionNumber = source.TTAIndex;
                        result->Args.assign(std::begin(source.Args), std::end(source.Args));
                        result->Priority = int16_t(source.Priority);
                        result->Flags2 = TSOFlags(0);
                        if (result->Callee->TreeTableStrings)
                            result->Name = result->Callee->TreeTableStrings->GetString(source.TTAIndex);
                        // Active actions resume without a fresh permission
                        // test. Pending records are rechecked only when Idle
                        // actually consumes them, as in the original engine.
                        auto available = result->Callee->GetAction(source.TTAIndex, live, vm.Context);
                        if (available) {
                            result->CheckRoutine = available->CheckRoutine;
                            result->Flags = available->Flags;
                            result->Flags2 = available->Flags2;
                        }
                        if (source.Flags & int(OBJMInteractionFlags::PushHeadContinuation))
                            result->Flags = result->Flags | TTABFlags::FSOPushHead;
                        if (source.Flags & int(OBJMInteractionFlags::CarryNameOver))
                            result->Flags = result->Flags | TTABFlags::TS1InheritActionName;
                        result->UserQueued = (source.Flags & int(OBJMInteractionFlags::UserInitiated)) != 0;
                        result->Cancelled = (source.Flags & int(OBJMInteractionFlags::UserInterrupted)) != 0;
                        if (source.Icon) result->IconOwner(entity(source.Icon));
                        // DVD cancellation injects this standard person action
                        // with an explicit null icon. Other classic null icons
                        // retain their existing target fallback until proved.
                        if (!source.Icon && source.TTAIndex == 33 && source.TargetID == live->ObjectID) {
                            auto cleanup = live->GetAction(33, live, vm.Context);
                            if (cleanup) {
                                result->CheckRoutine = cleanup->CheckRoutine;
                                result->Flags = cleanup->Flags | TTABFlags::TS1HideQueueIcon;
                            }
                        }
                        return result;
                    };
                    const bool active = person.ActiveInteraction.IsValid();
                    thread.TS1LastInteraction=person.LastInteraction;
                    thread.ActiveQueueBlock = active ? 0 : -1;
                    if (active) thread.Queue.push_back(action(person.ActiveInteraction));
                    if (person.ActiveInteraction.Icon) thread.TS1FlaggedUseTarget=entity(person.ActiveInteraction.Icon);
                    std::set<uint16_t> uids;
                    for (const auto& item : person.InteractionQueue) thread.Queue.push_back(action(item));
                    for (const auto& item : thread.Queue) {
                        Require(uids.insert(item->UID).second, "duplicate classic action UID");
                        thread.ActionUID = std::max(thread.ActionUID, item->UID);
                    }
                    if (!thread.Queue.empty()) {
                        do { ++thread.ActionUID; } while (uids.count(thread.ActionUID));
                    }
                    int boundaries = 0;
                    int32_t previousUseDepth=0;
                    for (const auto& use : person.ObjectUses) {
                        Require(use.StackLength > previousUseDepth && size_t(use.StackLength) <= inst.Stack.size(),
                                "classic object use has invalid stack depth");
                        previousUseDepth=use.StackLength;
                        auto target = use.TargetID ? entity(use.TargetID) : nullptr;
                        if (candidate.reset) continue;
                        thread.TS1ObjectUses.push_back({target,use.StackLength,use.Unknown2});
                        bool inAction = false;
                        auto& frame = thread.Stack[size_t(use.StackLength - 1)];
                        if (use.StackLength > 1) {
                            const auto& parent = thread.Stack[size_t(use.StackLength - 2)];
                            const auto opcode = parent->Routine->Instructions[parent->InstructionPointer].Opcode;
                            if (opcode == 27 || opcode == 45) frame->SpecialResult = VMSpecialResult::Retry;
                            else if (opcode == 30 || opcode == 17) {
                                if (frame->SpecialResult == VMSpecialResult::Normal) {
                                    frame->SpecialResult = VMSpecialResult::Interaction; ++boundaries;
                                } else Require(frame->SpecialResult == VMSpecialResult::Interaction, "conflicting classic interaction boundary");
                                inAction = true;
                            }
                        }
                        for (size_t i = size_t(use.StackLength - 1); i < thread.Stack.size(); ++i) {
                            if (target) thread.Stack[i]->Callee = target;
                            thread.Stack[i]->ActionTree |= inAction;
                        }
                    }
                    Require(candidate.reset || boundaries == (active ? 1 : 0), "classic active action has no matching stack boundary");
                    auto current = animation(person.Animation, person.AnimEventCount);
                    if (current) candidate.animations.push_back(current);
                    candidate.carry = animation(person.CarryAnimation, 0);
                    // The original actor constructor clears its decay stamp;
                    // its loaded global counter decides whether the first
                    // ordinary tick is due. Native ODMP phases are separate.
                    candidate.precision = classicMotives(person, 0);
                }
                candidates.push_back(std::move(candidate));
            } catch (const std::exception& e) {
                throw std::runtime_error("Classic continuation object " + std::to_string(saved.first) + ": " + e.what());
            }
        }
        // Preparation above cannot replace any live thread, animation or lock.
        // Commit the complete candidate only after every participant validates.
        for (auto& candidate : candidates) {
            if (candidate.avatar) {
                candidate.avatar->Animations = std::move(candidate.animations);
                candidate.avatar->CarryAnimationState = std::move(candidate.carry);
                candidate.avatar->MotiveData = candidate.precision.current;
                candidate.avatar->OldMotiveData = candidate.precision.old;
                candidate.avatar->TS1MotiveChanges = std::move(candidate.precision.changes);
                candidate.avatar->MotiveDecay->LastTick = candidate.precision.lastTick;
            }
            candidate.entity->Thread = std::move(candidate.thread);
            candidate.entity->TS1SchedulerState = candidate.schedulerState;
        }
        for (auto& candidate : candidates) if (candidate.reset) candidate.entity->Reset(vm.Context);
    }

    // Referenced participants form a closure over the active stacks and queues,
    // callbacks, slots and multi-tile groups. Other avatars may also own a target.
    std::map<int16_t, VMEntity*> participants;
    void include(VMEntity* entity) {
        if (entity) {
            Require(entity->ObjectID > 0 && !entity->Dead && !entity->GhostImage &&
                    entity->Object && entity->Object->OBJ && vm.GetObjectById(entity->ObjectID) == entity,
                    "referenced entity has no live instance identity");
            participants.emplace(entity->ObjectID, entity);
        }
    }
    void reference(VMEntity*& e, const char* use) {
        if (io.out && e && (e->Dead || e->GhostImage || vm.GetObjectById(e->ObjectID) != e)) {
            throw std::runtime_error(std::string("Interaction checkpoint: ") + use + " references " +
                    (e->Dead ? "deleted " : e->GhostImage ? "ghost " : "replaced ") +
                    e->ToString() + " #" + std::to_string(e->ObjectID));
        }
        int16_t id = e ? e->ObjectID : 0;
        uint32_t guid = e ? e->Object->OBJ->GUID : 0;
        io.value(id); io.value(guid);
        if (io.in) e = id ? vm.GetObjectById(id) : nullptr;
        Require(id >= 0 && (id ? e && e->Object && e->Object->OBJ && e->Object->OBJ->GUID == guid : !guid),
                "unresolved instance " + std::to_string(id) + " GUID " + std::to_string(guid));
        include(e);
    }
    void owner(Content::GameObject*& obj) {
        uint32_t guid = obj && obj->OBJ ? obj->OBJ->GUID : 0;
        io.value(guid);
        if (io.in) obj = Content::Content::Get()->WorldObjects->Get(guid, false);
        Require(obj && obj->OBJ && obj->OBJ->GUID == guid, "missing code owner " + std::to_string(guid));
    }
    void behavior(VMRoutine*& routine, Content::GameObject* obj, bool optional) {
        uint16_t id = routine ? routine->ID : 0;
        io.value(id);
        if (!id && optional) return;
        BHAV* bhav = id < 4096 ? vm.Context->Globals->Resource->Get<BHAV>(id) :
            id < 8192 ? obj->Resource->Get<BHAV>(id) :
            obj->Resource->SemiGlobal ? obj->Resource->SemiGlobal->Get<BHAV>(id) : nullptr;
        Require(bhav && !bhav->Instructions.empty() && (!io.out || (routine && routine->Chunk == bhav)), "unresolved BHAV " + std::to_string(id));
        uint32_t actual = BehaviorSignature(*bhav), signature = actual;
        io.value(signature);
        Require(actual == signature, "changed BHAV " + std::to_string(id));
        if (io.in) routine = vm.Assemble(bhav);
    }
    void animationTiming(VMAnimationState& state, uint32_t propertyCount) {
        if (version >= 15) {
            io.value(state.TS1Clock); io.value(state.TS1ScriptAnimation); io.value(state.TS1Phase);
            io.value(state.TS1Timestamp); io.value(state.TS1EventCursor);
            if (state.TS1Clock) {
                // A model-wide clock-policy switch can produce a signed
                // negative delta. The source retains the resulting phase on
                // the nonterminal side; Archive already rejects nonfinite data.
                const int64_t low = state.PlayingBackwards ? -1 : 0;
                const int64_t high = int64_t(propertyCount) - (state.PlayingBackwards ? 1 : 0);
                Require(state.TS1EventCursor >= low && int64_t(state.TS1EventCursor) <= high,
                        "invalid animation event cursor");
            }
        } else {
            Require(!io.out || !state.TS1Clock,
                    "source animation clock requires checkpoint version15");
            if (io.in) {
                state.TS1Clock = false;
                state.TS1ScriptAnimation = false;
                state.TS1Phase = 0;
                state.TS1Timestamp = 0;
                state.TS1EventCursor = 0;
            }
        }
    }
    void animation(std::shared_ptr<VMAnimationState>& state) {
        bool present = bool(state); io.value(present);
        if (!present) return;
        auto anim = state ? state->Anim : nullptr;
        std::string name = anim ? anim->Name : "";
        io.string(name);
        Require(!name.empty(), "animation without a resource name");
        auto resource = Content::Content::Get()->AvatarAnimations->Get(name + ".anim");
        Require(resource && (!io.out || resource == anim), "unresolved animation " + name);
        bool backwards = state && state->PlayingBackwards;
        io.value(backwards);
        auto full = std::make_shared<VMAnimationState>(resource, backwards);
        const auto resourcePropertyCount = full->TimePropertyLists.size();
        uint32_t signature = AnimationSignature(*resource, *full, version);
        uint32_t savedSignature = signature;
        io.value(savedSignature);
        Require(savedSignature == signature, "changed animation " + name);
        uint32_t remaining = io.count(state ? state->TimePropertyLists.size() : 0, full->TimePropertyLists.size());
        if (io.in) {
            state = std::move(full);
            auto& props = state->TimePropertyLists;
            if (backwards) props.resize(remaining);
            else props.erase(props.begin(), props.end() - remaining);
        }
        io.value(state->CurrentFrame); io.value(state->Speed); io.value(state->Weight);
        io.value(state->EndReached); io.value(state->Loop);
        io.value(state->EventsRun); io.vector(state->EventQueue, 65535);
        animationTiming(*state, resourcePropertyCount);
        Require(!state->TS1Clock || remaining == resourcePropertyCount,
                "source animation is missing event properties");
    }
    void callback(std::shared_ptr<VMActionCallback>& cb) {
        bool present = bool(cb); io.value(present);
        if (!present) return;
        if (io.in) cb = std::make_shared<VMActionCallback>(&vm, 0, nullptr, nullptr, nullptr, false, false);
        io.value(cb->type);
        Require(cb->type == 1, "unknown action callback");
        reference(cb->Target, "callback target"); reference(cb->StackObject, "callback stack object"); reference(cb->Caller, "callback caller");
        io.value(cb->Interaction); io.value(cb->SetParam); io.value(cb->IsTree);
        Require(cb->Target && cb->Caller && cb->Caller->Thread, "callback has no target or caller");
        uint16_t tree=uint16_t(cb->Interaction);
        if (!cb->IsTree) {
            Require(cb->Target->TreeTable && cb->Target->TreeTableStrings, "callback has no interaction table");
            auto& entries=cb->Target->TreeTable->InteractionByIndex;
            auto it=entries.find(uint8_t(cb->Interaction));
            Require(it!=entries.end() && it->second, "callback interaction is unavailable");
            tree=it->second->ActionFunction;
        }
        // VMActionCallback::Run supports private and semi-global trees.
        Require(tree>=4096,"global action callback is not implemented by the runtime");
        auto resolved=cb->Target->GetBHAVWithOwner(tree,vm.Context);
        Require(resolved && resolved->bhav,"callback action tree is unavailable");
        uint32_t actual=BehaviorSignature(*resolved->bhav), signature=actual;
        uint16_t savedTree=tree;
        io.value(savedTree); io.value(signature);
        Require(savedTree==tree && signature==actual, "callback routine changed");
    }
    void action(VMQueuedAction& a) {
        reference(a.Callee, "action target"); reference(a.StackObject, "action stack object");
        auto icon = io.out ? PersistedActionIcon(vm, a) : nullptr;
        reference(icon, "action icon");
        if (io.in) a.IconOwner(icon == a.Callee ? nullptr : icon);
        Require(a.Callee, "action has no target");
        owner(a.CodeOwner);
        behavior(a.ActionRoutine, a.CodeOwner, false);
        behavior(a.CheckRoutine, a.CodeOwner, true);
        io.string(a.Name); io.vector(a.Args, 4096);
        io.value(a.InteractionNumber); io.value(a.Cancelled); io.value(a.UserQueued);
        io.value(a.Priority); io.value(a.Mode); io.value(a.Flags); io.value(a.Flags2); io.value(a.UID);
        Require(a.Mode <= VMQueueMode::Idle, "unknown queue mode");
        callback(a.Callback);
    }

    void position(LotView::Model::LotTilePos& pos) {
        io.value(pos.x); io.value(pos.y); io.value(pos.Level);
    }
    void goal(VMFindLocationResult& g) {
        io.value(g.RadianDirection); position(g.Position); io.value(g.Score);
        io.value(g.FaceAnywhere); reference(g.Chair, "route chair"); io.value(g.RouteEntryFlags);
    }
    void portal(Model::VMRoomPortal& saved) {
        VMEntity* entity = io.out ? vm.GetObjectById(saved.ObjectID) : nullptr;
        if (io.out) Require(entity, "saved route names an absent portal");
        reference(entity, "route portal");
        Require(entity && entity->EntryPoints[15].ActionFunction, "route object has no portal entry point");
        saved.ObjectID = entity->ObjectID;
        io.value(saved.TargetRoom);
        const auto actualRoom = vm.Context->GetObjectRoom(entity);
        uint16_t sourceRoom = actualRoom;
        io.value(sourceRoom);
        const auto& graph = vm.Context->RoomInfo;
        Require(sourceRoom == actualRoom && sourceRoom < graph.size() && saved.TargetRoom < graph.size(),
                "route portal room changed");
        if (PreviewUnpublishedPortals) {
            Model::VMRoomPortal candidate;
            Require(!entity->GhostImage && entity->Position().Level>=1 && entity->MultitileGroup &&
                    vm.Context->FindRoomPortal(entity,sourceRoom,candidate) && candidate==saved,
                    "missing directed portal edge");
        } else {
            const auto& edges = graph[sourceRoom].Portals;
            Require(std::find(edges.begin(), edges.end(), saved) != edges.end(), "missing directed portal edge");
        }
    }

    void roomRoute(VMRoutingFrame& r) {
        io.value(r.RoomRouteInvalid);
        bool current = r.CurrentPortal.ObjectID != 0;
        io.value(current);
        if (current) portal(r.CurrentPortal);
        Require(current || r.State != VMRoutingFrameState::ROOM_PORTAL, "active portal route lacks its current edge");
        auto n = io.count(r.Rooms.size(), 32767);
        if (io.in) r.Rooms.resize(n);
        for (auto& edge : r.Rooms) portal(edge);
        std::vector<Model::VMRoomPortal> ignored(r.IgnoredRooms.begin(), r.IgnoredRooms.end());
        if (io.out) std::sort(ignored.begin(), ignored.end(), [](const auto& a, const auto& b) {
            return a.ObjectID == b.ObjectID ? a.TargetRoom < b.TargetRoom : a.ObjectID < b.ObjectID;
        });
        auto k = io.count(ignored.size(), 32767);
        if (io.in) ignored.resize(k);
        for (auto& edge : ignored) {
            portal(edge);
            if (io.in) Require(r.IgnoredRooms.insert(edge).second, "duplicate ignored portal");
        }
        if (n || k || current) {
            // Room numbers refer to generated geometry. Compare the floor maps,
            // including both halves of every tile, before accepting those IDs.
            if (!hasRoomSignature) {
                st::Writer layout(st::Endian::Little);
                for (const auto& level : vm.Context->Architecture()->Rooms) {
                    layout.WriteInt32(level.Width); layout.WriteInt32(level.Height);
                    layout.WriteUInt32(level.Map.size());
                    for (auto tile : level.Map) layout.WriteUInt32(tile);
                }
                roomSignature = Files::Hash::CalculateHash(layout.ToArray());
                hasRoomSignature = true;
            }
            uint32_t saved = roomSignature;
            io.value(saved);
            Require(saved == roomSignature, "route room map changed");
        }
    }
    void route(VMRoutingFrame& r, VMRoutingFrame* parent) {
        // The active route owns these values. Do not call InitRoutes, solve a
        // new path, push a portal BHAV, or consume another random choice on load.
        // Room transitions require identity for the reconstructed room graph.
        if (io.in) r.ParentRoute = parent;
        Require(r.ParentRoute == parent, "route parent differs from the stack");
        // M1 C2: deletion can leave an otherwise live Main route holding its
        // original target. Snapshot only that obsolete route as a failed return;
        // keep its enclosing Main, queue and lifecycle records intact. A fresh
        // route has no path/portal/goal references to resurrect or rebind. Saving
        // must not call HardFail (feedback BHAVs and lockouts mutate the live VM).
        // Ghosts and non-dead replaced instances are not evidence of deletion.
        if (io.out && r.Target && r.Target->Dead && !r.Target->GhostImage) {
            Require(r.Target->ObjectID > 0 && r.Target->Object && r.Target->Object->OBJ &&
                    r.Target->Object->OBJ->GUID, "deleted route target has invalid identity");
            Require(r.State <= VMRoutingFrameState::FAILED &&
                    (version >= 4 || r.State != VMRoutingFrameState::ROOM_PORTAL) &&
                    r.Velocity >= 0 && r.Velocity <= 8 &&
                    std::isfinite(r.WalkDirection) && std::isfinite(r.TargetDirection) && std::isfinite(r.TurnTweak) &&
                    (r.HeldResult == -1 || r.HeldResult == int(VMPrimitiveExitCode::RETURN_TRUE) ||
                     r.HeldResult == int(VMPrimitiveExitCode::RETURN_FALSE)), "invalid routing state");
            VMRoutingFrame failed;
            failed.ParentRoute = parent;
            failed.State = VMRoutingFrameState::FAILED;
            failed.PushTick = r.PushTick;
            failed.HeldResult = int8_t(VMPrimitiveExitCode::RETURN_FALSE);
            route(failed, parent);
            return;
        }
        io.value(r.State); io.value(r.PortalTurns); io.value(r.WaitTime);
        io.value(r.CallFailureTrees); io.value(r.PushTick); io.value(r.HeldResult);
        if (version >= 15) io.value(r.TS1SourceLocomotion);
        else if (io.in) r.TS1SourceLocomotion = false;
        else Require(!vm.TS1 || !r.TS1SourceLocomotion || r.State == VMRoutingFrameState::INITIAL ||
                     r.State == VMRoutingFrameState::FAILED,
                     "source locomotion requires checkpoint version15");
        // Older snapshots cannot establish a route-owned request from live PD72.
        if (version >= 10) { io.value(r.TS1PendingFootprint); io.value(r.TS1PendingFootprintKnown); }
        else if (io.in) { r.TS1PendingFootprint = 0; r.TS1PendingFootprintKnown = false; }
        io.value(r.WalkDirection); io.value(r.TargetDirection); io.value(r.IgnoreRooms);
        io.value(r.Timeout); io.value(r.Retries); io.value(r.AttemptedChair); io.value(r.TurnTweak);
        io.value(r.TurnFrames); io.value(r.MoveTotalFrames); io.value(r.MoveFrames); io.value(r.Velocity);
        position(r.PreviousPosition); position(r.CurrentWaypoint);
        Require(r.State <= VMRoutingFrameState::FAILED && (version >= 4 || r.State != VMRoutingFrameState::ROOM_PORTAL) &&
                r.Velocity >= 0 && r.Velocity <= 8 &&
                (r.HeldResult == -1 || r.HeldResult == int(VMPrimitiveExitCode::RETURN_TRUE) ||
                 r.HeldResult == int(VMPrimitiveExitCode::RETURN_FALSE)), "invalid routing state");
        if (version >= 4) roomRoute(r);
        reference(r.Target, "route target");
        bool hasSlot = r.Slot != nullptr; io.value(hasSlot);
        if (hasSlot) {
            if (io.in) { r.OwnedSlot = std::make_unique<SLOTItem>(); r.Slot = r.OwnedSlot.get(); }
            auto& s = *r.Slot;
            io.value(s.Type); io.value(s.Offset.X); io.value(s.Offset.Y); io.value(s.Offset.Z);
            io.value(s.Standing); io.value(s.Sitting); io.value(s.Ground); io.value(s.Rsflags);
            io.value(s.SnapTargetSlot); io.value(s.MinProximity); io.value(s.MaxProximity);
            io.value(s.OptimalProximity); io.value(s.Gradient); io.value(s.Facing);
            io.value(s.Resolution); io.value(s.Height);
            if (version >= 13) {
                io.value(s.RoutingPolicy);
                Require(s.RoutingPolicy == FSO::Files::Formats::IFF::Chunks::SLOTRoutingPolicy::Authored ||
                        s.RoutingPolicy == FSO::Files::Formats::IFF::Chunks::SLOTRoutingPolicy::TS1RelativePosition,
                        "invalid route slot provenance");
                if (s.RoutingPolicy == FSO::Files::Formats::IFF::Chunks::SLOTRoutingPolicy::TS1RelativePosition)
                    Require(ValidTS1RelativeSlot(s), "invalid generated relative-position slot");
            } else {
                Require(io.in || s.RoutingPolicy == FSO::Files::Formats::IFF::Chunks::SLOTRoutingPolicy::Authored,
                        "legacy checkpoint cannot retain generated slot provenance");
                if (io.in) s.RoutingPolicy = FSO::Files::Formats::IFF::Chunks::SLOTRoutingPolicy::Authored;
            }
        }
        bool hasWalk = bool(r.WalkTo); io.value(hasWalk);
        if (hasWalk) {
            if (io.in) r.WalkTo = std::make_shared<std::list<st::Point>>();
            const auto n = io.count(r.WalkTo->size(), 65535);
            if (io.in) r.WalkTo->resize(n);
            for (auto& point : *r.WalkTo) { io.value(point.X); io.value(point.Y); }
        }
        bool hasChoices = bool(r.Choices); io.value(hasChoices);
        if (hasChoices) {
            if (io.in) r.Choices = std::make_unique<std::vector<VMFindLocationResult>>();
            auto n = io.count(r.Choices->size(), 65535);
            if (io.in) r.Choices->resize(n);
            for (auto& choice : *r.Choices) goal(choice);
        }
        bool hasGoal = bool(r.CurRoute); io.value(hasGoal);
        if (hasGoal) {
            if (io.in) r.CurRoute=std::make_shared<VMFindLocationResult>();
            goal(*r.CurRoute);
        }
        // Stable order for this unordered runtime set.
        std::vector<VMEntity*> blockers(r.AvatarsToConsider.begin(), r.AvatarsToConsider.end());
        if (io.out) {
            // This is a collision cache, not an interaction participant. A
            // departed avatar is no longer in the room's obstacle collection.
            // Omit only deleted, non-ghost entries from the snapshot; never
            // mutate the running route or bind a reused ID to its replacement.
            blockers.erase(std::remove_if(blockers.begin(), blockers.end(), [](auto e) {
                Require(e != nullptr, "null route blocker");
                if (!e->Dead || e->GhostImage) return false;
                Require(e->ObjectID > 0 && e->Object && e->Object->OBJ && e->Object->OBJ->GUID,
                        "deleted route blocker has invalid identity");
                return true;
            }), blockers.end());
            std::sort(blockers.begin(), blockers.end(), [](auto a, auto b){return a->ObjectID < b->ObjectID;});
        }
        auto n = io.count(blockers.size(), 32767);
        if (io.in) blockers.resize(n);
        for (auto& blocker : blockers) {
            reference(blocker, "route blocker");
            auto a = dynamic_cast<VMAvatar*>(blocker);
            Require(a, "route blocker is not an avatar");
            if (io.in) Require(r.AvatarsToConsider.insert(a).second, "duplicate route blocker");
        }
        if (r.State == VMRoutingFrameState::WALKING) Require(r.CurRoute && r.WalkTo, "walking route lacks a goal or waypoint list");
    }

    void thread(VMThread& t) {
        Require(!t.IsCheck && (version >= 8 || !t.BlockingState) && !t.EODConnection && t.ThreadBreak == VMThreadBreakMode::Active,
                "unrepresented check, debugger, async or EOD state");
        io.value(t.ActiveQueueBlock); io.value(t.QueueDirty);
        io.scalars(t.TempRegisters); io.scalars(t.TempXL);
        io.value(t.LastStackExitCode); io.value(t.Interrupt); io.value(t.ActionUID); io.value(t.DialogCooldown);
        Require(t.LastStackExitCode <= VMPrimitiveExitCode::INTERRUPT, "unknown thread exit code");
        auto n = io.count(t.Queue.size(), 4096);
        Require(t.ActiveQueueBlock >= -1 && t.ActiveQueueBlock < int(n), "active queue index out of bounds");
        std::set<uint16_t> uids;
        for (uint32_t index = 0; index < n; ++index) {
            auto item = io.out ? t.Queue[index] : std::make_shared<VMQueuedAction>();
            Require(bool(item), "empty queue item");
            // EvaluateQueuePriorities erases pending actions whose target died,
            // without running their callback. Record that expiry in the file
            // and apply it only to the candidate queue built during load.
            bool expired = io.out && int(index) > t.ActiveQueueBlock && item->Callee && item->Callee->Dead;
            if (version >= 5) io.value(expired);
            if (expired) {
                int16_t target = io.out ? item->Callee->ObjectID : 0;
                uint32_t guid = io.out ? item->Callee->Object->OBJ->GUID : 0;
                io.value(item->UID); io.value(target); io.value(guid);
                Require(int(index) > t.ActiveQueueBlock && target > 0 && guid, "invalid expired queue item");
            } else {
                action(*item);
                if (io.in) t.Queue.push_back(item);
            }
            Require(uids.insert(item->UID).second, "duplicate action UID");
        }
        n = io.count(t.Stack.size(), 1024);
        if (io.in) t.Stack.resize(n);
        int boundaries = 0;
        VMRoutingFrame* parent = nullptr;
        for (auto& f : t.Stack) {
            bool routing = f && f->IsRoutingFrame();
            io.value(routing);
            if (io.in) {
                if (routing) f=std::make_shared<VMRoutingFrame>(); else f=std::make_shared<VMStackFrame>();
                f->Thread = &t;
            }
            Require(bool(f), "empty stack frame");
            owner(f->CodeOwner); behavior(f->Routine, f->CodeOwner, false);
            reference(f->Caller, "frame caller"); reference(f->Callee, "frame callee");
            // FreeSO's frame marshal saves StackObjectID even when the instance
            // has been deleted. Preserve the scalar without reviving the object
            // or binding a later occupant of the same ID. Active owners and
            // targets continue to require an exact live participant.
            VMEntity* stackObject = f->StackObject;
            int16_t rawID = f->StackObjectRawID;
            if (io.out && stackObject && stackObject->Dead) {
                rawID = f->StackObjectID();
                stackObject = nullptr;
            }
            reference(stackObject, "frame stack object"); io.value(rawID);
            if (io.in) { f->StackObject = stackObject; f->StackObjectRawID = rawID; }
            io.value(f->InstructionPointer);
            io.value(f->SpecialResult); io.value(f->ActionTree);
            io.vector(f->Args, 4096); io.vector(f->Locals, 4096);
            Require(f->Caller && f->Callee && f->InstructionPointer < f->Routine->Instructions.size() &&
                    f->SpecialResult <= VMSpecialResult::Retry, "invalid saved frame");
            if (routing) { auto rf=static_cast<VMRoutingFrame*>(f.get()); route(*rf, parent); parent=rf; }
            else Require(f->Locals.size() == std::max(f->Routine->Locals, f->Routine->Arguments), "local bank has wrong size");
            if (f->SpecialResult == VMSpecialResult::Interaction) ++boundaries;
        }
        Require(boundaries == t.ActiveQueueBlock + 1, "action frames disagree with active queue");
        if (version >= 8) {
            bool blocked = bool(t.BlockingState); io.value(blocked);
            if (blocked) {
                std::shared_ptr<Primitives::VMDialogResult> result;
                if (io.out) result = std::dynamic_pointer_cast<Primitives::VMDialogResult>(t.BlockingState);
                else result = std::make_shared<Primitives::VMDialogResult>();
                Require(bool(result), "unsupported TS1 async state");
                auto type = Model::TSOPlatform::VMAsyncStateType::DialogResult; io.value(type);
                Require(type == Model::TSOPlatform::VMAsyncStateType::DialogResult, "unsupported TS1 async tag");
                io.value(result->Responded); io.value(result->WaitTime); io.value(result->Version);
                io.value(result->Timeout); io.value(result->ResponseCode);
                io.string(result->ResponseText, VM::TS1TextInputMaxBytes); io.value(result->Type);
                Require(result->WaitTime >= 0 && result->Timeout >= 0 &&
                        uint8_t(result->Type) <= 15 && VM::IsTS1TextInputValid(result->ResponseText),
                        "invalid TS1 dialog state");
                Require(!t.Stack.empty(), "dialog wait has no source frame");
                auto frame = t.Stack.back();
                const auto& instruction = frame->Routine->Instructions[frame->InstructionPointer];
                auto operand = dynamic_cast<Primitives::VMDialogOperand*>(instruction.Operand.get());
                Require((instruction.Opcode == 36 || instruction.Opcode == 38 || instruction.Opcode == 39) &&
                        operand && operand->Type == result->Type &&
                        (operand->Flags & Primitives::VMDialogFlags::Continue) == 0,
                        "dialog wait disagrees with source primitive");
                Require(instruction.Opcode != 39 || (frame->Callee && frame->Callee->SemiGlobal),
                        "dialog semiglobal source missing");
                Require(instruction.Opcode != 38 || (frame->Global() && frame->Global()->Resource),
                        "dialog global source missing");
                // Presentation is local (same as FreeSO's HasDisplayed). Fresh
                // UI must be rebuilt, without consuming an accepted response.
                if (io.in) { result->HasDisplayed = false; t.BlockingState = result; }
            } else if (io.in) t.BlockingState.reset();
        }
        if (version >= 12) {
            auto uses=io.count(t.TS1ObjectUses.size(),1024);
            if (io.in) t.TS1ObjectUses.resize(uses);
            int32_t previous=0;
            for (auto& use : t.TS1ObjectUses) {
                reference(use.Target,"object use target");io.value(use.StackDepth);io.value(use.Flag);
                Require(use.StackDepth>0 && size_t(use.StackDepth)<=t.Stack.size() && use.StackDepth>previous,
                        "invalid object use depth");
                const auto& ownerFrame=t.Stack[size_t(use.StackDepth-1)];
                Require(ownerFrame && !ownerFrame->IsRoutingFrame() && ownerFrame->Routine,
                        "object use ends on a non-authored frame");
                previous=use.StackDepth;
            }
            reference(t.TS1FlaggedUseTarget,"flagged object use target");
            auto& last=t.TS1LastInteraction;
            io.value(last.UID);io.value(last.CallerID);io.value(last.TargetID);io.value(last.Icon);
            io.value(last.TTAIndex);for(auto& argument : last.Args)io.value(argument);
            io.value(last.Priority);io.value(last.ActionTreeID);io.value(last.Attenuation);io.value(last.Flags);
        }
    }
    void outfit(uint64_t& id) {
        io.value(id);
        auto content=Content::TS1::TS1AvatarContent::Get();
        if (!content->IsTS1OutfitID(id)) return;
        std::string key;
        if (io.out) {
            auto registered=content->GetOutfit(id);
            Require(registered,"missing registered TS1 outfit");
            key=registered->Name;
        }
        io.string(key);
        if (io.in) {
            // RegisterOutfit computes Name from appearance, texture and the six
            // original hand strings. The synthetic ID alone cannot reload them.
            std::vector<std::string> parts;
            size_t start=0;
            for (;;) {
                auto end=key.find('|',start);
                parts.emplace_back(key.substr(start,end==std::string::npos?end:end-start));
                if (end==std::string::npos) break;
                start=end+1;
            }
            Require(parts.size()==2 || parts.size()==8,"invalid TS1 outfit identity");
            bool hands=parts.size()==8;
            Require(content->RegisterOutfit(parts[0],parts[1],hands,hands?&parts[2]:nullptr)==id,
                    "outfit identity changed");
        }
    }
    void avatarTiming(AvatarRecord& a) {
        if (version >= 15) {
            io.value(a.rawAnimationClock); io.value(a.hurryableAnimation);
            io.value(a.motionPositionKnown); io.value(a.motionX); io.value(a.motionY);
            io.value(a.motionLevel); io.value(a.motionRate); io.value(a.motionRateTick);
            Require(a.motionRate > 0, "invalid locomotion rate");
            Require(!a.motionPositionKnown || (a.motionLevel > 0 &&
                    a.motionX >= -32768.f && a.motionX < 32768.f &&
                    a.motionY >= -32768.f && a.motionY < 32768.f),
                    "invalid locomotion position");
        } else if (io.out) {
            Require(!a.rawAnimationClock && !a.hurryableAnimation && !a.motionPositionKnown,
                    "source model state requires checkpoint version15");
        }
    }
    void avatar(AvatarRecord& a, VMAvatar& live) {
        Require(live.Avatar && live.MotiveDecay, "avatar has no animation or motive service");
        if (io.out) {
            a.animations = live.Animations; a.carry = live.CarryAnimationState;
            a.rawAnimationClock = live.TS1RawAnimationClock;
            a.hurryableAnimation = live.TS1HurryableAnimation;
            a.motionX = live.TS1MotionX; a.motionY = live.TS1MotionY;
            a.motionLevel = live.TS1MotionLevel;
            a.motionPositionKnown = live.TS1MotionPositionKnown;
            a.motionRate = live.TS1MotionRate; a.motionRateTick = live.TS1MotionRateTick;
            a.person = live.PersonData;
            a.hasFootprint = bool(live.Footprint);
            if (a.hasFootprint) {
                a.minX=live.Footprint->x1; a.minY=live.Footprint->y1;
                a.maxX=live.Footprint->x2; a.maxY=live.Footprint->y2;
            }
            for (size_t i = 0; i < 16; ++i) {
                a.motives[i] = live.GetMotiveData(Model::VMMotive(i));
                a.changes[i].motive = uint32_t(i);
                a.changes[i].ticked = true;
            }
            // A one-slot legacy reader cannot execute duplicate float deltas.
            // Emit a bounded last-record projection without residual debt; ObjM
            // remains the sole precise representation selected by ODMP.
            auto project = [](double value) {
                Require(std::isfinite(value), "non-finite motive projection");
                return int16_t(std::clamp(std::round(value), -32768., 32767.));
            };
            for (const auto& change : live.TS1MotiveChanges) {
                MotivePersistence::ValidateChange(change);
                auto& legacy = a.changes[size_t(change.Motive)];
                legacy.rate = project(double(change.TickDelta) * 1800.);
                legacy.stop = project(change.StopAt);
            }
            a.decayFractions.fill(0);
            a.decayMinute = vm.Context->Clock->Minutes / 2; a.direction = live.RadianDirection();
            a.killTimeout = live.KillTimeout;
            a.velocity = live.Velocity;
            a.body=live.BodyOutfit(); a.head=live.HeadOutfit(); a.suits=live.DefaultSuits;
            a.bodyPart=live.TS1Appearance.Body; a.headPart=live.TS1Appearance.Head;
            a.accessories.assign(live.BoundAppearances.begin(), live.BoundAppearances.end());
            std::sort(a.accessories.begin(), a.accessories.end());
            if (live.WorldUI) a.renderFlags=static_cast<int16_t>(live.AvaUI()->DisplayFlags);
            a.leftHand = int(static_cast<Vitaboy::SimHandGesture>(live.Avatar->LeftHandGesture));
            a.rightHand = int(static_cast<Vitaboy::SimHandGesture>(live.Avatar->RightHandGesture));
        }
        uint32_t n = io.count(a.animations.size(), 64);
        if (io.in) a.animations.resize(n);
        for (auto& layer : a.animations) { animation(layer); Require(bool(layer), "empty animation layer"); }
        animation(a.carry);
        avatarTiming(a);
        io.scalars(a.person); io.scalars(a.motives);
        for (auto& change : a.changes) {
            io.value(change.rate); io.value(change.stop); io.value(change.motive);
            Require(change.motive < 16, "invalid motive change");
            io.value(change.ticked); io.value(change.residual);
        }
        io.scalars(a.decayFractions); io.value(a.decayMinute); io.value(a.direction);
        io.value(a.killTimeout); io.value(a.leftHand); io.value(a.rightHand);
        io.value(a.velocity.X); io.value(a.velocity.Y); io.value(a.velocity.Z); io.value(a.renderFlags);
        outfit(a.body); outfit(a.head); outfit(a.suits.Daywear); outfit(a.suits.Swimwear); outfit(a.suits.Sleepwear);
        for (auto part : {&a.bodyPart, &a.headPart}) {
            io.string(part->Mesh); io.string(part->Binding); io.string(part->Texture);
        }
        auto cnt=io.count(a.accessories.size(),256);
        if (io.in) a.accessories.resize(cnt);
        for (auto& name:a.accessories) {
            io.string(name);
            // ChangeSuitOrAccessory retains the requested name even if this
            // optional appearance is absent; Avatar::AddAccessory(nullptr) does
            // nothing. Preserve that distinction and reject a changed asset set.
            bool available = Content::Content::Get()->AvatarAppearances->Get(name) != nullptr;
            bool savedAvailable = version >= 6 ? available : true;
            if (version >= 6) io.value(savedAvailable);
            Require(savedAvailable == available, "bound accessory availability changed: " + name);
        }
        if (version >= 10) {
            io.value(a.hasFootprint);
            if (a.hasFootprint) { io.value(a.minX); io.value(a.minY); io.value(a.maxX); io.value(a.maxY); }
        }
        Require(a.leftHand >= 0 && a.leftHand <= 3 && a.rightHand >= 0 && a.rightHand <= 3, "invalid hand gesture");
    }
    void record(Record& r) {
        reference(r.entity, "participant");
        Require(r.entity && r.entity->Thread, "participant has no thread");
        auto& e = *r.entity;
        auto av = dynamic_cast<VMAvatar*>(&e);
        bool isAvatar = av != nullptr;
        io.value(isAvatar);
        Require(isAvatar == bool(av), "participant type changed");
        if (io.out) {
            r.attributes = e.AttributeData(); r.objectData = e.ObjectData;
            r.mainParam = e.MainParam; r.mainStack = e.MainStackOBJ;
            r.sprites = e.DynamicSpriteFlags; r.sprites2 = e.DynamicSpriteFlags2;
            r.position = e.Position(); r.container = e.Container; r.slot = e.Container ? e.ContainerSlot : -1;
            for (int i = 0; i < e.TotalSlots(); ++i) r.slots.push_back(e.GetSlot(i));
            r.list.assign(e.MyList.begin(), e.MyList.end());
        }
        io.vector(r.attributes, 32768); io.vector(r.objectData, 1024); io.vector(r.list, 32768);
        Require(r.objectData.size() == e.ObjectData.size(), "incompatible object data");
        io.value(r.mainParam); io.value(r.mainStack);
        io.value(r.sprites); io.value(r.sprites2);
        io.value(r.position.x); io.value(r.position.y); io.value(r.position.Level);
        reference(r.container, "container"); io.value(r.slot);
        auto n = io.count(r.slots.size(), 1024);
        const auto slotsNow = unsigned(e.TotalSlots());
        const bool legacyAvatarPrefix = io.in && av && slotsNow == 5 && n == 1;
        Require(n == slotsNow || legacyAvatarPrefix, "slot count changed");
        if (io.in) r.slots.resize(n);
        for (auto& s : r.slots) reference(s, "slot");
        if (legacyAvatarPrefix) {
            // Checkpoints written by the prior integrated avatar stored only
            // slot 0. Accept that exact old extent only when the independently
            // loaded classic graph has an empty suffix; never discard a child.
            for (unsigned slot = n; slot < slotsNow; ++slot)
                Require(!e.GetSlot(int(slot)), "legacy avatar prefix omits an occupant");
            r.slots.resize(slotsNow, nullptr);
        }
        if (e.MultitileGroup) for (auto member : e.MultitileGroup->Objects) include(member);
        if (io.in) r.preparedThread = std::make_unique<VMThread>(vm.Context, &e, 0);
        thread(io.in ? *r.preparedThread : *e.Thread);
        if (isAvatar) { r.avatar = std::make_unique<AvatarRecord>(); avatar(*r.avatar, *av); }
    }

    void publish(std::map<int16_t, Record>& records, const OBJM& objm, bool validateOnly = false) {
        for (auto& p : participants) Require(records.count(p.first), "partial participant group");
        for (auto& p : records) {
            auto& r = p.second;
            if (r.avatar) {
                const auto saved = objm.ObjectData.find(r.entity->ObjectID);
                Require(saved != objm.ObjectData.end() && saved->second.Instance &&
                        saved->second.Instance->OBJT && r.entity->Object && r.entity->Object->OBJ &&
                        saved->second.Instance->OBJT->GUID == r.entity->Object->OBJ->GUID,
                        "avatar footprint identity changed");
            }
            if (r.container) {
                auto& slots = records.at(r.container->ObjectID).slots;
                Require(r.slot >= 0 && size_t(r.slot) < slots.size() && slots[r.slot] == r.entity, "container links disagree");
            } else Require(r.slot == -1, "invalid uncontained slot");
            std::set<VMEntity*> chain;
            for (auto cur = r.entity; cur; cur = records.at(cur->ObjectID).container)
                Require(chain.insert(cur).second, "cycle in saved containment");
            for (size_t i = 0; i < r.slots.size(); ++i) if (auto child = r.slots[i]) {
                auto& saved = records.at(child->ObjectID);
                Require(saved.container == r.entity && saved.slot == int(i), "slot links disagree");
            }
            // Recreating ordinary lot objects already placed them and their footprints.
            // A shifted multi-tile or failed placement needs its own placement transaction.
            if (!r.container && !r.avatar && !(r.entity->Position() == r.position)) {
                const auto actual = r.entity->Position();
                auto describe = [](LotView::Model::LotTilePos p) {
                    return std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.Level);
                };
                // ARRY places the group at the tile center. ObjM and ODHI retain
                // signed sixteenth-tile coordinates. Admit only a translation
                // inside the already placed tiles, identical for every part.
                bool translated = actual.Level > 0 && actual.TileID() == r.position.TileID();
                if (r.entity->MultitileGroup) for (auto part : r.entity->MultitileGroup->Objects) {
                    const auto& other = records.at(part->ObjectID);
                    const auto now = part->Position();
                    translated = translated && !other.container && now.TileID() == other.position.TileID() &&
                                 int(other.position.x) - now.x == int(r.position.x) - actual.x &&
                                 int(other.position.y) - now.y == int(r.position.y) - actual.y;
                }
                Require(translated, "object position changed for " + r.entity->ToString() + " #" +
                        std::to_string(r.entity->ObjectID) + " saved=" + describe(r.position) +
                        " loaded=" + describe(actual));
            }
            for (int i = 0; i < r.entity->TotalSlots(); ++i)
                Require(!r.entity->GetSlot(i) || records.count(r.entity->GetSlot(i)->ObjectID), "slot acquired an external occupant");
            Require(!r.entity->Container || records.count(r.entity->Container->ObjectID), "unexpected external container");
        }
        if (validateOnly) return;
        // Candidate belongs solely to the loader until it returns success. Detach
        // before rebuilding links; PlaceInSlot(false) dispatches no position EP.
        for (auto& p : records) {
            auto e = p.second.entity;
            if (e->Container) { vm.Context->UnregisterObjectPos(e); e->Container->ClearSlot(e->ContainerSlot); }
        }
        for (auto& p : records) {
            auto& r = p.second; auto e = r.entity;
            if (!r.container && e->Position() != r.position) {
                vm.Context->UnregisterObjectPos(e);
                e->Position(r.position);
                if (r.avatar) e->PositionChange(vm.Context, true);
                else {
                    // Same occupied tile and group geometry. Refresh spatial
                    // queries and obstacle without executing placement trees.
                    vm.Context->RegisterObjectPos(e);
                    e->VMEntity::PositionChange(vm.Context, true);
                }
            }
        }
        std::set<int16_t> placed;
        auto place = [&](auto&& self, Record& r) -> void {
            if (!placed.insert(r.entity->ObjectID).second || !r.container) return;
            self(self, records.at(r.container->ObjectID));
            vm.Context->UnregisterObjectPos(r.entity);
            Require(r.container->PlaceInSlot(r.entity, r.slot, false, vm.Context), "cannot bind validated slot");
            r.entity->PositionChange(vm.Context, true);
        };
        for (auto& p : records) place(place, p.second);
        for (auto& p : records) {
            auto& r = p.second; auto e = r.entity;
            Require(e->Position() == r.position, "saved participant position disagrees with its container");
            e->ObjectData = std::move(r.objectData);
            e->Attributes = std::move(r.attributes); e->MyList.assign(r.list.begin(), r.list.end());
            e->MainParam = r.mainParam; e->MainStackOBJ = r.mainStack;
            e->TS1SchedulerState = r.schedulerState;
            for (int i=0; i<128; ++i) {
                const auto flags = i < 64 ? r.sprites : r.sprites2;
                e->SetDynamicSpriteFlag(i, ((flags >> (i % 64)) & 1) != 0);
            }
            if (auto go = dynamic_cast<VMGameObject*>(e)) go->RefreshGraphic();
            if (r.avatar) {
                auto& a = *r.avatar; auto live = static_cast<VMAvatar*>(e);
                live->PersonData = a.person;
                live->MotiveData = a.precision.current; live->OldMotiveData = a.precision.old;
                live->TS1MotiveChanges = std::move(a.precision.changes);
                // Legacy accumulators have been consumed exactly once at prepare.
                live->MotiveDecay->MotiveFractions.fill(0);
                live->MotiveDecay->LastMinute = a.decayMinute;
                live->MotiveDecay->LastTick = a.precision.lastTick;
                live->Animations = std::move(a.animations); live->CarryAnimationState = std::move(a.carry);
                live->TS1RawAnimationClock = a.rawAnimationClock;
                live->TS1HurryableAnimation = a.hurryableAnimation;
                live->TS1MotionX = a.motionX; live->TS1MotionY = a.motionY;
                live->TS1MotionLevel = a.motionLevel;
                live->TS1MotionPositionKnown = a.motionPositionKnown;
                live->TS1MotionRate = a.motionRate; live->TS1MotionRateTick = a.motionRateTick;
                live->RadianDirection(a.direction); live->Velocity=a.velocity; live->KillTimeout = a.killTimeout;
                const auto saved = objm.ObjectData.find(e->ObjectID);
                const auto& bounds = saved->second.Instance->Footprint;
                if (version >= 10) {
                    if (a.hasFootprint) live->RestoreTS1Footprint(a.minX, a.minY, a.maxX, a.maxY);
                    else live->Footprint.reset();
                } else live->RestoreTS1Footprint(bounds.MinX, bounds.MinY, bounds.MaxX, bounds.MaxY);
                live->TS1Appearance.Body=std::move(a.bodyPart); live->TS1Appearance.Head=std::move(a.headPart);
                live->DefaultSuits=a.suits; live->BodyOutfit(a.body); live->HeadOutfit(a.head);
                for (const auto& old:live->BoundAppearances)
                    live->Avatar->RemoveAccessory(Content::Content::Get()->AvatarAppearances->Get(old));
                live->BoundAppearances.clear();
                for (const auto& name:a.accessories) {
                    live->Avatar->AddAccessory(Content::Content::Get()->AvatarAppearances->Get(name));
                    live->BoundAppearances.insert(name);
                }
                live->SetPersonData(Model::VMPersonDataVariable::CensorshipFlags,
                                    a.person[int(Model::VMPersonDataVariable::CensorshipFlags)]);
                live->SetPersonData(Model::VMPersonDataVariable::RenderDisplayFlags, a.renderFlags);
                live->Avatar->LeftHandGesture = static_cast<Vitaboy::SimHandGesture>(a.leftHand);
                live->Avatar->RightHandGesture = static_cast<Vitaboy::SimHandGesture>(a.rightHand);
            }
            e->Thread = std::move(r.preparedThread);
            // Old native records kept internal room0 in ObjectData. Publication
            // installs that bank after positioning, so normalize the script
            // sentinel here without replaying placement, Init or Reset.
            if (vm.TS1 && e->Position() == LotView::Model::LotTilePos::OUT_OF_WORLD) {
                e->SetRoom(0);
            }
        }
    }
private:
    VM& vm;
    Archive& io;
    uint32_t version;
    uint32_t roomSignature = 0;
    bool hasRoomSignature = false;
};

namespace {
void ClockState(Archive& io, VMClock& c, uint64_t& seed) {
    io.value(c.Ticks); io.value(c.MinuteFractions); io.value(c.TicksPerMinute);
    io.value(c.Hours); io.value(c.Minutes); io.value(c.DayOfMonth); io.value(c.Month); io.value(c.Year);
    io.value(c.FirePercent); io.value(seed);
    Require(c.Ticks >= 0 && c.TicksPerMinute > 0 && c.MinuteFractions >= 0 && c.MinuteFractions < c.TicksPerMinute &&
            c.Minutes >= 0 && c.Minutes < 60 && c.Hours >= 0 && c.Hours < 24, "invalid clock");
}
void DepartureState(Archive& io, TS1Departure& departure) {
    io.value(departure.source); io.value(departure.destination); io.value(departure.family);
    io.value(departure.caller); io.value(departure.callerGUID);
    io.value(departure.target); io.value(departure.targetGUID);
    io.value(departure.ownerGUID); io.value(departure.actionUID); io.value(departure.tree);
    io.value(departure.requestPC); io.value(departure.nextPC); io.vector(departure.party, 8);
}

void VerifyDeparture(VM& vm, const TS1Departure& departure, VMThread* thread) {
    Require(departure.source == vm.GetGlobalValue(VMTS1Neighbourhood::GLOBAL_CURRENT_HOUSE) &&
            (departure.destination > 0 || departure.destination == -1) && departure.destination != departure.source,
            "departure lot mismatch");
    auto caller = dynamic_cast<VMAvatar*>(vm.GetObjectById(departure.caller));
    auto target = vm.GetObjectById(departure.target);
    auto provider = VMTS1Neighbourhood::Provider();
    auto family = provider ? provider->GetFamily(departure.family) : nullptr;
    Require(caller && !caller->Dead && caller->Object->GUID == departure.callerGUID &&
            target && !target->Dead && target->Object->GUID == departure.targetGUID && family &&
            !departure.party.empty() && departure.party.front() == departure.callerGUID,
            "departure household, caller or target changed");
    std::set<uint32_t> party;
    for (auto guid : departure.party) {
        Require(party.insert(guid).second &&
                std::find(family->FamilyGUIDs.begin(), family->FamilyGUIDs.end(), guid) != family->FamilyGUIDs.end(),
                "invalid departure party");
        int present = 0;
        for (auto entity : vm.Entities) if (dynamic_cast<VMAvatar*>(entity) && !entity->Dead &&
                                            entity->Object->GUID == guid) ++present;
        Require(present == 1, "departure party is absent or ambiguous");
    }
    auto action = thread ? thread->ActiveAction() : nullptr;
    Require(action && action->UID == departure.actionUID && action->Callee == target && !thread->Stack.empty(),
            "dispatched action changed");
    const auto& frame = thread->Stack.back();
    Require(frame && frame->Caller == caller && frame->ActionTree && frame->CodeOwner &&
            frame->CodeOwner->GUID == departure.ownerGUID && frame->Routine && frame->Routine->Chunk &&
            frame->Routine->ID == departure.tree && frame->InstructionPointer == departure.nextPC &&
            departure.requestPC < frame->Routine->Chunk->Instructions.size(), "dispatch frame changed");
    const auto& instruction = frame->Routine->Chunk->Instructions[departure.requestPC];
    Require(instruction.Opcode == 1 && instruction.Operand[0] == 17 && instruction.TruePointer == departure.nextPC &&
            departure.nextPC != departure.requestPC && departure.nextPC < 253, "dispatch true edge changed");
}


}

void VMTS1InteractionCheckpoint::Capture(VM& vm, IffRawFile& destination, bool departing,
                                       bool schedulerWordsBound, uint32_t outputVersion) {
    Require(!vm.HasTS1PendingRemovals(), "object-removal dispatch is incomplete");
    // Publish no portion of a failed capture, including synchronized SIMI.
    IffRawFile house=destination;
    FSO::SimAntics::Utils::SyncTS1Budget(vm, house);
    if (outputVersion >= 14 && !vm.TS1Budget.Initialized)
        FSO::SimAntics::Utils::SyncTS1RandomState(vm, house);
    if (schedulerWordsBound) {
        auto rawObjects = house.Find("ObjM", 1);
        auto rawTypes = house.Find("objt", 0);
        Require(rawObjects && rawTypes, "scheduler binding lacks classic graph");
        OBJT types; st::Reader typeInput(rawTypes->Data); types.Read(nullptr, typeInput);
        OBJM objects; st::Reader objectInput(rawObjects->Data); objects.Read(nullptr, objectInput);
        std::map<uint32_t, OBJD*> definitions;
        size_t count = 0;
        for (auto e : vm.Entities) if (e && !e->Dead && !e->GhostImage && e->Object && e->Object->OBJ) {
            definitions.emplace(e->Object->OBJ->GUID, e->Object->OBJ); ++count;
        }
        Require(objects.Prepare([&](uint16_t id) {
            OBJMResource resource;
            for (auto& type : types.Entries) if (type.TypeID == id) {
                resource.OBJT = &type;
                auto definition = definitions.find(type.GUID);
                if (definition != definitions.end()) resource.OBJD = definition->second;
                break;
            }
            return resource;
        }), "scheduler binding cannot decode classic graph");
        Require(objects.ObjectData.size() == count, "scheduler binding instance count changed");
        for (auto e : vm.Entities) if (e && !e->Dead && !e->GhostImage && e->Object && e->Object->OBJ) {
            const auto saved = objects.ObjectData.find(e->ObjectID);
            Require(saved != objects.ObjectData.end() && saved->second.Instance &&
                    saved->second.Instance->OBJT && saved->second.Instance->OBJT->GUID == e->Object->OBJ->GUID,
                    "scheduler binding instance identity changed");
            Require(TS1SavedSchedulerState(*saved->second.Instance, objects.Version, 9) == e->TS1SchedulerState,
                    "scheduler binding manager word changed");
            if (dynamic_cast<VMAvatar*>(e) && !e->Container && e->Footprint &&
                e->Position() != LotView::Model::LotTilePos::OUT_OF_WORLD) {
                const auto& b = saved->second.Instance->Footprint;
                Require(e->Footprint->x1==b.MinX && e->Footprint->y1==b.MinY &&
                        e->Footprint->x2==b.MaxX && e->Footprint->y2==b.MaxY,
                        "classic avatar footprint differs from live cache");
            }
        }
    }
    bool financeBound = vm.TS1 && vm.TS1Budget.Initialized;
    Require((outputVersion>=12 && outputVersion<=CurrentVersion) || (outputVersion==10 && !financeBound) || (outputVersion==11 && financeBound),
            "unsupported capture compatibility version");
    const uint32_t captureVersion = outputVersion;
    st::Writer body(st::Endian::Little);
    Archive fields(body);
    Require(VM::IsTS1TextInputValid(vm.TS1TextInput), "invalid TS1 text input");
    InteractionCheckpointCodec codec(vm, fields, captureVersion);
    // Every persisted instance owns its Main routine, even when no avatar or
    // global controller currently references it. Restarting that routine can
    // replay one-time lifecycle effects (for example, naming a resident baby).
    // Match the classic saver identity domain and retain all live threads;
    // reference closure below still validates slots, groups and action targets.
    for (auto e : vm.Entities) {
        if (!e || e->Dead || e->GhostImage || !e->Object || !e->Object->OBJ) continue;
        Require(bool(e->Thread), "persistent instance has no execution thread " + std::to_string(e->ObjectID));
        codec.include(e);
    }
    if (codec.participants.empty()) { house.Remove("ODHI", 1); house.Remove("ODMP", 1); destination=std::move(house); return; }
    if (departing) {
        auto caller = vm.GetObjectById(vm.Departure.caller);
        VerifyDeparture(vm, vm.Departure, caller ? caller->Thread.get() : nullptr);
    }
    Require(!house.Find("FSOV", 1), "house already has an independent FSOV snapshot");
    auto objm = house.Find("ObjM", 1);
    Require(objm, "missing ObjM");
    std::set<int16_t> done;
    while (done.size() < codec.participants.size()) {
        for (auto& p : codec.participants) {
            if (!done.insert(p.first).second) continue;
            Record record; record.entity = p.second;
            try { codec.record(record); }
            catch (const std::exception& e) { throw std::runtime_error("Object " + std::to_string(p.first) + ": " + e.what()); }
            Require(body.size() < ODHI::MaxBytes, "snapshot exceeds limit");
        }
    }
    st::Writer data(st::Endian::Little);
    // v9 makes the signature-bound ObjM manager tail authoritative. No extra
    // copy is appended to each execution record; v8 and older tails were stale.
    data.WriteUInt32(captureVersion);
    data.WriteUInt32(Files::Hash::CalculateHash(objm->Data));
    Archive header(data);
    header.value(departing);
    if (departing) DepartureState(header, vm.Departure);
    header.value(schedulerWordsBound);
    if (captureVersion>=12) header.value(financeBound);
    if (financeBound) {
        auto simi=house.Find("SIMI",1);
        Require(simi && simi->Data.size() <= UINT32_MAX, "missing or oversized synchronized SIMI");
        uint32_t length=uint32_t(simi->Data.size()), hash=Files::Hash::CalculateHash(simi->Data);
        header.value(length);header.value(hash);
    }
    auto clock = *vm.Context->Clock;
    auto seed = vm.Context->RandomSeed;
    ClockState(header, clock, seed);
    // Earlier snapshots had a single stream. Version14 preserves the separate
    // owner dispatch state without changing any per-object execution record.
    if (captureVersion >= 14) {
        auto simulationSeed = vm.Context->TS1SimulationRandomSeed;
        header.value(simulationSeed);
    }
    header.value(vm.Ready); header.value(vm.AlternateTick); header.value(vm.TS1BuildBuyEnabled);
    header.vector(vm.GlobalState, 4096);
    header.string(vm.TS1TextInput, VM::TS1TextInputMaxBytes);
    data.WriteUInt32(done.size());
    data.WriteBytes(body.ToArray());
    Require(data.size() <= ODHI::MaxBytes, "snapshot exceeds limit");
    const auto objmHash = Files::Hash::CalculateHash(objm->Data);
    const auto native = data.ToArray();
    st::Writer marker(st::Endian::Little); Archive metadata(marker);
    uint32_t schema = 2, classicHash = objmHash, nativeHash = Files::Hash::CalculateHash(native);
    uint32_t nativeBytes = uint32_t(native.size());
    metadata.value(schema); metadata.value(classicHash); metadata.value(nativeHash); metadata.value(nativeBytes);
    uint32_t avatars = 0;
    for (const auto& participant : codec.participants) if (dynamic_cast<VMAvatar*>(participant.second)) ++avatars;
    metadata.value(avatars);
    for (const auto& participant : codec.participants) if (auto avatar = dynamic_cast<VMAvatar*>(participant.second)) {
        int16_t id = participant.first;
        uint32_t guid = avatar->Object->OBJ->GUID;
        int16_t neighbour = avatar->GetPersonData(Model::VMPersonDataVariable::NeighborId);
        int64_t phase = InteractionCheckpointCodec::motivePhase(*avatar);
        Require(phase >= 0 && phase <= clock.Ticks, "invalid saved motive phase");
        metadata.value(id); metadata.value(guid); metadata.value(neighbour); metadata.value(phase);
    }
    Require(marker.size() <= ODMP::MaxBytes, "motive marker exceeds limit");
    house.Set("ODHI", 1, native, "OpenDollhouse interactions");
    house.Set("ODMP", 1, marker.ToArray(), "OpenDollhouse motive precision");
    destination=std::move(house);
}

bool VMTS1InteractionCheckpoint::PreflightLegacyBudget(IffFile& house) {
    auto native=house.List<ODHI>();
    if(native.empty()) return false;
    Require(native.size()==1 && native.front()->ChunkID==1, "ambiguous ODHI");
    st::Reader input(native.front()->Data);const auto version=input.u32();
    if(version<2 || version>10) return false;
    auto classics=house.List<OBJM>();
    Require(classics.size()==1 && classics.front()->ChunkID==1, "ambiguous ObjM");
    const auto objmHash=input.u32();
    Require(objmHash==classics.front()->SourceSignature, "ObjM changed since capture");
    Archive fields(input);bool departing=false;
    if(version>=3) fields.value(departing);
    TS1Departure departure;if(departing)DepartureState(fields,departure);
    bool scheduler=false;if(version>=10)fields.value(scheduler);
    VMClock clock;uint64_t seed=0;ClockState(fields,clock,seed);
    bool ready=false,alternate=false,buildBuy=false;
    fields.value(ready);fields.value(alternate);fields.value(buildBuy);
    std::vector<int16_t> globals;fields.vector(globals,4096);
    std::string textInput;if(version>=7)fields.string(textInput,VM::TS1TextInputMaxBytes);
    Require(VM::IsTS1TextInputValid(textInput), "invalid TS1 text input");
    Require(fields.count(0,32767)>0, "empty execution supplement");
    auto markers=house.List<ODMP>();
    Require(markers.size()<=1 && (markers.empty() || markers.front()->ChunkID==1), "ambiguous ODMP");
    if(!markers.empty()) MotivePersistence::ReadMarker(markers.front()->Data,objmHash,
        Files::Hash::CalculateHash(native.front()->Data),native.front()->Data.size(),clock.Ticks);
    return true;
}

void VMTS1InteractionCheckpoint::ValidateFinanceBinding(IffFile& house) {
    auto native=house.List<ODHI>();
    if (native.empty()) return;
    Require(native.size()==1 && native.front()->ChunkID==1, "ambiguous ODHI");
    st::Reader input(native.front()->Data);const auto version=input.u32();
    Require(version>=2 && version<=CurrentVersion, "unsupported ODHI version");
    if (version<11) return;
    input.u32(); // ObjM signature is independently checked on restore.
    Archive header(input);bool departing=false;header.value(departing);
    TS1Departure departure;if(departing)DepartureState(header,departure);
    bool scheduler=false;header.value(scheduler);
    bool financeBound=true;if(version>=12)header.value(financeBound);
    if (!financeBound) return;
    uint32_t length=0,hash=0;header.value(length);header.value(hash);
    auto simis=house.List<SIMI>();
    Require(simis.size()==1 && simis.front()->ChunkID==1 &&
            simis.front()->SourceBytes==length && simis.front()->SourceSignature==hash,
            "SIMI changed since financial capture");
    Require(simis.front()->Unknown4==0, "bound SIMI has unconsumed financial delta");
}

bool VMTS1InteractionCheckpoint::IsDeparture(IffFile& house) {
    auto chunks = house.List<ODHI>();
    if (chunks.empty()) return false;
    Require(chunks.size() == 1 && chunks.front()->ChunkID == 1, "ambiguous ODHI");
    st::Reader input(chunks.front()->Data);
    auto version = input.u32();
    if (version == 2) return false;
    Require(version >= 3 && version <= CurrentVersion, "unsupported ODHI version");
    input.u32(); // ObjM signature is checked during restore.
    Archive header(input);
    bool departing = false;
    header.value(departing);
    return departing;
}

void VMTS1InteractionCheckpoint::Restore(VM& vm, IffFile& house, bool resumeDispatch, bool validateOnly, int32_t* legacyPurse, bool previewUnpublishedPortals) {
    Require(!previewUnpublishedPortals || validateOnly, "portal preview requires validation-only mode");
    auto chunks = house.List<ODHI>();
    if (chunks.empty()) return;
    Require(chunks.size() == 1 && chunks.front()->ChunkID == 1, "ambiguous ODHI");
    auto objm = house.Get<OBJM>(1);
    st::Reader input(chunks.front()->Data);
    const auto version = input.u32();
    Require(version >= 2 && version <= CurrentVersion, "unsupported ODHI version");
    Require(objm && objm->Decoded && input.u32() == objm->SourceSignature, "ObjM changed since capture");
    Archive fields(input);
    bool departing = false;
    TS1Departure departure;
    if (version >= 3) fields.value(departing);
    if (departing) DepartureState(fields, departure);
    bool schedulerWordsBound = version == 9;
    if (version >= 10) fields.value(schedulerWordsBound);
    SIMI* finance=nullptr;
    bool financeBound=version>=11;if(version>=12)fields.value(financeBound);
    if (financeBound) {
        uint32_t length=0, hash=0;fields.value(length);fields.value(hash);
        auto simis=house.List<SIMI>();
        Require(simis.size()==1 && simis.front()->ChunkID==1, "ambiguous financial SIMI");
        finance=simis.front();
        Require(finance->SourceBytes==length && finance->SourceSignature==hash, "SIMI changed since financial capture");
        Require(finance->Unknown4==0, "bound SIMI has unconsumed financial delta");
    }
    VMClock clock; uint64_t seed = 0;
    ClockState(fields, clock, seed);
    // Historical native snapshots cannot recover an unwritten owner stream;
    // their one saved state seeds both logical channels for migration.
    uint64_t simulationSeed = seed;
    if (version >= 14) fields.value(simulationSeed);
    if (version >= 14 && finance)
        Require(finance->SavedRandomState() == uint32_t(seed) &&
                finance->SavedSimulationRandomState() == uint32_t(simulationSeed),
                "native and classic random states differ");
    bool ready=false, alternate=false, buildBuy=false;
    fields.value(ready); fields.value(alternate); fields.value(buildBuy);
    std::vector<int16_t> globals;
    fields.vector(globals,4096);
    Require(globals.size()==vm.GlobalState.size(),"global bank size changed");
    if (finance) Require(globals.size()>14 && finance->GlobalData.size()>14 &&
        globals[13]==finance->GlobalData[13] && globals[14]==finance->GlobalData[14],
        "native and classic budget mirrors differ");
    // Versions2-6 did not store this channel. Empty is the original default;
    // do not infer a missing submitted name from a living parent's identity.
    std::string textInput;
    if (version >= 7) fields.string(textInput, VM::TS1TextInputMaxBytes);
    Require(VM::IsTS1TextInputValid(textInput), "invalid TS1 text input");
    const int houseID=VMTS1Neighbourhood::GLOBAL_CURRENT_HOUSE;
    Require(globals.at(houseID)==vm.GlobalState.at(houseID),"house identity changed");
    const auto n = fields.count(0, 32767);
    Require(n, "empty execution supplement");
    InteractionCheckpointCodec codec(vm, fields, version);
    codec.PreviewUnpublishedPortals=previewUnpublishedPortals;
    std::map<int16_t, Record> records;
    for (uint32_t i = 0; i < n; ++i) {
        Record r;
        try { codec.record(r); }
        catch (const std::exception& e) { throw std::runtime_error("Participant " + std::to_string(r.entity?r.entity->ObjectID:0) + ": " + e.what()); }
        const auto id = r.entity->ObjectID;
        Require(records.emplace(id, std::move(r)).second, "duplicate participant");
    }
    Require(!input.has_more(), "trailing execution state");
    if (version >= 7) {
        // v7 is a complete native graph, not a partial execution overlay.
        // Validate its saved identity domain before replacing any live thread.
        Require(records.size() == objm->ObjectData.size(), "incomplete v7 instance graph");
        for (const auto& saved : objm->ObjectData)
            Require(records.count(saved.first) != 0,
                    "v7 lacks ObjM instance " + std::to_string(saved.first));
    }
    // Prepare manager words from the same signature-bound classic graph before
    // publishing any field or thread. Older native supplements retain their
    // historical admitted behavior, rather than consuming a stale source word.
    if (schedulerWordsBound) for (auto& entry : records) {
        const auto saved = objm->ObjectData.find(entry.first);
        Require(saved != objm->ObjectData.end() && saved->second.Instance &&
                saved->second.Instance->OBJT &&
                saved->second.Instance->OBJT->GUID == entry.second.entity->Object->OBJ->GUID,
                "scheduler state instance identity changed");
        entry.second.schedulerState = TS1SavedSchedulerState(
            *saved->second.Instance, objm->Version, version);
        // Pre-v12 bound snapshots can recover the authoritative classic list.
        // Unbound older overlays have no trustworthy use list: do not infer it
        // from queue targets or callee changes.
        if (version<12 && entry.second.avatar && saved->second.Instance->HasPersonData) {
            auto& uses=entry.second.preparedThread->TS1ObjectUses;
            const auto& person=saved->second.Instance->PersonData;
            entry.second.preparedThread->TS1LastInteraction=person.LastInteraction;
            if (person.ActiveInteraction.Icon) {
                auto icon=vm.GetObjectById(person.ActiveInteraction.Icon);
                Require(icon && !icon->Dead,"invalid classic current interaction icon");
                entry.second.preparedThread->TS1FlaggedUseTarget=icon;
            }
            for (const auto& source : person.ObjectUses) {
                auto target=vm.GetObjectById(source.TargetID);
                const int32_t nativeDepth=entry.second.preparedThread->TS1NativeUseDepth(source.StackLength);
                Require(!source.TargetID || (target && !target->Dead),
                        "invalid classic object use");
                uses.push_back({target,nativeDepth,source.Unknown2});
            }
        }
    }
    if (version >= 10) for (const auto& entry : records) if (entry.second.avatar) {
        const auto& record = entry.second; const auto& a = *record.avatar;
        const bool inWorld = !record.container && record.position != LotView::Model::LotTilePos::OUT_OF_WORLD;
        Require(inWorld || !a.hasFootprint, "contained or out-of-world avatar has a footprint");
        if (schedulerWordsBound && inWorld && a.hasFootprint) {
            const auto saved = objm->ObjectData.find(entry.first);
            Require(saved != objm->ObjectData.end() && saved->second.Instance, "missing classic avatar footprint");
            const auto& b = saved->second.Instance->Footprint;
            Require(a.minX==b.MinX && a.minY==b.MinY && a.maxX==b.MaxX && a.maxY==b.MaxY,
                    "native and classic avatar footprints differ");
        }
    }
    if (departing) {
        auto caller = records.find(departure.caller);
        VerifyDeparture(vm, departure, caller != records.end() ? caller->second.preparedThread.get() : nullptr);
    }
    codec.preparePrimitiveStates(records, *objm, house.Get<OBJT>(0));
    codec.prepareMotives(records, house, *objm, *chunks.front(), clock.Ticks);
    codec.publish(records, *objm, validateOnly);
    if (validateOnly) {
        if (legacyPurse) {
            Require(globals.size()>14, "budget mirrors missing");
            const auto familyID=globals[VMTS1Neighbourhood::GLOBAL_CURRENT_FAMILY];
            if (version<=10) {
                auto provider=VMTS1Neighbourhood::Provider();
                auto family=provider ? provider->GameState.ActiveFamily : nullptr;
                *legacyPurse=(family && familyID>0 && int16_t(family->ChunkID)==familyID)
                    ? family->Budget : int32_t(globals[14])*10000+globals[13];
            }
            vm.TS1Budget.LegacyOwnerID=familyID;
        }
        auto simis=house.List<SIMI>();
        if (simis.size()==1 && simis.front()->LegacyBudgetCandidate) {
            vm.TS1Budget.LegacySIMIHash=simis.front()->SourceSignature;
            vm.TS1Budget.LegacyOBJMHash=objm->SourceSignature;
            vm.TS1Budget.LegacyODHIHash=Files::Hash::CalculateHash(chunks.front()->Data);
            vm.TS1Budget.LegacyCandidateValidated=true;
        }
        return;
    }
    *vm.Context->Clock = clock;
    vm.Context->RandomSeed = seed;
    vm.Context->TS1SimulationRandomSeed = simulationSeed;
    vm.Ready=ready; vm.AlternateTick=alternate; vm.TS1BuildBuyEnabled=buildBuy;
    // Family activation reconciles ownership and budget from FAMI. Its data
    // wins when this call also serves the low-level activator without a family.
    if (vm.GetGlobalValue(VMTS1Neighbourhood::GLOBAL_CURRENT_FAMILY)>0) {
        for (int index : {VMTS1Neighbourhood::GLOBAL_CURRENT_FAMILY, VMTS1Neighbourhood::GLOBAL_BUDGET_MOD,
                          VMTS1Neighbourhood::GLOBAL_BUDGET_DIV}) globals.at(index)=vm.GlobalState.at(index);
    }
    const bool activeFamily = vm.GetGlobalValue(VMTS1Neighbourhood::GLOBAL_CURRENT_FAMILY)>0;
    vm.GlobalState=std::move(globals);
    if (vm.TS1 && finance) {
        Primitives::VMTS1Budget::DaysType days{};
        for(size_t i=0;i<days.size() && i<finance->BudgetDays.size();++i) days[i]=finance->BudgetDays[i].Values();
        // Existing family/visit ownership wins over stale source-lot cash.
        const auto purse=activeFamily ? Primitives::VMTS1Budget::GetRawBudget(&vm) : finance->Unknown3;
        Primitives::VMTS1Budget::LoadState(&vm,purse,days);
    } else if (vm.TS1 && version<=10 && vm.TS1Budget.Initialized && !activeFamily) {
        // Older ODHI stored cash in the signed short mirrors, not SIMI.Unknown3.
        const int32_t purse=vm.TS1Budget.LegacyPromoted ? vm.TS1Budget.Purse : int32_t(vm.GlobalState[14])*10000+vm.GlobalState[13];
        Primitives::VMTS1Budget::LoadState(&vm,purse,vm.TS1Budget.Days);
    }
    vm.TS1TextInput=std::move(textInput);
    // Reopening the source of a saved visit must finish its accepted transfer.
    // Loading a previous home snapshot on arrival must never rearm that trip.
    if (departing && resumeDispatch) vm.Departure = std::move(departure);
    // The pre-publication promotion applies to this restore only. A later
    // snapshot on the same VM must use its own saved financial authority.
    vm.TS1Budget.LegacyPromoted = false;
}
}}}}

namespace FSO { namespace SimAntics { namespace Engine { namespace Utils {
void VMTS1InteractionCheckpoint::PresentDialogs(VM& vm) {
    for (auto entity : vm.Entities) {
        if (!entity || entity->Dead || !entity->Thread) continue;
        auto& thread = *entity->Thread;
        auto result = std::dynamic_pointer_cast<Primitives::VMDialogResult>(thread.BlockingState);
        if (!result || result->Responded || result->HasDisplayed ||
            result->WaitTime > Primitives::VMDialogPrivateStrings::DIALOG_MAX_WAITTIME) continue;
        Require(!thread.Stack.empty(), "restored dialog has no source");
        auto frame = thread.Stack.back();
        const auto& instruction = frame->Routine->Instructions.at(frame->InstructionPointer);
        auto operand = dynamic_cast<Primitives::VMDialogOperand*>(instruction.Operand.get());
        Require(operand, "restored dialog operand changed");
        STR* strings = nullptr;
        if (instruction.Opcode == 36) strings = frame->ScopeResource()->Get<STR>(301);
        else if (instruction.Opcode == 38) strings = frame->Global()->Resource->Get<STR>(301);
        else if (instruction.Opcode == 39 && frame->Callee->SemiGlobal) strings = frame->Callee->SemiGlobal->Get<STR>(301);
        else Require(false, "restored dialog source changed");
        // Existing producer: emits text/buttons/choice metadata only. No tick,
        // PC movement, timeout step or response consumption is performed.
        VMDialogHandler::ShowDialog(frame.get(), operand, strings);
        result->HasDisplayed = true;
    }
}

void VMTS1InteractionCheckpoint::RestoreClassic(VM& vm, IffFile& house) {
    Require(house.List<ODHI>().empty(), "classic restore cannot replace an ODHI continuation");
    auto objm = house.Get<OBJM>(1); auto objt = house.Get<OBJT>(0);
    Require(objm && objt, "classic continuation is missing ObjM/objt");
    st::Writer unused; Archive archive(unused);
    InteractionCheckpointCodec codec(vm, archive, 7);
    codec.classic(*objm, *objt);
}
}}}}
