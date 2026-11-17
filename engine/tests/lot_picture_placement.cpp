// SPDX-License-Identifier: MPL-2.0
#include "odh/lot_picture_placement.hpp"
#include <iostream>
#include <stdexcept>

static void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    const auto original=odh::lot_picture_origins(200,300,{{131,100,-18,0,0}},1);
    need(original.base==std::array<int,2>{69,218}&&original.roofed==original.base,
         "retained DVD House7 centered packet");
    for(int scale:{1,2})for(int dx:{-7,7})for(int dy:{-11,11}){
        const std::array<int,5> metadata{{131,100,-18,dx,dy}};
        const auto origin=odh::lot_picture_origins(200,300,metadata,scale);
        need(origin.base==std::array<int,2>{200-131/scale,300-82/scale},"base anchor");
        need(origin.roofed==std::array<int,2>{200+(dx-131)/scale,300+(dy-82)/scale},"signed full numerator");
        if(scale==1){
            // A pixel from the common untrimmed canvas has different local
            // coordinates after the two crops, but must land at the same point.
            const std::array<int,2> basePixel{{47,63}},roofPixel{{47-dx,63-dy}};
            need(origin.base[0]+basePixel[0]==origin.roofed[0]+roofPixel[0]&&
                 origin.base[1]+basePixel[1]==origin.roofed[1]+roofPixel[1],"asymmetric crop alignment");
        }
    }
    const auto odd=odh::lot_picture_origins(0,0,{{131,100,-18,1,1}},2);
    need(odd.roofed==std::array<int,2>{-65,-40},"do not round displacement separately");
    bool refused=false;try{odh::lot_picture_origins(0,0,{{}},0);}catch(const std::invalid_argument&){refused=true;}
    need(refused,"invalid scale refuses division");
    std::cout<<"Lot picture placement passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
