// SPDX-License-Identifier: MPL-2.0
#include "original_live_help.hpp"
#include "catalog_info_view.hpp"
#include <stdexcept>
#include <iostream>
using namespace odh_host;
namespace {void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}}
int main(){try{
 require(!original_needs_help(8,false)&&!original_personality_help(5,500,false),"invalid index");
 const unsigned indexes[]={1,5,9,13,3,7,11,15};
 for(unsigned need=0;need<8;++need)for(bool pet:{false,true}){const auto h=*original_needs_help(need,pet);require(h.table==(pet?180:130)&&h.title==indexes[need]&&h.description==indexes[need]+1,"HUD source order differs from Live resource order");}
 require(original_needs_help(0,false)->objects[0]==0xb94755df&&original_needs_help(0,false)->objects[1]==0xff9f18e0,"Hunger fridge/stove definitions");
 require(original_needs_help(4,false)->objects[1]==0xbf137195&&original_needs_help(6,false)->objects[0]==0,"Energy coffee; Social no invented figure");
 require(original_needs_help(0,true)->objects[0]==0x8eec19f7&&original_needs_help(3,true)->objects[1]==0xbedd7b26,"pet object bank");
 for(unsigned trait=0;trait<5;++trait)for(bool pet:{false,true})for(const auto raw:{-100,0,299,300,799,800,1000}){const auto h=*original_personality_help(trait,raw,pet);const unsigned expected=raw<300?0:raw<800?1:2;require(h.bucket==expected&&h.table==(pet?170:131)&&h.name==1+7*trait&&h.title==h.name+1+expected&&h.description==h.name+4+expected,"original personality boundaries/title/body bank");}
 CatalogInfoContent ordinary;ordinary.name="Catalog";ordinary.description="Description";
 const auto normal=catalog_info_layout(1024,768,ordinary);require(normal.name.x-normal.panel.x==178&&normal.description.width==373,"catalog default geometry");
 odh::BitmapImage picture;picture.width=picture.height=1;picture.pixels={{{255,0,0,255}}};
 CatalogInfoContent help;help.live_help=true;help.icon=&picture;help.right_icon=&picture;
 const auto both=catalog_info_layout(1024,768,help);require(both.name.x-both.panel.x==178&&both.description.width==204,"two-picture center text");
 require(both.icon.x==both.panel.x&&both.right_icon.x-both.panel.x==372,"picture slots have no text inset");
 require(both.icon.width==168&&both.right_icon.width==168&&both.icon.height==127,"authored help viewport");
 help.right_icon=nullptr;require(catalog_info_layout(1024,768,help).description.width==367,"release unused right slot");
 CatalogInfoScrollState scroll;auto two=help;two.right_icon=&picture;require(scroll.prepare(1024,768,two,{}).description.width==204&&scroll.prepare(1024,768,help,{}).description.width==367,"picture layout invalidates scroll cache");
 help.live_language=15;require(catalog_info_layout(1024,768,help).panel.height==both.panel.height+50,"original Asian help padding");help.live_language=1;
 help.icon=nullptr;help.text_only_live=true;require(catalog_info_layout(1024,768,help).description.width==530,"no-picture help text space");
 // Exercise the same help-kind selector used by LotScreen with the real
 // empty Social bank; picture absence must not erase live-panel identity.
 require(!original_live_panel_help("settings",false,{})&&!original_live_panel_help("catalog",false,{}),"ordinary consumers retain default layout");
 for(bool pet:{false,true}){
  const auto social_source=*original_needs_help(6,pet);
  require(!social_source.objects[0]&&!social_source.objects[1],"Social has no invented illustrations");
  CatalogInfoContent social;social.live_help=original_live_panel_help("needs",false,social_source.objects);social.text_only_live=true;
  const auto base=catalog_info_layout(1024,768,social);
  for(unsigned language:{15,17,18,19,20}){
   social.live_language=language;const auto selected=catalog_info_layout(1024,768,social);
   require(selected.panel.height==base.panel.height+50,"Social retains authored tall-language padding without figures");
   require(selected.description.width==(language==19?525:530),"Social language19 right boundary");
  }
 }
 auto one_picture=help;one_picture.text_only_live=false;one_picture.icon=&picture;one_picture.live_language=19;
 require(catalog_info_layout(1024,768,one_picture).description.width==362,"language19 no-right-picture margin also applies with left figure");
 one_picture.right_icon=&picture;require(catalog_info_layout(1024,768,one_picture).description.width==204,"right figure keeps its own boundary in language19");
 auto settings=help;settings.live_help=original_live_panel_help("settings",false,{});settings.live_language=19;
 require(catalog_info_layout(1024,768,settings).description.width==530,"settings default text geometry preserved");
 ordinary.live_language=19;require(catalog_info_layout(1024,768,ordinary).description.width==373,"catalog geometry ignores live language");
 bool refused=false;try{help.price="10";catalog_info_layout(1024,768,help);}catch(const odh::FormatError&){refused=true;}require(refused,"help refuses purchase fields");
 std::cout<<"original_live_help PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
