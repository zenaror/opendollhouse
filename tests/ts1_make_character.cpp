// SPDX-License-Identifier: MPL-2.0
#include "odh/ts1_make_character.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>
#include <utility>
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F&& f,const char* why){bool rejected=false;try{f();}catch(const odh::FormatError&){rejected=true;}require(rejected,why);}
struct Fixture {
    std::vector<odh::WardrobeCatalogAsset> assets;
    std::vector<std::uint32_t> appearances,textures;
    void add(odh::WardrobeAssetKind kind,std::string name){const auto n=static_cast<std::uint32_t>(assets.size());odh::WardrobeDigest hash{};hash[0]=1;
        assets.push_back({kind,std::move(name),"GameData/Skins/test.far","member",hash,n});(kind==odh::WardrobeAssetKind::appearance?appearances:textures).push_back(n);}
    void apr(const std::string& n){add(odh::WardrobeAssetKind::appearance,n);}
    void tex(const std::string& n){add(odh::WardrobeAssetKind::texture,n);}
    void hands(const std::string& prefix){for(const auto* suffix:{"lo","ro","lp","rp","rc"})apr(prefix+suffix);}
    Fixture(){
        // Source insertion order deliberately differs from alphabetical order.
        apr("c009mc_z");apr("c001mc_a");apr("c002mc_medonly");apr("b009mcchd_z");apr("b001mcchd_a");
        apr("c001ma_a");apr("b001mafit_a");apr("c001fa_a");apr("b001fafat_a");
        apr("b009dog_z");apr("b001dog_a");apr("b001cat_a");
        hands("hu");hands("hm");hands("hf");
        for(const auto* skin:{"lgt","med"}){
            for(const auto* p:{"c009mc","c001mc","b009mcchd","b001mcchd","c001ma","b001mafit","c001fa","b001fafat"})tex(std::string(p)+skin+"_texture");
            tex(std::string("huao")+skin);
        }
        // Later lexical-first texture must NOT replace the earlier provider winner.
        tex("c009mclgt_aaaa");
        tex("c002mcmed_only");tex("b009dog_texture");tex("b001dog_texture");tex("b001cat_texture");
    }
    odh::Ts1CharacterCollections owner(){return {assets,appearances,textures};}
};
const std::string& str(const odh::Ts1MakeCharacterPlan& p,std::uint16_t i){for(const auto& s:p.character.body_strings)if(s.index==i)return s.bytes;throw std::runtime_error("expected generated BodySTR field missing");}
bool has(const odh::Ts1MakeCharacterPlan& p,std::uint16_t i){for(const auto& s:p.character.body_strings)if(s.index==i)return true;return false;}
odh::Ts1MakeCharacterInput human(){odh::Ts1MakeCharacterInput in;in.age=9;in.source_name="Authored Name";return in;}
void human_order_and_filter(){Fixture f;auto owner=f.owner();auto in=human();std::vector<std::uint64_t> bounds;
    auto p=owner.prepare(in,[&](auto n){bounds.push_back(n);return std::uint64_t{0};});
    require(bounds==std::vector<std::uint64_t>({2,2}),"missing textures filtered BEFORE exact head then body draws");
    require(str(p,2)=="c009mc_z,HEAD-HEAD=c009mclgt_texture"&&str(p,1)=="b009mcchd_z,BODY=b009mcchdlgt_texture","selected provider insertion order, not alphabetical export");
    require(str(p,0)=="child"&&str(p,13)=="9"&&str(p,12)=="male"&&str(p,14)=="lgt","source class/sex/skin fields");
    require(str(p,17)=="hulo,HAND=huaolgt"&&str(p,21)=="hulo,HAND=huaolgt"&&str(p,22)=="hurc,HAND=huaolgt","exact pinned C# hand override strings, including left-fist idle reuse; original afbd not claimed");
    require(p.template_guid==0x7fd96b54u&&!p.character.guid&&!p.character.source&&p.character.name==in.source_name,"host still owns identity/source admission; authored name retained");
    require(!p.person_data&&!p.reset_pet_outfit_index,"original schema0 bank remains absent, not88/256zero words; no pet effect");
    // The medium-only head was removed from the shared mc list by the first
    // light query. It must not reappear merely because a later skin could use it.
    bounds.clear();in.color=1;p=owner.prepare(in,[&](auto n){bounds.push_back(n);return n-1;});
    require(bounds==std::vector<std::uint64_t>({2,2})&&str(p,2)=="c001mc_a,HEAD-HEAD=c001mcmed_texture","source collection filtering survives next call without rescanning/resurrection");
    in.age=17;p=owner.prepare(in,[](auto){return std::uint64_t{0};});require(str(p,13)=="9","positive arbitrary child age maps to class9, not exact supplied17");
    in.age=18;p=owner.prepare(in,[](auto){return std::uint64_t{0};});require(str(p,13)=="27"&&str(p,0)=="adult"&&str(p,1).find("b001mafit_a,")==0,"adult ma collection and canonical27");
    in.gender=-1;p=owner.prepare(in,[](auto){return std::uint64_t{0};});require(str(p,12)=="female"&&str(p,1).find("b001fafat_a,")==0,"original gender nonzero, including negative, is female");
}
void pets(){Fixture f;auto owner=f.owner();auto in=human();in.avatar_type=1;in.pet_outfit_index=1;std::size_t draws=0;
    auto p=owner.prepare(in,[&](auto){++draws;return std::uint64_t{0};});
    require(draws==0&&p.template_guid==0x4a70df92u&&str(p,0)=="dog"&&str(p,13)=="27"&&str(p,1)=="b001dog_a,BODY=b001dog_texture","pet exact selector and source adult template, no human RNG");
    require(p.reset_pet_outfit_index&&!has(p,2)&&!has(p,17)&&!p.person_data,"pet selector reset explicit; original template head/hands not invented or cleared");
    in.pet_outfit_index=-1;p=owner.prepare(in,[&](auto n){++draws;require(n==2,"pet random bound is actual textured collection");return n-1;});
    require(draws==1&&p.reset_pet_outfit_index&&str(p,1)=="b001dog_a,BODY=b001dog_texture","source sentinel-1 draws once and resets selector");
    in.avatar_type=2;in.gender=-1;in.pet_outfit_index=0;p=owner.prepare(in,[](auto){throw std::runtime_error("authored pet index must not draw");return std::uint64_t{0};});
    require(p.template_guid==0x7bea0977u&&str(p,0)=="kat"&&str(p,12)=="catfemale"&&str(p,1)=="b001cat_a,BODY=b001cat_texture","literal kat rig vs cat clothing and source female type");
    in.pet_outfit_index=-2;rejects([&]{owner.prepare(in,{});},"unknown negative pet selector rejected");in.pet_outfit_index=2;rejects([&]{owner.prepare(in,{});},"pet selector cannot wrap modulo or fallback");
}
void clone_matrix(){Fixture f;auto owner=f.owner();auto in=human();in.age=-1;in.color=-123;in.gender=-123;in.source_name="Authenticated CTSS name";
    in.source_person_data.assign(256,777);in.source_body_strings={{0,"adult"},{1,"B009MCCHD_Z,BODY=B009MCCHDLGT_TEXTURE;ignored"},{2,"C009MC_Z,HEAD-HEAD=C009MCLGT_TEXTURE;ignored"},{12,"MaLe"},{13,"17trailing"},{14,"lgt"},{17,"do not copy caller hands"}};
    std::size_t draws=0;const auto rng=[&](auto){++draws;return std::uint64_t{0};};
    for(const auto age:{std::int16_t(-1),std::int16_t(-2),std::numeric_limits<std::int16_t>::min()}){in.age=age;const auto p=owner.prepare(in,rng);
        require(str(p,0)=="child"&&str(p,13)=="9"&&str(p,12)=="male"&&str(p,1)=="b009mcchd_z,BODY=b009mcchdlgt_texture"&&str(p,2)=="c009mc_z,HEAD-HEAD=c009mclgt_texture","all signed-negative ages clone receiver class and first ASCII pair match without operands skin/sex");
        require(!p.person_data&&!p.reset_pet_outfit_index&&p.character.name==in.source_name&&p.character.biography.empty(),"clone never copies live88bank, biography or petselector; host preserves template BIO");
        require(str(p,17)!="do not copy caller hands"&&str(p,30)=="ADDED"&&str(p,34)=="ADDED","clone secondary defaults follow explicit pinned base, not caller whole-table copy");
    }
    require(draws==0,"clone performs no appearance RNG, regardless of signed-negative operand value");
    for(const auto* bad:{"b009mcchd_z,b009mcchdlgt_texture","b009mcchd_z,HEAD-HEAD=b009mcchdlgt_texture"," b009mcchd_z,BODY=b009mcchdlgt_texture","b999mcchd_custom,BODY=b999mcchdlgt_custom"}){
        in.source_body_strings[1].bytes=bad;const auto p=owner.prepare(in,rng);
        require(str(p,1).empty()&&!has(p,17)&&!has(p,30),"unmatched pair maps source index-1 to EMPTY; preserve clause key/no trimming/no random replacement or invented hands");
    }
    in.avatar_type=2;in.source_body_strings={{0,"kat"},{1,"B001CAT_A,BODY=B001CAT_TEXTURE"},{2,"no_head_catalog"},{12,"catfemale"},{13,"27"},{14,"med"}};in.pet_outfit_index=-1;
    const auto cat=owner.prepare(in,rng);require(cat.template_guid==0x7bea0977u&&str(cat,0)=="kat"&&str(cat,1)=="b001cat_a,BODY=b001cat_texture"&&str(cat,2).empty()&&!cat.reset_pet_outfit_index&&!has(cat,17)&&draws==0,"negative pet clone uses receiver pairs and bypasses normal pet random/reset branch");
}
void clone_full_pair_collection(){Fixture f;auto owner=f.owner();auto normal=human();
    auto first=owner.prepare(normal,[](auto){return std::uint64_t{0};});
    require(str(first,2)=="c009mc_z,HEAD-HEAD=c009mclgt_texture","normal still selects first provider texture");
    auto clone=human();clone.age=-1;clone.source_body_strings={{0,"child"},{1,"b009mcchd_z,BODY=b009mcchdlgt_texture"},{2,"c009mc_z,HEAD-HEAD=c009mclgt_aaaa"},{12,"male"},{13,"9"},{14,"lgt"}};
    const auto no_rng=[](auto)->std::uint64_t{throw std::runtime_error("clone must never draw appearance RNG");};
    auto p=owner.prepare(clone,no_rng);
    require(str(p,2)=="c009mc_z,HEAD-HEAD=c009mclgt_aaaa","clone finds the admitted SECOND texture pair, not normal first-prefix replacement or empty");
    // The previous normal light call removed c002mc_medonly from its mutable
    // list; the complete admitted clone pair collection still contains it.
    clone.source_body_strings[1].bytes="b009mcchd_z,BODY=b009mcchdmed_texture";
    clone.source_body_strings[2].bytes="c002mc_medonly,HEAD-HEAD=c002mcmed_only";clone.source_body_strings[5].bytes="med";
    p=owner.prepare(clone,no_rng);require(str(p,2)=="c002mc_medonly,HEAD-HEAD=c002mcmed_only","clone finds original admitted medium pair after normal light filtering removed its appearance");
    normal.color=1;std::vector<std::uint64_t> bounds;owner.prepare(normal,[&](auto n){bounds.push_back(n);return std::uint64_t{0};});
    require(bounds==std::vector<std::uint64_t>({2,2}),"clone neither restores nor prunes normal mutable collections");
}
void bounds_and_unknown_authority(){Fixture f;auto owner=f.owner();auto in=human();std::size_t calls=0;
    for(const auto age:{std::int16_t(-1),std::int16_t(-2),std::numeric_limits<std::int16_t>::min()}){in.age=age;in.color=-12;
        in.source_person_data.assign(88,777);in.source_body_strings={{0,"child"},{12,"male"},{13,"9"}};
        rejects([&]{owner.prepare(in,[&](auto){++calls;return std::uint64_t{0};});},"clone requires actual authenticated receiver body/head fields, not a stand-in live bank");}
    require(calls==0,"negative branch never uses skin or human RNG");in=human();in.color=3;rejects([&]{owner.prepare(in,{});},"unknown source skin index rejected");in=human();in.language=0;rejects([&]{owner.prepare(in,{});},"language authority required");
    in=human();in.source_person_data.assign(87,123);require(!owner.prepare(in,[](auto){return std::uint64_t{0};}).person_data,"unused caller bank extent is not an invented88-word prerequisite");
    in.source_person_data.assign(257,123);rejects([&]{owner.prepare(in,{});},"unused receiver bank still has bounded original256-word backing");in=human();in.source_body_strings={{0,"adult"},{0,"child"}};rejects([&]{owner.prepare(in,{});},"duplicate source field rejected");
    in=human();rejects([&]{owner.prepare(in,[](auto n){return n;});},"RNG result out of bounds rejected without modulo retry");
    auto order=f.appearances;order.push_back(order.front());rejects([&]{odh::Ts1CharacterCollections x(f.assets,order,f.textures);},"duplicate/incomplete order coverage rejected");
    order=f.appearances;std::swap(order.front(),f.textures.front());rejects([&]{odh::Ts1CharacterCollections x(f.assets,order,f.textures);},"provider index kind cannot be substituted");
    Fixture nohands;for(auto& a:nohands.assets)if(a.name=="huaolgt")a.name="unrelatedtexture";auto missing=nohands.owner();rejects([&]{missing.prepare(human(),[](auto){return std::uint64_t{0};});},"selected hand resource missing is strict, not fallback to another skin");
    Fixture duplicate;duplicate.apr("C009MC_Z");rejects([&]{auto x=duplicate.owner();(void)x;},"case-folded logical duplicate is ambiguous, not arbitrary first");
}

