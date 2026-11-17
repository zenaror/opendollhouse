// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace odh {
struct LotPictureOrigins {
    std::array<int,2> base,roofed;
};

// The DVD's five THMB words describe the base picture's projected centroid,
// bottom offset and the second picture's crop displacement. They are not
// bitmap dimensions. Both independently cropped images share one map marker.
inline LotPictureOrigins lot_picture_origins(int markerX,int markerY,
        const std::array<int,5>& metadata,int divisor) {
    if(divisor<=0)throw std::invalid_argument("Invalid lot picture scale");
    const auto coordinate=[divisor](int marker,int64_t numerator){
        const auto value=int64_t(marker)+numerator/divisor;
        if(value<std::numeric_limits<int>::min()||value>std::numeric_limits<int>::max())
            throw std::out_of_range("Lot picture origin exceeds coordinate bounds");
        return int(value);
    };
    const int64_t x=-int64_t(metadata[0]);
    const int64_t y=-int64_t(metadata[1])-metadata[2];
    // Divide each complete signed origin, not a separately rounded delta.
    return {{{coordinate(markerX,x),coordinate(markerY,y)}},
            {{coordinate(markerX,x+metadata[3]),coordinate(markerY,y+metadata[4])}}};
}
}
