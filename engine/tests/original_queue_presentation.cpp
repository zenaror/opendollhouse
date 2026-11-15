/* SPDX-License-Identifier: MPL-2.0 */
#include "original_queue_view.hpp"
#include "resource.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void put(std::string& bytes,std::size_t offset,std::uint32_t value,unsigned count){
    for(unsigned i=0;i<count;++i)bytes.at(offset+i)=char(value>>(i*8));
}
std::string bitmap(unsigned width,unsigned height,std::array<unsigned char,3> color){
    const auto stride=(width*3+3)&~3U;
    std::string bytes(54+stride*height,'\0');
    put(bytes,0,0x4d42,2);put(bytes,2,bytes.size(),4);put(bytes,10,54,4);
    put(bytes,14,40,4);put(bytes,18,width,4);put(bytes,22,height,4);
    put(bytes,26,1,2);put(bytes,28,24,2);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
        for(unsigned c=0;c<3;++c)bytes[54+y*stride+x*3+c]=char(color[2-c]);
    return bytes;
}
std::string archive(const std::vector<std::pair<std::string,std::string>>& entries){
    std::string bytes="FAR!byAZ";bytes.resize(16,'\0');put(bytes,8,1,4);
    std::vector<std::uint32_t> offsets;
    for(const auto& item:entries){offsets.push_back(bytes.size());bytes+=item.second;}
    put(bytes,12,bytes.size(),4);
    const auto append=[&](std::uint32_t value){const auto pos=bytes.size();bytes.resize(pos+4);put(bytes,pos,value,4);};
    append(entries.size());
    for(std::size_t i=0;i<entries.size();++i){
        append(entries[i].second.size());append(entries[i].second.size());
        append(offsets[i]);append(entries[i].first.size());bytes+=entries[i].first;
    }
    return bytes;
}
struct TempRoot {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("odh-queue-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TempRoot(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
}
int main(){try{
    odh::InteractionQueueSnapshot queue{};queue.actor_id=12;queue.actor_generation=2;queue.run_generation=3;
    for(unsigned i=0;i<25;++i){odh::InteractionActionSnapshot action{};action.uid=i+1;
        action.mode=odh::InteractionQueueMode::normal;queue.actions.push_back(action);}
    for(const auto size:{std::pair<unsigned,unsigned>{800,600},{1024,768},{1280,720}}){
        const auto tiles=odh_host::original_queue_layout(queue,size.first,size.second);
        require(tiles.size()==25,"viewport must not discard pending logical actions");
        require(tiles[0].box.x==32&&tiles[0].box.y==32&&tiles[0].box.width==45&&tiles[0].box.height==45,
            "original 45px frame begins at 32,32");
        require(tiles[1].box.x==82&&tiles[19].box.x==982&&tiles[19].box.y==32,
            "original fixed 50px pitch does not wrap a long queue");
        require(odh_host::queue_tile_at(tiles,82,32)==&tiles[1]&&!odh_host::queue_tile_at(tiles,77,32),
            "click targets preserve the gap between actions");
        require(tiles[19].identity.uid==20&&tiles[19].identity.run_generation==3,
            "layout preserves cancellation identity");
    }
    auto strip=bitmap(105,41,{240,10,10});const unsigned stride=(105*3+3)&~3U;
    for(unsigned y=0;y<41;++y)for(unsigned x=0;x<35;++x){
        const auto pos=54+y*stride+x*3;strip[pos]=40;strip[pos+1]=80;strip[pos+2]=120;
    }
    std::istringstream stream(strip,std::ios::binary);
    const auto face=odh_host::read_queue_portrait(odh::View(stream,strip.size()));
    require(face.width==35&&face.height==41&&face.pixels.size()==1435,"select complete first portrait cell");
    require(std::all_of(face.pixels.begin(),face.pixels.end(),[](const auto& px){return px[0]==120&&px[1]==80&&px[2]==40;}),
        "other portrait cells must not leak into the queue");
    const auto invalid=bitmap(104,41,{120,80,40});std::istringstream bad(invalid,std::ios::binary);
    bool rejected=false;try{odh_host::read_queue_portrait(odh::View(bad,invalid.size()));}catch(const odh::FormatError&){rejected=true;}
    require(rejected,"unequal portrait cells are invalid");
    TempRoot root;std::filesystem::create_directories(root.path/"UIGraphics");
    auto frame_art=bitmap(90,45,{0,0,255});
    const auto paint=[](std::string& b,unsigned w,unsigned h,unsigned x,unsigned y,std::array<unsigned char,3> rgb){
        const auto pos=54+(h-1-y)*((w*3+3)&~3U)+x*3;
        for(unsigned c=0;c<3;++c)b[pos+c]=char(rgb[2-c]);
    };
    for(unsigned state=0;state<2;++state){
        paint(frame_art,90,45,state*45,0,{255,0,255});
        paint(frame_art,90,45,state*45+1,1,state?std::array<unsigned char,3>{255,255,0}:std::array<unsigned char,3>{128,128,128});
    }
    auto cancel_art=bitmap(45,45,{255,0,255});paint(cancel_art,45,45,22,22,{255,0,0});
    const auto far=archive({{"cpanel/Buttons/IconFrame.bmp",frame_art},
        {"Other/QueueCancel.bmp",cancel_art},
        {"CPanel/Catalog/ThumbTemplate.bmp",bitmap(180,45,{24,70,100})},
        {"CPanel/People/PeopleTemplate.bmp",bitmap(180,45,{0,0,80})}});
    {std::ofstream file(root.path/"UIGraphics/UIGraphics.far",std::ios::binary);file.write(far.data(),far.size());}
    odh_host::OriginalQueueView view(root.path);
    odh::RasterFrame raster{};raster.width=raster.height=45;raster.pixels.assign(45*45,{0,0,80,255});
    odh_host::QueueTile tile{};tile.box={0,0,45,45};view.draw_tile(raster,tile,&face,odh_host::QueueIconKind::authored_avatar);
    const std::array<std::uint8_t,4> expected{120,80,40,255};
    require(std::count(raster.pixels.begin(),raster.pixels.end(),expected)==1435,
        "35x41 portrait must retain one source pixel per screen pixel");
    require(raster.pixels[2*45+5]==expected&&raster.pixels[42*45+39]==expected&&raster.pixels[2*45+4]!=expected,
        "portrait placement must retain the original five/two pixel inset");
    const std::array<std::uint8_t,4> scene{0,0,80,255};
    require(raster.pixels[0]==scene&&raster.pixels[45+1]==std::array<std::uint8_t,4>{128,128,128,255}&&
        raster.pixels[20*45+2]==scene,"blue overlay key preserves people plate; magenta corners preserve scene");
    odh::BitmapImage object{74,37,24,true,std::vector<std::array<std::uint8_t,4>>(74*37,{240,10,10,255}),0};
    for(unsigned y=0;y<37;++y)for(unsigned x=0;x<37;++x)object.pixels[y*74+x]={20,210,30,255};
    raster.pixels.assign(45*45,scene);tile.active=true;
    view.draw_tile(raster,tile,&object);
    require(raster.pixels[20*45+2]==std::array<std::uint8_t,4>{24,70,100,255}&&
        raster.pixels[45+1]==std::array<std::uint8_t,4>{255,255,0,255}&&raster.pixels[0]==scene,
        "object template, active overlay and final corner transparency compose in original order");
    require(std::count(raster.pixels.begin(),raster.pixels.end(),std::array<std::uint8_t,4>{20,210,30,255})==37*37&&
        std::count(raster.pixels.begin(),raster.pixels.end(),std::array<std::uint8_t,4>{240,10,10,255})==0,
        "normal catalog half retains37px native size; highlighted half is excluded");
    tile.cancelled=true;view.draw_tile(raster,tile,&face,odh_host::QueueIconKind::authored_avatar);
    require(raster.pixels[22*45+22]==std::array<std::uint8_t,4>{255,0,0,255},"cancel overlay is above icon and frame");
    tile.cancelled=false;raster.pixels.assign(45*45,scene);
    odh::BitmapImage fallback{25,25,24,true,std::vector<std::array<std::uint8_t,4>>(25*25,{80,50,20,255}),0};
    view.draw_tile(raster,tile,&fallback,odh_host::QueueIconKind::avatar_thumbnail);
    require(std::count(raster.pixels.begin(),raster.pixels.end(),std::array<std::uint8_t,4>{80,50,20,255})==625&&
        raster.pixels[20*45+2]==scene,"independent2004 fallback retains existing fitting and person plate");
    raster.pixels.assign(45*45,scene);view.draw_tile(raster,tile,nullptr);
    require(raster.pixels[22*45+22]==std::array<std::uint8_t,4>{0,0,255,255},
        "unobserved no-BMP object presentation stays unchanged without fabricated image");
    tile.box={-4,-3,45,45};raster.pixels.assign(45*45,scene);
    view.draw_tile(raster,tile,&object);
    require(raster.pixels[0]==std::array<std::uint8_t,4>{24,70,100,255}&&raster.pixels[45]==std::array<std::uint8_t,4>{20,210,30,255},
        "viewport clipping preserves source phase of fully composed tile");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
