/* SPDX-License-Identifier: MPL-2.0 */
// Dialogs from different resources can share an illustration ID. The queued
// message must retain its own picture even after another dialog is raised.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMDialogHandler.h"
#include "simantics/model/VMDialogInfo.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/BMP.h"
#include "app/LotSession.h"
#include "app/LotScreen.h"
#include "app/UIStrings.h"
#include "app/front/SimDialogView.h"
#include <cstdio>
#include <stdexcept>

using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;

static void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static Chunks::STR* MakeResource(IffFile& file, uint8_t blue) {
    // One-pixel, uncompressed 24-bit BMP; no commercial game data.
    std::vector<uint8_t> bytes(58, 0);
    bytes[0]='B'; bytes[1]='M'; bytes[2]=58; bytes[10]=54;
    bytes[14]=40; bytes[18]=1; bytes[22]=1; bytes[26]=1;
    bytes[28]=24; bytes[34]=4; bytes[54]=blue;
    auto* bmp=new Chunks::BMP();
    bmp->ChunkID=5001;
    st::Reader reader(bytes);
    bmp->Read(&file, reader);
    bmp->ChunkProcessed=true;
    file.AddChunk(bmp);
    auto* strings=new Chunks::STR();
    strings->ChunkID=301; strings->ChunkProcessed=true;
    file.AddChunk(strings);
    return strings;
}

int main() {
    try {
        VMContext context(nullptr);
        VM vm(&context, nullptr);
        VMThread thread(&context, nullptr, 8);
        FSO::Content::GameObject owner;
        VMRoutine routine;
        VMStackFrame frame;
        frame.Thread=&thread; frame.CodeOwner=&owner; frame.Routine=&routine;
        std::vector<Model::VMDialogInfo> queue;
        vm.OnDialog=[&](Model::VMDialogInfo& info){queue.push_back(info);};
        IffFile first, second;
        auto* a=MakeResource(first, 23);
        auto* b=MakeResource(second, 71);
        VMDialogOperand operand;
        operand.Flags=static_cast<VMDialogFlags>(3 << 1);
        operand.IconNameStringID=1;
        VMDialogHandler::ShowDialog(&frame, &operand, a);
        VMDialogHandler::ShowDialog(&frame, &operand, b);
        Require(queue.size()==2, "dialog signal missing");
        Require(queue[0].IndexedIconBMP.size()==58 && queue[0].IndexedIconBMP[54]==23,
                "first dialog lost its scoped illustration");
        Require(queue[1].IndexedIconBMP.size()==58 && queue[1].IndexedIconBMP[54]==71,
                "second dialog used the wrong resource");
        operand.IconNameStringID=2;
        VMDialogHandler::ShowDialog(&frame, &operand, a);
        Require(queue.back().IndexedIconBMP.empty(), "missing illustration borrowed another image");
        operand.IconNameStringID=1;
        operand.Flags=static_cast<VMDialogFlags>(1 << 1);
        VMDialogHandler::ShowDialog(&frame, &operand, a);
        Require(queue.back().IndexedIconBMP.empty(), "no-icon dialog retained a bitmap");
        // Answer a pressed button with the keyboard, then release over an
        // identical next notice. That release must not dismiss both notices.
        simapp::LotSession session;
        simapp::PendingDialog notice;
        notice.HasYes=true; notice.Yes="OK"; notice.Message="Notice"; notice.IconMode=1;
        session.Dialogs={notice,notice};
        simapp::front::SimDialogView view;
        simapp::Canvas canvas;
        canvas.Resize(800,600);
        simapp::UIStrings strings;
        simapp::LotScreen lot(&session,&strings,&canvas,&canvas);
        Require(view.Draw(session,lot,canvas,canvas), "painted notice was not reported");
        // A queued pet choice can lose its source before drawing. It must not
        // be reported as a displayed modal merely because it is in the queue.
        simapp::PendingDialog invalidPet;
        invalidPet.Type=9;
        session.Dialogs={invalidPet};
        const auto beforeSuppressed=canvas.Px;
        Require(!view.Draw(session,lot,canvas,canvas), "suppressed pet choice reported as painted");
        Require(canvas.Px==beforeSuppressed, "suppressed pet choice changed the canvas");
        session.Dialogs.clear();
        Require(!view.Draw(session,lot,canvas,canvas), "empty next frame retained a painted modal");
        session.Dialogs={notice,notice};
        Require(view.Draw(session,lot,canvas,canvas), "normal notice did not resume painting");
        int x=0,y=0;
        Require(view.ButtonPos(0,x,y), "desktop dialog button missing");
        simapp::AppInput input;
        input.DesktopPointer=true; input.TouchX=x; input.TouchY=y;
        input.TouchDown=true; input.Touching=true;
        view.Update(session,input);
        Require(session.Dialogs.size()==2, "desktop dialog activated before release");
        input.TouchDown=false; input.Down=simapp::BTN_A;
        view.Update(session,input);
        Require(session.Dialogs.size()==1, "keyboard did not answer current dialog");
        view.Draw(session,lot,canvas,canvas);
        input.Down=0; input.Touching=false; input.TouchUp=true;
        view.Update(session,input);
        Require(session.Dialogs.size()==1, "held click leaked into the next dialog");
        std::puts("dialog art contract: scoped and queued illustration identity preserved");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
