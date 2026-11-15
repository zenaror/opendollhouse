// SPDX-License-Identifier: MPL-2.0
#include "resource.hpp"
#include "preview_archive_inventory.hpp"
#include "portrait_name_cache.hpp"
#include <thread>
#include <atomic>
#if defined(__linux__)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
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
 auto first=shared_far_manifest(a);
 auto again=shared_far_manifest(b);
 check(first==again,"live manifest not shared");check(b.physical_reads()==0,"manifest hit reread source");
 auto other=shared_far_manifest(c);check(first!=other&&other->at(0).name=="Other","separate source reused manifest");
 odh::Limits limits;limits.far_entries=0;rejects([&]{shared_far_manifest(b,limits);});
 limits={};limits.far_name_length=4;rejects([&]{shared_far_manifest(b,limits);});
 limits={};limits.far_name_bytes=4;rejects([&]{shared_far_manifest(b,limits);});
 const auto original_time=std::filesystem::last_write_time(one);save(one,far("Later"));std::filesystem::last_write_time(one,original_time+std::chrono::seconds(4));
 BufferedResourceFile changed(one.string());auto replaced=shared_far_manifest(changed);check(first!=replaced&&replaced->at(0).name=="Later","modified source reused stale index");check(first->at(0).name=="First","immutable old owner changed");
#if defined(__linux__) || defined(__APPLE__)
 const auto previous_time=std::filesystem::last_write_time(one);
 const auto replacement=root/"replacement.far";
 save(replacement,far("Moved"));std::filesystem::last_write_time(replacement,previous_time);
 std::filesystem::rename(replacement,one);
 BufferedResourceFile moved(one.string());auto moved_index=shared_far_manifest(moved);
 check(moved_index!=replaced&&moved_index->at(0).name=="Moved","different inode reused same-size and same-time manifest");
