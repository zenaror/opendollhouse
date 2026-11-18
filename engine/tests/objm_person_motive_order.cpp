// SPDX-License-Identifier: MPL-2.0
#include "files/formats/iff/chunks/OBJMWriter.h"
#include "files/formats/iff/IffFieldEncode.h"
#include <stdexcept>
#include <iostream>
using namespace FSO::Files::Formats::IFF::Chunks;
static void Require(bool v) { if (!v) throw std::runtime_error("DVD current/previous motive bank order"); }
static void Prefix(IffFieldEncodeWriter& out) {
    out.WriteInt32(0); out.WriteInt32(0);
    for (int i=0;i<10;++i) out.WriteString("",i==9);
    out.WriteInt32(0);
    for (int i=0;i<3;++i) out.WriteString("",i==2);
    out.WriteInt32(0);
    for (int i=0;i<9;++i) out.WriteFloat(0);
}
static void SkipPrefix(IffFieldEncode& in) {
    in.ReadInt32(); in.ReadInt32();
    for (int i=0;i<10;++i) in.ReadString(i==9);
    Require(in.ReadInt32()==0);
    for (int i=0;i<3;++i) in.ReadString(i==2);
    in.ReadInt32(); for (int i=0;i<9;++i) in.ReadFloat();
}
int main() { try {
    // Explicit source-order fields, independent of WriteOBJMPerson. The DVD
    // serializes its live bank first, then its previous snapshot (16 each).
    for (uint32_t version : {0x45u,0x49u}) {
        IffFieldEncodeWriter literal; Prefix(literal);
        for(int i=0;i<16;++i) literal.WriteFloat(10.25f+i);
        for(int i=0;i<16;++i) literal.WriteFloat(-20.5f-i);
        for(int i=0;i<(version>0x45?256:80);++i) literal.WriteInt16(0);
        literal.WriteInt32(0);
        WriteOBJMInteraction({},literal); WriteOBJMInteraction({},literal);
        literal.WriteInt32(0); literal.WriteInt32(0); literal.WriteInt32(0);
        literal.Interrupt();
        st::Reader input(literal.Data); IffFieldEncode fields(&input);
        OBJMPerson decoded; decoded.Read(version,fields);
        for(int i=0;i<16;++i) {
            Require(decoded.MotiveData[i]==10.25f+i);
            Require(decoded.MotiveDataOld[i]==-20.5f-i);
        }
        IffFieldEncodeWriter output; WriteOBJMPerson(decoded,version,output); output.Interrupt();
        st::Reader bytes(output.Data); IffFieldEncode raw(&bytes); SkipPrefix(raw);
        for(int i=0;i<16;++i) Require(raw.ReadFloat()==10.25f+i);
        for(int i=0;i<16;++i) Require(raw.ReadFloat()==-20.5f-i);
    }
    std::cout<<"PASS DVD motive bank order in classic person records\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
