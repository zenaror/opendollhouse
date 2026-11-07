#pragma once

#include "odh/animation.hpp"
#include "odh/character_catalog.hpp"
#include "resource.hpp"
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
#include "linux_text_codec.hpp"
#endif
#include <iterator>
#include <map>
#include <utility>

namespace odh_host {
// NBRS.Name is an asset key, not a display name. Only the uniquely admitted
// person's own localized CTSS can provide UI text. No filename fallback.
class CharacterDisplayNames {
public:
    CharacterDisplayNames(std::filesystem::path installed,std::filesystem::path player,std::uint8_t language)
        :installed_(std::move(installed)),player_(std::move(player)),language_(language){}

    // Existing references remain valid until reset/destruction. Reaching the
    // budget declines new lookups rather than evicting referenced strings.
    const std::string& name(std::uint32_t guid){
        if(const auto found=names_.find(guid);found!=names_.end())return found->second;
        if(names_.size()>=512)return empty_;
        auto& result=names_.try_emplace(guid).first->second;
        if(!loaded_)load();
        const auto* candidate=catalog_?catalog_->unique(guid):nullptr;
        if(!candidate)return result;
        try{
            const auto directory=std::filesystem::path(candidate->source);
            const auto asset=std::filesystem::path(candidate->asset);
            if(asset.empty()||asset.has_parent_path()||asset.is_absolute()||
                (directory!=installed_root_&&directory!=player_root_)||!canonical_directory(directory))return result;
            const auto path=directory/asset;
            std::error_code error;
            if(!std::filesystem::is_regular_file(std::filesystem::symlink_status(path,error))||error)return result;
            if(std::filesystem::file_size(path)>odh::CharacterCatalogLimits{}.file_bytes)return result;
            with_iff_file(path.string(),[&](const odh::View& view){
                std::optional<std::uint16_t> strings_id;
                bool owner=false;
                odh::iff_chunks(view,[&](const odh::Chunk& chunk){
                    if(chunk.type!="OBJD"||chunk.id!=candidate->chunk_id)return;
                    if(owner)throw odh::FormatError("ambiguous character name owner");
                    owner=true;const auto object=odh::read_object_definition(chunk.payload);
                    if(object.guid!=guid||object.version!=138||object.object_type!=2)
                        throw odh::FormatError("character name owner changed");
                    const auto words=odh::read_object_definition_words(chunk.payload);
                    if(words.size()>41)strings_id=static_cast<std::uint16_t>(words[41]);
                });
                if(!strings_id)return;
                // Native CAS owns CTSS2000 in its private character directory.
                if(!*strings_id&&directory==player_root_)strings_id=2000;
                bool found=false;
                odh::iff_chunks(view,[&](const odh::Chunk& chunk){
                    if(chunk.type!="CTSS"||chunk.id!=*strings_id)return;
                    if(found)throw odh::FormatError("ambiguous character name table");
                    found=true;const auto table=odh::read_strings(chunk.payload);
                    const auto* entry=odh::string_in_language_or_english(table,language_,0);
                    if(!entry||entry->value.empty()||entry->value.size()>256)return;
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
                    result=legacy_text_to_utf8(entry->value,entry->language?entry->language:1,1024);
#else
                    result=entry->value;
#endif
                });
            });
        }catch(const std::exception&){result.clear();}
        return result;
    }
    // Call after changing character files/admission, e.g. creating a family.
    void reset(){names_.clear();catalog_.reset();loaded_=false;installed_root_.clear();player_root_.clear();}
private:
    static bool canonical_directory(const std::filesystem::path& path){
        std::error_code error;
        const auto status=std::filesystem::symlink_status(path,error);
        if(error||!std::filesystem::is_directory(status)||std::filesystem::is_symlink(status))return false;
        const auto canonical=std::filesystem::canonical(path,error);
        return !error&&canonical==path;
    }
    static std::filesystem::path admitted_directory(const std::filesystem::path& path){
        if(path.empty())return {};
        std::error_code error;
        const auto absolute=std::filesystem::absolute(path,error).lexically_normal();
        return !error&&canonical_directory(absolute)?absolute:std::filesystem::path{};
    }
    void load(){
        loaded_=true;
        try{
            installed_root_=admitted_directory(installed_);player_root_=admitted_directory(player_);
            std::vector<odh::DefinitionCandidate> definitions;
            for(const auto& directory:{installed_root_,player_root_}){
                if(directory.empty())continue;
                // The same directory supplied twice is one admission source.
                if(directory==player_root_&&directory==installed_root_&&!definitions.empty())continue;
                auto extra=odh::read_character_catalog(directory);
                definitions.insert(definitions.end(),std::make_move_iterator(extra.begin()),std::make_move_iterator(extra.end()));
            }
            catalog_.emplace(definitions);
        }catch(const std::exception&){catalog_.reset();}
    }
    std::filesystem::path installed_,player_,installed_root_,player_root_;
    std::uint8_t language_;
    bool loaded_=false;
    std::optional<odh::DefinitionIndex> catalog_;
    std::map<std::uint32_t,std::string> names_;
    const std::string empty_;
};
} // namespace odh_host
