// SPDX-License-Identifier: MPL-2.0
// Synthetic service contract: no Content::Init, commercial files, save paths,
// simulation ticks or gameplay claims. Save/fresh App coverage is a separate gate.
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "files/formats/iff/chunks/BCON.h"
#include "files/formats/iff/chunks/NBRS.h"
#include "files/formats/iff/chunks/FAMI.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMClock.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMQueuedAction.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMTS1Neighbourhood.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Model;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using Service = FSO::SimAntics::Engine::Utils::VMTS1Neighbourhood;
static int checks = 0;
static void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
    ++checks;
}
// Same test-only access idiom as saved_object_lifecycle.cpp; no production hook.
template<class T, typename T::Type M> struct Access {
    friend typename T::Type field(T) { return M; }
};
struct ObjectsField {
    using Type = std::unordered_map<int16_t, VMEntity*> VM::*;
    friend Type field(ObjectsField);
};
template struct Access<ObjectsField, &VM::ObjectsById>;

struct Fixture {
    // Deliberately unrelated to any shipped GUID, NID, lot or social category.
    const int16_t nid, actorID, objectID, socialCategory;
    IffFile objectIff, globalIff;
    FSO::Content::GameObjectResource resource{&objectIff, nullptr, nullptr, "social-reference-fixture", true};
    FSO::Content::GameGlobalResource globalResource{&globalIff, nullptr};
    FSO::Content::GameGlobal globals;
    OBJD personDef, tokenDef;
    FSO::Content::GameObject personObject, tokenObject;
    NBRS neighbors;
    FAMI family;
    VMContext context{nullptr};
    VM vm{&context, nullptr};
    Neighbour* neighbor = nullptr;
    VMAvatar* actor = nullptr;
    VMGameObject* linked = nullptr;
    BCON* categories = nullptr;
    std::unique_ptr<FSO::Content::TS1::TS1NeighborhoodProvider> previousProvider;

