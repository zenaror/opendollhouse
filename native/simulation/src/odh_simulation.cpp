#include "odh_simulation.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace {

struct CapacityError {};
struct CallbackError {};

struct Budget {
    std::uint64_t limit;
    std::uint64_t live;
    std::uint64_t peak;

    explicit Budget(std::uint64_t max, std::uint64_t initial)
        : limit(max), live(initial), peak(initial) {}

    void require(std::size_t count, std::size_t unit) const {
        if (count > std::numeric_limits<std::size_t>::max() / unit)
            throw CapacityError{};
        const auto bytes = static_cast<std::uint64_t>(count * unit);
        if (bytes > limit - live) throw CapacityError{};
    }

    void add(std::size_t bytes) {
        live += static_cast<std::uint64_t>(bytes);
        if (live > peak) peak = live;
    }

    void remove(std::size_t bytes) { live -= static_cast<std::uint64_t>(bytes); }
};

template <typename T> struct CountedAllocator {
    using value_type = T;
    Budget* budget;

    explicit CountedAllocator(Budget* value = nullptr) noexcept : budget(value) {}
    template <typename U>
    CountedAllocator(const CountedAllocator<U>& other) noexcept : budget(other.budget) {}

    T* allocate(std::size_t count) {
        budget->require(count, sizeof(T));
        T* result = std::allocator<T>{}.allocate(count);
        budget->add(count * sizeof(T));
        return result;
    }
    void deallocate(T* ptr, std::size_t count) noexcept {
        std::allocator<T>{}.deallocate(ptr, count);
        budget->remove(count * sizeof(T));
    }
    template <typename U> bool operator==(const CountedAllocator<U>& other) const noexcept {
        return budget == other.budget;
    }
    template <typename U> bool operator!=(const CountedAllocator<U>& other) const noexcept {
        return !(*this == other);
    }
};

struct CallbackScope;
struct List;

} // namespace

struct odh_wake_queue {
    using ListPtr = std::shared_ptr<List>;
    using Pair = std::pair<const std::uint32_t, ListPtr>;
    using Schedule = std::map<std::uint32_t, ListPtr, std::less<std::uint32_t>, CountedAllocator<Pair>>;

    void* context;
    odh_wake_lookup_id_fn lookup_id;
    odh_wake_retain_fn retain;
    odh_wake_release_fn release;
    std::uint32_t max_lists;
    std::uint32_t max_entries;
    bool in_callback = false;
    std::uint32_t live_lists = 0;
    std::uint32_t live_entries = 0;
    std::uint32_t peak_entries = 0;
    std::uint32_t peak_scheduled_buckets = 0;
    Budget budget;
    Schedule schedule;
    ListPtr current;

    odh_wake_queue(void* ctx, odh_wake_lookup_id_fn id,
                   odh_wake_retain_fn retain_fn, odh_wake_release_fn release_fn,
                   std::uint32_t lists, std::uint32_t entries, std::uint64_t bytes)
        : context(ctx), lookup_id(id), retain(retain_fn), release(release_fn),
          max_lists(lists), max_entries(entries), budget(bytes, sizeof(odh_wake_queue)),
          schedule(std::less<std::uint32_t>(), CountedAllocator<Pair>(&budget)) {}

    ~odh_wake_queue() {
        schedule.clear();
        current.reset();
    }

    std::int16_t id_of(std::uint64_t token);
    void retain_token(std::uint64_t token);
    void release_token(std::uint64_t token);
    void added_entry() {
        ++live_entries;
        if (live_entries > peak_entries) peak_entries = live_entries;
    }
    void removed_entry() { --live_entries; }
    std::int32_t schedule_token(std::uint64_t token, std::uint32_t tick);
    std::int32_t schedule_current(std::uint64_t token);
    std::int32_t deschedule(std::uint64_t token, std::uint32_t end, std::uint32_t now);
    std::int32_t prepare_interrupt(std::uint64_t token, std::uint32_t end,
                                   std::uint32_t now, std::int16_t current_id,
                                   std::uint32_t* target);
};

namespace {

struct CallbackScope {
    odh_wake_queue* queue;
    explicit CallbackScope(odh_wake_queue* value) : queue(value) { queue->in_callback = true; }
    ~CallbackScope() { queue->in_callback = false; }
};

struct List {
    odh_wake_queue* owner;
    std::vector<std::uint64_t, CountedAllocator<std::uint64_t>> tokens;
    bool committed = false;

