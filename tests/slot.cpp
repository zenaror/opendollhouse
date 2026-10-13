#include "odh/formats.hpp"
#include <iostream>
#include <sstream>

namespace {
int checks=0;
void require(bool condition,const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
void put(std::string& b,std::size_t offset,std::uint32_t value,unsigned width=4) {
    for (unsigned i=0;i<width;++i) b.at(offset+i)=static_cast<char>((value>>(8*i))&255);
}
odh::SlotTable parse(const std::string& b,const odh::Limits& limits={}) {
    std::istringstream stream(b,std::ios::binary);
    return odh::read_slot(odh::View(stream,b.size()),limits);
}
void rejects(const std::string& b) {
    bool failed=false; try {parse(b);} catch (const odh::FormatError&) {failed=true;}
    require(failed,"invalid/truncated SLOT must fail");
}
std::string fixture(unsigned version) {
    const auto span=version<6?34:54+(version-6)*4;
    std::string b(16+span,'\0');
    put(b,0,0xAABBCCDD);put(b,4,version);b.replace(8,4,"TOLS");put(b,12,1);
    put(b,16,3,2);put(b,18,0x80000000);put(b,22,0x7FC12345);put(b,26,0x3F800000);
    put(b,30,0xFFFFFFFF);put(b,34,2);put(b,38,0x80000000);put(b,42,0xFEDCBA98);put(b,46,0xFFFFFFFF);
    if (version>=6) {put(b,50,0x80000000);put(b,54,32);put(b,58,16);put(b,62,100);put(b,66,0xFFFFFFFB);}
    if (version>=7) put(b,70,0x7F800000);
    if (version>=8) put(b,74,0);
    if (version>=9) put(b,78,0xFFFFFFFE);
    if (version>=10) put(b,82,8);
    return b;
}
}
int main() {
    try {
        for (unsigned v=4;v<=10;++v) {
            const auto b=fixture(v);const auto t=parse(b);const auto& r=t.records.at(0);
            require(t.version==v && t.reserved==0xAABBCCDD && t.remaining_bytes==0,"header and reserved field");
            require(r.type==3 && r.offset_bits[0]==0x80000000 && r.offset_bits[1]==0x7FC12345 &&
                    r.offset_bits[2]==0x3F800000,"float bits incl negative zero/NaN preserved");
            require(r.standing==-1 && r.sitting==2 && r.ground==(-2147483647-1) &&
                    r.flags==0xFEDCBA98 && r.snap_target_slot==-1,"signed values and flag bit pattern");
            require(r.proximity.has_value()==(v>=6) && r.gradient_bits.has_value()==(v>=7) &&
                    r.height.has_value()==(v>=8) && r.facing.has_value()==(v>=9) &&
                    r.resolution.has_value()==(v>=10),"version extensions retain absence");
            if (v>=6) require((*r.proximity)[0]==(-2147483647-1) && (*r.proximity)[1]==32 &&
                             r.max_size==100 && r.unknown_i10==-5,"proximity raw; unknown field preserved");
            if (v>=8) require(r.height==0,"height zero is not replaced by runtime default");
            if (v>=9) require(r.facing==-2,"signed facing");
            if (v>=10) require(r.resolution==8,"resolution");
            for (std::size_t n=0;n<b.size();++n) rejects(b.substr(0,n));
        }
        auto b=fixture(10);b+="tail";
        require(parse(b).remaining_bytes==4,"unknown tail reported");
        b=fixture(4);put(b,12,0);
        require(parse(b).records.empty() && parse(b).remaining_bytes==34,"empty table retains tail");
        b=fixture(10);b[8]='X';rejects(b);
        b=fixture(10);put(b,4,3);rejects(b);put(b,4,11);rejects(b);
        b=fixture(10);put(b,12,0xFFFFFFFF);rejects(b);
        odh::Limits limits;limits.slot_records=0;
        bool failed=false;try {parse(fixture(10),limits);}catch(const odh::FormatError&) {failed=true;}
        require(failed,"configured allocation budget");
        auto t=parse(fixture(10));
        auto a=t.records[0];a.type=1;
        auto first=t.records[0];first.standing=11;
        auto second=t.records[0];second.standing=22;
        t.records={a,first,a,second};
        require(odh::slot_of_type(t,3,0)->standing==11 && odh::slot_of_type(t,3,1)->standing==22,
                "type-group order differs from absolute indices");
        require(!odh::slot_of_type(t,3,2) && !odh::slot_of_type(t,99,0),"missing selection");
        std::cout<<checks<<" SLOT checks passed (synthetic fixtures only)\n";return 0;
    }catch(const std::exception& e){std::cerr<<"SLOT test failed: "<<e.what()<<'\n';return 1;}
}
