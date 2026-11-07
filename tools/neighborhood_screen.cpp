#include "neighborhood_screen.hpp"
#include "character_display_names.hpp"

#include "odh/family.hpp"
#include "odh/neighborhood.hpp"
#include "odh/character_catalog.hpp"
#include "odh/family_overlay.hpp"
#include "odh/neighborhood_picker.hpp"
#include "odh/resident_selection.hpp"
#include "odh/sha256.hpp"
#include "resource.hpp"
#include "original_ui_art.hpp"
#include "linux_cas_layout.hpp"
#include "screen_pointer.hpp"
#include "original_name_prompt.hpp"
#include "odh/ui_bitmap.hpp"
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
#include "linux_text_renderer.hpp"
#include "linux_vm_dialog.hpp"
#include "linux_ui_strings.hpp"
#endif
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
#include "linux_text_codec.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <thread>
#include <utility>

namespace odh_host {
namespace {
constexpr std::array<std::uint8_t, 4> kNeighborhoodMatte{32, 36, 44, 255};
constexpr std::array<unsigned, 6> kPickerModes{1, 2, 3, 4, 5, 7};
constexpr std::array<unsigned,4> travel_modes{2,3,5,7};
constexpr std::array<const char*,4> region_icons{
    "NghUI/Downtown.bmp","NghUI/Vacation.bmp","NghUI/Studiotown.bmp","NghUI/Magicland.bmp"};
constexpr odh::UiBitmapRect family_button{200,0,50,52},import_button{250,0,50,52},manage_button{300,0,50,52},
    rezone_button{350,0,50,52},exit_button{600,0,53,52},previous_neighborhood{106,0,26,52},next_neighborhood{141,0,26,52};
odh::UiBitmapRect region_button(std::size_t index){return {400+static_cast<int>(index)*50,0,50,52};}
std::optional<OriginalUiArt> screen_art(const std::filesystem::path& archive,const std::vector<std::string>& keys){
    if(archive.empty())return std::nullopt;
    // The standalone map inspector also accepts arbitrary archives; do not
    // silently substitute a different installation's artwork for that input.
    const auto file=std::filesystem::absolute(archive).lexically_normal();
    const auto root=file.parent_path().parent_path();
    if(file!=(root/"UIGraphics/UIGraphics.far"))return std::nullopt;
    return OriginalUiArt(root,{},keys);
}
void ui_image(odh::RasterFrame& frame,const OriginalUiArt* art,const char* key,odh::UiBitmapRect box,unsigned states=1,unsigned state=0){
    const auto* image=art?art->find(key):nullptr;
    if(!image)return;
    if(!states||state>=states||image->width%states)throw odh::FormatError("invalid original neighborhood button strip");
    const int span=static_cast<int>(image->width/states);
    odh::blit_color_keyed_bitmap(frame,*image,box,odh::UiBitmapRect{static_cast<int>(state)*span,0,span,
        static_cast<int>(image->height)},{255,0,255});
}
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
void ui_caption(odh::RasterFrame& frame,odh::UiBitmapRect box,const std::string& label,std::uint8_t language){
    TextOverlayStyle style;style.x=box.x;style.y=box.y;style.width=box.width;style.height=box.height;
    style.font_size_px=13;style.padding=1;style.centered=true;style.background={0,0,0,0};
    (void)draw_text_overlay(frame,label,language,style);
}
#endif
// Optional artwork is checked per control: a partial archive may contain the
// banner while missing an individual button. Always paint its real hit area.
void ui_button(odh::RasterFrame& frame,const OriginalUiArt* art,const char* key,odh::UiBitmapRect box,
        unsigned state,const std::string& caption,std::uint8_t language,bool always_caption=false){
    const bool installed=art&&art->find(key);
    if(installed)ui_image(frame,art,key,box,4,state);
    else{
        const std::array<std::uint8_t,4> fill=state==3?std::array<std::uint8_t,4>{48,48,63,255}:
            std::array<std::uint8_t,4>{20,48,96,255};
        for(int y=box.y;y<box.y+box.height;++y)for(int x=box.x;x<box.x+box.width;++x){
            const bool edge=x==box.x||x==box.x+box.width-1||y==box.y||y==box.y+box.height-1;
            frame.pixels[std::size_t(y)*frame.width+x]=edge?std::array<std::uint8_t,4>{135,150,167,255}:fill;
        }
    }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    if(!installed||always_caption)ui_caption(frame,box,caption,language);
#else
    (void)caption;(void)language;(void)always_caption;
#endif
}
// These hit areas follow the three empty button wells in Nbhd\PickBkg.bmp.
constexpr std::int32_t kFamilyPickerButtonTop = 530;
constexpr std::int32_t kFamilyPickerButtonBottom = 592;
constexpr std::int32_t kFamilyPickerArchiveLeft = 414;
constexpr std::int32_t kFamilyPickerMoveInLeft = 506;
constexpr std::int32_t kFamilyPickerButtonRight = 670;

bool neighborhood_mask_color(const std::array<std::uint8_t, 4>& pixel) {
    return (pixel[0] == 0xff && pixel[1] == 0x00 && pixel[2] == 0xff) ||
           (pixel[0] == 0xfe && pixel[1] == 0x02 && pixel[2] == 0xfe) ||
           (pixel[0] == 0xff && pixel[1] == 0x01 && pixel[2] == 0xff);
}

void apply_neighborhood_layers(odh::BitmapImage& base,
        const std::vector<odh::NeighborhoodMapLayer>& layers, const std::string& archive,
        std::vector<std::string>& missing_layers) {
    for (auto& pixel : base.pixels)
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
    return NeighborhoodHouseArt{location.house,box,open_box,{location.x-width/2,location.y-width/4,width,height/2},
        std::move(*closed),std::move(open)};
}
void draw_map_houses(odh::RasterFrame& frame,const PreparedNeighborhoodMap& map,std::optional<std::uint16_t> hovered){
    for(const auto& house:map.houses){
        const bool opened=hovered==house.house&&house.open.has_value();
        odh::blit_color_keyed_bitmap(frame,opened?*house.open:house.closed,opened?house.open_box:house.closed_box,
            std::nullopt,{255,0,255});
    }
}

// NBRS.Name identifies character files such as "user00001" and may be
// literally "iffname" for player creations. Display only the localized CTSS
// for a unique person definition, never the NBRS string or filename.
void draw_house_bubble(odh::RasterFrame& frame,const OriginalUiArt* art,const odh::LotLocation& spot,
        const NeighborhoodHoverText& text,const odh::FamilyEntry& family,
        const NeighborhoodPortraitProvider& portraits,const std::string& resident_names,std::uint8_t language){
    constexpr int width=200,height=166;
    const int x=std::clamp(spot.x+4,0,int(frame.width)-width);
    const int y=std::clamp(spot.y-height-6,0,int(frame.height)-height);
    const auto* base=art?art->find("Nbhd/HotSpotPopupBkg.bmp"):nullptr;
    const auto* alpha=art?art->find("Nbhd/HotSpotPopupBkgAlpha.bmp"):nullptr;
    const bool masked=base&&alpha&&base->width==width&&base->height==height&&alpha->width==width&&alpha->height==height;
    for(int dy=0;dy<height;++dy)for(int dx=0;dx<width;++dx){
        const auto at=static_cast<std::size_t>(dy*width+dx);
        const auto opacity=masked?alpha->pixels[at][0]:160;
        const auto color=masked?base->pixels[at]:std::array<std::uint8_t,4>{0,0,82,255};
        auto& pixel=frame.pixels[static_cast<std::size_t>(y+dy)*frame.width+x+dx];
        for(unsigned c=0;c<3;++c)pixel[c]=static_cast<std::uint8_t>((color[c]*opacity+pixel[c]*(255-opacity)+127)/255);
    }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    TextOverlayStyle style;style.x=x+14;style.y=y+4;style.width=width-22;style.height=28;style.font_size_px=14;
    style.padding=0;style.background={0,0,0,0};
    if(!text.address.empty())(void)draw_text_overlay(frame,text.address,language,style);
    style.y=y+40;style.height=68;style.font_size_px=13;style.wrap_lines=true;
    if(!text.family_summary.empty())(void)draw_text_overlay(frame,text.family_summary,language,style);
    if(!portraits&&!resident_names.empty()){
        style.y=y+110;style.height=50;
        (void)draw_text_overlay(frame,resident_names,language,style);
    }
#else
    (void)text;(void)resident_names;(void)language;
#endif
    if(portraits)for(std::size_t i=0;i<family.family.member_guids.size()&&i<8;++i){
        if(const auto* face=portraits(family.family.member_guids[i])){
            const odh::UiBitmapRect box{x+14+int(i%6)*25,y+110+int(i/6)*25,24,24};
            ui_image(frame,art,"CPanel/People/PeopleTemplate.bmp",box,4);
            odh::blit_color_keyed_bitmap(frame,*face,{box.x+1,box.y+1,22,22},std::nullopt,{255,0,255});
        }
    }
}

std::array<std::uint8_t, 32> neighborhood_identity(const odh::View& view) {
    odh::Sha256 hash;
    std::array<std::uint8_t, 64 * 1024> block{};
    for (std::uint64_t offset = 0; offset < view.size();) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            block.size(), view.size() - offset));
        view.read(offset, block.data(), count);
        hash.update(block.data(), count);
        offset += count;
    }
    return hash.finish();
}