#endif
 save(one,far("LongerName"));BufferedResourceFile resized(one.string());auto grown=shared_far_manifest(resized);check(grown->at(0).name=="LongerName","resized source reused stale index");
}
constexpr std::size_t SourceBytes=196731;
char source_byte(std::size_t owner,std::size_t offset){return static_cast<char>((owner*29+offset*71+9)%256);}
std::filesystem::path source_path(const std::filesystem::path& root,std::size_t owner){return root/("owner-"+std::to_string(owner)+".bin");}
void make_sources(const std::filesystem::path& root){
 for(std::size_t owner=0;owner<90;++owner){std::string bytes(SourceBytes,'\0');for(std::size_t n=0;n<bytes.size();++n)bytes[n]=source_byte(owner,n);save(source_path(root,owner),bytes);}
}
void owner_read(BufferedResourceFile& input,std::size_t owner,std::size_t offset){
 std::array<char,23> bytes{};odh::View view(input,SourceBytes);view.read(offset,bytes.data(),bytes.size());
 for(std::size_t n=0;n<bytes.size();++n)check(bytes[n]==source_byte(owner,offset+n),"evicted owner read another owner's bytes");
 check(resource_io_counters().live_handles<=BufferedResourceFile::HandleLimit,"resource backing handle budget exceeded");
}
void many_owners(const std::filesystem::path& root){
 const auto owners_before=resource_io_counters().owners.load(),handles_before=resource_io_counters().live_handles.load();
 {
  std::vector<std::unique_ptr<BufferedResourceFile>> owners;
  for(std::size_t n=0;n<90;++n){owners.emplace_back(new BufferedResourceFile(source_path(root,n).string()));check(bool(*owners.back()),"90-owner admission failed");owner_read(*owners.back(),n,0);}
  const auto opens=resource_io_counters().opens.load();owner_read(*owners.front(),0,19);
  check(resource_io_counters().opens==opens,"evicted in-window hit reopened the source");
  for(std::size_t pass=0;pass<6;++pass)for(std::size_t n=0;n<owners.size();++n)owner_read(*owners[n],n,65530+(pass%2)*65536);
  BufferedResourceFile independent(source_path(root,0).string());owners[0]->clear();owners[0]->seekg(17);independent.seekg(131081);
  char a{},b{};owners[0]->read(&a,1);independent.read(&b,1);check(a==source_byte(0,17)&&b==source_byte(0,131081),"same path owners shared a cursor");
  check(owners[0]->tellg()==18&&independent.tellg()==131082,"owner logical positions interfered");
  independent.seekg(-3,std::ios::end);check(independent.tellg()==static_cast<std::streamoff>(SourceBytes-3),"evicted end seek failed");
  for(std::size_t n=0;n<owners.size();n+=2)owners[n].reset();
 }
 check(resource_io_counters().owners==owners_before&&resource_io_counters().live_handles==handles_before,"owner teardown left registered readers/handles");
 try{std::vector<std::unique_ptr<BufferedResourceFile>> unwind;for(int n=0;n<10;++n)unwind.emplace_back(new BufferedResourceFile(source_path(root,n).string()));throw std::runtime_error("unwind");}catch(const std::runtime_error&){}
 check(resource_io_counters().owners==owners_before&&resource_io_counters().live_handles==handles_before,"exception unwind left registered readers/handles");
}
void descriptor_limit(const std::filesystem::path& root){
#if defined(__linux__)
 const auto child=::fork();check(child>=0,"fork descriptor fixture");
 if(child==0){
  try{
   struct rlimit limit{};check(::getrlimit(RLIMIT_NOFILE,&limit)==0,"read descriptor budget");limit.rlim_cur=64;check(::setrlimit(RLIMIT_NOFILE,&limit)==0,"set child descriptor budget");
   std::vector<std::ifstream> other;for(int n=0;n<16;++n){other.emplace_back(source_path(root,0),std::ios::binary);check(bool(other.back()),"open native-budget stand-in");}
   std::vector<std::ifstream> retained;bool exhausted=false;
   for(int n=0;n<90;++n){errno=0;retained.emplace_back(source_path(root,n),std::ios::binary);const int error=errno;if(!retained.back()){check(error==EMFILE,"retained baseline failed for a non-budget reason");exhausted=true;break;}}
   check(exhausted,"retained baseline unexpectedly fit child descriptor budget");retained.clear();
   many_owners(root);check(resource_io_counters().peak_handles<=BufferedResourceFile::HandleLimit,"child peak exceeded resource budget");::_exit(0);
  }catch(const std::exception& error){std::cerr<<"descriptor child: "<<error.what()<<'\n';::_exit(1);}
 }
 int status=0;check(::waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"bounded readers failed child descriptor fixture");
#else
 (void)root;
#endif
}
void concurrent_owners(const std::filesystem::path& root){
 const auto before=resource_io_counters().owners.load();std::atomic<bool> start{false},failed{false};std::vector<std::thread> workers;
 for(std::size_t worker=0;worker<8;++worker)workers.emplace_back([&,worker]{
  try{std::vector<std::unique_ptr<BufferedResourceFile>> inputs;for(std::size_t n=0;n<8;++n)inputs.emplace_back(new BufferedResourceFile(source_path(root,worker*8+n).string()));
   while(!start.load())std::this_thread::yield();
   for(std::size_t n=0;n<150;++n)owner_read(*inputs[n%8],worker*8+n%8,(n*7919)%(SourceBytes-23));
  }catch(...){failed=true;}
 });
 start=true;for(auto& thread:workers)thread.join();check(!failed,"independent owners raced while handles were evicted");
 check(resource_io_counters().owners==before&&resource_io_counters().live_handles==0,"thread teardown left registered readers");
}
void changed_reopen(const std::filesystem::path& root){
 const auto path=root/"mutable.bin";
 for(int scenario=0;scenario<3;++scenario){
  std::string original(SourceBytes,'A');save(path,original);const auto old_time=std::filesystem::last_write_time(path);
  BufferedResourceFile old(path.string());char byte{};old.read(&byte,1);check(byte=='A',"seed immutable owner window");
  std::vector<std::unique_ptr<BufferedResourceFile>> churn;for(int n=0;n<5;++n)churn.emplace_back(new BufferedResourceFile(source_path(root,n).string()));
  if(scenario==0){save(path,std::string(SourceBytes+1,'B'));}
  else if(scenario==1){std::filesystem::remove(path);}
  else{
   const auto replacement=root/"replacement.bin";save(replacement,std::string(SourceBytes,'B'));std::filesystem::last_write_time(replacement,old_time);
   std::filesystem::rename(replacement,path);
  }
  // Cached bytes are immutable snapshots, not claims of revalidated disk state.
  const auto opens=resource_io_counters().opens.load();old.clear();old.seekg(9);old.read(&byte,1);check(byte=='A'&&resource_io_counters().opens==opens,"cached window was replaced or restatted through open");
#if defined(__linux__) || defined(__APPLE__)
  old.clear();old.seekg(131080);old.read(&byte,1);check(!old,"changed/removed/replaced source reopened unchecked");
  check(old.diagnostic().find(path.string())!=std::string::npos&&old.diagnostic().find("reopen-identity")!=std::string::npos,"reopen rejection lost source identity diagnostic");
#else
  if(scenario!=2){old.clear();old.seekg(131080);old.read(&byte,1);check(!old,"changed source reopened unchecked");}
#endif
  if(scenario!=1){BufferedResourceFile fresh(path.string());fresh.read(&byte,1);check(byte=='B',"new owner inherited an old source window");}
 }
 BufferedResourceFile missing((root/"not-present.bin").string());check(!missing&&missing.diagnostic().find("initial-open")!=std::string::npos&&missing.diagnostic().find("errno="+std::to_string(ENOENT))!=std::string::npos,"initial open failure lost path/errno evidence");
 BufferedResourceFile directory(root.string());check(!directory&&directory.diagnostic().find("identity")!=std::string::npos,"unsupported source metadata/type was admitted");
}
void preview_inventory(const std::filesystem::path& root){
 const auto tree=root/"inventory";std::filesystem::create_directories(tree/"Downloads/deep/custom");std::filesystem::create_directories(tree/"directory.FAR");
 save(tree/"Main.FAR","a");save(tree/"Downloads/deep/custom/skin.fAr","b");save(tree/"directory.FAR/inside.far","c");
 for(int n=0;n<40;++n)save(tree/("unrelated-"+std::to_string(n)+".txt"),"ignored");
 std::error_code symlink_error;std::filesystem::create_symlink(tree/"Main.FAR",tree/"alias.far",symlink_error);
 const auto canonical=std::filesystem::canonical(tree);const auto inventory=odh_host::preview_detail::discover_archives(canonical);
 std::vector<std::string> expected{std::filesystem::canonical(tree/"Main.FAR").string(),std::filesystem::canonical(tree/"Downloads/deep/custom/skin.fAr").string(),std::filesystem::canonical(tree/"directory.FAR/inside.far").string()};std::sort(expected.begin(),expected.end());
 check(inventory.archives==expected,"complete nested/mixed-case/symlink archive inventory changed");
 std::size_t visited=0;for(const auto& entry:std::filesystem::recursive_directory_iterator(tree)){(void)entry;++visited;}
 check(inventory.filesystem_entries==visited&&inventory.regular_checks<=5&&inventory.regular_checks<visited,"discovery skipped traversal entries or queried every unrelated file");
 rejects([&]{(void)odh_host::preview_detail::discover_archives(canonical,2);});rejects([&]{(void)odh_host::preview_detail::discover_archives(canonical,200000,2);});
 if(!symlink_error){save(root/"outside.far","outside");std::filesystem::create_symlink(root/"outside.far",tree/"escaped.far");rejects([&]{(void)odh_host::preview_detail::discover_archives(canonical);});}
}

