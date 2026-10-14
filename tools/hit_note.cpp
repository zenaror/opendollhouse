#include "odh/hit_selection.hpp"
#include "json.hpp"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace {
template<class F>auto read_file(const std::string& path,F read){
    std::ifstream stream(path,std::ios::binary);if(!stream)throw odh::FormatError("cannot open HIT resource");
    return read(odh::View(stream,std::filesystem::file_size(path)));
}
template<class T>T number(const std::string& text){
    T value{};const auto r=std::from_chars(text.data(),text.data()+text.size(),value);
    if(r.ec!=std::errc{}||r.ptr!=text.data()+text.size())throw odh::FormatError("invalid decimal HIT fixture value");
    return value;
}
std::string lower(std::string text){for(auto& c:text)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);return text;}
}
int main(int argc,char** argv){try{
    if(argc<5)throw odh::FormatError("usage: odh-hit-note HSM HOT HIT EVENT [--field INDEX INT32] [--field-source INT32] [--choice INDEX] [--jump-operand free-so|literal32]...");
    auto jump_operand=odh::HitJumpOperand::FreeSoHeuristic;bool jump_seen=false;
    std::optional<std::int32_t> field_source;
    std::array<std::int32_t,29> fields{};std::array<bool,29> seen{};
    std::vector<std::size_t> choices;
    for(int i=5;i<argc;++i){const std::string option=argv[i];
        if(option=="--field"&&i+2<argc){
            const auto index=number<std::uint32_t>(argv[++i]);
            if(index>=fields.size()||seen[index])throw odh::FormatError("invalid or repeated HIT object field");
            seen[index]=true;fields[index]=number<std::int32_t>(argv[++i]);
        }else if(option=="--choice"&&i+1<argc){
            if(choices.size()>=256)throw odh::FormatError("HIT choice fixture count exceeds budget");
            choices.push_back(number<std::size_t>(argv[++i]));
        }else if(option=="--field-source"&&i+1<argc&&!field_source){
            field_source=number<std::int32_t>(argv[++i]);
        }else if(option=="--jump-operand"&&i+1<argc&&!jump_seen){
            jump_seen=true;const std::string value=argv[++i];
            if(value=="literal32")jump_operand=odh::HitJumpOperand::Literal32;
            else if(value!="free-so")throw odh::FormatError("invalid HIT Jump operand profile");
        }else throw odh::FormatError("invalid HIT note option");
    }
    const auto hsm=read_file(argv[1],[](const auto& view){return odh::read_hsm(view);});
    const auto hot=read_file(argv[2],[&](const auto& view){return odh::read_hot(view,&hsm);});
    const auto hit=read_file(argv[3],[](const auto& view){return odh::read_hit(view);});
    const auto name=lower(argv[4]);const auto event=hot.events.find(name);
    if(event==hot.events.end()||!event->second.event_type||!event->second.track_id||
       *event->second.event_type!=1||!event->second.unsupported_reason.empty())throw odh::FormatError("event is missing or outside play-note profile");
    const auto track=hot.tracks.find(*event->second.track_id);
    if(track==hot.tracks.end()||!track->second.subroutine_offset||!track->second.unsupported_reason.empty())
        throw odh::FormatError("event track has no supported HIT bytecode");
    std::size_t next_choice=0;std::vector<odh::HitChoice> choice_records;std::optional<odh::HitNotePlan> plan;std::string diagnostic;
    odh::HitSourceFieldReader source_reader;
    if(field_source) source_reader=[&](std::int32_t source,std::int32_t field)->std::optional<std::int32_t>{
        if(source!=*field_source || field<0 || static_cast<std::uint32_t>(field)>=fields.size() || !seen[static_cast<std::size_t>(field)])return std::nullopt;
        return fields[static_cast<std::size_t>(field)];
    };
    try{
        plan=odh::select_hit_note(hit,hot,*track->second.subroutine_offset,fields,
            [&](std::uint32_t list,const auto& ids)->std::size_t{
                if(next_choice>=choices.size())throw odh::FormatError("explicit HIT choice sequence exhausted");
                const auto index=choices[next_choice++];
                if(index<ids.size())choice_records.push_back({list,ids[index],index});
                return index;
            },{},&track->second,jump_operand,source_reader);
        if(next_choice!=choices.size()){plan.reset();diagnostic="unused explicit HIT choices";}
    }catch(const odh::FormatError& e){diagnostic=e.what();}
    std::cout<<"{\"evidence\":"<<json_bytes(field_source?"offline_HIT_selection_with_explicit_source_fields":jump_operand==odh::HitJumpOperand::Literal32?
            "offline_FreeSO_HIT_selection_with_literal32_Jump":"offline_FreeSO_HIT_note_selection_profile")
        <<",\"jump_operand\":"
        <<json_bytes(jump_operand==odh::HitJumpOperand::Literal32?"literal32":"free-so")
        <<",\"field_source\":"<<(field_source?std::to_string(*field_source):"null")
        <<",\"audio_submitted\":false,\"event\":"<<json_bytes(name)
        <<",\"track\":"<<*event->second.track_id<<",\"start_pc\":"<<*track->second.subroutine_offset
        <<",\"resources\":{\"hsm_records\":"<<hsm.records<<",\"hsm_duplicates\":"<<hsm.duplicate_names
        <<",\"hot_records\":"<<hot.records<<",\"hot_unsupported_records\":"<<hot.unsupported_records
        <<",\"hit_bytes\":"<<hit.bytes.size()<<",\"hit_minor\":"<<hit.minor_version<<"}"
        <<",\"object_vars_fixture\":[";
    for(std::size_t i=0;i<fields.size();++i){if(i)std::cout<<',';std::cout<<fields[i];}
    std::cout<<"],\"choices_used\":"<<next_choice<<",\"choices\":[";
    for(std::size_t i=0;i<choice_records.size();++i){if(i)std::cout<<',';const auto& choice=choice_records[i];
        std::cout<<"{\"list\":"<<choice.hitlist_id<<",\"index\":"<<choice.index<<",\"patch\":"<<choice.patch_id<<'}';}
    std::cout<<"],\"diagnostic\":"<<json_bytes(diagnostic)<<",\"note\":";
    if(plan){const auto patch=hot.patches.find(plan->patch_id);
        std::cout<<"{\"patch_id\":"<<plan->patch_id<<",\"filename\":"<<json_bytes(patch->second.filename)
            <<",\"pc\":"<<plan->pc<<",\"steps\":"<<plan->steps<<",\"destination\":"<<static_cast<unsigned>(plan->note_destination)
            <<",\"requested_group_kills\":[";
        for(std::size_t i=0;i<plan->sequence_group_kills.size();++i){if(i)std::cout<<',';std::cout<<static_cast<unsigned>(plan->sequence_group_kills[i]);}
        std::cout<<"],\"trace\":[";
        for(std::size_t i=0;i<plan->trace.size();++i){if(i)std::cout<<',';const auto& t=plan->trace[i];
            std::cout<<"{\"pc\":"<<t.pc<<",\"next_pc\":"<<t.next_pc<<",\"opcode\":"<<static_cast<unsigned>(t.opcode)
                <<",\"zero\":"<<(t.zero_flag?"true":"false")<<",\"sign\":"<<(t.sign_flag?"true":"false");
            if(t.field_index)std::cout<<",\"source\":"<<*t.source_object_value<<",\"field\":"<<*t.field_index<<",\"field_value\":"<<*t.field_value;
            if(t.choice)std::cout<<",\"choice_list\":"<<t.choice->hitlist_id<<",\"choice_index\":"<<t.choice->index<<",\"choice_patch\":"<<t.choice->patch_id;
            if(t.branch_id)std::cout<<",\"branch_id\":"<<*t.branch_id<<",\"branch_taken\":"<<(*t.branch_taken?"true":"false");
            std::cout<<'}';
        }std::cout<<"]}";
    }else std::cout<<"null";
    std::cout<<"}\n";return plan?0:3;
}catch(const std::exception& e){std::cerr<<"odh-hit-note: "<<e.what()<<'\n';return 2;}}
