// SPDX-License-Identifier: MPL-2.0
// Resource-free contract for observing exact VMThread::Pop inputs before frame
// and interaction retirement. This does not assert saved-game or gameplay behavior.
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/engine/VMThread.h"
#include "simantics/entities/VMGameObject.h"
#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;

static void need(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        FSO::Files::Formats::IFF::IffFile iff;
        FSO::Content::GameObjectResource resource(&iff, nullptr, nullptr, "frame-return-fixture", true);
        FSO::Files::Formats::IFF::Chunks::OBJD definition;
        definition.GUID = 0x76543210;
        FSO::Content::GameObject object;
        object.OBJ = &definition;
        object.GUID = definition.GUID;
        object.Resource = &resource;
        VMContext context(nullptr);
        VM vm(&context, nullptr);
        VMGameObject entity(&object, nullptr);
        entity.ObjectID = 7;
        VMThread thread(&context, &entity, 16);

        const auto observePop = [&](bool routing, VMPrimitiveExitCode expected, uint16_t uid) {
            thread.Stack.clear();
            thread.Queue.clear();
            thread.ActiveQueueBlock = 0;
            thread.LastStackExitCode = VMPrimitiveExitCode::GOTO_FALSE;
            auto action = std::make_shared<VMQueuedAction>();
            action->UID = uid;
            thread.Queue.push_back(action);

            std::shared_ptr<VMStackFrame> frame = routing
                ? std::static_pointer_cast<VMStackFrame>(std::make_shared<VMRoutingFrame>())
                : std::make_shared<VMStackFrame>();
            frame->Thread = &thread;
            frame->SpecialResult = VMSpecialResult::Interaction;
            const auto* const identity = frame.get();
            thread.Stack.push_back(frame);

            bool called = false;
            bool observedBeforeRetirement = false;
            vm.OnTraceFrameReturn = [&](const VMStackFrame* returned, int result) {
                called = true;
                const auto active = thread.ActiveAction();
                observedBeforeRetirement = returned == identity &&
                    result == static_cast<int>(expected) && thread.Stack.size() == 1 &&
                    thread.Stack.back().get() == identity && active == action &&
                    active->UID == uid && (dynamic_cast<const VMRoutingFrame*>(returned) != nullptr) == routing;
            };

            thread.Pop(expected);
            need(called, "Pop did not call the configured frame-return observer");
            need(observedBeforeRetirement,
                 "observer did not see the exact return, frame and active action before retirement");
            need(thread.Stack.empty() && thread.Queue.empty() && thread.ActiveQueueBlock == -1,
                 "Pop did not retire the frame and its interaction after observation");
            need(thread.LastStackExitCode == expected, "Pop changed the recorded primitive return result");
            vm.OnTraceFrameReturn = nullptr;
        };

        observePop(false, VMPrimitiveExitCode::RETURN_TRUE, 0x1234);
        observePop(false, VMPrimitiveExitCode::RETURN_FALSE, 0x1235);
        observePop(true, VMPrimitiveExitCode::RETURN_TRUE, 0x2234);
        observePop(true, VMPrimitiveExitCode::RETURN_FALSE, 0x2235);

        // With no observer, Pop retains the same normal cleanup and exact result.
        thread.Stack.clear();
        thread.Queue.clear();
        thread.ActiveQueueBlock = 0;
        auto action = std::make_shared<VMQueuedAction>();
        action->UID = 0x3234;
        thread.Queue.push_back(action);
        auto frame = std::make_shared<VMStackFrame>();
        frame->Thread = &thread;
        frame->SpecialResult = VMSpecialResult::Interaction;
        thread.Stack.push_back(frame);
        need(!vm.OnTraceFrameReturn, "observer should be unset for the baseline case");
        thread.Pop(VMPrimitiveExitCode::RETURN_FALSE);
        need(thread.Stack.empty() && thread.Queue.empty() && thread.ActiveQueueBlock == -1 &&
             thread.LastStackExitCode == VMPrimitiveExitCode::RETURN_FALSE,
             "Pop behavior changed with the observer unset");

        std::puts("frame return observer contract: PASS (5 cases)");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "frame return observer contract: FAIL: %s\n", error.what());
        return 1;
    }
}
