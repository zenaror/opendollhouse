#include "odh/lot_locations.hpp"
#include <iostream>
#include <sstream>
namespace {
int checks=0;
void require(bool condition) { ++checks; if(!condition) throw std::runtime_error("check failed"); }
std::vector<odh::LotLocation> read(const std::vector<std::string>& rows,const odh::LotLocationLimits& limits={},unsigned lang=1, const std::string& tail="") {
    std::string data="\xfd\xff";
    data+=static_cast<char>(rows.size());data+=static_cast<char>(rows.size()>>8);
    for(const auto& row:rows) { data+='\1';data+=row;data+='\0';data+='\0'; }
    data+=tail;
    std::istringstream stream(data);const auto decoded=odh::read_lot_locations(odh::View(stream,data.size()),static_cast<std::uint8_t>(lang),limits);
    require(decoded.remaining_bytes==tail.size());return decoded.lots;
}
template<class F> void rejects(F f) { bool rejected=false;try{f();}catch(const odh::FormatError&){rejected=true;}require(rejected); }
}
int main() {
    try {
        for(const std::string raw : {std::string{},std::string("\xfd\xff",2),std::string("\xfe\xff\0\0",4),std::string("\xfd\xff\1\0\1" "7,1,2",10)}) {
            rejects([&]{std::istringstream stream(raw);odh::read_lot_locations(odh::View(stream,raw.size()));});
        }
        auto rows=read({" \t7,\t-32767,32767\t ","99,0,0","8,0,1"});
        require(rows.size()==2&&rows[0].house==7&&rows[0].x==-32767&&rows[0].y==32767&&rows[1].house==8);
        for(const auto& line:{"7,1","7,1,2,3","7,,2","7,1x,2","+7,1,2","7,1,2\n","0,1,2","32768,1,2","100,1,2","7,-32768,0","7,0,32768","999999999999999999999,1,2"})
            rejects([&]{read({line});});
        rejects([&]{read({"7,1,2","7,3,4"});});
        rejects([&]{read({"99,1,2","99,3,4"});});
        odh::LotLocationLimits limits;limits.rows=1;rejects([&]{read({"7,1,2","8,1,2"},limits);});
        limits={};limits.row_bytes=4;rejects([&]{read({"7,1,2"},limits);});
        limits={};limits.max_house=6;rejects([&]{read({"7,1,2"},limits);});
        limits={};limits.coordinate_magnitude=1;rejects([&]{read({"7,1,2"},limits);});
        rejects([&]{read({"7,1,2"},{},2);});
        require(read({"7,1,2"},{},1,"tail").size()==1);
        limits={};limits.payload_bytes=1;rejects([&]{read({"7,1,2"},limits);});
        for(unsigned mode=1;mode<=7;++mode) require(odh::neighborhood_mode_supported(mode)==(mode!=6));
        rejects([]{odh::neighborhood_mode_supported(0);});rejects([]{odh::neighborhood_mode_supported(8);});
        std::cout<<"LotLocations: "<<checks<<" checks passed (synthetic)\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
