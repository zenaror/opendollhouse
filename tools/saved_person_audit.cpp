#include "saved_resources.hpp"

#include <iomanip>
#include <iostream>

namespace {
void print_interaction(const odh::ObjmSavedInteraction& action) {
    std::cout << "    action uid=" << action.uid
              << " caller=" << action.caller_id
              << " target=" << action.target_id
              << " tta=" << action.tta_index
              << " priority=" << action.priority
              << " tree=" << action.action_tree_id
              << " flags=" << action.flags << '\n';
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2)
            throw odh::FormatError("usage: HOUSE_IFF [--object-definitions FAR | --object-definition-file IFF]...");
        const std::string house = argv[1];
        std::vector<SavedDefinitionSource> sources;
        for (int n = 2; n < argc; ++n) {
            const std::string option = argv[n];
            if ((option == "--object-definitions" || option == "--object-definition-file") && n + 1 < argc) {
                if (sources.size() >= 128) throw odh::FormatError("definition source count exceeds limit");
                sources.push_back({argv[++n], option == "--object-definition-file"});
            } else {
                throw odh::FormatError("invalid saved person audit option");
            }
        }

        auto definitions = saved_definition_index(sources);
        with_iff_file(house, [&](const odh::View& view) {
            std::optional<odh::Objm> objects;
            std::optional<odh::Objt> types;
            odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
                if (chunk.type == "ObjM" && chunk.id == 1) {
                    if (objects) throw odh::FormatError("duplicate ObjM1");
                    objects = odh::read_objm(chunk.payload);
                }
                if (chunk.type == "objt" && chunk.id == 0) {
                    if (types) throw odh::FormatError("duplicate objt0");
                    types = odh::read_objt(chunk.payload);
                }
            });
            if (!objects || !types) throw odh::FormatError("ObjM1/objt0 absent");

            const auto saved = odh::read_saved_object_prefixes(*objects, *types);
            std::size_t people = 0;
            std::cout << "Saved Person records (read only; no World or action execution)\n";
            for (std::size_t n = 0; n < saved.size(); ++n) {
                const auto& record = saved[n];
                const auto* entry = types->entry_for_type(record.type_id);
                if (!entry || !entry->metadata) throw odh::FormatError("saved Person OBJT entry unavailable");
                if (entry->metadata->objd_type != 2) continue;

                const auto* matches = definitions->candidates(record.guid);
                const auto* definition = definitions->unique(record.guid);
                if (matches && !definition)
                    throw odh::FormatError("saved Person OBJD ambiguous for object " + std::to_string(record.object_id));

                const auto common = odh::read_objm_common(objects->blocks[n], record.prefix);
                const auto tail = definition
                    ? odh::read_objm_tail(objects->blocks[n], common, objects->version, definition->definition, *entry)
                    : odh::read_objm_person_tail_without_definition(objects->blocks[n], common,
                        objects->version, *entry, entry->name.empty());
                if (!tail.person) throw odh::FormatError("saved Person type did not produce Person data");

                const auto& person = *tail.person;
                const auto neighbor = person.person_data.size() > 31 ? person.person_data[31] : 0;
                std::cout << "  id=" << record.object_id << " name=" << std::quoted(entry->name)
                          << " guid=" << record.guid << " neighbor_id=" << neighbor
                          << " stack=" << common.stack.size()
                          << " definition=" << (definition ? "OBJD" : "missing; Simitone OBJT-name fallback")
                          << " multitile=" << (tail.multitile ? "yes" : "no")
                          << " routing_frames=" << person.routing_frame_count
                          << " active_uid=" << person.active_interaction.uid
                          << " last_uid=" << person.last_interaction.uid
                          << " queued=" << person.interaction_queue.size()
                          << " object_uses=" << person.object_uses.size() << '\n';
                for (const auto& action : person.interaction_queue) print_interaction(action);
                ++people;
            }
            std::cout << "Person count: " << people << " of " << saved.size()
                      << " saved objects; definition candidates: " << definitions->candidate_count() << '\n';
        });
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "saved Person audit error: " << error.what() << '\n';
        return 2;
    }
}
