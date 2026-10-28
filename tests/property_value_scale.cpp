#include "odh/property_value_scale.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void value(std::int32_t input,std::uint32_t multiplier,std::int32_t expected,const char* message) {
    const auto result=odh::comparison_pc24_truncate_scale(input,multiplier);
    check(result.reason==odh::PropertyValueScaleReason::none && result.value==expected,message);
}
void reject(std::int32_t input,std::uint32_t multiplier,odh::PropertyValueScaleReason reason,const char* message) {
    const auto result=odh::comparison_pc24_truncate_scale(input,multiplier);
    check(!result.value && result.reason==reason,message);
}
}
int main(){try{
    using R=odh::PropertyValueScaleReason;
    constexpr auto minimum=std::numeric_limits<std::int32_t>::min();
    constexpr auto maximum=std::numeric_limits<std::int32_t>::max();
    //0042730f FILD/FMUL and0063604c RC11 truncation. With intendedPC24,
    // 10*binary32(.7) rounds to7 before conversion; extended product alone is<7.
    value(10,0x3f333333U,7,"PC24 rounds product before truncation");
    value(-10,0x3f333333U,-7,"negative product rounding");
    value(10,0xbf333333U,-7,"negative multiplier");
    value(-10,0xbf333333U,7,"two negative signs");
    value(5,0x3f333333U,3,"positive fractional truncate");
    value(-5,0x3f333333U,-3,"negative fractional truncate towardzero");
    // Fixed exact-rational vectors. Integer input must not be cast tofloat32
    // first: doing that would give11744051 and11744054 respectively.
    value(16777217,0x3f333333U,11744052,"FILD exact above2^24");
    value(16777219,0x3f333333U,11744053,"FILD exact with odd integer");
    value(16777217,0x3f800001U,16777220,"input-rounding counterexample");
    // Exact products are12582913.5 and12582916.5: opposite tie parity.
    value(3,0x4a800001U,12582914,"tie odd significand rounds up");
    value(3,0x4a800003U,12582916,"tie even significand remains down");
    value(-3,0x4a800001U,-12582914,"negative tie symmetry");
    value(16777217,0x3f7fffffU,16777216,"rounding carry normalizes significand");
    value(minimum,0x3f800000U,minimum,"INTMIN exact boundary");
    reject(maximum,0x3f800000U,R::out_of_range,"INTMAX rounds above positive supported limit");
    value(maximum,0xbf800000U,minimum,"negative result reaches INTMIN after round");
    reject(minimum,0xbf800000U,R::out_of_range,"INTMIN signflip exceeds positive limit");
    value(1073741823,0x3f800000U,1073741824,"nearest carry within range");
    value(maximum,0x3f000000U,1073741824,"large input half rounds product");
    value(minimum,0x3f000000U,-1073741824,"negative poweroftwo boundary");
    reject(maximum,0x40000000U,R::out_of_range,"scaled result overflow");
    reject(minimum,0x40000000U,R::out_of_range,"negative scaled result overflow");
    value(maximum,0x00000001U,0,"smallest positive subnormal truncates");
    value(minimum,0x80000001U,0,"negative smallest subnormal truncates");
    value(maximum,0x007fffffU,0,"largest subnormal truncates");
    value(maximum,0x00800000U,0,"smallest normal truncates");
    value(minimum,0x00000000U,0,"positive zero multiplier");
    value(minimum,0x80000000U,0,"negative zero multiplier");
    value(0,0x7f7fffffU,0,"zero input finite large multiplier");
    reject(1,0x7f7fffffU,R::out_of_range,"largest finite multiplier overflow");
    for(const auto bits:{0x7f800000U,0xff800000U,0x7fc00000U,0x7f800001U,0xffc00000U}) {
        reject(10,bits,R::nonfinite_multiplier,"nonfinite rejected");
        reject(0,bits,R::nonfinite_multiplier,"zero input does not hide nonfinite multiplier");
    }
    check(std::string(odh::property_value_scale_reason_name(R::out_of_range))=="out_of_range","reason names");
    std::cout<<"explicit PC24 integer scale, exact input, tie, carry and signed bounds passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
