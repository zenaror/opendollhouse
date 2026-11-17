// SPDX-License-Identifier: MPL-2.0
// Synthetic opcode25 tests; SIMI persistence and real App are separate gates.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMSimanticsException.h"
#include "simantics/primitives/VMTS1Budget.h"
#include "simantics/engine/VMTS1Travel.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "files/formats/iff/IffFile.h"
#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::SimAntics::Engine::Scopes;
static int checks=0;
static void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);++checks;}
static VMPrimitiveExitCode run(VMContext& ctx,VMStackFrame& f,const std::array<uint8_t,8>& bytes){
 auto* p=ctx.Primitives.at(25).get();need(p&&p->GetHandler()&&bool(p->OperandModel),"registered opcode25");
 std::unique_ptr<VMPrimitiveOperand> operand(p->OperandModel());operand->Read(bytes.data());return p->GetHandler()->Execute(&f,operand.get());
}
int main(){try{
 VMContext ctx(nullptr);VM vm(&ctx,nullptr);vm.TS1=true;vm.GlobalState.resize(64,0);
 VMThread thread(&ctx,nullptr,8);VMStackFrame frame;frame.Thread=&thread;frame.Args={123};frame.Locals={-10};
 vm.SetGlobalValue(13,100);const auto cold=vm.TS1Budget;
 auto coldProbe=std::array<uint8_t,8>{0,0,5,0,1,0,3,0};
 need(run(ctx,frame,coldProbe)==VMPrimitiveExitCode::GOTO_TRUE && !vm.TS1Budget.Initialized &&
      vm.TS1Budget.Purse==cold.Purse && vm.TS1Budget.Days==cold.Days && vm.GetGlobalValue(13)==100,
      "cold affordability probe does not initialize or mutate financial state");
 VMTS1Budget::DaysType days{};days[0][3]=20;days[1][4]=99;
 VMTS1Budget::LoadState(&vm,590,days);
 auto op=std::array<uint8_t,8>{0,0,10,0,0,0,3,0};
 need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_TRUE&&VMTS1Budget::GetRawBudget(&vm)==580&&vm.TS1Budget.Days[0][3]==30&&vm.TS1Budget.Days[1][4]==99,"food debit exactly once");
 need(vm.GetGlobalValue(13)==580&&vm.GetGlobalValue(14)==0,"purse mirrors synchronized");
 op[2]=123;op[6]=4;op[4]=1;need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_TRUE&&vm.TS1Budget.Purse==580&&vm.TS1Budget.Days[0][4]==0,"affordability probe is read only");
 op[4]=0;need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_TRUE&&vm.TS1Budget.Purse==457&&vm.TS1Budget.Days[0][4]==123,"bill debit posts signed amount");
 op[2]=0xff;op[3]=0x7f;need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_FALSE&&vm.TS1Budget.Purse==457&&vm.TS1Budget.Days[0][4]==123,"insufficient debit does not post");
 op={1,0,0,0,2,0,1,0};need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_TRUE&&vm.TS1Budget.Purse==580&&vm.TS1Budget.Days[0][1]==-123,"parameter negation credits signed ledger");
 op={2,0,0,0,0,0,2,0};need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_TRUE&&vm.TS1Budget.Purse==590&&vm.TS1Budget.Days[0][2]==-10,"local negative credit");
 VMTS1Budget::SetBudget(&vm,-5);op={0,0,1,0,2,0,7,0};need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_TRUE&&vm.TS1Budget.Purse==-4&&vm.TS1Budget.Days[0][7]==-1,"credit accepted from negative purse");
 op[4]=0;need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_FALSE&&vm.TS1Budget.Purse==-4,"debit rejected from negative purse");
 VMTS1Budget::SetBudget(&vm,99999);op={0,0,0,0x80,2,0,6,0};need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_TRUE&&vm.TS1Budget.Purse==67231&&vm.TS1Budget.Days[0][6]==32768,"signed minimum inverted in wider domain");
 thread.TempXL[0]=0x10001;op={3,uint8_t(VMVariableScope::TempXL),0,0,0,0,5,0};need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_TRUE&&vm.TS1Budget.Days[0][5]==1,"32bit input narrows to source signed16");
 auto before=vm.TS1Budget;op={3,0xff,0,0,0,0,9,0};bool caught=false;try{run(ctx,frame,op);}catch(const VMSimanticsException&){caught=true;}need(caught&&vm.TS1Budget.Purse==before.Purse&&vm.TS1Budget.Days==before.Days,"invalid expense precedes scope lookup");
 VMTS1Budget::SetBudget(&vm,std::numeric_limits<int32_t>::max());vm.TS1Budget.Days[0][0]=std::numeric_limits<int32_t>::min();op={0,0,1,0,2,0,0,0};need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_TRUE&&vm.TS1Budget.Purse==std::numeric_limits<int32_t>::min()&&vm.TS1Budget.Days[0][0]==std::numeric_limits<int32_t>::max(),"purse and ledger wrap independently");
 VMTS1Budget::SetBudget(&vm,42);auto oldDays=vm.TS1Budget.Days;op={0,0,2,0,0,0,8,0};need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_TRUE&&vm.TS1Budget.Purse==40&&vm.TS1Budget.Days==oldDays,"expense8 commits purse without a ninth slot");
 op={0,0,0,0,0,0,3,0};need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_TRUE&&vm.TS1Budget.Purse==40&&vm.TS1Budget.Days==oldDays,"zero amount follows commit without numeric change");
 vm.SetGlobalValue(32,1);before=vm.TS1Budget;op={0,0,10,0,0,0,3,0};need(VMTS1Budget::GetBudget(&vm)==999999&&run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_TRUE&&vm.TS1Budget.Purse==before.Purse&&vm.TS1Budget.Days==before.Days,"Global32 getter and no-op commit");
 vm.SetGlobalValue(32,0);VMTS1Budget::SetBudget(&vm,1234567890);need(VMTS1Budget::GetRawBudget(&vm)==1234567890&&vm.TS1Budget.Days==before.Days,"nonposting synchronization retains full purse and ledger");
 VMTS1Budget::DaysType roll{};for(size_t i=0;i<roll.size();++i)roll[i].fill(int32_t(10+i));
 VMTS1Budget::LoadState(&vm,123,roll);VMTS1Budget::RolloverDay(&vm);
 need(vm.TS1Budget.Purse==123&&vm.TS1Budget.Days[0][3]==0&&vm.TS1Budget.Days[1][3]==10&&
      vm.TS1Budget.Days[2][3]==11&&vm.TS1Budget.Days[3][3]==11&&vm.TS1Budget.Days[4][3]==11&&vm.TS1Budget.Days[5][3]==11,
      "daily history uses source ascending copies");
 // Actual family activation is a nonposting purse transfer. The previously
 // active family must never receive the arriving family's balance.
 auto content=FSO::Content::Content::Get();auto oldProvider=std::move(content->Neighborhood);
 content->Neighborhood=std::make_unique<FSO::Content::TS1::TS1NeighborhoodProvider>(content);
 auto& provider=*content->Neighborhood;provider.MainResource=std::make_unique<FSO::Files::Formats::IFF::IffFile>();
 FSO::Files::Formats::IFF::Chunks::NBRS neighbors;provider.Neighbors=&neighbors;
 FSO::Files::Formats::IFF::Chunks::FAMI oldFamily,newFamily;oldFamily.ChunkID=1;oldFamily.Budget=270;newFamily.ChunkID=2;newFamily.Budget=810;
 provider.GameState.ActiveFamily=&oldFamily;auto ledgerBefore=vm.TS1Budget.Days;
 vm.TS1Budget.LegacyOwnerRestricted=true;vm.TS1Budget.LegacyOwnerID=2;
 VMTS1Budget::LoadState(&vm,590,{});VMTS1Budget::Commit(&vm,3,10);
 need(oldFamily.Budget==270 && VMTS1Budget::GetRawBudget(&vm)==580,"legacy pre-activation transaction does not charge a different active family");
 oldFamily.ChunkID=0;vm.TS1Budget.LegacyOwnerID=0;VMTS1Budget::Commit(&vm,3,10);
 need(oldFamily.Budget==270 && VMTS1Budget::GetRawBudget(&vm)==570,"legacy owner zero cannot authorize a family zero write");
 oldFamily.ChunkID=1;
 vm.TS1Budget.LegacyOwnerRestricted=false;vm.TS1Budget.Days=ledgerBefore;
 VMTS1Budget::SetBudget(&vm,270);
 FSO::SimAntics::Engine::Utils::VMTS1Travel::ActivateFamily(&vm,&newFamily);
 need(oldFamily.Budget==270&&newFamily.Budget==810&&VMTS1Budget::GetRawBudget(&vm)==810&&vm.TS1Budget.Days==ledgerBefore&&provider.GameState.ActiveFamily==&newFamily,"family arrival synchronizes new purse without charging or overwriting previous family");
 op={0,0,10,0,0,0,3,0};need(run(ctx,frame,op)==VMPrimitiveExitCode::GOTO_TRUE&&newFamily.Budget==800&&oldFamily.Budget==270&&vm.TS1Budget.Days[0][3]==ledgerBefore[0][3]+10,"real opcode commit updates active FAMI once");
 provider.GameState.ActiveFamily=nullptr;content->Neighborhood=std::move(oldProvider);
 std::printf("PASS %d TS1 budget checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
