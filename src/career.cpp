// SPDX-License-Identifier: MPL-2.0
// CARR.cs / IffFieldEncode.cs and native objm_tail.cpp alignment algorithm.
#include "odh/career.hpp"
#include <limits>
namespace odh {
namespace {
class CareerFields {
public:
    CareerFields(const View& view,std::uint64_t start,const CareerLimits& limits,std::uint64_t strings)
        :view_(view),position_(start*8),total_(view.size()*8),limits_(limits),strings_(strings){}
    std::uint64_t position() const {return position_;}
    std::int32_t integer(){
        field_budget();
        if(!bits(1))return 0;
        static constexpr unsigned widths[]{6,11,21,32};
        const auto width=widths[bits(2)];const auto raw=bits(width);
        const auto value=(raw&(std::uint32_t{1}<<(width-1)))?
            static_cast<std::int64_t>(raw)-(std::int64_t{1}<<width):static_cast<std::int64_t>(raw);
        return static_cast<std::int32_t>(value);
    }
    std::string string(bool next,std::optional<std::uint8_t>& pad){
        field_budget();
        if(!string_mode_){if(position_%8==0)odd_=!odd_;else position_+=8-position_%8;}
        const auto start=position_/8;auto end=start;
        for(;;++end){
            if(end>=view_.size())throw FormatError("truncated CARR string NUL");
            if(!view_.u8(end))break;
            if(end-start>=limits_.string_length||strings_>=limits_.string_bytes)
                throw FormatError("CARR string budget exceeded");
            ++strings_;
        }
        const auto length=end-start;++end;
        if((length%2==0)==!odd_){if(end>=view_.size())throw FormatError("truncated CARR string padding");pad=view_.u8(end++);}
        std::string value(static_cast<std::size_t>(length),'\0');
        if(length)view_.read(start,value.data(),static_cast<std::size_t>(length));
        position_=end*8;string_mode_=!next;odd_=next;
        return value;
    }
private:
    void field_budget(){if(decoded_>=limits_.decoded_fields)throw FormatError("CARR decoded field budget exceeded");++decoded_;}
    std::uint32_t bits(unsigned n){
        if(string_mode_||n>total_-position_)throw FormatError("truncated CARR compressed field");
        std::uint32_t value=0;
        for(unsigned i=0;i<n;++i){value=(value<<1)|((view_.u8(position_/8)>>(7-position_%8))&1U);
            ++position_;if(position_%8==0)odd_=!odd_;}
        return value;
    }
    const View& view_;std::uint64_t position_,total_;const CareerLimits& limits_;
    std::uint64_t strings_=0,decoded_=0;bool odd_=true,string_mode_=false;
};
}
Career read_carr(const View& view,const CareerLimits& limits){
    if(view.size()>limits.payload_bytes||view.size()>std::numeric_limits<std::uint64_t>::max()/8)
        throw FormatError("CARR payload budget exceeded");
    Career value;value.padding=view.le32(0);value.version=view.le32(4);value.magic=view.le32(8);value.compression=view.u8(12);
    if(value.compression!=1)throw FormatError("unsupported CARR compression code");
    std::uint64_t end=13;
    for(;;++end){if(end>=view.size())throw FormatError("truncated CARR name NUL");if(!view.u8(end))break;
        if(end-13>=limits.string_length||end-13>=limits.string_bytes)throw FormatError("CARR name budget exceeded");}
    value.name.resize(static_cast<std::size_t>(end-13));if(!value.name.empty())view.read(13,value.name.data(),value.name.size());
    ++end;if(value.name.size()%2){if(end>=view.size())throw FormatError("truncated CARR name padding");value.name_padding=view.u8(end++);}
    CareerFields fields(view,end,limits,value.name.size());
    const auto count=fields.integer();
    if(count<0||static_cast<std::uint32_t>(count)>limits.levels)throw FormatError("invalid CARR level count or budget");
    // At least one bit per integer and a NUL per string, before any pads.
    if(static_cast<std::uint64_t>(count)>(view.size()*8-fields.position())/61)
        throw FormatError("CARR levels exceed remaining payload");
    value.levels.reserve(static_cast<std::size_t>(count));
    for(std::int32_t n=0;n<count;++n){
        CareerLevel level;
        for(auto& field:level.minimum_required)field=fields.integer();
        for(auto& field:level.motive_delta)field=fields.integer();
        level.salary=fields.integer();level.start_hour=fields.integer();level.end_hour=fields.integer();level.car_type=fields.integer();
        level.job_name=fields.string(false,level.string_padding[0]);
        level.male_uniform_mesh=fields.string(false,level.string_padding[1]);
        level.female_uniform_mesh=fields.string(false,level.string_padding[2]);
        level.uniform_skin=fields.string(false,level.string_padding[3]);
        level.unknown=fields.string(true,level.string_padding[4]);
        value.levels.push_back(std::move(level));
    }
    value.consumed_bits=fields.position();value.remaining_bits=view.size()*8-value.consumed_bits;
    value.suffix.assign(static_cast<std::size_t>((value.remaining_bits+7)/8),0);
    for(std::uint64_t n=0;n<value.remaining_bits;++n){const auto offset=value.consumed_bits+n;
        if((view.u8(offset/8)>>(7-offset%8))&1U)value.suffix[n/8]|=static_cast<std::uint8_t>(1U<<(7-n%8));}
    return value;
}
CareerCatalog::CareerCatalog(std::vector<std::pair<std::uint16_t,Career>> careers,const CareerLimits& limits){
    if(careers.size()>limits.careers)throw FormatError("career catalog count exceeds budget");
    std::uint64_t levels=0,strings=0;
    const auto add_string=[&](const std::string& s){
        if(s.size()>limits.string_length||s.size()>limits.string_bytes-strings)throw FormatError("career catalog string budget exceeded");
        strings+=s.size();};
    for(auto& entry:careers){
        if(entry.second.levels.size()>limits.levels-levels)throw FormatError("career catalog level budget exceeded");
        levels+=entry.second.levels.size();
        add_string(entry.second.name);
        for(const auto& level:entry.second.levels){add_string(level.job_name);add_string(level.male_uniform_mesh);
            add_string(level.female_uniform_mesh);add_string(level.uniform_skin);add_string(level.unknown);}
        if(!careers_.emplace(entry.first,std::move(entry.second)).second)throw FormatError("duplicate CARR career ID");
        enumeration_order_.push_back(entry.first);
    }
}
std::int16_t CareerCatalog::next(std::int16_t current) const noexcept {
    for(const auto id:enumeration_order_)if(static_cast<std::int32_t>(id)>current)
        return id<=32767?static_cast<std::int16_t>(id):static_cast<std::int16_t>(static_cast<std::int32_t>(id)-65536);
    return -1;
}
const Career* CareerCatalog::find(std::uint16_t id) const noexcept {const auto found=careers_.find(id);return found==careers_.end()?nullptr:&found->second;}
std::optional<std::int16_t> CareerCatalog::job_data(std::uint16_t job,std::int16_t level,std::int16_t field) const noexcept {
    const auto* career=find(job);if(!career)return std::int16_t{0};
    if(level<0||static_cast<std::size_t>(level)>=career->levels.size())return {};
    const auto& entry=career->levels[static_cast<std::size_t>(level)];
    std::int64_t value=0;
    switch(field) {
    case 0:value=static_cast<std::int64_t>(career->levels.size());break;
    case 1:value=entry.salary;break;
    case 12:value=entry.start_hour;break;
    case 13:value=entry.end_hour;break;
    case 21:value=entry.car_type;break;
    case 22:break;
    default:
        if(field>=2&&field<=11)value=entry.minimum_required[static_cast<std::size_t>(field-2)];
        else if(field>=14&&field<=20)value=entry.motive_delta[static_cast<std::size_t>(field-14)];
        else return {};
    }
    const auto raw=static_cast<std::uint16_t>(value);
    return static_cast<std::int16_t>(raw<=32767?static_cast<std::int32_t>(raw):static_cast<std::int32_t>(raw)-65536);
}
CareerLookup CareerCatalog::lookup(std::int16_t job,std::int32_t level) const noexcept {
    const auto* career=find(static_cast<std::uint16_t>(job));if(!career)return {CareerLookupStatus::missing_career,nullptr};
    if(level<0||static_cast<std::uint64_t>(level)>=career->levels.size())return {CareerLookupStatus::invalid_level,nullptr};
    return {CareerLookupStatus::found,&career->levels[static_cast<std::size_t>(level)]};
}
} // namespace odh
