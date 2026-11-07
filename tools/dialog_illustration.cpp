// SPDX-License-Identifier: MPL-2.0
#include "dialog_illustration.hpp"
#include "resource.hpp"
#include <algorithm>
#include <sstream>

namespace odh_host {
namespace {
DialogIllustrationPlan failure(DialogIllustrationKind kind,std::string reason){
    if(reason.size()>512)reason.resize(512);
    DialogIllustrationPlan plan;plan.kind=kind;plan.diagnostic=std::move(reason);return plan;
}
bool valid_name(const std::string& value,bool empty){
    return (empty||!value.empty())&&value.size()<=4096&&value.find('\0')==std::string::npos;
}
}
DialogIllustrationPlan plan_dialog_illustration(const odh::DialogState& state,const odh::World& world,
        const DialogIllustrationSource& source){
    using Kind=DialogIllustrationKind;
    if(state.instruction.opcode!=36&&state.instruction.opcode!=38&&state.instruction.opcode!=39)
        return failure(Kind::unsupported,"illustration requires an actual private/global/semiglobal Dialog instruction");
    const auto operand=odh::parse_dialog_operands(state.instruction.operands);
    const auto mode=(operand.flags>>1)&7;
    if(mode==1)return {};
    if(mode==3){DialogIllustrationPlan plan;plan.kind=Kind::indexed_bitmap;
        plan.bitmap_id=static_cast<std::uint16_t>(5000u+operand.icon);return plan;}
    if(mode==0){
        if(!source.automatic_known)return failure(Kind::unsupported,"automatic dialog illustration subject is not authenticated");
        if(source.automatic_entity&&source.automatic_neighbor_id)
            return failure(Kind::unsupported,"automatic dialog illustration subject has conflicting entity/neighbor identities");
        if(source.automatic_neighbor_id){
            if(*source.automatic_neighbor_id<=0)return failure(Kind::unavailable,"automatic dialog neighbor illustration identity is invalid");
            DialogIllustrationPlan plan;plan.kind=Kind::neighbor;plan.neighbor_id=source.automatic_neighbor_id;return plan;
        }
        if(!source.automatic_entity)return {};
        const auto identity=*source.automatic_entity;
        if(identity.id<=0||!identity.generation||!world.is_live_reference(identity))
            return failure(Kind::unavailable,"automatic dialog illustration subject is stale or absent");
        DialogIllustrationPlan plan;plan.kind=Kind::entity;plan.entity=identity;return plan;
    }
    if(mode==2){
        if(!source.neighbor_known)return failure(Kind::unsupported,"dialog neighbor illustration identity is not authenticated");
        if(!source.neighbor_id)return {};
        if(*source.neighbor_id<=0)return failure(Kind::unavailable,"dialog neighbor illustration identity is invalid");
        DialogIllustrationPlan plan;plan.kind=Kind::neighbor;plan.neighbor_id=source.neighbor_id;return plan;
    }
    return failure(Kind::unsupported,mode==4?"named dialog illustration lookup is not implemented":
        "dialog illustration mode is not supported");
}
PreparedDialogIllustration prepare_dialog_illustration(const odh::DialogState& state,const odh::World& world,
        const DialogIllustrationSource& source,const std::optional<DialogIllustrationResource>& resource,
        const DialogIllustrationLimits& limits){
    PreparedDialogIllustration result;result.plan=plan_dialog_illustration(state,world,source);
    if(result.plan.kind!=DialogIllustrationKind::indexed_bitmap)return result;
    if(!resource){result.plan=failure(DialogIllustrationKind::unavailable,"exact dialog illustration IFF is unavailable");return result;}
    try{
        if(!valid_name(resource->archive.string(),false)||!valid_name(resource->entry,true))
            throw odh::FormatError("dialog illustration resource identity is invalid");
        const auto load=[&](const odh::View& iff){
            if(iff.size()>limits.iff_bytes)throw odh::FormatError("dialog illustration IFF exceeds configured extent");
            std::optional<odh::View> bitmap;
            odh::iff_chunks(iff,[&](const odh::Chunk& chunk){
                if(chunk.type=="BMP_"&&chunk.id==*result.plan.bitmap_id){
                    if(bitmap)throw odh::FormatError("duplicate exact dialog illustration BMP_ chunk");
                    bitmap=chunk.payload;
                }
            },limits.containers);
            if(!bitmap)return;
            result.image=odh::read_bmp(*bitmap,limits.bitmap,odh::BmpExtentPolicy::iff_container);
        };
        if(resource->frozen){
            if(resource->frozen->size()>limits.iff_bytes)
                throw odh::FormatError("frozen dialog illustration IFF exceeds configured extent");
            std::istringstream input(*resource->frozen,std::ios::in|std::ios::binary);
            load(odh::View(input,resource->frozen->size()));
        }
        else if(resource->entry.empty())with_iff_file(resource->archive.string(),load);
        else with_far_entry(resource->archive.string(),resource->entry,load,limits.containers);
        if(!result.image)result.plan=failure(DialogIllustrationKind::unavailable,"exact indexed dialog illustration BMP_ is absent");
    }catch(const std::exception& e){result.image.reset();result.plan=failure(DialogIllustrationKind::unavailable,e.what());}
    return result;
}
}
