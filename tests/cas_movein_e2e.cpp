#include "linux_cas_catalog.hpp"
#include "community_lot_store.hpp"
#include "linux_family_overlay_store.hpp"
#include "linux_family_session_journal.hpp"
#include "linux_travel_session.hpp"
#include "linux_private_family.hpp"
#include "linux_simitone_cas_member.hpp"
#include "resource.hpp"
#include "odh/family.hpp"
#include "odh/family_overlay.hpp"
#include "odh/lot.hpp"
#include "odh/lot_locations.hpp"
#include "odh/session_entry.hpp"
#include "odh/session_bundle.hpp"
#include "odh/ts1_travel_return.hpp"
#include "odh/animation.hpp"
#include "odh/character_catalog.hpp"
#ifdef ODH_HAVE_MANAGEMENT_LAYOUT
#include "neighborhood_screen.hpp"
#include "linux_ui_strings.hpp"
#include "neighborhood_lot_admission.hpp"
#include "odh/lot_context.hpp"
#endif
#include "odh/sha256.hpp"

#include <algorithm>
#include <set>
#include <filesystem>
#include <cstdlib>
#include <charconv>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
template<class F> void iff(const fs::path& path, F&& callback) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path.string());
    callback(odh::View(input, fs::file_size(path)));
}

std::vector<odh::FamilyEntry> source_families(const fs::path& path) {
    std::vector<odh::FamilyEntry> result;
    iff(path, [&](const auto& view) {
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if (chunk.type == "FAMI") result.push_back({chunk.id, odh::read_fami(chunk.payload)});
        });
    });
    return result;
}

odh::LotLocations locations(const fs::path& path) {
    std::optional<odh::LotLocations> result;
    iff(path, [&](const auto& view) {
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if (chunk.type == "STR#" && chunk.id == 1) {
                if (result) throw odh::FormatError("ambiguous LotLocations mode 1");
                result = odh::read_lot_locations(chunk.payload, 1);
            }
        });
    });
    if (!result) throw odh::FormatError("LotLocations mode 1 is absent");
    return *result;
}

std::int32_t purchase_price(const fs::path& path) {
    std::optional<odh::Simi> selected;
    iff(path, [&](const auto& view) {
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if (chunk.type != "SIMI" || chunk.id != 1) return;
            if (selected) throw odh::FormatError("ambiguous SIMI1");
            selected = odh::read_simi(chunk.payload);
        });
    });
    if (!selected) throw odh::FormatError("SIMI1 is absent");
    return selected->purchase_value();
}

std::vector<std::uint8_t> template_person(const fs::path& game) {
    std::vector<std::uint8_t> result;
    with_far_entry((game / "GameData/Objects/Objects.far").string(),
        R"(People\TemplatePerson.iff)", [&](const auto& view) {
            if (view.size() > 16u * 1024u * 1024u)
                throw odh::FormatError("TemplatePerson.iff exceeds 16 MiB");
            result.resize(static_cast<std::size_t>(view.size()));
            if (!result.empty()) view.read(0, result.data(), result.size());
        });
    return result;
}
std::string json_string(const std::string& input) {
    std::string out(1,'"');constexpr char hex[]="0123456789abcdef";
    for(const unsigned char c:input){
        if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
        else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=char(c);
    }
    return out+'"';
}
std::string hex_source_bytes(const std::string& bytes){
    constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);
    for(const unsigned char c:bytes){out+=digits[c>>4];out+=digits[c&15];}return out;
}
std::string hex_digest(const std::array<std::uint8_t,32>& bytes){
    static constexpr char h[]="0123456789abcdef";std::string out;
    for(auto b:bytes){out+=h[b>>4];out+=h[b&15];}return out;
}
// Report the stored owned catalog, not a reconstructed filesystem lookup.
// The ordinary decoder validates it; diagnostic output has the same4096-row cap.
void print_personal_catalog(const std::optional<odh::NativePersonalCatalog>& saved){
    if(!saved){std::cout<<"null";return;}
    const auto& catalog=*saved;constexpr std::size_t limit=4096;
    const auto shown=std::min(catalog.definitions.size(),limit);
    std::cout<<"{\"version\":"<<catalog.version<<",\"neighborhood_identity\":"<<json_string(hex_digest(catalog.neighborhood_identity))
        <<",\"definition_count\":"<<catalog.definitions.size()<<",\"binding_count\":"<<catalog.bindings.size()
        <<",\"entries_shown\":"<<shown<<",\"truncated\":"<<(shown<catalog.definitions.size()?"true":"false")<<",\"entries\":[";
    for(std::size_t n=0;n<shown;++n){if(n)std::cout<<',';const auto& d=catalog.definitions[n];
        std::cout<<"{\"provider_ordinal\":"<<d.provider_ordinal<<",\"guid\":"<<d.guid<<",\"object_type\":"<<d.object_type
            <<",\"object_definition_id\":"<<d.definition_chunk_id<<",\"source\":"<<json_string(d.source)<<",\"asset\":"<<json_string(d.asset)
            <<",\"fingerprint\":"<<json_string(hex_digest(d.fingerprint))<<",\"binding_guid\":";
        if(n<catalog.bindings.size())std::cout<<catalog.bindings[n].guid;else std::cout<<"null";
        std::cout<<",\"neighbor_id\":";if(n<catalog.bindings.size())std::cout<<catalog.bindings[n].neighbor_id;else std::cout<<"null";
        std::cout<<'}';
    }
    std::cout<<"]}";
}
std::pair<odh_host::StoredCommunityLot,odh::FamilyPropertyRelease> current_property(
        const fs::path& game,const odh::FamilyOverlay& overlay,std::uint16_t house){
    const auto neighborhood=game/"UserData/Neighborhood.iff";
    const auto identity=odh_host::linux_neighborhood_identity(neighborhood);
    const auto replay=odh::replay_family_overlay(source_families(neighborhood),identity,overlay);
    const auto head=std::find_if(replay.properties.begin(),replay.properties.end(),[&](const auto& v){return v.house==house;});
    if(head==replay.properties.end())throw std::runtime_error("house has no committed property head");
    const auto release=std::find_if(overlay.property_releases.begin(),overlay.property_releases.end(),[&](const auto& v){
        return v.house==house&&v.head==head->head&&v.expected_revision+1==head->revision&&
            v.installed_lot_sha256==head->installed_lot_sha256&&v.policy==head->policy&&
            v.neighborhood==head->neighborhood&&v.mode==head->mode&&v.vacant_purchase_value==head->vacant_purchase_value;});
    if(release==overlay.property_releases.end())throw std::runtime_error("property head lacks its exact current release");
    const char* data=std::getenv("XDG_DATA_HOME");if(!data||!*data||!fs::path(data).is_absolute())
        throw std::runtime_error("property inspector requires an absolute private XDG_DATA_HOME");
    odh_host::CommunityLotStoreOptions options;options.game_root=game;options.user_data=game/"UserData";
    options.house=house;options.mode=release->mode;options.neighborhood_number=release->neighborhood;
    std::ostringstream file;file<<"House";if(house<10)file<<'0';file<<house<<".iff";
    options.lot_path=options.user_data/"Houses"/file.str();
    options.private_directory=odh_host::NeighborhoodStorage(release->neighborhood).qualify(fs::path(data)/"opendollhouse/saves")/
        ("neighborhood-"+hex_digest(identity))/"community-lots"/("mode-"+std::to_string(release->mode)+"-house-"+std::to_string(house));
    auto stored=odh_host::load_community_lot_blob(options,head->head);
    if(!stored.document.objects)throw std::runtime_error("property lacks an actual object lifetime");
    return {std::move(stored),*release};
}
int inspect_property(const fs::path& game,std::uint16_t house){
    const auto overlay=odh_host::read_linux_family_overlay_for_neighborhood(game/"UserData/Neighborhood.iff");
    if(!overlay)throw std::runtime_error("property inspector requires its committed overlay");
    const auto property=current_property(game,*overlay,house);const auto& doc=property.first.document;
    const auto& release=property.second;const auto& objects=*doc.objects;
    const auto replay=odh::replay_family_overlay(source_families(game/"UserData/Neighborhood.iff"),
        odh_host::linux_neighborhood_identity(game/"UserData/Neighborhood.iff"),*overlay);
    std::cout<<R"({"authenticated_property_head":true,"house":)"<<house<<R"(,"head":)"<<json_string(hex_digest(release.head))
        <<R"(,"source_checkpoint_sha256":)"<<json_string(hex_digest(release.source_checkpoint_sha256))
        <<R"(,"released_family":)"<<release.family_chunk_id<<R"(,"vacant_purchase_value":)"<<release.vacant_purchase_value
        <<R"(,"revision":)"<<release.expected_revision+1<<R"(,"occupied_by":[)";bool comma=false;
    for(const auto& state:replay.states)if(state.house_number==house){if(comma)std::cout<<',';comma=true;std::cout<<state.family_chunk_id;}
    const auto architecture_bytes=odh::encode_architecture_snapshot(doc.architecture);
    std::cout<<R"(],"tick":)"<<objects.tick<<R"(,"architecture":{"canonical_sha256":)"
        <<json_string(hex_digest(odh::sha256(architecture_bytes.data(),architecture_bytes.size())))
        <<R"(,"width":)"<<doc.architecture.prepared.width
        <<R"(,"height":)"<<doc.architecture.prepared.height<<R"(,"floors":[)";
    for(unsigned level=0;level<2;++level){if(level)std::cout<<',';std::cout<<'[';
        const auto& floors=doc.architecture.prepared.levels[level].floors;
        for(std::size_t i=0;i<floors.size();++i){if(i)std::cout<<',';std::cout<<floors[i];}std::cout<<']';}
    std::cout<<R"(]},"active_objects":[)";comma=false;
    for(const auto& entity:objects.world.entities){if(comma)std::cout<<',';comma=true;
        std::cout<<R"({"id":)"<<entity.id<<R"(,"generation":)"<<entity.generation<<R"(,"guid":)"<<(entity.definition?entity.definition->guid:0)
            <<R"(,"avatar":)"<<(entity.kind==odh::EntityKind::avatar?"true":"false")<<'}';}
    std::cout<<"]}\n";return 0;
}
void print_family_vacation(const std::optional<odh::FamilyVacationSnapshot>& saved){
    if(!saved){std::cout<<"null";return;}
    const auto& state=*saved;
    odh::validate_family_vacation_snapshot(state);
    const auto available=odh::family_vacation_available_count(state);
    std::cout<<R"({"version":)"<<state.version
        <<R"(,"neighborhood_identity":)"<<json_string(hex_digest(state.neighborhood_identity))
        <<R"(,"revision":)"<<state.revision
        <<R"(,"family_count":)"<<state.families.size()
        <<R"(,"canonical_codec_sha256":)"<<json_string(hex_digest(odh::family_vacation_digest(state)))
        <<R"(,"available_lots":)";
    if(available)std::cout<<*available;else std::cout<<"null";
    std::cout<<R"(,"truncated":false,"entries":[)";
    bool comma=false;
    for(const auto& row:state.families){
        if(comma)std::cout<<',';comma=true;
        std::cout<<R"({"family_chunk_id":)"<<row.family_chunk_id
            <<R"(,"family_identity":)"<<json_string(hex_digest(row.family_identity))
            <<R"(,"serialized_family_number":)"<<row.serialized_family_number
            <<R"(,"fami_version":)"<<row.fami_version
            <<R"(,"active":)"<<(row.active?"true":"false")
            <<R"(,"seed_word":)";
        if(row.seed_word)std::cout<<*row.seed_word;else std::cout<<"null";
        std::cout<<R"(,"current_word":)";
        if(row.current_word)std::cout<<*row.current_word;else std::cout<<"null";
        std::cout<<'}';
    }
    std::cout<<"]}";
}

