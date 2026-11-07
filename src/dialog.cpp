#include "odh/dialog.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include <algorithm>
#include <charconv>
#include <exception>
#include <limits>
#include <stdexcept>
namespace odh {
DialogOperands parse_dialog_operands(const std::array<std::uint8_t,8>& b) noexcept {
    return {b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7]};
}
std::string expand_dialog_text(std::string_view input,const DialogSubstitution& substitute){
    if(input.size()>4096)throw std::invalid_argument("dialog source text exceeds budget");
    static const std::vector<std::string_view> tokens={"DynamicStringLocal:","DynamicObjectName","CatalogLocal:","ObjectLocal:","FixedLocal:","TimeLocal:","NameLocal:","DateLocal:","Attribute:","JobOffer:","JobDesc:","MoneyXL:","TempXL:","Local:","Param:","Temp:","Job:","ListObject","Neighbor","Object","Me"};
    std::string out;
    for(std::size_t at=0;at<input.size();){
        if(input[at]!='$'){out.push_back(input[at++]);continue;}
        ++at;
        std::string_view token;for(auto t:tokens)if(input.substr(at,t.size())==t){token=t;break;}
        if(token.empty()){
            if(input.substr(at,2)=="\\n"){out.push_back('\n');at+=2;continue;}
            // Native TS1 replaces known prefixes only. Currency and other
            // literal dollars remain text; leave the following byte untouched
            // so $$Local, $$Temp and other doubled prefixes expand uniformly.
            out.push_back('$');continue;
        }
        at+=token.size();std::vector<std::uint16_t> indexes;
        if(token.back()==':'){
            // Original TS1 career/time commands consume at most two selectors.
            // A third colon suffix remains literal, including its leading zeros.
            const bool career=token=="JobOffer:"||token=="Job:"||token=="JobDesc:"||token=="TimeLocal:";
            const auto max=career?2U:(token=="DynamicStringLocal:"||token=="DateLocal:")?3U:1U;
            for(unsigned i=0;i<max;++i){
                const auto begin=at;while(at<input.size()&&input[at]>='0'&&input[at]<='9')++at;
                unsigned index=0;if(begin==at)throw std::invalid_argument("dialog substitution index missing");
                const auto parsed=std::from_chars(input.data()+begin,input.data()+at,index);
                if(parsed.ec!=std::errc{}||index>32767)throw std::invalid_argument("dialog substitution index exceeds source short");
                indexes.push_back(static_cast<std::uint16_t>(index));
                if(i+1==max||at==input.size()||input[at]!=':')break;
                ++at;
            }
        }
        const auto text=substitute?substitute(token,indexes):std::nullopt;
        if(!text)throw std::invalid_argument("dialog string substitution backend unavailable: "+std::string(token));
        if(out.size()>4096||text->size()>4096-out.size())throw std::invalid_argument("expanded dialog text exceeds budget");
        out+=*text;
    }
    if(out.size()>4096)throw std::invalid_argument("expanded dialog text exceeds budget");
    return out;
}
bool dialog_selector_type(std::uint8_t type) noexcept {
    return (type>=5&&type<=11) || type==13;
}
namespace {
bool text_valid(const std::string& s,std::size_t limit) {return s.size()<=limit&&s.find('\0')==std::string::npos;}
bool prompt_valid(const DialogPrompt& p) {
    for(const auto* s:{&p.title,&p.message,&p.icon_name,&p.yes,&p.no,&p.cancel})if(!text_valid(*s,4096))return false;
    if(p.choices.size()>4096)return false;
    std::size_t bytes=0;
    for(const auto& c:p.choices){if(!text_valid(c.label,4096))return false;bytes+=c.label.size();}
    return bytes<=1048576;
}
std::optional<std::int32_t> integer(std::string value) {
    const auto space=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'||c=='\v';};
    while(!value.empty()&&space(value.back()))value.pop_back();
    std::size_t first=0;while(first<value.size()&&space(value[first]))++first;
    if(first==value.size())return {};
    if(value[first]=='+'){++first;if(first<value.size()&&(value[first]=='+'||value[first]=='-'))return {};}
    if(first==value.size())return {};
    std::int32_t result=0;const auto r=std::from_chars(value.data()+first,value.data()+value.size(),result);
    if(r.ec!=std::errc{}||r.ptr!=value.data()+value.size())return {};
    return result;
}
// Response command caps1024 UTF16 units; rename subsequently caps32. UTF8
// input must encode Unicode scalar values. Never split a surrogate pair/UTF8.
std::optional<std::string> utf16_prefix(const std::string& text,std::size_t units) {
    std::size_t at=0,end=0,count=0;
    while(at<text.size()){
        const auto first=static_cast<unsigned char>(text[at]);std::uint32_t cp;std::size_t n;
        if(first<128){cp=first;n=1;}else if(first>=0xc2&&first<=0xdf){cp=first&31;n=2;}
        else if(first>=0xe0&&first<=0xef){cp=first&15;n=3;}else if(first>=0xf0&&first<=0xf4){cp=first&7;n=4;}else return {};
        if(at+n>text.size())return {};
        for(std::size_t i=1;i<n;++i){const auto c=static_cast<unsigned char>(text[at+i]);if((c&0xc0)!=0x80)return {};cp=(cp<<6)|(c&63);}
        if((n==2&&cp<128)||(n==3&&cp<2048)||(n==4&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||cp==0)return {};
        const auto cost=cp>0xffff?2U:1U;if(count+cost>units&&count<units&&cost==2)return {};if(count+cost<=units){end=at+n;count+=cost;}else break;
        at+=n;
    }
    return text.substr(0,end);
}
}
bool dialog_state_valid(const DialogState& s) noexcept {
    try {
        return (s.instruction.opcode==36||s.instruction.opcode==38||s.instruction.opcode==39)&&
            s.thread_owner.id>0&&s.thread_owner.generation&&s.instruction.operands[5]<=15&&!(s.instruction.operands[7]&1)&&s.ticket&&s.run_generation&&
            routine_key_valid(s.source.key)&&vm_context_references_valid(s.source.context)&&
            s.source.arguments.size()<=4096&&s.source.locals.size()<=4096&&prompt_valid(s.prompt)&&
            text_valid(s.input,4096)&&utf16_prefix(s.input,1024)==std::optional<std::string>(s.input)&&s.selected_button<=(s.instruction.operands[5]==1?1:s.instruction.operands[5]==2?2:0);
    }catch(...){return false;}
}
DialogController::DialogController(World& w,DialogServices services):world_(w),services_(std::move(services)){}
PrimitiveOutcome DialogController::primitive(const VmFrame& frame,const Instruction& ins,const Ts1Temps&,const VmDispatch& dispatch) {
    if(ins.opcode!=36&&ins.opcode!=38&&ins.opcode!=39)return {};
    const auto fail=[](std::string reason){return PrimitiveOutcome{PrimitiveDisposition::unsupported,"Dialog: "+std::move(reason)};};
    if(!dispatch.origin||!dispatch.ticket||!dispatch.origin->matches_active_query(frame,ins,dispatch))return fail("requires actual VM dispatch");
    const auto op=parse_dialog_operands(ins.operands);
    if(op.type>15)return fail("FSO-only or unknown dialog kind");
    if(pending_)return {PrimitiveDisposition::continue_next_tick,{}};
    if(dispatch.check_mode&&op.blocking())return fail("blocking dialog cannot outlive a synchronous check thread");
    if(!services_.prepare||!services_.show||(op.blocking()&&!services_.thread_owner))return fail("resource or presentation backend unavailable");
    const auto scope=ins.opcode==36?DialogStringScope::private_resource:ins.opcode==38?DialogStringScope::global_resource:DialogStringScope::semiglobal_resource;
    std::string diagnostic;
    try {
        const auto owner=services_.thread_owner?services_.thread_owner(*dispatch.origin):std::nullopt;
        if(op.blocking()&&(!owner||!world_.is_live_reference(*owner)))return fail("actual Thread owner unavailable");
        const auto owner_still_matches=[&]{
            if(!op.blocking())return true;
            const auto current=services_.thread_owner(*dispatch.origin);
            return current&&owner&&*current==*owner&&world_.is_live_reference(*owner);
        };
        auto prompt=services_.prepare(scope,frame,op,diagnostic);
        if(!prompt||!prompt_valid(*prompt))return fail(diagnostic.empty()?"invalid or missing prompt resource":diagnostic);
        // Source special selectors are genuine resource choices, not a numeric
        // form. Empty catalogs are valid and can only be cancelled.
        DialogState state{owner.value_or(WorldEntityReference{}),frame,ins,dispatch.ticket,dispatch.origin->run_generation(),std::move(*prompt),{},0};
        if(!dispatch.origin->matches_active_query(frame,ins,dispatch)||!owner_still_matches())return fail("source or Thread owner changed during resource preparation");
        if(!services_.show(state,op.blocking(),diagnostic))return fail(diagnostic.empty()?"presentation refused dialog":diagnostic);
        if(!dispatch.origin->matches_active_query(frame,ins,dispatch)||!owner_still_matches())return fail("source or Thread owner changed during display");
        if(!op.blocking())return {PrimitiveDisposition::goto_true,{}};
        pending_=std::move(state);vm_=const_cast<Vm*>(dispatch.origin);
        return {PrimitiveDisposition::wait,{}};
    } catch(const std::exception& e){return fail(std::string(e.what()).substr(0,512));}
}
bool dialog_matches_vm_snapshot(const DialogState& s,const VmSnapshot& vm) noexcept {
    if(vm.run_generation!=s.run_generation||vm.status!=VmStatus::waiting||!vm.pending||vm.stack.empty())return false;
    const auto& p=*vm.pending;
    return p.ticket==s.ticket&&vm_context_equal(p.context,s.source.context)&&
        p.instruction.key.id==s.source.key.id&&p.instruction.key.scope==s.source.key.scope&&
        p.instruction.pc==s.source.pc&&p.instruction.code_owner==s.source.context.code_owner&&
        p.instruction.instruction.opcode==s.instruction.opcode&&p.instruction.instruction.operands==s.instruction.operands&&
        p.instruction.instruction.on_true==s.instruction.on_true&&p.instruction.instruction.on_false==s.instruction.on_false&&
        vm_frame_equal(vm.stack.back(),s.source);
}
bool DialogController::source_matches() const {
    if(!pending_||!vm_||!world_.is_live_reference(pending_->thread_owner)||vm_->run_generation()!=pending_->run_generation||vm_->status()!=VmStatus::waiting||!vm_->pending())return false;
    const auto owner=services_.thread_owner?services_.thread_owner(*vm_):std::nullopt;
    if(!owner||*owner!=pending_->thread_owner)return false;
    const auto& p=*vm_->pending();const auto& s=*pending_;
    return p.ticket==s.ticket&&vm_context_equal(p.context,s.source.context)&&
        p.instruction.key.id==s.source.key.id&&p.instruction.key.scope==s.source.key.scope&&
        p.instruction.pc==s.source.pc&&p.instruction.code_owner==s.source.context.code_owner&&
        p.instruction.instruction.opcode==s.instruction.opcode&&p.instruction.instruction.operands==s.instruction.operands&&
        p.instruction.instruction.on_true==s.instruction.on_true&&p.instruction.instruction.on_false==s.instruction.on_false&&
        !vm_->stack().empty()&&vm_frame_equal(vm_->stack().back(),s.source);
}
bool DialogController::respond(std::uint64_t ticket,const DialogResponse& response,std::string& diagnostic) {
    if(!pending_||ticket!=pending_->ticket||!source_matches()){diagnostic="dialog response has stale VM/frame/ticket";return false;}
    if(response.code>2||!text_valid(response.text,4096)){diagnostic="dialog response exceeds its bounded input contract";return false;}
    const auto clipped=utf16_prefix(response.text,1024);if(!clipped){diagnostic="dialog response is not valid UTF8";return false;}
    const auto op=parse_dialog_operands(pending_->instruction.operands);bool result=true;
    std::optional<std::int32_t> number;std::optional<std::int16_t> temp,phonebook_contact;
    const bool numeric=dialog_selector_type(op.type);
    if(op.type==1)result=response.code==0;
    else if(op.type==2&&response.code>0){result=false;temp=static_cast<std::int16_t>((response.code-1)%2);}
    else if(op.type==10){
        // TS1 analyzed program6e598: 004517da/00451843-5a. PhoneBook is
        // a signed-short result channel, not NumericEntry. Negative cancels
        // via FALSE without register writes; positive publishes only Temp0.
        // Zero is its internal pending state, never an invented contact.
        const auto selected=integer(*clipped);
        if(!selected||*selected==0||*selected< -32768||*selected>32767){
            diagnostic="phonebook response requires a nonzero signed-short result";return false;
        }
        if(*selected<0){
            if(response.code!=0){diagnostic="phonebook cancellation requires its cancel response code";return false;}
            result=false;
        }else{
            if(std::none_of(pending_->prompt.choices.begin(),pending_->prompt.choices.end(),[&](const auto& c){return c.value==*selected;})){
                diagnostic="dialog selection is not an offered catalog choice";return false;
            }
            phonebook_contact=static_cast<std::int16_t>(*selected);
        }
    }
    else if(numeric){
        if((op.type==6||op.type==9)&&response.code==0)result=false;
        else {
            number=integer(*clipped);result=number.has_value();
            if(number&&std::none_of(pending_->prompt.choices.begin(),pending_->prompt.choices.end(),[&](const auto& c){return c.value==*number;})){
                diagnostic="dialog selection is not an offered catalog choice";return false;
            }
        }
    }else if(op.type==3&&!clipped->empty()){
        const auto* object=world_.frame_entity(pending_->source,VmEntityRole::stack_object);
        const auto name=utf16_prefix(*clipped,32);
        if(!object||!name||!services_.rename){diagnostic="dialog text entry requires exact StackObject rename backend";return false;}
        if(!services_.rename({object->id,object->generation},*name,diagnostic))return false;
        if(!source_matches()){diagnostic="dialog source changed while renaming";return false;}
    }
    if(!vm_->complete(ticket,result)){diagnostic="dialog completion rejected its pending ticket";return false;}
    const auto index=(op.flags&32)?1U:0U;
    if(temp)vm_->temps()[index]=*temp;
    if(phonebook_contact)vm_->temps()[0]=*phonebook_contact;
    if(number){if(op.flags&16)vm_->temp_xl()[index]=*number;else {
        const auto raw=static_cast<std::uint16_t>(*number);vm_->temps()[index]=static_cast<std::int16_t>(raw<32768?static_cast<int>(raw):static_cast<int>(raw)-65536);
    }}
    pending_.reset();vm_=nullptr;return true;
}
bool DialogController::update_draft(std::uint64_t ticket,const std::string& text,std::uint8_t button){
    if(!pending_||ticket!=pending_->ticket||!source_matches()||text.size()>4096)return false;
    const auto type=pending_->instruction.operands[5];
    if(button>(type==1?1:type==2?2:0))return false;
    const auto clean=utf16_prefix(text,1024);if(!clean||*clean!=text)return false;
    pending_->input=text;pending_->selected_button=button;return true;
}
bool DialogController::restore(const DialogState& state,Vm& vm,std::string& diagnostic) {
    if(pending_||!dialog_state_valid(state)){diagnostic="invalid or duplicate restored dialog";return false;}
    const auto owner=services_.thread_owner?services_.thread_owner(vm):std::nullopt;
    if(!owner||*owner!=state.thread_owner){diagnostic="restored dialog VM owner differs";return false;}
    pending_=state;vm_=&vm;
    if(!source_matches()){pending_.reset();vm_=nullptr;diagnostic="restored dialog does not match its actual VM wait";return false;}
    return true;
}
void DialogController::cancel_for(const Vm* vm) noexcept {if(vm_==vm){pending_.reset();vm_=nullptr;}}
VmPrimitive dialog_primitive(DialogController& controller){return [&controller](const auto& f,const auto& i,const auto& t,const auto& d){return controller.primitive(f,i,t,d);};}
namespace {
struct DialogWriter {
    std::vector<std::uint8_t> bytes;
    void value(std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)bytes.push_back(static_cast<std::uint8_t>(v>>(8*i)));}
    void text(const std::string& s){value(s.size(),4);bytes.insert(bytes.end(),s.begin(),s.end());}
};
struct DialogReader {
    const std::vector<std::uint8_t>& bytes;std::size_t at=0;
    std::uint64_t value(unsigned n){if(n>bytes.size()-at)throw FormatError("truncated dialog state");std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=static_cast<std::uint64_t>(bytes[at++])<<(8*i);return v;}
    std::string text(std::size_t limit=4096){const auto n=value(4);if(n>limit||n>bytes.size()-at)throw FormatError("dialog string budget exceeded");std::string s(bytes.begin()+at,bytes.begin()+at+n);at+=n;return s;}
};
}
std::vector<std::uint8_t> encode_dialog_state(const DialogState& state) {
    if(!dialog_state_valid(state))throw FormatError("invalid dialog snapshot");
    DialogWriter w;w.value(1,2);w.value(static_cast<std::uint16_t>(state.thread_owner.id),2);w.value(state.thread_owner.generation,8);
    VmSnapshot shell;shell.stack={state.source};shell.status=VmStatus::running;
    shell.run_generation=state.run_generation;shell.ticket_sequence=state.ticket;shell.action_tree_known=true;
    VmSnapshotCodecLimits limits;limits.max_bytes=131072;
    const auto frame=encode_vm_snapshot(shell,limits);w.value(frame.size(),4);w.bytes.insert(w.bytes.end(),frame.begin(),frame.end());
    w.value(state.instruction.opcode,2);w.value(state.instruction.on_true,1);w.value(state.instruction.on_false,1);
    for(auto b:state.instruction.operands)w.value(b,1);
    for(const auto* s:{&state.prompt.title,&state.prompt.message,&state.prompt.icon_name,&state.prompt.yes,&state.prompt.no,&state.prompt.cancel})w.text(*s);
    w.value(state.prompt.choices.size(),4);
    for(const auto& c:state.prompt.choices){w.text(c.label);w.value(static_cast<std::uint32_t>(c.value),4);}
    w.text(state.input);w.value(state.selected_button,1);
    if(w.bytes.size()>2*1024*1024)throw FormatError("dialog snapshot byte budget exceeded");
    return std::move(w.bytes);
}
DialogState decode_dialog_state(const std::vector<std::uint8_t>& bytes) {
    if(bytes.size()>2*1024*1024)throw FormatError("dialog snapshot byte budget exceeded");
    DialogReader r{bytes};if(r.value(2)!=1)throw FormatError("unsupported dialog snapshot version");
    DialogState s;s.thread_owner.id=static_cast<std::int16_t>(r.value(2));s.thread_owner.generation=r.value(8);
    const auto count=r.value(4);if(count>131072||count>bytes.size()-r.at)throw FormatError("dialog frame exceeds byte budget");
    std::vector<std::uint8_t> raw(bytes.begin()+r.at,bytes.begin()+r.at+count);r.at+=count;
    VmSnapshotCodecLimits limits;limits.max_bytes=131072;const auto shell=decode_vm_snapshot(raw,limits);
    if(shell.stack.size()!=1||shell.status!=VmStatus::running||shell.pending||shell.suspended_pending.size()||
       shell.interaction_return||shell.external_frame_return||shell.immediate_return||!shell.immediate_continuations.empty()||
       shell.steps||shell.advertisement_changes||!shell.diagnostic.empty()||
       std::any_of(shell.temps.begin(),shell.temps.end(),[](auto v){return v!=0;})||
       std::any_of(shell.temp_xl.begin(),shell.temp_xl.end(),[](auto v){return v!=0;}))throw FormatError("invalid dialog frame envelope");
    s.source=shell.stack.front();s.ticket=shell.ticket_sequence;s.run_generation=shell.run_generation;
    s.instruction.opcode=static_cast<std::uint16_t>(r.value(2));s.instruction.on_true=static_cast<std::uint8_t>(r.value(1));s.instruction.on_false=static_cast<std::uint8_t>(r.value(1));
    for(auto& b:s.instruction.operands)b=static_cast<std::uint8_t>(r.value(1));
    for(auto* text:{&s.prompt.title,&s.prompt.message,&s.prompt.icon_name,&s.prompt.yes,&s.prompt.no,&s.prompt.cancel})*text=r.text();
    const auto choices=r.value(4);if(choices>4096||choices>(bytes.size()-r.at)/8)throw FormatError("dialog choice budget exceeded");
    for(std::size_t i=0;i<choices;++i){auto label=r.text();const auto raw_value=r.value(4);const auto value=raw_value<=INT32_MAX?static_cast<std::int32_t>(raw_value):static_cast<std::int32_t>(static_cast<std::int64_t>(raw_value)-4294967296LL);s.prompt.choices.push_back({std::move(label),value});}
    s.input=r.text();s.selected_button=static_cast<std::uint8_t>(r.value(1));
    if(r.at!=bytes.size()||!dialog_state_valid(s)||encode_dialog_state(s)!=bytes)throw FormatError("invalid or noncanonical dialog snapshot");
    return s;
}

}
