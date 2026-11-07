// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/dialog.hpp"
#include "odh/bitmap.hpp"
#include <filesystem>
#include <memory>

namespace odh_host {
enum class DialogIllustrationKind { none,indexed_bitmap,entity,neighbor,unavailable,unsupported };
// Derived afresh from the same original string expansion as the dialog. Known
// absence differs from unavailable provenance. The host supplies automatic's
// expanded-text subject, then the actual StackObject fallback if appropriate;
// neither the displayed icon_name nor a guessed actor establishes authority.
struct DialogIllustrationSource {
    bool automatic_known=false;
    std::optional<odh::WorldEntityReference> automatic_entity;
    bool neighbor_known=false;
    std::optional<std::int16_t> neighbor_id;
    // Original $Neighbor substitution may supply automatic's image even when
    // that character has no instantiated World entity. Mutually exclusive with
    // automatic_entity; the host preserves the original field/token priority.
    std::optional<std::int16_t> automatic_neighbor_id;
};
struct DialogIllustrationPlan {
    DialogIllustrationKind kind=DialogIllustrationKind::none;
    std::optional<std::uint16_t> bitmap_id;
    std::optional<odh::WorldEntityReference> entity;
    std::optional<std::int16_t> neighbor_id;
    std::string diagnostic;
};
// Value-only selection; never reads files, creates an entity, or advances a VM.
// EXE004360c1: flags bits1..3 select auto/none/neighbor/indexed/named;
// indexed includes icon0 and selects BMP_5000+operand.icon. Named remains
// explicit unsupported: icon_name must never become an operating-system path.
DialogIllustrationPlan plan_dialog_illustration(const odh::DialogState&,const odh::World&,
    const DialogIllustrationSource& = {});

struct DialogIllustrationResource {
    std::filesystem::path archive;
    std::string entry; // Empty means the explicitly selected standalone IFF.
    // Exact immutable resource already authenticated/loaded by the host. When
    // present this IFF wins over disk; archive/entry retain provenance only.
    std::shared_ptr<const std::string> frozen;
};
struct DialogIllustrationLimits {
    odh::BitmapLimits bitmap{};
    std::uint64_t iff_bytes=16u*1024u*1024u;
    odh::Limits containers=[] {odh::Limits l;l.far_entries=16384;l.iff_chunks=8192;return l;}();
};
struct PreparedDialogIllustration {
    DialogIllustrationPlan plan;
    std::optional<odh::BitmapImage> image; // Owned; bitmap readers retain no stream.
};
// Host preparation, once per actual prompt/restore and outside painting.
// resource must be the exact code/string-scope IFF authenticated by the host;
// no archive precedence, named-file search, fallback bitmap or guessed root.
// Entity/neighbor plans remain typed portrait requests for existing host art
// consumers. Rendering must recheck their identity before using a cached image.
PreparedDialogIllustration prepare_dialog_illustration(const odh::DialogState&,
    const odh::World&,const DialogIllustrationSource&,
    const std::optional<DialogIllustrationResource>&,const DialogIllustrationLimits& = {});
}
