#include "odh/neighborhood_picker.hpp"
#include "odh/family.hpp"
#include "linux_window.hpp"
#include "resource.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <thread>
#include <iostream>
#include <iomanip>
#include <sstream>
int main(int argc,char** argv) {
    try {
        std::optional<std::string> folder,archive;
        std::optional<unsigned> mode;
        for(int i=1;i<argc;i+=2) {
            if(i+1>=argc) throw odh::FormatError("missing argument value");
            const std::string key=argv[i],value=argv[i+1];
            if(key=="--user-data" && !folder) folder=value;
            else if(key=="--ui-graphics" && !archive) archive=value;
            else if(key=="--mode" && !mode) {
                unsigned number=0;auto result=std::from_chars(value.data(),value.data()+value.size(),number);
                if(value.empty() || result.ec!=std::errc{} || result.ptr!=value.data()+value.size())
                    throw odh::FormatError("invalid decimal mode");
                mode=number;
            } else throw odh::FormatError("unknown or duplicate option");
        }
        if(!folder || !archive || !mode) throw odh::FormatError(
            "Usage: odh-neighborhood-window --user-data DIR --ui-graphics UIGraphics.far --mode 1..7");
        const auto config=odh::neighborhood_map_config(*mode);
        if(!config) {
            std::cout<<"{\"evidence\":\"map_selection_only\",\"mode\":6,\"status\":\"unsupported\",\"selected_house\":null}\n";
            return 0;
        }
        std::optional<odh::BitmapImage> image;
        with_far_entry(*archive,config->resource,[&](const auto& view) { image=odh::read_bmp(view); });
        const std::filesystem::path root=*folder;
        std::optional<odh::LotLocations> locations;
        with_iff_file((root/"LotLocations.iff").string(),[&](const auto& view) {
            odh::iff_chunks(view,[&](const odh::Chunk& chunk) {
                if(chunk.type!="STR#" || chunk.id!=*mode) return;
                if(locations) throw odh::FormatError("ambiguous LotLocations STR# mode");
                locations=odh::read_lot_locations(chunk.payload);
            });
        });
        if(!locations) throw odh::FormatError("LotLocations mode STR# not found");
        std::vector<odh::FamilyEntry> entries;
        with_iff_file((root/"Neighborhood.iff").string(),[&](const auto& view) {
            odh::iff_chunks(view,[&](const odh::Chunk& chunk) {
                if(chunk.type!="FAMI") return;
                if(entries.size()>=4096) throw odh::FormatError("too many FAMI chunks");
                entries.push_back({chunk.id,odh::read_fami(chunk.payload)});
            });
        });
        const odh::FamilyContext families(entries);
        std::vector<odh::NeighborhoodMarker> markers;
        for(const auto& loc:locations->lots) {
            std::ostringstream filename;filename<<"House"<<std::setw(2)<<std::setfill('0')<<loc.house<<".iff";
            markers.push_back({loc,std::filesystem::is_regular_file(root/"Houses"/filename.str()),
                               families.by_house(static_cast<std::int16_t>(loc.house))!=nullptr});
        }
        std::stable_sort(markers.begin(),markers.end(),[](const auto& a,const auto& b) {
            return a.location.y<b.location.y;
        });
        const auto frame=odh::draw_neighborhood_map(*image,markers,config->marker_scale);
        odh_host::LinuxWindow window(800,600,
            "OpenDollhouse map picker | green: no FAMI | amber: FAMI | gray: missing | Esc: cancel");
        window.present(frame);
        std::optional<std::uint16_t> selected;
        for(;;) {
            const auto input=window.poll();
            if(input.quit) break;
            if(input.primary_click) {
                selected=odh::pick_neighborhood_lot(markers,config->marker_scale,
                    (*input.primary_click)[0],(*input.primary_click)[1]);
                if(selected) break;
            }
            if(input.redraw) window.present(frame);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        std::cout<<"{\"evidence\":\"map_selection_only\",\"mode\":"<<*mode
            <<",\"status\":\""<<(selected?"selected":"cancelled")<<"\",\"selected_house\":";
        if(selected) std::cout<<*selected;else std::cout<<"null";
        std::cout<<"}\n";
    } catch(const std::exception& e) { std::cerr<<"odh-neighborhood-window: "<<e.what()<<'\n';return 2; }
}
