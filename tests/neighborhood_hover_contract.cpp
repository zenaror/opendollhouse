#include "neighborhood_presentation.hpp"
#include "original_ui_art.hpp"
#include "linux_text_renderer.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
struct Painted { std::string text; odh_host::TextOverlayStyle style; };
std::vector<Painted> painted;
int body_height=110;
odh_host::TextOverlayExtent measure(std::string_view,std::uint8_t,int,int){return {300,body_height};}
odh_host::TextOverlayResult draw(odh::RasterFrame&,std::string_view text,std::uint8_t,odh_host::TextOverlayStyle style){
    painted.push_back({std::string(text),style});return {};
}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){
    using namespace odh_host;
    const auto previous=set_text_overlay_backend({measure,draw,nullptr});
    try {
        PreparedNeighborhoodMap map;map.frame.width=800;map.frame.height=600;
        map.frame.pixels.assign(800*600,{100,100,100,255});
        map.frame.depth.assign(800*600,std::numeric_limits<float>::infinity());
        map.markers={{{68,320,300},true,false}};
        odh::BitmapImage tile{};tile.width=tile.height=111;tile.bit_count=32;
        tile.pixels.assign(111*111,{0,0,80,128});
        OriginalUiArt art({{"Nbhd/HotspotPopupTiler.TGA",tile}});
        NeighborhoodMapPresentation view;view.pointer=std::array<std::int32_t,2>{320,300};
        unsigned reads=0;std::string budget="Remaining: 7797";
        const auto provider=[&](std::uint16_t house){++reads;require(house==68,"wrong vacant lot requested");
            return NeighborhoodHoverText{"68 Main Street","Vanilla House\nCost: 12203\n"+budget};};
        const auto render=[&]{painted.clear();return view.draw(map,&art,"Cancel",1,1,provider,{},{},{});};
        auto frame=render();
        require(reads==1,"vacant rollover must request its metadata without a resident family");
        require(painted.size()>=2&&painted[0].text=="68 Main Street"&&painted[1].text.find("12203")!=std::string::npos&&
            painted[1].text.find("7797")!=std::string::npos,"vacant address, cost and remaining funds must reach the painter");
        const auto body=painted[1].style;
        require(body.wrap_lines&&body.height>=body_height&&body.x>=0&&body.y>=52&&
            body.x+body.width<=800&&body.y+body.height<=600,"vacant cost explanation must fit its measured viewport");
        const auto pixel=frame.pixels[std::size_t(200)*800+400];
        require(pixel[0]>=49&&pixel[0]<=51&&pixel[2]>=89&&pixel[2]<=91,
            "original popup alpha must blend with the map rather than paint an opaque plate");
        render();require(reads==1,"unchanged hover must reuse metadata");
        budget="Remaining: 6797";view.hover_cache.clear();render();
        require(reads==2&&painted[1].text.find("6797")!=std::string::npos,"context invalidation must replace the affordability explanation");
        body_height=360;view.hover_cache.clear();render();
        require(painted[1].style.height>=body_height,"long localized price explanations must expand the vacant popup");
        map.markers[0].occupied=true;odh::FamilyEntry family{};family.family.house_number=68;
        family.display_name="Residents";map.effective_families.push_back(family);view.hover_cache.clear();body_height=110;render();
        require(painted[1].style.width==178&&painted[1].style.height==68,
            "occupied families must retain the existing portrait-bubble text viewport");
        map.markers[0].file_exists=false;view.hover_cache.clear();const auto before=reads;render();
        require(reads==before,"missing lot must not request rollover metadata");
        set_text_overlay_backend(previous);std::cout<<"vacant/occupied neighborhood hover contract passed\n";return 0;
    }catch(const std::exception& error){set_text_overlay_backend(previous);std::cerr<<error.what()<<'\n';return 1;}
}
