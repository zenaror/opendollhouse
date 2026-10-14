#include "odh/wall_junction.hpp"
#include <cfenv>
#include <cmath>
#include <iostream>

namespace {
int checks=0;
void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F fn){bool caught=false;try{fn();}catch(const odh::FormatError&){caught=true;}require(caught,"invalid junction input rejected");}
odh::SavedArchitecture grid(unsigned width=3){odh::SavedArchitecture a;a.width=a.height=width;for(auto& level:a.levels)level.walls.resize(std::size_t(width)*width);return a;}
const odh::WallJunction* find(const odh::WallJunctionResult& r,unsigned x,unsigned y,unsigned level=1){
    for(const auto& j:r.junctions)if(j.x==x&&j.y==y&&j.level==level)return &j;
    return nullptr;
}
void flags(const odh::WallJunctionResult& r,unsigned x,unsigned y,unsigned expected,unsigned level=1){
    const auto* j=find(r,x,y,level);require(j&&j->raw_flags==expected,"junction endpoint flags");
}
}
int main(){try{
    // Independent table directly expressed in source enum bit names.
    constexpr unsigned TL=1,TR=2,BR=4,BL=8,DL=16,DT=32,DR=64,DB=128;
    const unsigned mapping[]={
        TR,TL,TL|TR,BL,BL|TR,BL|TL,BL|TL|TR,BR,BR|TR,BR|TL,BR|TR|TL,
        BR|BL,BR|BL|TR,BR|BL|TL,TL|TR|BR|BL,
        DT,DT|BL,DT|BR,DT|BL|BR,DL,DL|TR,DL|BR,DL|BR|TR,DL|DT,DL|DT|BR,
        DB,DB|TR,DB|TL,DB|TL|TR,DB|DT,DB|DL,DB|DL|TR,DB|DL|DT,
        DR,DR|TL,DR|BL,DR|TL|BL,DR|DT,DR|DT|BL,DR|DL,DR|DL|DT,
        DR|DB,DR|DB|TL,DR|DB|DT,DR|DB|DL,DL|DT|DR|DB
    };
    for(unsigned raw=0;raw<256;++raw){int expected=-1;for(unsigned frame=0;frame<46;++frame)if(mapping[frame]==raw)expected=static_cast<int>(frame);
        const auto actual=odh::wall_junction_frame(static_cast<std::uint8_t>(raw));
        require(expected<0?!actual:actual&&*actual==expected,"complete256flag junction map domain");
        for(unsigned rotation=0;rotation<4;++rotation){unsigned rotated=0;
            for(unsigned bit=0;bit<8;++bit)if(raw&(1U<<bit))rotated|=1U<<((bit/4)*4+(bit+rotation)%4);
            const auto result=odh::rotate_wall_junction(static_cast<std::uint8_t>(raw),static_cast<std::uint8_t>(rotation));
            require(result==rotated,"independent per-bit circular nibble rotation");
            if(expected>=0)require(odh::wall_junction_frame(result).has_value(),"all46maps closed under fourrotations");
        }
    }
    rejects([]{odh::rotate_wall_junction(1,4);});
    auto a=grid();require(odh::standing_wall_junctions(a,0).junctions.empty(),"empty grids produce no junctions");
    a.levels[0].walls[4]={1,0,1,0,9,8,7,6};
    auto r=odh::standing_wall_junctions(a,0);require(r.junctions.size()==2&&r.unmapped==0,"singleTL cardinal endpoints");flags(r,1,1,BL);flags(r,1,2,TR);
    a.levels[0].walls[4].segments=2;a.levels[0].walls[4].top_right_style=1;
    r=odh::standing_wall_junctions(a,0);flags(r,1,1,BR);flags(r,2,1,TL);
    a.levels[0].walls[4].segments=3;a.levels[0].walls[3]={2,0,0,1,0,0,0,0};a.levels[0].walls[1]={1,0,1,0,0,0,0,0};
    r=odh::standing_wall_junctions(a,0);flags(r,1,1,TL|TR|BR|BL);
    for(unsigned rotation=0;rotation<4;++rotation){r=odh::standing_wall_junctions(a,static_cast<std::uint8_t>(rotation));
        for(const auto& j:r.junctions)require(j.rotated_flags==odh::rotate_wall_junction(j.raw_flags,static_cast<std::uint8_t>(rotation))&&
            j.frame==*odh::wall_junction_frame(j.rotated_flags),"generated orientation/frame identities");
    }
    for(unsigned style:{2U,12U,13U,14U}){a=grid();a.levels[0].walls[4]={3,0,static_cast<std::uint16_t>(style),static_cast<std::uint16_t>(style),0,0,0,0};
        require(odh::standing_wall_junctions(a,0).junctions.empty(),"fences do not produce cardinal caps");}
    for(unsigned style:{0U,1U,3U,22U,255U,65535U}){a=grid();a.levels[0].walls[4]={3,0,static_cast<std::uint16_t>(style),static_cast<std::uint16_t>(style),0,0,0,0};
        r=odh::standing_wall_junctions(a,0);require(r.junctions.size()==3,"DecodeWalls normalizes other cardinal styles to thick1");
        require(a.levels[0].walls[4].top_left_style==style,"working normalization preserves raw input");}
    a=grid();a.levels[0].walls[4]={12,0,1,1,0,0,0,0};require(odh::standing_wall_junctions(a,0).junctions.empty(),"reverse-only segments do not create endpoints");
    for(unsigned diagonal:{16U,32U})for(unsigned style:{0U,1U,2U,12U,22U,255U}){a=grid();a.levels[0].walls[4]={static_cast<std::uint8_t>(diagonal),0,1,static_cast<std::uint16_t>(style),0,0,0,0};
        r=odh::standing_wall_junctions(a,0);
        if(style==1||style==255){require(r.junctions.size()==2,"diagonal1/255 endpoint pairs");
            if(diagonal==16){flags(r,2,1,DL);flags(r,1,2,DR);}else{flags(r,1,1,DB);flags(r,2,2,DT);}}
        else require(r.junctions.empty(),"diagonal styles preserved without cardinal normalization");
    }
    a=grid();a.levels[0].walls[2]={2,0,0,1,0,0,0,0};r=odh::standing_wall_junctions(a,0);flags(r,2,0,BR);flags(r,0,1,TL);
    a.levels[0].walls[2].segments=16;r=odh::standing_wall_junctions(a,0);flags(r,0,1,DL);flags(r,2,1,DR);
    a.levels[0].walls[2].segments=32;r=odh::standing_wall_junctions(a,0);flags(r,2,0,DB);flags(r,0,2,DT);
    a=grid();a.levels[0].walls[8]={3,0,1,1,0,0,0,0};r=odh::standing_wall_junctions(a,0);require(r.junctions.size()==1,"finalcorner outsidearray endpoints discarded");flags(r,2,2,BR|BL);
    a=grid(1);a.levels[1].walls[0]={3,0,1,1,0,0,0,0};r=odh::standing_wall_junctions(a,0);flags(r,0,0,BR|BL,2);
    a=grid();a.levels[0].walls[1]={16,0,0,1,0,0,0,0};a.levels[0].walls[4]={2,0,0,1,0,0,0,0};
    r=odh::standing_wall_junctions(a,0);require(r.unmapped==1&&r.junctions.size()==2&&!find(r,1,1),"unsupported junction combination counted and skipped");
    for(unsigned rotation=0;rotation<4;++rotation)require(odh::standing_wall_junctions(a,static_cast<std::uint8_t>(rotation)).unmapped==1,"unmapped count rotation-independent");
    a=grid(64);for(auto& level:a.levels)for(auto& tile:level.walls)tile={3,0,1,1,0,0,0,0};
    r=odh::standing_wall_junctions(a,0);require(r.junctions.size()==8192&&r.unmapped==0,"maximum8192sites accepted bounded");
    for(std::size_t i=1;i<r.junctions.size();++i){const auto& prev=r.junctions[i-1];const auto& next=r.junctions[i];
        require(prev.level<next.level||(prev.level==next.level&&(prev.y<next.y||(prev.y==next.y&&prev.x<next.x))),"stable level/y/x order");}
    rejects([&]{odh::standing_wall_junctions(a,4);});a.width=65;rejects([&]{odh::standing_wall_junctions(a,0);});
    a=grid();a.height=2;rejects([&]{odh::standing_wall_junctions(a,0);});a=grid(0);rejects([&]{odh::standing_wall_junctions(a,0);});
    a=grid();a.levels[1].walls.pop_back();rejects([&]{odh::standing_wall_junctions(a,0);});
    for(unsigned bits:{17U,33U,48U,64U,255U}){a=grid();a.levels[1].walls[8].segments=static_cast<std::uint8_t>(bits);rejects([&]{odh::standing_wall_junctions(a,0);});}

    const int saved_round=std::fegetround();std::fesetround(FE_TONEAREST);
    for(unsigned zoom=1;zoom<=3;++zoom){const auto actual=odh::wall_junction_depth(static_cast<std::uint8_t>(zoom));
        require(actual.width==(32U<<(zoom-1))&&actual.height==(16U<<(zoom-1)),"junction depth dimensions");
        // Independent native binary32 reference, forced to memory each step.
        volatile float yi=263.f;const float step=-1.525f*static_cast<float>(1U<<(3-zoom));
        for(unsigned y=0;y<actual.height;++y){const float value=std::min(255.f,static_cast<float>(yi));
            const auto expected=static_cast<std::uint8_t>(std::nearbyint(value));
            for(unsigned x=0;x<actual.width;++x)require(actual.pixels[y*actual.width+x]==expected,"iterative volatilefloat IEEEnear oracle allpixels");
            yi=yi+step;
        }
        require(actual.pixels[0]==255&&actual.pixels.back()<255,"upperclamp and descending ramp");
        for(auto mode:{FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO}){std::fesetround(mode);
            require(odh::wall_junction_depth(static_cast<std::uint8_t>(zoom)).pixels==actual.pixels,"depth generation ignores host fenv");
            require(std::fegetround()==mode,"depth generation leaves caller fenv unchanged");}
        std::fesetround(FE_TONEAREST);
    }
    std::fesetround(saved_round);rejects([]{odh::wall_junction_depth(0);});rejects([]{odh::wall_junction_depth(4);});
    std::cout<<checks<<" wall junction checks passed (synthetic only)\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
