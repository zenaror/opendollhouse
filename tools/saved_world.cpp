#include "saved_resources.hpp"
#include "odh/saved_thread_plan.hpp"
#include "saved_thread_plan_audit.hpp"
#include "saved_tail_json.hpp"
#include <iostream>

int main(int argc,char** argv){try{
    if(argc<2)throw odh::FormatError("usage: HOUSE_IFF --object-definitions FAR [--object-definition-file IFF] [--neighborhood IFF] [--verify-saved-threads [--semiglobal FAR ENTRY] [--global FAR ENTRY]]");
    const std::string house=argv[1];std::vector<SavedDefinitionSource> sources;std::optional<std::string> neighborhood;
    std::optional<SavedThreadCodeResource> semiglobal,global;bool verify_saved_threads=false;
    for(int n=2;n<argc;++n){const std::string option=argv[n];
        if((option=="--object-definitions"||option=="--object-definition-file")&&n+1<argc){
            if(sources.size()>=16)throw odh::FormatError("definition source count exceeds limit");
            sources.push_back({argv[++n],option=="--object-definition-file"});
        }else if(option=="--neighborhood"&&!neighborhood&&n+1<argc)neighborhood=argv[++n];
        else if(option=="--semiglobal"&&!semiglobal&&n+2<argc){semiglobal=SavedThreadCodeResource{argv[n+1],argv[n+2]};n+=2;}
        else if(option=="--global"&&!global&&n+2<argc){global=SavedThreadCodeResource{argv[n+1],argv[n+2]};n+=2;}
        else if(option=="--verify-saved-threads"&&!verify_saved_threads)verify_saved_threads=true;
        else throw odh::FormatError("invalid saved World option");
    }
    if(sources.empty())throw odh::FormatError("explicit definition source required");
    if((semiglobal&&(semiglobal->archive.size()>4096||semiglobal->entry.size()>4096))||
       (global&&(global->archive.size()>4096||global->entry.size()>4096)))
        throw odh::FormatError("saved-thread resource path exceeds limit");
    auto index=saved_definition_index(sources);odh::Objt object_types;auto records=saved_world_records(house,*index,&object_types);
    std::optional<odh::NeighborContext> neighbors;if(neighborhood)neighbors.emplace(saved_neighbors(*neighborhood));
    auto loaded=odh::make_saved_world(records,*index,neighbors?&*neighbors:nullptr);
    std::vector<std::optional<odh::SimitoneSavedThreadPlan>> thread_plans(records.size());
    std::vector<std::vector<SavedThreadCodeCheck>> thread_checks(records.size());
    for(std::size_t n=0;n<records.size();++n){
        if(!records[n].tail.person)continue;
        thread_plans[n]=odh::make_simitone_saved_thread_plan(records[n],object_types);
        if(verify_saved_threads)
            thread_checks[n]=audit_saved_thread_plan(*thread_plans[n],*index,semiglobal,global);
    }
    std::cout<<"{\"evidence\":\"ODH_saved_dormant_World_import\",\"house\":"<<json_bytes(house)
        <<",\"world_entities_restored\":true,\"saved_threads_restored\":false,\"architecture_loaded\":false,\"rendered\":false"
        <<",\"profile\":\"saved identity/banks/positions/containment/object relationships; dormant frames and raw source retained; no original runtime proof\""
        <<",\"definition_guids\":"<<index->guid_count()<<",\"entities\":"<<loaded->records.size()
        <<",\"world_words\":"<<loaded->world->word_count()<<",\"relationship_sources\":"<<loaded->relationships->source_count()
        <<",\"relationship_pairs\":"<<loaded->relationships->pair_count()<<",\"relationship_values\":"<<loaded->relationships->value_count()<<",\"records\":[";
    for(std::size_t n=0;n<loaded->records.size();++n){if(n)std::cout<<',';const auto& raw=loaded->records[n];
        const auto* entity=loaded->world->find(static_cast<std::int16_t>(raw.saved.object_id));
        const auto* definition=index->unique(raw.saved.guid);
        std::cout<<"{\"id\":"<<entity->id<<",\"generation\":"<<entity->generation<<",\"kind\":"<<json_bytes(entity->kind==odh::EntityKind::avatar?"avatar":"object")
            <<",\"guid\":"<<entity->definition->guid<<",\"master_guid\":"<<(entity->definition->master_guid?std::to_string(*entity->definition->master_guid):"null")
            <<",\"tree_table_id\":"<<(definition?std::to_string(definition->definition.tree_table_id):"null")
            <<",\"position\":";
        if(entity->position){const auto& p=*entity->position;std::cout<<"{\"x\":"<<p.x<<",\"y\":"<<p.y<<",\"level\":"<<p.level<<",\"direction\":"<<static_cast<unsigned>(p.direction)<<'}';}
        else std::cout<<"null";
        std::cout<<",\"attachment\":";
        if(entity->attachment)std::cout<<"{\"container\":"<<entity->attachment->container<<",\"slot\":"<<entity->attachment->slot<<'}';else std::cout<<"null";
        std::cout<<",\"attributes\":";odh_tools::saved_bank(std::cout,entity->attributes);
        std::cout<<",\"person_data\":";odh_tools::saved_bank(std::cout,entity->person_data);
        std::cout<<",\"motives\":";odh_tools::saved_bank(std::cout,entity->motives);
        std::cout<<",\"slots\":";odh_tools::saved_bank(std::cout,entity->slots);
        std::cout<<",\"runtime_object_data\":[";
        const odh::VmFrame frame{odh::routine_key(4096),0,{entity->id,entity->id,entity->id,0},{},{}};
        for(std::int16_t i=0;i<68;++i){if(i)std::cout<<',';const auto value=loaded->world->read(frame,3,i);std::cout<<(value?std::to_string(*value):"null");}
        std::cout<<"],\"raw_object_data\":";odh_tools::saved_bank(std::cout,entity->saved_object_banks->object_data);
        std::cout<<",\"raw_extras\":";odh_tools::saved_bank(std::cout,entity->saved_object_banks->extras);
        std::cout<<",\"raw_tail\":";odh_tools::saved_tail(std::cout,raw.tail);
        std::cout<<",\"simitone_thread_plan\":";
        if(raw.tail.person){
            const auto& plan=*thread_plans[n];
            std::cout<<"{\"profile\":\"pinned_simitone_frame_mapping\",\"executable\":false,\"code_verified\":"<<(verify_saved_threads?"true":"false")<<",\"entity_id\":"<<plan.entity_id
                <<",\"temp_registers\":";odh_tools::saved_bank(std::cout,plan.temp_registers);
            std::cout<<",\"frames\":[";
            for(std::size_t f=0;f<plan.frames.size();++f){if(f)std::cout<<',';const auto& frame=plan.frames[f];
                std::cout<<"{\"routine_id\":"<<frame.routine_id<<",\"instruction_pointer\":"<<frame.instruction_pointer
                    <<",\"stack_object\":"<<frame.stack_object<<",\"primitive_state_preserved_but_ignored\":"<<frame.saved_primitive_state
                    <<",\"code_owner_objt_type\":"<<frame.code_owner_objt_type<<",\"code_owner_guid\":"<<frame.code_owner_guid
                    <<",\"caller\":"<<frame.caller<<",\"callee\":"<<frame.callee<<",\"arguments\":";
                odh_tools::saved_bank(std::cout,frame.arguments);std::cout<<",\"locals\":";odh_tools::saved_bank(std::cout,frame.locals);
                if(verify_saved_threads){
                    const auto& check=thread_checks[n].at(f);
                    std::cout<<",\"code_check\":{\"status\":\"present_and_in_range\",\"scope\":"<<json_bytes(check.scope)
                        <<",\"archive\":"<<json_bytes(check.archive)<<",\"entry\":"<<json_bytes(check.entry)
                        <<",\"instruction_count\":"<<check.instruction_count<<'}';
                }
                std::cout<<'}';
            }
            std::cout<<"]}";
        }else std::cout<<"null";
        std::cout<<",\"saved_stack_depth\":"<<raw.common.stack.size()<<",\"object_relationships\":[";
        for(std::size_t r=0;r<raw.common.relationships.size();++r){
            if(r)std::cout<<',';
            const auto& relationship=raw.common.relationships[r];
            std::cout<<"{\"target\":"<<relationship.target_id<<",\"raw_values\":";
            odh_tools::saved_bank(std::cout,relationship.values);
            std::cout<<",\"runtime_values\":[";
            for(std::size_t i=0;i<relationship.values.size();++i){
                if(i)std::cout<<',';
                const auto value=loaded->relationships->object_value(*loaded->world,entity->id,
                    static_cast<std::int16_t>(relationship.target_id),i);
                std::cout<<(value?std::to_string(*value):"null");
            }
            std::cout<<"]}";
        }
        std::cout<<"]}";
    }
    std::cout<<"],\"multitile_groups\":[";
    for(std::size_t n=0;n<loaded->multitile_groups.size();++n){if(n)std::cout<<',';const auto& g=loaded->multitile_groups[n];
        std::cout<<"{\"leader\":"<<g.leader<<",\"master_guid\":"<<(g.master_guid?std::to_string(*g.master_guid):"null")<<",\"members\":";
        odh_tools::saved_bank(std::cout,g.members);std::cout<<'}';
    }
    std::cout<<"]}\n";return 0;
}catch(const std::exception& e){std::cerr<<"saved World error: "<<e.what()<<'\n';return 2;}}
