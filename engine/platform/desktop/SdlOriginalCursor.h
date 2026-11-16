/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <SDL.h>
#include "app/ui/UIGraphics.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace odh::desktop {
// The SDL window owns its native handles; UIGraphics keeps selection/decoding
// tied to the actual installed FAR and reuses the previous validated CUR reader.
class SdlOriginalCursor {
public:
    ~SdlOriginalCursor(){SDL_SetCursor(SDL_GetDefaultCursor());for(auto& entry:handles_)if(entry.second)SDL_FreeCursor(entry.second);}
    SdlOriginalCursor()=default;
    SdlOriginalCursor(const SdlOriginalCursor&)=delete;
    SdlOriginalCursor& operator=(const SdlOriginalCursor&)=delete;
    void Select(const std::string& path){
        if(path==selected_)return;
        if(path.empty()){SDL_SetCursor(SDL_GetDefaultCursor());selected_.clear();return;}
        auto found=handles_.find(path);
        if(found==handles_.end()){
            SDL_Cursor* native=nullptr;
            try {
                if(const auto* image=simapp::UIGraphics::Get().NativeCursor(path)){
                    // The old native backend also refuses inversion explicitly:
                    // SDL's inverted monochrome pixels are platform-dependent.
                    if(std::none_of(image->invert.begin(),image->invert.end(),[](auto value){return value!=0;})){
                        const auto stride=(image->width+7)/8;
                        std::vector<Uint8> data(size_t(stride)*image->height,0),mask(data.size(),0);
                        for(unsigned y=0;y<image->height;++y)for(unsigned x=0;x<image->width;++x){
                            const auto& pixel=image->pixels[size_t(y)*image->width+x];
                            if(!pixel[3])continue;
                            const auto at=size_t(y)*stride+x/8;const auto bit=Uint8(0x80u>>(x%8));
                            mask[at]|=bit;if(pixel[0]==0)data[at]|=bit;
                        }
                        native=SDL_CreateCursor(data.data(),mask.data(),stride*8,image->height,image->hotspot_x,image->hotspot_y);
                    }
                }
            }catch(const std::exception& error){std::fprintf(stderr,"desktop cursor %s: %s\n",path.c_str(),error.what());}
            if(!native && path=="Shared/cursors/HOURGLASS.cur")native=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
            if(!native)std::fprintf(stderr,"desktop cursor unavailable/unsupported: %s; retaining system pointer\n",path.c_str());
            found=handles_.emplace(path,native).first;
        }
        SDL_SetCursor(found->second?found->second:SDL_GetDefaultCursor());selected_=path;
    }
private:
    std::map<std::string,SDL_Cursor*> handles_;
    std::string selected_;
};
}
