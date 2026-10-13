#include "odh/lot_context.hpp"
#include <iostream>
namespace {
unsigned checks=0;
void check(bool value){++checks;if(!value)throw std::runtime_error("lot context assertion "+std::to_string(checks));}
template<class F>void rejects(F f){try{f();}catch(const odh::FormatError&){check(true);return;}check(false);}
odh::StringTable table(std::vector<odh::StringEntry> entries){return {odh::StringTableState::decoded,-3,std::move(entries),0};}
}
int main(){try{
    auto strings=table({{1,"1, residential",""},{1,"58, community",""},{2,"1, unknown foreign label",""}});
    const auto zones=odh::read_lot_zoning(strings);check(zones.size()==2&&zones.at(1)==0&&zones.at(58)==1);
    std::vector<std::int16_t> saved(32,0);saved[10]=99;saved[23]=64;saved[31]=-7;
    odh::LotContext lot(1,saved,zones);
    check(lot.read(10)==1&&saved[10]==99);check(lot.read(23)==64&&lot.read(31)==-7&&lot.read(2)==0);
    for(const auto n:{0,1,3,4,5,6,7,8,9,17,20,25,32,33,34,35,36,37,-1,38,32767}){
        check(!lot.read(static_cast<std::int16_t>(n)));check(!lot.write(static_cast<std::int16_t>(n),7));
    }
    check(lot.write(10,58)&&lot.read(10)==58);check(lot.write(2,-32768)&&lot.read(2)==-32768);
    const auto residential=lot.zone(1),community=lot.zone(58);check(residential&&residential->value==0&&!residential->fallback);
    check(community&&community->value==1&&!community->fallback);
    for(const auto n:{-32768,0,80,81,89,90,32767}){const auto result=lot.zone(static_cast<std::int16_t>(n));
        check(result&&result->input==n&&result->fallback&&result->value==(n>=81&&n<=89?2:1));}
    odh::LotContext unavailable(1,saved);check(!unavailable.zone(81));
    saved.resize(64,0);saved[35]=4;saved[37]=-2;saved[38]=7;
    odh::LotContext expanded(2,saved,zones);check(expanded.read(35)==4&&expanded.read(37)==-2&&!expanded.read(38));
    rejects([&]{odh::LotContext bad(0,saved);});rejects([&]{odh::LotContext bad(-1,saved);});
    rejects([&]{odh::LotContext bad(1,std::vector<std::int16_t>(38));});
    rejects([&]{odh::LotContext bad(1,saved,odh::LotZoning{{1,2}});});
    std::vector<odh::StringEntry> many;for(int n=0;n<4096;++n)many.push_back({1,std::to_string(n)+", residential",""});
    check(odh::read_lot_zoning(table(many)).size()==4096);
    many.push_back({1,"4096, residential",""});rejects([&]{odh::read_lot_zoning(table(many));});
    odh::LotZoning oversized;for(int n=0;n<4097;++n)oversized.emplace(static_cast<std::int16_t>(n),0);
    rejects([&]{odh::LotContext bad(1,saved,oversized);});
    for(const auto& s:{"1, unknown","1,community","32768, residential","1x, residential","1","1, residential, extra"," 1, residential"})
        rejects([&]{odh::read_lot_zoning(table({{1,s,""}}));});
    rejects([&]{odh::read_lot_zoning(table({{0,"1, residential",""},{1,"1, community",""}}));});
    rejects([&]{odh::read_lot_zoning(table({{2,"1, residential",""}}));});
    auto empty=table({});empty.state=odh::StringTableState::header_only;rejects([&]{odh::read_lot_zoning(empty);});
    const auto primitive=lot.primitive();odh::VmFrame frame{};odh::Instruction instruction{1,254,255,{28,99,0,0,0,0,0,0}};
    std::array<std::int16_t,20> temps{};temps[0]=1;
    const auto outcome=primitive(frame,instruction,temps,{nullptr,0});
    check(outcome.disposition==odh::PrimitiveDisposition::goto_true&&outcome.temp_write&&outcome.temp_write->value==0&&outcome.temp_write->index==0);
    check(temps[0]==1);instruction.operands[0]=27;check(primitive(frame,instruction,temps,{nullptr,0}).disposition==odh::PrimitiveDisposition::unsupported);
    instruction.operands[0]=28;check(unavailable.primitive()(frame,instruction,temps,{nullptr,0}).disposition==odh::PrimitiveDisposition::unsupported);
    // Real VM publication, with Scope6 expression followed by zoning output.
    odh::Bhav b{0x8002,{{2,1,255,{0,0,10,0,0,5,8,6}},{1,254,255,{28,0,0,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,0,0,0}};
    odh::VmMemory memory;memory.read=[&](const auto&,auto scope,auto index){return scope==6?lot.read(index):std::nullopt;};
    odh::Vm vm({{odh::routine_key(4096),b}},memory,{},primitive);vm.start(odh::routine_key(4096));
    check(vm.run(2)==odh::VmStatus::returned_true&&vm.temps()[0]==1);
    std::cout<<checks<<" lot context checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