std::string family_display_label(const odh::FamilyEntry& family) {
    if (!family.display_name.empty()) return family.display_name;
    return "Family " + std::to_string(family.family.family_number);
}
}

PreparedNeighborhoodMap prepare_neighborhood_map(
        const std::filesystem::path& user_data,
        const std::filesystem::path& ui_graphics, unsigned mode,
        const odh::FamilyOverlay* saved_overlay, std::uint8_t language) {
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
    with_iff_file((user_data/"LotLocations.iff").string(),[&](const auto& view){
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="STR#"||chunk.id>=region_locations.size())return;
            if(region_locations[chunk.id])throw odh::FormatError("ambiguous LotLocations STR# mode");
            region_locations[chunk.id]=true;
            if(chunk.id==mode)locations=odh::read_lot_locations(chunk.payload);
            else if(mode==1&&chunk.id==4&&region_available[4])expanded=odh::read_lot_locations(chunk.payload);
        });
    });
    if(mode==1&&expanded){locations=std::move(expanded);visual_mode=4;}
    if(!locations)throw odh::FormatError("LotLocations mode STR# not found");
    const auto config=odh::neighborhood_map_config(visual_mode);
    std::optional<odh::BitmapImage> image;
    with_far_entry(ui_graphics.string(), config->resource,
        [&](const auto& view) { image = odh::read_bmp(view); });
    if (!image) throw odh::FormatError("neighborhood map resource was not decoded");
    std::vector<std::string> missing_layers;
    apply_neighborhood_layers(*image, config->first_frame_layers, ui_graphics.string(),
                              missing_layers);
    for (const auto& resource : missing_layers)
        std::clog << "OpenDollhouse: neighborhood map layer is missing: " << resource << '\n';

    std::vector<odh::FamilyEntry> entries;
    std::vector<odh::FamilyEntry> source_families;
    std::map<std::uint16_t, std::string> family_names;
    std::map<std::uint32_t,bool> unique_neighbors;
    with_iff_file((user_data / "Neighborhood.iff").string(), [&](const auto& view) {
        const auto identity = saved_overlay ? neighborhood_identity(view) :
            std::array<std::uint8_t, 32>{};
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if(chunk.type=="NBRS"&&chunk.id==1){
                try{
                    auto nbrs=odh::read_nbrs(chunk.payload);
                    if(saved_overlay&&!saved_overlay->creations.empty())
                        nbrs=odh::apply_neighbor_overlay(nbrs,identity,*saved_overlay);
                    for(const auto& item:nbrs.entries)if(item.record&&item.record->guid){
                        const auto added=unique_neighbors.emplace(item.record->guid,true);
                        if(!added.second)added.first->second=false;
                    }
                }catch(const odh::FormatError& error){
                    unique_neighbors.clear();
                    std::clog<<"OpenDollhouse: neighborhood resident labels unavailable: "<<error.what()<<'\n';
                }
                return;
            }
            if (chunk.type == "FAMs") {
                if (family_names.count(chunk.id))
                    throw odh::FormatError("ambiguous FAMs family name chunk");
                const auto table = odh::read_strings(chunk.payload);
                if (const auto* name = odh::string_in_language_or_english(table, language, 0)) {
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
                    family_names.emplace(chunk.id, legacy_text_to_utf8(name->value,name->language?name->language:1));
#else
                    family_names.emplace(chunk.id, name->value);
#endif
                }
                return;
            }
            if (chunk.type != "FAMI") return;
            if (entries.size() >= 4096) throw odh::FormatError("too many FAMI chunks");
            entries.push_back({chunk.id, odh::read_fami(chunk.payload)});
        });
        for (auto& entry : entries) {
            const auto name = family_names.find(entry.chunk_id);
            if (name != family_names.end()) entry.display_name = name->second;
        }
        source_families = entries;
        if (saved_overlay) {
            entries = odh::apply_family_overlay(entries, identity, *saved_overlay);
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
            // Sidecar names retain game-encoded bytes; decode at this single
            // presentation boundary, after the original FAMs name fallback.
            for (auto& entry : entries) {
                const auto override_name = std::find_if(saved_overlay->names.begin(), saved_overlay->names.end(),
                    [&](const auto& name) { return name.family_chunk_id == entry.chunk_id; });
                const bool created = std::any_of(saved_overlay->creations.begin(), saved_overlay->creations.end(),
                    [&](const auto& creation) { return creation.family_chunk_id == entry.chunk_id; });
                if (override_name != saved_overlay->names.end())
                    entry.display_name = legacy_text_to_utf8(entry.display_name, override_name->language);
                else if (created)
                    entry.display_name = legacy_text_to_utf8(entry.display_name, language);
            }
#endif
        }
    });
    const odh::FamilyContext families(entries);
    std::vector<odh::NeighborhoodMarker> markers;
    for (const auto& loc : locations->lots) {
        std::ostringstream filename;
        filename << "House" << std::setw(2) << std::setfill('0') << loc.house << ".iff";
        markers.push_back({loc, std::filesystem::is_regular_file(user_data / "Houses" / filename.str()),
            families.by_house(static_cast<std::int16_t>(loc.house)) != nullptr});
    }
    std::stable_sort(markers.begin(), markers.end(), [](const auto& a, const auto& b) {
        return a.location.y < b.location.y;
    });

    for(const auto supported:kPickerModes)region_available[supported]=region_available[supported]&&region_locations[supported];
    region_available[1]=region_available[1]||region_available[4];
    std::vector<NeighborhoodHouseArt> houses;
    std::vector<odh::NeighborhoodMarker> fallback;
    std::size_t remaining_pixels=8*1024*1024;
    const auto houses_root=std::filesystem::canonical(user_data)/"Houses";
    for(auto& marker:markers){
        if(!marker.file_exists)continue;
        std::ostringstream filename;filename<<"House"<<std::setw(2)<<std::setfill('0')<<marker.location.house<<".iff";
        std::error_code error;
        const auto real=std::filesystem::canonical(houses_root/filename.str(),error);
        const auto relative=real.lexically_relative(houses_root);
        if(error||relative.empty()||relative.is_absolute()||*relative.begin()==".."){
            marker.file_exists=false; // Neither draw nor admit a lot outside the selected user's Houses.
            std::clog<<"OpenDollhouse: ignored external or inaccessible "<<filename.str()<<'\n';continue;
        }
        try{
            auto house=load_house_art(real,marker.location,config->marker_scale,remaining_pixels);
            if(house){houses.push_back(std::move(*house));continue;}
        }catch(const odh::FormatError& e){
            std::clog<<"OpenDollhouse: "<<filename.str()<<" thumbnail unavailable: "<<e.what()<<'\n';
        }
        fallback.push_back(marker); // An installed lot without usable artwork stays accessible.
    }
    auto frame=odh::draw_neighborhood_map(*image,fallback,config->marker_scale);
    PreparedNeighborhoodMap map{std::move(frame),std::move(markers),config->marker_scale,mode,
        std::move(source_families),std::move(entries),std::move(missing_layers),ui_graphics,region_available,language,std::move(houses),{},std::move(unique_neighbors),std::filesystem::canonical(user_data)/"Characters",visual_mode};
    if(!map.houses.empty()){
        map.background=map.frame.pixels;
        draw_map_houses(map.frame,map,std::nullopt);
    }
    return map;
}

std::optional<std::uint16_t> pick_map_lot(const PreparedNeighborhoodMap& map,std::int32_t x,std::int32_t y){
    if(map.houses.empty())return odh::pick_neighborhood_lot(map.markers,map.marker_scale,x,y);
    if(x<0||y<0||x>=800||y>=600)return std::nullopt;
    for(auto marker=map.markers.rbegin();marker!=map.markers.rend();++marker){
        if(!marker->file_exists)continue;
        const auto house=std::find_if(map.houses.begin(),map.houses.end(),[&](const auto& value){return value.house==marker->location.house;});
        if(house!=map.houses.end()){
            if(house->hit.contains(x,y))return house->house;
        }else if(std::abs(x-marker->location.x)<=static_cast<int>(6*map.marker_scale)&&
                 std::abs(y-marker->location.y)<=static_cast<int>(6*map.marker_scale))return marker->location.house;
    }
    return std::nullopt;
}