int inspect_session(const fs::path& path,const std::vector<std::uint8_t>* authenticated=nullptr){
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("cannot inspect session");
    const auto size=fs::file_size(path);if(size>256u*1024u*1024u)throw std::runtime_error("session inspector input exceeds limit");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if(size&&!input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("short session inspector read");
    if(authenticated&&bytes!=*authenticated)throw std::runtime_error("authenticated session changed while inspecting");
    const auto bundle=odh::decode_session_bundle(bytes);
    std::cout<<R"({"authenticated_journal_head":)"<<(authenticated?"true":"false")
        <<R"(,"archive_path":)"<<json_string(fs::canonical(path).string())
        <<R"(,"tick":)"<<bundle.host.tick<<R"(,"house":)"<<bundle.host.house_number
        <<R"(,"family_chunk_id":)"<<bundle.host.family_chunk_id<<R"(,"family_number":)"<<bundle.host.family_number<<R"(,"budget":)";
    if(bundle.host.family_budget)std::cout<<*bundle.host.family_budget;else std::cout<<"null";
    std::cout<<R"(,"occupancy_epoch":)";if(bundle.family_lease)std::cout<<bundle.family_lease->occupancy_epoch;else std::cout<<"null";
    std::cout<<R"(,"members":[)";bool comma=false;
    for(const auto& member:bundle.household_roster){
        if(comma)std::cout<<',';comma=true;const odh::Entity* body=nullptr;
        for(const auto& entity:bundle.world.entities)if(entity.id==member.id&&entity.generation==member.generation){body=&entity;break;}
        if(!body||!body->definition||body->definition->guid!=member.guid||body->person_data.size()<=65)throw std::runtime_error("saved household identity or person data unavailable");
        std::cout<<R"({"id":)"<<member.id<<R"(,"generation":)"<<member.generation<<R"(,"guid":)"<<member.guid
            <<R"(,"neighbor_id":)"<<member.neighbor_id<<R"(,"age":)"<<body->person_data[58]<<R"(,"gender":)"<<body->person_data[65]<<R"(,"body":)";
        if(body->avatar_appearance&&body->avatar_appearance->body)std::cout<<json_string(body->avatar_appearance->body->literal);else std::cout<<"null";
        std::cout<<R"(,"motives":[)";
        for(std::size_t n=0;n<body->motives.size();++n){if(n)std::cout<<',';std::cout<<body->motives[n];}
        std::cout<<"]}";
    }
    std::cout<<R"(],"relationship_sources":[)";comma=false;
    for(const auto& row:bundle.relationships.sources){if(comma)std::cout<<',';comma=true;
        odh::RelationshipSnapshot single;single.limits=bundle.relationships.limits;single.sources.push_back(row);
        single.pair_count=row.pairs.size();for(const auto& p:row.pairs)single.value_count+=p.values.size();
        const auto encoded=odh::encode_relationship_snapshot(single);
        std::cout<<R"({"matrix":)"<<static_cast<unsigned>(row.matrix)<<R"(,"id":)"<<row.id<<R"(,"generation":)"<<row.generation
            <<R"(,"pairs":)"<<row.pairs.size()<<R"(,"sha256":)"<<json_string(hex_digest(odh::sha256(encoded.data(),encoded.size())))<<'}';
    }
    std::cout<<R"(],"purchase_groups":[)";comma=false;
    if(bundle.object_trade)for(const auto& group:bundle.object_trade->groups){
        if(group.price_origin.kind!=odh::SessionObjectPriceSource::purchase)continue;
        if(comma)std::cout<<',';comma=true;
        std::cout<<R"({"group_id":)"<<group.group_id<<R"(,"requested_guid":)"<<group.price_origin.guid<<R"(,"price":)";
        if(group.initial_price)std::cout<<*group.initial_price;else std::cout<<"null";
        std::cout<<R"(,"members":[)";bool part_comma=false;
        for(const auto& part:group.members){
            if(part_comma)std::cout<<',';part_comma=true;
            const odh::Entity* body=nullptr;
            for(const auto& entity:bundle.world.entities)if(entity.id==part.object.id&&entity.generation==part.object.generation){body=&entity;break;}
            if(!body)throw std::runtime_error("purchased group member is absent from saved World");
            std::cout<<R"({"id":)"<<body->id<<R"(,"generation":)"<<body->generation<<R"(,"guid":)"<<part.object.guid<<R"(,"position":)";
            if(body->position)std::cout<<'['<<body->position->x<<','<<body->position->y<<','<<body->position->level<<','<<unsigned(body->position->direction)<<']';else std::cout<<"null";
            std::cout<<'}';
        }std::cout<<"]}";
    }
    std::cout<<R"(],"world_generation_sequence":)"<<bundle.world.generation_sequence;
    const auto created_origins=[&](const auto& origins){std::cout<<'[';bool comma=false;
        for(const auto& origin:origins){if(comma)std::cout<<',';comma=true;
            std::cout<<R"({"id":)"<<origin.id<<R"(,"generation":)"<<origin.generation<<R"(,"guid":)"<<origin.guid
                <<R"(,"source":)"<<json_string(origin.source)<<R"(,"asset":)"<<json_string(origin.asset)
                <<R"(,"definition_chunk_id":)"<<origin.definition_chunk_id<<'}';}std::cout<<']';};
    std::cout<<R"(,"created_object_origins":)";created_origins(bundle.created_objects);
    std::cout<<R"(,"retired_created_origins":)";
    if(bundle.object_history)created_origins(bundle.object_history->retired_created_objects);else std::cout<<"[]";
    std::cout<<R"(,"retired_household_origins":[)";comma=false;
    if(bundle.object_history)for(const auto& origin:bundle.object_history->retired_household_avatars){
        if(comma)std::cout<<',';comma=true;const auto& m=origin.member;
        std::cout<<R"({"id":)"<<m.id<<R"(,"generation":)"<<m.generation<<R"(,"guid":)"<<m.guid
            <<R"(,"family_chunk_id":)"<<origin.source_lease.family_chunk_id<<R"(,"occupancy_epoch":)"<<origin.source_lease.occupancy_epoch
            <<R"(,"source_checkpoint_sha256":)"<<json_string(hex_digest(origin.source_checkpoint_sha256))<<'}';}
    std::cout<<R"(],"retained_entities":[)";comma=false;
    for(const auto& body:bundle.world.retained_entities){if(comma)std::cout<<',';comma=true;
        std::cout<<R"({"id":)"<<body.id<<R"(,"generation":)"<<body.generation<<R"(,"guid":)"<<(body.definition?body.definition->guid:0)<<'}';}
    std::cout<<R"(],"object_thread_provenance":[)";comma=false;
    for(const auto& thread:bundle.object_threads){if(comma)std::cout<<',';comma=true;
        std::cout<<R"({"id":)"<<thread.id<<R"(,"generation":)"<<thread.generation<<R"(,"guid":)"<<thread.guid
            <<R"(,"primary_owner":)"<<thread.primary_owner<<R"(,"code_owner_count":)"<<thread.code_owners.size()
            <<R"(,"resource_owner_count":)"<<thread.resource_owners.size()<<R"(,"resource_owners":[)";bool owners=false;
        for(const auto& owner:thread.resource_owners){if(owners)std::cout<<',';owners=true;
            std::cout<<R"({"owner":)"<<owner.owner<<R"(,"resource_guid":)"<<owner.resource_guid
                <<R"(,"source":)"<<json_string(owner.source)<<R"(,"asset":)"<<json_string(owner.asset)
                <<R"(,"definition_chunk_id":)"<<owner.definition_chunk_id<<R"(,"root_routine":)"<<owner.root_routine<<'}';}
        std::cout<<"]}";}
    std::cout<<R"(],"architecture":)";
    if(bundle.architecture){
        const auto& prepared=bundle.architecture->prepared;
        const auto architecture_bytes=odh::encode_architecture_snapshot(*bundle.architecture);
        std::cout<<R"({"canonical_sha256":)"<<json_string(hex_digest(odh::sha256(architecture_bytes.data(),architecture_bytes.size())))
            <<R"(,"width":)"<<prepared.width<<R"(,"height":)"<<prepared.height<<R"(,"floors":[)";
        for(unsigned level=0;level<2;++level){
            if(level)std::cout<<',';std::cout<<'[';
            for(std::size_t i=0;i<prepared.levels[level].floors.size();++i){if(i)std::cout<<',';std::cout<<prepared.levels[level].floors[i];}
            std::cout<<']';
        }std::cout<<"]}";
    }else std::cout<<"null";
    std::cout<<R"(,"travel":)";
    std::optional<odh::SessionBundle> visited;
    if(bundle.travel_visited_session)visited=odh::decode_session_bundle(*bundle.travel_visited_session);
    const auto& active=visited?*visited:bundle;
    if(active.travel){
        const auto& state=*active.travel;
        std::cout<<R"({"envelope":)"<<(visited?"true":"false")<<R"(,"active_host_house":)"<<active.host.house_number
            <<R"(,"active_tick":)"<<active.host.tick<<R"(,"home_house":)"<<state.home_house
            <<R"(,"current_house":)"<<state.current_house<<R"(,"current_mode":)"<<unsigned(state.current_mode)
            <<R"(,"family_activated":)"<<(state.family_activated?"true":"false")<<R"(,"pending_destination":)";
        if(state.pending)std::cout<<state.pending->destination;else std::cout<<"null";
        std::cout<<R"(,"travelers":[)";bool first=true;
        for(const auto& member:state.travelers){
            if(!first)std::cout<<',';first=false;
            std::cout<<R"({"guid":)"<<member.guid<<R"(,"neighbor_id":)"<<member.neighbor_id<<'}';
        }
        std::cout<<R"(],"inventory":[)";first=true;
        if(active.neighbor_inventory)for(const auto& entry:active.neighbor_inventory->inventory_by_id){
            if(!first)std::cout<<',';first=false;
            std::cout<<R"({"neighbor_id":)"<<entry.first<<R"(,"items":[)";bool next=false;
            for(const auto& item:entry.second){
                if(next)std::cout<<',';next=true;
                std::cout<<R"({"type":)"<<item.type<<R"(,"guid":)"<<item.guid<<R"(,"count":)"<<item.count<<'}';
            }std::cout<<"]}";
        }std::cout<<"]}";
    }else std::cout<<"null";
    std::cout<<R"(,"neighbor_inventory":)";
    if(!active.neighbor_inventory)std::cout<<"null";
    else{const auto& inventory=*active.neighbor_inventory;std::size_t neighbors=0,items=0;
        std::cout<<R"({"version":)"<<inventory.version<<R"(,"neighbor_count":)"<<inventory.inventory_by_id.size()<<R"(,"neighbors_truncated":)"<<(inventory.inventory_by_id.size()>4096?"true":"false")<<R"(,"entries":[)";
        for(const auto& row:inventory.inventory_by_id){
            if(neighbors==4096)break;if(neighbors++)std::cout<<',';
            std::cout<<R"({"neighbor_id":)"<<row.first<<R"(,"item_count":)"<<row.second.size()<<R"(,"items":[)";
            std::size_t shown=0;for(const auto& item:row.second){if(items==65536)break;if(shown++)std::cout<<',';++items;
                std::cout<<R"({"type":)"<<item.type<<R"(,"guid":)"<<item.guid<<R"(,"count":)"<<item.count<<'}';}
            std::cout<<R"(],"items_truncated":)"<<(shown<row.second.size()?"true":"false")<<'}';
        }std::cout<<"]}";
    }
    std::cout<<R"(,"type_attributes":)";
    if(!active.type_attributes)std::cout<<"null";
    else{const auto bytes=odh::encode_type_attributes_snapshot(*active.type_attributes);const auto digest=odh::sha256(bytes.data(),bytes.size());
        static constexpr char hex[]="0123456789abcdef";std::string text;text.reserve(64);
        for(const auto b:digest){text.push_back(hex[b>>4]);text.push_back(hex[b&15]);}
        std::cout<<R"({"row_count":)"<<active.type_attributes->by_guid.size()<<R"(,"canonical_codec_bytes":)"<<bytes.size()
            <<R"(,"canonical_codec_sha256":)"<<json_string(text)<<'}';
    }
    std::cout<<R"(,"personal_state":)";
    if(!active.personal_state)std::cout<<"null";
    else{const auto& personal=*active.personal_state;std::size_t shown=0;
        std::cout<<R"({"version":)"<<personal.version<<R"(,"neighborhood_identity":)"<<json_string(hex_digest(personal.neighborhood_identity))
            <<R"(,"catalog":)";print_personal_catalog(personal.catalog);
        std::cout<<R"(,"record_count":)"<<personal.records.size()
            <<R"(,"truncated":)"<<(personal.records.size()>64?"true":"false")<<R"(,"entries":[)";
        for(const auto& record:personal.records){if(shown==64)break;if(shown++)std::cout<<',';
            std::cout<<R"({"neighbor_id":)"<<record.identity.neighbor_id<<R"(,"guid":)"<<record.identity.guid
                <<R"(,"character_asset":)"<<json_string(record.identity.character_asset)
                <<R"(,"character_fingerprint":)"<<json_string(hex_digest(record.identity.character_fingerprint))
                <<R"(,"revision":)"<<record.revision<<R"(,"writer":)"<<static_cast<unsigned>(record.writer)
                <<R"(,"schema_version":)"<<unsigned(record.projection.schema_version)<<R"(,"fields":{)";
            bool field_comma=false;for(std::size_t index=0;index<record.projection.present.size();++index){
                if(!record.projection.present[index])continue;if(field_comma)std::cout<<',';field_comma=true;
                std::cout<<json_string(std::to_string(index))<<':'<<record.projection.values[index];
            }std::cout<<"}}";
        }std::cout<<"]}";
    }
    std::cout<<R"(,"family_vacation":)";print_family_vacation(active.family_vacation);
    std::cout<<R"(,"blocking_dialog":)";
    if(active.dialog){
        const auto& dialog=*active.dialog;
        const auto operands=odh::parse_dialog_operands(dialog.instruction.operands);
        std::cout<<R"({"ticket":)"<<dialog.ticket<<R"(,"routine":)"<<dialog.source.key.id
            <<R"(,"pc":)"<<dialog.source.pc<<R"(,"caller":)"<<dialog.source.context.caller
            <<R"(,"type":)"<<unsigned(operands.type)<<R"(,"title":)"<<json_string(dialog.prompt.title)
            <<R"(,"message":)"<<json_string(dialog.prompt.message)<<R"(,"choices":[)";
        for(std::size_t n=0;n<dialog.prompt.choices.size();++n){
            if(n)std::cout<<',';const auto& choice=dialog.prompt.choices[n];
            std::cout<<R"({"value":)"<<choice.value<<R"(,"label":)"<<json_string(choice.label)<<'}';
        }std::cout<<"]}";
    }else std::cout<<"null";
    // Read decoded values only; no live VM/controller is created or executed.
    const auto route_pose=[](const odh::EntityPosition& p){std::cout<<'['<<p.x<<','<<p.y<<','<<p.level<<','<<unsigned(p.direction)<<']';};
    const auto route_identity=[&](std::int16_t id,std::uint64_t generation){
        const auto found=std::find_if(active.world.entities.begin(),active.world.entities.end(),
            [&](const auto& e){return e.id==id&&e.generation==generation;});
        std::cout<<R"({"id":)"<<id<<R"(,"generation":)"<<generation<<R"(,"current":)"<<(found!=active.world.entities.end()?"true":"false")<<R"(,"position":)";
        if(found!=active.world.entities.end()&&found->position)route_pose(*found->position);else std::cout<<"null";
        std::cout<<'}';
    };
    const auto route_edge=[&](const odh::RoomPortalEdge& e){
        std::cout<<R"({"portal_id":)"<<e.portal_id<<R"(,"source_room":)"<<e.source_room<<R"(,"target_room":)"<<e.target_room<<R"(,"position":)";
        route_pose(e.position);std::cout<<'}';
    };
    const auto route_job=[&](const odh::LocalRouteSnapshot& j){
        std::cout<<R"({"ticket":)"<<j.ticket<<R"(,"run_generation":)"<<j.run_generation<<R"(,"actor":)";route_identity(j.actor,j.actor_generation);
        std::cout<<R"(,"target":)";route_identity(j.target,j.target_generation);
        std::cout<<R"(,"expected":)";route_pose(j.expected);std::cout<<R"(,"target_position":)";route_pose(j.target_position);
        std::cout<<R"(,"state":)"<<unsigned(j.frame.state)<<R"(,"portal_turns":)"<<j.frame.portal_turns<<R"(,"wait_time":)";
        if(j.frame.wait_time)std::cout<<*j.frame.wait_time;else std::cout<<"null";
        std::cout<<R"(,"room_route_invalid":)"<<(j.room_route_invalid?"true":"false")<<R"(,"room_route_replan_active":)"<<(j.room_route_replan_active?"true":"false")
            <<R"(,"path_points":)"<<j.path.size()<<R"(,"next_point":)"<<j.next<<R"(,"source_sparse":)";
        if(!j.source_sparse)std::cout<<"null";
        else{const auto& s=*j.source_sparse;const auto& m=s.motion;
            std::cout<<R"({"room":)"<<s.room<<R"(,"lifecycle":)"<<unsigned(s.lifecycle)<<R"(,"walk_to_pending":)"<<(s.walk_to_pending?"true":"false")
                <<R"(,"walk_to_purpose":)"<<unsigned(s.walk_to_purpose)<<R"(,"locomotion_started":)"<<(s.locomotion_started?"true":"false")
                <<R"(,"motion":{"phase":)"<<unsigned(m.phase)<<R"(,"move_frames":)"<<m.move_frames<<R"(,"move_total_frames":)"<<m.move_total_frames
                <<R"(,"velocity":)"<<m.velocity<<R"(,"turn_frames":)"<<m.turn_frames<<R"(,"remaining_segments":)"<<m.remaining.size()
                <<R"(,"previous_position_hip":[)"<<m.previous_position.position.x<<','<<m.previous_position.position.y<<']'
                <<R"(,"previous_level":)"<<m.previous_position.level<<R"(,"current_segment":)";
            if(m.current){const auto& c=*m.current;std::cout<<R"({"kind":)"<<unsigned(c.kind)<<R"(,"a":[)"<<c.a.x<<','<<c.a.y
                <<R"(],"d":[)"<<c.d.x<<','<<c.d.y<<"]}";}else std::cout<<"null";
            std::cout<<R"(},"collision":)";
            if(s.collision){const auto& c=*s.collision;std::cout<<R"({"phase":)"<<unsigned(c.phase)<<R"(,"wait_time":)"<<c.wait_time
                <<R"(,"timeout":)"<<c.timeout<<R"(,"retries":)"<<c.retries<<'}';}else std::cout<<"null";
            std::cout<<R"(,"considered_count":)"<<s.avatars_to_consider.size()<<R"(,"considered":[)";
            for(std::size_t n=0;n<std::min<std::size_t>(64,s.avatars_to_consider.size());++n){if(n)std::cout<<',';route_identity(s.avatars_to_consider[n].id,s.avatars_to_consider[n].generation);}
            std::cout<<R"(],"considered_truncated":)"<<(s.avatars_to_consider.size()>64?"true":"false")<<'}';
        }std::cout<<'}';
    };
    std::cout<<R"(,"actor_routing":{"actor_count":)"<<active.runtime_entities.size()<<R"(,"actors_truncated":)"<<(active.runtime_entities.size()>64?"true":"false")<<R"(,"actors":[)";
    for(std::size_t n=0;n<std::min<std::size_t>(64,active.runtime_entities.size());++n){
        if(n)std::cout<<',';const auto& actor=active.runtime_entities[n];const auto& vm=actor.vm;
        const auto inspect_words=[&](const auto& words){std::cout<<'[';for(std::size_t k=0;k<words.size();++k){if(k)std::cout<<',';std::cout<<+words[k];}std::cout<<']';};
        std::cout<<R"({"id":)"<<actor.id<<R"(,"generation":)"<<actor.controllers.interactions.actor_generation<<R"(,"run_generation":)"<<vm.run_generation<<R"(,"vm_status":)"<<json_string(odh::vm_status_name(vm.status))
            <<R"(,"temps":)";inspect_words(vm.temps);std::cout<<R"(,"temp_xl":)";inspect_words(vm.temp_xl);std::cout
            <<R"(,"stack_depth":)"<<vm.stack.size()<<R"(,"frames_truncated":)"<<(vm.stack.size()>32?"true":"false")<<R"(,"frames":[)";
        for(std::size_t k=0;k<std::min<std::size_t>(32,vm.stack.size());++k){if(k)std::cout<<',';const auto& f=vm.stack[k];
            std::cout<<R"({"routine":)"<<f.key.id<<R"(,"pc":)"<<f.pc<<R"(,"scope":)"<<unsigned(f.key.scope)<<R"(,"owner":)"<<f.context.code_owner
                <<R"(,"caller":)"<<f.context.caller<<R"(,"callee":)"<<f.context.callee<<R"(,"stack_object":)"<<f.context.stack_object
                <<R"(,"arguments":)";inspect_words(f.arguments);std::cout<<R"(,"locals":)";inspect_words(f.locals);std::cout<<'}';}
        std::cout<<R"(],"pending":)";
        if(vm.pending){const auto& p=*vm.pending;std::cout<<R"({"ticket":)"<<p.ticket<<R"(,"routine":)"<<p.instruction.key.id<<R"(,"pc":)"<<p.instruction.pc
            <<R"(,"opcode":)"<<p.instruction.instruction.opcode<<R"(,"depth":)"<<p.instruction.depth<<'}';}else std::cout<<"null";
        std::cout<<R"(,"legacy_route":)";if(actor.route)route_job(*actor.route);else std::cout<<"null";
        std::cout<<R"(,"route_stack":)";
        if(!actor.route_stack)std::cout<<"null";
        else{const auto& stack=*actor.route_stack;
            std::cout<<R"({"last_tick":)";if(stack.last_tick)std::cout<<*stack.last_tick;else std::cout<<"null";
            std::cout<<R"(,"job_count":)"<<stack.jobs.size()<<R"(,"jobs_truncated":)"<<(stack.jobs.size()>32?"true":"false")<<R"(,"jobs":[)";
            for(std::size_t k=0;k<std::min<std::size_t>(32,stack.jobs.size());++k){if(k)std::cout<<',';const auto& j=stack.jobs[k];
                std::cout<<R"({"route":)";route_job(j);std::cout<<R"(,"external_frame_active":)"<<(j.external_frame_active?"true":"false")
                    <<R"(,"external_frame_owner":)"<<j.external_frame_owner<<R"(,"room_context_open":)"<<(j.room_context_open?"true":"false")
                    <<R"(,"failure_pending":)"<<(j.failure_pending?"true":"false")<<R"(,"failure_tree_invoked":)"<<(j.failure_tree_invoked?"true":"false")
                    <<R"(,"failure_code":)"<<j.failure_code<<R"(,"failure_reason":)"<<json_string(j.failure_reason)<<'}';}
            std::cout<<R"(],"portal_context_count":)"<<stack.portal_contexts.size()<<R"(,"portal_contexts_truncated":)"<<(stack.portal_contexts.size()>32?"true":"false")<<R"(,"portal_contexts":[)";
            for(std::size_t k=0;k<std::min<std::size_t>(32,stack.portal_contexts.size());++k){if(k)std::cout<<',';const auto& c=stack.portal_contexts[k];
                std::cout<<R"({"ticket":)"<<c.ticket<<R"(,"portal_generation":)"<<c.portal_generation<<R"(,"ignored_count":)"<<c.ignored.size()<<R"(,"pending_edge":)";
                if(c.pending_edge)route_edge(*c.pending_edge);else std::cout<<"null";
                std::cout<<R"(,"pending_call":)";if(c.pending_call){const auto& call=*c.pending_call;
                    std::cout<<R"({"routine":)"<<call.key.id<<R"(,"owner":)"<<call.context.code_owner<<R"(,"caller":)"<<call.context.caller
                        <<R"(,"callee":)"<<call.context.callee<<R"(,"stack_object":)"<<call.context.stack_object<<'}';}else std::cout<<"null";std::cout<<'}';}
            std::cout<<R"(],"portal_planned_calls":)"<<stack.portal_planned_calls<<R"(,"portal_returns":)"<<stack.portal_returns<<R"(,"portal_transitions":)"<<stack.portal_transitions<<'}';
        }std::cout<<'}';
    }std::cout<<"]}";
    // Read admitted NPC carriers separately from HHRD. A companion does not
    // become a permanent traveler merely because its full execution is saved.
    std::cout<<R"(,"active_npc_avatars":{"count":)"<<active.npc_avatars.size()<<R"(,"truncated":)"<<(active.npc_avatars.size()>64?"true":"false")<<R"(,"entries":[)";
    for(std::size_t n=0;n<std::min<std::size_t>(64,active.npc_avatars.size());++n){
        if(n)std::cout<<',';const auto& npc=active.npc_avatars[n];const auto& origin=npc.origin;
        std::cout<<R"({"id":)"<<origin.id<<R"(,"generation":)"<<origin.generation<<R"(,"guid":)"<<origin.guid
            <<R"(,"neighbor_id":)"<<origin.neighbor_id<<R"(,"source":)"<<json_string(origin.source)
            <<R"(,"asset":)"<<json_string(origin.asset)<<R"(,"object_definition_id":)"<<origin.object_definition_id
            <<R"(,"character_fingerprint":)"<<json_string(hex_digest(origin.character_fingerprint))
            <<R"(,"primary_owner":)"<<npc.primary_owner<<R"(,"runtime_id":)"<<npc.runtime.id
            <<R"(,"run_generation":)"<<npc.runtime.vm.run_generation<<R"(,"vm_status":)"<<json_string(odh::vm_status_name(npc.runtime.vm.status))
            <<R"(,"frame_count":)"<<npc.runtime.vm.stack.size()<<R"(,"frames_truncated":)"<<(npc.runtime.vm.stack.size()>32?"true":"false")<<R"(,"frames":[)";
        for(std::size_t k=0;k<std::min<std::size_t>(32,npc.runtime.vm.stack.size());++k){if(k)std::cout<<',';const auto& f=npc.runtime.vm.stack[k];
            std::cout<<R"({"routine":)"<<f.key.id<<R"(,"pc":)"<<f.pc<<R"(,"owner":)"<<f.context.code_owner
                <<R"(,"caller":)"<<f.context.caller<<R"(,"callee":)"<<f.context.callee<<R"(,"stack_object":)"<<f.context.stack_object<<'}';}
        std::cout<<R"(],"selected_name_bytes_hex":)";if(npc.selected_name)std::cout<<json_string(hex_source_bytes(*npc.selected_name));else std::cout<<"null";
        std::cout<<'}';
    }std::cout<<"]}";
    const auto* actual_registry=active.architecture_object_state&&active.architecture_object_state->position_registry?
        &*active.architecture_object_state->position_registry:nullptr;
    std::cout<<R"(,"registry_complete":)"<<(actual_registry?(actual_registry->complete?"true":"false"):"null");
    std::cout<<R"(,"active_objects":[)";bool object_comma=false;
    for(const auto& entity:active.world.entities){
        if(object_comma)std::cout<<',';object_comma=true;
        std::cout<<R"({"id":)"<<entity.id<<R"(,"generation":)"<<entity.generation
            <<R"(,"guid":)"<<(entity.definition?entity.definition->guid:0)
            <<R"(,"avatar":)"<<(entity.kind==odh::EntityKind::avatar?"true":"false")<<R"(,"position":)";
        if(entity.position)std::cout<<'['<<entity.position->x<<','<<entity.position->y<<','<<entity.position->level<<','<<unsigned(entity.position->direction)<<']';else std::cout<<"null";
        std::cout<<R"(,"neighbor_id":)";
        if(entity.kind==odh::EntityKind::avatar&&entity.person_data.size()>31)std::cout<<entity.person_data[31];else std::cout<<"null";
        if(entity.kind==odh::EntityKind::avatar){
            std::cout<<R"(,"person_data":[)";
            for(std::size_t n=0;n<entity.person_data.size();++n){if(n)std::cout<<',';std::cout<<entity.person_data[n];}
            std::cout<<']';
        }
        std::cout<<R"(,"motives":[)";
        for(std::size_t n=0;n<entity.motives.size();++n){if(n)std::cout<<',';std::cout<<entity.motives[n];}
        std::cout<<']';
        std::cout<<R"(,"native_out_of_world_level":)";
        if(entity.native_out_of_world_level)std::cout<<*entity.native_out_of_world_level;else std::cout<<"null";
        std::cout<<R"(,"attachment":)";
        if(entity.attachment)std::cout<<R"({"container":)"<<entity.attachment->container<<R"(,"slot":)"<<entity.attachment->slot
            <<R"(,"positioned":)"<<(entity.attachment->positioned?"true":"false")<<'}';else std::cout<<"null";
        std::cout<<R"(,"tile_entries":[)";bool registry_comma=false;
        if(actual_registry)for(const auto& tile:actual_registry->tiles)for(const auto owner:tile.entities)if(owner.id==entity.id){
            if(registry_comma)std::cout<<',';registry_comma=true;
            std::cout<<R"({"offset":)"<<tile.offset<<R"(,"generation":)"<<owner.generation<<'}';
        }
        std::cout<<R"(],"room_entries":[)";registry_comma=false;
        if(actual_registry)for(const auto& room:actual_registry->rooms)for(const auto owner:room.entities)if(owner.id==entity.id){
            if(registry_comma)std::cout<<',';registry_comma=true;
            std::cout<<R"({"room":)"<<room.id<<R"(,"generation":)"<<owner.generation<<'}';
        }
        std::cout<<R"(],"footprint_entries":[)";registry_comma=false;
        if(actual_registry)for(const auto& value:actual_registry->footprints)if(value.owner.id==entity.id){
            if(registry_comma)std::cout<<',';registry_comma=true;
            std::cout<<R"({"generation":)"<<value.owner.generation<<R"(,"present":)"<<(value.present?"true":"false")<<R"(,"room":)";
            if(value.room)std::cout<<*value.room;else std::cout<<"null";
            std::cout<<R"(,"instance":)"<<value.instance<<'}';
        }
        std::cout<<R"(],"motion_entries":[)";registry_comma=false;
        if(actual_registry)for(const auto& value:actual_registry->motion)if(value.owner.id==entity.id){
            if(registry_comma)std::cout<<',';registry_comma=true;
            std::cout<<R"({"generation":)"<<value.owner.generation<<R"(,"moved_self":)"<<(value.moved_self?"true":"false")<<'}';
        }
        unsigned obstacles=0,portals=0,windows=0;
        if(actual_registry)for(const auto& room:actual_registry->rooms){
            for(const auto* values:{&room.static_obstacles,&room.dynamic_obstacles})for(const auto& value:*values)if(value.owner.id==entity.id)++obstacles;
            for(const auto& value:room.portals)if(value.owner.id==entity.id)++portals;
            for(const auto& value:room.window_portals)if(value.owner.id==entity.id)++windows;
        }
        std::cout<<R"(],"obstacle_count":)"<<obstacles<<R"(,"portal_count":)"<<portals<<R"(,"window_portal_count":)"<<windows;
        const odh::SessionResourceCodeOwner* resource=nullptr;
        for(const auto& thread:active.object_threads)if(thread.id==entity.id&&thread.generation==entity.generation)
            for(const auto& bank:thread.resource_owners)if(bank.owner==thread.primary_owner){resource=&bank;break;}
        std::cout<<R"(,"source":)"<<(resource?json_string(resource->source):"null")
            <<R"(,"asset":)"<<(resource?json_string(resource->asset):"null")
            <<R"(,"tree_table_id":)";
        if(resource)std::cout<<resource->tree_table_id;else std::cout<<"null";
        // Read-only targeted source vehicle evidence; no inferred timers/state.
        if(resource&&resource->asset=="CarTaxi.iff"){
            const auto words=[&](const auto& values){
                std::cout<<'[';for(std::size_t n=0;n<values.size();++n){if(n)std::cout<<',';std::cout<<+values[n];}std::cout<<']';
            };
            std::cout<<R"(,"vehicle_attributes":)";words(entity.attributes);
            std::cout<<R"(,"vehicle_object_words":[)";bool word_comma=false;
            for(const auto& word:entity.object_words){
                if(word_comma)std::cout<<',';word_comma=true;
                std::cout<<R"({"index":)"<<word.first<<R"(,"value":)"<<word.second<<'}';
            }std::cout<<']';
            const odh::SessionObjectThread* owned_thread=nullptr;
            for(const auto& t:active.object_threads)if(t.id==entity.id&&t.generation==entity.generation){
                if(owned_thread)throw std::runtime_error("ambiguous vehicle thread identity");owned_thread=&t;
            }
            std::cout<<R"(,"vehicle_thread":)";
            if(!owned_thread)std::cout<<"null";
            else{
                const auto& vm=owned_thread->vm;const auto& sleep=owned_thread->sleep;
                std::cout<<R"({"status":)"<<json_string(odh::vm_status_name(vm.status))
                    <<R"(,"steps":)"<<vm.steps<<R"(,"run_generation":)"<<vm.run_generation
                    <<R"(,"primary_owner":)"<<owned_thread->primary_owner<<R"(,"temps":)";
                words(vm.temps);std::cout<<R"(,"temp_xl":)";words(vm.temp_xl);
                std::cout<<R"(,"frames":[)";
                for(std::size_t n=0;n<vm.stack.size();++n){
                    if(n)std::cout<<',';const auto& f=vm.stack[n];
                    std::cout<<R"({"routine":)"<<f.key.id<<R"(,"scope":)"<<static_cast<unsigned>(f.key.scope)
                        <<R"(,"pc":)"<<f.pc<<R"(,"owner":)"<<f.context.code_owner
                        <<R"(,"caller":)"<<f.context.caller<<R"(,"callee":)"<<f.context.callee
                        <<R"(,"stack_object":)"<<f.context.stack_object<<R"(,"arguments":)";
                    words(f.arguments);std::cout<<R"(,"locals":)";words(f.locals);std::cout<<'}';
                }std::cout<<R"(],"pending":)";
                if(vm.pending){const auto& pending=*vm.pending;
                    std::cout<<R"({"ticket":)"<<pending.ticket<<R"(,"depth":)"<<pending.instruction.depth
                        <<R"(,"routine":)"<<pending.instruction.key.id<<R"(,"pc":)"<<pending.instruction.pc
                        <<R"(,"owner":)"<<pending.instruction.code_owner
                        <<R"(,"opcode":)"<<pending.instruction.instruction.opcode<<R"(,"operands":)";
                    words(pending.instruction.instruction.operands);std::cout<<'}';
                }else std::cout<<"null";
                std::cout<<R"(,"sleep":{"clock":)"<<sleep.clock<<R"(,"last_tick":)";
                if(sleep.last_tick)std::cout<<*sleep.last_tick;else std::cout<<"null";
                std::cout<<R"(,"interrupted":)"<<(sleep.interrupted?"true":"false")
                    <<R"(,"notify_resume":)"<<(sleep.notify_resume?"true":"false")<<R"(,"job":)";
                if(sleep.job){const auto& job=*sleep.job;
                    std::cout<<R"({"ticket":)"<<job.ticket<<R"(,"argument":)"<<job.argument
                        <<R"(,"idle_start":)"<<job.idle_start<<R"(,"deadline":)"<<job.deadline
                        <<R"(,"next_evaluation":)";
                    if(job.next_evaluation)std::cout<<*job.next_evaluation;else std::cout<<"null";
                    std::cout<<'}';
                }else std::cout<<"null";
                std::cout<<"}}";
            }
            std::cout<<R"(,"vehicle_scheduler":)";bool scheduler_found=false;
            for(const auto& row:active.scheduler.entities)if(row.id==entity.id){
                if(scheduler_found)throw std::runtime_error("ambiguous vehicle scheduler identity");scheduler_found=true;
                std::cout<<R"({"run_generation":)"<<row.run_generation<<R"(,"steps":)"<<row.steps
                    <<R"(,"status":)"<<json_string(odh::vm_status_name(row.status))
                    <<R"(,"before":)"<<(row.before?"true":"false")<<R"(,"after":)"<<(row.after?"true":"false")
                    <<R"(,"interrupt":)"<<(row.interrupt?"true":"false")<<'}';
            }
            if(!scheduler_found)std::cout<<"null";
        }
        std::cout<<'}';
    }
    std::cout<<"]}\n";return 0;
}

// Observe the exact unpublished return projection; home() remains guarded.
int inspect_travel_return(const fs::path& path,const fs::path& neighborhood){
    const auto size=fs::file_size(path);if(size>256u*1024u*1024u)throw std::runtime_error("return inspector archive exceeds 256 MiB");
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("cannot open return archive");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));if(!bytes.empty()&&!input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("short return archive read");
    const auto envelope=odh::decode_session_bundle(bytes);
    std::ifstream source(neighborhood,std::ios::binary);if(!source)throw std::runtime_error("cannot open inheritance neighborhood IFF");
    const auto inheritance=odh::read_ts1_travel_person_inheritance(odh::View(source,fs::file_size(neighborhood)));
    const auto prepared=odh::prepare_ts1_travel_return(envelope,{},&inheritance);
    const auto home=odh::decode_session_bundle(*envelope.travel_home_session);
    const auto visited=odh::decode_session_bundle(*envelope.travel_visited_session);
    const auto& preview=prepared.preview_home();
    const auto optional=[](const std::optional<std::int16_t>& v){if(v)std::cout<<*v;else std::cout<<"null";};
    const auto body=[&](const odh::SessionBundle& bundle,const odh::Ts1TravelMember& m)->const odh::Entity*{
        if(&bundle==&visited)return &odh::resolve_ts1_travel_departure_avatar(bundle,m);
        const odh::SessionHouseholdMember* binding=nullptr;
        for(const auto& h:bundle.household_roster)if(h.neighbor_id==m.neighbor_id&&h.guid==m.guid&&h.object_definition_id==m.definition_chunk_id&&h.character_asset==m.character_asset&&h.character_fingerprint==m.character_fingerprint){if(binding)throw std::runtime_error("ambiguous diagnostic return member");binding=&h;}
        if(!binding)return nullptr;
        for(const auto& e:bundle.world.entities)if(e.id==binding->id&&e.generation==binding->generation)return &e;
        return nullptr;
    };
    const auto value=[&](const odh::SessionBundle& b,const odh::Ts1TravelMember& m,std::size_t i)->std::optional<std::int16_t>{const auto* e=body(b,m);return e&&i<e->person_data.size()?std::optional<std::int16_t>{e->person_data[i]}:std::nullopt;};
    std::cout<<"{\"archive_path\":"<<json_string(fs::canonical(path).string())<<",\"archive_sha256\":"<<json_string(hex_digest(odh::sha256(bytes.data(),bytes.size())))<<",\"prepared_source_sha256\":"<<json_string(hex_digest(prepared.source_checkpoint_sha256()))<<",\"home_sha256\":"<<json_string(hex_digest(prepared.suspended_home_sha256()))<<",\"neighborhood_path\":"<<json_string(fs::canonical(neighborhood).string())<<",\"neighborhood_sha256\":"<<json_string(hex_digest(inheritance.neighborhood_identity()))<<",\"home_tick\":"<<home.host.tick<<",\"visited_tick\":"<<visited.host.tick<<",\"requires_resolution\":"<<(prepared.requires_original_personal_persistence_resolution()?"true":"false");
    const auto deltas=[&](const auto& list){std::cout<<'[';bool comma=false;for(const auto& d:list){if(comma)std::cout<<',';comma=true;
        std::cout<<"{\"neighbor_id\":"<<d.member.neighbor_id<<",\"guid\":"<<d.member.guid<<",\"objd\":"<<d.member.definition_chunk_id<<",\"character_asset\":"<<json_string(d.member.character_asset)<<",\"character_sha256\":"<<json_string(hex_digest(d.member.character_fingerprint))<<",\"index\":"<<d.index<<",\"compared_home_value\":";optional(d.home_value);std::cout<<",\"compared_visited_value\":";optional(d.visited_value);
        std::cout<<",\"raw_home_value\":";optional(value(home,d.member,d.index));std::cout<<",\"raw_visited_value\":";optional(value(visited,d.member,d.index));std::cout<<",\"preview_home_value\":";optional(value(preview,d.member,d.index));
        const auto* old=body(home,d.member);const auto* current=body(visited,d.member);std::cout<<",\"home_body\":";if(old)std::cout<<'['<<old->id<<','<<old->generation<<']';else std::cout<<"null";std::cout<<",\"visited_body\":";if(current)std::cout<<'['<<current->id<<','<<current->generation<<']';else std::cout<<"null";std::cout<<'}';}std::cout<<']';};
    std::cout<<",\"unresolved_personal_deltas\":";deltas(prepared.unresolved_personal_deltas());std::cout<<",\"retained_runtime_deltas\":";deltas(prepared.retained_runtime_deltas());std::cout<<",\"unchanged_inherited_deltas\":";deltas(prepared.unchanged_inherited_personal_deltas());
    const auto records=[](const std::optional<odh::NativePersonalSnapshot>& state){if(!state){std::cout<<"null";return;}
        std::cout<<"{\"version\":"<<state->version<<",\"neighborhood_sha256\":"<<json_string(hex_digest(state->neighborhood_identity))<<",\"catalog\":";
        print_personal_catalog(state->catalog);std::cout<<",\"records\":[";bool comma=false;
        for(const auto& r:state->records){if(comma)std::cout<<',';comma=true;std::cout<<"{\"neighbor_id\":"<<r.identity.neighbor_id<<",\"guid\":"<<r.identity.guid<<",\"objd\":"<<r.identity.definition_chunk_id<<",\"character_asset\":"<<json_string(r.identity.character_asset)<<",\"character_sha256\":"<<json_string(hex_digest(r.identity.character_fingerprint))<<",\"revision\":"<<r.revision<<",\"writer\":"<<unsigned(r.writer)<<",\"profile\":"<<unsigned(r.projection.profile)<<",\"schema_version\":"<<unsigned(r.projection.schema_version)<<",\"projection\":[";bool next=false;
            for(std::size_t i=0;i<r.projection.present.size();++i)if(r.projection.present[i]){if(next)std::cout<<',';next=true;std::cout<<'['<<i<<','<<r.projection.values[i]<<']';}
            std::cout<<"],\"seed_words\":";if(r.seed_words){std::cout<<'[';for(std::size_t i=0;i<r.seed_words->size();++i){if(i)std::cout<<',';std::cout<<r.seed_words->at(i);}std::cout<<']';}else std::cout<<"null";std::cout<<'}';}std::cout<<"]}";};
    std::cout<<",\"home_personal_state\":";records(home.personal_state);std::cout<<",\"visited_personal_state\":";records(visited.personal_state);std::cout<<",\"preview_personal_state\":";records(preview.personal_state);std::cout<<"}\n";return 0;
}

