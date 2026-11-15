// SPDX-License-Identifier: MPL-2.0
#include "odh/host_filesystem.hpp"
// These resource/profile implementations were moved from neighborhood_screen.cpp.
#include "neighborhood_resources.hpp"
#include "resource.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
namespace odh_host {
namespace {
constexpr std::array<std::uint8_t,4> kNeighborhoodMatte{32,36,44,255};
constexpr std::array<unsigned,6> kPickerModes{1,2,3,4,5,7};
bool neighborhood_mask_color(const std::array<std::uint8_t, 4>& pixel) {
    return (pixel[0] == 0xff && pixel[1] == 0x00 && pixel[2] == 0xff) ||
           (pixel[0] == 0xfe && pixel[1] == 0x02 && pixel[2] == 0xfe) ||
           (pixel[0] == 0xff && pixel[1] == 0x01 && pixel[2] == 0xff);
}

void apply_neighborhood_layers(odh::BitmapImage& base,
        const std::vector<odh::NeighborhoodMapLayer>& layers, const std::string& archive,
        std::vector<std::string>& missing_layers, bool matte = true) {
    if (matte) for (auto& pixel : base.pixels)
        if (neighborhood_mask_color(pixel)) pixel = kNeighborhoodMatte;

    for (const auto& layer : layers) {
        std::optional<odh::BitmapImage> image;
        try {
            with_far_entry(archive, layer.resource, [&](const auto& view) {
                image = odh::read_bmp(view);
            });
        } catch (const FarEntryNotFound&) {
            // Decorative layers are optional in base-game-only installations.
            missing_layers.push_back(layer.resource);
            continue;
        }
        if (!image || image->pixels.size() !=
                static_cast<std::size_t>(image->width) * image->height)
            throw odh::FormatError("neighborhood layer has inconsistent pixel data");
        for (std::uint32_t y = 0; y < image->height; ++y) {
            const auto target_y = static_cast<std::int64_t>(layer.y) + y;
            if (target_y < 0 || target_y >= base.height) continue;
            for (std::uint32_t x = 0; x < image->width; ++x) {
                const auto target_x = static_cast<std::int64_t>(layer.x) + x;
                if (target_x < 0 || target_x >= base.width) continue;
                const auto& pixel = image->pixels[static_cast<std::size_t>(y) * image->width + x];
                if (!neighborhood_mask_color(pixel))
                    base.pixels[static_cast<std::size_t>(target_y) * base.width +
                                static_cast<std::size_t>(target_x)] = pixel;
            }
        }
    }
}

// Simitone's UINeighborhoodHouseButton uses the house's own roofed/open
// artwork and signed THMB offsets, including the extra offset on hover.
std::optional<NeighborhoodHouseArt> load_house_art(const std::filesystem::path& file,
        const odh::LotLocation& location,unsigned scale,std::size_t& remaining_pixels){
    std::size_t pixels_left=remaining_pixels;
    std::optional<odh::BitmapImage> closed,open;
    std::optional<std::array<int,5>> offsets;
    with_iff_file(file.string(),[&](const odh::View& view){
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type=="THMB"&&chunk.id==512){
                if(offsets||chunk.payload.size()!=20)throw odh::FormatError("ambiguous or malformed house thumbnail offsets");
                offsets.emplace();
                for(unsigned n=0;n<5;++n){
                    const auto value=chunk.payload.le32(n*4);
                    const auto signed_value=value<=INT32_MAX?static_cast<std::int64_t>(value):static_cast<std::int64_t>(value)-4294967296LL;
                    if(signed_value<-32768||signed_value>32767)throw odh::FormatError("house thumbnail offset out of bounds");
                    (*offsets)[n]=static_cast<int>(signed_value);
                }
            }
            if(chunk.type!="BMP_"||(chunk.id!=512&&chunk.id!=513))return;
            auto& image=chunk.id==513?closed:open;
            if(image)throw odh::FormatError("duplicate house thumbnail bitmap");
            odh::BitmapLimits limits;
            limits.pixels=static_cast<std::uint32_t>(std::min<std::size_t>(pixels_left,limits.pixels));
            image=odh::read_bmp(chunk.payload,limits,odh::BmpExtentPolicy::iff_container);
            pixels_left-=image->pixels.size();
            // FreeSO ImageLoader's original TS1 transparency range includes
            // 248,0,248 in the installed house thumbnails.
            for(auto& pixel:image->pixels)if(pixel[0]>=248&&pixel[2]>=248&&pixel[1]<=4)pixel={255,0,255,255};
        });
    });
    if(!closed||!offsets)return std::nullopt;
    const int divisor=static_cast<int>(scale),width=static_cast<int>(closed->width)/divisor,height=static_cast<int>(closed->height)/divisor;
    if(width<1||height<2)return std::nullopt;
    const auto& o=*offsets;
    odh::UiBitmapRect box{location.x+(o[3]-o[0])/divisor,location.y-(o[1]+o[2])/divisor,width,height};
    auto open_box=box;open_box.y=location.y-(o[1]+o[2]+o[4])/divisor;
    if(open){
        open_box.width=static_cast<int>(open->width)/divisor;open_box.height=static_cast<int>(open->height)/divisor;
        if(open_box.width<1||open_box.height<1){pixels_left+=open->pixels.size();open.reset();}
    }
    remaining_pixels=pixels_left;
    return prepare_house_presentation(location.house,location,box,open_box,std::move(*closed),std::move(open));
}
}
PreparedNeighborhoodMap prepare_neighborhood_map_resources(
    const std::filesystem::path& user_data,const std::filesystem::path& ui_graphics,
    unsigned mode,std::uint8_t language,const std::function<bool(std::uint16_t)>& occupied){
    return prepare_neighborhood_map_resources(user_data,ui_graphics,mode,language,occupied,
                                              {(user_data/"LotLocations.iff").string()});
}

