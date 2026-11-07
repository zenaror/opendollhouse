// SPDX-License-Identifier: MPL-2.0
#include "community_lot_document.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char*why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F f){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error("invalid community document accepted");}
}
int main(){try{
    odh_host::CommunityLotDocument d;d.identity.neighborhood=3;d.identity.mode=4;d.identity.house=56;
    d.identity.neighborhood_sha256.fill(1);d.identity.lot_sha256.fill(2);
    auto&p=d.architecture.prepared;p.width=p.height=2;p.buildable.assign(4,1);
    for(auto&level:p.levels){level.floors.assign(4,0);level.walls.resize(4);}
    const auto archive=odh_host::encode_community_lot_document(d);
    const auto encoded=odh::encode_session_archive(archive);
    const auto reopened=odh_host::decode_community_lot_document(odh::decode_session_archive(encoded));
    require(odh_host::same_community_lot(d.identity,reopened.identity),"community source and numbered neighborhood survive reopen");
    require(odh::encode_session_archive(odh_host::encode_community_lot_document(reopened))==encoded,"whole document roundtrip");
    require(archive.sections.size()==2&&archive.sections[0].id!=std::array<char,4>{'H','O','S','T'},"lot document is not a fake household session");
    auto other=d.identity;other.neighborhood=4;require(!odh_host::same_community_lot(d.identity,other),"identical commercial data in other neighborhood remains separate");
    other=d.identity;++other.house;require(!odh_host::same_community_lot(d.identity,other),"house identity participates");
    other=d.identity;++other.mode;require(!odh_host::same_community_lot(d.identity,other),"region identity participates");
    auto bad=archive;bad.sections.push_back(archive.sections.front());rejects([&]{odh_host::decode_community_lot_document(bad);});
    bad=archive;bad.sections[1]=archive.sections[0];rejects([&]{odh_host::decode_community_lot_document(bad);});
    bad=archive;bad.sections[0].required=false;rejects([&]{odh_host::decode_community_lot_document(bad);});
    bad=archive;bad.sections[0].data[2]=9;rejects([&]{odh_host::decode_community_lot_document(bad);});
    for(const auto mode:{0,6,8}){
        bad=archive;bad.sections[0].data[3]=static_cast<std::uint8_t>(mode);
        rejects([&]{odh_host::decode_community_lot_document(bad);});
    }
    bad=archive;bad.sections[0].data.pop_back();rejects([&]{odh_host::decode_community_lot_document(bad);});
    bad=archive;bad.sections[1].id={'H','O','S','T'};rejects([&]{odh_host::decode_community_lot_document(bad);});
    d.identity.lot_sha256.fill(0);rejects([&]{odh_host::encode_community_lot_document(d);});
    std::cout<<"community lot portable document checks passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
