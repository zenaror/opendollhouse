#include "odh/property_value_scale.hpp"

namespace odh {
PropertyValueScaleResult comparison_pc24_truncate_scale(std::int32_t input,
    std::uint32_t bits) noexcept {
    using R=PropertyValueScaleReason;
    const auto encoded_exponent=(bits>>23)&255U;
    if(encoded_exponent==255U)return {{},R::nonfinite_multiplier};
    const auto fraction=bits&0x7fffffU;
    const auto significand=fraction|(encoded_exponent?0x800000U:0U);
    if(!input || !significand)return {std::int32_t{0},R::none};
    const bool negative=(input<0)!=((bits&0x80000000U)!=0);
    const auto absolute_input=static_cast<std::uint64_t>(input<0?-static_cast<std::int64_t>(input):input);
    // abs(INT32_MIN)*(2^24-1)<2^55. FILD is exact: do not first round input to
    // binary32, even when abs(input)>2^24.0042733b..00427347 is the source seam.
    auto product=absolute_input*significand;
    int exponent=encoded_exponent?static_cast<int>(encoded_exponent)-150:-149;
    unsigned width=0;
    for(auto remaining=product;remaining;remaining>>=1)++width;
    if(width>24) {
        const auto shift=width-24;
        auto rounded=product>>shift;
        const auto remainder=product&((std::uint64_t{1}<<shift)-1);
        const auto halfway=std::uint64_t{1}<<(shift-1);
        if(remainder>halfway || (remainder==halfway && (rounded&1U)))++rounded;
        exponent+=static_cast<int>(shift);
        // A ties-even round can carry beyond the24-bit significand.
        if(rounded==(std::uint64_t{1}<<24)){rounded>>=1;++exponent;}
        product=rounded;
    }
    //0063604c changes RC totruncate only AFTER multiplication and restores it.
    // Its native consumer uses a signed32 subset here; reject wider results.
    const std::uint64_t limit=negative?2147483648ULL:2147483647ULL;
    std::uint64_t magnitude;
    if(exponent>=0) {
        if(exponent>31 || product>(limit>>static_cast<unsigned>(exponent)))return {{},R::out_of_range};
        magnitude=product<<static_cast<unsigned>(exponent);
    } else {
        const auto shift=static_cast<unsigned>(-exponent);
        magnitude=shift>=64?0:product>>shift;
        if(magnitude>limit)return {{},R::out_of_range};
    }
    const auto signed_value=negative?-static_cast<std::int64_t>(magnitude):static_cast<std::int64_t>(magnitude);
    return {static_cast<std::int32_t>(signed_value),R::none};
}
const char* property_value_scale_reason_name(PropertyValueScaleReason value) noexcept {
    switch(value){
    case PropertyValueScaleReason::none:return "none";
    case PropertyValueScaleReason::nonfinite_multiplier:return "nonfinite_multiplier";
    case PropertyValueScaleReason::out_of_range:return "out_of_range";
    }return "invalid";
}
} // namespace odh
