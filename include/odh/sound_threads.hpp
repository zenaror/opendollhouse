#pragma once
#include "odh/sound.hpp"
#include "odh/hit_thread.hpp"

namespace odh {
// Owns the backend resources used by one HIT thread. stop must cancel all its
// notes, be idempotent, nonthrowing and never reenter the collection. Destructor releases
// resources; it must also be safe if factory construction fails.
struct SoundThreadLifetime {
    virtual ~SoundThreadLifetime() = default;
    virtual void stop() noexcept = 0;
};
class SoundThreadInstance {
public:
    SoundThreadInstance(std::unique_ptr<HitThread>, std::unique_ptr<SoundThreadLifetime>);
    ~SoundThreadInstance();
    SoundThreadInstance(const SoundThreadInstance&) = delete;
    SoundThreadInstance& operator=(const SoundThreadInstance&) = delete;
    HitThreadTick tick();
private:
    std::unique_ptr<HitThread> thread_;
    std::unique_ptr<SoundThreadLifetime> lifetime_;
};
using SoundThreadFactory = std::function<std::unique_ptr<SoundThreadInstance>(
    const SoundRequest&, const SoundSourceBinding&)>;
struct SoundThreadCollectionLimits { std::size_t threads = 64, source_fields = 256; };
struct SoundThreadFault {
    std::int16_t source=0;
    std::uint64_t generation=0;
    std::string diagnostic; // At most 1024 bytes, no retry of the failed thread.
};
struct SoundThreadTickReport {
    std::size_t active = 0, completed = 0, owner_invalid = 0, faulted = 0;
    std::vector<SoundThreadFault> failures; // Bounded by admitted thread capacity.
};
// Single-source, nonlooping threads. World/HIT resources and referenced factory
// inputs must outlive this collection. Single owner, no callback reentrancy.
// No visibility filter, clock advancement, BHAV waiting or implicit arguments.
class SoundThreadCollection {
public:
    SoundThreadCollection(const World&, SoundThreadFactory, SoundThreadCollectionLimits = {});
    SoundThreadCollection(const SoundThreadCollection&) = delete;
    SoundThreadCollection& operator=(const SoundThreadCollection&) = delete;
    ~SoundThreadCollection();
    // Bind before factory; reserve capacity before acquiring audio. A factory
    // rejection returns false. Exceptions propagate and acquired instances
    // release resources. No HIT instructions execute during submit.
    bool submit(const SoundRequest&);
    // One tick per surviving entry. Invalid owners stop before HIT executes;
    // faults are counted separately from completion, with immediate cleanup.
    SoundThreadTickReport tick();
    // Cancel exact submission incarnation without ticking any HIT thread.
    std::size_t stop_owner(std::int16_t source, std::uint64_t generation);
    void clear();
    std::size_t size() const { return entries_.size(); }
private:
    struct Entry { SoundSourceBinding binding; std::int16_t source; std::uint64_t generation;
                   std::unique_ptr<SoundThreadInstance> instance; };
    const World& world_;
    SoundThreadFactory factory_;
    SoundThreadCollectionLimits limits_;
    std::vector<Entry> entries_;
    bool busy_ = false;
};
}