std::optional<std::uint8_t> show_language_picker(LinuxWindow& window,
        std::uint8_t initial_language) {
    static constexpr std::array<const char*,20> names{{
        "English (US)", "English (UK)", "Fran\u00e7ais", "Deutsch", "Italiano",
        "Espa\u00f1ol", "Nederlands", "Dansk", "Svenska", "Norsk", "Suomi",
        "\u05e2\u05d1\u05e8\u05d9\u05ea", "\u0420\u0443\u0441\u0441\u043a\u0438\u0439", "Portugu\u00eas",
        "\u65e5\u672c\u8a9e", "Polski", "\u7b80\u4f53\u4e2d\u6587", "\u7e41\u9ad4\u4e2d\u6587",
        "\u0e44\u0e17\u0e22", "\ud55c\uad6d\uc5b4"}};
#ifdef ODH_HAVE_LINUX_BITMAP_TEXT_FALLBACK
    static constexpr std::array<const char*,20> ascii_names{{
        "English (US)", "English (UK)", "Francais", "Deutsch", "Italiano",
        "Espanol", "Nederlands", "Dansk", "Svenska", "Norsk", "Suomi",
        "Hebrew", "Russian", "Portugues", "Japanese", "Polski",
        "Chinese Simplified", "Chinese Traditional", "Thai", "Korean"}};
    const auto& visible_names = ascii_names;
#else
    const auto& visible_names = names;
#endif
    if (initial_language < 1 || initial_language > visible_names.size())
        throw odh::FormatError("language picker requires a supported language code");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    std::size_t selected = static_cast<std::size_t>(initial_language - 1);
    constexpr std::size_t row_start = 56, row_height = 25;
    const auto render = [&]() {
        odh::RasterFrame frame{};
        frame.width = window.width(); frame.height = window.height();
        frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                            std::array<std::uint8_t,4>{35,39,48,255});
        frame.depth.assign(frame.pixels.size(), std::numeric_limits<float>::infinity());
        for (std::size_t index = 0; index < visible_names.size(); ++index) {
            const auto color = index == selected ? std::array<std::uint8_t,4>{47,93,138,255} :
                std::array<std::uint8_t,4>{52,57,68,255};
            const auto y0 = row_start + index * row_height;
            for (std::size_t y = y0; y < y0 + 22; ++y)
                for (std::size_t x = 24; x + 24 < frame.width; ++x)
                    frame.pixels[y * frame.width + x] = color;
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        TextOverlayStyle style;
        style.x = 24; style.width = static_cast<std::int32_t>(frame.width) - 48;
        style.height = 24; style.font_size_px = 16; style.padding = 4;
        style.foreground = {245,245,245,255}; style.background = {22,25,32,255};
        style.y = 18;
        draw_text_overlay(frame,
            "Choose game language - Tab: next, Shift+Tab: previous, Enter or click: confirm, Esc: cancel",
            static_cast<std::uint8_t>(selected + 1), style);
        for (std::size_t index = 0; index < visible_names.size(); ++index) {
            style.y = static_cast<std::int32_t>(row_start + index * row_height);
            draw_text_overlay(frame, std::string(index == selected ? "> " : "  ") +
                std::to_string(index + 1) + "  " + visible_names[index],
                static_cast<std::uint8_t>(selected + 1), style);
        }
        style.y = static_cast<std::int32_t>(frame.height) - 32;
        draw_text_overlay(frame, "The game text will use this language; you can also set --language later",
            static_cast<std::uint8_t>(selected + 1), style);
#endif
        window.set_title(std::string("OpenDollhouse | Language ") +
            std::to_string(selected + 1) + " - " + visible_names[selected] +
            " | Tab/Shift+Tab: change, Enter: confirm, Esc: cancel");
        window.present(frame);
    };
    render();
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if (input.interaction_selection_delta) {
            selected = odh::advance_resident_selection(selected,
                input.interaction_selection_delta, visible_names.size());
            render();
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        if (input.primary_click) {
            const auto y = (*input.primary_click)[1];
            const auto row_end = row_start + visible_names.size() * row_height;
            if (y >= static_cast<std::int32_t>(row_start) && y < static_cast<std::int32_t>(row_end)) {
                selected = static_cast<std::size_t>(y - static_cast<std::int32_t>(row_start)) / row_height;
                return static_cast<std::uint8_t>(selected + 1);
            }
        }
#endif
        if (input.action && !input.primary_click)
            return static_cast<std::uint8_t>(selected + 1);
        if (input.redraw) render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

odh::RasterFrame compose_neighborhood_picker_frame(
        const PreparedNeighborhoodMap& map,
        const std::string& localized_title,
        const std::string& localized_cancel,
        std::uint8_t language) {
    if (map.frame.width != 800 || map.frame.height != 600 || language < 1 || language > 20)
        throw odh::FormatError("neighborhood frame requires 800x600 pixels and a supported language");
    auto frame=map.frame;
    (void)localized_title;(void)localized_cancel;
    return frame;
}

std::optional<std::uint16_t> show_neighborhood_picker(
        LinuxWindow& window, const PreparedNeighborhoodMap& map,
        const std::string& localized_title,
        const std::string& localized_cancel,
        std::uint8_t language) {
    if (window.width() != 800 || window.height() != 600)
        throw odh::FormatError("neighborhood picker requires an 800x600 window");
    window.set_title("OpenDollhouse | " + localized_title);
    const auto frame = compose_neighborhood_picker_frame(map, localized_title,
        localized_cancel, language);
    window.present(frame);
    ScreenPointer activation;
    const auto hit=[&](int x,int y)->std::optional<std::size_t>{return pick_map_lot(map,x,y);};
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if (const auto click=activation.click(input,hit)) {
            const auto selected = pick_map_lot(map,(*click)[0],(*click)[1]);
            if (selected) return selected;
        }
        if (input.redraw) window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<NeighborhoodPickerResult> show_neighborhood_picker_with_create(
        LinuxWindow& window,const PreparedNeighborhoodMap& map,
        const std::string& localized_title,const std::string& localized_cancel,
        std::uint8_t language) {
    return show_neighborhood_picker_with_actions(window, map, localized_title,
        localized_cancel, language, true);
}

int neighborhood_arrow_at(std::int32_t x,std::int32_t y) noexcept {
    if(previous_neighborhood.contains(x,y))return -1;
    if(next_neighborhood.contains(x,y))return 1;
    return 0;
}
unsigned neighborhood_mode_button_at(std::int32_t x, std::int32_t y) noexcept {
    for(std::size_t i=0;i<travel_modes.size();++i)if(region_button(i).contains(x,y))return travel_modes[i];
    return 0;
}

std::optional<NeighborhoodPickerResult> show_neighborhood_picker_with_actions(
        LinuxWindow& window,const PreparedNeighborhoodMap& map,
        const std::string& localized_title,const std::string& localized_cancel,
        std::uint8_t language, bool allow_create_family,const std::string& create_label,
        NeighborhoodHoverTextProvider hover_text,const std::filesystem::path& private_characters,
        NeighborhoodPortraitProvider portraits,unsigned neighborhood_number,bool previous_available,bool next_available) {
    if(window.width()!=800||window.height()!=600)
        throw odh::FormatError("neighborhood picker requires an 800x600 window");
    if (!odh::neighborhood_mode_supported(map.mode))
        throw odh::FormatError("neighborhood picker received an unsupported mode");
    std::vector<std::string> keys(region_icons.begin(),region_icons.end());
    keys.insert(keys.end(),{"NghUI/banner_neighborhood.bmp","NghUI/banner_downtown.bmp","NghUI/Exit.bmp",
        "NghUI/MoveIn.bmp","NghUI/Bulldoze.bmp","NghUI/Rezone.bmp","NghUI/Import.bmp","NghUI/Return.bmp",
        "NghUI/Previous.bmp","NghUI/Next.bmp","NghUI/Current.bmp","NghUI/TheSimsLogo.bmp",
        "Nbhd/HotSpotPopupBkg.bmp","Nbhd/HotSpotPopupBkgAlpha.bmp","CPanel/People/PeopleTemplate.bmp"});
    const auto skin=screen_art(map.ui_graphics,keys);
    const auto* art=skin?&*skin:nullptr;
    (void)create_label;
    window.set_title("OpenDollhouse | "+localized_title);
    std::optional<std::array<std::int32_t,2>> pointer;
    std::map<std::uint16_t,NeighborhoodHoverText> hover_cache;
    const odh::FamilyContext households(map.effective_families);
    CharacterDisplayNames character_names(map.installed_characters,private_characters,language);
    const bool can_manage=std::any_of(map.markers.begin(),map.markers.end(),[](const auto& marker){return marker.file_exists&&marker.occupied;});
    NeighborhoodPortraitProvider known_portraits;
    if(portraits)known_portraits=[&](std::uint32_t guid)->const odh::BitmapImage*{
        const auto it=map.unique_neighbors.find(guid);
        return it!=map.unique_neighbors.end()&&it->second?portraits(guid):nullptr;
    };
    bool managing=false;
    ScreenPointer activation;
    const auto target_at=[&](int x,int y)->std::optional<std::size_t>{
        if(family_button.contains(x,y))return allow_create_family?std::optional<std::size_t>{8}:std::nullopt;
        if(exit_button.contains(x,y))return 9;
        if(manage_button.contains(x,y))return can_manage?std::optional<std::size_t>{10}:std::nullopt;
        if(previous_neighborhood.contains(x,y))return previous_available?std::optional<std::size_t>{11}:std::nullopt;
        if(next_neighborhood.contains(x,y))return next_available?std::optional<std::size_t>{12}:std::nullopt;
        if(import_button.contains(x,y)||rezone_button.contains(x,y))return {};
        if(const auto mode=neighborhood_mode_button_at(x,y))return map.region_available[mode]?std::optional<std::size_t>{mode}:std::nullopt;
        if(const auto house=pick_map_lot(map,x,y))return 100+*house;
        return {};
    };
    const auto render=[&]{
        auto frame=compose_neighborhood_picker_frame(map,localized_title,localized_cancel,language);
        const auto hovered_lot=pointer?pick_map_lot(map,(*pointer)[0],(*pointer)[1]):std::nullopt;
        if(hovered_lot&&!map.background.empty()){
            frame.pixels=map.background;
            draw_map_houses(frame,map,hovered_lot);
        }
        ui_image(frame,art,map.mode==1||map.mode==4?"NghUI/banner_neighborhood.bmp":"NghUI/banner_downtown.bmp",{0,0,800,52});
        ui_image(frame,art,"NghUI/TheSimsLogo.bmp",{5,0,70,52});
        if(hovered_lot&&pointer&&(*pointer)[1]>=52){
            const auto* family=households.by_house(static_cast<std::int16_t>(*hovered_lot));
            const auto marker=std::find_if(map.markers.begin(),map.markers.end(),[&](const auto& m){return m.location.house==*hovered_lot;});
            if(family&&marker!=map.markers.end()&&marker->occupied){
                auto entry=hover_cache.try_emplace(*hovered_lot);
                if(entry.second){
                    if(hover_text)try{entry.first->second=hover_text(*hovered_lot);}
                    catch(const odh::FormatError& error){
                        std::clog<<"OpenDollhouse: lot "<<*hovered_lot<<" hover text unavailable: "<<error.what()<<'\n';
                    }
                    if(entry.first->second.family_summary.empty())entry.first->second.family_summary=
                        family->display_name+"\n§ "+std::to_string(static_cast<std::int64_t>(family->family.budget)+family->family.value_in_arch);
                }
                std::string residents;
                if(!portraits)for(const auto guid:family->family.member_guids){
                    const auto id=map.unique_neighbors.find(guid);
                    if(id==map.unique_neighbors.end()||!id->second)continue;
                    const auto& name=character_names.name(guid);
                    if(name.empty())continue;
                    if(!residents.empty())residents+=", ";
                    residents+=name;
                }
                draw_house_bubble(frame,art,marker->location,entry.first->second,*family,known_portraits,residents,language);
            }
        }
        const auto button=[&](odh::UiBitmapRect box,const char* asset,const std::string& label,bool enabled,bool selected=false){
            const bool hover=pointer&&box.contains((*pointer)[0],(*pointer)[1]);
            // The original circular strips are 53x52 with a 50px advance.
            if(box.width==50&&art&&art->find(asset))box.width=53;
            const auto target=target_at(box.x+box.width/2,box.y+box.height/2);
            const bool pressed=target&&activation.pressed()==target;
            ui_button(frame,art,asset,box,!enabled?3U:selected||(pressed&&hover)?1U:hover?2U:0U,label,language);
        };
        button(previous_neighborhood,"NghUI/Previous.bmp","‹",previous_available);
        button(next_neighborhood,"NghUI/Next.bmp","›",next_available);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        ui_caption(frame,{126,6,20,24},std::to_string(neighborhood_number),language);
#endif
        button(family_button,"NghUI/MoveIn.bmp",language==14?"Famílias":"Families",allow_create_family);
        button(import_button,"NghUI/Import.bmp",language==14?"Importar":"Import",false);
        button(manage_button,"NghUI/Bulldoze.bmp",language==14?"Despejar":"Evict",can_manage,managing);
        button(rezone_button,"NghUI/Rezone.bmp",language==14?"Zona":"Zone",false);
        for(std::size_t i=0;i<travel_modes.size();++i){
            const auto mode=travel_modes[i];
            const std::array<const char*,4> pt{"Centro","Férias","Estúdio","Magia"},en{"Downtown","Vacation","Studio","Magic"};
            button(region_button(i),mode==map.mode?"NghUI/Return.bmp":region_icons[i],
                mode==map.mode?(language==14?"Voltar":"Back"):(language==14?pt[i]:en[i]),map.region_available[mode]);
        }
        button(exit_button,"NghUI/Exit.bmp",localized_cancel,true);
        window.present(frame);
    };
    render();
    for(;;) {
        const auto input=window.poll(allow_create_family);
        const auto hover_target=[&](const auto& at){
            if(!at)return std::pair<unsigned,std::uint16_t>{0,0};
            const int x=(*at)[0],y=(*at)[1];
            const unsigned control=family_button.contains(x,y)?8:exit_button.contains(x,y)?9:manage_button.contains(x,y)?10:
                previous_neighborhood.contains(x,y)?11:next_neighborhood.contains(x,y)?12:neighborhood_mode_button_at(x,y);
            return std::pair<unsigned,std::uint16_t>{control,control?0:pick_map_lot(map,x,y).value_or(0)};
        };
        const bool hover_changed=hover_target(pointer)!=hover_target(input.pointer_position);
        pointer=input.pointer_position;
        if(input.quit){
            if(managing&&input.quit_reason==WindowQuitReason::escape_key){
                managing=false;activation.reset();window.clear_escape_quit_reason();render();continue;
            }
            return std::nullopt;
        }
        if(input.move_out&&can_manage){activation.reset();managing=!managing;render();continue;}
        if(allow_create_family && input.create_family)
            return NeighborhoodPickerResult{NeighborhoodPickerAction::select_family,0,0};
        const auto previous_press=activation.pressed();
        const auto click=activation.click(input,target_at);
        if(click) {
            const auto x=(*click)[0],y=(*click)[1];
            if(exit_button.contains(x,y))return std::nullopt;
            if(const int delta=neighborhood_arrow_at(x,y)){
                if(delta<0?previous_available:next_available)
                    return NeighborhoodPickerResult{NeighborhoodPickerAction::switch_neighborhood,0,0,delta};
                continue;
            }
            if(import_button.contains(x,y)||rezone_button.contains(x,y)||(family_button.contains(x,y)&&!allow_create_family))continue;
            if(manage_button.contains(x,y)){if(can_manage){managing=!managing;render();}continue;}
            if(allow_create_family&&family_button.contains(x,y))
                return NeighborhoodPickerResult{NeighborhoodPickerAction::select_family,0,0};
            const auto selected_mode = neighborhood_mode_button_at(
                (*click)[0], (*click)[1]);
            if (selected_mode) {
                const auto target=selected_mode==map.mode?1U:selected_mode;
                if(map.region_available[target])return NeighborhoodPickerResult{NeighborhoodPickerAction::switch_mode,0,target};
                continue;
            }
            const auto selected=pick_map_lot(map,(*click)[0],(*click)[1]);
            if(selected){
                if(!managing)return NeighborhoodPickerResult{NeighborhoodPickerAction::lot,*selected,0};
                if(households.by_house(static_cast<std::int16_t>(*selected)))
                    return NeighborhoodPickerResult{NeighborhoodPickerAction::manage_lot,*selected,0};
            }
        }
        if(input.redraw||hover_changed||previous_press!=activation.pressed())render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

NeighborhoodLotPanelChoice show_neighborhood_lot_panel(LinuxWindow& window,
        const PreparedNeighborhoodMap& map, std::uint16_t house_number,
        const std::string& title, const std::string& description,
        const std::string& summary, const std::string& localized_accept,
        const std::string& localized_back, std::uint8_t language) {
    if (map.frame.width != 800 || map.frame.height != 600 ||
        !odh::neighborhood_mode_supported(map.mode) || house_number == 0 || house_number > 98 ||
        title.empty() || language < 1 || language > 20)
        throw odh::FormatError("lot details panel received invalid neighborhood data");
    const auto marker = std::find_if(map.markers.begin(), map.markers.end(),
        [&](const auto& value) { return value.location.house == house_number; });
    if (marker == map.markers.end())
        throw odh::FormatError("lot details panel house is absent from the selected map");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);

    auto frame = map.frame;
    constexpr std::int32_t panel_x = 20, panel_y = 92, panel_w = 420, panel_h = 438;
    const auto skin=screen_art(map.ui_graphics,{"Nbhd/HotSpotPopupBkg.bmp","Shared/Sys/WinBtn.bmp"});
    const auto* art=skin?&*skin:nullptr;
    if(art&&art->find("Nbhd/HotSpotPopupBkg.bmp"))
        ui_image(frame,art,"Nbhd/HotSpotPopupBkg.bmp",{panel_x,panel_y,panel_w,panel_h});
    else for(int y=panel_y;y<panel_y+panel_h;++y)
        std::fill_n(frame.pixels.begin()+y*800+panel_x,panel_w,std::array<std::uint8_t,4>{22,28,56,255});
    const odh::UiBitmapRect accept{panel_x+18,panel_y+panel_h-58,184,38},back{panel_x+210,panel_y+panel_h-58,panel_w-228,38};
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    TextOverlayStyle style;
    style.x = panel_x + 18; style.width = panel_w - 36; style.height = 34;
    style.font_size_px = 21; style.padding = 3;
    style.foreground = {248, 248, 248, 255}; style.background = {28, 33, 42, 0};
    style.y = panel_y + 20;
    draw_text_overlay(frame, title, language, style);
    style.font_size_px = 14; style.height = 29;
    style.foreground = {220, 225, 229, 255};
    std::size_t line = 0;
    std::size_t begin = 0;
    while (begin < description.size() && line < 4) {
        const auto end = description.find_first_of("\r\n", begin);
        const auto part = description.substr(begin, end == std::string::npos ? end : end - begin);
        if (!part.empty()) {
            style.y = panel_y + 66 + static_cast<std::int32_t>(line++) * 28;
            draw_text_overlay(frame, part, language, style);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
        if (description[end] == '\r' && begin < description.size() && description[begin] == '\n')
            ++begin;
    }
    line = 0;
    begin = 0;
    style.font_size_px = 15; style.height = 32;
    style.foreground = {245, 245, 245, 255};
    while (begin < summary.size() && line < 5) {
        const auto end = summary.find_first_of("\r\n", begin);
        const auto part = summary.substr(begin, end == std::string::npos ? end : end - begin);
        if (!part.empty()) {
            style.y = panel_y + 190 + static_cast<std::int32_t>(line++) * 34;
            draw_text_overlay(frame, part, language, style);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
        if (summary[end] == '\r' && begin < summary.size() && summary[begin] == '\n') ++begin;
    }
    ui_image(frame,art,"Shared/Sys/WinBtn.bmp",accept,4);
    ui_image(frame,art,"Shared/Sys/WinBtn.bmp",back,4);
    ui_caption(frame,accept,localized_accept,language);
    ui_caption(frame,back,localized_back,language);
#endif
    window.set_title("OpenDollhouse | "+title);
    window.present(frame);
    for (;;) {
        const auto input = window.poll();
        if (input.quit) {
            if (input.quit_reason == WindowQuitReason::escape_key)
                window.clear_escape_quit_reason();
            return NeighborhoodLotPanelChoice::back_to_map;
        }
        if (input.confirm) return NeighborhoodLotPanelChoice::continue_to_lot;
        if (input.primary_click) {
            const auto x = (*input.primary_click)[0], y = (*input.primary_click)[1];
            if(accept.contains(x,y))return NeighborhoodLotPanelChoice::continue_to_lot;
            if(back.contains(x,y))return NeighborhoodLotPanelChoice::back_to_map;
        }
        if (input.redraw) window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

OccupiedLotChoice show_occupied_lot_choice(LinuxWindow& window,
        const odh::FamilyEntry& family, std::uint16_t house_number,
        bool move_out_supported, std::uint8_t language,const PreparedNeighborhoodMap* map) {
    const auto current_house = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(family.family.house_number) & 0xffffU);
    if (house_number == 0 || house_number > 98 || current_house != house_number ||
        language < 1 || language > 20)
        throw odh::FormatError("occupied-lot choice received an invalid family");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    odh::RasterFrame frame{};
    frame.width = window.width(); frame.height = window.height();
    frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                        std::array<std::uint8_t,4>{35, 39, 48, 255});
    frame.depth.assign(frame.pixels.size(), std::numeric_limits<float>::infinity());
    if(map&&map->frame.width==800&&map->frame.height==600)frame=map->frame;
    const auto skin=screen_art(map?map->ui_graphics:std::filesystem::path{},{"Nbhd/HotSpotPopupBkg.bmp","Shared/Sys/WinBtn.bmp"});
    const auto* art=skin?&*skin:nullptr;
    const odh::UiBitmapRect enter{230,220,340,36},evict{230,276,340,36},rename{230,320,340,36},cancel{230,380,340,36};
    ui_image(frame,art,"Nbhd/HotSpotPopupBkg.bmp",{204,140,392,296});
    const auto button=[&](odh::UiBitmapRect box,const char* label,bool enabled){
        ui_image(frame,art,"Shared/Sys/WinBtn.bmp",box,4,enabled?0:3);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        ui_caption(frame,box,label,language);
#else
        (void)label;
#endif
    };
    const auto title=family_display_label(family);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    ui_caption(frame,{230,157,340,38},title,language);
#endif
    button(enter,language==14?"Entrar na casa":"Enter house",true);
    button(evict,language==14?"Despejar família":"Evict family",move_out_supported);
    button(rename,language==14?"Renomear família":"Rename family",true);
    button(cancel,language==14?"Cancelar":"Cancel",true);
    window.set_title("OpenDollhouse | "+title);
    window.present(frame);
    for (;;) {
        const auto input=window.poll();
        if(input.quit)return OccupiedLotChoice::cancel;
        if(input.text_input_utf8=="r"||input.text_input_utf8=="R")return OccupiedLotChoice::rename;
        if(input.move_out&&move_out_supported)return OccupiedLotChoice::move_out;
        if(input.primary_click){
            const auto x=(*input.primary_click)[0],y=(*input.primary_click)[1];
            if(enter.contains(x,y))return OccupiedLotChoice::enter;
            if(cancel.contains(x,y))return OccupiedLotChoice::cancel;
            if(rename.contains(x,y))return OccupiedLotChoice::rename;
            if(evict.contains(x,y)&&move_out_supported)return OccupiedLotChoice::move_out;
        }else if(input.action)return OccupiedLotChoice::enter;
        if (input.redraw) window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<MoveInFamilySelection> show_move_in_family_picker(
        LinuxWindow& window, const std::vector<odh::FamilyEntry>& families,
        std::uint16_t house_number, std::int32_t purchase_price, std::uint8_t language,
        const std::string& localized_action, const std::string& localized_title,
        const std::string& localized_no_family_title,
        const std::string& localized_no_family_message,
        const std::string& localized_cancel,
        const std::filesystem::path& ui_graphics,
        const std::string& localized_net_worth,
        const std::string& localized_family_friends,
        const std::string& localized_archive,
        const std::vector<std::uint16_t>& deletable_family_ids,NeighborhoodPortraitProvider portraits) {
    if (house_number > 98 || purchase_price < 0 || language < 1 || language > 20)
        throw odh::FormatError("family picker received an invalid move-in request");
    std::vector<const odh::FamilyEntry*> choices;
    for(const auto& entry:families){
        if((std::uint32_t(entry.family.house_number)&0xffffU)==0&&entry.family.family_number>=0&&
           !entry.family.member_guids.empty()&&entry.family.member_guids.size()<=8)choices.push_back(&entry);
    }
    const auto can_move=[&](const odh::FamilyEntry& entry){return !house_number||entry.family.budget>=purchase_price;};
    const auto can_archive=[&](const odh::FamilyEntry& entry){
        return std::find(deletable_family_ids.begin(),deletable_family_ids.end(),entry.chunk_id)!=deletable_family_ids.end();
    };
    const auto skin=screen_art(ui_graphics,{"Nbhd/PickBkg.bmp","Nbhd/FamilyPickItemBkg.bmp","Nbhd/PAFAddFamily.bmp",
        "Nbhd/PAFDeleteFamily.bmp","Nbhd/PAFMoveIn.bmp","Nbhd/NbhdTileBtn.bmp","CPanel/People/PeopleTemplate.bmp",
        "NghUI/Previous.bmp","NghUI/Next.bmp"});
    const auto* art=skin?&*skin:nullptr;
    const auto* background=art?art->find("Nbhd/PickBkg.bmp"):nullptr;
    const auto* rows=art?art->find("Nbhd/FamilyPickItemBkg.bmp"):nullptr;
    if(background&&(background->width!=800||background->height!=600))throw odh::FormatError("family picker dimensions changed");
    if(rows&&(rows->width!=690||rows->height!=204))throw odh::FormatError("family picker row dimensions changed");
    if(window.width()!=800||window.height()!=600)window.resize(800,600);
    constexpr std::size_t page_size=6;
    constexpr int row_x=41,row_y=76,row_height=68;
    const odh::UiBitmapRect cancel_box{130,529,140,62},add_box{322,529,92,62},archive_box{414,529,92,62},
        move_box{506,529,164,62},rename_box{32,485,168,24},previous_box{686,484,26,39},next_box{713,484,26,39};
    std::optional<std::size_t> selected;
    if(house_number&&!choices.empty())selected=0; // Explicit vacant-lot chooser keeps its existing default.
    std::size_t first=0;
    ScreenPointer activation;
    std::optional<std::array<std::int32_t,2>> pointer;
    const auto entry=[&](){return selected?choices[*selected]:nullptr;};
    const auto selection=[&](MoveInFamilySelection::Action action){
        const auto& e=*entry();return MoveInFamilySelection{e.chunk_id,odh::family_overlay_identity(e),action};
    };
    const auto target_at=[&](int x,int y)->std::optional<std::size_t>{
        if(cancel_box.contains(x,y))return 0;
        if(add_box.contains(x,y))return 1;
        if(archive_box.contains(x,y))return entry()&&can_archive(*entry())?std::optional<std::size_t>{2}:std::nullopt;
        if(move_box.contains(x,y))return entry()&&can_move(*entry())?std::optional<std::size_t>{3}:std::nullopt;
        if(house_number&&entry()&&rename_box.contains(x,y))return 4;
        if(previous_box.contains(x,y))return first?std::optional<std::size_t>{5}:std::nullopt;
        if(next_box.contains(x,y))return first+page_size<choices.size()?std::optional<std::size_t>{6}:std::nullopt;
        if(x>=row_x&&x<row_x+690&&y>=row_y&&y<row_y+int(page_size)*row_height){
            const auto i=first+std::size_t((y-row_y)/row_height);
            if(i<choices.size())return 100+i;
        }
        return {};
    };
    const auto state=[&](std::size_t id,bool enabled){
        const auto hovered=pointer?target_at((*pointer)[0],(*pointer)[1]):std::nullopt;
        return !enabled?3U:hovered==id&&activation.pressed()==id?1U:hovered==id?2U:0U;
    };
    const auto render=[&]{
        odh::RasterFrame frame{};frame.width=800;frame.height=600;
        if(background)frame.pixels=background->pixels;
        else frame.pixels.assign(800*600,{0,0,82,255});
        frame.depth.assign(frame.pixels.size(),std::numeric_limits<float>::infinity());
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        TextOverlayStyle text;text.background={0,0,0,0};text.padding=1;text.centered=true;
        text.x=0;text.y=4;text.width=800;text.height=35;text.font_size_px=21;
        (void)draw_text_overlay(frame,localized_title,language,text);
#endif
        for(std::size_t r=0;r<page_size;++r){
            const auto i=first+r;const int y=row_y+int(r)*row_height;
            const bool active=selected==i;
            const bool hovered=pointer&&target_at((*pointer)[0],(*pointer)[1])==100+i;
            if(rows)odh::blit_color_keyed_bitmap(frame,*rows,{row_x,y,690,68},
                odh::UiBitmapRect{0,active||(hovered&&activation.pressed()==100+i)?136:hovered?68:0,690,68},{255,0,255});
            if(i>=choices.size())continue;
            const auto& family=*choices[i];
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
            text.x=61;text.y=y;text.width=455;text.height=23;text.centered=false;text.font_size_px=16;
            (void)draw_text_overlay(frame,family_display_label(family),language,text);
            text.x=564;text.y=y+24;text.width=102;text.height=22;text.centered=true;text.font_size_px=13;
            const auto worth=static_cast<std::int64_t>(family.family.value_in_arch)+family.family.budget;
            auto amount=std::to_string(worth);
            const auto sign=worth<0?1U:0U;
            if(language==1||language==2||language==14){
                for(auto at=amount.size();at>sign+3;at-=3)amount.insert(at-3,1,language==14?'.':',');
            }
            (void)draw_text_overlay(frame,"§"+amount,language,text);
            text.x=648;text.width=50;
            (void)draw_text_overlay(frame,std::to_string(family.family.family_friends),language,text);
#endif
            for(std::size_t member=0;member<family.family.member_guids.size();++member){
                const odh::UiBitmapRect box{150+int(member)*48,y+20,45,45};
                ui_image(frame,art,"CPanel/People/PeopleTemplate.bmp",box,4);
                if(portraits)if(const auto* head=portraits(family.family.member_guids[member]))
                    odh::blit_color_keyed_bitmap(frame,*head,{box.x+2,box.y+2,41,41},std::nullopt,{255,0,255});
            }
        }
        ui_button(frame,art,"Nbhd/NbhdTileBtn.bmp",cancel_box,state(0,true),localized_cancel,language,true);
        ui_button(frame,art,"Nbhd/PAFAddFamily.bmp",add_box,state(1,true),language==14?"Criar família":"Create family",language);
        ui_button(frame,art,"Nbhd/PAFDeleteFamily.bmp",archive_box,state(2,entry()&&can_archive(*entry())),localized_archive,language);
        ui_button(frame,art,"Nbhd/PAFMoveIn.bmp",move_box,state(3,entry()&&can_move(*entry())),localized_action,language);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        if(house_number&&entry())ui_caption(frame,rename_box,language==14?"Renomear família":"Rename family",language);
        if(house_number&&!std::any_of(choices.begin(),choices.end(),[&](auto e){return can_move(*e);})){
            text.x=60;text.y=485;text.width=600;text.height=28;text.font_size_px=13;text.centered=true;
            (void)draw_text_overlay(frame,choices.empty()?localized_no_family_title:localized_no_family_message,language,text);
        }
#endif
        if(choices.size()>page_size){
            ui_button(frame,art,"NghUI/Previous.bmp",previous_box,state(5,first!=0),"‹",language);
            ui_button(frame,art,"NghUI/Next.bmp",next_box,state(6,first+page_size<choices.size()),"›",language);
        }
        window.set_title("OpenDollhouse | "+localized_title);window.present(frame);
    };
    (void)localized_net_worth;(void)localized_family_friends; // Original header icons are part of PickBkg.
    render();
    for(;;){
        const auto input=window.poll(true);
        if(input.quit){if(input.quit_reason==WindowQuitReason::escape_key)window.clear_escape_quit_reason();return {};}
        if(input.create_family)return MoveInFamilySelection{0,{},MoveInFamilySelection::Action::create_family};
        if(house_number&&entry()&&(input.text_input_utf8=="r"||input.text_input_utf8=="R"))
            return selection(MoveInFamilySelection::Action::rename);
        if(input.interaction_selection_delta&&!choices.empty()){
            activation.reset();
            selected=selected?odh::advance_resident_selection(*selected,input.interaction_selection_delta,choices.size()):
                input.interaction_selection_delta>0?0:choices.size()-1;
            first=*selected/page_size*page_size;render();
        }
        const auto previous_pointer=pointer;
        const auto previous_press=activation.pressed();
        pointer=input.pointer_position;
        const auto click=activation.click(input,target_at);
        if(click){
            const auto x=(*click)[0],y=(*click)[1];
            if(cancel_box.contains(x,y))return {};
            if(add_box.contains(x,y))return MoveInFamilySelection{0,{},MoveInFamilySelection::Action::create_family};
            if(entry()&&archive_box.contains(x,y)&&can_archive(*entry()))return selection(MoveInFamilySelection::Action::archive);
            if(entry()&&move_box.contains(x,y)&&can_move(*entry()))return selection(MoveInFamilySelection::Action::move_in);
            if(house_number&&entry()&&rename_box.contains(x,y))return selection(MoveInFamilySelection::Action::rename);
            if(previous_box.contains(x,y)&&first){first-=page_size;selected=first;render();}
            else if(next_box.contains(x,y)&&first+page_size<choices.size()){first+=page_size;selected=first;render();}
            else if(x>=row_x&&x<row_x+690&&y>=row_y&&y<row_y+int(page_size)*row_height){
                const auto i=first+std::size_t((y-row_y)/row_height);
                selected=i<choices.size()?std::optional<std::size_t>{i}:std::nullopt;render();
            }
        }else if(input.action&&!input.primary_click&&input.pointer_events.empty()&&entry()&&can_move(*entry()))return selection(MoveInFamilySelection::Action::move_in);
        if(input.redraw||previous_pointer!=pointer||previous_press!=activation.pressed())render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<std::string> show_family_name_editor(LinuxWindow& window,
        const std::string& initial_name_utf8, std::uint8_t language,
        const std::string& localized_title, const std::string& localized_accept,
        const std::string& localized_cancel,const std::filesystem::path& ui_graphics,
        const std::string& localized_prompt) {
    if(language<1||language>20)throw odh::FormatError("invalid family editor language");
    if(window.width()!=800||window.height()!=600)window.resize(800,600);
    odh::RasterFrame backdrop;backdrop.width=800;backdrop.height=600;
    backdrop.pixels=window.readback();
    backdrop.depth.assign(backdrop.pixels.size(),std::numeric_limits<float>::infinity());
    const auto skin=screen_art(ui_graphics,{"Shared/Sys/WinBtn.bmp",
        "CPanel/Backgrounds/PopupInfoTiles.bmp","Nbhd/DsgnFamBkg.bmp"});
    const auto layout=original_name_prompt_layout(800,600);
    // The launcher already owns the translated text catalog. Reuse its label;
    // opening this modal must not reread the whole UIText archive.
    const std::string prompt=localized_prompt.empty()?
        (language==14?"Digite o sobrenome:":"Enter Last Name:"):localized_prompt;
    std::string name=initial_name_utf8,feedback;
    bool replace_on_input=true;
    std::size_t focus=0; // Field, accept and cancel share keyboard/mouse focus.
    ScreenPointer activation;
    std::optional<std::array<std::int32_t,2>> pointer;
    const auto render=[&]{
        auto frame=backdrop;
        draw_original_name_prompt(frame,layout,skin?&*skin:nullptr,localized_title,
            feedback.empty()?prompt:feedback,name,localized_accept,localized_cancel,language,
            focus==0,replace_on_input,pointer,activation.pressed(),focus?std::optional<std::size_t>{focus-1}:std::nullopt);
        window.set_title("OpenDollhouse | "+localized_title);
        window.present(frame);
    };
    const auto invalid_name=[&]{return language==14?
        "Use um nome mais curto, com caracteres disponíveis no idioma escolhido.":
        "Use a shorter name with characters supported by the selected language.";};
    render();
    for(;;){
        const auto input=window.poll();
        if(input.quit){
            if(input.quit_reason==WindowQuitReason::escape_key)window.clear_escape_quit_reason();
            return std::nullopt;
        }
        bool repaint=input.redraw;
        if(input.interaction_selection_delta){
            focus=std::size_t((int(focus)+input.interaction_selection_delta%3+3)%3);
            activation.reset();repaint=true;
        }
        if(focus==0&&!input.text_input_utf8.empty()){
            if(replace_on_input)name.clear();
            replace_on_input=false;
            if(name.size()+input.text_input_utf8.size()<=1024){name+=input.text_input_utf8;feedback.clear();}
            else feedback=invalid_name();
            repaint=true;
        }
        if(focus==0&&input.text_backspace){
            if(replace_on_input)name.clear();
            else if(!name.empty()){
                auto at=name.size()-1;
                while(at&&(static_cast<unsigned char>(name[at])&0xc0U)==0x80U)--at;
                name.erase(at);
            }
            replace_on_input=false;feedback.clear();repaint=true;
        }
        const auto previous_pointer=pointer;
        const auto previous_press=activation.pressed();
        pointer=input.pointer_position;
        const auto click=activation.click(input,[&](int x,int y){return original_name_prompt_target(layout,x,y);});
        std::optional<std::size_t> choice;
        if(click){
            choice=original_name_prompt_target(layout,(*click)[0],(*click)[1]);
            if(choice){focus=*choice;if(focus==0){replace_on_input=false;choice.reset();}repaint=true;}
        }
        if(input.confirm)choice=focus==2?2:1;
        if(choice==2)return std::nullopt;
        if(choice==1){
            const auto first=name.find_first_not_of(" "),last=name.find_last_not_of(" ");
            if(first==std::string::npos){feedback=prompt;focus=0;repaint=true;}
            else{
                const auto trimmed=name.substr(first,last-first+1);
                feedback.clear();
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
                try{
                    const auto encoded=utf8_to_legacy_text(trimmed,language,1024);
                    if(encoded.size()>256)throw std::length_error("family name exceeds byte limit");
                    if(legacy_text_to_utf8(encoded,language)!=trimmed)
                        throw std::invalid_argument("family name cannot roundtrip exactly");
                }catch(const std::exception&){feedback=invalid_name();focus=0;repaint=true;}
                if(feedback.empty())return trimmed;
#else
                if(trimmed.size()<=256)return trimmed;
                feedback=invalid_name();focus=0;repaint=true;
#endif
            }
        }
        if(repaint||pointer!=previous_pointer||activation.pressed()!=previous_press)render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

namespace {
// One presentation and hit-test path for all management confirmations. The
// caller owns the transaction and revalidates it after this returns intent.
bool show_management_confirmation(LinuxWindow& window, std::uint8_t language,
        const std::string& title, const std::string& question,
        const std::string& yes, const std::string& no,
        const std::filesystem::path& ui_graphics) {
    odh::RasterFrame backdrop{};
    backdrop.width=window.width();backdrop.height=window.height();
    backdrop.pixels=window.readback();
    backdrop.depth.assign(backdrop.pixels.size(),std::numeric_limits<float>::infinity());
    auto art=screen_art(ui_graphics,{"Shared/Sys/WinBtn.bmp","CPanel/Backgrounds/PopupInfoTiles.bmp"});
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    odh::DialogState state;
    state.instruction.operands[5]=1; // Yes/No; no VM ticket or mutation.
    state.prompt.title=title;state.prompt.message=question;
    state.prompt.yes=yes;state.prompt.no=no;
    VmDialogAppearance appearance;
    appearance.button_strip=art?art->find("Shared/Sys/WinBtn.bmp"):nullptr;
    appearance.panel_tiles=art?art->find("CPanel/Backgrounds/PopupInfoTiles.bmp"):nullptr;
    const auto layout=vm_message_dialog_layout(backdrop.width,backdrop.height,state,language,appearance);
    if(!layout)throw odh::FormatError("management confirmation window is too small");
    const auto hit=[&](int x,int y){return vm_dialog_button_hit(*layout,1,x,y);};
    std::size_t first_line=0;
    TextOverlayResult page;
    const auto render=[&]{auto frame=backdrop;page=draw_vm_dialog(frame,state,language,first_line,appearance);window.present(frame);};
#else
    (void)language;(void)title;(void)question;(void)yes;(void)no;(void)art;
    // Without a text backend retain explicit buttons, never a whole-window
    // affirmative click. This diagnostic fallback is not original-UI proof.
    const int x=static_cast<int>(backdrop.width)/2,y=static_cast<int>(backdrop.height)/2;
    const odh::UiBitmapRect yes_box{x-114,y+25,104,33},no_box{x+10,y+25,104,33};
    const auto hit=[&](int px,int py)->std::optional<std::uint8_t>{
        if(yes_box.contains(px,py))return 0;
        if(no_box.contains(px,py))return 1;
        return {};
    };
    const auto render=[&]{auto frame=backdrop;
        for(const auto box:{yes_box,no_box})for(int py=box.y;py<box.y+box.height;++py)
            for(int px=box.x;px<box.x+box.width;++px)
                if(px>=0&&py>=0&&px<int(frame.width)&&py<int(frame.height))
                    frame.pixels[std::size_t(py)*frame.width+std::size_t(px)]={35,39,48,255};
        window.present(frame);};
#endif
    window.set_title("OpenDollhouse | "+title);
    render();
    CasPointer pointer;
    for(;;){
        const auto input=window.poll();
        if(input.quit){
            if(input.quit_reason==WindowQuitReason::escape_key){
                window.clear_escape_quit_reason();window.present(backdrop);
            }
            return false;
        }
        if(input.confirm||(input.action&&!input.primary_click)){window.present(backdrop);return true;}
        if(const auto click=pointer.click_on(input,hit)){
            if(const auto response=hit((*click)[0],(*click)[1])){
                window.present(backdrop);return *response==0;
            }
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        if(input.dialog_scroll_delta){
            const auto step=std::max<std::size_t>(1,page.visible_lines);
            const auto maximum=page.line_count>page.visible_lines?page.line_count-page.visible_lines:0;
            first_line=input.dialog_scroll_delta>0?std::min(maximum,first_line+step):first_line>step?first_line-step:0;
            render();
        }
#endif
        if(input.redraw)render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
}

bool show_family_archive_confirmation(LinuxWindow& window, const odh::FamilyEntry& family,
        std::uint8_t language, const std::string& localized_title,
        const std::string& localized_question, const std::string& localized_yes,
        const std::string& localized_no, const std::filesystem::path& ui_graphics) {
    if(language<1||language>20||
        (static_cast<std::uint32_t>(family.family.house_number)&0xffffU)!=0||
        family.family.member_guids.empty())
        throw odh::FormatError("family deletion confirmation received invalid family data");
    return show_management_confirmation(window,language,localized_title,localized_question,
        localized_yes,localized_no,ui_graphics);
}

bool show_move_in_confirmation(LinuxWindow& window, const odh::FamilyEntry& family,
        std::uint16_t house_number, std::int32_t purchase_price, std::uint8_t language,
        const std::string& localized_title, const std::string& localized_question,
        const std::string& localized_yes, const std::string& localized_no,
        const std::filesystem::path& ui_graphics) {
    if(house_number==0||house_number>98||purchase_price<0||language<1||language>20||
        family.family.budget<purchase_price)
        throw odh::FormatError("move-in confirmation received an invalid purchase");
    return show_management_confirmation(window,language,localized_title,localized_question,
        localized_yes,localized_no,ui_graphics);
}

bool show_move_out_confirmation(LinuxWindow& window, const odh::FamilyEntry& family,
        std::uint16_t house_number, std::uint8_t language,
        const std::string& localized_title, const std::string& localized_yes,
        const std::string& localized_no, const std::filesystem::path& ui_graphics,
        const std::string& localized_question) {
    if(house_number==0||house_number>98||language<1||language>20||
        family.family.value_in_arch<0||family.family.budget<0)
        throw odh::FormatError("move-out confirmation received invalid family data");
    const auto next_budget=static_cast<std::int64_t>(family.family.budget)+family.family.value_in_arch;
    if(next_budget>std::numeric_limits<std::int32_t>::max())
        throw odh::FormatError("move-out refund overflows the family budget");
    return show_management_confirmation(window,language,localized_title,localized_question,
        localized_yes,localized_no,ui_graphics);
}

std::optional<NeighborhoodMapRequest> show_neighborhood_resume_picker(
        LinuxWindow& window, const PreparedNeighborhoodMap& map, std::uint16_t active_house) {
    if (window.width() != 800 || window.height() != 600)
        throw odh::FormatError("neighborhood picker requires an 800x600 window");
    window.set_title("OpenDollhouse | "+std::string(map.language==14?"Vizinhança":"Neighborhood"));
    (void)active_house;
    auto frame=map.frame;
    if(frame.width!=800||frame.height!=600||frame.pixels.size()!=800*600)
        throw odh::FormatError("neighborhood resume map frame is invalid");
    std::optional<std::array<std::int32_t,2>> management_button;
    constexpr std::array<std::array<std::int32_t,2>,4> corners{{
        {24,548},{456,548},{24,24},{456,24}}};
    const auto radius=std::int64_t{6}*map.marker_scale;
    for(const auto& corner:corners){
        const auto x=std::int64_t{corner[0]},y=std::int64_t{corner[1]};
        const bool obscures_lot=std::any_of(map.markers.begin(),map.markers.end(),[&](const auto& marker){
            const auto mx=std::int64_t{marker.location.x},my=std::int64_t{marker.location.y};
            return mx+radius>=x&&mx-radius<x+320&&my+radius>=y&&my-radius<y+40;
        });
        const bool obscures_house=std::any_of(map.houses.begin(),map.houses.end(),[&](const auto& house){
            const auto& hit=house.hit;
            return hit.x<x+320&&hit.x+hit.width>x&&hit.y<y+40&&hit.y+hit.height>y;
        });
        if(!obscures_lot&&!obscures_house){management_button=corner;break;}
    }
    if(!management_button)management_button=corners.front();
    if(management_button){
        const auto skin=screen_art(map.ui_graphics,{"Shared/Sys/WinBtn.bmp"});
        const odh::UiBitmapRect box{(*management_button)[0],(*management_button)[1],320,40};
        ui_image(frame,skin?&*skin:nullptr,"Shared/Sys/WinBtn.bmp",box,4);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        ui_caption(frame,box,map.language==14?"Famílias":"Families",map.language);
#endif
    }
    window.present(frame);
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if(input.move_out)
            return NeighborhoodMapRequest{NeighborhoodRequestKind::manage_families,0};
        if (input.primary_click) {
            if(management_button&&(*input.primary_click)[0]>=(*management_button)[0]&&
               (*input.primary_click)[0]<(*management_button)[0]+320&&
               (*input.primary_click)[1]>=(*management_button)[1]&&
               (*input.primary_click)[1]<(*management_button)[1]+40)
                return NeighborhoodMapRequest{NeighborhoodRequestKind::manage_families,0};
            const auto selected = pick_map_lot(map,(*input.primary_click)[0],(*input.primary_click)[1]);
            if (selected) return NeighborhoodMapRequest{NeighborhoodRequestKind::travel,*selected};
        }
        if (input.redraw) window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<std::size_t> show_resident_picker(
        LinuxWindow& window, const std::vector<odh::SessionEntryMember>& residents,
        std::uint8_t language) {
    if (residents.empty()) throw odh::FormatError("selected family has no residents");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    std::size_t selected = 0;
    constexpr std::size_t row_height = 36;
    const std::size_t page_size = std::max<std::size_t>(1, (window.height() - 184) / row_height);
    const auto render = [&]() {
        const std::size_t page_start = (selected / page_size) * page_size;
        odh::RasterFrame frame{};
        frame.width = window.width(); frame.height = window.height();
        frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                            std::array<std::uint8_t,4>{35, 39, 48, 255});
        frame.depth.assign(frame.pixels.size(), std::numeric_limits<float>::infinity());
        for (std::size_t row = 0; row < page_size; ++row) {
            const auto index = page_start + row;
            if (index >= residents.size()) break;
            const auto color = index == selected ? std::array<std::uint8_t,4>{47,93,138,255} :
                std::array<std::uint8_t,4>{52,57,68,255};
            const auto y0 = 92 + row * row_height;
            for (std::size_t y = y0; y < y0 + 32; ++y)
                for (std::size_t x = 24; x + 24 < frame.width; ++x)
                    frame.pixels[y * frame.width + x] = color;
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        TextOverlayStyle style;
        style.x = 24; style.width = static_cast<std::int32_t>(frame.width) - 48;
        style.height = 32; style.font_size_px = 17; style.padding = 7;
        style.foreground = {245,245,245,255}; style.background = {22,25,32,255};
        style.y = 28;
        draw_text_overlay(frame, "Choose a resident - Tab: next, click a row or Enter: continue, Esc: cancel", language, style);
        for (std::size_t row = 0; row < page_size; ++row) {
            const auto index = page_start + row;
            if (index >= residents.size()) break;
            const auto& member = residents[index];
            const auto text = std::string(index == selected ? "> Resident ID " : "  Resident ID ") +
                std::to_string(member.neighbor_id) + " - " + member.character_asset;
            style.y = static_cast<std::int32_t>(92 + row * row_height);
            style.background = {22,25,32,255};
            draw_text_overlay(frame, text, language, style);
        }
        style.y = static_cast<std::int32_t>(frame.height) - 62;
        style.background = {22,25,32,255};
        const auto page_count = (residents.size() + page_size - 1) / page_size;
        draw_text_overlay(frame, "Page " + std::to_string(page_start / page_size + 1) + "/" +
            std::to_string(page_count) + " - selection is by neighborhood ID and character file", language, style);
#else
        (void)language;
#endif
        const auto& member = residents[selected];
        window.set_title("OpenDollhouse | resident " + std::to_string(member.neighbor_id) +
            " | " + member.character_asset + " | Tab: next, Enter: continue, Esc: cancel");
        window.present(frame);
    };
    render();
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if (input.interaction_selection_delta) {
            selected = odh::advance_resident_selection(selected,
                input.interaction_selection_delta, residents.size());
            render();
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        if (input.primary_click) {
            const auto y = (*input.primary_click)[1];
            constexpr std::size_t row_start = 92;
            const std::size_t row_end = row_start + page_size * row_height;
            if (y >= static_cast<std::int32_t>(row_start) && y < static_cast<std::int32_t>(row_end)) {
                const auto row = static_cast<std::size_t>(y - static_cast<std::int32_t>(row_start)) / row_height;
                const auto page_start = (selected / page_size) * page_size;
                const auto clicked = odh::resident_selection_row(page_start, row, page_size, residents.size());
                if (clicked) return clicked;
            }
        }
#endif
        if (input.action && !input.primary_click) return selected;
        if (input.redraw) render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

} // namespace odh_host
