#include "odh/wardrobe.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>
namespace {
int checks=0;
void require(bool b,const char* text){++checks;if(!b)throw std::runtime_error(text);}
template<class F>void rejects(F f,const char* text){bool caught=false;try{f();}catch(const odh::FormatError&){caught=true;}require(caught,text);}
odh::OriginalWardrobeSource source(const std::string& gender="male",const std::string& age="27",const std::string& body="b001mafit_suit",const std::string& skin="lgt"){
 odh::OriginalWardrobeSource s;s.body_str_id=200;s.strings.resize(30);s.strings[1]=body+",BODY=original";s.strings[12]=gender;s.strings[13]=age;s.strings[14]=skin;return s;
}
odh::WardrobeCatalogAsset asset(odh::WardrobeAssetKind kind,const std::string& name,std::uint64_t order){
 odh::WardrobeCatalogAsset result;result.kind=kind;result.name=name;result.source="ExpansionShared/SkinsBuy/container"+std::to_string(order)+(kind==odh::WardrobeAssetKind::appearance?".cmx":".bmp");result.fingerprint.fill(static_cast<std::uint8_t>(order+1));result.producer_ordinal=order;return result;
}
std::vector<odh::WardrobeCatalogAsset> catalog(const std::string& code,const std::string& skin,bool child=false){
 std::vector<odh::WardrobeCatalogAsset> values;std::uint64_t ordinal=0;for(const auto& prefix:std::vector<std::string>{"f100","s100","l100","w100","h533"}){
  if(child&&(prefix=="f100"||prefix=="h533"))continue;
  values.push_back(asset(odh::WardrobeAssetKind::appearance,prefix+code+"_original",ordinal++));values.push_back(asset(odh::WardrobeAssetKind::texture,prefix+code+skin+"_original",ordinal++));
 }return values;
}
void matrix(){
 for(const auto& sex:std::vector<std::pair<std::string,std::string>>{{"male","m"},{"female","f"}})
 for(const auto& type:{"fit","fat","skn"})for(const auto& tone:{"lgt","med","drk"}){
  auto s=source(sex.first,"27","b001"+sex.second+"a"+type+"_suit",tone);const auto before=s.strings;const auto values=catalog(sex.second+"a"+type,tone);const auto p=odh::prepare_original_wardrobe_upgrade(s,values);
  require(p.rule_profile==1&&p.seed[3]==std::nullopt&&p.strings[3]==std::optional<std::string>{"w100"+sex.second+"a"+type+"_original,BODY=w100"+sex.second+"a"+type+tone+"_original"},"adult originaltype/sex/skin matrix uses actual catalog logical members");
  require(std::all_of(p.chosen.begin(),p.chosen.end(),[](const auto& c){return c.has_value();})&&s.strings==before,"all five slots produced atomically without altering raw30 source");odh::validate_original_wardrobe_source(p,s);
 }
 for(const auto& sex:std::vector<std::pair<std::string,std::string>>{{"male","m"},{"female","f"}}){const auto s=source(sex.first,"9","b001"+sex.second+"cchd_outfit","med");const auto p=odh::prepare_original_wardrobe_upgrade(s,catalog(sex.second+"cchd","med",true));
  require(p.strings[0]==std::optional<std::string>{"ADDED"}&&p.strings[4]==std::optional<std::string>{"ADDED"}&&!p.chosen[0]&&!p.chosen[4]&&p.strings[3]->find("w100"+sex.second+"cchd_")==0,"child gets sourceADDEDformal/highfashion and sex-specificcchd winter, not unisexuchd");
  require(p.chosen[1]&&p.chosen[2]&&p.chosen[3],"child source grouped swim/sleep/winter resources all owned");
 }
 auto flexible=catalog("mafit","lgt");for(auto& a:flexible){const auto pos=a.name.find("h533");if(pos==0)a.name.replace(0,4,"h599");}require(odh::prepare_original_wardrobe_upgrade(source(),flexible).strings[4]->find("h599mafit_")==0,"highfashion uses original h5 plus skip2 query rather than hardcoded533");
 const auto unexpected=source("male","27","b001maxxx_custom","unknown");const auto fallback=odh::prepare_original_wardrobe_upgrade(unexpected,catalog("mafit","lgt"));require(fallback.strings[3]->find("w100mafit_original,BODY=w100mafitlgt_")==0,"only original-proven bodytype/skin fit/lgt fallbacks apply");
 auto custom=source();custom.strings.resize(35);for(unsigned n=30;n<35;++n)custom.strings[n]="custom"+std::to_string(n);const auto same=odh::prepare_original_wardrobe_upgrade(custom,{});require(same.seed==same.strings&&std::none_of(same.chosen.begin(),same.chosen.end(),[](const auto& c){return c.has_value();}),"fully custom wardrobe remains unchanged without catalog dependency");
 auto partial=custom;partial.strings[30].clear();auto values=catalog("mafit","lgt");const auto grouped=odh::prepare_original_wardrobe_upgrade(partial,values);require(grouped.strings.at(1)!=std::optional<std::string>{partial.strings.at(31)}&&grouped.strings.at(2)!=std::optional<std::string>{partial.strings.at(32)}&&grouped.strings.at(3)==std::optional<std::string>{custom.strings.at(33)}&&grouped.strings.at(4)==std::optional<std::string>{custom.strings.at(34)},"formal trigger authorizes original grouped overwrite of partialswim/sleep, preserves other groups");
 auto animal=source("dogmale","3","short");const auto dog=odh::prepare_original_wardrobe_upgrade(animal,{});require(std::all_of(dog.strings.begin(),dog.strings.end(),[](const auto& s){return s&&s->empty();})&&std::none_of(dog.chosen.begin(),dog.chosen.end(),[](const auto& c){return c.has_value();}),"nonhuman gets proven loaderpadding but no invented human outfit/catalog lookup");
 auto bad=source();bad.body_str_id=0;rejects([&]{odh::prepare_original_wardrobe_upgrade(bad,values);},"absent BodySTR identity refused");bad=source();bad.strings.resize(257);rejects([&]{odh::prepare_original_wardrobe_upgrade(bad,values);},"bounded source entry count");bad=source();bad.strings[1]=std::string("a\0b",3);rejects([&]{odh::prepare_original_wardrobe_upgrade(bad,values);},"embeddedNUL source refused");
 auto immutable=odh::prepare_original_wardrobe_upgrade(source(),values);auto changed=source();changed.strings[14]="med";rejects([&]{odh::validate_original_wardrobe_source(immutable,changed);},"raw source metadata cannot silently change");auto p=immutable;p.rule_profile=2;rejects([&]{odh::validate_original_wardrobe_projection(p);},"unadmitted ruleprofile refused");p=immutable;p.body_str_id=0;rejects([&]{odh::validate_original_wardrobe_projection(p);},"stored BodySTR identity required");p=immutable;p.seed[0]="";rejects([&]{odh::validate_original_wardrobe_source(p,source());},"rawabsence and explicit empty remain distinct");p=immutable;p.strings[3]="daywear";rejects([&]{odh::validate_original_wardrobe_projection(p);},"generated winter cannot become daywear/None without producer authority");p=immutable;p.chosen[3]->appearance.kind=odh::WardrobeAssetKind::texture;rejects([&]{odh::validate_original_wardrobe_projection(p);},"selected resource kinds exact");
 values=catalog("mafit","lgt");values[0].name.assign(257,'x');rejects([&]{odh::prepare_original_wardrobe_upgrade(source(),values);},"logical member length bounds numeric match proof");
 values=catalog("mafit","lgt");values.pop_back();rejects([&]{odh::prepare_original_wardrobe_upgrade(source(),values);},"missing selectedresource refuses whole preparation");
 values=catalog("mafit","lgt");values[0].source="../outside.cmx";rejects([&]{odh::prepare_original_wardrobe_upgrade(source(),values);},"source traversal not admitted");values=catalog("mafit","lgt");values[0].asset="folder\\..\\escape";rejects([&]{odh::prepare_original_wardrobe_upgrade(source(),values);},"FAR backslash traversal not admitted");values=catalog("mafit","lgt");values[0].name+=".cmx";rejects([&]{odh::prepare_original_wardrobe_upgrade(source(),values);},"logical member names distinct from containing filenames");values=catalog("mafit","lgt");values[2].producer_ordinal=values[0].producer_ordinal;rejects([&]{odh::prepare_original_wardrobe_upgrade(source(),values);},"actual per-kind provider order required");
}
void numeric_order_and_cold(){
 auto s=source();s.strings.resize(35);for(unsigned n=30;n<35;++n)s.strings[n]="custom";s.strings[33].clear();
 for(const auto& item:std::vector<std::pair<unsigned,bool>>{{20,true},{19,false},{18,false}}){const auto count=item.first;const auto prefix=std::string("w100mafit_");const auto a=prefix+std::string(count-prefix.size(),'a'),b=prefix+std::string(count-prefix.size(),'b');std::vector<odh::WardrobeCatalogAsset> values={asset(odh::WardrobeAssetKind::appearance,a,0),asset(odh::WardrobeAssetKind::appearance,b,1),asset(odh::WardrobeAssetKind::texture,"w100mafitlgt_original",2)};
  const auto p=odh::prepare_original_wardrobe_upgrade(s,values);require(p.chosen[3]->appearance.name==(item.second?b:a),"source x87ratio versus storedbinary32 distinguishes down/up/exact fractions");
  std::swap(values[0],values[1]);values[0].producer_ordinal=0;values[1].producer_ordinal=1;const auto reordered=odh::prepare_original_wardrobe_upgrade(s,values);require(reordered.chosen[3]->appearance.name==(item.second?a:b),"native provider order is explicit and influences real ties without claiming Windowsenumeration equivalence");
  odh::validate_original_wardrobe_assets(p,[&](const auto& old)->std::optional<odh::WardrobeCatalogAsset>{auto current=old;current.producer_ordinal+=100;return current;});require(p.chosen[3]->appearance.name==(item.second?b:a),"cold selectedreceipts survive current provider reorder without resampling");
  rejects([&]{odh::validate_original_wardrobe_assets(p,[](const auto& old)->std::optional<odh::WardrobeCatalogAsset>{auto changed=old;changed.fingerprint[0]^=1;return changed;});},"cold content change refuses instead of selectinganotheroutfit");
  rejects([&]{odh::validate_original_wardrobe_assets(p,[](const auto&)->std::optional<odh::WardrobeCatalogAsset>{return {};});},"cold missingasset refuses instead of daywearfallback");
 }
}
}
int main(){try{matrix();numeric_order_and_cold();std::cout<<"wardrobe checks="<<checks<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
