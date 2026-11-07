#include "odh/saved_code_compatibility.hpp"

#include <iostream>
#include <map>
#include <sstream>

namespace {
int checks=0;
void require(bool condition,const char* message) {
    ++checks;if(!condition) throw std::runtime_error(message);
}
odh::Bhav tree(std::uint32_t word,std::initializer_list<std::uint16_t> calls={},
               std::uint16_t version=0x8002) {
    odh::Bhav result{};result.version=version;
    for(unsigned i=0;i<4;++i) result.header[i+4]=static_cast<std::uint8_t>(word>>(8*i));
    // Deliberately different metadata: signatures use the raw DWORD, not this.
    result.metadata=odh::Bhav::Metadata{0,0,0,0xabcd};
    for(auto call:calls) result.instructions.push_back({call,0xfd,0xfe,{}});
    return result;
}
using Trees=std::map<std::uint16_t,odh::Bhav>;
odh::SavedCodeResolver resolver(const Trees& trees) {
    return [&trees](std::uint16_t id)->const odh::Bhav* {
        const auto found=trees.find(id);return found==trees.end()?nullptr:&found->second;
    };
}
void graph_matrix() {
    Trees trees{{4096,tree(0x12340002,{0x8100,8192,256,700,255,0x80ff})},
                {256,tree(0x00020005,{4096,8192,700})},
                {8192,tree(0xfffffff0,{256})},
                // Neither native opcode255 nor its flagged form are BHAV calls.
                {255,tree(0x55555555)}};
    auto result=odh::compute_saved_code_signature(4096,resolver(trees));
    require(result.state==odh::SavedCodeSignatureState::computed && result.value==0x1237fff7,
        "full raw DWORD closure, masked IDs, duplicate calls and cycles");
    require(result.visited_ids==4 && result.examined_instructions==10,
        "one visit per distinct ID including missing; every instruction examined");
    Trees carry{{4096,tree(0x0000ffff,{4097})},{4097,tree(1)}};
    require(odh::compute_saved_code_signature(4096,resolver(carry)).value==0,
        "low sum carry and final modulo32 overflow");
    Trees overflow{{4096,tree(0xffffffff)}};
    require(odh::compute_saved_code_signature(4096,resolver(overflow)).value==0xfffeffff,
        "root shift uses modulo32 and high bits participate in sum");
    const Trees empty;
    result=odh::compute_saved_code_signature(4096,resolver(empty));
    require(result.state==odh::SavedCodeSignatureState::computed && !result.value &&
            result.visited_ids==1 && !result.examined_instructions,"missing root pure calculation is zero");
    Trees unknown{{4096,tree(7,{4097})},{4097,tree(9,{},0x8003)}};
    result=odh::compute_saved_code_signature(4096,resolver(unknown));
    require(result.state==odh::SavedCodeSignatureState::unsupported_format && !result.value &&
            result.problem_id==4097 && result.problem_format==0x8003,
        "unsupported descendant differs from missing and publishes no partial signature");
    unknown[4096].version=0x7fff;
    require(odh::compute_saved_code_signature(4096,resolver(unknown)).state==
        odh::SavedCodeSignatureState::unsupported_format,"unknown root native format rejects");
    require(odh::compute_saved_code_signature(4096,{}).state==
        odh::SavedCodeSignatureState::missing_resolver,"missing resolver explicit");
    result=odh::compute_saved_code_signature(4096,resolver(trees),{3,100});
    require(result.state==odh::SavedCodeSignatureState::routine_limit && !result.value &&
            result.visited_ids==3,"distinct ID budget includes absent IDs");
    result=odh::compute_saved_code_signature(4096,resolver(trees),{100,2});
    require(result.state==odh::SavedCodeSignatureState::instruction_limit && !result.value &&
            result.examined_instructions==2,"instruction budget is exact");
    result=odh::compute_saved_code_signature(4096,resolver(trees),{0,0});
    require(result.state==odh::SavedCodeSignatureState::routine_limit && !result.visited_ids,
        "zero budget does not resolve roots");
}
void exact_context() {
    // A semiglobal calls a private ID: retain the original owner's context.
    Trees owner_a{{8192,tree(2,{4096,256})},{4096,tree(3)},{256,tree(5)}};
    Trees owner_b{{8192,tree(2,{4096,256})},{4096,tree(11)},{256,tree(5)}};
    require(odh::compute_saved_code_signature(8192,resolver(owner_a)).value==0x0002000a,
        "global/private/semiglobal IDs resolved within owner A");
    require(odh::compute_saved_code_signature(8192,resolver(owner_b)).value==0x00020012,
        "same root and global IDs retain distinct owner B private tree");
    // Resource aliases count by distinct ID, not by Bhav pointer or RoutineKey.
    const auto shared=tree(9,{4097});
    const auto alias=[&](std::uint16_t id)->const odh::Bhav* {
        return id==4096 || id==4097?&shared:nullptr;
    };
    require(odh::compute_saved_code_signature(4096,alias).value==0x00090012,
        "two distinct IDs referencing one resource both contribute");
}
void u16(std::string& s,std::uint16_t v) {
    s+=static_cast<char>(v);s+=static_cast<char>(v>>8);
}
void u32(std::string& s,std::uint32_t v) {
    for(unsigned i=0;i<4;++i)s+=static_cast<char>(v>>(8*i));
}
void parser_profiles() {
    for(const auto format:{0x8000,0x8001,0x8002}) {
        std::string bytes;u16(bytes,static_cast<std::uint16_t>(format));u16(bytes,1);
        u32(bytes,0x02010304);u32(bytes,0x12345678);
        u16(bytes,0x8100);bytes+=static_cast<char>(0xfd);bytes+=static_cast<char>(0xfe);
        bytes.append(8,'\0');
        std::istringstream stream(bytes,std::ios::binary);
        const auto parsed=odh::read_bhav(odh::View(stream,bytes.size()));
        Trees trees{{4096,parsed},{256,tree(0x00000002)}};
        const auto result=odh::compute_saved_code_signature(4096,resolver(trees));
        require(result.state==odh::SavedCodeSignatureState::computed && result.value==0x68ac567a,
            "actual native parser preserves full DWORD at payload8 and masked call");
        require(stream.tellg()==std::streampos(24),"parser consumes exact payload");
        bool rejected=false;
        try {
            std::istringstream short_stream(bytes.substr(0,bytes.size()-1),std::ios::binary);
            odh::read_bhav(odh::View(short_stream,bytes.size()-1));
        } catch(const odh::FormatError&) {rejected=true;}
        require(rejected,"truncated parser input does not manufacture missing routine");
    }
}
odh::ObjtEntry entry(std::uint32_t guid,std::uint32_t init,std::uint32_t main,
                     std::uint16_t raw_type=99) {
    return {guid,odh::ObjtMetadata{static_cast<std::uint16_t>(init),static_cast<std::uint16_t>(init>>16),
        static_cast<std::uint16_t>(main),static_cast<std::uint16_t>(main>>16),raw_type,4},"",{}, {}};
}
void comparisons() {
    const Trees trees{{4096,tree(1)},{4097,tree(2)}};
    const odh::SavedCodeResources resources{true,4096,4097,resolver(trees)};
    odh::Objt table{};table.version=2;
    table.entries={{0,{},"",{},{}},entry(17,0x00010001,0x00020002,1),
        entry(18,0x00010002,0x00020002,1),entry(19,0x00010001,0x00020003,1),
        entry(0x3c08965f,0x00010001,0x00020002,1)};
    auto compare=[&](std::uint16_t type,bool enabled=true) {
        return odh::compare_saved_code_type(table,type,resources,enabled);
    };
    require(compare(1).state==odh::SavedCodeTypeState::sentinel,"GUID0 stays in serialized type order");
    require(compare(0).state==odh::SavedCodeTypeState::invalid_type &&
        compare(6).state==odh::SavedCodeTypeState::invalid_type,"type0 and out of range explicit");
    auto result=compare(2);
    require(result.state==odh::SavedCodeTypeState::matched && !result.recovery_required &&
        result.saved.init==0x00010001 && result.current.main==0x00020002,
        "type-minus-one lookup ignores metadata type_id and compares full pairs");
    require(compare(3).state==odh::SavedCodeTypeState::mismatched && compare(3).recovery_required,
        "Init low-word mismatch triggers recovery despite identical high words");
    require(compare(4).state==odh::SavedCodeTypeState::mismatched,"Main low-word mismatch triggers");
    require(!compare(3,false).recovery_required && compare(3,false).state==odh::SavedCodeTypeState::mismatched,
        "global recovery control does not change comparison");
    require(compare(5).special_guid && compare(5).recovery_required &&
        compare(5).state==odh::SavedCodeTypeState::matched,"only exact special GUID resets matched code");
    require(!compare(2).special_guid,"ordinary type cannot infer special cursor from category");
    // Original 0x431edd: special reset bypasses the global control; the caller's
    // final Init requires the enabled mismatch independently of that reset.
    for(const bool special:{false,true}) {
        for(const bool mismatch:{false,true}) {
            for(const bool enabled:{false,true}) {
                odh::Objt matrix{};matrix.version=2;
                matrix.entries={entry(special?0x3c08965fU:17U,
                    mismatch?0x00010002U:0x00010001U,0x00020002)};
                const auto decision=odh::compare_saved_code_type(matrix,1,resources,enabled);
                const bool expected_init=enabled && mismatch;
                const bool expected_reset=special || expected_init;
                require(decision.state==(mismatch?odh::SavedCodeTypeState::mismatched:
                    odh::SavedCodeTypeState::matched),"truth matrix retains comparison independently of action authority");
                require(decision.recovery_required==expected_reset &&
                    decision.final_init_required==expected_init,
                    "original reset and final Init truth matrix for control, mismatch, exact special GUID");
            }
        }
    }
    table.entries[1].metadata->init_tree_version=2;
    require(compare(2).state==odh::SavedCodeTypeState::mismatched,"Init high-word mismatch triggers");
    for(const auto version:{0U,0x80000000U,0xffffffffU}) {
        table.version=version;
        require(compare(2).state==odh::SavedCodeTypeState::unsupported_objt_version &&
            !compare(2).recovery_required,"signed OBJT versions before1 never false-match");
    }
    table.version=1;
    require(compare(3).state==odh::SavedCodeTypeState::mismatched,"OBJT1 full comparison supported");
    table.entries[1].metadata.reset();
    require(compare(2).state==odh::SavedCodeTypeState::missing_metadata,"nonzero type needs metadata");
    require(odh::compare_saved_code_type(table,3,{}).state==
        odh::SavedCodeTypeState::unsupported_resources,"missing definition distinct unsupported");
    auto absent=resources;absent.init_root=5000;
    require(odh::compare_saved_code_type(table,3,absent).state==
        odh::SavedCodeTypeState::missing_required_root,"missing required root cannot false-match zero");
    absent.init_root=12;
    require(odh::compare_saved_code_type(table,3,absent).state==
        odh::SavedCodeTypeState::invalid_entry_root,"nonzero primitive ID is not BHAV entry");
    table.entries[1]=entry(17,0,0);
    const odh::SavedCodeResources no_trees{true,0,0,{}};
    require(odh::compare_saved_code_type(table,2,no_trees).state==odh::SavedCodeTypeState::matched,
        "explicit absent actions ID0 are valid zero without a resource lookup");
}
void preflight() {
    const Trees trees{{4096,tree(1,{255})},{4097,tree(2,{255})}};
    odh::Objt table{};table.version=2;
    table.entries={{0,{},"",{},{}},entry(17,0x10001,0x20002),entry(18,0x10001,0x20003),
        entry(19,0x10001,0x20002)};
    std::vector<std::uint16_t> resolved;
    const auto provide=[&](std::uint16_t type,std::uint32_t guid) {
        resolved.push_back(type);require(guid==std::uint32_t(type+15),"provider sees serialized type and GUID");
        return odh::SavedCodeResources{type!=3,4096,4097,resolver(trees)};
    };
    auto result=odh::preflight_saved_code_compatibility(table,provide);
    require(result.state==odh::SavedCodePreflightState::unsupported && result.types.size()==4 &&
        result.types[3].state==odh::SavedCodeTypeState::matched,
        "preflight diagnoses all types beyond unsupported row before effects");
    require(resolved==std::vector<std::uint16_t>({2,3,4}),"sentinel skipped only for resolution, never indexing");
    require(result.examined_instructions==4 && result.visited_ids==4,
        "aggregate cost counts actual work across pairs");
    auto all=[&](std::uint16_t,std::uint32_t) {return odh::SavedCodeResources{true,4096,4097,resolver(trees)};};
    result=odh::preflight_saved_code_compatibility(table,all);
    require(result.state==odh::SavedCodePreflightState::complete && result.mismatched_types==1 &&
        result.recovery_types==1,"complete all-type preflight retains mismatch count");
    odh::SavedCodePreflightLimits limits{};limits.types=3;
    result=odh::preflight_saved_code_compatibility(table,all,true,limits);
    require(result.state==odh::SavedCodePreflightState::type_limit && result.types.empty(),
        "table budget checked before publication");
    limits.types=4;limits.instructions=3;
    result=odh::preflight_saved_code_compatibility(table,all,true,limits);
    require(result.state==odh::SavedCodePreflightState::work_limit && result.examined_instructions==3 &&
        result.types.back().state==odh::SavedCodeTypeState::unsupported_signature &&
        !result.types.back().recovery_required,"aggregate instruction limit shared by both roots and all types");
    limits.instructions=100;limits.routine_ids=3;
    result=odh::preflight_saved_code_compatibility(table,all,true,limits);
    require(result.state==odh::SavedCodePreflightState::work_limit && result.visited_ids==3,
        "aggregate ID limit cannot be doubled by pair computation");
}
}
int main() {
    try {
        graph_matrix();exact_context();parser_profiles();comparisons();preflight();
        std::cout<<"saved_code_compatibility: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"saved_code_compatibility: "<<error.what()<<'\n';return 1;
    }
}
