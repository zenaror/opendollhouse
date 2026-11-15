/* SPDX-License-Identifier: MPL-2.0 */
#ifndef __3DS__
#include "app/ui/OriginalTextEncoding.h"
#include "files/formats/iff/chunks/STR.h"
#include "linux_text_codec.hpp"
#include "json.hpp"
namespace simapp { namespace original {
std::uint8_t TextLanguage(){
    auto language=std::uint8_t(FSO::Files::Formats::IFF::Chunks::STR::DefaultLangCode);
    return language?language:1;
}
std::string LegacyToUTF8(std::string_view text){return odh_host::legacy_text_to_utf8(text,TextLanguage());}
std::string LegacyToUTF8(std::string_view text,std::uint8_t sourceLanguage){return odh_host::legacy_text_to_utf8(text,sourceLanguage?sourceLanguage:1);}
std::string PersonalToUTF8(std::string_view text,std::uint8_t sourceLanguage,bool sourceUTF8){return sourceUTF8?std::string(text):LegacyToUTF8(text,sourceLanguage);}
std::string UTF8ToLegacy(std::string_view text){return odh_host::utf8_to_legacy_text(text,TextLanguage());}
std::string UIString(std::string_view text){
    const std::string owned(text);
    return odh_json_detail::valid_utf8(owned)?owned:LegacyToUTF8(text);
}
}}
#endif
