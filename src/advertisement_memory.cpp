// SPDX-License-Identifier: MPL-2.0
#include "odh/advertisement_memory.hpp"
#include "odh/vm.hpp"
namespace odh {
std::optional<std::uint8_t> advertisement_scope_type(std::uint8_t scope) noexcept {
    switch(scope){case 12:return 1;case 28:return 2;case 29:return 0;default:return {};}
}
bool advertisement_changes_valid(const AdvertisementChanges& state,std::size_t limit) noexcept {
    if(!state)return true;
    if(state->size()>limit)return false;
    for(const auto& entry:*state)if((entry.first>>16)>2)return false;
    return true;
}
std::optional<std::int16_t> read_advertisement(const AdvertisementChanges& state,const VmFrame& frame,
    std::uint8_t type,std::uint16_t index,const AdvertisementFallback& fallback){
    if(type>2)return {};
    if(state){const auto at=state->find((std::uint32_t(type)<<16)|index);if(at!=state->end())return at->second;}
    return fallback?fallback(frame,type,index):std::optional<std::int16_t>{0};
}
bool write_advertisement(AdvertisementChanges& state,std::uint8_t type,std::uint16_t index,
    std::int16_t value,std::size_t limit){
    if(type>2)return false;
    const auto key=(std::uint32_t(type)<<16)|index;
    if(!state){if(!limit)return false;state.emplace();}
    if(state->size()>=limit&&!state->count(key))return false;
    (*state)[key]=value;return true;
}
VmMemory with_advertisement_memory(VmMemory memory,AdvertisementChanges& state,AdvertisementFallback fallback,std::size_t limit){
    const auto read=memory.read;const auto write=memory.write;const auto expression=memory.expression_write;
    memory.read=[&state,fallback,read](const VmFrame& frame,std::uint8_t scope,std::int16_t index)->std::optional<std::int16_t>{
        if(const auto type=advertisement_scope_type(scope))return read_advertisement(state,frame,*type,static_cast<std::uint16_t>(index),fallback);
        return read?read(frame,scope,index):std::nullopt;
    };
    memory.write=[&state,write,limit](const VmFrame& frame,std::uint8_t scope,std::int16_t index,std::int16_t value){
        if(const auto type=advertisement_scope_type(scope))return write_advertisement(state,*type,static_cast<std::uint16_t>(index),value,limit);
        return write&&write(frame,scope,index,value);
    };
    memory.expression_write=[&state,expression,write,limit](const VmFrame& frame,std::uint8_t scope,std::int16_t index,std::int16_t value)->std::optional<bool>{
        if(const auto type=advertisement_scope_type(scope))return write_advertisement(state,*type,static_cast<std::uint16_t>(index),value,limit)?std::optional<bool>{true}:std::nullopt;
        if(expression)return expression(frame,scope,index,value);
        return write&&write(frame,scope,index,value)?std::optional<bool>{true}:std::nullopt;
    };
    return memory;
}
}
