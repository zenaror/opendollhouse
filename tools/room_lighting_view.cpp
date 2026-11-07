#include "room_lighting_view.hpp"
#include <algorithm>
#include <cmath>
namespace odh_host { namespace {
std::uint8_t byte(float value){return static_cast<std::uint8_t>(std::clamp(static_cast<int>(value),0,255));}
void unit(float value){if(!std::isfinite(value)||value<0||value>1)throw odh::FormatError("basic lighting setting outside [0,1]");}
std::uint32_t room_word(const odh::GeometricRoomMap& map,std::uint32_t x,std::uint32_t y,std::uint8_t level){
    if(level<1||level>2||x>=map.width||y>=map.height||map.levels[level-1].size()!=std::size_t(map.width)*map.height)
        throw odh::FormatError("basic lighting tile room unavailable");
    return map.levels[level-1][std::size_t(y)*map.width+x];
}
}
RoomLightingDaylight room_lighting_daylight(unsigned hour,unsigned minute,unsigned second,const RoomLightingViewSettings& settings){
    if(hour>=24||minute>=60||second>=60)throw odh::FormatError("basic lighting clock outside day");
    unit(settings.darkness);unit(settings.weather_strength);unit(settings.minimum_outside_multiplier);
    for(auto c:settings.minimum_color_multiplier)unit(c);
    // TimeOfDayConfig.ColorFromTime. MonoGame3.8.4 Color operations truncate at
    // every byte conversion, including the initial *1.25 and both Lerps.
    static constexpr std::array<std::array<std::uint8_t,3>,14> colors{{
        {62,87,152},{62,87,152},{68,93,138},{87,87,87},{217,109,50},
        {255,255,255},{255,255,255},{255,255,255},{255,255,255},{255,255,255},
        {217,109,50},{87,87,87},{68,93,138},{62,87,152}}};
    const double time=double(hour)/24.0+double(minute)/(24.0*60)+double(second)/(24.0*60*60);
    const auto scaled=time*13;const auto index=static_cast<std::size_t>(std::floor(scaled));
    const auto progress=static_cast<float>(scaled-std::floor(scaled));RoomLightingDaylight result;
    for(unsigned c=0;c<3;++c){
        const auto first=byte(255+(float(colors[index][c])-255)*settings.darkness);
        const auto next=byte(255+(float(colors[index+1][c])-255)*settings.darkness);
        const auto interpolated=byte(float(first)+(float(next)-float(first))*progress);
        const auto linear=byte(static_cast<float>(std::pow(float(interpolated)/255.0f,2.2f))*255.0f);
        const auto weather=byte(255+(float(settings.weather_color[c])-255)*settings.weather_strength);
        result.outside[c]=settings.weather_strength>0?
            byte((float(linear)/255.0f)*(float(weather)/255.0f)*255.0f):linear;
    }
    const float factor=1.0f-((int(result.outside[0])+int(result.outside[1])+int(result.outside[2]))-115)/(512.0f*2);
    for(unsigned c=0;c<3;++c)result.minimum[c]=std::max<std::uint8_t>(1,byte(
        (float(result.outside[c])/255.0f)*settings.minimum_outside_multiplier*factor*settings.minimum_color_multiplier[c]*255.0f));
    return result;
}
RoomLightingView make_room_lighting_view(const odh::RoomLightingSnapshot& snapshot,const odh::GeometricRoomMap& map,
    unsigned hour,unsigned minute,unsigned second,const RoomLightingViewSettings& settings){
    odh::validate_room_lighting_snapshot(snapshot,map);
    const auto daylight=room_lighting_daylight(hour,minute,second,settings);RoomLightingView result;
    if(map.rooms.empty()||map.rooms.size()>32768)throw odh::FormatError("basic lighting room count invalid");
    result.room_colors.reserve(map.rooms.size());
    for(std::size_t room=0;room<map.rooms.size();++room){
        const auto* light=room?odh::room_lighting_for_room(snapshot,map,static_cast<std::uint16_t>(room)):nullptr;
        if(room&&!light)throw odh::FormatError("basic lighting room has no processed source state");
        std::array<float,3> color;
        for(unsigned c=0;c<3;++c){
            const auto exterior=byte(float(daylight.outside[c])*(light?float(light->outside_light)/100.0f:0));
            const auto ambient=byte(255.0f*(light?float(light->ambient_light)/100.0f:0));
            color[c]=float(std::min(255,int(std::max(exterior,daylight.minimum[c]))+int(ambient)))/255.0f;
        }
        result.room_colors.push_back(color);
    }
    return result;
}
std::array<float,3> room_lighting_view_color(const RoomLightingView& view,std::uint16_t room){
    if(room==65535)return {1,1,1};
    // WallComponent orientation3 deliberately retains bit15 after a ushort
    // cast. These are known-unused slots in Blueprint's new Color[65536],
    // never geometric rooms populated by GenerateRoomLights. Preserve source
    // black, distinct from an actual room whose lighting has not processed.
    if(room>=32768&&room<=65532)return {0,0,0};
    if(room>=view.room_colors.size())throw odh::FormatError("basic lighting visual room unavailable");
    const auto color=view.room_colors[room];for(auto c:color)unit(c);return color;
}
std::array<float,3> room_lighting_gamma_multiplier(const std::array<float,3>& linear){
    auto result=linear;for(auto& c:result){unit(c);c=static_cast<float>(std::pow(double(c),1.0/2.2));}return result;
}
std::uint16_t room_lighting_tile_room(const odh::GeometricRoomMap& map,std::uint32_t x,std::uint32_t y,std::uint8_t level){
    return static_cast<std::uint16_t>(room_word(map,x,y,level));
}
std::uint16_t room_lighting_wall_room(const odh::GeometricRoomMap& map,std::uint32_t x,std::uint32_t y,std::uint8_t level,std::uint8_t orientation,std::uint8_t rotation){
    if(rotation>3||orientation>3)throw odh::FormatError("basic lighting wall orientation invalid");
    const auto word=room_word(map,x,y,level);
    if(orientation<2)return static_cast<std::uint16_t>(word);
    const auto shift=orientation==2?16-(((rotation+1)/2)%2)*16:(rotation/2)*16;
    return static_cast<std::uint16_t>((word>>shift)&(orientation==2?0x7fffU:0xffffU));
}
}
