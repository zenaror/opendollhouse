// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace FSO { namespace Common { namespace TS1 {
enum class LotPictureFormat : uint8_t { RGB555, RGB565 };
struct LotPictureSurface {
    int Width=0,Height=0;
    LotPictureFormat Format=LotPictureFormat::RGB565;
    uint16_t Key=0xf81f;
    bool Keyed=true;
    std::vector<uint16_t> Pixels;
    bool Valid() const {return Width>0 && Height>0 && Pixels.size()==std::size_t(Width)*Height;}
};
struct LotPictureTrim {
    int Left=0,Top=0,Right=0,Bottom=0;
    int WidthAfter(int width) const {return width-Left-Right;}
    int HeightAfter(int height) const {return height-Top-Bottom;}
    bool HasContent(int width,int height) const {return WidthAfter(width)>0 && HeightAfter(height)>0;}
};
// Exactly the five source THMB DWORDs, independent of BMP dimensions.
struct LotPictureMetadata {
    int32_t AnchorX=0,AnchorY=0,BaseYOff=0,RoofXOff=0,RoofYOff=0;
    std::array<int32_t,5> Words() const {return {{AnchorX,AnchorY,BaseYOff,RoofXOff,RoofYOff}};}
};
struct LotPicturePlan {
    int RequestedWidth=0,RequestedHeight=0;
    int OffsetX=0,OffsetY=0,BaseYOff=0;
    int MinX=0,MaxX=0,MinY=0,MaxY=0;
};
// Source terrain-only diagonal profiles, then004aa1b9 bounds. View dimensions
// and story step are explicit renderer inputs; old image chunks are not inputs.
LotPicturePlan PlanLotPictures(const std::vector<uint8_t>& canonicalHeightQuads,
    int activeWidth,int viewWidth,int viewHeight,int zoom,int camera,int storyPixelStep);
LotPictureSurface CropLotPicture(const LotPictureSurface& source,const LotPictureTrim& trim);
std::vector<uint8_t> LotPictureRGB(const LotPictureSurface& source);
uint16_t PackLotPictureRGB(const std::array<uint8_t,3>& rgb,LotPictureFormat format);
std::array<uint8_t,3> ExpandLotPictureRGB(uint16_t word,LotPictureFormat format);
//004ba98d: horizontal then vertical, cubic support, reflected edge indices,
// key mass threshold and format quantization after EACH axis.
LotPictureSurface ResizeLotPicture(const LotPictureSurface& source,float scaleX,float scaleY);
// Preserve literal trim margins even for an all-key surface. The encoder/caller
// owns invalid/empty crop handling; no invented zero-size convention here.
LotPictureTrim TrimLotPicture(const LotPictureSurface& source);
LotPictureMetadata PlaceLotPictures(const LotPictureMetadata& uncropped,
    const LotPictureTrim& open,const LotPictureTrim& roofed);
// Source BMP encoding first reduces565 to555, then zero-expands channels.
std::array<uint8_t,3> LotPictureBMPColor(uint16_t word,LotPictureFormat format);
}}}
