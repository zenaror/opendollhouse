#pragma once

#include "odh/hit_object_catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace odh {

// The factory owns construction and rollback of its concrete backend. The
// registry owns exactly one returned object per live Patch generation, even
// when a native spatial Patch would expose two interfaces to that object.
// clear_resources must be safe after a failed load and safe when called once
// during final teardown. No PCM format, voice count or raw-control scale is
// part of this interface.
class NativeHitPatchBackend {
public:
    virtual ~NativeHitPatchBackend() noexcept = default;
    virtual void load_resources() = 0;
    virtual void clear_resources() = 0;
    virtual bool active() const = 0;
    virtual void note_on() = 0;
    virtual void stop() = 0;
    virtual void raw_control(std::uint8_t id, std::int32_t raw) = 0;
};

using NativeHitPatchBackendFactory =
    std::function<std::unique_ptr<NativeHitPatchBackend>(const NativeHitPatchRef&)>;

struct NativeHitPatchRegistryLimits {
    // ODH safety budgets, not native backend or voice limits. Each dimension
    // is additionally capped at 65536 by the constructor.
    std::size_t live_patches = 64;
    std::size_t bindings = 256;
    std::size_t resource_refs = 256;
};

struct NativeHitPatchRegistryUsage {
    std::size_t live_patches = 0;
    std::size_t bindings = 0;
    std::size_t resource_refs = 0;
    std::uint64_t generations_created = 0;
    bool cleanup_failed = false; // Backend cleanup throw or reentry during cleanup.
};

class NativeHitPatchRegistry;

// A borrowed view. It never owns or caches a backend pointer. Stop on a
// retired generation is harmless; other operations reject it.
struct NativeHitPatchView {
    const NativeHitPatchRegistry* owner = nullptr;
    std::size_t slot = 0;
    std::uint64_t generation = 0;
};

// Single-threaded registry bound to an immutable global object catalog. The
// catalog, its HOT resources and factory captures must outlive the registry.
// The registry must outlive all leases; destroy owners/controllers first.
// External callbacks must not reenter the registry. Reentry is detected even
// if a callback catches the rejection. Lease cleanup during a callback is
// deferred until that callback has returned.
class NativeHitPatchRegistry {
public:
    class BindingLease {
    public:
        BindingLease() noexcept = default;
        ~BindingLease() noexcept { reset(); }
        BindingLease(const BindingLease&) = delete;
        BindingLease& operator=(const BindingLease&) = delete;
        BindingLease(BindingLease&& other) noexcept;
        BindingLease& operator=(BindingLease&& other) noexcept;

        void reset() noexcept;
        // True means this lease still holds one binding reference.
        explicit operator bool() const noexcept { return registry_ != nullptr; }
        NativeHitPatchView view() const noexcept { return view_; }

    private:
        friend class NativeHitPatchRegistry;
        BindingLease(NativeHitPatchRegistry& registry, NativeHitPatchView view) noexcept
            : registry_(&registry), view_(view) {}
        NativeHitPatchRegistry* registry_ = nullptr;
        NativeHitPatchView view_{};
    };

    class ResourceLease {
    public:
        ResourceLease() noexcept = default;
        ~ResourceLease() noexcept { reset(); }
        ResourceLease(const ResourceLease&) = delete;
        ResourceLease& operator=(const ResourceLease&) = delete;
        ResourceLease(ResourceLease&& other) noexcept;
        ResourceLease& operator=(ResourceLease&& other) noexcept;

        void reset() noexcept;
        // True means this lease still holds a release token. If the final
        // binding retired its generation, reset remains safe but this value
        // does not imply that resources or a backend remain live.
        explicit operator bool() const noexcept { return registry_ != nullptr; }
        NativeHitPatchView view() const noexcept { return view_; }

    private:
        friend class NativeHitPatchRegistry;
        ResourceLease(NativeHitPatchRegistry& registry, NativeHitPatchView view) noexcept
            : registry_(&registry), view_(view) {}
        NativeHitPatchRegistry* registry_ = nullptr;
        NativeHitPatchView view_{};
    };

    NativeHitPatchRegistry(const NativeHitObjectCatalog&,
                           NativeHitPatchBackendFactory,
                           NativeHitPatchRegistryLimits = {});
    ~NativeHitPatchRegistry() noexcept;
    NativeHitPatchRegistry(const NativeHitPatchRegistry&) = delete;
    NativeHitPatchRegistry& operator=(const NativeHitPatchRegistry&) = delete;
    NativeHitPatchRegistry(NativeHitPatchRegistry&&) = delete;
    NativeHitPatchRegistry& operator=(NativeHitPatchRegistry&&) = delete;

    BindingLease acquire(std::uint32_t patch_id);
    // Resource acquisition is explicit and independent of binding acquisition.
    // The first resource lease loads once; the last clears once. Releasing the
    // final binding forces clear and retires any outstanding resource leases.
    ResourceLease load_resources(const BindingLease&);

    bool active(NativeHitPatchView) const;
    void note_on(NativeHitPatchView, std::uint8_t literal);
    // Both explicit Stop and wrapper destruction affect the shared backend.
    // A late Stop after final release is a no-op, including if the slot is reused.
    void stop(NativeHitPatchView);
    void pause(NativeHitPatchView);  // Native concrete backend pause is Stop.
    void resume(NativeHitPatchView); // Native concrete backend resume does no audio work.
    void raw_control(NativeHitPatchView, std::uint8_t id, std::int32_t raw);

    NativeHitPatchRegistryUsage usage() const;
    // sizeof(registry) plus fixed slot/pending arrays; excludes backend
    // allocations, factory captures and allocator overhead.
    std::size_t fixed_storage_bytes() const;

private:
    struct Slot {
        NativeHitPatchRef patch{};
        std::unique_ptr<NativeHitPatchBackend> backend;
        std::size_t bindings = 0;
        std::size_t resource_refs = 0;
        std::uint64_t generation = 0;
        bool faulted = false;
    };
    enum class PendingKind { binding, resource };
    struct PendingRelease { PendingKind kind; NativeHitPatchView view; };
    class Guard {
    public:
        explicit Guard(NativeHitPatchRegistry&);
        ~Guard() noexcept;
        void check_reentry() const;
    private:
        NativeHitPatchRegistry& registry_;
    };

    const NativeHitObjectCatalog& catalog_;
    NativeHitPatchBackendFactory factory_;
    NativeHitPatchRegistryLimits limits_;
    std::vector<Slot> slots_;
    std::vector<PendingRelease> pending_;
    std::size_t pending_size_ = 0;
    NativeHitPatchRegistryUsage usage_{};
    std::uint64_t next_generation_ = 1;
    bool busy_ = false;
    bool reentered_ = false;
    std::uint64_t reentry_attempts_ = 0;

    Slot* current(NativeHitPatchView) noexcept;
    const Slot* current(NativeHitPatchView) const noexcept;
    Slot& require_current(NativeHitPatchView);
    const Slot& require_current(NativeHitPatchView) const;
    void release_noexcept(PendingKind, NativeHitPatchView) noexcept;
    void do_release_noexcept(PendingKind, NativeHitPatchView) noexcept;
    void drain_pending_noexcept() noexcept;
    void clear_resources_noexcept(Slot&) noexcept;
    void destroy_slot_noexcept(Slot&) noexcept;
};

} // namespace odh
