// SPDX-License-Identifier: MPL-2.0
#include "resource.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F> void rejects(F f){bool caught=false;try{f();}catch(const odh::FormatError&){caught=true;}check(caught,"expected format rejection");}
void word(std::string& bytes,std::uint32_t n){for(int i=0;i<4;++i)bytes.push_back(static_cast<char>((n>>(i*8))&255));}
void save(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(out),"write temporary fixture");}
std::string far(const std::string& name){std::string bytes="FAR!byAZ";word(bytes,1);word(bytes,20);bytes+="data";word(bytes,1);word(bytes,4);word(bytes,4);word(bytes,16);word(bytes,static_cast<std::uint32_t>(name.size()));return bytes+name;}
struct Temp {std::filesystem::path root=std::filesystem::temp_directory_path()/("odh-resource-io-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Temp(){std::filesystem::create_directory(root);}~Temp(){std::error_code e;std::filesystem::remove_all(root,e);}};
void buffered_reads(const std::filesystem::path& root){
 const auto path=root/"bytes.bin";std::string expected(196731,'\0');for(std::size_t n=0;n<expected.size();++n)expected[n]=static_cast<char>((n*71+9)%256);save(path,expected);
 BufferedResourceFile input(path.string());odh::View view(input,expected.size());
 for(std::size_t n=0;n<5000;++n){const auto offset=(n*7919)%(expected.size()-23);std::array<char,23> actual{};view.read(offset,actual.data(),actual.size());check(std::memcmp(actual.data(),expected.data()+offset,actual.size())==0,"random seek data mismatch");}
 std::string actual(expected.size(),'\0');view.read(0,actual.data(),actual.size());check(actual==expected,"sequential cross-window mismatch");
 auto child=view.subview(65530,35);std::array<char,35> edge{};child.read(0,edge.data(),edge.size());check(std::memcmp(edge.data(),expected.data()+65530,edge.size())==0,"subview boundary mismatch");
 rejects([&]{child.read(35,edge.data(),1);});rejects([&]{view.read(expected.size(),edge.data(),1);});
 input.clear();input.seekg(0,std::ios::end);check(input.tellg()==static_cast<std::streamoff>(expected.size()),"end seek mismatch");char byte{};input.read(&byte,1);check(input.eof(),"EOF not reported");view.read(0,&byte,1);check(byte==expected[0],"seek after EOF mismatch");
 input.seekg(-1,std::ios::beg);check(input.fail(),"negative seek accepted");input.clear();input.seekg(20);input.seekg(std::numeric_limits<std::streamoff>::max(),std::ios::cur);check(input.fail(),"overflowing relative seek accepted");
 input.clear();input.seekg(20);input.seekg(std::numeric_limits<std::streamoff>::min(),std::ios::cur);check(input.fail(),"underflowing relative seek accepted");
 BufferedResourceFile scalar(path.string());odh::View fields(scalar,expected.size());for(std::uint64_t n=0;n<10000;++n)(void)fields.le32(n);check(scalar.physical_reads()==1,"scalar seeks discarded read window");
 BufferedResourceFile absent((root/"absent").string());check(!absent,"missing source accepted");
}
// Exercise the public reader without any pre-existing strong manifest owner.
// A cached weak registration must not outlive the temporary indexing owner.
void entry_lifetime(const std::filesystem::path& root){
 const auto path=root/"standalone.far";save(path,far("StandaloneNameLongEnoughToAllocate"));
 for(int repeat=0;repeat<3;++repeat){
  bool visited=false;
  with_far_entry(path.string(),"StandaloneNameLongEnoughToAllocate",[&](const odh::View& payload){
   std::array<char,4> bytes{};payload.read(0,bytes.data(),bytes.size());
   check(std::string(bytes.data(),bytes.size())=="data","standalone FAR payload mismatch");visited=true;
  });
  check(visited,"standalone FAR payload not visited");
 }
 bool missing=false;try{with_far_entry(path.string(),"Missing",[](const odh::View&){});}
 catch(const FarEntryNotFound&){missing=true;}check(missing,"missing standalone entry not rejected");
}
void manifest_identity(const std::filesystem::path& root){
 const auto one=root/"one.far",two=root/"two.far";save(one,far("First"));save(two,far("Other"));
 BufferedResourceFile a(one.string()),b(one.string()),c(two.string());
 auto first=shared_far_manifest(one.string(),odh::View(a,std::filesystem::file_size(one)));
 auto again=shared_far_manifest(one.string(),odh::View(b,std::filesystem::file_size(one)));
 check(first==again,"live manifest not shared");check(b.physical_reads()==0,"manifest hit reread source");
 auto other=shared_far_manifest(two.string(),odh::View(c,std::filesystem::file_size(two)));check(first!=other&&other->at(0).name=="Other","separate source reused manifest");
 odh::Limits limits;limits.far_entries=0;rejects([&]{shared_far_manifest(one.string(),odh::View(b,std::filesystem::file_size(one)),limits);});
 limits={};limits.far_name_length=4;rejects([&]{shared_far_manifest(one.string(),odh::View(b,std::filesystem::file_size(one)),limits);});
 limits={};limits.far_name_bytes=4;rejects([&]{shared_far_manifest(one.string(),odh::View(b,std::filesystem::file_size(one)),limits);});
 const auto original_time=std::filesystem::last_write_time(one);save(one,far("Later"));std::filesystem::last_write_time(one,original_time+std::chrono::seconds(4));
 BufferedResourceFile changed(one.string());auto replaced=shared_far_manifest(one.string(),odh::View(changed,std::filesystem::file_size(one)));check(first!=replaced&&replaced->at(0).name=="Later","modified source reused stale index");check(first->at(0).name=="First","immutable old owner changed");
 save(one,far("LongerName"));BufferedResourceFile resized(one.string());auto grown=shared_far_manifest(one.string(),odh::View(resized,std::filesystem::file_size(one)));check(grown->at(0).name=="LongerName","resized source reused stale index");
}
}
int main(){try{Temp temp;buffered_reads(temp.root);entry_lifetime(temp.root);manifest_identity(temp.root);std::cout<<"Buffered resource reads, bounds and live manifest identity passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
