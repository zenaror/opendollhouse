// SPDX-License-Identifier: MPL-2.0
#include "simantics/utils/VMTS1BehaviorIdentity.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include <cstdio>
#include <stdexcept>
using FSO::Files::Formats::IFF::Chunks::BHAV;
using namespace FSO::SimAntics::Engine::Utils;
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static BHAV Read(uint16_t version,uint8_t args=4,uint8_t locals=0,uint8_t reserved=0){
    std::vector<uint8_t> wire(12,0);
    wire[0]=uint8_t(version);wire[1]=uint8_t(version>>8);wire[2]=1;
    wire[5]=args;wire[6]=locals;wire[7]=reserved;
    const uint8_t instruction[]={2,0,254,253,1,2,3,4,5,6,7,8};
    wire.insert(wire.end(),std::begin(instruction),std::end(instruction));
    BHAV tree;st::Reader input(wire,st::Endian::Little);tree.Read(nullptr,input);return tree;
}
static uint32_t OldLegacyHash(BHAV tree){tree.Args=0;tree.Locals=0;return TS1BehaviorSignature(tree);}
static void Legacy(uint16_t version){
    const auto tree=Read(version,2,2);
    Need(tree.SourceFormatVersion==version,"actual parser owns source provenance");
    const auto historical=OldLegacyHash(tree);
    Need(TS1BehaviorSignatureMatches(tree,historical,true),"old legacy signature remains readable");
    Need(TS1BehaviorSignatureMatches(tree,TS1BehaviorSignature(tree),false),"new canonical signature stays exact");
    Need(!TS1BehaviorSignatureMatches(tree,historical,false),"write validation never uses historical fallback");
    auto reject=[&](const BHAV& changed){Need(!TS1BehaviorSignatureMatches(changed,historical,true),"historical identity rejects changed live code or metadata");};
    auto changed=tree;changed.Type^=1;reject(changed);
    changed=tree;changed.Args^=1;reject(changed);
    changed=tree;changed.Locals^=1;reject(changed);
    changed=tree;changed.Instructions[0].Opcode^=1;reject(changed);
    changed=tree;changed.Instructions[0].TruePointer^=1;reject(changed);
    changed=tree;changed.Instructions[0].FalsePointer^=1;reject(changed);
    for(size_t i=0;i<8;++i){changed=tree;changed.Instructions[0].Operand[i]^=0x80;reject(changed);}
    if(version==0x8001){
        const auto reparsed=Read(version,3,3);
        Need(TS1BehaviorSignatureMatches(reparsed,historical,true),"old decoder never bound legacy declarations in its source file");
        Need(!TS1BehaviorSignatureMatches(reparsed,TS1BehaviorSignature(tree),false),"new canonical identity binds corrected counts");
    }
    changed=tree;st::Writer output(st::Endian::Little);changed.Write(nullptr,output);
    Need(changed.SourceFormatVersion==version,"writing never rewrites input provenance");
    auto wire=output.ToArray();st::Reader reread(wire,st::Endian::Little);changed.Read(nullptr,reread);
    Need(changed.SourceFormatVersion==0x8003,"reread replaces source provenance");
    Need(!TS1BehaviorSignatureMatches(changed,historical,true),"modern reread never inherits the old decoder");
}
static void Banks(){
    for(int count:{0,2,6}){
        auto tree=Read(0x8002,4,uint8_t(count));
        Need(TS1LogicalLocals(true,count,4)==size_t(count),"original logical locals are declared independently");
        Need(TS1LogicalLocals(false,count,4)==size_t(std::max(count,4)),"online adapter retains its existing allocation");
        for(size_t n=0;n<9;++n){
            Need(TS1SavedLocalsSizeValid(tree,n,true,24)==(n==size_t(count)||n==size_t(std::max(count,4))),"only declared or exact historical native shape is valid");
            Need(TS1SavedLocalsSizeValid(tree,n,false,24)==(n==size_t(std::max(count,4))),"online size validation stays exact");
        }
        std::vector<int16_t> padded(std::max(count,4),731);
        const auto before=padded;
        Need(!TS1ExpandHistoricalLocals(tree,padded,true,24)&&padded==before,"nonzero historical padding is never trimmed or reset");
    }
    auto legacy=Read(0x8001,2,2);
    std::vector<int16_t> empty;
    Need(!TS1ExpandHistoricalLocals(legacy,empty,false,24)&&empty.empty(),"canonical malformed short bank cannot borrow another frame's authentication");
    Need(!TS1SavedLocalsSizeValid(legacy,empty.size(),true,24),"missing canonical locals are invalid");
    Need(TS1ExpandHistoricalLocals(legacy,empty,true,24)&&empty==std::vector<int16_t>({0,0}),"authenticated old decoder expands only previously absent entries");
    std::vector<int16_t> wrong(1,91);
    Need(!TS1ExpandHistoricalLocals(legacy,wrong,true,24)&&wrong[0]==91,"arbitrary partial bank cannot be repaired");
    auto wide=Read(0x8002,4,2,1);
    Need(wide.Locals==2&&wide.PreviousDecoderLocals==258,"reserved byte is retained only as old decoder provenance");
    auto old=wide;old.Locals=258;const auto oldHash=TS1BehaviorSignature(old);
    Need(TS1BehaviorSignatureMatches(wide,oldHash,true),"exact old uint16 decoder identity is readable");
    old=wide;old.Args=3;Need(!TS1BehaviorSignatureMatches(old,oldHash,true),"changed live arguments cannot use reserved-byte fallback");
    old=wide;old.Locals=3;Need(!TS1BehaviorSignatureMatches(old,oldHash,true),"changed live locals cannot use reserved-byte fallback");
    for(size_t n:{257,258,259})Need(TS1SavedLocalsSizeValid(wide,n,true,24)==(n==258),"old wide bank admits only its exact historical size");
    std::vector<int16_t> tail(258,0);tail[257]=3131;
    Need(!TS1ExpandHistoricalLocals(wide,tail,true,24)&&tail.size()==258&&tail[257]==3131,"wide historical tail survives without truncation");
}
static void ActualPush(){
    using namespace FSO::SimAntics;
    for(bool original:{false,true})for(int locals:{0,2,6}){
        VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=original;
        Engine::VMThread thread(&context,nullptr,4);
        VMRoutine routine;routine.Arguments=4;routine.Locals=uint16_t(locals);routine.Instructions.resize(1);
        auto frame=std::make_shared<Engine::VMStackFrame>();frame->Routine=&routine;frame->Args={91,92,93,94};
        Need(thread.Push(frame),"actual native thread accepts prepared routine");
        Need(frame->Locals.size()==size_t(original?locals:std::max(locals,4)),"actual Push uses original separate banks while retaining the online adapter");
        Need(frame->Args==std::vector<int16_t>({91,92,93,94}),"local allocation never changes argument values");
    }
}
int main(){try{
    Legacy(0x8000);Legacy(0x8001);Banks();ActualPush();
    auto modern=Read(0x8002);
    Need(!TS1BehaviorSignatureMatches(modern,OldLegacyHash(modern),true),"modern arguments remain strict");
    BHAV synthetic;synthetic.Args=4;synthetic.Instructions=modern.Instructions;
    Need(!TS1BehaviorSignatureMatches(synthetic,OldLegacyHash(synthetic),true),"unknown provenance cannot authorize compatibility");
    for(uint8_t locals:{0,2,4,9,255})Need(Read(0x8001,4,locals).Locals==std::min<int>(locals,4),"wire8001 local declarations use unsigned cap4");
    modern.Type=3;modern.Flags=19;st::Writer out(st::Endian::Little);modern.Write(nullptr,out);
    auto wire=out.ToArray();st::Reader again(wire,st::Endian::Little);modern.Read(nullptr,again);
    Need(modern.SourceFormatVersion==0x8003,"actual modern parse");
    auto oldest=Read(0x8000);st::Writer oldOut(st::Endian::Little);oldest.Write(nullptr,oldOut);
    // A raw old-header reread must clear all metadata left by a modern asset.
    std::vector<uint8_t> raw(12,0);raw[1]=0x80;st::Reader reset(raw,st::Endian::Little);modern.Read(nullptr,reset);
    Need(modern.SourceFormatVersion==0x8000&&modern.Locals==0&&modern.Args==4&&modern.Type==0&&modern.Flags==0,"old reread clears stale modern metadata");
    std::puts("PASS original behavior identity and frame banks");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
