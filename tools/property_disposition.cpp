#include "saved_resources.hpp"
#include "odh/property_disposition.hpp"
#include <array>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
// Legacy OBJT names are arbitrary bytes, not necessarily UTF-8. Preserve
// non-ASCII/control bytes without emitting malformed text to a log reader.
std::string escaped_name(const std::string& bytes) {
    std::ostringstream result;
    for(const unsigned char byte:bytes) {
        if(byte<32||byte>=127)result<<"\\x"<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned>(byte);
        else result<<static_cast<char>(byte);
    }
    return result.str();
}
}

int main(int argc,char** argv) {
    try {
        if(argc<2)throw odh::FormatError("usage: odh-property-disposition HOUSE_IFF [--object-definitions FAR | --object-definition-file IFF]...");
        std::vector<SavedDefinitionSource> sources;
        for(int n=2;n<argc;++n) {
            const std::string option=argv[n];
            if((option!="--object-definitions"&&option!="--object-definition-file")||n+1>=argc)
                throw odh::FormatError("invalid property disposition option");
            if(sources.size()>=128)throw odh::FormatError("definition source count exceeds limit");
            sources.push_back({argv[++n],option=="--object-definition-file"});
        }
        const auto definitions=saved_definition_index(sources);
        std::size_t unresolved=0;
        with_iff_file(argv[1],[&](const odh::View& view) {
            std::optional<odh::Objm> objects;std::optional<odh::Objt> types;
            odh::iff_chunks(view,[&](const odh::Chunk& chunk) {
                if(chunk.type=="ObjM"&&chunk.id==1) {
                    if(objects)throw odh::FormatError("duplicate ObjM1");
                    objects=odh::read_objm(chunk.payload);
                }
                if(chunk.type=="objt"&&chunk.id==0) {
                    if(types)throw odh::FormatError("duplicate objt0");
                    types=odh::read_objt(chunk.payload);
                }
            });
            if(!objects||!types)throw odh::FormatError("ObjM1/objt0 absent");
            const auto records=odh::read_saved_object_prefixes(*objects,*types);
            std::size_t retained=0,removed=0;
            std::cout<<"inspection_only=1 eviction_authorized=0 source=legacy_ObjM_prefix_ObjectData43"
                <<" FSOV_inspected=0 family_checkpoint_inspected=0 live_state_inspected=0\n";
            for(const auto& record:records) {
                const auto* type=types->entry_for_type(record.type_id);
                if(!type||!type->metadata)throw odh::FormatError("saved object type metadata absent");
                const auto flags=static_cast<std::uint16_t>(record.prefix.object_data[43]);
                const auto result=odh::inspect_property_disposition(*definitions,record.guid,type->metadata->objd_type,flags);
                if(result.disposition==odh::PropertyDisposition::unresolved)++unresolved;
                else if(result.disposition==odh::PropertyDisposition::retain)++retained;
                else ++removed;
                std::cout<<"id="<<record.object_id<<" guid="<<record.guid<<" type="<<type->metadata->objd_type
                    <<" movement_flags="<<flags<<" disposition="<<odh::property_disposition_name(result.disposition)
                    <<" reason="<<odh::property_disposition_reason_name(result.reason);
                if(result.effective_guid)std::cout<<" effective_guid="<<*result.effective_guid;
                if(result.effective_build_mode)std::cout<<" build_mode="<<*result.effective_build_mode;
                std::cout<<" name_bytes="<<std::quoted(escaped_name(type->name))<<'\n';
            }
            std::cout<<"objects="<<records.size()<<" retain="<<retained<<" remove="<<removed<<" unresolved="<<unresolved<<'\n';
        });
        return unresolved?3:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
