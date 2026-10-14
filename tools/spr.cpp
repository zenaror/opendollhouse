#include "odh/spr.hpp"
#include "resource.hpp"
#include "json.hpp"
#include <charconv>
#include <iostream>
#include <set>
namespace {
std::uint32_t number(std::string s){unsigned base=10;if(s.size()>2&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')){s.erase(0,2);base=16;}
 std::uint32_t v=0;auto r=std::from_chars(s.data(),s.data()+s.size(),v,static_cast<int>(base));if(s.empty()||r.ec!=std::errc{}||r.ptr!=s.data()+s.size())throw odh::FormatError("invalid SPR# numeric selector");return v;}
void distinct(const std::string& output,const std::string& input){if(std::filesystem::weakly_canonical(output)==std::filesystem::weakly_canonical(input)||(std::filesystem::exists(output)&&std::filesystem::exists(input)&&std::filesystem::equivalent(output,input)))throw odh::FormatError("SPR# output aliases an input/other output");}
void write(const std::string& path,const void* data,std::size_t size){std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out)throw odh::FormatError("cannot open SPR# output");if(size)out.write(static_cast<const char*>(data),static_cast<std::streamsize>(size));out.close();if(!out)throw odh::FormatError("cannot write SPR# output; partial exports may remain");}
}
int main(int argc,char** argv){try{
 if(argc<4)throw odh::FormatError("usage: odh-spr INPUT SPR_ID FRAME [--iff-entry FAR_ENTRY] [--missing-palette-black] [--output PREFIX]");
 const auto id=number(argv[2]),index=number(argv[3]);if(id>65535||std::string(argv[1]).size()>4096)throw odh::FormatError("SPR# input/ID exceeds limit");
 bool black=false;std::string output,entry;std::set<std::string> options;
 for(int n=4;n<argc;++n){const std::string option=argv[n];if(!options.insert(option).second)throw odh::FormatError("duplicate SPR# option");
  if(option=="--missing-palette-black")black=true;
  else if(option=="--iff-entry"){if(n+1>=argc)throw odh::FormatError("SPR# FAR entry name required");entry=argv[++n];if(entry.empty()||entry.size()>4096)throw odh::FormatError("SPR# FAR entry name outside limit");}
  else if(option=="--output"){if(n+1>=argc)throw odh::FormatError("SPR# output prefix required");output=argv[++n];if(output.empty()||output.size()>4090)throw odh::FormatError("SPR# output prefix outside limit");}
  else throw odh::FormatError("unknown SPR# option");}
 if(!output.empty()){distinct(output+".rgba",argv[1]);distinct(output+".idx",argv[1]);distinct(output+".rgba",output+".idx");}
 odh::SprFrame frame;bool fallback=false;std::size_t palette_count=0;
 auto load=[&](const auto& view){std::optional<odh::View> spr,pal;std::vector<odh::Chunk> chunks;odh::Limits limits;limits.iff_chunks=4096;
  odh::iff_chunks(view,[&](const auto& c){chunks.push_back(c);if(c.type=="SPR#"&&c.id==id){if(spr)throw odh::FormatError("ambiguous SPR# chunk ID");spr=c.payload;}},limits);
  if(!spr)throw odh::FormatError("SPR# chunk not found");
  const auto header=odh::read_spr_header(*spr);
  for(const auto& c:chunks)if(c.type=="PALT"&&c.id==header.palette_id){if(pal)throw odh::FormatError("ambiguous SPR# palette ID");pal=c.payload;}
  odh::Palette palette{};
  if(pal)palette=odh::read_palt(*pal);else{if(!black)throw odh::FormatError("SPR# palette missing; explicit policy required");fallback=true;palette.colors.assign(256,{0,0,0,255});}
  palette_count=palette.colors.size();frame=odh::read_spr_frame(*spr,index,palette);
 };
 if(entry.empty())with_iff_file(argv[1],load);else with_far_entry(argv[1],entry,load);
 std::uint64_t opaque=0;for(const auto& p:frame.pixels)if(p[3])++opaque;
 if(!output.empty()){static_assert(sizeof(std::array<std::uint8_t,4>)==4);write(output+".rgba",frame.pixels.data(),frame.pixels.size()*4);write(output+".idx",frame.indices.data(),frame.indices.size());}
 std::cout<<"{\"evidence\":\"ODH_SPR_number_paletted_decode\",\"input\":"<<json_bytes(argv[1])<<",\"iff_entry\":"<<(entry.empty()?"null":json_bytes(entry))<<",\"sprite_id\":"<<id<<",\"frame\":"<<index
 <<",\"version\":"<<frame.header.version<<",\"big_endian\":"<<(frame.header.big_endian?"true":"false")<<",\"frame_count\":"<<frame.header.frame_count
 <<",\"palette_id_raw\":"<<frame.header.palette_id_raw<<",\"palette_id\":"<<frame.header.palette_id<<",\"palette_missing\":"<<(fallback?"true":"false")
 <<",\"palette_policy\":"<<json_bytes(fallback?"explicit_black_256":"explicit_PALT")<<",\"palette_entries\":"<<palette_count
 <<",\"reserved\":"<<frame.reserved<<",\"width\":"<<frame.width<<",\"height\":"<<frame.height<<",\"opaque_pixels\":"<<opaque
 <<",\"transparent_pixels\":"<<frame.pixels.size()-opaque<<",\"commands\":"<<frame.commands<<",\"consumed_bytes\":"<<frame.consumed_bytes
 <<",\"remaining_bytes\":"<<frame.remaining_bytes<<",\"ended_with_marker\":"<<(frame.ended_with_marker?"true":"false")
 <<",\"wall_style_applied\":false,\"rendered\":false,\"original_visual_equivalence\":false,\"output_prefix\":"<<(output.empty()?"null":json_bytes(output))<<"}\n";
 return 0;
}catch(const std::exception& e){std::cerr<<"odh-spr: "<<e.what()<<'\n';return 2;}}
