#include "odh/hit_patch_registry.hpp"

#include <limits>
#include <utility>

namespace odh {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw FormatError(message);
}
}

NativeHitPatchRegistry::BindingLease::BindingLease(BindingLease&& other) noexcept
    : registry_(std::exchange(other.registry_, nullptr)), view_(other.view_) {
    other.view_ = {};
}

NativeHitPatchRegistry::BindingLease&
NativeHitPatchRegistry::BindingLease::operator=(BindingLease&& other) noexcept {
    if (this != &other) {
        reset();
        registry_ = std::exchange(other.registry_, nullptr);
        view_ = other.view_;
        other.view_ = {};
    }
    return *this;
}

void NativeHitPatchRegistry::BindingLease::reset() noexcept {
    if (auto* registry = std::exchange(registry_, nullptr))
        registry->release_noexcept(PendingKind::binding, view_);
}

NativeHitPatchRegistry::ResourceLease::ResourceLease(ResourceLease&& other) noexcept
    : registry_(std::exchange(other.registry_, nullptr)), view_(other.view_) {
    other.view_ = {};
}

NativeHitPatchRegistry::ResourceLease&
NativeHitPatchRegistry::ResourceLease::operator=(ResourceLease&& other) noexcept {
    if (this != &other) {
        reset();
        registry_ = std::exchange(other.registry_, nullptr);
        view_ = other.view_;
        other.view_ = {};
    }
    return *this;
}

void NativeHitPatchRegistry::ResourceLease::reset() noexcept {
    if (auto* registry = std::exchange(registry_, nullptr))
        registry->release_noexcept(PendingKind::resource, view_);
}

NativeHitPatchRegistry::Guard::Guard(NativeHitPatchRegistry& registry)
    : registry_(registry) {
    if (registry_.busy_) {
        registry_.reentered_ = true;
        ++registry_.reentry_attempts_;
        throw FormatError("native HIT patch registry callback reentered");
    }
    registry_.busy_ = true;
    registry_.reentered_ = false;
}

NativeHitPatchRegistry::Guard::~Guard() noexcept {
    registry_.drain_pending_noexcept();
    registry_.busy_ = false;
}

void NativeHitPatchRegistry::Guard::check_reentry() const {
    require(!registry_.reentered_, "native HIT patch registry callback reentered");
}

NativeHitPatchRegistry::NativeHitPatchRegistry(
        const NativeHitObjectCatalog& catalog,
        NativeHitPatchBackendFactory factory,
        NativeHitPatchRegistryLimits limits)
    : catalog_(catalog), factory_(std::move(factory)), limits_(limits) {
    require(static_cast<bool>(factory_), "native HIT patch registry factory missing");
    require(limits_.live_patches > 0 && limits_.bindings > 0 &&
            limits_.resource_refs > 0, "native HIT patch registry limits must be positive");
    constexpr std::size_t hard_max = 65536;
    require(limits_.live_patches <= hard_max && limits_.bindings <= hard_max &&
            limits_.resource_refs <= hard_max,
            "native HIT patch registry limit exceeds ODH hard maximum");
    require(limits_.bindings <= std::numeric_limits<std::size_t>::max() -
            limits_.resource_refs, "native HIT patch registry lease limits overflow");
    slots_.resize(limits_.live_patches);
    pending_.resize(limits_.bindings + limits_.resource_refs);
}

NativeHitPatchRegistry::~NativeHitPatchRegistry() noexcept {
    busy_ = true;
    for (auto& slot : slots_) destroy_slot_noexcept(slot);
    drain_pending_noexcept();
    busy_ = false;
}

NativeHitPatchRegistry::Slot*
NativeHitPatchRegistry::current(NativeHitPatchView view) noexcept {
    if (view.owner != this || view.slot >= slots_.size() || !view.generation)
        return nullptr;
    auto& slot = slots_[view.slot];
    if (!slot.backend || slot.generation != view.generation) return nullptr;
    return &slot;
}

const NativeHitPatchRegistry::Slot*
NativeHitPatchRegistry::current(NativeHitPatchView view) const noexcept {
    return const_cast<NativeHitPatchRegistry*>(this)->current(view);
}

