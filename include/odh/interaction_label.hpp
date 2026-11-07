// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace odh {
// Presentation only: retain the exact TS1 bytes and the resource/entry that
// produced them. No paths from this metadata are opened on restore.
enum class InteractionLabelKind : std::uint8_t { tree_strings, script_string };
struct InteractionLabelOrigin {
    InteractionLabelKind kind=InteractionLabelKind::tree_strings;
    std::uint8_t language=1;
    std::string resource,asset;
    std::uint16_t table=0;
    std::uint32_t index=0; // zero-based TTAs or STR# entry.
};
struct InteractionLabel {std::string text;InteractionLabelOrigin origin;};
inline bool valid_interaction_label(const InteractionLabel& label){
    const auto& origin=label.origin;
    if(label.text.empty()||label.text.size()>2048||label.text.find('\0')!=std::string::npos||
       origin.kind>InteractionLabelKind::script_string||!origin.language||origin.language>20||
       origin.resource.empty()||origin.resource.size()>4096||origin.asset.size()>4096||
       origin.resource.find_first_of("\\:")!=std::string::npos||origin.resource.find('\0')!=std::string::npos||
       origin.asset.find('\0')!=std::string::npos)return false;
    std::string_view path=origin.resource;
    while(true){
        const auto slash=path.find('/');const auto part=path.substr(0,slash);
        if(part.empty()||part=="."||part=="..")return false;
        if(slash==path.npos)break;
        path.remove_prefix(slash+1);
    }
    return true;
}
}
