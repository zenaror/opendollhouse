#include "linux_text_codec.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){try{
    check(odh_host::legacy_text_to_utf8(std::string("Sof\xe1s",5),14)=="Sofás","Portuguese CP1252");
    check(odh_host::legacy_text_to_utf8(std::string("\x83\x7c\x81\x5b\x83\x59",6),15)=="ポーズ","Japanese CP932");
    check(odh_host::legacy_text_to_utf8(std::string("\xd4\xdd\xcd\xa3",4),17)=="暂停","Simplified Chinese CP936");
    check(odh_host::legacy_text_to_utf8(std::string("\xbc\xc8\xb0\xb1",4),18)=="暫停","Traditional Chinese CP950");
    check(odh_host::legacy_text_to_utf8(std::string("\xe0\xa1\xc1\xcb\xc2\xd8\xb4",7),19)=="เกมหยุด","Thai CP874");
    check(odh_host::legacy_text_to_utf8(std::string("\xc0\xcf\xbd\xc3\x20\xc1\xa4\xc1\xf6",9),20)=="일시 정지","Korean CP949");
    check(odh_host::legacy_text_to_utf8(std::string(1,'\xa3'),16)=="Ł","Polish CP1250");
    check(odh_host::legacy_text_to_utf8(std::string(1,'\xd7'),12)=="׳","Hebrew CP1255 lookup (localized game mapping remains unverified)");
    check(odh_host::legacy_text_to_utf8(std::string(1,'\xc0'),13)=="А","Russian CP1251");
    check(odh_host::legacy_text_to_utf8(std::string(1,'\xa3'),0)=="£","US alias and Western code page");
    check(odh_host::legacy_text_to_utf8(std::string("\x81",1),14)=="�","invalid byte is replaced instead of aborting display");
    bool rejected=false;try{(void)odh_host::legacy_text_to_utf8("abc",21);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"unknown language is rejected");
    rejected=false;try{(void)odh_host::legacy_text_to_utf8("abcd",14,3);}catch(const std::length_error&){rejected=true;}
    check(rejected,"output budget is enforced");
    std::cout<<"linux text codec: 13 checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"linux text codec: "<<error.what()<<'\n';return 1;}}
