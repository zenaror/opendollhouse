/* SPDX-License-Identifier: MPL-2.0 */
#include "files/LotRezoneTransaction.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/IffFile.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
using namespace FSO::Files;
using Tx=LotRezoneTransaction;
namespace fs=std::filesystem;
// No IffFile is constructed in this fixture: its open-reader registry is empty.
#ifdef ODH_ISOLATED_JOURNAL_TEST
int FSO::Files::Formats::IFF::IffFile::DetachSource(const std::string&){return 0;}
#endif
using Bytes=std::vector<uint8_t>;
Bytes iff(unsigned x){FSO::Files::Formats::IFF::IffRawFile f;f.Set("TEST",1,{uint8_t(x)});return f.Write();}
Bytes read(const fs::path& p){Bytes b;bool exists=false;std::string e;assert(Tx::Read(p.string(),b,exists,e));assert(exists);return b;}
void put(const fs::path& p,const Bytes& b){std::string e;assert(SafeFile::Write(p.string(),b,&e));}
int main(){
 // Never accept a profile path. Atomically create a fresh, exclusively owned
 // temporary sandbox, and clean only that sandbox on every exit path.
 fs::path sandbox;bool created=false;
 const auto nonce=std::chrono::high_resolution_clock::now().time_since_epoch().count();
 for(unsigned i=0;i<32&&!created;++i){
  sandbox=fs::temp_directory_path()/("odh-rezone-fixture-"+std::to_string(nonce)+"-"+std::to_string(i));
  std::error_code ec;created=fs::create_directory(sandbox,ec);
  if(ec&&ec!=std::errc::file_exists)throw std::runtime_error("Cannot create exclusive converter fixture");
 }
 if(!created)throw std::runtime_error("Cannot reserve converter fixture");
 struct Cleanup {fs::path owned;~Cleanup(){std::error_code ec;fs::remove_all(owned,ec);}} cleanup{sandbox};
 fs::path root=sandbox/"UserData";fs::create_directories(root/"Houses");std::string error;
 auto reset=[&]{for(const auto& it:fs::directory_iterator(root))if(it.is_regular_file())fs::remove(it.path());for(const auto& it:fs::directory_iterator(root/"Houses"))fs::remove(it.path());put(root/"Neighborhood.iff",iff(1));put(root/"Houses/House56.iff",iff(2));};
 auto changes=[&]{std::vector<Tx::Change> v;for(unsigned slot=1;slot<=3;++slot){Tx::Change c;assert(Tx::Capture(root.string(),56,slot,iff(10+slot),c,error));v.push_back(c);}return v;};
 auto old=[&]{assert(read(root/"Neighborhood.iff")==iff(1));assert(read(root/"Houses/House56.iff")==iff(2));assert(!fs::exists(root/"LotZoning.iff"));assert(!fs::exists(root/".odh-rezone-journal"));};
 unsigned cases=0;
 for(unsigned fail=1;fail<=4;++fail){reset();auto v=changes();assert(!Tx::Commit(root.string(),56,v,error,[&](unsigned step){return step==fail;}));old();++cases;}
 for(unsigned crash=1;crash<=4;++crash){reset();auto v=changes();try{Tx::Commit(root.string(),56,v,error,[&](unsigned step)->bool{if(step==crash)throw std::runtime_error("crash");return false;});assert(false);}catch(const std::runtime_error&){}assert(fs::exists(root/".odh-rezone-journal"));assert(Tx::Recover(root.string(),error));old();++cases;}
 reset();auto v=changes();assert(Tx::Commit(root.string(),56,v,error));for(auto& c:v)assert(read(root/Tx::Relative(c.Slot,56))==c.After);assert(!fs::exists(root/".odh-rezone-journal"));++cases;
 // Actorless vacant lots do not touch Neighborhood.iff; a template-only
 // house is published as a private override and can also roll back to absence.
 reset();v=changes();v.erase(v.begin());assert(Tx::Commit(root.string(),56,v,error));
 assert(read(root/"Neighborhood.iff")==iff(1));assert(read(root/"Houses/House56.iff")==iff(12));++cases;
 reset();fs::remove(root/"Houses/House56.iff");v=changes();v.erase(v.begin());
 assert(!Tx::Commit(root.string(),56,v,error,[](unsigned n){return n==2;}));
 assert(!fs::exists(root/"Houses/House56.iff")&&!fs::exists(root/"LotZoning.iff"));assert(read(root/"Neighborhood.iff")==iff(1));++cases;
 // Capture a real pending journal, then exercise the writer's rename gaps.
 for(unsigned state=0;state<5;++state){reset();v=changes();try{Tx::Commit(root.string(),56,v,error,[](unsigned step)->bool{if(step==2)throw std::runtime_error("crash");return false;});}catch(...){}
  auto dest=root/"Neighborhood.iff";auto journal=root/".odh-rezone-journal";
  if(state==0){fs::rename(dest,dest.string()+".bak");put(dest.string()+".new",iff(11));}
  if(state==1){fs::rename(dest,dest.string()+".bak");put(dest.string()+".new",Bytes{1,2,3});}
  if(state==2)fs::rename(journal,journal.string()+".new");
  if(state==3)fs::rename(journal,journal.string()+".bak");
  if(state==4){put(journal.string()+".new",Bytes{1,2,3});}
  assert(Tx::Recover(root.string(),error));old();++cases;
 }
 // Commit marker under .new: files were published, marker flush completed,
 // rename was interrupted. Recovery must retain the new transaction.
 reset();v=changes();try{Tx::Commit(root.string(),56,v,error,[](unsigned n)->bool{if(n==4)throw std::runtime_error("crash");return false;});}catch(...){}
 {auto b=read(root/".odh-rezone-journal");uint64_t hash=1469598103934665603ULL;for(auto x:b){hash^=x;hash*=1099511628211ULL;}Bytes marker={'O','D','H','C',1};for(unsigned n=0;n<8;++n)marker.push_back(uint8_t(hash>>(n*8)));put(root/".odh-rezone-commit.new",marker);}
 bool committed=false;assert(Tx::Recover(root.string(),error,&committed)&&committed);for(auto& c:v)assert(read(root/Tx::Relative(c.Slot,56))==c.After);++cases;
 // Manual edit after an interrupted transaction blocks rollback, retaining it.
 reset();v=changes();try{Tx::Commit(root.string(),56,v,error,[](unsigned n)->bool{if(n==2)throw std::runtime_error("crash");return false;});}catch(...){}
 put(root/"Neighborhood.iff",iff(99));assert(!Tx::Recover(root.string(),error));assert(read(root/"Neighborhood.iff")==iff(99));assert(fs::exists(root/".odh-rezone-journal"));++cases;
 reset();v=changes();put(root/".odh-rezone-journal",{0,1,2});assert(!Tx::Recover(root.string(),error));assert(fs::exists(root/".odh-rezone-journal"));++cases;
 reset();fs::path outside=root.parent_path()/"journal-outside.iff";put(outside,iff(77));fs::create_symlink(outside,root/"LotZoning.iff.new");Tx::Change c;assert(!Tx::Capture(root.string(),56,3,iff(3),c,error));assert(read(outside)==iff(77));++cases;
 fs::remove(root/"LotZoning.iff.new");fs::remove(outside);
 std::cout<<cases<<" journal commit/rollback/restart/rename-gap/conflict/symlink cases passed; actual SafeFile/FilePool, empty IffFile registry adapter\n";
}
