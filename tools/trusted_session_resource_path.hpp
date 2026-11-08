// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "session_generic_resource_path.hpp"
#include <optional>
namespace odh_host {
struct TrustedSessionResourceRoots {
    std::filesystem::path game_root,installed_characters,player_characters;
};
// Only selected direct character catalog roots gain portable aliases. Saved
// strings grant no path authority; all other files retain game-root policy.
inline std::string trusted_session_source_key(const TrustedSessionResourceRoots& roots,const std::string& source){
    namespace fs=std::filesystem;
    if(source.empty()||source.find('\0')!=std::string::npos)throw odh::FormatError("session resource path is empty or contains NUL");
    const auto root=[&](const fs::path& input)->std::optional<fs::path>{
        if(input.empty())return {};
        std::error_code error;const auto status=fs::symlink_status(input,error);
        if(error==std::errc::no_such_file_or_directory||status.type()==fs::file_type::not_found)return {};
        if(error)throw odh::FormatError("selected character catalog status is unavailable");
        if(fs::is_symlink(status)||!fs::is_directory(status))throw odh::FormatError("selected character catalog is missing or a symlink");
        return fs::canonical(input);
    };
    const auto installed=root(roots.installed_characters),player=root(roots.player_characters);
    if(installed&&player&&*installed==*player)throw odh::FormatError("selected character catalog aliases are ambiguous");
    fs::path input(source);if(!input.is_absolute())validate_generic_session_source_key(source);const auto parent=input.parent_path().generic_string();
    const bool installed_alias=!input.is_absolute()&&parent=="UserData/Characters";
    const bool player_alias=!input.is_absolute()&&parent=="PlayerData/Characters";
    if(installed_alias||player_alias){
        const auto& selected=installed_alias?installed:player;
        if(!selected)throw odh::FormatError("saved character alias has no selected trusted catalog");
        validate_generic_session_source_key(source);input=*selected/input.filename();
    }else if(!input.is_absolute())input=roots.game_root/input;
    std::error_code file_error;const auto status=fs::symlink_status(input,file_error);
    if(file_error||fs::is_symlink(status)||!fs::is_regular_file(status))throw odh::FormatError("session resource is not a regular non-symlink file");
    const auto physical=fs::canonical(input);
    const auto character=[&](const std::optional<fs::path>& catalog,const char* logical)->std::optional<std::string>{
        if(!catalog||physical.parent_path()!=*catalog)return {};
        if(input.parent_path().lexically_normal()!=*catalog&&fs::canonical(input.parent_path())!=*catalog)throw odh::FormatError("character resource escaped selected catalog");
        return std::string(logical)+physical.filename().generic_string();
    };
    if(const auto key=character(player,"PlayerData/Characters/"))return *key;
    if(const auto key=character(installed,"UserData/Characters/"))return *key;
    // A different installed neighborhood's characters cannot acquire the
    // selected UserData alias through the generic resource-key normalizer.
    const auto container=physical.parent_path().parent_path().filename().string();
    if(physical.parent_path().filename()=="Characters"&&(container=="UserData"||(container.size()==9&&container.compare(0,8,"UserData")==0&&container[8]>='1'&&container[8]<='8')))
        throw odh::FormatError("character resource is outside the selected installed catalog");
    return generic_session_source_key(roots.game_root,physical.string());
}
} //namespace odh_host