void portrait_names(const std::filesystem::path& root) {
 using namespace odh_host::preview_detail;
 const auto cache=root/"test-names.cache";
 int parsed=0;
 auto read=[&](PortraitNameCache& index,const std::string& data,char kind='T'){
  const odh::View view(reinterpret_cast<const std::uint8_t*>(data.data()),data.size());
  return index.read(view,kind,[&](const odh::View& src){
   ++parsed; std::string name(src.size(),' ');
   src.subview(0,src.size()).read(0,name.data(),name.size());
   return PortraitNames{{'A',name}};
  });
 };
 {
  PortraitNameCache cold(cache);
  check(read(cold,"suit")[0].second=="suit" && parsed==1,"cold portrait declaration");
  cold.save();
 }
 {
  PortraitNameCache warm(cache);
  check(read(warm,"suit")[0].second=="suit" && parsed==1,"warm portrait index reparsed source");
  check(read(warm,"sIut")[0].second=="sIut" && parsed==2,"same-size content change retained old declaration");
  check(read(warm,"suit",'E')[0].second=="suit" && parsed==3,"archive/CMX cache domains collided");
 }
 { std::fstream damage(cache,std::ios::in|std::ios::out|std::ios::binary);damage.seekp(-1,std::ios::end);damage.put('X'); }
 PortraitNameCache repaired(cache);
 check(read(repaired,"suit")[0].second=="suit" && parsed==4,"damaged index bypassed parser");
#if defined(__linux__)
 const auto tree=root/"native";
 std::filesystem::create_directories(tree/"Downloads/Nested");
 std::filesystem::create_directories(tree/"Other");
 save(tree/"Other/outside.cmx","x");save(tree/"Other/mod.Far","x");
 save(tree/"Downloads/Nested/loose.CmX","x");save(tree/"Downloads/texture.bmp","x");
 SkinPaths skins;
 const auto files=discover_device_archives(tree,{(tree/"Downloads").string()},skins);
 check(files.archives==discover_archives(tree).archives && skins.size()==1 && skins.front().size()==2,
       "single-pass device and full archive discovery disagree");
 std::sort(skins[0].begin(),skins[0].end());
 check(skins[0][0]==tree/"Downloads/Nested/loose.CmX" && skins[0][1]==tree/"Downloads/texture.bmp",
       "device loose skin inventory escaped its selected root");
 const auto cached=[](const std::string& path,const odh::InstallEntryVisitor& visit) {
  if(std::filesystem::path(path).filename()!="Nested")return false;
  visit("loose.CmX",false);return true;
 };
 SkinPaths reused_skins;
 const auto reused=discover_device_archives(tree,{(tree/"Downloads").string()},reused_skins,cached);
 check(reused.live_directories_reused==1 && reused.filesystem_entries==files.filesystem_entries &&
       reused.archives==files.archives && reused_skins==skins,"live directory reuse changed the full inventory");
 rejects([&]{(void)discover_device_archives(tree,{},skins,cached,1);});
 rejects([&]{(void)discover_device_archives(tree,{},skins,cached,200000,0);});
#endif
}

}
int main(){try{Temp temp;buffered_reads(temp.root);entry_lifetime(temp.root);manifest_identity(temp.root);make_sources(temp.root);descriptor_limit(temp.root);many_owners(temp.root);concurrent_owners(temp.root);changed_reopen(temp.root);preview_inventory(temp.root);portrait_names(temp.root);check(resource_io_counters().live_handles==0&&resource_io_counters().owners==0,"final resource lifetime leak");std::cout<<"Buffered resource reads, handle budget, lifetime, identity and complete preview inventory passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
