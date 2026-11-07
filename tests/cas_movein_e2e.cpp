#include "linux_cas_catalog.hpp"
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
#include "odh/animation.hpp"

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
    std::cout<<R"(],"architecture":)";
    if(bundle.architecture){
        const auto& prepared=bundle.architecture->prepared;
        std::cout<<R"({"width":)"<<prepared.width<<R"(,"height":)"<<prepared.height<<R"(,"floors":[)";
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

}

int main(int argc, char** argv) try {
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
    const bool integrated=argc==4&&std::string(argv[3])=="--family-lot-flow";
    const bool source_occupied = argc == 5 && std::string(argv[3]) == "--create-only-occupied-house";
    if ((argc != 3 && argc != 4 && !source_occupied) ||
        (argc == 4 && std::string(argv[3]) != "--create-only"&&!integrated))
        throw std::runtime_error("usage: odh-cas-movein-e2e GAME_ROOT PRIVATE_PROFILE_ROOT [--create-only | --create-only-occupied-house HOUSE | --family-lot-flow]; or --inspect-session ARCHIVE");
    const bool create_only = argc >= 4;
    unsigned selected_source_house = 0;
    if (source_occupied) {
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
    for (const auto& lot : lot_locations.lots) {
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
    if (fs::exists(overlay_path)) throw std::runtime_error("temporary profile unexpectedly already has a family overlay");
    const auto characters = profile / "data/opendollhouse/Characters";
    const auto created = odh_host::create_linux_private_family(template_person(game),
        user / "Characters", characters, neighborhood, overlay_path,
        "ODH CAS Gate", {first_draft, second_draft}, initial_budget);
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
