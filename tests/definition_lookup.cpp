#include "../tools/definition_lookup.hpp"
#include <iostream>
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char* why){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error(why);}
odh::DefinitionCandidate candidate(std::string source,std::string asset,std::uint16_t chunk,std::uint32_t guid){
    odh::ObjectDefinition definition{};definition.guid=guid;
    return {std::move(source),std::move(asset),chunk,definition};
}
}
int main(){try{
    const odh_host::DefinitionCanonicalSource canonical=[](const std::string& source){
        if(source=="alias-a"||source=="alias-b")return std::string{"physical"};return source;
    };
    std::vector<odh::DefinitionCandidate> base{candidate("alias-a","",3,10),candidate("base.far","Object.iff",4,20)};
    auto part=candidate("alias-b","",5,11);part.definition.master_id=7;part.definition.sub_index=0;
    base[0].definition.master_id=7;base[0].definition.sub_index=-1;
    const std::vector<odh::DefinitionCandidate> supplemental{candidate("alias-b","",3,10),part,
        candidate("independent","Object.iff",4,20),candidate("base.far","Other.iff",4,30)};
    const auto merged=odh_host::combine_definition_lookup(base,supplemental,canonical);
    require(merged.size()==5&&merged[0].source=="alias-a"&&merged[0].definition.guid==10&&
        merged[1].definition.guid==20&&merged[2].definition.guid==11&&merged[2].source=="alias-a"&&merged[3].source=="independent"&&
        merged[4].definition.guid==30,"base order and supplemental first occurrence order preserved after physical deduplication");
    require(base.size()==2&&base[0].source=="alias-a"&&base[1].asset=="Object.iff"&&
        supplemental.size()==4&&supplemental[0].source=="alias-b"&&supplemental[1].definition.guid==11,
        "caller base catalogue and supplemental inputs remain untouched");
    odh::DefinitionIndex index(merged);
    require(!index.unique(20)&&index.candidates(20)->size()==2,
        "same GUID from different physical sources remains ambiguous without precedence");
    require(index.unique(11)&&index.unique_master(*index.unique(11))==index.unique(10),
        "first spelling lets part and master from different aliases resolve within same physical file");
    const auto reverse=odh_host::combine_definition_lookup({part},{base.front()},canonical);
    require(reverse.size()==2&&reverse[0].source=="alias-b"&&reverse[1].source=="alias-b"&&
        reverse[0].chunk_id==5&&reverse[1].chunk_id==3,
        "first original physical-source spelling stays stable independently of chunk/master order");
    odh::DefinitionIndex buy(base);
    require(buy.unique(20)&&!buy.unique(11)&&buy.candidate_count()==2,
        "base-only catalogue remains independent of supplemental ambiguity and characters");
    auto conflict=supplemental;conflict.front().definition.guid=99;
    rejects([&]{odh_host::combine_definition_lookup(base,conflict,canonical);},"same physical OBJD conflicting GUID accepted");
    require(base[0].source=="alias-a"&&base[0].definition.guid==10,"failed merge also preserves caller catalogue");
    const auto identity=[](const std::string& source){return source;};
    odh::DefinitionIndexLimits limits;limits.candidates=1;limits.provenance_bytes=3;
    const auto tiny=candidate("a","bb",1,1);
    require(odh_host::combine_definition_lookup({tiny},{tiny},identity,limits).size()==1,
        "duplicate consumes no extra candidate or provenance budget");
    limits.candidates=0;rejects([&]{odh_host::combine_definition_lookup({tiny},{},identity,limits);},"candidate budget ignored");
    limits.candidates=1;limits.provenance_bytes=2;
    rejects([&]{odh_host::combine_definition_lookup({tiny},{},identity,limits);},"provenance byte budget ignored");
    limits.candidates=2;limits.provenance_bytes=5;
    rejects([&]{odh_host::combine_definition_lookup({tiny},{candidate("b","bb",2,2)},identity,limits);},"aggregate byte budget ignored");
    std::vector<odh::DefinitionCandidate> scale;
    for(std::uint16_t i=0;i<8192;++i)scale.push_back(candidate("scale","",i,static_cast<std::uint32_t>(i)+1));
    require(odh_host::combine_definition_lookup(scale,{},identity).size()==8192,
        "default lookup admits the exact DefinitionIndex candidate cap");
    rejects([&]{odh_host::combine_definition_lookup(scale,{candidate("scale","",8192,8193)},identity);},
        "default lookup rejects the first candidate beyond DefinitionIndex cap");
    limits.candidates=0;limits.provenance_bytes=0;
    require(odh_host::combine_definition_lookup({},{},identity,limits).empty(),"empty merge fits zero budget");
    rejects([&]{odh_host::combine_definition_lookup(base,{},{});},"missing canonical callback accepted");
    rejects([&]{odh_host::combine_definition_lookup(base,{},[](const auto&){return std::string{};});},"empty physical source accepted");
    std::cout<<"definition lookup combination checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
