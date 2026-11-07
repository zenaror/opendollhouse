// SPDX-License-Identifier: MPL-2.0
#include "community_lot_store.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char*why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("invalid community store accepted");}
void write(const std::filesystem::path&p,const std::string&s){std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f<<s;if(!f)throw std::runtime_error("fixture write failed");}
}
int main(){try{
    const auto root=std::filesystem::temp_directory_path()/("odh-community-store-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}}cleanup{root};
    odh_host::CommunityLotStoreOptions o;o.game_root=root/"game";o.user_data=o.game_root/"UserData3";
    o.lot_path=o.user_data/"Houses/House56.iff";o.private_directory=root/"private/UserData3/lot4-56";o.neighborhood_number=3;o.mode=4;o.house=56;
    write(o.user_data/"Neighborhood.iff","identical neighborhood");write(o.lot_path,"original commercial lot");
    require(!odh_host::load_private_community_architecture(o)&&!odh_host::community_lot_checkpoint_head(o),"absence does not create private state");
    require(!std::filesystem::exists(o.private_directory),"query leaves storage absent");
    odh::ArchitectureSnapshot state;auto&p=state.prepared;p.width=p.height=2;p.buildable.assign(4,1);
    for(auto&level:p.levels){level.floors.assign(4,0);level.walls.resize(4);}
    o.expected_identity=odh_host::community_lot_identity(o);
    const odh_host::CommunityLotDocument document{*o.expected_identity,state};
    const auto blob=odh_host::publish_community_lot_blob(o,document);
    require(std::filesystem::is_regular_file(blob.checkpoint)&&!odh_host::load_private_community_lot(o)&&
        !std::filesystem::exists(o.private_directory/"architecture.odhl"),
        "immutable residential blob publication never creates a second authoritative legacy head");
    const auto same_blob=odh_host::publish_community_lot_blob(o,document);
    require(same_blob.checkpoint==blob.checkpoint&&same_blob.head==blob.head,
        "identical document publication reuses the same immutable content address");
    const auto reread_blob=odh_host::load_community_lot_blob(o,blob.head);
    require(odh::encode_architecture_snapshot(reread_blob.document.architecture)==odh::encode_architecture_snapshot(state),
        "exact overlay-provided blob head reopens its complete independent document");
    auto missing_blob=blob.head;missing_blob[0]^=1;
    rejects([&]{odh_host::load_community_lot_blob(o,missing_blob);});
    const auto path=odh_host::publish_private_community_architecture(o,{},state);
    const auto head=odh_host::community_lot_checkpoint_head(o);require(head&&std::filesystem::is_regular_file(path),"first edit durable checkpoint");
    auto reopened=odh_host::load_private_community_architecture(o);require(reopened&&odh::encode_architecture_snapshot(*reopened)==odh::encode_architecture_snapshot(state),"fresh read preserves exact architecture");
    auto next=state;next.prepared.levels[0].floors[0]=65534;
    const auto successor_blob=odh_host::publish_community_lot_blob(o,{*o.expected_identity,next});
    require(successor_blob.head!=blob.head&&odh_host::community_lot_checkpoint_head(o)==head,
        "publishing an uncommitted successor blob cannot move the legacy editor head");
    require(odh_host::load_community_lot_blob(o,blob.head).head==blob.head,
        "old immutable property blob remains readable after successor preparation");
    rejects([&]{odh_host::publish_private_community_architecture(o,{},next);});
    require(odh_host::community_lot_checkpoint_head(o)==head,"stale editor cannot overwrite first checkpoint");
    odh_host::publish_private_community_architecture(o,head,next);require(odh_host::community_lot_checkpoint_head(o)!=head,"current editor commits successor");
    auto other=o;other.expected_identity.reset();other.neighborhood_number=4;other.user_data=o.game_root/"UserData4";
    write(other.user_data/"Neighborhood.iff","identical neighborhood");rejects([&]{odh_host::load_private_community_architecture(other);});
    other=o;other.private_directory=o.game_root/"should-not-write";rejects([&]{odh_host::publish_private_community_architecture(other,{},state);});
    write(o.lot_path,"changed source");rejects([&]{odh_host::load_private_community_architecture(o);});
    rejects([&]{odh_host::load_community_lot_blob(o,blob.head);});
    rejects([&]{odh_host::publish_private_community_architecture(o,odh_host::community_lot_checkpoint_head(o),state);});
    write(o.lot_path,"original commercial lot");
    other=o;other.private_directory=root/"linked";std::filesystem::create_directory_symlink(o.private_directory,other.private_directory);
    rejects([&]{odh_host::load_private_community_architecture(other);});
    std::ifstream f(o.lot_path);std::string text;std::getline(f,text);require(text=="original commercial lot","only isolated fixture source restored; store never writes original lot");
    std::cout<<"community lot CAS/source/scope checks passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
