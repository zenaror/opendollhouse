/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. https://mozilla.org/MPL/2.0/ */
#include "app/PieMenu.h"
#include <cstdio>
#include <stdexcept>
using namespace simapp;
static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static AppInput pointer(int x,int y,bool down=false,bool up=false){AppInput i;i.TouchX=x;i.TouchY=y;i.TouchDown=down;i.TouchUp=up;i.Touching=down;return i;}
int main(){try{
 for(auto size:{std::pair{800,600},std::pair{1024,768},std::pair{1280,720}}){
  PieMenu menu;menu.Desktop=true;PieChoice chosen;
  auto open=[&]{menu.Open({{"Talk/About Work",7},{"Talk/About Interests",8},{"Wave",9}},"",Rect(0,0,size.first,size.second-100),size.first/2,(size.second-100)/2);};
  auto click=[&](const char* label){int x=0,y=0;require(menu.FindItem(label,x,y),"option not found");menu.Pointer(pointer(x,y,true),chosen);return menu.Pointer(pointer(x,y,false,true),chosen);};
  open();
  require(menu.Pointer(pointer(size.first/2,(size.second-100)/2,false,true),chosen)==PieMenu::Result::None&&menu.IsOpen,"opening mouse release dismissed the menu");
  require(click("Wave")==PieMenu::Result::Chosen&&chosen.ID==9&&!menu.IsOpen,"normal click did not choose action");
  open();require(click("Talk")==PieMenu::Result::Navigated&&menu.IsOpen,"category click did not open submenu");
  require(click("About Work")==PieMenu::Result::Chosen&&chosen.ID==7,"submenu click chose wrong action");
  open();menu.Pointer(pointer(1,1,true),chosen);
  require(menu.Pointer(pointer(1,1,false,true),chosen)==PieMenu::Result::Closed&&!menu.IsOpen,"new outside click did not close menu");
  open();int x=0,y=0;require(menu.FindItem("Wave",x,y),"option not found after reopen");menu.Pointer(pointer(x,y,true),chosen);
  auto cancel=pointer(1,1);cancel.PointerCancelled=true;menu.Pointer(cancel,chosen);
  require(menu.Pointer(pointer(1,1,false,true),chosen)==PieMenu::Result::None&&menu.IsOpen,"cancelled press leaked into a later release");
  auto right=pointer(1,1);right.MouseRightDown=true;
  require(menu.Pointer(right,chosen)==PieMenu::Result::Closed&&!menu.IsOpen,"right click did not close menu");
  std::printf("desktop pie pointer %dx%d: open/release, choose, submenu, outside, focus cancellation, right cancel pass\n",size.first,size.second);
 }
 return 0;
}catch(const std::exception& e){std::fprintf(stderr,"desktop pie pointer failed: %s\n",e.what());return 1;}}
