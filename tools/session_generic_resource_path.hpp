#pragma once
#include "odh/formats.hpp"
#include <filesystem>
#include <string>

namespace odh_host {
// Generic execution checkpoints store canonical game-root-relative resource
// identities. Live resource bindings may keep their native absolute paths.
inline std::string generic_session_source_key(const std::filesystem::path& game_root,const std::string& source) {
    const auto root=std::filesystem::canonical(game_root);
    const auto input=std::filesystem::path(source);
    const auto physical=std::filesystem::canonical(input.is_absolute()?input:root/input);
    const auto relative=physical.lexically_relative(root);
    if(relative.empty()||relative.is_absolute())
        throw odh::FormatError("generic session resource is not inside the selected game root");
    for(const auto& component:relative)if(component==".."||component==".")
        throw odh::FormatError("generic session resource is outside the portable game catalog");
    return relative.generic_string();
}
inline void validate_generic_session_source_key(const std::string& source) {
    const auto key=std::filesystem::path(source);
    if(source.empty()||key.is_absolute()||source.find('\\')!=std::string::npos||
       source.find(':')!=std::string::npos||key.generic_string()!=source)
        throw odh::FormatError("generic session resource key is not portable");
    for(const auto& component:key)if(component==".."||component==".")
        throw odh::FormatError("generic session resource key contains traversal");
}
}
