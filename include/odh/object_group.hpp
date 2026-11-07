// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/world.hpp"
#include "odh/object_definition.hpp"

namespace odh {
// Source VMMultitileGroup membership order is observable to Init/placement and
// queries. Offsets use VM coordinates (16 units per tile); level is relative.
// Even direct-subpart singleton groups retain raw source offsets. Initial
// individual SetPosition and subsequent group ChangePosition are distinct.
enum class ObjectGroupKind : std::uint8_t { constructed=0,live_association=1 };
enum class ObjectGroupMemberSource : std::uint8_t { native_created=0,original_import=1 };
// Per-member resource provenance for a mutable live association. Original
// imports still require external source authentication by the loading host;
// a serialized tag is never that proof by itself.
struct ObjectGroupMemberOrigin {
    ObjectGroupMemberSource source_kind=ObjectGroupMemberSource::native_created;
    std::uint32_t guid=0;
    std::optional<std::uint32_t> master_guid;
    std::string source,asset;
    std::uint16_t definition_chunk_id=0;
};
// Exact native cXMTObject fields, in TILE units for XY (not ObjectGroup's
// VM-unit construction offsets). Keep authored194 and current1a0 separately.
struct NativeObjectGroupOffset {std::int32_t x=0,y=0,level=0;};
struct NativeObjectGroupMember {
    NativeObjectGroupOffset authored,current;
    WorldEntityReference next{};
};
// Explicit host capability: legacy archives never acquire native fields from
// old construction offsets. Enable only with original/import/rotation owners.
enum class NativeObjectGroupConstructionProfile {unknown_legacy,original_native};
// Actual cXMTObject constructor + class initialization, not script Init0:
// 422827..422845 clear both triples; 4228a5/4318eb then assign authored.
// master_id==0 constructs cXObject and returns absent, even for a singleton.
// An actual MT subpart requires its complete signed16 level word; abstract
// master definitions are not bodies and are rejected. NEXT is still unlinked.
std::optional<NativeObjectGroupMember> native_object_group_constructor_fields(const ObjectDefinition&);
struct NativeObjectGroupSourceMember {
    WorldEntityReference reference;
    NativeObjectGroupOffset authored,current;
};
struct RetiredNativeObjectGroupMember {
    WorldEntityReference reference;
    NativeObjectGroupMember fields;
};
enum class NativeObjectGroupLinkStatus {member,end,unavailable};
struct NativeObjectGroupLink {NativeObjectGroupLinkStatus status=NativeObjectGroupLinkStatus::unavailable;WorldEntityReference reference{};};
struct ObjectGroupMember {
    std::int16_t id=0;
    std::uint64_t generation=0;
    std::int16_t offset_x=0,offset_y=0,offset_level=0;
    std::optional<ObjectGroupMemberOrigin> origin; // Required only for live associations.
    std::optional<NativeObjectGroupMember> native; // Unknown legacy remains absent.
};
struct ObjectGroup {
    std::uint32_t definition_guid=0; // Constructed: requested master/exact singleton. Live association: zero.
    bool multitile=false;
    std::int16_t base_id=0; // Actual source BaseObject, not minimum numeric ID.
    std::vector<ObjectGroupMember> members; // Preserve source order, including base placement.
    ObjectGroupKind kind=ObjectGroupKind::constructed;
    std::optional<WorldEntityReference> native_head; // Explicit source parent/head, independent of BaseObject.
};
struct ObjectGroupSnapshot {
    std::vector<ObjectGroup> groups;
    // Same owner, no second membership list. Native unlink clears removed.next;
    // these exact fields are usable only while the corresponding body is retained.
    std::vector<RetiredNativeObjectGroupMember> retired_native_members;
};
struct ObjectGroupLimits {
    std::size_t groups=4096,members=4096; // Aggregate, not per group.
    std::uint64_t payload_bytes=256u*1024u;
};
// Validates canonical group order plus exact live World identities. Legacy
// constructed groups retain their complete construction-plan meaning. A live
// association owns explicit mutable membership with per-part provenance; the
// host still authenticates original-import resources against its baseline.
// No IDs, generations, offsets or membership are inferred from positions,
// adjacent pieces, resource order or master IDs.
void validate_object_groups(const ObjectGroupSnapshot&,const WorldSnapshot&,ObjectGroupLimits={});
// Canonicalizes ONLY the order between groups, then validates before returning
// an owned value. The member vector and explicit source base stay untouched.
ObjectGroupSnapshot capture_object_groups(std::vector<ObjectGroup>,const WorldSnapshot&,ObjectGroupLimits={});
ObjectGroupSnapshot capture_object_group_snapshot(ObjectGroupSnapshot,const WorldSnapshot&,ObjectGroupLimits={});
// Constructed-only snapshots retain the exact OGRP v1 representation. A live
// association selects OGRP v2 with an explicit kind and individual provenance;
// its definition_guid is zero because it is not a construction request.
// Native metadata selects v3; v1/v2 bytes and unknown legacy state stay intact.
// Structural validation here; SessionBundle checks World/OTRD/resource links.
std::vector<std::uint8_t> encode_object_group_snapshot(const ObjectGroupSnapshot&,ObjectGroupLimits={});
ObjectGroupSnapshot decode_object_group_snapshot(const std::vector<std::uint8_t>&,ObjectGroupLimits={});
}
