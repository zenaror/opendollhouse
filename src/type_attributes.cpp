// SPDX-License-Identifier: MPL-2.0
#include "odh/type_attributes.hpp"
#include "odh/world.hpp"
#include <array>
#include <limits>
#include <sstream>
namespace odh {
namespace {
std::int16_t signed16(std::uint32_t v){return static_cast<std::int16_t>(v<32768?v:static_cast<std::int32_t>(v)-65536);}
class Fields {
public:
    Fields(const View& v,bool compressed):v_(v),compressed_(compressed){}
    std::int32_t integer(){if(compressed_)return field({6,11,21,32});const auto v=v_.le32(byte_);byte_+=4;return static_cast<std::int32_t>(v<=2147483647U?static_cast<std::int64_t>(v):static_cast<std::int64_t>(v)-4294967296LL);}
    std::int16_t word(){if(compressed_)return static_cast<std::int16_t>(field({5,8,13,16}));const auto v=v_.le16(byte_);byte_+=2;return signed16(v);}
    std::uint64_t consumed()const{return compressed_?(bit_+7)/8:byte_;}
private:
    std::uint32_t bits(unsigned count){
        if(bit_>v_.size()*8||count>v_.size()*8-bit_)throw FormatError("truncated TATT compressed field");
        std::uint32_t result=0;for(unsigned i=0;i<count;++i){result=(result<<1)|((v_.u8(bit_/8)>>(7-bit_%8))&1U);++bit_;}return result;
    }
    std::int32_t field(const std::array<unsigned,4>& widths){if(!bits(1))return 0;const auto width=widths[bits(2)];const auto raw=bits(width);return static_cast<std::int32_t>((raw&(std::uint32_t{1}<<(width-1)))?static_cast<std::int64_t>(raw)-(std::int64_t{1}<<width):raw);}
    const View& v_;bool compressed_;std::uint64_t byte_=13,bit_=104;
};
std::size_t bounded_count(std::int32_t value,std::size_t limit,const char* message){if(value<0||static_cast<std::uint64_t>(value)>limit)throw FormatError(message);return static_cast<std::size_t>(value);}
void validate(const TypeAttributesSnapshot& state,const TypeAttributesLimits& limits){
    if(state.version!=1||state.by_guid.size()>limits.types)throw FormatError("type attributes version/type limit exceeded");
    std::size_t words=0;for(const auto& [guid,row]:state.by_guid){(void)guid;if(row.size()>limits.words_per_type||row.size()>limits.words-words)throw FormatError("type attributes word budget exceeded");words+=row.size();}
}
std::optional<std::uint32_t> frame_guid(const World& world,const VmFrame& frame,std::uint8_t scope,const TypeAttributeGuidResolver& resolver){
    if((scope!=36&&scope!=37)||!resolver)return {};
    const auto* entity=world.frame_entity(frame,scope==36?VmEntityRole::caller:VmEntityRole::stack_object);
    if(!entity||!entity->definition)return {};
    return resolver(entity->definition->master_guid.value_or(entity->definition->guid));
}
}
TypeAttributesSnapshot read_tatt(const View& view,TypeAttributesLimits limits){
    if(view.size()>limits.payload_bytes)throw FormatError("TATT payload exceeds byte budget");
    const auto header=view.subview(0,13);
    if(header.le32(0)!=0||header.le32(4)!=0||header.le32(8)!=0x54415454U)throw FormatError("unsupported TATT header");
    const auto compression=header.u8(12);if(compression>1)throw FormatError("unsupported TATT compression");
    Fields fields(view,compression==1);TypeAttributesSnapshot result;
    const auto count=bounded_count(fields.integer(),limits.types,"TATT type count exceeds limits");std::size_t decoded_words=0;
    for(std::size_t n=0;n<count;++n){
        const auto guid=static_cast<std::uint32_t>(fields.integer());
        const auto size=bounded_count(fields.integer(),limits.words_per_type,"TATT bank length exceeds limits");
        if(size>limits.words-decoded_words)throw FormatError("TATT cumulative decoded word budget exceeded");decoded_words+=size;
        std::vector<std::int16_t> row;row.reserve(size);for(std::size_t i=0;i<size;++i)row.push_back(fields.word());
        result.by_guid[guid]=std::move(row); // Source dictionary uses last serialized row.
    }
    if(fields.consumed()!=view.size())throw FormatError("unpreserved TATT trailing bytes");
    validate(result,limits);return result;
}
TypeAttributes::TypeAttributes(TypeAttributesSnapshot value,TypeAttributesLimits limits):limits_(limits){restore(value);changed_=false;}
void TypeAttributes::restore(const TypeAttributesSnapshot& state){
    validate(state,limits_);auto candidate=state;std::size_t words=0;for(const auto& item:candidate.by_guid)words+=item.second.size();
    state_=std::move(candidate);words_=words;changed_=true;
}
std::optional<std::int16_t> TypeAttributes::read(std::uint32_t guid,std::int16_t index)const{
    const auto found=state_.by_guid.find(guid);if(found==state_.by_guid.end())return std::int16_t{0};
    if(index<0)return {};if(static_cast<std::size_t>(index)>=found->second.size())return std::int16_t{0};return found->second[static_cast<std::size_t>(index)];
}
bool TypeAttributes::write(std::uint32_t guid,std::int16_t index,std::int16_t value,bool resource_exists){
    auto found=state_.by_guid.find(guid);
    if(found==state_.by_guid.end()){
        if(!resource_exists)return true;
        if(index<0)return false;
        if(state_.by_guid.size()>=limits_.types||32>limits_.words_per_type||32>limits_.words-words_)throw FormatError("type attribute creation budget exceeded");
        found=state_.by_guid.emplace(guid,std::vector<std::int16_t>(32,0)).first;words_+=32;changed_=true;
    }
    if(index<0)return false;
    if(static_cast<std::size_t>(index)>=found->second.size())return true;
    auto& word=found->second[static_cast<std::size_t>(index)];if(word!=value){word=value;changed_=true;}return true;
}
VmMemory type_attribute_memory(const World& world,TypeAttributes& state,TypeAttributeGuidResolver resolver,TypeAttributeResourceExists exists){
    VmMemory memory;
    memory.read=[&world,&state,resolver](const VmFrame& frame,std::uint8_t scope,std::int16_t index)->std::optional<std::int16_t>{const auto guid=frame_guid(world,frame,scope,resolver);return guid?state.read(*guid,index):std::nullopt;};
    memory.write=[&world,&state,resolver,exists](const VmFrame& frame,std::uint8_t scope,std::int16_t index,std::int16_t value){const auto guid=frame_guid(world,frame,scope,resolver);return guid&&exists&&state.write(*guid,index,value,exists(*guid));};return memory;
}
std::vector<std::uint8_t> encode_type_attributes_snapshot(const TypeAttributesSnapshot& state,TypeAttributesLimits limits){
    validate(state,limits);std::vector<std::uint8_t> out;
    const auto u32=[&](std::uint32_t v){for(unsigned i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(v>>(i*8)));};
    u32(state.version);u32(static_cast<std::uint32_t>(state.by_guid.size()));
    for(const auto& [guid,row]:state.by_guid){u32(guid);u32(static_cast<std::uint32_t>(row.size()));for(auto value:row){const auto raw=static_cast<std::uint16_t>(value);out.push_back(static_cast<std::uint8_t>(raw));out.push_back(static_cast<std::uint8_t>(raw>>8));}}
    if(out.size()>limits.payload_bytes)throw FormatError("type attribute snapshot byte budget exceeded");return out;
}
TypeAttributesSnapshot decode_type_attributes_snapshot(const std::vector<std::uint8_t>& bytes,TypeAttributesLimits limits){
    if(bytes.size()>limits.payload_bytes)throw FormatError("type attribute snapshot byte budget exceeded");
    std::string raw(bytes.begin(),bytes.end());std::istringstream stream(raw,std::ios::binary);const View view(stream,raw.size());std::uint64_t cursor=0;
    const auto u32=[&](){const auto v=view.le32(cursor);cursor+=4;return v;};TypeAttributesSnapshot state;state.version=u32();
    const auto count=u32();if(count>limits.types)throw FormatError("type attribute snapshot type limit exceeded");std::size_t words=0;
    for(std::uint32_t n=0;n<count;++n){const auto guid=u32();const auto size=u32();if(size>limits.words_per_type||size>limits.words-words||state.by_guid.count(guid))throw FormatError("invalid type attribute snapshot row");words+=size;
        if(size>(view.size()-cursor)/2)throw FormatError("truncated type attribute snapshot row");auto& row=state.by_guid[guid];row.reserve(size);for(std::uint32_t i=0;i<size;++i){row.push_back(signed16(view.le16(cursor)));cursor+=2;}}
    if(cursor!=view.size())throw FormatError("trailing type attribute snapshot bytes");validate(state,limits);return state;
}
}
