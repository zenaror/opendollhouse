// SPDX-License-Identifier: MPL-2.0
#include "community_lot_document.hpp"
#include <algorithm>

namespace odh_host {
namespace {
void validate(const CommunityLotIdentity& i){
    const auto zero=[](const auto& value){return std::all_of(value.begin(),value.end(),[](auto byte){return byte==0;});};
    if(i.neighborhood<1||i.neighborhood>8||i.mode<1||i.mode>7||i.mode==6||i.house<1||i.house>98||
       zero(i.neighborhood_sha256)||zero(i.lot_sha256))
        throw odh::FormatError("community lot document identity is invalid");
}
}
bool same_community_lot(const CommunityLotIdentity&a,const CommunityLotIdentity&b)noexcept{
    return a.neighborhood==b.neighborhood&&a.mode==b.mode&&a.house==b.house&&
        a.neighborhood_sha256==b.neighborhood_sha256&&a.lot_sha256==b.lot_sha256;
}
odh::SessionArchive encode_community_lot_document(const CommunityLotDocument&d){
    validate(d.identity);
    std::vector<std::uint8_t> identity{1,0,d.identity.neighborhood,d.identity.mode,
        std::uint8_t(d.identity.house),std::uint8_t(d.identity.house>>8)};
    identity.insert(identity.end(),d.identity.neighborhood_sha256.begin(),d.identity.neighborhood_sha256.end());
    identity.insert(identity.end(),d.identity.lot_sha256.begin(),d.identity.lot_sha256.end());
    odh::SessionArchive result{1,{{{{'C','L','I','D'}},true,std::move(identity)},
        {{{'C','A','R','C'}},true,odh::encode_architecture_snapshot(d.architecture)}}};
    if(d.objects)result.sections.push_back({{{'C','O','B','J'}},true,odh::encode_actorless_object_trade_state(*d.objects)});
    return result;
}
CommunityLotDocument decode_community_lot_document(const odh::SessionArchive&a){
    if(a.version!=1||(a.sections.size()!=2&&a.sections.size()!=3))throw odh::FormatError("community lot document section count/version invalid");
    const std::vector<std::uint8_t>* identity=nullptr,*architecture=nullptr,*objects=nullptr;
    for(const auto&s:a.sections){
        if(!s.required)throw odh::FormatError("community lot document section must be required");
        if(s.id==std::array<char,4>{'C','L','I','D'}){if(identity)throw odh::FormatError("duplicate community identity");identity=&s.data;}
        else if(s.id==std::array<char,4>{'C','A','R','C'}){if(architecture)throw odh::FormatError("duplicate community architecture");architecture=&s.data;}
        else if(s.id==std::array<char,4>{'C','O','B','J'}){if(objects)throw odh::FormatError("duplicate community objects");objects=&s.data;}
        else throw odh::FormatError("unknown community lot document section");
    }
    if(!identity||!architecture||identity->size()!=70||(*identity)[0]!=1||(*identity)[1]!=0)
        throw odh::FormatError("community lot document identity extent/version invalid");
    CommunityLotDocument d;d.identity.neighborhood=(*identity)[2];d.identity.mode=(*identity)[3];
    d.identity.house=std::uint16_t((*identity)[4])|(std::uint16_t((*identity)[5])<<8);
    std::copy_n(identity->begin()+6,32,d.identity.neighborhood_sha256.begin());
    std::copy_n(identity->begin()+38,32,d.identity.lot_sha256.begin());validate(d.identity);
    d.architecture=odh::decode_architecture_snapshot(*architecture);
    if(objects)d.objects=odh::decode_actorless_object_trade_state(*objects);
    return d;
}
}