NativeHitPatchRegistry::Slot&
NativeHitPatchRegistry::require_current(NativeHitPatchView view) {
    auto* slot = current(view);
    require(slot != nullptr, "native HIT patch view retired");
    require(!slot->faulted, "native HIT patch backend cleanup failed");
    return *slot;
}

const NativeHitPatchRegistry::Slot&
NativeHitPatchRegistry::require_current(NativeHitPatchView view) const {
    return const_cast<NativeHitPatchRegistry*>(this)->require_current(view);
}

NativeHitPatchRegistry::BindingLease
NativeHitPatchRegistry::acquire(std::uint32_t patch_id) {
    Guard guard(*this);
    const auto ref = catalog_.lookup_patch(patch_id);
    require(ref.has_value(), "native HIT global Patch unavailable");
    require(usage_.bindings < limits_.bindings, "native HIT patch binding limit exceeded");

    for (std::size_t i = 0; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        if (slot.backend && slot.patch.id == ref->id &&
                slot.patch.group == ref->group && slot.patch.line == ref->line &&
                slot.patch.patch == ref->patch) {
            require(!slot.faulted, "native HIT patch backend cleanup failed");
            ++slot.bindings;
            ++usage_.bindings;
            return BindingLease(*this, {this, i, slot.generation});
        }
    }

    require(usage_.live_patches < limits_.live_patches,
            "native HIT live Patch limit exceeded");
    require(next_generation_ != std::numeric_limits<std::uint64_t>::max(),
            "native HIT patch generation exhausted");
    std::size_t free_slot = 0;
    while (free_slot < slots_.size() && slots_[free_slot].backend) ++free_slot;
    require(free_slot < slots_.size(), "native HIT patch registry has no free slot");

    // Nothing observable is committed until the factory returns and reentry
    // has been checked. A throwing factory must undo its own partial work.
    auto backend = factory_(*ref);
    guard.check_reentry();
    require(static_cast<bool>(backend), "native HIT patch factory returned null");
    auto& slot = slots_[free_slot];
    slot.patch = *ref;
    slot.backend = std::move(backend);
    slot.bindings = 1;
    slot.resource_refs = 0;
    slot.generation = next_generation_++;
    slot.faulted = false;
    ++usage_.live_patches;
    ++usage_.bindings;
    ++usage_.generations_created;
    return BindingLease(*this, {this, free_slot, slot.generation});
}

NativeHitPatchRegistry::ResourceLease
NativeHitPatchRegistry::load_resources(const BindingLease& binding) {
    Guard guard(*this);
    require(binding.registry_ == this, "native HIT resource load needs a live binding");
    auto& slot = require_current(binding.view_);
    require(usage_.resource_refs < limits_.resource_refs,
            "native HIT patch resource reference limit exceeded");
    if (slot.resource_refs == 0) {
        try {
            slot.backend->load_resources();
            guard.check_reentry();
        } catch (...) {
            // The backend contract makes this safe even after partial load.
            try { slot.backend->clear_resources(); }
            catch (...) { slot.faulted = true; usage_.cleanup_failed = true; }
            if (reentered_) { slot.faulted = true; usage_.cleanup_failed = true; }
            throw;
        }
    }
    ++slot.resource_refs;
    ++usage_.resource_refs;
    return ResourceLease(*this, binding.view_);
}

bool NativeHitPatchRegistry::active(NativeHitPatchView view) const {
    auto& self = const_cast<NativeHitPatchRegistry&>(*this);
    Guard guard(self);
    const bool result = require_current(view).backend->active();
    guard.check_reentry();
    return result;
}

void NativeHitPatchRegistry::note_on(NativeHitPatchView view,
                                      std::uint8_t literal) {
    Guard guard(*this);
    static_cast<void>(literal); // The observed wrapper ignores NoteOn2's byte.
    require_current(view).backend->note_on();
    guard.check_reentry();
}

void NativeHitPatchRegistry::stop(NativeHitPatchView view) {
    Guard guard(*this);
    require(view.owner == this, "native HIT patch view belongs to another registry");
    if (auto* slot = current(view)) {
        slot->backend->stop();
        guard.check_reentry();
    }
}