PreparedNeighborhoodMap prepare_neighborhood_map_resources(
    const std::filesystem::path& user_data,const std::filesystem::path& ui_graphics,
    unsigned mode,std::uint8_t language,const std::function<bool(std::uint16_t)>& occupied,
    const std::vector<std::string>& location_files){
    return prepare_neighborhood_map_resources(user_data,ui_graphics,mode,language,occupied,location_files,{});
}
PreparedNeighborhoodMap prepare_neighborhood_map_resources(
    const std::filesystem::path& user_data,const std::filesystem::path& ui_graphics,
    unsigned mode,std::uint8_t language,const std::function<bool(std::uint16_t)>& occupied,
    const std::vector<std::string>& location_files,const NeighborhoodHouseSources& sources){
    if(!odh::neighborhood_mode_supported(mode)||language<1||language>20)
        throw odh::FormatError("unsupported neighborhood screen profile");
    std::array<bool,8> region_available{};
    with_iff_file(ui_graphics.string(),[&](const auto& view){
        const auto index=odh::far1_index(view);
        for(const auto supported:kPickerModes){
            const auto entry=odh::neighborhood_map_config(supported);
            region_available[supported]=entry&&std::any_of(index.begin(),index.end(),[&](const auto& e){return e.name==entry->resource;});
        }
    });
    if(!region_available[mode]&&!(mode==1&&region_available[4]))
        throw FarEntryNotFound("exact archive entry not found");
    unsigned visual_mode=mode;
    std::array<bool,8> region_locations{};
    std::optional<odh::LotLocations> locations,expanded;
    for(const auto& path : location_files){
        const auto higher_priority = region_locations;
        with_iff_file(path,[&](const auto& view){
            odh::iff_chunks(view,[&](const odh::Chunk& chunk){
                if(chunk.type!="STR#" || chunk.id>=region_locations.size() || higher_priority[chunk.id])return;
                if(region_locations[chunk.id])throw odh::FormatError("ambiguous LotLocations STR# mode");
                region_locations[chunk.id]=true;
                if(chunk.id==mode)locations=odh::read_lot_locations(chunk.payload);
                else if(mode==1&&chunk.id==4&&region_available[4])expanded=odh::read_lot_locations(chunk.payload);
            });
        });
        if(std::all_of(kPickerModes.begin(),kPickerModes.end(),[&](unsigned id){
                return !region_available[id] || region_locations[id];
            }))break;
    }
    if(mode==1&&expanded){locations=std::move(expanded);visual_mode=4;}
    if(!locations)throw odh::FormatError("LotLocations mode STR# not found");
    const auto config=odh::neighborhood_map_config(visual_mode);
    std::optional<odh::BitmapImage> image;
    with_far_entry(ui_graphics.string(), config->resource,
        [&](const auto& view) { image = odh::read_bmp(view); });
    if (!image) throw odh::FormatError("neighborhood map resource was not decoded");
    std::vector<std::string> missing_layers;
    // The pinned Simitone UINeighborhoodSelectionPanel adds its authored map
    // layers after all house buttons. Keep the background separate so hovering
    // a roof cannot paint lots over trees, water edges or the studio canopy.
    for (const auto& resource : missing_layers)
        std::clog << "OpenDollhouse: neighborhood map layer is missing: " << resource << '\n';

    const auto user_root=odh::hostfs::canonical(user_data);
    const auto houses_root=user_root/"Houses";
    std::vector<std::filesystem::path> roots{houses_root};
    roots.insert(roots.end(),sources.roots.begin(),sources.roots.end());
    std::map<std::uint16_t,std::filesystem::path> resolved;
    std::vector<odh::NeighborhoodMarker> markers;
    for (const auto& loc : locations->lots) {
        std::ostringstream filename;
        filename << "House" << std::setw(2) << std::setfill('0') << loc.house << ".iff";
        const auto requested=sources.resolve?sources.resolve(loc.house):houses_root/filename.str();
        std::error_code error;
        const auto real=odh::hostfs::canonical(requested,error);
        bool permitted=!error&&std::filesystem::is_regular_file(real,error)&&!error;
        if(permitted){
            permitted=std::any_of(roots.begin(),roots.end(),[&](const auto& root){
                if(!odh::hostfs::is_absolute(root))return false;
                const auto relative=real.lexically_relative(root.lexically_normal());
                return !relative.empty()&&!odh::hostfs::is_absolute(relative)&&*relative.begin()!="..";
            });
        }
        if(permitted)resolved.emplace(loc.house,real);
        markers.push_back({loc,permitted,occupied&&occupied(loc.house)});
    }
    std::stable_sort(markers.begin(), markers.end(), [](const auto& a, const auto& b) {
        return a.location.y < b.location.y;
    });

    for(const auto supported:kPickerModes)region_available[supported]=region_available[supported]&&region_locations[supported];
    region_available[1]=region_available[1]||region_available[4];
    std::vector<NeighborhoodHouseArt> houses;
    std::size_t remaining_pixels=8*1024*1024;
    for(auto& marker:markers){
        if(!marker.file_exists)continue;
        const auto& real=resolved.at(marker.location.house);
        try{
            auto house=load_house_art(real,marker.location,config->marker_scale,remaining_pixels);
            if(house){houses.push_back(std::move(*house));continue;}
        }catch(const odh::FormatError& e){
            std::clog<<"OpenDollhouse: "<<real.filename().string()<<" thumbnail unavailable: "<<e.what()<<'\n';
        }
    }
    PreparedNeighborhoodMap map{{},std::move(markers),config->marker_scale,mode,
        {},{},std::move(missing_layers),ui_graphics,region_available,language,std::move(houses),{},{},user_root/"Characters",visual_mode};
    auto static_layers=config->first_frame_layers;
    if(config->animation_frames.size()>1&&!static_layers.empty()){
        NeighborhoodMapAnimation animation;animation.x=static_layers.front().x;animation.y=static_layers.front().y;
        animation.pulsates=config->animation_pulsates;animation.frame_ms=config->animation_frame_ms;
        std::size_t remaining=4*1024*1024;
        for(const auto& resource:config->animation_frames){
            try{
                with_far_entry(ui_graphics.string(),resource,[&](const auto& view){
                    auto frame=odh::read_bmp(view);
                    if(frame.pixels.size()>remaining)throw odh::FormatError("neighborhood animation exceeds pixel budget");
                    remaining-=frame.pixels.size();
                    for(auto& pixel:frame.pixels)if(neighborhood_mask_color(pixel))pixel={255,0,255,255};
                    animation.frames.push_back(std::move(frame));
                });
            }catch(const FarEntryNotFound&){map.missing_layers.push_back(resource);animation.frames.clear();break;}
        }
        if(animation.frames.size()==config->animation_frames.size()){
            map.animations.push_back(std::move(animation));static_layers.erase(static_layers.begin());
        }
    }
    if (!static_layers.empty()) {
        odh::BitmapImage foreground{};foreground.width=image->width;foreground.height=image->height;
        foreground.bit_count=24;foreground.pixels.assign(image->pixels.size(),{255,0,255,255});
        apply_neighborhood_layers(foreground,static_layers,ui_graphics.string(),map.missing_layers,false);
        map.foreground.push_back(std::move(foreground));
    }
    prepare_neighborhood_presentation(map,*image);
    return map;
}
}