    explicit Fixture(int16_t seed = 1)
        : nid(120 + seed), actorID(300 + seed), objectID(600 + seed),
          socialCategory(70 + seed) {
        vm.TS1 = true;
        globals.Resource = &globalResource;
        context.Globals = &globals;
        categories = new BCON();
        categories->ChunkID = 262; categories->ChunkType = "BCON";
        categories->ChunkProcessed = true;
        categories->Constants.assign(30, 0);
        categories->Constants[29] = uint16_t(socialCategory);
        globalIff.AddChunk(categories);
        personDef.GUID = 0x0d000000u + uint32_t(seed);
        personDef.ObjectType = OBJDType::Person;
        personDef.NumAttributes = 3; personDef.StackSize = 32;
        tokenDef = personDef; tokenDef.GUID += 0x1000;
        tokenDef.ObjectType = OBJDType::GiftToken;
        personObject.OBJ = &personDef; personObject.GUID = personDef.GUID;
        personObject.Resource = &resource;
        tokenObject.OBJ = &tokenDef; tokenObject.GUID = tokenDef.GUID;
        tokenObject.Resource = &resource;
        auto entry = std::make_unique<Neighbour>();
        neighbor = entry.get();
        neighbor->NeighbourID = nid; neighbor->GUID = personDef.GUID;
        neighbor->Version = 0xa; neighbor->PersonMode = 9;
        neighbor->Name = "Authored neighbor";
        neighbor->PersonData.resize(88);
        for (size_t i = 0; i < neighbor->PersonData.size(); ++i)
            neighbor->PersonData[i] = int16_t(200 + i);
        neighbor->PersonData[31] = nid; neighbor->PersonData[59] = objectID;
        neighbor->Relationships[nid + 1] = {12, 34, 56};
        neighbors.NeighbourByID[nid] = neighbor;
        neighbors.DefaultNeighbourByGUID[personDef.GUID] = nid;
        neighbors.Entries.push_back(std::move(entry));
        auto content = FSO::Content::Content::Get();
        previousProvider = std::move(content->Neighborhood);
        content->Neighborhood = std::make_unique<FSO::Content::TS1::TS1NeighborhoodProvider>(content, false);
        content->Neighborhood->Neighbors = &neighbors;
        family.ChunkID = 7; family.Budget = 1789;
        content->Neighborhood->GameState.ActiveFamily = &family;
        vm.GlobalState.assign(38, 0);
        vm.SetGlobalValue(9, int16_t(family.ChunkID));
        vm.SetGlobalValue(13, 1789);
        actor = static_cast<VMAvatar*>(publish(new VMAvatar(&personObject), actorID));
        linked = static_cast<VMGameObject*>(publish(new VMGameObject(&tokenObject, nullptr), objectID));
        std::vector<int16_t> person(101);
        for (size_t i = 0; i < person.size(); ++i) person[i] = int16_t(500 + i);
        float motives[16] = {};
        actor->RestorePersonData(person, motives);
        actor->SetPersonData(VMPersonDataVariable::NeighborId, nid);
        actor->SetPersonData(VMPersonDataVariable::InteractionStatus, objectID);
        for (int i = 0; i < 16; ++i) actor->SetMotiveData(VMMotive(i), int16_t(30 + i));
        actor->SetAttribute(0, 13);
        linked->SetAttribute(0, 17);
        linked->SetValue(VMStackObjectVariable::Category, socialCategory + 1);
    }
    ~Fixture() {
        // Neither provider owns these in-memory chunks/family.
        FSO::Content::Content::Get()->Neighborhood = std::move(previousProvider);
    }
    VMEntity* publish(VMEntity* e, int16_t id) {
        vm.OwnEntity(e);
        auto group = vm.OwnGroup(new Entities::VMMultitileGroup());
        e->ObjectID = id; e->MultitileGroup = group; group->AddObject(e);
        vm.Entities.push_back(e); (vm.*field(ObjectsField{}))[id] = e;
        e->PrepareForRestore(&context);
        return e;
    }
    std::shared_ptr<VMStackFrame> frame(VMEntity* owner) {
        auto f = std::make_shared<VMStackFrame>();
        f->Thread = owner->Thread.get(); f->Caller = owner; f->Callee = owner;
        f->CodeOwner = owner->Object; f->InstructionPointer = 23;
        f->Args = {4, 5}; f->Locals = {6, 7};
        owner->Thread->Stack.push_back(f);
        return f;
    }
    std::shared_ptr<VMQueuedAction> action(VMEntity* owner) {
        auto q = std::make_shared<VMQueuedAction>();
        q->UID = 91; q->Priority = 50; q->Args = {8, 9};
        q->Callee = owner; q->StackObject = owner; q->CodeOwner = owner->Object;
        owner->Thread->Queue.push_back(q);
        owner->Thread->ActiveQueueBlock = 0;
        return q;
    }
};
template<class T> static void values(std::ostream& out, const T& v) {
    out << '[';
    for (const auto& item : v) out << +item << ',';
    out << ']';
}
// Snapshot all person/motive words, attributes/object data, identities, relations,
// queues and frames involved in these fixtures, plus clock/RNG/globals/budget/NBRS.
// overrideReference expresses the sole permitted mutation; it never changes VM data.
static std::string state(Fixture& f, int overrideReference = -32769, bool personWords = true) {
    std::ostringstream out;
    out << f.context.RandomSeed << ':' << f.family.Budget << ':';
    auto c = f.context.Clock.get();
    out << c->Ticks << ':' << c->MinuteFractions << ':' << c->TicksPerMinute
        << ':' << c->Minutes << ':' << c->Hours << ':' << c->DayOfMonth
        << ':' << c->Month << ':' << c->Year << ':' << c->FirePercent;
    values(out, f.vm.GlobalState);
    out << f.neighbor->NeighbourID << ':' << f.neighbor->GUID << ':'
        << f.neighbor->Version << ':' << f.neighbor->PersonMode << ':' << f.neighbor->Name;
    values(out, f.neighbor->PersonData);
    for (const auto& r : f.neighbor->Relationships) { out << r.first; values(out, r.second); }
    for (auto e : f.vm.Entities) {
        out << e << ':' << e->ObjectID << ':' << e->Object->GUID << ':' << e->PersistID
            << ':' << e->Dead << ':' << e->Container << ':' << e->ContainerSlot;
        auto p = e->Position(); out << ':' << p.x << ':' << p.y << ':' << int(p.Level);
        values(out, e->ObjectData); values(out, e->AttributeData());
        for (const auto& r : e->MeToObject) { out << r.first; values(out, r.second); }
        if (auto a = dynamic_cast<VMAvatar*>(e)) {
            if (personWords) for (int i = 0; i <= 100; ++i)
                out << ':' << (a == f.actor && i == 59 && overrideReference != -32769
                    ? overrideReference : a->GetPersonData(VMPersonDataVariable(i)));
            for (int i = 0; i < 16; ++i) out << ':' << a->GetMotiveData(VMMotive(i));
        }
        out << ':' << e->Thread.get();
        if (!e->Thread) continue;
        auto t = e->Thread.get();
        out << ':' << int(t->ActiveQueueBlock) << ':' << t->Interrupt;
        values(out, t->TempRegisters);
        for (auto q : t->Queue) {
            out << q.get(); if (!q) continue;
            out << ':' << q->UID << ':' << q->Callee << ':' << q->StackObject
                << ':' << q->CodeOwner << ':' << q->ActionRoutine << ':' << q->CheckRoutine
                << ':' << q->Priority << ':' << int(q->Mode) << ':' << uint32_t(q->Flags)
                << ':' << uint32_t(q->Flags2) << ':' << q->InteractionNumber
                << ':' << q->Cancelled << ':' << q->UserQueued << ':' << q->Name;
            values(out, q->Args);
        }
        for (auto frame : t->Stack) {
            out << frame.get(); if (!frame) continue;
            out << ':' << frame->Thread << ':' << frame->Caller << ':' << frame->Callee
                << ':' << frame->StackObject << ':' << frame->StackObjectRawID
                << ':' << frame->CodeOwner << ':' << frame->Routine
                << ':' << frame->InstructionPointer << ':' << frame->ActionTree
                << ':' << int(frame->SpecialResult);
            values(out, frame->Args); values(out, frame->Locals);
        }
    }
    return out.str();
}
static void importContract(int16_t seed) {
    Fixture f(seed);
    f.action(f.actor)->Callee = f.linked;
    f.frame(f.actor)->Callee = f.linked;
    // This fixture represents a newly created destination before authored Init.
    // Also exercise an existing local reference and a version4 (80-word) NBRS.
    for (auto version : {uint32_t(0x4), uint32_t(0xa)}) {
        f.neighbor->Version = version;
        for (int16_t local : {int16_t(0), int16_t(1900 + seed)}) {
            f.actor->SetPersonData(VMPersonDataVariable::InteractionStatus, local);
            f.actor->SetPersonData(VMPersonDataVariable::Priority, 99);
            f.actor->SetPersonData(VMPersonDataVariable::Posture, 27);
            const auto raw = f.neighbor->PersonData;
            f.actor->SetPersonData(VMPersonDataVariable::Gender, 0);
            const auto untouched = state(f, -32769, false);
            std::vector<int16_t> oldPerson;
            for (int i = 0; i <= 100; ++i) oldPerson.push_back(f.actor->GetPersonData(VMPersonDataVariable(i)));
            const auto rng = f.context.RandomSeed;
            const auto ticks = f.context.Clock->Ticks;
            const auto cash = f.family.Budget;
            // vm=null deliberately isolates data import from visitor/greeting policy.
            check(Service::InheritNeighbor(f.actor, nullptr) == f.nid, "import neighbor identity");
            check(f.actor->GetPersonData(VMPersonDataVariable::InteractionStatus) == local,
                  "import retains destination social reference instead of foreign NBRS OID");
            check(f.actor->GetPersonData(VMPersonDataVariable::Posture) == 27 &&
                  f.actor->GetPersonData(VMPersonDataVariable::Priority) == 0,
                  "import retains existing posture/priority policies");
            const int limit = version == 0x4 ? 80 : 88;
            for (int i = 0; i < limit; ++i) {
                // IsHousemate is computed from permissions; RenderDisplayFlags
                // has its existing presentation setter policy. Neither is changed here.
                if (i == 0 || i == 21 || i == 33 || i == 59 || i == 74) continue;
                check(f.actor->GetPersonData(VMPersonDataVariable(i)) == raw[size_t(i)],
                      "import retains durable person field " + std::to_string(i));
            }
            for (int i : {21, 74})
                check(f.actor->GetPersonData(VMPersonDataVariable(i)) == oldPerson[size_t(i)],
                      "import retains computed/presentation field policy " + std::to_string(i));
            for (int i = limit; i <= 100; ++i)
                check(f.actor->GetPersonData(VMPersonDataVariable(i)) == oldPerson[size_t(i)],
                      "import preserves words absent from NBRS " + std::to_string(i));
            check(state(f, -32769, false) == untouched,
                  "import preserves motives, objects, queue, frames and unrelated state");
            check(raw == f.neighbor->PersonData && rng == f.context.RandomSeed &&
                  ticks == f.context.Clock->Ticks && cash == f.family.Budget,
                  "import leaves NBRS/RNG/clock/budget intact");
        }
    }
}
using Setup = std::function<void(Fixture&)>;
struct Case { const char* name; Setup setup; bool repair; };
static void migrationContract() {
    const std::vector<Case> cases = {
        {"foreign incompatible GiftToken", [](Fixture&){}, true},
        {"negative authored sentinel", [](Fixture& f){f.actor->SetPersonData(VMPersonDataVariable::InteractionStatus,-1);}, false},
        {"zero", [](Fixture& f){f.actor->SetPersonData(VMPersonDataVariable::InteractionStatus,0);}, false},
        {"author-defined social category", [](Fixture& f){f.linked->SetValue(VMStackObjectVariable::Category,f.socialCategory);}, false},
        {"other object type", [](Fixture& f){f.tokenDef.ObjectType=OBJDType::Normal;}, false},
        {"NBRS mismatch", [](Fixture& f){++f.neighbor->PersonData[59];}, false},
        {"missing object with matching NBRS", [](Fixture& f){
            (f.vm.*field(ObjectsField{})).erase(f.objectID);
            f.vm.Entities.erase(std::remove(f.vm.Entities.begin(), f.vm.Entities.end(), f.linked), f.vm.Entities.end());
            check(f.neighbor->PersonData[59] == f.actor->GetPersonData(VMPersonDataVariable::InteractionStatus)
                  && !f.vm.GetObjectById(f.objectID)
                  && std::none_of(f.vm.Entities.begin(), f.vm.Entities.end(), [&](VMEntity* e){return e->ObjectID == f.objectID;}), "missing fixture reaches absent object, not NBRS mismatch");
        }, false},
        {"short NBRS", [](Fixture& f){f.neighbor->PersonData.resize(59);}, false},
        {"missing neighbor", [](Fixture& f){f.neighbors.NeighbourByID.clear();}, false},
        {"dead avatar", [](Fixture& f){f.actor->Dead=true;}, false},
        {"dead linked object", [](Fixture& f){f.linked->Dead=true;}, false},
        {"avatar without thread", [](Fixture& f){f.actor->Thread.reset();}, false},
        {"no globals", [](Fixture& f){f.context.Globals=nullptr;}, false},
        {"no global resource", [](Fixture& f){f.globals.Resource=nullptr;}, false},
        {"missing category BCON", [](Fixture& f){f.globalResource.Iff=&f.objectIff;}, false},
        {"short category constant", [](Fixture& f){f.categories->Constants.resize(29);}, false},
        {"non-TS1 VM", [](Fixture& f){f.vm.TS1=false;}, false},
        {"actor attribute participant", [](Fixture& f){f.actor->SetAttribute(1,f.objectID);}, false},
        {"linked attribute participant", [](Fixture& f){f.linked->SetAttribute(1,f.actorID);}, false},
        {"actor target", [](Fixture& f){f.actor->SetValue(VMStackObjectVariable::TargetId,f.objectID);}, false},
        {"linked target", [](Fixture& f){f.linked->SetValue(VMStackObjectVariable::TargetId,f.actorID);}, false},
        {"unrelated queued action", [](Fixture& f){f.action(f.actor);}, false},
        {"actor queued callee", [](Fixture& f){f.action(f.actor)->Callee=f.linked;}, false},
        {"linked queued callee", [](Fixture& f){f.action(f.linked)->Callee=f.actor;}, false},
        {"linked queued stack object", [](Fixture& f){f.action(f.linked)->StackObject=f.actor;}, false},
        {"running ActionTree", [](Fixture& f){f.frame(f.actor)->ActionTree=true;}, false},
        {"actor frame caller", [](Fixture& f){f.frame(f.actor)->Caller=f.linked;}, false},
        {"actor frame callee", [](Fixture& f){f.frame(f.actor)->Callee=f.linked;}, false},
        {"actor frame stack object", [](Fixture& f){f.frame(f.actor)->StackObject=f.linked;}, false},
        {"actor frame raw stack ID", [](Fixture& f){f.frame(f.actor)->StackObjectRawID=f.objectID;}, false},
        {"linked frame caller", [](Fixture& f){f.frame(f.linked)->Caller=f.actor;}, false},
        {"linked frame callee", [](Fixture& f){f.frame(f.linked)->Callee=f.actor;}, false},
        {"linked frame raw stack ID", [](Fixture& f){f.frame(f.linked)->StackObjectRawID=f.actorID;}, false},
        {"idle unrelated frames do not mask contamination", [](Fixture& f){f.frame(f.actor);f.frame(f.linked);}, true},
        {"valid resumed social keeps participants/queue/frames", [](Fixture& f){
            f.linked->SetValue(VMStackObjectVariable::Category,f.socialCategory);
            f.linked->SetAttribute(1,f.actorID);
            f.action(f.actor)->Callee=f.linked;
            auto frame=f.frame(f.actor);frame->ActionTree=true;frame->Callee=f.linked;
            f.frame(f.linked)->StackObject=f.actor;
        }, false}
    };
    for (int16_t seed : {int16_t(1), int16_t(17)}) for (const auto& test : cases) {
        Fixture f(seed);
        test.setup(f);
        const auto expected = state(f, test.repair ? 0 : -32769);
        check(Service::RepairForeignObjectSocialReferences(&f.vm) == int(test.repair),
              std::string(test.name) + ": repaired count");
        check(state(f) == expected, std::string(test.name) + ": only allowed PD59 changed");
        check(Service::RepairForeignObjectSocialReferences(&f.vm) == 0 && state(f) == expected,
              std::string(test.name) + ": repeated repair is stable");
        std::cout << "PASS " << test.name << " seed=" << seed << '\n';
    }
    check(Service::RepairForeignObjectSocialReferences(nullptr) == 0, "null VM is conservative");
    Fixture f;
    auto context = f.vm.Context;
    f.vm.Context = nullptr;
    check(Service::RepairForeignObjectSocialReferences(&f.vm) == 0, "missing context is conservative");
    f.vm.Context = context;
}
int main() {
    try {
        VM::UseWorld(false);
        importContract(1);
        importContract(17);
        migrationContract();
        std::cout << "social reference checks=" << checks << " failures=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "social reference: " << error.what() << '\n';
        return 1;
    }
}

