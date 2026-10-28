#include "property_architecture_price_resources.hpp"
#include "json.hpp"
#include <iostream>

int main(int argc,char** argv){try{
    if(argc<2)throw odh::FormatError("usage: HOUSE --build-iff FILE --floor-globals FILE --wall-globals FILE [--resource-file FILE | --resource-far FAR]...");
    std::string build,floors,walls;std::vector<odh::ArchitectureResourceSource> sources;
    for(int n=2;n<argc;++n){const std::string option=argv[n];
        if(n+1>=argc)throw odh::FormatError("missing architecture price option value");
        const std::string value=argv[++n];
        if(option=="--build-iff" && build.empty())build=value;
        else if(option=="--floor-globals" && floors.empty())floors=value;
        else if(option=="--wall-globals" && walls.empty())walls=value;
        else if(option=="--resource-file" || option=="--resource-far"){
            if(sources.size()>=256)throw odh::FormatError("architecture price source budget exceeded");
            sources.push_back({value,option=="--resource-file"?odh::ArchitectureResourceSourceKind::loose_file:odh::ArchitectureResourceSourceKind::far1_archive});
        }else throw odh::FormatError("unknown or duplicate architecture price option");
    }
    if(build.empty() || floors.empty() || walls.empty())throw odh::FormatError("all three explicit build/global files are required");
    const auto resources=odh_tools::load_property_architecture_prices(argv[1],build,floors,walls,sources);
    const auto& raw=resources.saved;const auto& prices=resources.prices;
    const auto loaded=resources.loaded;
    const auto& used_styles=resources.used_styles;const auto& used_floors=resources.used_floors;const auto& used_walls=resources.used_walls;
    const auto result=odh::inspect_property_architecture_value(raw.architecture,prices);
    std::cout<<"{\"evidence\":\"ODH_raw_geometry_resource_prices\",\"inspection_only\":true,\"refund_authorized\":false,\"original_runtime_verified\":false,\"status\":"
        <<json_bytes(odh::property_architecture_value_status_name(result.status))<<",\"reason\":"<<json_bytes(odh::property_architecture_value_reason_name(result.reason))
        <<",\"dynamic_price_resources_loaded\":"<<loaded<<",\"floor_prices\":"<<prices.floors.size()<<",\"wallpaper_prices\":"<<prices.wallpaper.size()
        <<",\"structural\":"<<result.structural<<",\"surfaces_and_pools\":"<<result.surfaces_and_pools<<",\"total\":"<<result.total
        <<",\"has_architecture\":"<<(result.has_architecture?"true":"false")
        <<",\"structural_profile\":\"comparison_stock_catalog_initialization\""
        <<",\"catalog_resources\":"<<resources.catalog_resources
        <<",\"floor_map_resolved\":"<<resources.floor_map_resolved
        <<",\"wall_map_resolved\":"<<resources.wall_map_resolved;
    const auto missing=[&](const char* key,const auto& used,const auto& available){
        std::cout<<",\""<<key<<"\":[";bool first=true;
        for(const auto id:used)if(!available.count(id)){if(!first)std::cout<<',';first=false;std::cout<<id;}
        std::cout<<']';
    };
    missing("missing_structural_ids",used_styles,prices.structural);
    missing("missing_floor_ids",used_floors,prices.floors);
    missing("missing_wallpaper_ids",used_walls,prices.wallpaper);
    std::cout<<"}\n";
    return result.status==odh::PropertyArchitectureValueStatus::resolved?0:3;
}catch(const std::exception& error){std::cerr<<"architecture price inspection error: "<<error.what()<<'\n';return 2;}}