// Bounded read-only observation of exact saved generations; never restore/run a VM.
int inspect_runtime_entities(const fs::path& path,const std::set<std::int16_t>& ids){
    const auto size=fs::file_size(path);
    if(size>256u*1024u*1024u)throw std::runtime_error("runtime inspector archive exceeds 256 MiB");
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("cannot open runtime archive");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if(!bytes.empty()&&!input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("short runtime archive read");
    const auto envelope=odh::decode_session_bundle(bytes);
    std::optional<odh::SessionBundle> visited;
    if(envelope.travel_visited_session)visited=odh::decode_session_bundle(*envelope.travel_visited_session);
    const auto& active=visited?*visited:envelope;
    std::vector<const odh::Entity*> bodies;
    for(const auto id:ids){bool found=false;
        for(const auto& e:active.world.entities)if(e.id==id){bodies.push_back(&e);found=true;}
        for(const auto& e:active.world.retained_entities)if(e.id==id){bodies.push_back(&e);found=true;}
        if(!found)throw std::runtime_error("runtime entity absent from active live/retained World: "+std::to_string(id));
    }
    if(bodies.size()>64)throw std::runtime_error("runtime inspector selected generations exceed 64");
    const auto words=[](const auto& v){if(v.size()>65536)throw std::runtime_error("runtime inspector word limit exceeded");std::cout<<'[';bool c=false;for(const auto x:v){if(c)std::cout<<',';c=true;std::cout<<+x;}std::cout<<']';};
    const auto ref=[](const auto& r){std::cout<<"{\"id\":"<<r.id<<",\"generation\":"<<r.generation<<'}';};
    const auto pairs=[](const auto& v){if(v.size()>65536)throw std::runtime_error("runtime inspector word limit exceeded");std::cout<<'[';bool c=false;for(const auto& x:v){if(c)std::cout<<',';c=true;std::cout<<'['<<x.first<<','<<x.second<<']';}std::cout<<']';};
    const auto sleep=[](const odh::SleepSnapshot& s){std::cout<<"{\"clock\":"<<s.clock<<",\"last_tick\":";if(s.last_tick)std::cout<<*s.last_tick;else std::cout<<"null";
        std::cout<<",\"interrupted\":"<<(s.interrupted?"true":"false")<<",\"notify_resume\":"<<(s.notify_resume?"true":"false")<<",\"diagnostic\":"<<json_string(s.diagnostic)<<",\"actor\":";
        if(s.actor)std::cout<<"{\"id\":"<<s.actor->id<<",\"generation\":"<<s.actor->generation<<",\"run_generation\":"<<s.actor->run_generation<<'}';else std::cout<<"null";
        std::cout<<",\"job\":";if(s.job){const auto& j=*s.job;std::cout<<"{\"ticket\":"<<j.ticket<<",\"argument\":"<<j.argument<<",\"idle_start\":"<<j.idle_start<<",\"deadline\":"<<j.deadline<<",\"next_evaluation\":";if(j.next_evaluation)std::cout<<*j.next_evaluation;else std::cout<<"null";std::cout<<'}';}else std::cout<<"null";std::cout<<'}';};
    const auto context_refs=[](const odh::VmContext& c){std::cout<<"{\"bound\":"<<(c.entity_references_bound?"true":"false")<<",\"caller_generation\":";if(c.entity_references_bound)std::cout<<c.caller_generation;else std::cout<<"null";std::cout<<",\"callee_generation\":";if(c.entity_references_bound)std::cout<<c.callee_generation;else std::cout<<"null";std::cout<<",\"stack_object_generation\":";if(c.entity_references_bound)std::cout<<c.stack_object_generation;else std::cout<<"null";std::cout<<'}';};
    const auto pending=[&](const odh::VmPending& p){std::cout<<"{\"ticket\":"<<p.ticket<<",\"depth\":"<<p.instruction.depth<<",\"routine\":"<<p.instruction.key.id<<",\"pc\":"<<p.instruction.pc<<",\"owner\":"<<p.instruction.code_owner<<",\"opcode\":"<<p.instruction.instruction.opcode<<",\"operands\":";words(p.instruction.instruction.operands);std::cout<<",\"context_references\":";context_refs(p.context);std::cout<<'}';};
    const auto vm=[&](const odh::VmSnapshot& v){if(v.stack.size()>1024||v.suspended_pending.size()>1024)throw std::runtime_error("runtime inspector frame limit exceeded");
        std::cout<<"{\"status\":"<<json_string(odh::vm_status_name(v.status))<<",\"diagnostic\":"<<json_string(v.diagnostic)<<",\"steps\":"<<v.steps<<",\"run_generation\":"<<v.run_generation<<",\"temps\":";words(v.temps);std::cout<<",\"temp_xl\":";words(v.temp_xl);std::cout<<",\"frames\":[";bool c=false;
        for(const auto& f:v.stack){if(c)std::cout<<',';c=true;std::cout<<"{\"routine\":"<<f.key.id<<",\"scope\":"<<unsigned(f.key.scope)<<",\"pc\":"<<f.pc<<",\"owner\":"<<f.context.code_owner<<",\"caller\":"<<f.context.caller<<",\"callee\":"<<f.context.callee<<",\"stack_object\":"<<f.context.stack_object<<",\"arguments\":";words(f.arguments);std::cout<<",\"locals\":";words(f.locals);std::cout<<",\"context_references\":";context_refs(f.context);std::cout<<'}';}
        std::cout<<"],\"pending\":";if(v.pending)pending(*v.pending);else std::cout<<"null";std::cout<<",\"suspended_pending\":[";c=false;for(const auto& p:v.suspended_pending){if(c)std::cout<<',';c=true;pending(p);}std::cout<<"]}";};
    const auto owners=[](const auto& values){if(values.size()>4096)throw std::runtime_error("runtime inspector owner limit exceeded");std::cout<<'[';bool c=false;for(const auto& o:values){if(c)std::cout<<',';c=true;std::cout<<"{\"owner\":"<<o.owner<<",\"guid\":"<<o.resource_guid<<",\"source\":"<<json_string(o.source)<<",\"asset\":"<<json_string(o.asset)<<",\"objd\":"<<o.definition_chunk_id<<",\"root_routine\":"<<o.root_routine<<'}';}std::cout<<']';};
    const auto queue=[&](const odh::InteractionQueueSnapshot& q){if(q.actions.size()>1024)throw std::runtime_error("runtime inspector queue limit exceeded");std::cout<<"{\"actor_id\":"<<q.actor_id<<",\"actor_generation\":"<<q.actor_generation<<",\"run_generation\":"<<q.run_generation<<",\"clock\":"<<q.clock<<",\"idle_start\":"<<q.idle_start<<",\"interrupted\":"<<(q.interrupted?"true":"false")<<",\"diagnostic\":"<<json_string(q.diagnostic)<<",\"active\":";
        if(q.active)std::cout<<"{\"uid\":"<<q.active->uid<<",\"ticket\":"<<q.active->ticket<<'}';else std::cout<<"null";
        std::cout<<",\"last_completion\":";if(q.completion)std::cout<<"{\"uid\":"<<q.completion->uid<<",\"ticket\":"<<q.completion->ticket<<",\"tick\":"<<q.completion->tick<<",\"result\":"<<(q.completion->result?"true":"false")<<'}';else std::cout<<"null";
        std::cout<<",\"wait\":";if(q.wait)std::cout<<"{\"ticket\":"<<q.wait->ticket<<",\"deadline\":"<<q.wait->deadline<<'}';else std::cout<<"null";
        std::cout<<",\"suspended_active\":[";bool sc=false;for(const auto& a:q.suspended_active){if(sc)std::cout<<',';sc=true;std::cout<<"{\"uid\":"<<a.uid<<",\"ticket\":"<<a.ticket<<'}';}std::cout<<']';
        std::cout<<",\"actions\":[";bool c=false;for(const auto& a:q.actions){if(c)std::cout<<',';c=true;std::cout<<"{\"uid\":"<<a.uid<<",\"target_generation\":"<<a.target_generation<<",\"routine\":"<<a.call.key.id<<",\"owner\":"<<a.call.context.code_owner<<",\"caller\":"<<a.call.context.caller<<",\"callee\":"<<a.call.context.callee<<",\"stack_object\":"<<a.call.context.stack_object<<",\"priority\":"<<a.priority<<",\"notify_idle\":"<<(a.notify_idle?"true":"false")<<",\"arguments\":";words(a.call.arguments);std::cout<<'}';}std::cout<<"]}";};
    std::cout<<"{\"archive_path\":"<<json_string(fs::canonical(path).string())<<",\"visited_envelope\":"<<(visited?"true":"false")<<",\"tick\":"<<active.host.tick<<",\"house\":"<<active.host.house_number<<",\"entities\":[";bool comma=false;
    for(const auto* body:bodies){const auto& e=*body;bool retained=false;for(const auto& r:active.world.retained_entities)if(&r==body)retained=true;if(comma)std::cout<<',';comma=true;
        std::cout<<"{\"id\":"<<e.id<<",\"generation\":"<<e.generation<<",\"retained\":"<<(retained?"true":"false")<<",\"dead\":"<<(e.dead?"true":"false")<<",\"guid\":"<<(e.definition?e.definition->guid:0)<<",\"attributes\":";words(e.attributes);std::cout<<",\"object_words\":";pairs(e.object_words);std::cout<<",\"definition_words\":";pairs(e.definition_words);std::cout<<",\"person_data\":";words(e.person_data);std::cout<<",\"motives\":";words(e.motives);
        std::cout<<",\"position\":";if(e.position)std::cout<<'['<<e.position->x<<','<<e.position->y<<','<<e.position->level<<','<<unsigned(e.position->direction)<<']';else std::cout<<"null";
        std::cout<<",\"attachment\":";if(e.attachment)std::cout<<"{\"container\":"<<e.attachment->container<<",\"slot\":"<<e.attachment->slot<<",\"positioned\":"<<(e.attachment->positioned?"true":"false")<<'}';else std::cout<<"null";
        std::cout<<",\"native_slot0\":";if(e.native_slot0)ref(*e.native_slot0);else std::cout<<"null";std::cout<<",\"nonempty_slots\":[";bool c=false;for(std::size_t i=0;i<e.slots.size();++i)if(e.slots[i]){if(c)std::cout<<',';c=true;std::cout<<'['<<i<<','<<e.slots[i]<<']';}std::cout<<']';
        const odh::SessionObjectThread* object=nullptr;for(const auto& t:active.object_threads)if(t.id==e.id&&t.generation==e.generation){if(object)throw std::runtime_error("ambiguous runtime object thread");object=&t;}
        const odh::SessionRuntimeEntity* actor=nullptr;for(const auto& r:active.runtime_entities)if(r.id==e.id&&r.controllers.interactions.actor_generation==e.generation){if(actor)throw std::runtime_error("ambiguous runtime actor");actor=&r;}
        for(const auto& n:active.npc_avatars)if(n.origin.id==e.id&&n.origin.generation==e.generation){if(actor)throw std::runtime_error("ambiguous NPC runtime actor");actor=&n.runtime;}
        if(object&&actor)throw std::runtime_error("ambiguous object/avatar runtime");
        std::cout<<",\"runtime_kind\":"<<json_string(object?"object":actor?"avatar":"none")<<",\"primary_owner\":";if(object)std::cout<<object->primary_owner;else std::cout<<"null";
        std::cout<<",\"vm\":";if(object)vm(object->vm);else if(actor)vm(actor->vm);else std::cout<<"null";
        std::cout<<",\"sleep\":";if(object)sleep(object->sleep);else if(actor)sleep(actor->controllers.sleep);else std::cout<<"null";
        std::cout<<",\"thread_interrupt\":";if(actor)std::cout<<(actor->controllers.thread_interrupt?"true":"false");else std::cout<<"null";
        std::cout<<",\"schedule_idle_start\":";if(actor&&actor->controllers.schedule_idle_start)std::cout<<*actor->controllers.schedule_idle_start;else std::cout<<"null";
        std::cout<<",\"queue\":";if(actor)queue(actor->controllers.interactions);else std::cout<<"null";
        std::cout<<",\"resource_owners\":";if(object)owners(object->resource_owners);else if(actor)owners(actor->resource_owners);else std::cout<<"[]";
        std::cout<<",\"scheduler\":[";c=false;for(const auto& s:active.scheduler.entities)if(!retained&&s.id==e.id){if(c)std::cout<<',';c=true;std::cout<<"{\"run_generation\":"<<s.run_generation<<",\"status\":"<<json_string(odh::vm_status_name(s.status))<<",\"steps\":"<<s.steps<<",\"before\":"<<(s.before?"true":"false")<<",\"after\":"<<(s.after?"true":"false")<<",\"interrupt\":"<<(s.interrupt?"true":"false")<<'}';}std::cout<<"]}";
    }std::cout<<"]}\n";return 0;
}

// Read-only observation for the existing CAS integration executable.
int inspect_management(const fs::path& game,bool layout){
    const auto neighborhood=game/"UserData/Neighborhood.iff";
    const auto overlay=odh_host::read_linux_family_overlay_for_neighborhood(neighborhood);
    if(!overlay)throw std::runtime_error("management observation requires its actual overlay");
    const auto replay=odh::replay_family_overlay(source_families(neighborhood),odh_host::linux_neighborhood_identity(neighborhood),*overlay);
    const auto encoded=odh::encode_family_overlay(*overlay);
    const auto player=odh_host::linux_player_character_directory();
    const odh::DefinitionIndex catalog(odh::read_character_catalog(player));
    std::cout<<R"({"overlay_sha256":)"<<json_string(hex_digest(odh::sha256(encoded.data(),encoded.size())))
        <<R"(,"assignment_count":)"<<overlay->assignments.size()<<R"(,"creation_count":)"<<overlay->creations.size()
        <<R"(,"archive_count":)"<<overlay->archives.size()<<R"(,"families":[)";
    bool comma=false;
    for(const auto& entry:replay.families){if(comma)std::cout<<',';comma=true;
        const auto state=std::find_if(replay.states.begin(),replay.states.end(),[&](const auto& s){return s.family_chunk_id==entry.chunk_id;});
        if(state==replay.states.end())throw std::runtime_error("effective family lacks state");
        const auto creation=std::find_if(overlay->creations.begin(),overlay->creations.end(),[&](const auto& c){return c.family_chunk_id==entry.chunk_id;});
        std::cout<<R"({"id":)"<<entry.chunk_id<<R"(,"number":)"<<entry.family.family_number<<R"(,"house":)"<<state->house_number
            <<R"(,"epoch":)"<<state->occupancy_epoch<<R"(,"budget":)"<<state->budget<<R"(,"value_in_arch":)"<<state->value_in_arch
            <<R"(,"head":)"<<(state->checkpoint_sha256?json_string(hex_digest(*state->checkpoint_sha256)):"null")
            <<R"(,"private":)"<<(creation!=overlay->creations.end()?"true":"false")<<R"(,"name":)";
        // Only private names below are emitted as JSON text; original names may use legacy encodings.
        if(creation!=overlay->creations.end())std::cout<<json_string(entry.display_name);else std::cout<<"null";
        std::cout<<R"(,"guids":[)";for(std::size_t i=0;i<entry.family.member_guids.size();++i){if(i)std::cout<<',';std::cout<<entry.family.member_guids[i];}
        std::cout<<R"(],"members":[)";
        if(creation!=overlay->creations.end())for(std::size_t i=0;i<creation->members.size();++i){
            const auto& member=creation->members[i];if(i)std::cout<<',';
            const auto* candidate=catalog.unique(member.guid);if(!candidate)throw std::runtime_error("private creation lacks unique actual character");
            const auto path=fs::canonical(fs::path(candidate->source)/candidate->asset);
            if(path.parent_path()!=fs::canonical(player))throw std::runtime_error("private character escapes directory");
            std::optional<unsigned> strings;std::optional<std::string> name;
            iff(path,[&](const auto& view){odh::iff_chunks(view,[&](const odh::Chunk& chunk){
                if(chunk.type=="OBJD"&&chunk.id==candidate->chunk_id){const auto words=odh::read_object_definition_words(chunk.payload);
                    if(words.size()>41)strings=static_cast<std::uint16_t>(words[41]);}
            });if(strings&&*strings)odh::iff_chunks(view,[&](const odh::Chunk& chunk){
                if(chunk.type=="CTSS"&&chunk.id==*strings){if(name)throw std::runtime_error("ambiguous actual CTSS");
                    const auto table=odh::read_strings(chunk.payload);const auto* row=odh::string_in_language_or_english(table,14,0);
                    if(row)name=row->value;}
            });});
            if(!member.person_data||member.person_data->size()<=65)throw std::runtime_error("private member lacks person data");
            const auto extent=fs::file_size(path);if(extent>64u*1024u*1024u)throw std::runtime_error("private character exceeds limit");
            std::ifstream stream(path,std::ios::binary);std::vector<std::uint8_t> bytes(static_cast<std::size_t>(extent));
            if(!stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("private character read failed");
            std::cout<<R"({"guid":)"<<member.guid<<R"(,"nid":)"<<member.neighbor_id<<R"(,"age":)"<<(*member.person_data)[58]
                <<R"(,"gender":)"<<(*member.person_data)[65]<<R"(,"catalog_strings_id":)";
            if(strings)std::cout<<*strings;else std::cout<<"null";
            std::cout<<R"(,"given_name":)"<<(name?json_string(*name):"null")<<R"(,"asset":)"<<json_string(candidate->asset)
                <<R"(,"sha256":)"<<json_string(hex_digest(odh::sha256(bytes.data(),bytes.size())))<<'}';
        }
        std::cout<<"]}";
    }
    std::cout<<R"(],"unhoused_order":[)";comma=false;for(const auto& f:replay.families)
        if(f.family.house_number==0&&f.family.family_number>=0&&!f.family.member_guids.empty()&&f.family.member_guids.size()<=8){if(comma)std::cout<<',';comma=true;std::cout<<f.chunk_id;}
    std::cout<<R"(],"ui_titles":)";
#ifdef ODH_HAVE_MANAGEMENT_LAYOUT
    odh_host::GameUiStrings ui(game);
    std::cout<<R"({"map":)"<<json_string("OpenDollhouse | "+ui.get(145,1,14,"Neighborhood"))
        <<R"(,"picker":)"<<json_string("OpenDollhouse | "+ui.get(128,8,14,"SELECT A FAMILY"))
        <<R"(,"cas_family":)"<<json_string("OpenDollhouse | "+ui.get(129,9,14,"Create a Family"))
        <<R"(,"cas_character":)"<<json_string("OpenDollhouse | "+ui.get(130,15,14,"Create a Sim"))
        <<R"(,"archive":)"<<json_string("OpenDollhouse | "+ui.get(128,6,14,"Delete Family?"))<<'}';
#else
    std::cout<<"null";
#endif
    std::cout<<R"(,"vacant_click":)";
    if(layout){
#ifdef ODH_HAVE_MANAGEMENT_LAYOUT
        const auto map=odh_host::prepare_neighborhood_map(game/"UserData",game/"UIGraphics/UIGraphics.far",1,&*overlay,14);
        std::optional<odh::LotZoning> zoning;
        iff(game/"UserData/LotZoning.iff",[&](const auto& view){odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="STR#"||chunk.id!=1)return;
            if(zoning)throw std::runtime_error("ambiguous actual lot zoning");
            zoning=odh::read_lot_zoning(odh::read_strings(chunk.payload));
        });});
        if(!zoning)throw std::runtime_error("actual lot zoning is unavailable");
        bool found=false;
        for(int y=100;y<580&&!found;y+=4)for(int x=12;x<790&&!found;x+=4){const auto selected=odh_host::pick_map_lot(map,x,y);if(!selected)continue;
            const auto zone=zoning->find(static_cast<std::int16_t>(*selected));
            if(odh_host::neighborhood_lot_use(*selected,zone==zoning->end()?std::nullopt:std::optional<std::int16_t>{zone->second})!=odh_host::NeighborhoodLotUse::residential)continue;
            const auto marker=std::find_if(map.markers.begin(),map.markers.end(),[&](const auto& m){return m.location.house==*selected;});
            if(marker!=map.markers.end()&&marker->file_exists&&!marker->occupied){std::cout<<R"({"house":)"<<*selected<<R"(,"x":)"<<x<<R"(,"y":)"<<y<<'}';found=true;}}
        if(!found)throw std::runtime_error("no actual selectable vacant lot");
#else
        throw std::runtime_error("management layout requires neighborhood screen observation link");
#endif
    }else std::cout<<"null";
    std::cout<<"}\n";return 0;
}

}

