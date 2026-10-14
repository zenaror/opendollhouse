#include "odh/spr.hpp"
#include <sstream>
#include <iostream>
#include <functional>
namespace {
int checks=0;
void check(bool ok){++checks;if(!ok)throw std::runtime_error("SPR# assertion #"+std::to_string(checks));}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const odh::FormatError&){caught=true;}check(caught);}
void set(std::string& b,std::size_t at,std::uint32_t v,unsigned n=4,bool big=false){for(unsigned i=0;i<n;++i)b.at(at+i)=static_cast<char>((v>>((big?n-1-i:i)*8))&255);}
std::string bytes(std::initializer_list<unsigned> values){std::string b;for(auto v:values)b+=static_cast<char>(v);return b;}
std::string sprite(std::string commands,unsigned w=4,unsigned h=3,bool big=false,unsigned version=504){std::string b(24,'\0');set(b,0,version,4,big);set(b,4,1,4,big);set(b,8,0x12340007,4,big);set(b,12,16,4,big);set(b,16,0x12345678,4,big);set(b,20,h,2,big);set(b,22,w,2,big);return b+commands;}
odh::Palette palette(){odh::Palette p{};p.colors={{10,20,30,255},{40,50,60,255},{70,80,90,255}};return p;}
odh::SprFrame decode(const std::string& b,odh::SpriteLimits limits={},std::uint32_t frame=0,odh::Palette pal=palette()){std::istringstream stream(b);return odh::read_spr_frame(odh::View(stream,b.size()),frame,pal,limits);}
}
int main(){try{
 const auto commands=bytes({0,16,16,0,4,8,1,1,2,2,1,77,9,1,4,10,3,3,0,1,2,99,1,1,5,0});
 for(bool big:{false,true})for(unsigned version:{502U,504U,505U,1000U}){
  const auto b=sprite(commands,4,3,big,version);const auto f=decode(b);check(f.header.version==version&&f.header.big_endian==big&&f.header.palette_id_raw==0x12340007&&f.header.palette_id==7);
  check(f.reserved==0x12345678&&f.width==4&&f.height==3&&f.ended_with_marker&&f.remaining_bytes==0&&f.consumed_bytes==commands.size()+8);
  check(f.pixels[0]==std::array<std::uint8_t,4>{0,0,0,0}&&f.pixels[1]==palette().colors[1]&&f.pixels[2]==palette().colors[1]&&f.pixels[3][3]==0);
  for(unsigned i=4;i<8;++i)check(f.pixels[i][3]==0&&f.indices[i]==0);
  check(f.indices==std::vector<std::uint8_t>{0,1,1,0,0,0,0,0,0,1,2,0});
  check(f.pixels[8]==palette().colors[0]&&f.pixels[9]==palette().colors[1]&&f.pixels[10]==palette().colors[2]&&f.pixels[11][3]==0);
  auto tail=b+bytes({18,19});check(decode(tail).remaining_bytes==2);
  for(std::size_t n=0;n<b.size();++n)rejects([&]{decode(b.substr(0,n));});
 }
 auto b=sprite(commands);auto f=decode(b);check(f.commands==10);
 auto early=decode(sprite(bytes({5,99})));check(early.pixels==std::vector<std::array<std::uint8_t,4>>(12,{0,0,0,0}));
 check(decode(sprite(bytes({5,0}),0,0)).pixels.empty());
 check(decode(sprite(bytes({4,2,5,0}),0,1)).pixels.empty());
 auto alias=b;alias.insert(16,4,'\0');set(alias,4,2);set(alias,12,20);set(alias,16,20);check(decode(alias,{},1).pixels==f.pixels);
 auto overlap=alias;set(overlap,16,23);rejects([&]{decode(overlap);});
 auto empty=b.substr(0,12);set(empty,4,0);std::istringstream stream(empty);check(odh::read_spr_header(odh::View(stream,empty.size())).frame_count==0);rejects([&]{decode(empty);});
 for(auto pair:{std::pair<std::size_t,std::uint32_t>{0,0},{0,1001},{0,501},{4,4097},{12,15},{12,0xffffffffU}}){auto bad=b;set(bad,pair.first,pair.second);rejects([&]{decode(bad);});}
 for(const auto& badcmd:std::vector<std::string>{bytes({7,0}),bytes({9,4,5,0}),bytes({4,1}),bytes({4,4,1,5,5,0}),bytes({4,6,2,1,3,0,5,0}),bytes({4,4,2,1,5,0}),bytes({4,6,3,3,0,1,5,0}),bytes({4,4,6,1,5,0}),bytes({4,3,1,5,0})})rejects([&]{decode(sprite(badcmd));});
 auto partial=palette();partial.colors[0][3]=0;rejects([&]{decode(b,{},0,partial);});auto missing=palette();missing.colors.clear();rejects([&]{decode(b,{},0,missing);});missing=palette();missing.colors.resize(257);rejects([&]{decode(b,{},0,missing);});rejects([&]{decode(b,{},1);});
 for(unsigned kind=0;kind<6;++kind){odh::SpriteLimits l;if(kind==0)l.frames=0;if(kind==1)l.width=3;if(kind==2)l.height=2;if(kind==3)l.pixels=11;if(kind==4)l.palette_entries=2;if(kind==5)l.commands=9;rejects([&]{decode(b,l);});}
 odh::SpriteLimits exact;exact.frames=1;exact.width=4;exact.height=3;exact.pixels=12;exact.palette_entries=3;exact.commands=10;check(decode(b,exact).pixels==f.pixels);
 check(decode(sprite(bytes({4,6,2,0,1,88,5,0}))).pixels==std::vector<std::array<std::uint8_t,4>>(12,{0,0,0,0}));
 std::cout<<checks<<" SPR# checks passed (synthetic only)\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
