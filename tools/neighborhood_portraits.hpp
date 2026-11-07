// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "linux_cas_preview.hpp"
#include "character_ui_images.hpp"
#include "resource.hpp"
#include "odh/character_catalog.hpp"
#include "odh/object_definition.hpp"
#include <iostream>
#include <map>

namespace odh_host {
// A launcher view owns this cache. The caller admits only GUIDs in its effective
// neighborhood roster; a character's OBJD chooses its own body STR#.
class NeighborhoodPortraits {
public:
    NeighborhoodPortraits(std::filesystem::path game,std::filesystem::path installed,
                          std::filesystem::path player):game_(std::move(game)),
        installed_(std::move(installed)),player_(std::move(player)){}
    void reset(std::filesystem::path game,std::filesystem::path installed,std::filesystem::path player){
        game_=std::move(game);installed_=std::move(installed);player_=std::move(player);
        previews_.reset();invalidate();
    }
    void invalidate(){catalog_.reset();bodies_.clear();disabled_=false;}
    const CharacterUiImages* images(std::uint32_t guid){
        (void)get(guid);
        const auto it=bodies_.find(guid);
        return it==bodies_.end()?nullptr:&it->second.images;
    }
    bool failed(std::uint32_t guid)const {
        const auto it=bodies_.find(guid);return it!=bodies_.end()&&it->second.failed;
    }
    const odh::BitmapImage* get(std::uint32_t guid,std::size_t face=0){
        if(disabled_)return nullptr;
        try{
            if(!catalog_){
                auto entries=odh::read_character_catalog(installed_);
                if(!player_.empty()&&std::filesystem::exists(player_)){
                    auto extra=odh::read_character_catalog(player_);
                    entries.insert(entries.end(),std::make_move_iterator(extra.begin()),std::make_move_iterator(extra.end()));
                }
                catalog_.emplace(entries);
            }
            auto found=bodies_.find(guid);
            if(found==bodies_.end()){
                if(bodies_.size()>=64)bodies_.erase(bodies_.begin());
                auto& entry=bodies_[guid]; // Cache absence as well as valid bodies.
                auto& body=entry.body;
                const auto* candidate=catalog_->unique(guid);
                if(!candidate)return nullptr;
                const auto directory=std::filesystem::path(candidate->source);
                if(directory!=installed_&&directory!=player_)return nullptr;
                const auto path=directory/candidate->asset;
                std::error_code error;
                if(!std::filesystem::is_regular_file(std::filesystem::symlink_status(path,error))||error)return nullptr;
                if(std::filesystem::canonical(path).parent_path()!=std::filesystem::canonical(directory))return nullptr;
                with_iff_file(path,[&](const odh::View& view){
                    std::optional<std::uint16_t> table;
                    odh::iff_chunks(view,[&](const odh::Chunk& chunk){
                        if(chunk.type!="OBJD"||chunk.id!=candidate->chunk_id)return;
                        const auto definition=odh::read_object_definition(chunk.payload);
                        if(table||definition.guid!=guid||definition.object_type!=2)
                            throw odh::FormatError("neighborhood portrait owner changed");
                        table=odh::read_objd_startup(chunk.payload).body_strings;
                    });
                    if(!table)return;
                    entry.images=read_character_ui_images(view);
                    odh::iff_chunks(view,[&](const odh::Chunk& chunk){
                        if(chunk.type!="STR#"||chunk.id!=*table)return;
                        if(body)throw odh::FormatError("ambiguous neighborhood portrait body table");
                        body=odh::parse_avatar_body_strings(odh::read_strings(chunk.payload));
                        if(!body->age)throw odh::FormatError("neighborhood portrait has no age");
                    });
                });
                found=bodies_.find(guid);
            }
            if(!found->second.images.faces.empty()){
                if(face>=found->second.images.faces.size())return nullptr;
                return &found->second.images.faces[face];
            }
            // Newly created characters may not have authored cached icons yet.
            // Generate once as a fallback; installed portraits remain authoritative.
            const auto& body=found->second.body;
            if(!body||(body->avatar_type!="adult"&&body->avatar_type!="child"))return nullptr;
            if(!previews_)previews_=std::make_unique<LinuxCasPreview>(game_);
            return previews_->portrait(*body,body->avatar_type=="child");
        }catch(const odh::FormatError& error){
            bodies_[guid]={};bodies_[guid].failed=true;
            // A broken catalog disables this view, not the other game screens.
            if(!catalog_)disabled_=true;
            std::clog<<"OpenDollhouse: portrait unavailable: "<<error.what()<<'\n';
        }catch(const std::filesystem::filesystem_error&){bodies_[guid]={};bodies_[guid].failed=true;}
        return nullptr;
    }
private:
    std::filesystem::path game_,installed_,player_;
    bool disabled_=false;
    std::optional<odh::DefinitionIndex> catalog_;
    struct Entry {std::optional<odh::AvatarBodyStrings> body;CharacterUiImages images;bool failed=false;};
    std::map<std::uint32_t,Entry> bodies_;
    std::unique_ptr<LinuxCasPreview> previews_;
};
}
