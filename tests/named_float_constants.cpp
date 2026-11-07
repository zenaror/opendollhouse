// SPDX-License-Identifier: MPL-2.0
#include "odh/named_float_constants.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void put(std::string& s,std::uint32_t value){for(unsigned i=0;i<4;++i)s.push_back(static_cast<char>(value>>(i*8)));}
void at(std::string& s,unsigned offset,std::uint32_t value){for(unsigned i=0;i<4;++i)s.at(offset+i)=static_cast<char>(value>>(i*8));}
void text(std::string& s,const std::string& value){s+=value;s.push_back(0);if(value.size()%2==0)s.push_back(0);}
std::string encode(const std::vector<odh::NamedFloatConstant>& records){
    std::string s;put(s,0);put(s,1);s+="SNCF";put(s,static_cast<unsigned>(records.size()));
    for(const auto& record:records){text(s,record.name);std::uint32_t bits;std::memcpy(&bits,&record.value,4);put(s,bits);text(s,record.description);}return s;
}
odh::NamedFloatConstants read(const std::string& s,odh::NamedFloatLimits limits={}){
    std::istringstream stream(s,std::ios::binary);return odh::read_named_float_constants(odh::View(stream,s.size()),limits);
}
void reject(const std::string& s,odh::NamedFloatLimits limits={}){bool failed=false;try{(void)read(s,limits);}catch(const odh::FormatError&){failed=true;}need(failed,"bad FCNS admitted");}
void cases(){
    const std::vector<odh::NamedFloatConstant> records{{"a",3,""},{"xy",-1.25f,"odd"},{" yard score multiplier ",.33f,"even"}};
    const auto bytes=encode(records);
    const auto parsed=read(bytes);
    need(parsed.records.size()==3,"owned record count");
    for(unsigned i=0;i<3;++i)need(parsed.records[i].name==records[i].name&&parsed.records[i].value==records[i].value&&parsed.records[i].description==records[i].description,
        "exact names/spaces/float bits/descriptions and per-string alignment");
    need(read(encode({})).records.empty(),"validated empty table differs from malformed table");
    for(unsigned position=0;position<bytes.size();++position)reject(bytes.substr(0,position));
    reject(bytes+std::string(1,'\0'));
    for(unsigned change=0;change<4;++change){auto bad=bytes;
        if(change==0)at(bad,0,1);
        if(change==1)at(bad,4,2);
        if(change==2)bad[8]='N';
        if(change==3)at(bad,12,0xffffffffu);
        reject(bad);
    }
    reject(encode({{"same",1,""},{"same",2,""}}));reject(encode({{"",1,""}}));
    for(float value:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        reject(encode({{"finite",value,""}}));
    auto pad=encode({{"ab",1,""}});pad[19]=1;reject(pad); // name NUL padding.
    pad=encode({{"ab",1,""}});pad.back()=1;reject(pad); // empty description padding.
    odh::NamedFloatLimits limits;limits.bytes=bytes.size()-1;reject(bytes,limits);
    limits={};limits.records=2;reject(bytes,limits);
    const auto exact=encode({{"abc",1,"defg"}});limits={};limits.text_bytes=6;reject(exact,limits);
    limits.text_bytes=7;need(read(exact,limits).records.size()==1,"inclusive cumulative string allocation budget");
}
}
int main(){try{cases();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
