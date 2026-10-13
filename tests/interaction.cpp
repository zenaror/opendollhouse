#include "odh/interaction.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
namespace {
int checks=0;
void require(bool v,const char* text){++checks;if(!v)throw std::runtime_error(text);}
odh::RoutineKey key(unsigned id){return odh::routine_key(static_cast<std::uint16_t>(id));}
odh::Instruction idle(unsigned allow=1,unsigned yes=254){return {17,static_cast<std::uint8_t>(yes),255,{0,0,static_cast<std::uint8_t>(allow),0,0,0,0,0}};}
odh::Instruction set(unsigned temp,unsigned value,unsigned yes=254){return {2,static_cast<std::uint8_t>(yes),255,{static_cast<std::uint8_t>(temp),0,static_cast<std::uint8_t>(value),0,0,5,8,7}};}
odh::Bhav bhav(std::initializer_list<odh::Instruction> i){return {0x8002,i,{},odh::Bhav::Metadata{0,4,4,0}};}
struct Fixture {
 odh::World world;odh::InteractionQueue queue;odh::Vm vm;
 Fixture(unsigned allow=1,std::size_t capacity=2,bool flags=true):queue(world,1,capacity),
 vm({{key(4096),bhav({idle(allow,1),set(2,99)})}},world.vm_memory(),{},queue.primitive(),
    {{22,{{key(4096),bhav({set(0,22)})},{key(4097),bhav({set(1,33,255)})}}}}) {
  world.create(1,odh::EntityKind::avatar);world.create(2,odh::EntityKind::object);
  if(flags)world.declare_object_word(1,8,16);
  require(queue.bind(vm),"bind registered avatar");vm.start(key(4096),{1,1,1,0},{0});
 }
 bool enqueue(unsigned id=4096){return queue.enqueue(key(id),{1,2,2,22});}
};
}
int main(int argc,char** argv){try {
 if(argc==4 && std::string(argv[1])=="--native-global") {
  std::ifstream input(argv[2],std::ios::binary);if(!input)throw std::runtime_error("native archive absent");
  odh::View archive(input,std::filesystem::file_size(argv[2]));std::optional<odh::Bhav> wrapper;bool matched=false;
  for(const auto& entry:odh::far1_index(archive))if(entry.name==argv[3]) {
   if(matched)throw std::runtime_error("ambiguous native entry");matched=true;
   odh::iff_chunks(archive.subview(entry.offset,entry.length),[&](const auto& chunk){
    if(chunk.type=="BHAV"&&chunk.id==281){if(wrapper)throw std::runtime_error("ambiguous native BHAV281");wrapper=odh::read_bhav(chunk.payload);}
   });
  }
  require(wrapper.has_value(),"native global wrapper selected explicitly");
  odh::World world;world.create(1,odh::EntityKind::avatar);world.create(2,odh::EntityKind::object);world.declare_object_word(1,8,16);
  odh::InteractionQueue queue(world,1);
  odh::Instruction invoke{281,1,1,{0,0,1,0,0,0,0,0}};
  odh::Vm vm({{key(4096),bhav({invoke,set(2,99)})},{key(281),*wrapper}},world.vm_memory(),{},queue.primitive(),
      {{22,{{key(4096),bhav({set(0,22)})}}}});
  require(queue.bind(vm),"native wrapper controller bound");vm.start(key(4096),{1,1,1,0});
  require(queue.enqueue(key(4096),{1,2,2,22})&&vm.run(30)==odh::VmStatus::yielded,"native281 AllowPush hands over to explicit owner22 action");
  require(vm.stack().size()==2&&vm.stack().back().key.id==281&&vm.stack().back().pc==4,"native wrapper same IdleForInput pc survives action return");
  require(queue.observe()&&queue.size()==0&&queue.tick(1)&&vm.run(30)==odh::VmStatus::returned_true&&vm.temps()[0]==22&&vm.temps()[2]==99,
          "native wrapper resumes next tick then returns through its own flag test");
  require(world.find(1)->object_words.at(8)==16,"native push path preserves unrelated flags");
  std::cout<<checks<<" native-wrapper ODH checks passed; action/Main fixtures synthetic\n";return 0;
 }
 if(argc!=1)throw std::runtime_error("usage: --native-global ARCHIVE ENTRY");
 {Fixture f;require(f.enqueue()&&f.enqueue(4097)&&!f.enqueue(),"FIFO capacity includes active front");
  require(f.vm.run(10)==odh::VmStatus::yielded&&f.vm.temps()[0]==22,"owner22 action executes over preserved owner0 Main");
  require(f.vm.stack().size()==1&&f.vm.stack()[0].pc==0&&f.vm.stack()[0].arguments[0]==-1,"decrement before push and unchanged Main pc after return");
  require(f.queue.observe()&&!f.queue.active()&&f.queue.size()==1&&f.queue.completion()->result,"observe removes completed front synchronously");
  require(f.vm.run(10)==odh::VmStatus::yielded&&f.vm.temps()[2]==0,"same tick cannot resume Main");
  require(f.queue.tick(0)&&f.vm.status()==odh::VmStatus::yielded,"tick zero does not consume next-tick boundary");
  require(f.queue.tick(1)&&f.vm.run(10)==odh::VmStatus::yielded&&f.vm.temps()[1]==33,"next tick reexecutes AllowPush and invokes FIFO second action");
  require(f.queue.observe()&&f.queue.size()==0&&!f.queue.completion()->result,"false action result recorded without branching Main false");
  require(!f.queue.tick(1)&&!f.queue.tick(3),"duplicate and skipped ticks rejected");
  require(f.queue.tick(2)&&f.vm.run(10)==odh::VmStatus::returned_true&&f.vm.temps()[2]==99,"Main follows own Idle true branch after queue drained");
 }
 {Fixture f;f.vm.start(key(4096),{1,1,1,0},{3});
  require(f.vm.run(10)==odh::VmStatus::waiting&&f.vm.stack()[0].arguments[0]==2,"empty AllowPush waits after decrement");
  require(f.queue.tick(1)&&f.vm.run(10)==odh::VmStatus::waiting&&f.vm.stack()[0].arguments[0]==1,"AllowPush checks queue each tick");
  require(f.enqueue()&&f.queue.tick(2)&&f.vm.run(10)==odh::VmStatus::yielded,"queue arrival wakes wait and starts action");
  require(f.queue.observe()&&f.queue.tick(3)&&f.vm.run(10)==odh::VmStatus::returned_true,"post-action parent reentry completes countdown");
 }
 {Fixture f(0);require(f.enqueue(),"request before AllowPush0");
  require(f.vm.run(10)==odh::VmStatus::returned_true&&f.queue.size()==1&&f.vm.temps()[0]==0,"AllowPush0 notifies but never dispatches action");
  require(f.world.find(1)->object_words.at(8)==80,"notification bit6 preserves other flag bits");
 }
 {Fixture f(0);f.vm.start(key(4096),{1,1,1,0},{2});
  require(f.vm.run(10)==odh::VmStatus::waiting&&f.vm.stack()[0].arguments[0]==1,"AllowPush0 future deadline");
  require(f.queue.tick(1)&&f.vm.status()==odh::VmStatus::waiting,"future idle not polled prematurely");
  require(f.queue.tick(2)&&f.vm.run(10)==odh::VmStatus::waiting&&f.vm.stack()[0].arguments[0]==0,"zero idleStart sentinel uses decrement one");
  require(f.queue.tick(3)&&f.vm.run(10)==odh::VmStatus::returned_true,"future idle completes on following deadline");
 }
 {Fixture f(0);f.vm.start(key(4096),{1,1,1,0},{8});f.vm.run(10);
  require(f.enqueue()&&f.queue.tick(1)&&f.vm.run(10)==odh::VmStatus::returned_true,"queued request wakes nonpush idle notification before deadline");
 }
 {Fixture f;f.vm.start(key(4096),{1,1,1,0},{8});f.vm.run(10);
  require(f.queue.interrupt()&&f.queue.tick(1)&&f.vm.run(10)==odh::VmStatus::returned_true,"explicit Main interrupt wakes countdown");
 }
 {Fixture f;f.enqueue();f.vm.run(10);f.queue.observe();
  require(!f.queue.enqueue(key(4096),{9,2,2,22})&&!f.queue.enqueue(key(4096),{1,9,9,22}),"wrong caller or absent target rejected");
  f.vm.start(key(4096),{1,1,1,0},{0});
  require(f.queue.bind(f.vm)&&f.queue.tick(1)&&f.queue.size()==0&&!f.queue.active()&&!f.queue.completion(),"restart invalidates events and queued state even after rebind");
 }
 {Fixture f;f.enqueue();f.world.remove(2);f.world.create(2,odh::EntityKind::object);
  require(f.vm.run(10)==odh::VmStatus::fault&&f.vm.stack().size()==1&&f.vm.stack()[0].arguments[0]==0,"recycled target generation fails before parent write/action execution");
 }
 {Fixture f;f.enqueue();require(f.vm.run(1)==odh::VmStatus::budget_exhausted&&f.queue.active(),"active action can await next budget slice");
  f.world.remove(2);f.world.create(2,odh::EntityKind::object);
  require(!f.queue.observe()&&!f.queue.tick(1)&&!f.enqueue()&&f.vm.temps()[0]==0,"active target recycle rejected before further host execution");
 }
 {Fixture f;f.world.remove(1);f.world.create(1,odh::EntityKind::avatar);
  require(!f.queue.tick(1)&&!f.enqueue()&&!f.queue.bind(f.vm),"recycled caller invalidates controller");
 }
 {Fixture f(0,2,false);f.enqueue();
  require(f.vm.run(10)==odh::VmStatus::fault&&f.vm.stack()[0].arguments[0]==0,"unknown Flags fails atomically rather than fabricate notification");
 }
 {Fixture f;f.vm.run(10);require(!f.queue.interrupt(),"terminal Main cannot be interrupted");}
 {Fixture f(2);require(f.vm.run(10)==odh::VmStatus::fault,"unknown AllowPush profile rejected");}
 {Fixture f;f.enqueue(9999);require(f.vm.run(10)==odh::VmStatus::fault&&f.vm.stack().size()==1&&f.vm.stack()[0].arguments[0]==0&&f.queue.size()==1,"missing executable remains faulted request without false completion");}
 std::cout<<checks<<" interaction checks passed (synthetic fixtures only)\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