int main(int argc, char** argv) try {
    if(argc>=2&&std::string(argv[1])=="--inspect-travel-return"){
        if(argc!=4)throw std::runtime_error("usage: --inspect-travel-return ARCHIVE NEIGHBORHOOD_IFF");
        return inspect_travel_return(argv[2],argv[3]);
    }
    if(argc>=2&&std::string(argv[1])=="--inspect-runtime-entity"){
        if(argc<4||argc>19)throw std::runtime_error("usage: --inspect-runtime-entity ARCHIVE ID [ID ...], maximum16");
        std::set<std::int16_t> ids;
        for(int n=3;n<argc;++n){const std::string text=argv[n];
            if(text.empty()||text.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("runtime entity ID must be decimal1..32767");
            std::size_t used=0;const auto id=std::stoul(text,&used);
            if(used!=text.size()||!id||id>32767||!ids.insert(static_cast<std::int16_t>(id)).second)throw std::runtime_error("runtime entity ID invalid or repeated");
        }
        return inspect_runtime_entities(argv[2],ids);
    }
    if((argc==3||argc==4)&&std::string(argv[1])=="--inspect-family-management"){
        if(argc==4&&std::string(argv[3])!="--map-layout")throw std::runtime_error("invalid management observation option");
        return inspect_management(fs::canonical(argv[2]),argc==4);
    }
    if(argc==3&&std::string(argv[1])=="--inspect-session")return inspect_session(argv[2]);
    if((argc==4||argc==5)&&std::string(argv[1])=="--inspect-family-state"){
        const auto game=fs::canonical(argv[2]);
        const auto number=[](const char* text){unsigned value=0;const std::string s=text;
            const auto parsed=std::from_chars(s.data(),s.data()+s.size(),value);
            if(parsed.ec!=std::errc{}||parsed.ptr!=s.data()+s.size()||!value||value>65535)
                throw std::runtime_error("invalid family/destination identity");
            return static_cast<std::uint16_t>(value);};
        const auto family=number(argv[3]);const auto neighborhood=game/"UserData/Neighborhood.iff";
        const auto overlay=odh_host::read_linux_family_overlay_for_neighborhood(neighborhood);
        if(!overlay)throw std::runtime_error("family state requires its real player overlay");
        const auto replay=odh::replay_family_overlay(source_families(neighborhood),
            odh_host::linux_neighborhood_identity(neighborhood),*overlay);
        const auto state=std::find_if(replay.states.begin(),replay.states.end(),[&](const auto& v){return v.family_chunk_id==family;});
        const auto entry=std::find_if(replay.families.begin(),replay.families.end(),[&](const auto& v){return v.chunk_id==family;});
        if(state==replay.states.end()||entry==replay.families.end())throw std::runtime_error("family state is absent");
        std::cout<<R"({"family_chunk_id":)"<<family<<R"(,"family_number":)"<<entry->family.family_number
            <<R"(,"house":)"<<state->house_number<<R"(,"occupancy_epoch":)"<<state->occupancy_epoch
            <<R"(,"budget":)"<<state->budget<<R"(,"value_in_arch":)"<<state->value_in_arch
            <<R"(,"managed":)"<<(state->managed?"true":"false")<<R"(,"member_guids":[)";
        for(std::size_t i=0;i<entry->family.member_guids.size();++i){if(i)std::cout<<',';std::cout<<entry->family.member_guids[i];}
        std::cout<<R"(],"move_out_quote_budget":)";
        if(state->house_number){const auto quote=odh::plan_family_managed_move_out_overlay(source_families(neighborhood),
            odh_host::linux_neighborhood_identity(neighborhood),&*overlay,family);
            if(quote.assignments.empty())throw std::runtime_error("move-out quote has no actual operation");
            std::cout<<quote.assignments.back().budget_after_purchase;
        }else std::cout<<"null";
        std::cout<<R"(,"destination_house":)";
        if(argc==5){const auto destination=number(argv[4]);
            std::ostringstream filename;filename<<"House";if(destination<10)filename<<'0';filename<<destination<<".iff";
            std::cout<<destination<<R"(,"destination_price":)"<<purchase_price(game/"UserData/Houses"/filename.str());
        }else std::cout<<"null";
        std::cout<<"}\n";return 0;
    }
    if(argc==5&&std::string(argv[1])=="--inspect-session-head"){
        const auto game=fs::canonical(argv[2]);
        const auto number=[](const char* text){
            unsigned value=0;const std::string s=text;
            const auto parsed=std::from_chars(s.data(),s.data()+s.size(),value);
            if(parsed.ec!=std::errc{}||parsed.ptr!=s.data()+s.size()||value==0||value>65535)
                throw std::runtime_error("invalid journal house/family identity");
            return static_cast<std::uint16_t>(value);
        };
        const auto neighborhood=game/"UserData/Neighborhood.iff";
        odh_host::FamilySessionJournal journal(source_families(neighborhood),
            odh_host::linux_neighborhood_identity(neighborhood),
            odh_host::read_linux_family_overlay_for_neighborhood(neighborhood),number(argv[3]),number(argv[4]));
        const auto path=journal.checkpoint_path();
        if(!path)throw std::runtime_error("family has no current journal checkpoint");
        const auto size=fs::file_size(*path);
        if(size>256u*1024u*1024u)throw std::runtime_error("journal head exceeds inspector limit");
        std::ifstream input(*path,std::ios::binary);std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if(!input||(size&&!input.read(reinterpret_cast<char*>(bytes.data()),bytes.size())))
            throw std::runtime_error("cannot read journal head");
        journal.validate_restore(bytes);
        const auto bundle=odh::decode_session_bundle(bytes);
        if(bundle.travel)odh_host::reopen_travel_session_checkpoint(journal,bytes);
        return inspect_session(*path,&bytes);
    }
    if(argc==4&&std::string(argv[1])=="--inspect-property-head"){
        unsigned value=0;const std::string text=argv[3];const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!value||value>98)throw std::runtime_error("invalid released house");
        return inspect_property(fs::canonical(argv[2]),static_cast<std::uint16_t>(value));
    }
    const bool reoccupation=argc==5&&std::string(argv[3])=="--create-reoccupation-family";
    const bool integrated=(argc==4&&std::string(argv[3])=="--family-lot-flow")||reoccupation;
    const bool source_occupied = argc == 5 && std::string(argv[3]) == "--create-only-occupied-house";
    if ((argc != 3 && argc != 4 && !source_occupied&&!reoccupation) ||
        (argc == 4 && std::string(argv[3]) != "--create-only"&&!integrated))
        throw std::runtime_error("usage: odh-cas-movein-e2e GAME_ROOT PRIVATE_PROFILE_ROOT [--create-only | --create-only-occupied-house HOUSE | --family-lot-flow]; or --inspect-session ARCHIVE");
    const bool create_only = argc >= 4;
    unsigned selected_source_house = 0;
    if (source_occupied||reoccupation) {
        const std::string text = argv[4];
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), selected_source_house);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || selected_source_house < 1 || selected_source_house > 98)
            throw std::runtime_error("source occupied house must be in 1..98");
    }
    const fs::path game = fs::canonical(argv[1]);
    const fs::path profile = fs::absolute(argv[2]).lexically_normal();
    if(integrated)for(const auto& entry:{std::make_pair("XDG_CONFIG_HOME","config"),std::make_pair("XDG_DATA_HOME","data")}){
        const auto* configured=std::getenv(entry.first);
        if(!configured||fs::absolute(configured).lexically_normal()!=profile/entry.second)
            throw std::runtime_error("integrated CAS requires XDG_CONFIG_HOME and XDG_DATA_HOME inside its supplied private profile");
    }
    const fs::path user = game / "UserData";
    const auto families = source_families(user / "Neighborhood.iff");
    const odh::FamilyContext family_context(families);

    std::uint16_t house = 0;
    fs::path house_path;
    std::int32_t price = 0, second_price = 0;
    std::uint16_t second_house = 0;
    const odh::FamilyEntry* source_family = nullptr;
    std::optional<odh::SessionEntry> source_session;
    const auto lot_locations = locations(user / "LotLocations.iff");
    if(reoccupation){
        house=static_cast<std::uint16_t>(selected_source_house);
        const auto existing=odh_host::read_linux_family_overlay_for_neighborhood(user/"Neighborhood.iff");
        if(!existing)throw std::runtime_error("reoccupation CAS requires an existing overlay");
        const auto property=current_property(game,*existing,house);
        const auto replay=odh::replay_family_overlay(families,odh_host::linux_neighborhood_identity(user/"Neighborhood.iff"),*existing);
        for(const auto& state:replay.states)if(state.house_number==house)throw std::runtime_error("released house is already occupied");
        price=property.second.vacant_purchase_value;
        if(price<0||price>=20000)throw std::runtime_error("released house is not affordable at normal CAS budget");
        std::ostringstream file;file<<"House";if(house<10)file<<'0';file<<house<<".iff";house_path=user/"Houses"/file.str();
    }
    if (source_occupied) {
        house = static_cast<std::uint16_t>(selected_source_house);
        source_family = family_context.by_house(static_cast<std::int16_t>(house));
        if (!source_family) throw std::runtime_error("selected source house has no original family");
        bool listed = false;
        for (const auto& lot : lot_locations.lots) if (lot.house == house) listed = true;
        if (!listed) throw std::runtime_error("selected source house is absent from LotLocations");
        std::ostringstream name;
        name << "House" << (house < 10 ? "0" : "") << house << ".iff";
        house_path = user / "Houses" / name.str();
        price = purchase_price(house_path);
        if (price < 0) throw std::runtime_error("selected source house has negative purchase price");
        source_session = odh::load_session_entry(user, 1, house, 1);
    }
    if(!reoccupation)for (const auto& lot : lot_locations.lots) {
        if (lot.house < 1 || lot.house > 98 || family_context.by_house(static_cast<std::int16_t>(lot.house))) continue;
        std::ostringstream name;
        name << "House" << (lot.house < 10 ? "0" : "") << lot.house << ".iff";
        const auto candidate = user / "Houses" / name.str();
        if (!fs::is_regular_file(candidate)) continue;
        std::int32_t candidate_price = 0;
        try { candidate_price = purchase_price(candidate); }
        catch (const std::exception&) { continue; }
        if (candidate_price < 0 || (integrated&&candidate_price>=20000)) continue;
        if (!house) {
            price = candidate_price;
            house = lot.house;
            house_path = candidate;
            if (!create_only) break;
        } else if (lot.house != house && (!second_house || candidate_price < second_price)) {
            second_house = lot.house;
            second_price = candidate_price;
        }
    }
    if (!house) throw std::runtime_error(integrated?"no vacant selectable lot affordable within the normal 20000 budget with funds left for editing":"no vacant selectable base-game house with SIMI1 found");

    const auto cas = odh_host::scan_linux_cas_catalog(game);
    const auto heads = cas.heads("ma", "lgt");
    const auto bodies = cas.bodies("ma", "lgt");
    if (heads.empty() || bodies.empty()) throw std::runtime_error("real CAS catalog has no adult male light-skin choices");
    if (price > std::numeric_limits<std::int32_t>::max() - 20000)
        throw std::runtime_error("selected lot price leaves no safe budget headroom");
    const auto initial_budget_wide = integrated ? 20000LL : create_only ?
        2LL * price + second_price + 20000 : static_cast<std::int64_t>(price) + 20000;
    if (initial_budget_wide > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("vacant lot round trip exceeds safe initial CAS budget");
    const auto initial_budget = static_cast<std::int32_t>(initial_budget_wide);
    std::uint32_t cas_random = 0;
    const auto make_member = [&](std::string name,bool child=false) {
        const auto selected_heads=child?cas.heads("mc","lgt"):heads;
        const auto selected_bodies=child?cas.bodies("mcchd","lgt"):bodies;
        if(selected_heads.empty()||selected_bodies.empty())throw std::runtime_error("real CAS catalog lacks the requested adult/child appearance");
        const auto& head=selected_heads.front();const auto& body=selected_bodies.front();
        odh_host::SimitoneCasPersonInput input;
        input.child=child;
        input.name_utf8 = std::move(name);
        input.biography_utf8 = "Opt-in integration gate";
        input.language = 1;
        input.skin = "lgt";
        input.body_type = body.body_type;
        input.body_appearance = body.appearance;
        input.body_texture = body.texture;
        input.head_appearance = head.appearance;
        input.head_texture = head.texture;
        input.handgroup_texture = body.handgroup_texture.empty() ? head.handgroup_texture : body.handgroup_texture;
        input.personality_points = {5, 5, 5, 5, 5};
        return odh_host::make_simitone_cas_member(input,
            [&cas_random](std::uint32_t upper) { return (cas_random++) % upper; });
    };
    const auto first_draft = make_member("ODH CAS Gate A");
    const auto second_draft = make_member(integrated?"ODH CAS Child":"ODH CAS Gate B",integrated);

    const auto neighborhood = user / "Neighborhood.iff";
    const auto identity = odh_host::linux_neighborhood_identity(neighborhood);
    const auto overlay_path = odh_host::linux_family_overlay_path(identity);
    if (!reoccupation&&fs::exists(overlay_path)) throw std::runtime_error("temporary profile unexpectedly already has a family overlay");
    const auto characters = profile / "data/opendollhouse/Characters";
    const auto created = odh_host::create_linux_private_family(template_person(game),
        user / "Characters", characters, neighborhood, overlay_path,
        reoccupation?"ODH New Residents":"ODH CAS Gate", {first_draft, second_draft}, initial_budget);
    auto overlay = odh_host::read_linux_family_overlay(overlay_path);
    if (!overlay) throw std::runtime_error("CAS creation did not persist a player overlay");
    if (source_occupied) {
        // Explicit test setup: expire only the selected original occupancy via
        // the native legacy planner, leaving all commercial resources untouched.
        const auto vacated = odh::plan_family_move_out_overlay(families, identity, &*overlay, source_family->chunk_id);
        odh_host::compare_exchange_linux_family_overlay_atomic(overlay_path, overlay, vacated);
        overlay = vacated;
    }
    const auto moved = create_only ?
        odh::plan_family_managed_move_in_overlay(families, identity, &*overlay, created.family_chunk_id, house, price) :
        odh::plan_family_move_in_overlay(families, identity, &*overlay, created.family_chunk_id, house, price);
    if (!create_only) odh_host::compare_exchange_linux_family_overlay_atomic(overlay_path, overlay, moved);
    // Resolve prospective members in create-only mode without housing the
    // persisted family: the real runtime owns deferred purchase publication.
    overlay = moved;

    const auto session = odh::load_session_entry(user, 1, house, 1, &*overlay, characters);
    if (session.members.size() != 2 || created.members.size() != 2 ||
        session.members.front().guid != created.members.front().guid ||
        session.members.front().character_source != "PlayerData/Characters" ||
        session.members.back().guid != created.members.back().guid ||
        session.members.back().character_source != "PlayerData/Characters")
        throw std::runtime_error("load_session_entry did not resolve the created private CAS member");

    std::cout << "{\"status\":\"passed\",\"mode\":1,\"house\":" << house
              << ",\"purchase_price\":" << price << ",\"family_chunk_id\":" << created.family_chunk_id
              << ",\"family_number\":" << created.family_number << ",\"member_count\":" << created.members.size()
              << ",\"guid\":" << created.members.front().guid
              << ",\"neighbor_id\":" << session.members.front().neighbor_id
              << ",\"member_source\":\"" << session.members.front().character_source
              << "\",\"private_character\":\"" << created.members.front().path.string()
              << "\",\"house_file\":\"" << house_path.string()
              << "\",\"create_only\":" << (create_only ? "true" : "false")
              << (integrated?",\"integrated_family_lot\":true":"")
              << ",\"initial_budget\":" << initial_budget
              << ",\"second_house\":" << second_house << ",\"second_purchase_price\":" << second_price
              << ",\"source_house\":" << (source_occupied ? house : 0)
              << ",\"source_vacated_family_id\":" << (source_family ? source_family->chunk_id : 0)
              << ",\"source_expired_guids\":[";
    if (source_session) for (std::size_t i = 0; i < source_session->members.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << source_session->members[i].guid;
    }
    std::cout << "],\"source_expired_neighbor_ids\":[";
    if (source_session) for (std::size_t i = 0; i < source_session->members.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << source_session->members[i].neighbor_id;
    }
    std::cout << "],\"members\":[";
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < session.members.size(); ++i) {
        if (i) std::cout << ',';
        const auto& member = session.members[i];
        std::string fingerprint;
        for (const auto byte : created.members[i].content_sha256) {
            fingerprint += hex[byte >> 4]; fingerprint += hex[byte & 15];
        }
        std::string body_type;
        if(integrated)iff(created.members[i].path,[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
            if(chunk.type=="STR#"&&chunk.id==200){const auto strings=odh::read_strings(chunk.payload);
                if(const auto* type=odh::string_in_language(strings,1,0))body_type=type->value;}
        });});
        const auto& draft=i==0?first_draft:second_draft;
        if(integrated&&(draft.person_data.size()<=58||draft.person_data[58]!=(i==0?27:9)||body_type!=(i==0?"adult":"child")))
            throw std::runtime_error("created family age and actual private character body type disagree");
        std::cout << '{';
        if(integrated)std::cout<<"\"age\":"<<draft.person_data[58]<<",\"body_type\":"<<json_string(body_type)<<',';
        std::cout << "\"guid\":" << member.guid << ",\"neighbor_id\":" << member.neighbor_id
            << ",\"object_definition_id\":" << member.object_definition_id
            << ",\"character_asset\":\"" << member.character_source << '/' << member.character_asset
            << "\",\"character_sha256\":\"" << fingerprint << "\"}";
    }
    std::cout << "]}\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "odh-cas-movein-e2e: " << error.what() << '\n';
    return 1;
}
