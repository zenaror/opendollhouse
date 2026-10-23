#include "odh/lot_context.hpp"
#include <charconv>
#include <limits>

namespace odh {
LotZoning read_lot_zoning(const StringTable& table) {
    if(table.state!=StringTableState::decoded)throw FormatError("lot zoning requires decoded STR#1");
    LotZoning result;
    for(const auto& entry:table.entries) {
        if(entry.language!=0&&entry.language!=1)continue;
        const auto& text=entry.value;const auto comma=text.find(',');
        if(comma==std::string::npos)throw FormatError("lot zoning entry has no comma");
        int id=0;const auto parsed=std::from_chars(text.data(),text.data()+comma,id);
        if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+comma||
           id<std::numeric_limits<std::int16_t>::min()||id>std::numeric_limits<std::int16_t>::max())
            throw FormatError("lot zoning ID outside signed16 profile");
        const auto label=text.substr(comma+1);
        if(label!=" residential"&&label!=" community")throw FormatError("unsupported lot zoning label");
        if(result.size()>=4096)throw FormatError("lot zoning exceeds bounded4096 profile");
        if(!result.emplace(static_cast<std::int16_t>(id),label==" community"?1:0).second)
            throw FormatError("ambiguous lot zoning ID");
    }
    if(result.empty())throw FormatError("lot zoning US language group absent or empty");
    return result;
}
LotContext::LotContext(std::int16_t selected_lot,const std::vector<std::int16_t>& serialized,
                       std::optional<LotZoning> zoning):zoning_(std::move(zoning)) {
    if(selected_lot<=0)throw FormatError("selected lot must be positive signed16");
    if(serialized.size()!=32&&serialized.size()!=64)throw FormatError("lot globals require serialized32/64 profile");
    if(zoning_&&zoning_->size()>4096)throw FormatError("lot zoning exceeds bounded4096 profile");
    if(zoning_)for(const auto& entry:*zoning_)if(entry.second!=0&&entry.second!=1)
        throw FormatError("loaded lot zoning value outside0/1 profile");
    for(std::size_t n=0;n<globals_.size()&&n<serialized.size();++n) {
        // VM.GetGlobalValue derives these from a live clock. The others below
        // are overwritten by VMTS1Activator; saved entity IDs are not World IDs.
        if(n==0||n==1||n==4||n==5||n==6||n==7||n==8||n==3||n==9||n==17||n==20||n==25||n==32||n==33)continue;
        globals_[n]=serialized[n];
    }
    globals_[10]=selected_lot;
}
std::optional<std::int16_t> LotContext::read(std::int16_t index) const {
    return index>=0&&static_cast<std::size_t>(index)<globals_.size()?globals_[static_cast<std::size_t>(index)]:std::nullopt;
}
bool LotContext::write(std::int16_t index,std::int16_t value) {
    if(!read(index))return false;
    globals_[static_cast<std::size_t>(index)]=value;return true;
}
LotContext::Snapshot LotContext::snapshot() const { return globals_; }
void LotContext::restore(const Snapshot& snapshot) { globals_=snapshot; }
std::optional<LotZoneResult> LotContext::zone(std::int16_t input) const {
    if(!zoning_)return {};
    const auto found=zoning_->find(input);
    if(found!=zoning_->end())return LotZoneResult{input,found->second,false};
    return LotZoneResult{input,static_cast<std::int16_t>(input>=81&&input<=89?2:1),true};
}
VmPrimitive LotContext::primitive(std::function<void(const LotZoneResult&)> observed) const {
    return [this,observed=std::move(observed)](const VmFrame&,const Instruction& i,const auto& temps,const VmDispatch&)->PrimitiveOutcome {
        if(i.opcode!=1||i.operands[0]!=28)return {};
        const auto result=zone(temps[0]);
        if(!result)return {PrimitiveDisposition::unsupported,"lot zoning resource unavailable"};
        if(observed)observed(*result);
        PrimitiveOutcome outcome{PrimitiveDisposition::goto_true,{}};
        outcome.temp_write=VmTempWrite{0,result->value};return outcome;
    };
}
}