void NativeHitPatchRegistry::pause(NativeHitPatchView view) {
    Guard guard(*this);
    require_current(view).backend->stop();
    guard.check_reentry();
}

void NativeHitPatchRegistry::resume(NativeHitPatchView view) {
    Guard guard(*this);
    require_current(view); // Native concrete backend resumes with no audio work.
}

void NativeHitPatchRegistry::raw_control(NativeHitPatchView view,
        std::uint8_t id, std::int32_t raw) {
    Guard guard(*this);
    require_current(view).backend->raw_control(id, raw);
    guard.check_reentry();
}

NativeHitPatchRegistryUsage NativeHitPatchRegistry::usage() const {
    auto& self = const_cast<NativeHitPatchRegistry&>(*this);
    Guard guard(self);
    return usage_;
}

std::size_t NativeHitPatchRegistry::fixed_storage_bytes() const {
    auto& self = const_cast<NativeHitPatchRegistry&>(*this);
    Guard guard(self);
    return sizeof(*this) + slots_.size() * sizeof(Slot) +
        pending_.size() * sizeof(PendingRelease);
}

void NativeHitPatchRegistry::release_noexcept(
        PendingKind kind, NativeHitPatchView view) noexcept {
    if (busy_) {
        reentered_ = true;
        ++reentry_attempts_;
        // Final Patch release can retire resource tokens without destroying
        // their RAII wrappers. Such stale tokens are not charged to the live
        // resource limit, so never let them consume bounded pending space.
        const auto* slot = current(view);
        if (!slot || (kind == PendingKind::resource && !slot->resource_refs) ||
                (kind == PendingKind::binding && !slot->bindings)) return;
        if (pending_size_ < pending_.size())
            pending_[pending_size_++] = {kind, view};
        else usage_.cleanup_failed = true; // Cannot occur for live bounded leases.
        return;
    }
    busy_ = true;
    reentered_ = false;
    do_release_noexcept(kind, view);
    drain_pending_noexcept();
    if (reentered_) usage_.cleanup_failed = true;
    busy_ = false;
}

void NativeHitPatchRegistry::do_release_noexcept(
        PendingKind kind, NativeHitPatchView view) noexcept {
    auto* slot = current(view);
    if (!slot) return; // Last binding already retired this generation.
    if (kind == PendingKind::resource) {
        if (!slot->resource_refs) return;
        --slot->resource_refs;
        --usage_.resource_refs;
        if (!slot->resource_refs) clear_resources_noexcept(*slot);
    } else {
        if (!slot->bindings) return;
        --slot->bindings;
        --usage_.bindings;
        if (!slot->bindings) destroy_slot_noexcept(*slot);
    }
}

void NativeHitPatchRegistry::drain_pending_noexcept() noexcept {
    std::size_t index = 0;
    while (index < pending_size_) {
        const auto pending = pending_[index++];
        do_release_noexcept(pending.kind, pending.view);
    }
    pending_size_ = 0;
}

void NativeHitPatchRegistry::clear_resources_noexcept(Slot& slot) noexcept {
    if (!slot.backend) return;
    const auto attempts_before = reentry_attempts_;
    try { slot.backend->clear_resources(); }
    catch (...) { slot.faulted = true; usage_.cleanup_failed = true; }
    if (reentry_attempts_ != attempts_before) {
        slot.faulted = true;
        usage_.cleanup_failed = true;
    }
}

void NativeHitPatchRegistry::destroy_slot_noexcept(Slot& slot) noexcept {
    if (!slot.backend) return;
    if (slot.resource_refs) {
        usage_.resource_refs -= slot.resource_refs;
        slot.resource_refs = 0;
        clear_resources_noexcept(slot);
    }
    usage_.bindings -= slot.bindings;
    slot.bindings = 0;
    slot.backend.reset(); // Exactly one RAII owner, regardless of native aliases.
    slot.patch = {};
    slot.generation = 0;
    slot.faulted = false;
    --usage_.live_patches;
}

} // namespace odh
