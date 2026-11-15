/* SPDX-License-Identifier: MPL-2.0 */
#include "linux_text_codec.hpp"
// Link the real strict codec through the same FindIconv target as the UI.
// This callable contract is not reported as executed without an Android runner.
extern "C" int odh_android_iconv_contract() {
    try {
        if(odh_host::legacy_text_to_utf8("\x80",14)!="\xE2\x82\xAC")return 1;
        if(odh_host::utf8_to_legacy_text("\xE2\x82\xAC",14)!="\x80")return 2;
        if(odh_host::utf8_to_legacy_text("\xE3\x81\x82",15)!="\x82\xA0")return 3;
        return 0;
    } catch(...) {return 4;}
}
