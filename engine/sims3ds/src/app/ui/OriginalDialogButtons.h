/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace simapp { namespace original {
struct DialogButtonLabel {std::string Label;int Code=0;};
// Operand string IDs are not button-presence flags. Message/TextEntry have
// one response even when unused No/Cancel operands contain unrelated strings.
inline std::vector<DialogButtonLabel> DialogButtonLabels(uint8_t type,
        const std::string& yes,const std::string& no,const std::string& cancel,
        const std::string& ok,const std::string& defaultYes,const std::string& defaultNo,
        const std::string& defaultCancel){
    if(type==1||type==2){
        std::vector<DialogButtonLabel> result{{yes.empty()?defaultYes:yes,0},{no.empty()?defaultNo:no,1}};
        if(type==2)result.push_back({cancel.empty()?defaultCancel:cancel,2});
        return result;
    }
    return {{yes.empty()?ok:yes,0}};
}
}}