// The host must not turn corrupted inputs or a broken RNG into normal false.
void typed_unavailable_boundary(){
    const auto unavailable=[](auto&& action){bool seen=false;try{action();}catch(const odh::Ts1CharacterUnavailable&){seen=true;}require(seen,"known outfit absence must carry the typed unavailable outcome");};
    const auto malformed=[](auto&& action){bool seen=false;try{action();}catch(const odh::Ts1CharacterUnavailable&){throw std::runtime_error("malformed input was converted to normal unavailable");}catch(const odh::FormatError&){seen=true;}require(seen,"malformed input must remain an explicit error");};
    std::size_t draws=0;const auto rng=[&](auto){++draws;return std::uint64_t{0};};
    Fixture empty;empty.assets.clear();empty.appearances.clear();empty.textures.clear();auto no_collection=empty.owner();
    unavailable([&]{no_collection.prepare(human(),rng);});
    empty.apr("c009mc_z");empty.apr("b009mcchd_z");empty.apr("b009dog_z");auto no_textures=empty.owner();
    unavailable([&]{no_textures.prepare(human(),rng);});auto dog=human();dog.avatar_type=1;
    unavailable([&]{no_textures.prepare(dog,rng);});require(draws==0,"unavailable normal candidates must not consume simulation RNG");
    Fixture complete;auto valid=complete.owner();malformed([&]{valid.prepare(human(),[](auto n){return n;});});
    auto bad=human();bad.source_body_strings={{0,"adult"},{0,"child"}};malformed([&]{valid.prepare(bad,rng);});
    dog.pet_outfit_index=-2;malformed([&]{no_collection.prepare(dog,rng);});
    require(draws==0,"invalid selector must be checked before missing collection classification or RNG");
    dog.pet_outfit_index=-2;malformed([&]{valid.prepare(dog,rng);});dog.pet_outfit_index=300;malformed([&]{valid.prepare(dog,rng);});
}

}
int main(){try{typed_unavailable_boundary();human_order_and_filter();pets();clone_matrix();clone_full_pair_collection();bounds_and_unknown_authority();std::cout<<"Make19 bounded appearance planner matrices passed; original schema0 and receiver clone matrices, with intermediate hand/default provenance\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
