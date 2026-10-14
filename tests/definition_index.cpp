#include "odh/definition_index.hpp"
#include <iostream>
namespace {
unsigned checks=0;
void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void rejected(F f){try{f();}catch(const odh::FormatError&){require(true,"reject budget");return;}require(false,"expected budget rejection");}
}
int main(){try{
    odh::ObjectDefinition definition{};definition.version=138;definition.guid=0xfedcba98;
    definition.master_id=42;definition.sub_index=-1;definition.is_multitile=true;
    std::vector<odh::DefinitionCandidate> source={{"one.far","x.iff",7,definition}};
    odh::DefinitionIndex index(source);
    require(index.guid_count()==1&&index.candidate_count()==1,"owned index counts");
    const auto* one=index.unique(definition.guid);
    require(one&&one->source=="one.far"&&one->asset=="x.iff"&&one->chunk_id==7,"explicit provenance and unsigned GUID");
    require(one->definition.is_multitile&&one->definition.sub_index==-1,"raw layout metadata preserved");
    source[0].asset="changed";source[0].definition.guid=1;
    require(index.unique(definition.guid)==one&&one->asset=="x.iff"&&!index.unique(1),"snapshot owns metadata and stable reference");
    source={{"one.far","x.iff",7,definition},{"two.far","y.iff",8,definition}};
    odh::DefinitionIndex duplicate(source);
    require(duplicate.guid_count()==1&&duplicate.candidate_count()==2&&!duplicate.unique(definition.guid),"duplicate GUID ambiguous instead of precedence guess");
    const auto* candidates=duplicate.candidates(definition.guid);
    require(candidates&&candidates->size()==2&&(*candidates)[0].source=="one.far"&&(*candidates)[1].source=="two.far","candidate order and provenance retained");
    require(!duplicate.candidates(0)&&!duplicate.unique(77),"missing distinct from ambiguous");
    odh::DefinitionIndexLimits limits;limits.candidates=2;limits.provenance_bytes=24;
    require(odh::DefinitionIndex(source,limits).candidate_count()==2,"exact count and byte budget");
    limits.candidates=1;rejected([&]{odh::DefinitionIndex failed(source,limits);});
    limits.candidates=2;limits.provenance_bytes=23;rejected([&]{odh::DefinitionIndex failed(source,limits);});
    limits.candidates=0;limits.provenance_bytes=0;
    require(odh::DefinitionIndex({},limits).guid_count()==0,"empty explicit resource set valid zero budget");
    definition.guid=0;odh::DefinitionIndex zero({{"","",0,definition}});
    require(zero.unique(0)&&zero.unique(0)->definition.guid==0,"explicit metadata zero GUID retained independently of objt sentinel rules");
    std::cout<<checks<<" definition index checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
