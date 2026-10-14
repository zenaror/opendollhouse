#include "odh/architecture_arrays.hpp"
#include <iostream>
#include <stdexcept>
#include <functional>
namespace {
int checks = 0;
void require(bool b,const char* message) { ++checks; if (!b) throw std::runtime_error(message); }
void bad(const std::function<void()>& f) { bool rejected=false; try {f();} catch(const std::runtime_error&) {rejected=true;} require(rejected,"malformed input accepted"); }
void word(std::vector<std::uint8_t>& b,std::uint16_t x) {b.push_back(static_cast<std::uint8_t>(x)); b.push_back(static_cast<std::uint8_t>(x>>8));}
void integer(std::vector<std::uint8_t>& b,std::uint32_t x) {word(b,static_cast<std::uint16_t>(x));word(b,static_cast<std::uint16_t>(x>>16));}
std::vector<std::uint8_t> header(int w,int h,int stride) {std::vector<std::uint8_t>b;integer(b,0xfefdfcfb);integer(b,static_cast<std::uint32_t>(w));integer(b,static_cast<std::uint32_t>(h));integer(b,static_cast<std::uint32_t>(stride));integer(b,0x12345678);return b;}
std::vector<std::uint8_t> mapping(unsigned id) {std::vector<std::uint8_t>b;integer(b,99);integer(b,0);integer(b,0x57414c6d);integer(b,2);for(int i=0;i<2;++i){b.push_back(0xff);b.push_back('X');b.push_back(0);b.push_back(0xa3);integer(b,0xfffffffd);b.push_back(7);for(unsigned j=0;j<5+id*2;++j)b.push_back(static_cast<std::uint8_t>(j));}return b;}
}
int main() {try {
 auto b=header(3,2,1);word(b,0);word(b,3);b.insert(b.end(),{1,2,3,0xa3});word(b,0x8003);b.insert(b.end(),{255,0xbc});b.insert(b.end(),{0x34,0x12,0x99});
 auto a=odh::read_architecture_array(b);require(a.data==std::vector<std::uint8_t>({1,2,3,255,255,255}),"RLE fills and raw");require(a.encoded==b,"exact encoded ownership");require(a.zero==-16909061 && a.unknown==0x12345678,"signed metadata");require(a.consumed_bytes==32 && a.trailing_bytes==std::vector<std::uint8_t>({0x34,0x12,0x99}),"uninterpreted suffix");require(a.raw_runs==1 && a.fill_runs==1 && a.zero_opcodes==1,"opcode counts");require(odh::transpose_architecture_array(a)==std::vector<std::uint8_t>({1,255,2,255,3,255}),"rectangular transpose");
 auto sq=header(2,2,2);word(sq,8);sq.insert(sq.end(),{0,1,2,3,4,5,6,7});auto as=odh::read_architecture_array(sq);require(odh::transpose_architecture_array(as)==std::vector<std::uint8_t>({0,1,4,5,2,3,6,7}),"multibyte transpose");
 for(int stride : {1,2,4,8,14,16}) {auto c=header(2,2,stride);word(c,static_cast<std::uint16_t>(0x8000+4*stride));c.insert(c.end(),{0x7f,0x55});auto ac=odh::read_architecture_array(c);require(ac.data.size()==std::size_t(4*stride) && ac.data.back()==0x7f,"stride terminal fill");}
 // Every truncation before the exact payload end must fail, including missing
 // odd literal padding and terminal fill padding. Suffix is independently opaque.
 for(std::size_t n=0;n<32;++n)bad([&]{odh::read_architecture_array(std::vector<std::uint8_t>(b.begin(),b.begin()+static_cast<std::ptrdiff_t>(n)));});
 for(auto dim : {0,-1,2147483647})bad([&]{odh::read_architecture_array(header(dim,2,1));});
 for(auto stride : {0,-1,17})bad([&]{odh::read_architecture_array(header(2,2,stride));});
 auto over=header(2,2,1);word(over,0x8005);over.insert(over.end(),{1,0});bad([&]{odh::read_architecture_array(over);});auto empty=header(2,2,1);word(empty,0x8000);empty.insert(empty.end(),{1,0});bad([&]{odh::read_architecture_array(empty);});auto incomplete=header(2,2,1);word(incomplete,0x8003);incomplete.insert(incomplete.end(),{1,0});bad([&]{odh::read_architecture_array(incomplete);});
 odh::ArchitectureArrayLimits lim;lim.decoded_bytes=5;bad([&]{odh::read_architecture_array(b,lim);});lim={};lim.encoded_bytes=b.size()-1;bad([&]{odh::read_architecture_array(b,lim);});lim={};lim.dimension=2;bad([&]{odh::read_architecture_array(b,lim);});lim={};lim.stride=1;bad([&]{odh::read_architecture_array(sq,lim);});a.data.pop_back();bad([&]{odh::transpose_architecture_array(a);});
 for(unsigned id : {0U,1U,3U}) {auto m=mapping(id);auto out=odh::read_architecture_map(m,static_cast<std::uint16_t>(id));require(out.entries.size()==2 && out.chunk_id==id,"map count/id");require(out.entries[0].name==std::string("\xffX",2) && out.entries[0].name_padding==std::vector<std::uint8_t>({0xa3}),"raw name/pad");require(out.entries[0].unknown==-3 && out.entries[0].id==7 && out.entries[0].unknown2.size()==5+id*2,"entry fields");require(out.entries[0].name==out.entries[1].name && out.encoded==m,"duplicate aliases preserved");for(std::size_t n=0;n<m.size();++n)bad([&]{odh::read_architecture_map(std::vector<std::uint8_t>(m.begin(),m.begin()+static_cast<std::ptrdiff_t>(n)),static_cast<std::uint16_t>(id));});m.push_back(0);bad([&]{odh::read_architecture_map(m,static_cast<std::uint16_t>(id));});}
 auto m=mapping(0);odh::ArchitectureMapLimits ml;ml.entries=1;bad([&]{odh::read_architecture_map(m,0,ml);});ml={};ml.string_bytes=3;bad([&]{odh::read_architecture_map(m,0,ml);});ml={};ml.string_length=1;bad([&]{odh::read_architecture_map(m,0,ml);});ml={};ml.entry_metadata_bytes=4;bad([&]{odh::read_architecture_map(m,0,ml);});ml={};ml.encoded_bytes=m.size()-1;bad([&]{odh::read_architecture_map(m,0,ml);});m[12]=255;m[13]=255;m[14]=255;m[15]=255;bad([&]{odh::read_architecture_map(m,0);});
 std::cout<<checks<<" architecture array checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
