// SPDX-License-Identifier: MPL-2.0
#include "property_release_session.hpp"
#include "linux_family_overlay_store.hpp"
#include "odh/property_disposition.hpp"
#include "odh/world_snapshot_codec.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
#include <set>
#include <limits>
#include <exception>

namespace odh_host {
namespace {
void need(bool ok,const std::string& why){if(!ok)throw odh::FormatError(why);}
constexpr odh::SessionArchiveLimits archive_limits{32*1024*1024,16*1024*1024,3};
bool same_lease(const odh::SessionFamilyLease&a,const odh::SessionFamilyLease&b){
    return a.neighborhood_identity==b.neighborhood_identity&&a.family_identity==b.family_identity&&
        a.family_chunk_id==b.family_chunk_id&&a.occupancy_epoch==b.occupancy_epoch;
}
}
PreparedPropertyRelease prepare_property_release_session(const odh::NeighborhoodFamilySource& source,
        const odh::FamilyOverlay& expected,std::uint16_t family,const std::vector<std::uint8_t>& bytes,
        const odh::SessionResourceIdentity& resources,const CommunityLotIdentity& identity,
        PropertyReleaseRuntime runtime,std::uint64_t budget){
    need(runtime.source_still_matches&&runtime.first&&runtime.next&&runtime.delete_entity&&runtime.valuation&&
        runtime.current_cash&&runtime.export_objects&&runtime.export_architecture&&runtime.validate_reopen,
        "property release actual runtime owners are incomplete");
    const auto authority=[&]{need(runtime.source_still_matches(),"property release runtime authority expired");};
    const auto step=[&]{authority();need(budget>0,"property release operation budget exhausted; discard private runtime");--budget;};
    authority();const auto replay=odh::replay_family_overlay(source.families,source.identity,expected);
    const auto state=std::find_if(replay.states.begin(),replay.states.end(),[&](const auto& v){return v.family_chunk_id==family;});
    const auto entry=std::find_if(replay.families.begin(),replay.families.end(),[&](const auto& v){return v.chunk_id==family;});
    need(state!=replay.states.end()&&entry!=replay.families.end()&&state->managed&&state->house_number&&
        state->checkpoint_sha256&&state->checkpoint_house==state->house_number&&
        state->checkpoint_epoch==state->occupancy_epoch&&state->checkpoint_budget==state->budget,
        "property release requires its exact current occupied checkpoint");
    need(identity.neighborhood_sha256==source.identity&&identity.house==state->house_number&&
        (identity.mode==1||identity.mode==4),"property release document is not the selected residential lot");
    const auto digest=odh::sha256(bytes.data(),bytes.size());
    need(digest==*state->checkpoint_sha256,"property release checkpoint is not its current head");
    const auto saved=odh::decode_session_bundle(bytes,resources);
    const odh::SessionFamilyLease lease{source.identity,state->family_identity,state->occupancy_epoch,family};
    need(saved.family_lease&&same_lease(*saved.family_lease,lease)&&saved.host.house_number==identity.house&&
        saved.host.family_chunk_id==family&&saved.host.family_budget==std::optional<std::int32_t>{state->budget},
        "property release capture differs from its family lease/cash/house");
    std::vector<std::uint32_t> roster;for(const auto& member:saved.household_roster)roster.push_back(member.guid);
    auto expected_roster=entry->family.member_guids;std::sort(roster.begin(),roster.end());std::sort(expected_roster.begin(),expected_roster.end());
    need(!roster.empty()&&roster==expected_roster,"property release household differs from the effective family");
    need(odh::encode_world_snapshot(runtime.world.snapshot())==odh::encode_world_snapshot(saved.world),
        "property release private World differs from the authenticated checkpoint");
    need(runtime.current_cash()==state->budget,"property release runtime cash differs from its captured owner");
    const auto audit_cursor=[&]{
        std::set<odh::WorldEntityReference> seen;auto cursor=runtime.first();
        while(cursor.status==PropertyReleaseLinkStatus::member){step();
            need(runtime.world.is_live_reference(cursor.reference)&&seen.insert(cursor.reference).second,
                "property release cursor is stale or cyclic");cursor=runtime.next(cursor.reference);}
        need(cursor.status==PropertyReleaseLinkStatus::end,"property release cursor is unavailable");
        const auto actual=runtime.world.snapshot();need(seen.size()==actual.entities.size(),"property release cursor omits live bodies");
        for(const auto& body:actual.entities)need(seen.count({body.id,body.generation})!=0,"property release cursor cohort differs");
    };
    audit_cursor();const auto before=runtime.valuation();authority();
    need(before.status==odh::PropertyValuationStatus::resolved&&before.property_value&&*before.property_value>=0,
        "property release current pre-filter valuation is unresolved");
    std::uint64_t deleted=0;std::set<odh::WorldEntityReference> visited;auto cursor=runtime.first();
    while(cursor.status==PropertyReleaseLinkStatus::member){step();const auto ref=cursor.reference;
        const auto* body=runtime.world.find(ref.id);
        need(body&&body->generation==ref.generation&&body->definition&&visited.insert(ref).second,
            "property release current cursor lost its exact body");
        odh::VmFrame frame;frame.context={ref.id,ref.id,ref.id,0};
        need(runtime.world.bind_context_live(frame.context),"property release cannot bind current object fields");
        const auto flags=runtime.world.read(frame,3,43),type=runtime.world.read(frame,21,9);
        need(flags&&type,"property release movement flags or actual instance type are unknown");
        const auto decision=odh::inspect_property_disposition(runtime.definitions,body->definition->guid,
            static_cast<std::uint16_t>(*type),static_cast<std::uint16_t>(*flags));authority();
        need(decision.disposition!=odh::PropertyDisposition::unresolved,
            std::string("property release disposition unresolved: ")+odh::property_disposition_reason_name(decision.reason));
        if(decision.disposition==odh::PropertyDisposition::remove){
            std::string why;need(runtime.delete_entity(ref,why),"property release source Delete incomplete: "+why);authority();
            need(!runtime.world.is_live_reference(ref),"property release Delete made no progress on the exact generation");
            ++deleted;visited.clear();cursor=runtime.first(); //427572 restarts at CURRENT HEAD.
        }else cursor=runtime.next(ref);
    }
    need(cursor.status==PropertyReleaseLinkStatus::end,"property release final cursor is unavailable");audit_cursor();
    const auto after=runtime.valuation();authority();const auto cash=runtime.current_cash();authority();
    need(after.status==odh::PropertyValuationStatus::resolved&&after.property_value&&*after.property_value>=0&&cash>=0,
        "property release post-filter price/current cash is unresolved");
    auto objects=runtime.export_objects();authority();
    need(objects.tick==saved.host.tick&&objects.resources.language_code==resources.language_code&&
        odh::encode_session_resource_identity(objects.resources)==odh::encode_session_resource_identity(resources)&&
        odh::encode_world_snapshot(objects.world)==odh::encode_world_snapshot(runtime.world.snapshot()),
        "property release actorless export differs from its actual current runtime");
    odh::validate_actorless_object_trade_state(objects);
    PreparedPropertyRelease result;result.expected_=expected;result.refund_=*before.property_value;
    result.source_cash_=state->budget;result.deleted_roots_=deleted;
    result.document_={identity,runtime.export_architecture(),std::move(objects)};authority();
    const auto archive=encode_community_lot_document(result.document_);
    const auto encoded=odh::encode_session_archive(archive,archive_limits);
    result.document_=decode_community_lot_document(odh::decode_session_archive(encoded,archive_limits));
    runtime.validate_reopen(result.document_);authority();
    auto candidate=expected;
    if(state->value_in_arch!=result.refund_)candidate=odh::plan_family_checkpoint_overlay(source.families,source.identity,
        &candidate,family,state->house_number,state->occupancy_epoch,state->budget,digest,result.refund_);
    auto& release=result.release_;release.assignment_index=static_cast<std::uint32_t>(candidate.assignments.size());
    release.house=identity.house;release.family_chunk_id=family;release.neighborhood=identity.neighborhood;release.mode=identity.mode;
    release.family_identity=state->family_identity;release.occupancy_epoch=state->occupancy_epoch;
    release.source_checkpoint_sha256=digest;release.installed_lot_sha256=identity.lot_sha256;
    release.head=odh::sha256(encoded.data(),encoded.size());release.vacant_purchase_value=*after.property_value;
    release.cash_after_cleanup=cash;
    const auto previous=std::find_if(replay.properties.begin(),replay.properties.end(),[&](const auto& p){return p.house==identity.house;});
    if(previous!=replay.properties.end()){release.expected_revision=previous->revision;release.expected_head=previous->head;}
    result.replacement_=odh::plan_family_property_release_overlay(source.families,source.identity,candidate,family,release);
    if(runtime.export_type_attributes){const auto attributes=runtime.export_type_attributes();authority();
        if(attributes)result.replacement_=odh::plan_neighborhood_type_attributes_overlay(source.identity,&result.replacement_,*attributes);
        else need(!expected.type_attributes,"property release lost the current neighborhood type-attribute owner");
    }else need(!expected.type_attributes,"property release lacks the current neighborhood type-attribute export");
    (void)odh::encode_family_overlay(result.replacement_);return result;
}
PropertyReleasePublication publish_property_release_session(const odh::NeighborhoodFamilySource& source,
        const CommunityLotStoreOptions& options,const PreparedPropertyRelease& prepared){
    need(source.identity==prepared.expected_overlay().neighborhood_identity&&
        same_community_lot(community_lot_identity(options),prepared.document().identity),"property release publication identity differs");
    const LinuxNeighborhoodStorage storage(options.neighborhood_number);
    const auto path=linux_family_overlay_path(source.identity,storage);
    const auto current=read_linux_family_overlay(path);
    need(current&&odh::encode_family_overlay(*current)==odh::encode_family_overlay(prepared.expected_overlay()),
        "property release whole-overlay authority changed before publication");
    const auto stored=publish_community_lot_blob(options,prepared.document());
    need(stored.head==prepared.release().head,"property release prepared blob digest changed");
    bool uncertain=false;
    try {
        compare_exchange_linux_family_overlay_atomic(path,current,prepared.replacement());
    } catch (...) {
        // As in FamilySessionJournal, rename can precede a failed directory
        // flush. Only this exact candidate is a committed result; no rebasing.
        const auto original=std::current_exception();
        try {
            const auto published=read_linux_family_overlay(path);
            if(!published||odh::encode_family_overlay(*published)!=odh::encode_family_overlay(prepared.replacement()))
                std::rethrow_exception(original);
        } catch (...) {std::rethrow_exception(original);}
        uncertain=true;
    }
    return {stored,uncertain};
}
}
