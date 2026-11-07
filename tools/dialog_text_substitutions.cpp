// SPDX-License-Identifier: MPL-2.0
#include "dialog_text_substitutions.hpp"
#include <stdexcept>
namespace odh_host {
namespace {
using Availability=DialogTextAvailability;
std::string fail(const char* message){throw std::invalid_argument(message);}
std::string value(DialogTextValue result,const char* absent){
    if(result.availability==Availability::unavailable)return fail("dialog text resource authority unavailable");
    return result.availability==Availability::absent?std::string(absent):std::move(result.utf8);
}
std::string expand(const std::string& input,const DialogTextContext& context,const DialogTextProviders& providers,unsigned depth,DialogTextBudget& budget){
    if(++budget.expansions>256)return fail("dialog cumulative expansion budget exhausted");
    const auto lookup=[&]{if(++budget.lookups>1024)throw std::invalid_argument("dialog cumulative lookup budget exhausted");};
    if(depth>10)return fail("dialog dynamic-string recursion budget exhausted");
    const auto local=[&](std::uint16_t index){if(index>=context.frame.locals.size())return std::optional<std::int16_t>{};return std::optional<std::int16_t>{context.frame.locals[index]};};
    const auto name=[&](const odh::Entity* entity,DialogEntityCatalogPolicy policy)->std::string{
        if(!entity)return fail("dialog display-name entity authority unavailable");
        if(entity->name_override&&!entity->name_override->empty())return *entity->name_override;
        if(providers.entity_runtime_name){const auto selected=providers.entity_runtime_name(*entity);
            if(selected.availability==Availability::value&&!selected.utf8.empty())return selected.utf8;
            if(selected.availability==Availability::unavailable)return fail("dialog actual group/avatar Name owner unavailable");
        }else if(entity->kind==odh::EntityKind::avatar)return fail("dialog actual avatar Name owner unavailable");
        if(!providers.entity_catalog)return fail("dialog entity catalog backend unavailable");
        lookup();const auto result=providers.entity_catalog(*entity,0,policy);
        if(result.availability!=Availability::value)return fail("dialog entity ToString/catalog fallback authority unavailable");return result.utf8;
    };
    return odh::expand_dialog_text(input,[&](std::string_view token,const auto& indexes)->std::optional<std::string>{
        const auto index=[&](){if(indexes.size()!=1)throw std::invalid_argument("dialog token requires one selector");return indexes[0];};
        if(token=="Me")return name(context.world.frame_entity(context.frame,odh::VmEntityRole::caller),DialogEntityCatalogPolicy::own);
        if(token=="Object"||token=="DynamicObjectName"){
            if(context.frame.context.stack_object==0)return std::string{}; // known null source object
            return name(context.world.frame_entity(context.frame,odh::VmEntityRole::stack_object),DialogEntityCatalogPolicy::prefer_master);
        }
        if(token=="Neighbor"){
            if(!context.neighbors)return fail("dialog neighbor authority unavailable");
            const auto* neighbor=context.neighbors->by_id(context.frame.context.stack_object);
            // Pinned VMDialogHandler explicitly displays Unknown for a known
            // missing neighbor/object/catalog. Unknown providers still refuse.
            if(!neighbor)return std::string{"Unknown"};
            if(!providers.neighbor_catalog)return fail("dialog neighbor character catalog backend unavailable");
            lookup();return value(providers.neighbor_catalog(neighbor->guid,0),"Unknown");
        }
        if(token=="Local:"||token=="FixedLocal:"){
            const auto word=local(index());if(!word)return fail("dialog local selector unavailable");
            if(token=="Local:")return std::to_string(*word);
            if(!providers.fixed)return fail("dialog fixed locale formatter unavailable");return providers.fixed(*word);
        }
        if(token=="Param:"){const auto at=index();if(at>=context.frame.arguments.size())return fail("dialog argument selector unavailable");return std::to_string(context.frame.arguments[at]);}
        if(token=="Temp:"||token=="TempXL:"||token=="MoneyXL:"){
            if(!context.source_vm)return fail("dialog executing thread register authority unavailable");const auto at=index();
            if(token=="Temp:"){if(at>=context.source_vm->temps().size())return fail("dialog Temp selector unavailable");return std::to_string(context.source_vm->temps()[at]);}
            if(at>=context.source_vm->temp_xl().size())return fail("dialog TempXL selector unavailable");const auto word=context.source_vm->temp_xl()[at];
            if(token=="TempXL:")return std::to_string(word);
            if(!providers.money)return fail("dialog currency locale formatter unavailable");return providers.money(word);
        }
        if(token=="Attribute:"){const auto word=context.world.read(context.frame,1,static_cast<std::int16_t>(index()));if(!word)return fail("dialog bound StackObject attribute unavailable");return std::to_string(*word);}
        if(token=="NameLocal:"||token=="ObjectLocal:"||token=="CatalogLocal:"){
            const auto id=local(index());if(!id)return fail("dialog local object selector unavailable");const auto* entity=context.world.find(*id);
            if(!entity)return std::string{}; // explicit numeric GetObjectById null, unlike stale bound context
            if(token!="CatalogLocal:")return name(entity,DialogEntityCatalogPolicy::own);
            if(!providers.entity_catalog)return fail("dialog catalog description backend unavailable");lookup();return value(providers.entity_catalog(*entity,1,DialogEntityCatalogPolicy::own),"");
        }
        if(token=="ListObject"){
            const auto* entity=context.world.frame_entity(context.frame,odh::VmEntityRole::stack_object);
            if(!entity||!providers.list_utf8)return fail("dialog native object-list owner unavailable");return providers.list_utf8(*entity);
        }
        if(token=="DateLocal:"){
            if(indexes.size()!=3||!providers.date)return fail("dialog long-date locale formatter unavailable");
            const auto day=local(indexes[0]),month=local(indexes[1]),year=local(indexes[2]);
            if(!day||!month||!year)return fail("dialog date local selectors unavailable");return providers.date(*day,*month,*year);
        }
        if(token=="Job:"||token=="JobDesc:"||token=="JobOffer:"||token=="TimeLocal:"){
            if(!providers.career_time)return fail("dialog original career/time backend unavailable");return providers.career_time(token,indexes);
        }
        if(token=="DynamicStringLocal:"){
            if(indexes.empty()||indexes.size()>3||!providers.string)return fail("dialog dynamic string backend or selectors unavailable");
            const auto raw_index=local(indexes[0]);if(!raw_index)return fail("dialog dynamic string index local unavailable");
            std::vector<DialogTextTable> tables;
            if(indexes.size()==1){if(!context.current_table)return fail("dialog current STR authority unavailable");tables.push_back(*context.current_table);}
            else {
                const auto raw_table=local(indexes[1]);if(!raw_table)return fail("dialog dynamic table local unavailable");const auto table=static_cast<std::uint16_t>(*raw_table);
                if(indexes.size()==2){if(!providers.global_table)return fail("dialog global STR authority unavailable");const auto key=providers.global_table(table);if(!key)return fail("dialog global STR descriptor unavailable");tables.push_back(*key);}
                else {const auto raw_entity=local(indexes[2]);if(!raw_entity)return fail("dialog dynamic object local unavailable");const auto* entity=context.world.find(*raw_entity);if(!entity)return std::string{};
                    if(!providers.entity_tables)return fail("dialog entity STR lookup order unavailable");const auto keys=providers.entity_tables(*entity,table);if(!keys)return fail("dialog entity STR descriptor authority unavailable");tables=*keys;}
            }
            for(const auto& table:tables){lookup();auto text=providers.string(table,static_cast<std::uint16_t>(*raw_index));
                if(text.availability==Availability::unavailable)return fail("dialog dynamic STR resource unavailable");
                if(text.availability==Availability::absent)continue;
                auto nested=context;nested.current_table=table;return expand(text.utf8,nested,providers,depth+1,budget);
            }
            return std::string{}; // known absent tables, not unknown ownership
        }
        return {};
    });
}
}
std::optional<std::string> expand_ts1_dialog_text(const std::string& utf8,const DialogTextContext& context,const DialogTextProviders& providers,std::string& diagnostic){
    diagnostic.clear();DialogTextBudget local_budget;auto& budget=context.budget?*context.budget:local_budget;try{return expand(utf8,context,providers,0,budget);}catch(const std::exception& error){diagnostic=error.what();return {};}
}
}
