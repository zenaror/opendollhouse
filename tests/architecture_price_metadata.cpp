#include "odh/architecture_price_metadata.hpp"
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message) {
    try{action();}catch(const odh::FormatError&){return;}throw std::runtime_error(message);
}
odh::StringTable table(const std::string& value) {
    return {odh::StringTableState::decoded,-3,{{1,value,"original comment"}},0};
}
void put16(std::string& bytes,unsigned value) {
    bytes.push_back(static_cast<char>(value&255));bytes.push_back(static_cast<char>((value>>8)&255));
}
}
int main(){try{
    using namespace odh;
    check(architecture_price_from_strings(table("2147483647"),0)==std::numeric_limits<std::int32_t>::max(),"INT32_MAX");
    check(architecture_price_from_strings(table("-2147483648"),0)==std::numeric_limits<std::int32_t>::min(),"INT32_MIN");
    check(architecture_price_from_strings(table(" \t\r\n\f\v+000123\v\f\n\r\t "),0)==123,"ASCII whitespace/sign/leading zeros");
    check(architecture_price_from_strings(table("-0"),0)==0,"negative zero");
    for(const auto* bad:{"2147483648","-2147483649","9999999999999999999",""," ","+","-","--1","+-1","+ 1","1 2","1x","1.0","1,000","$12","12$","0x10","1e2"})
        rejects([&]{architecture_price_from_strings(table(bad),0);},"malformed or overflowing price accepted");
    rejects([&]{architecture_price_from_strings(table(std::string("1\0",2)),0);},"embedded NUL accepted");
    rejects([&]{architecture_price_from_strings(table(std::string("\xc2\xa0")+"1"),0);},"nonASCII whitespace accepted");
    rejects([&]{architecture_price_from_strings(table(std::string(65,'0')),0);},"text length cap ignored");
    check(architecture_price_from_strings(table(std::string(64,'0')),0)==0,"64-byte boundary");
    StringTable languages{StringTableState::decoded,-3,{{1,"10","US0"},{2,"20","UK0"},{1,"11","US1"}},0};
    check(architecture_price_from_strings(languages,0)==10 && architecture_price_from_strings(languages,0,0)==10,"default and alias US");
    check(architecture_price_from_strings(languages,0,2)==20,"selected group has own indexing");
    check(architecture_price_from_strings(languages,1,14)==11,"absent group selects complete English set");
    rejects([&]{architecture_price_from_strings(languages,1,2);},"present group missing index fell back to English");
    rejects([&]{architecture_price_from_strings(languages,2,14);},"fallback missing index invented price");
    languages.entries.push_back({14,"","empty localized price"});
    rejects([&]{architecture_price_from_strings(languages,0,14);},"empty present entry fell back");
    StringTable no_english{StringTableState::decoded,-3,{{2,"20",""}},0};
    rejects([&]{architecture_price_from_strings(no_english,0,14);},"missing English borrowed another group");
    auto alias=table("19");alias.entries[0].language=0;
    check(architecture_price_from_strings(alias,0,1)==19,"entry language0 aliases US");
    for(const auto state:{StringTableState::absent,StringTableState::header_only}) {
        auto undecoded=table("12");undecoded.state=state;
        rejects([&]{architecture_price_from_strings(undecoded,0);},"undecoded table accepted");
    }
    auto invalid=table("12");invalid.format.reset();
    rejects([&]{architecture_price_from_strings(invalid,0);},"missing decoded format accepted");
    invalid.format=1;rejects([&]{architecture_price_from_strings(invalid,0);},"unsupported format accepted");
    // Real parser integration: dynamic catalog name0/price1/description2, and
    // old-format English tables, without adding file I/O to this reader.
    for(const auto format:{std::uint16_t{65533},std::uint16_t{65535}}) {
        std::string bytes;put16(bytes,format);put16(bytes,3);
        for(const auto* value:{"catalog name","37","catalog description"}) {
            if(format==65533)bytes.push_back(1);
            bytes+=value;bytes.push_back(0);if(format==65533)bytes.push_back(0);
        }
        std::istringstream stream(bytes,std::ios::binary);
        const auto parsed=read_strings(View(stream,bytes.size()));
        check(architecture_price_from_strings(parsed,1,14)==37,"parsed STR catalog price index1");
        rejects([&]{architecture_price_from_strings(parsed,0);},"catalog name treated as price");
    }
    const auto original=languages;rejects([&]{architecture_price_from_strings(languages,0,14);},"empty price accepted");
    check(languages.state==original.state && languages.format==original.format && languages.remaining_bytes==original.remaining_bytes &&
        languages.entries.size()==original.entries.size(),"table metadata unchanged");
    for(std::size_t n=0;n<languages.entries.size();++n)
        check(languages.entries[n].language==original.entries[n].language && languages.entries[n].value==original.entries[n].value &&
            languages.entries[n].comment==original.entries[n].comment,"table strings unchanged");
    std::cout<<"architecture decimal metadata, language-set selection and strict bounds passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