    explicit List(odh_wake_queue* queue)
        : owner(queue), tokens(CountedAllocator<std::uint64_t>(&queue->budget)) {}

    ~List() {
        if (committed) {
            owner->live_entries -= static_cast<std::uint32_t>(tokens.size());
            --owner->live_lists;
        }
        for (const auto token : tokens) owner->release_token(token);
    }

    std::int32_t find_insert_position(std::uint64_t token, std::size_t* position) {
        if (tokens.empty()) { *position = 0; return ODH_WAKE_OK; }
        const auto id = owner->id_of(token);
        std::size_t max = tokens.size();
        std::size_t min = 0;
        while (max > min) {
            const auto mid = (max + min) / 2;
            const auto mid_id = owner->id_of(tokens[mid]);
            if (id < mid_id) max = mid;
            else if (id == mid_id) return ODH_WAKE_NOT_FOUND; // Duplicate ID: no insertion.
            else min = mid + 1;
        }
        *position = min;
        return ODH_WAKE_OK;
    }

    void insert(std::size_t position, std::uint64_t token) {
        owner->retain_token(token);
        try {
            tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(position), token);
        } catch (...) {
            owner->release_token(token);
            throw;
        }
        if (committed) owner->added_entry();
    }

    void remove_first(std::uint64_t token) {
        for (auto it = tokens.begin(); it != tokens.end(); ++it) {
            if (*it == token) {
                tokens.erase(it);
                if (committed) owner->removed_entry();
                owner->release_token(token);
                break;
            }
        }
    }
};

template <typename Fn>
std::int32_t guarded(odh_wake_queue* queue, Fn&& operation) {
    if (!queue) return ODH_WAKE_INVALID_ARGUMENT;
    if (queue->in_callback) return ODH_WAKE_REENTRANT;
    try {
        return operation();
    } catch (const CapacityError&) {
        return ODH_WAKE_CAPACITY;
    } catch (const CallbackError&) {
        return ODH_WAKE_CALLBACK_FAILED;
    } catch (const std::bad_alloc&) {
        return ODH_WAKE_CAPACITY;
    } catch (...) {
        return ODH_WAKE_INTERNAL;
    }
}

} // namespace

std::int16_t odh_wake_queue::id_of(std::uint64_t token) {
    std::int16_t id = 0;
    const CallbackScope scope(this);
    if (lookup_id(context, token, &id) != 0) throw CallbackError{};
    return id;
}

void odh_wake_queue::retain_token(std::uint64_t token) {
    const CallbackScope scope(this);
    retain(context, token);
}

void odh_wake_queue::release_token(std::uint64_t token) {
    const CallbackScope scope(this);
    release(context, token);
}

std::int32_t odh_wake_queue::schedule_token(std::uint64_t token, std::uint32_t tick) {
    auto found = schedule.find(tick);
    if (found != schedule.end()) {
        std::size_t position = 0;
        if (found->second->find_insert_position(token, &position) == ODH_WAKE_NOT_FOUND)
            return ODH_WAKE_OK;
        if (live_entries >= max_entries) return ODH_WAKE_CAPACITY;
        found->second->insert(position, token);
        return ODH_WAKE_OK;
    }
    if (live_lists >= max_lists || live_entries >= max_entries)
        return ODH_WAKE_CAPACITY;
    auto list = std::allocate_shared<List>(CountedAllocator<List>(&budget), this);
    list->insert(0, token);
    schedule.emplace(tick, list);
    list->committed = true;
    ++live_lists;
    added_entry();
    if (schedule.size() > peak_scheduled_buckets)
        peak_scheduled_buckets = static_cast<std::uint32_t>(schedule.size());
    return ODH_WAKE_OK;
}

std::int32_t odh_wake_queue::schedule_current(std::uint64_t token) {
    if (!current) return ODH_WAKE_NO_CURRENT;
    std::size_t position = 0;
    if (current->find_insert_position(token, &position) == ODH_WAKE_NOT_FOUND)
        return ODH_WAKE_OK;
    if (live_entries >= max_entries) return ODH_WAKE_CAPACITY;
    current->insert(position, token);
    return ODH_WAKE_OK;
}

