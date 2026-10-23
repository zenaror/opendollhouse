#pragma once
#include "odh/objm_tail.hpp"
#include "json.hpp"
#include <ostream>

namespace odh_tools {
template<class Values>void saved_bank(std::ostream& out,const Values& values){
    out<<'[';for(std::size_t n=0;n<values.size();++n){if(n)out<<',';out<<+values[n];}out<<']';
}
inline void saved_string(std::ostream& out,const odh::ObjmSavedString& value){
    out<<"{\"bytes\":"<<json_bytes(value.bytes)<<",\"start_byte\":"<<value.start_byte
        <<",\"padding\":"<<(value.padding?std::to_string(*value.padding):"null")<<'}';
}
inline void saved_interaction(std::ostream& out,const odh::ObjmSavedInteraction& i){
    out<<"{\"unknown_zero\":"<<i.unknown_zero<<",\"uid\":"<<i.uid<<",\"caller_id\":"<<i.caller_id
        <<",\"target_id\":"<<i.target_id<<",\"icon\":"<<i.icon<<",\"tta_index\":"<<i.tta_index
        <<",\"args\":";saved_bank(out,i.args);
    out<<",\"priority\":"<<i.priority<<",\"action_tree_id\":"<<i.action_tree_id
        <<",\"attenuation_bits\":"<<i.attenuation_bits<<",\"flags\":"<<i.flags<<'}';
}
inline void saved_tail(std::ostream& out,const odh::ObjmTailState& s){
    out<<"{\"block_offset\":"<<s.block_offset<<",\"odd_offset\":"<<(s.odd_offset?"true":"false")
        <<",\"start_bits\":"<<s.start_bits<<",\"consumed_bits\":"<<s.consumed_bits
        <<",\"remaining_bits\":"<<s.remaining_bits<<",\"decoded_words\":"<<s.decoded_words
        <<",\"string_bytes\":"<<s.string_bytes<<",\"unknown_int\":"<<s.unknown_int
        <<",\"unknown_int_present\":"<<(s.unknown_int_present?"true":"false")
        <<",\"placeholder_unknown_int_omission_verified\":"
        <<(s.placeholder_unknown_int_omission_verified?"true":"false")<<",\"remaining_data\":";
    saved_bank(out,s.remaining_data);out<<",\"multitile\":";
    if(s.multitile){const auto& m=*s.multitile;
        out<<"{\"offset_x\":"<<m.offset_x<<",\"offset_y\":"<<m.offset_y<<",\"offset_level\":"<<m.offset_level
            <<",\"group_x\":"<<m.group_x<<",\"group_y\":"<<m.group_y<<",\"group_level\":"<<m.group_level
            <<",\"parent_id\":"<<m.parent_id<<'}';
    }else out<<"null";
    out<<",\"portal_count\":"<<(s.portal_count?std::to_string(*s.portal_count):"null")<<",\"portal_float_bits\":";
    if(s.portal_float_bits)saved_bank(out,*s.portal_float_bits);else out<<"null";
    out<<",\"person\":";
    if(s.person){const auto& p=*s.person;
        out<<"{\"animation_event_count\":"<<p.animation_event_count<<",\"engaged\":"<<p.engaged;
        const std::array<std::pair<const char*,const odh::ObjmSavedString*>,13> strings{{
            {"body",&p.body},{"body_texture",&p.body_texture},{"unknown1",&p.unknown1},{"unknown2",&p.unknown2},
            {"left_hand",&p.left_hand},{"left_hand_texture",&p.left_hand_texture},{"right_hand",&p.right_hand},
            {"right_hand_texture",&p.right_hand_texture},{"head",&p.head},{"head_texture",&p.head_texture},
            {"animation",&p.animation},{"carry_animation",&p.carry_animation},{"base_animation",&p.base_animation}}};
        for(const auto& pair:strings){out<<','<<json_bytes(pair.first)<<':';saved_string(out,*pair.second);}
        out<<",\"accessory_count\":"<<p.accessory_count<<",\"accessories\":[";
        for(std::size_t n=0;n<p.accessories.size();++n){if(n)out<<',';out<<"{\"name\":";
            saved_string(out,p.accessories[n].name);out<<",\"binding\":";saved_string(out,p.accessories[n].binding);out<<'}';}
        out<<"],\"routing_state\":"<<p.routing_state<<",\"first_float_bits\":";saved_bank(out,p.first_float_bits);
        out<<",\"motive_data_old_bits\":";saved_bank(out,p.motive_data_old_bits);
        out<<",\"motive_data_bits\":";saved_bank(out,p.motive_data_bits);
        out<<",\"person_data\":";saved_bank(out,p.person_data);
        out<<",\"routing_frame_count\":"<<p.routing_frame_count<<",\"active_interaction\":";saved_interaction(out,p.active_interaction);
        out<<",\"last_interaction\":";saved_interaction(out,p.last_interaction);
        out<<",\"interaction_queue_count\":"<<p.interaction_queue_count<<",\"interaction_queue\":[";
        for(std::size_t n=0;n<p.interaction_queue.size();++n){if(n)out<<',';saved_interaction(out,p.interaction_queue[n]);}
        out<<"],\"object_use_count\":"<<p.object_use_count<<",\"object_uses\":[";
        for(std::size_t n=0;n<p.object_uses.size();++n){if(n)out<<',';const auto& u=p.object_uses[n];
            out<<"{\"target_id\":"<<u.target_id<<",\"stack_length\":"<<u.stack_length<<",\"unknown2\":"<<static_cast<unsigned>(u.unknown2)<<'}';}
        out<<"],\"motive_delta_count\":"<<p.motive_delta_count<<",\"motive_deltas\":[";
        for(std::size_t n=0;n<p.motive_deltas.size();++n){if(n)out<<',';const auto& m=p.motive_deltas[n];
            out<<"{\"motive\":"<<m.motive<<",\"tick_delta_bits\":"<<m.tick_delta_bits<<",\"stop_at_bits\":"<<m.stop_at_bits<<'}';}
        out<<"]}";
    }else out<<"null";
    out<<'}';
}
}
