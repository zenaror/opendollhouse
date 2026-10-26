#include "odh/sound_threads.hpp"

namespace odh {
namespace {
struct Operation {
    bool& busy;
    explicit Operation(bool& value):busy(value) {
        if(busy)throw FormatError("sound collection callback reentrancy");
        busy=true;
    }
    ~Operation(){busy=false;}
};
}
SoundThreadInstance::SoundThreadInstance(std::unique_ptr<HitThread> thread,
    std::unique_ptr<SoundThreadLifetime> lifetime)
    : thread_(std::move(thread)),lifetime_(std::move(lifetime)) {
    if(!thread_ || !lifetime_) {
        if(lifetime_)lifetime_->stop();
        throw FormatError("sound instance requires thread and lifetime owner");
    }
}
SoundThreadInstance::~SoundThreadInstance(){lifetime_->stop();thread_.reset();}
HitThreadTick SoundThreadInstance::tick(){return thread_->tick();}

SoundThreadCollection::SoundThreadCollection(const World& world,SoundThreadFactory factory,
    SoundThreadCollectionLimits limits):world_(world),factory_(std::move(factory)),limits_(limits) {
    if(!factory_ || !limits.threads || limits.threads>4096 ||
       !limits.source_fields || limits.source_fields>32769)
        throw FormatError("invalid sound collection factory or limits");
    entries_.reserve(limits.threads);
}
SoundThreadCollection::~SoundThreadCollection(){busy_=true;entries_.clear();}
bool SoundThreadCollection::submit(const SoundRequest& request) {
    Operation operation(busy_);
    if(request.loop || entries_.size()>=limits_.threads)return false;
    const auto binding=SoundSourceBinding::bind(world_,request,limits_.source_fields);
    if(!binding)return false;
    auto instance=factory_(request,*binding);
    // The factory can cause World mutations while resolving resources.
    if(!instance || !binding->valid())return false;
    entries_.push_back({*binding,request.source,request.source_variables->generation,std::move(instance)});
    return true;
}
std::size_t SoundThreadCollection::stop_owner(std::int16_t source,std::uint64_t generation) {
    Operation operation(busy_);
    std::size_t removed=0;
    for(std::size_t i=0;i<entries_.size();) {
        if(entries_[i].source==source && entries_[i].generation==generation) {
            entries_.erase(entries_.begin()+static_cast<std::ptrdiff_t>(i));++removed;
        } else ++i;
    }
    return removed;
}
void SoundThreadCollection::clear() {
    Operation operation(busy_);
    entries_.clear();
}
SoundThreadTickReport SoundThreadCollection::tick() {
    Operation operation(busy_);
    SoundThreadTickReport result;
    for(std::size_t i=0;i<entries_.size();) {
        auto& entry=entries_[i];
        bool remove=false;
        if(!entry.binding.valid()) { ++result.owner_invalid;remove=true; }
        else {
            bool completed=false,faulted=false;
            try {
                completed=entry.instance->tick()==HitThreadTick::complete;
            }catch(const std::exception& error) {
                faulted=true;
                std::string diagnostic=error.what();if(diagnostic.size()>1024)diagnostic.resize(1024);
                result.failures.push_back({entry.source,entry.generation,std::move(diagnostic)});
            }catch(...) {
                faulted=true;result.failures.push_back({entry.source,entry.generation,"nonstandard sound thread failure"});
            }
            // A backend callback may remove the owner during this tick.
            if(!entry.binding.valid()){++result.owner_invalid;remove=true;}
            else if(faulted){++result.faulted;remove=true;}
            else if(completed){++result.completed;remove=true;}
        }
        if(remove)entries_.erase(entries_.begin()+static_cast<std::ptrdiff_t>(i));
        else ++i;
    }
    result.active=entries_.size();
    return result;
}
}