std::int32_t odh_wake_queue::deschedule(
    std::uint64_t token, std::uint32_t end, std::uint32_t now) {
    if (end <= now) return ODH_WAKE_OK;
    const auto found = schedule.find(end);
    if (found == schedule.end()) return ODH_WAKE_NOT_FOUND;
    found->second->remove_first(token);
    return ODH_WAKE_OK;
}

std::int32_t odh_wake_queue::prepare_interrupt(
    std::uint64_t token, std::uint32_t end, std::uint32_t now,
    std::int16_t current_id, std::uint32_t* target) {
    if (end == 0 || end == now) return ODH_WAKE_OK;
    if (end > now && schedule.find(end) == schedule.end()) return ODH_WAKE_NOT_FOUND;
    const auto id = id_of(token); // Callback failure before removing any occurrence.
    const auto chosen = id > current_id && current
        ? ODH_WAKE_TARGET_CURRENT : ODH_WAKE_TARGET_NEXT;
    if (end > now) {
        const auto status = deschedule(token, end, now);
        if (status != ODH_WAKE_OK) return status;
    }
    *target = chosen;
    return ODH_WAKE_OK;
}

extern "C" std::uint32_t ODH_SIM_CALL odh_wake_abi_version(void) { return 1; }

extern "C" std::int32_t ODH_SIM_CALL odh_wake_create_v1(
    void* context, odh_wake_lookup_id_fn lookup_id, odh_wake_retain_fn retain,
    odh_wake_release_fn release, std::uint32_t max_buckets,
    std::uint32_t max_entries, std::uint64_t max_bytes,
    odh_wake_queue** out_queue) {
    if (out_queue) *out_queue = nullptr;
    if (!out_queue || !lookup_id || !retain || !release)
        return ODH_WAKE_INVALID_ARGUMENT;
    if (max_bytes < sizeof(odh_wake_queue)) return ODH_WAKE_CAPACITY;
    try {
        *out_queue = new odh_wake_queue(context, lookup_id, retain, release,
                                        max_buckets, max_entries, max_bytes);
        return ODH_WAKE_OK;
    } catch (const std::bad_alloc&) {
        return ODH_WAKE_CAPACITY;
    } catch (...) {
        return ODH_WAKE_INTERNAL;
    }
}

extern "C" std::int32_t ODH_SIM_CALL odh_wake_destroy_v1(odh_wake_queue* queue) {
    return guarded(queue, [queue] {
        delete queue;
        return ODH_WAKE_OK;
    });
}

extern "C" std::int32_t ODH_SIM_CALL odh_wake_schedule_v1(
    odh_wake_queue* queue, std::uint64_t token, std::uint32_t tick) {
    return guarded(queue, [queue, token, tick]() -> std::int32_t {
        if (!token) return ODH_WAKE_INVALID_ARGUMENT;
        return queue->schedule_token(token, tick);
    });
}

extern "C" std::int32_t ODH_SIM_CALL odh_wake_schedule_current_v1(
    odh_wake_queue* queue, std::uint64_t token) {
    return guarded(queue, [queue, token]() -> std::int32_t {
        if (!token) return ODH_WAKE_INVALID_ARGUMENT;
        return queue->schedule_current(token);
    });
}

extern "C" std::int32_t ODH_SIM_CALL odh_wake_deschedule_v1(
    odh_wake_queue* queue, std::uint64_t token,
    std::uint32_t scheduled_end, std::uint32_t current_tick) {
    return guarded(queue, [queue, token, scheduled_end, current_tick]() -> std::int32_t {
        if (!token) return ODH_WAKE_INVALID_ARGUMENT;
        return queue->deschedule(token, scheduled_end, current_tick);
    });
}

extern "C" std::int32_t ODH_SIM_CALL odh_wake_prepare_interrupt_v1(
    odh_wake_queue* queue, std::uint64_t token, std::uint32_t scheduled_end,
    std::uint32_t current_tick, std::int16_t current_object_id,
    std::uint32_t* target) {
    if (target) *target = ODH_WAKE_TARGET_NONE;
    return guarded(queue, [=]() -> std::int32_t {
        if (!token || !target) return ODH_WAKE_INVALID_ARGUMENT;
        return queue->prepare_interrupt(token, scheduled_end, current_tick,
                                        current_object_id, target);
    });
}

