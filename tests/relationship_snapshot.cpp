#include "odh/relationship.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace { int allocation_countdown=-1; }
void* operator new(std::size_t size) {
    if (allocation_countdown==0) {allocation_countdown=-1;throw std::bad_alloc();}
    if (allocation_countdown>0) --allocation_countdown;
    if (auto* p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new(std::size_t size,const std::nothrow_t&) noexcept {
    try {return ::operator new(size);} catch (...) {return nullptr;}
}
void operator delete(void* p,const std::nothrow_t&) noexcept {std::free(p);}
namespace {
int checks=0;
void require(bool value,const char* why) {++checks;if (!value) throw std::runtime_error(why);}
bool same(const odh::RelationshipSnapshot& a,const odh::RelationshipSnapshot& b) {
    if (a.limits.sources!=b.limits.sources || a.limits.pairs!=b.limits.pairs ||
        a.limits.values!=b.limits.values || a.pair_count!=b.pair_count ||
        a.value_count!=b.value_count || a.sources.size()!=b.sources.size()) return false;
    for (std::size_t i=0;i<a.sources.size();++i) {
        const auto& x=a.sources[i];const auto& y=b.sources[i];
        if (x.matrix!=y.matrix || x.id!=y.id || x.generation!=y.generation || x.pairs.size()!=y.pairs.size()) return false;
        for (std::size_t j=0;j<x.pairs.size();++j)
            if (x.pairs[j].target!=y.pairs[j].target || x.pairs[j].values!=y.pairs[j].values) return false;
    }
    return true;
}
odh::NeighborEntry neighbor(std::int16_t id,std::vector<odh::NeighborRelationship> pairs={}) {
    odh::NeighborRecord record{};record.version=4;record.neighbor_id=id;record.relationships=std::move(pairs);
    return {1,std::move(record)};
}
void semantics_and_validation() {
    odh::NeighborContext neighbors(odh::Nbrs{0,62,{
        neighbor(1,{{1,70000,std::vector<std::int32_t>(300,-1)},
                    {1,std::numeric_limits<std::int32_t>::min(),{}},
                    {1,std::numeric_limits<std::int32_t>::max(),{32767}}}),
        neighbor(-7,{{1,65535,{-32768}}}),neighbor(0)},0});
    odh::RelationshipState source(neighbors,{8,16,1000});
    odh::World world;
    require(world.create(1,odh::EntityKind::object)&&world.create(2,odh::EntityKind::object),"World object fixture");
    const auto old_generation=world.find(1)->generation;
    require(source.import_object_relationships(world,{{1,{{2,{101}},{65535,{}},{0,{303}}}},{2,{}}}),"initial OBJECT matrix");
    require(world.remove(1)&&world.create(1,odh::EntityKind::object)&&world.find(1)->generation!=old_generation,
            "reuse object ID with new generation");
    require(source.import_object_relationships(world,{{1,{{2,{202}}}}}),"retain both object generations");
    const auto snapshot=source.snapshot();
    require(snapshot.sources.size()==6&&snapshot.pair_count==8&&snapshot.value_count==305,"exact counts include tails and empty sources/pairs");
    odh::RelationshipState restored;
    require(restored.restore_snapshot(snapshot)&&same(snapshot,restored.snapshot()),"all owned values and limits restored");
    require(restored.value(1,70000,299)==-1&&restored.value(-7,65535,0)==-32768&&
            restored.value(1,std::numeric_limits<std::int32_t>::max(),0)==32767,
            "raw signed32 neighbor targets and tails beyond byte255 survive");
    require(restored.object_value(world,1,2,0)==202,"live generation selects current OBJECT source");
    bool old_found=false,empty_object=false,zero_neighbor=false;
    for (const auto& entry : restored.snapshot().sources) {
        if (entry.matrix==odh::RelationshipMatrix::object&&entry.id==1&&entry.generation==old_generation)
            old_found=entry.pairs.size()==3&&entry.pairs[1].values==std::vector<std::int16_t>{101};
        if (entry.matrix==odh::RelationshipMatrix::object&&entry.id==2) empty_object=entry.pairs.empty();
        if (entry.matrix==odh::RelationshipMatrix::neighbor&&entry.id==0) zero_neighbor=entry.pairs.empty();
    }
    require(old_found&&empty_object&&zero_neighbor,"full Source key and empty bindings retained");
    auto unordered=snapshot;
    std::reverse(unordered.sources.begin(),unordered.sources.end());
    for (auto& entry : unordered.sources) std::reverse(entry.pairs.begin(),entry.pairs.end());
    odh::RelationshipState reordered;
    require(reordered.restore_snapshot(unordered)&&same(snapshot,reordered.snapshot()),"input ordering does not replace key identity");
    auto independent=snapshot;
    independent.sources[0].pairs[0].values[0]=55;
    require(same(snapshot,source.snapshot())&&same(snapshot,restored.snapshot()),"snapshot vectors own independent copies");
    odh::VmFrame frame{};frame.context={1,1,2,0};
    const odh::Instruction write{26,254,255,{0,0,68,0,7,0,77,0}};
    require(restored.primitive(world)(frame,write,{},{}).disposition==odh::PrimitiveDisposition::goto_true&&
            restored.object_value(world,1,2,0)==77&&source.object_value(world,1,2,0)==202,
            "restored mutations do not alias source or snapshot");
    require(same(snapshot,source.snapshot()),"runtime write leaves captured snapshot unchanged");
    require(!restored.restore_snapshot(snapshot),"populated destination rejects replacement");
    const auto reject=[&](const odh::RelationshipSnapshot& bad,odh::RelationshipLimits limits=odh::RelationshipLimits{}) {
        odh::RelationshipState target(limits);const auto before=target.snapshot();
        require(!target.restore_snapshot(bad)&&same(before,target.snapshot()),"invalid snapshot rejected atomically");
        require(target.restore_snapshot(snapshot),"rejected restore leaves destination fresh");
    };
    auto bad=snapshot;bad.sources[0].matrix=static_cast<odh::RelationshipMatrix>(99);reject(bad);
    bad=snapshot;bad.sources[0].generation=1;reject(bad);
    const auto object_index=std::size_t{3};
    bad=snapshot;bad.sources[object_index].id=0;reject(bad);
    bad=snapshot;bad.sources[object_index].id=-1;reject(bad);
    bad=snapshot;bad.sources[object_index].generation=0;reject(bad);
    bad=snapshot;bad.sources[object_index].pairs[0].target=-1;reject(bad);
    bad=snapshot;bad.sources[object_index].pairs[0].target=65536;reject(bad);
    bad=snapshot;bad.sources.push_back(bad.sources.back());
    bad.pair_count+=bad.sources.back().pairs.size();reject(bad);
    bad=snapshot;bad.sources[object_index].pairs.push_back(bad.sources[object_index].pairs[0]);
    ++bad.pair_count;++bad.value_count;reject(bad);
    bad=snapshot;++bad.pair_count;reject(bad);
    bad=snapshot;++bad.value_count;reject(bad);
    bad=snapshot;bad.limits.sources=snapshot.sources.size()-1;reject(bad);
    bad=snapshot;bad.limits.pairs=snapshot.pair_count-1;reject(bad);
    bad=snapshot;bad.limits.values=snapshot.value_count-1;reject(bad);
    for (const auto caps : {odh::RelationshipLimits{7,16,1000},odh::RelationshipLimits{8,15,1000},
                            odh::RelationshipLimits{8,16,999}}) {
        odh::RelationshipState capped(caps);const auto before=capped.snapshot();
        require(!capped.restore_snapshot(snapshot)&&same(before,capped.snapshot()),"saved limits cannot exceed destination caps even if current usage fits");
    }
    auto exact=snapshot;exact.limits={snapshot.sources.size(),snapshot.pair_count,snapshot.value_count};
    odh::RelationshipState exact_state;
    require(exact_state.restore_snapshot(exact)&&same(exact,exact_state.snapshot()),"exact source/pair/value limits accepted and restored");
    require(world.create(3,odh::EntityKind::object)&&!exact_state.import_object_relationships(world,{{3,{}}}),
            "restored source limit controls subsequent writes");
    odh::RelationshipState empty;
    const auto empty_snapshot=empty.snapshot();
    odh::RelationshipState empty_restore;
    require(empty_restore.restore_snapshot(empty_snapshot)&&!empty_restore.restore_snapshot(empty_snapshot),"empty restore still consumes destination freshness");
    odh::NeighborContext no_neighbors(odh::Nbrs{0,62,{},0});
    odh::RelationshipState initialized_empty(no_neighbors);
    require(!initialized_empty.restore_snapshot(empty_snapshot),"already initialized empty neighbor state is not fresh");
    auto zero_limits=empty_snapshot;zero_limits.limits={0,0,0};
    odh::RelationshipState zero;
    require(zero.restore_snapshot(zero_limits)&&same(zero_limits,zero.snapshot()),"empty zero budgets preserved");
}
void allocation_atomicity() {
    const odh::RelationshipSnapshot snapshot{{4,8,1024},{
        {odh::RelationshipMatrix::neighbor,-7,0,{{-1,{17}},{70000,std::vector<std::int16_t>(300,-1)}}},
        {odh::RelationshipMatrix::object,1,9,{{0,{}},{65535,{21,22}}}},
        {odh::RelationshipMatrix::object,1,10,{}}},4,303};
    odh::RelationshipState target;
    const auto before=target.snapshot();bool complete=false;int failures=0;
    for (int fail=0;fail<128;++fail) {
        allocation_countdown=fail;bool threw=false;
        try {complete=target.restore_snapshot(snapshot);} catch (const std::bad_alloc&) {threw=true;}
        allocation_countdown=-1;
        if (complete) break;
        ++failures;
        require(threw&&same(before,target.snapshot()),"each allocation failure preserves matrix, counters, limits and freshness");
    }
    require(failures>0&&complete&&same(snapshot,target.snapshot()),"allocation sweep eventually publishes complete state");
    bool capture_complete=false;
    for (int fail=0;fail<128;++fail) {
        allocation_countdown=fail;bool threw=false;
        try {auto captured=target.snapshot();capture_complete=true;} catch (const std::bad_alloc&) {threw=true;}
        allocation_countdown=-1;
        require(same(snapshot,target.snapshot()),"capture allocation failure leaves source unchanged");
        if (capture_complete) break;
        require(threw,"capture only fails by allocation");
    }
    require(capture_complete,"capture allocation sweep completes");
}
void avatar_neighbor_branch() {
    odh::NeighborContext neighbors(odh::Nbrs{0,62,{neighbor(55)},0});
    odh::RelationshipState source(neighbors);
    odh::World world;
    require(world.create(101,odh::EntityKind::avatar,{0,32,0})&&
            world.create(202,odh::EntityKind::avatar,{0,32,0})&&
            world.set_person_data(101,31,55)&&world.set_person_data(202,31,66),"avatar neighbor mapping fixture");
    odh::VmFrame frame{};frame.context={101,101,202,0};
    const odh::Instruction write{26,254,255,{0,0,68,0,7,0,77,0}};
    std::optional<odh::RelationshipRecord> observed;
    auto primitive=source.primitive(world,{},[&](const auto& record){observed=record;});
    require(primitive(frame,write,{},{}).disposition==odh::PrimitiveDisposition::goto_true&&observed&&
            observed->matrix==odh::RelationshipMatrix::neighbor&&observed->source==55&&observed->target==66,
            "OBJECT operand with two identified avatars uses neighbor Source");
    const auto snapshot=source.snapshot();
    require(snapshot.sources.size()==1&&snapshot.sources[0].matrix==odh::RelationshipMatrix::neighbor&&
            snapshot.sources[0].id==55&&snapshot.sources[0].generation==0,"actual avatar branch retains generation-zero neighbor identity");
    odh::RelationshipState restored;
    require(restored.restore_snapshot(snapshot)&&same(snapshot,restored.snapshot())&&restored.value(55,66,0)==77,
            "actual avatar branch snapshot restores exactly");
}
}
int main() {
    try {semantics_and_validation();avatar_neighbor_branch();allocation_atomicity();std::cout<<"relationship_snapshot: "<<checks<<" checks passed (synthetic values only)\n";}
    catch (const std::exception& error) {allocation_countdown=-1;std::cerr<<"relationship_snapshot: "<<error.what()<<'\n';return 1;}
}
