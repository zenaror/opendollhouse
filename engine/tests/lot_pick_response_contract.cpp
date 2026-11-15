/* SPDX-License-Identifier: MPL-2.0 */
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/primitives/VMDialogPrivateStrings.h"
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
static void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(){try{VMContext context(nullptr);VM vm(&context,nullptr);VMThread thread(&context,nullptr,0);VMStackFrame frame;frame.Thread=&thread;
for(int type:{5,7,8,11,13})for(bool temp1:{false,true})for(int selected:{88,-1}){
 thread.TempRegisters.fill(112);auto wait=std::make_shared<VMDialogResult>();wait->Type=(VMDialogType)type;wait->Responded=true;wait->ResponseCode=selected>0?1:0;wait->ResponseText=std::to_string(selected);thread.BlockingState=wait;
 VMDialogOperand op;op.Type=(VMDialogType)type;op.Flags=temp1?VMDialogFlags::UseTemp1:static_cast<VMDialogFlags>(0);
 auto result=VMDialogPrivateStrings::ExecuteGeneric(&frame,&op,nullptr);
 require(result==VMPrimitiveExitCode::GOTO_TRUE,"signed lot response did not take numeric continuation");require(thread.TempRegisters[temp1?1:0]==selected,"signed lot was lost");require(thread.TempRegisters[temp1?0:1]==112,"unrequested temp changed");
 std::cout<<"PASS type="<<type<<" temp="<<(temp1?1:0)<<" lot="<<selected<<'\n';
}
 thread.TempRegisters.fill(112);auto old=std::make_shared<VMDialogResult>();old->Type=VMDialogType::TS1StudioTown;old->Responded=true;old->ResponseCode=1;old->ResponseText="";thread.BlockingState=old;VMDialogOperand op;op.Type=VMDialogType::TS1StudioTown;
 require(VMDialogPrivateStrings::ExecuteGeneric(&frame,&op,nullptr)==VMPrimitiveExitCode::GOTO_FALSE,"old empty cancellation no longer reproduces");require(thread.TempRegisters[0]==112,"old cancellation did not preserve stale destination112");std::cout<<"REPRODUCED old empty cancel: false branch, stale temp0=112\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