extern "C" std::int32_t ODH_SIM_CALL odh_wake_begin_v1(
    odh_wake_queue* queue, std::uint32_t tick, std::uint32_t previous_tick) {
    return guarded(queue, [=] {
        if (previous_tick != 0) return ODH_WAKE_OK;
        const auto first = queue->schedule.find(1);
        if (first == queue->schedule.end() || tick == 1) return ODH_WAKE_OK;
        auto node = queue->schedule.extract(first);
        queue->schedule.erase(tick); // The original overwrites this bucket.
        node.key() = tick;
        queue->schedule.insert(std::move(node));
        return ODH_WAKE_OK;
    });
}

extern "C" std::int32_t ODH_SIM_CALL odh_wake_select_current_v1(
    odh_wake_queue* queue, std::uint32_t tick, std::uint32_t* found) {
    if (found) *found = 0;
    return guarded(queue, [=] {
        if (!found) return ODH_WAKE_INVALID_ARGUMENT;
        const auto match = queue->schedule.find(tick);
        queue->current = match == queue->schedule.end() ? nullptr : match->second;
        *found = queue->current ? 1U : 0U;
        return ODH_WAKE_OK;
    });
}

extern "C" std::int32_t ODH_SIM_CALL odh_wake_current_count_v1(
    odh_wake_queue* queue, std::uint32_t* count) {
    if (count) *count = 0;
    return guarded(queue, [=] {
        if (!count) return ODH_WAKE_INVALID_ARGUMENT;
        if (!queue->current) return ODH_WAKE_NO_CURRENT;
        *count = static_cast<std::uint32_t>(queue->current->tokens.size());
        return ODH_WAKE_OK;
    });
}

extern "C" std::int32_t ODH_SIM_CALL odh_wake_current_at_v1(
    odh_wake_queue* queue, std::uint32_t index, std::uint64_t* token) {
    if (token) *token = 0;
    return guarded(queue, [=] {
        if (!token) return ODH_WAKE_INVALID_ARGUMENT;
        if (!queue->current) return ODH_WAKE_NO_CURRENT;
        if (index >= queue->current->tokens.size()) return ODH_WAKE_NOT_FOUND;
        *token = queue->current->tokens[index];
        return ODH_WAKE_OK;
    });
}

extern "C" std::int32_t ODH_SIM_CALL odh_wake_complete_v1(
    odh_wake_queue* queue, std::uint32_t tick) {
    return guarded(queue, [=] {
        queue->schedule.erase(tick);
        return ODH_WAKE_OK;
    });
}

extern "C" std::int32_t ODH_SIM_CALL odh_wake_reset_v1(odh_wake_queue* queue) {
    return guarded(queue, [=] {
        queue->schedule.clear();
        return ODH_WAKE_OK;
    });
}

extern "C" std::int32_t ODH_SIM_CALL odh_wake_stats_v1(
    odh_wake_queue* queue, std::uint32_t* scheduled_buckets,
    std::uint32_t* peak_scheduled_buckets, std::uint32_t* live_lists,
    std::uint32_t* live_entries, std::uint32_t* peak_entries,
    std::uint32_t* current_alias, std::uint64_t* live_bytes,
    std::uint64_t* peak_bytes) {
    if (scheduled_buckets) *scheduled_buckets = 0;
    if (peak_scheduled_buckets) *peak_scheduled_buckets = 0;
    if (live_lists) *live_lists = 0;
    if (live_entries) *live_entries = 0;
    if (peak_entries) *peak_entries = 0;
    if (current_alias) *current_alias = 0;
    if (live_bytes) *live_bytes = 0;
    if (peak_bytes) *peak_bytes = 0;
    return guarded(queue, [=] {
        if (!scheduled_buckets || !peak_scheduled_buckets || !live_lists ||
            !live_entries || !peak_entries || !current_alias || !live_bytes ||
            !peak_bytes) return ODH_WAKE_INVALID_ARGUMENT;
        *scheduled_buckets = static_cast<std::uint32_t>(queue->schedule.size());
        *peak_scheduled_buckets = queue->peak_scheduled_buckets;
        *live_lists = queue->live_lists;
        *live_entries = queue->live_entries;
        *peak_entries = queue->peak_entries;
        *current_alias = queue->current ? 1U : 0U;
        *live_bytes = queue->budget.live;
        *peak_bytes = queue->budget.peak;
        return ODH_WAKE_OK;
    });
}
