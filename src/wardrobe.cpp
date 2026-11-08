// SPDX-License-Identifier: MPL-2.0
#include "odh/wardrobe.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>
namespace odh {
namespace {
constexpr double source_score_epsilon=9.999999747378752e-05;
bool nonzero(const WardrobeDigest& d){return std::any_of(d.begin(),d.end(),[](auto c){return c!=0;});}
void text(const std::string& value,std::size_t limit,bool empty=true){
    if(value.size()>limit||(!empty&&value.empty())||value.find('\0')!=std::string::npos)throw FormatError("wardrobe string extent/NUL");
}
std::string lower(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return value;}
void locator(const std::string& path,bool empty){
    text(path,wardrobe_max_locator_bytes,empty);if(path.empty())return;
    if(path.front()=='/'||path.front()=='\\'||path.find(':')!=std::string::npos)throw FormatError("wardrobe locator is not portable relative provenance");
    std::size_t start=0;for(std::size_t n=0;n<=path.size();++n){
        if(n<path.size()&&static_cast<unsigned char>(path[n])<32)throw FormatError("wardrobe locator control byte");
        if(n==path.size()||path[n]=='/'||path[n]=='\\'){
            const auto part=path.substr(start,n-start);if(part.empty()||part=="."||part=="..")throw FormatError("wardrobe locator ambiguous component");start=n+1;
        }
    }
}
void asset(const WardrobeCatalogAsset& a){
    if(a.kind!=WardrobeAssetKind::appearance&&a.kind!=WardrobeAssetKind::texture)throw FormatError("wardrobe asset kind");
    text(a.name,wardrobe_max_name_bytes,false);
    if(a.name.find_first_of("/\\:")!=std::string::npos||a.name=="."||a.name=="..")throw FormatError("wardrobe logical name is not a member name");
    const auto folded=lower(a.name);for(const auto* suffix:{".cmx",".apr",".bmp",".tga"})
        if(folded.size()>=4&&folded.compare(folded.size()-4,4,suffix)==0)throw FormatError("wardrobe name must be the logical member/stem, not filename");
    locator(a.source,false);locator(a.asset,true);if(!nonzero(a.fingerprint))throw FormatError("wardrobe asset content identity absent");
}
void catalog(const std::vector<WardrobeCatalogAsset>& values){
    if(values.size()>wardrobe_max_catalog_entries)throw FormatError("wardrobe catalog entry budget");
    std::array<std::optional<std::uint64_t>,2> previous{};std::size_t bytes=0;
    std::set<std::tuple<WardrobeAssetKind,std::string,std::string,std::string>> identities;
    std::map<std::pair<std::string,std::string>,WardrobeDigest> content;
    for(const auto& a:values){asset(a);const auto index=static_cast<std::size_t>(a.kind)-1;
        if(previous[index]&&a.producer_ordinal<=*previous[index])throw FormatError("wardrobe provider order is not strictly increasing per kind");previous[index]=a.producer_ordinal;
        if(!identities.emplace(a.kind,a.source,a.asset,a.name).second)throw FormatError("wardrobe duplicate provider identity");
        const auto inserted=content.emplace(std::make_pair(a.source,a.asset),a.fingerprint);if(!inserted.second&&inserted.first->second!=a.fingerprint)throw FormatError("wardrobe provider conflicts on source content");
        const auto added=a.name.size()+a.source.size()+a.asset.size();if(added>wardrobe_max_catalog_bytes-bytes)throw FormatError("wardrobe catalog byte budget");bytes+=added;
    }
}
std::optional<std::string> at(const OriginalWardrobeSource& source,std::size_t n){return n<source.strings.size()?std::optional<std::string>{source.strings[n]}:std::nullopt;}
void source_valid(const OriginalWardrobeSource& source){
    if(!source.body_str_id||source.strings.size()>wardrobe_max_source_entries)throw FormatError("wardrobe BodySTR profile/id/entry budget");
    std::size_t bytes=0;for(const auto& s:source.strings){text(s,wardrobe_max_string_bytes);if(s.size()>wardrobe_max_source_bytes-bytes)throw FormatError("wardrobe source byte budget");bytes+=s.size();}
}
std::int32_t source_atoi(const std::string& value){
    std::size_t n=0;while(n<value.size()&&(value[n]==' '||value[n]=='\t'||value[n]=='\n'||value[n]=='\r'||value[n]=='\f'||value[n]=='\v'))++n;
    bool negative=false;if(n<value.size()&&(value[n]=='+'||value[n]=='-'))negative=value[n++]=='-';
    std::uint64_t number=0;const auto limit=negative?std::uint64_t{2147483648ULL}:std::uint64_t{2147483647ULL};
    for(;n<value.size()&&value[n]>='0'&&value[n]<='9';++n){const auto d=static_cast<unsigned>(value[n]-'0');if(number>(limit-d)/10)throw FormatError("wardrobe original age atoi overflow unsupported");number=number*10+d;}
    return static_cast<std::int32_t>(negative?-static_cast<std::int64_t>(number):static_cast<std::int64_t>(number));
}
struct Query {std::string literal;std::size_t skip_at=0,skip=0;};
struct Plan {std::array<std::optional<std::string>,5> padded;std::array<std::optional<Query>,5> queries;};
Plan plan(const OriginalWardrobeProjection& value){
    Plan result;for(std::size_t n=0;n<5;++n)result.padded[n]=value.seed[n].value_or("");
    // 00587b7b pads the loaded table to35 BEFORE 00409377 admission. Raw
    // optional seed presence remains untouched in the persisted projection.
    const auto body=value.source_fields[0].value_or("");const auto gender=lower(value.source_fields[1].value_or(""));
    if(body.empty()||(gender!="male"&&gender!="female"))return result;
    const bool child=source_atoi(value.source_fields[2].value_or(""))<18;const std::string sex=gender=="male"?"m":"f";
    const bool group=result.padded[0]->empty(),high=result.padded[4]->empty(),winter=result.padded[3]->empty();
    if(!group&&!high&&!winter)return result; // Fully customized source needs no catalog or body-name parsing.
    auto first=lower(body.substr(0,body.find(';')));first=first.substr(0,first.find(','));
    if(first.size()<9)throw FormatError("wardrobe original body name lacks source type substring");
    auto type=first.substr(6,3);const auto adult_type=(type=="fit"||type=="fat"||type=="skn")?type:std::string("fit");
    if(group){
        if(child){result.padded[0]="ADDED";result.queries[1]=Query{"s100"+sex+"c"+type};result.queries[2]=Query{"l100"+sex+"c"+type};}
        else {result.queries[0]=Query{"f100"+sex+"a"+adult_type};result.queries[1]=Query{"s100"+sex+"a"+adult_type};result.queries[2]=Query{"l100"+sex+"a"+adult_type};}
    }
    if(high){if(child)result.padded[4]="ADDED";else result.queries[4]=Query{"h5"+sex+"a"+adult_type,2,2};}
    if(winter)result.queries[3]=Query{"w100"+sex+(child?"cchd":"a"+adult_type)};
    return result;
}
std::size_t matched(const std::string& name,const Query& query){
    const auto width=query.literal.size()+query.skip;if(width>name.size())return 0;
    std::size_t at=0;for(std::size_t n=0;n<query.literal.size();++n){if(query.skip&&n==query.skip_at)at+=query.skip;char c=name[at++];if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');if(c!=query.literal[n])return 0;}
    return width;
}
// The original new ratio stays in x87 precision, while the stored best ratio
// is binary32. Reconstruct that binary32 value with integer nearest-even,
// avoiding host long-double width or ambient float rounding mode. For bounded
// numerator/denominator<=256, binary64 division preserves every comparison
// against that binary32 boundary (see SOURCE-PROOF-COMPLETE.md).
double stored_ratio(std::size_t numerator,std::size_t denominator){
    int exponent=0;while((static_cast<std::uint64_t>(numerator)<<static_cast<unsigned>(-exponent))<denominator)--exponent;
    const auto scaled=static_cast<std::uint64_t>(numerator)<<static_cast<unsigned>(23-exponent);auto significant=scaled/denominator;const auto remainder=scaled%denominator;
    if(remainder*2>denominator||(remainder*2==denominator&&(significant&1)))++significant;
    if(significant==(std::uint64_t{1}<<24)){significant>>=1;++exponent;}
    return std::ldexp(static_cast<double>(significant),exponent-23);
}
const WardrobeCatalogAsset& select(const std::vector<WardrobeCatalogAsset>& values,WardrobeAssetKind kind,const Query& query){
    const WardrobeCatalogAsset* selected=nullptr;double stored=0;std::size_t length=0;
    for(const auto& a:values)if(a.kind==kind){const auto count=matched(a.name,query);if(!count)continue;
        const double ratio=static_cast<double>(count)/static_cast<double>(a.name.size());
        if((std::abs(ratio-stored)<=source_score_epsilon&&count>length)||stored<ratio){selected=&a;length=count;stored=stored_ratio(count,a.name.size());}
    }
    if(!selected)throw FormatError("wardrobe admitted catalog has no matching original query");return *selected;
}
std::string skin(const OriginalWardrobeProjection& p){const auto s=lower(p.source_fields[3].value_or("???"));return s=="lgt"||s=="med"||s=="drk"?s:std::string("lgt");}
bool same_asset(const WardrobeCatalogAsset& a,const WardrobeCatalogAsset& b){return a.kind==b.kind&&a.name==b.name&&a.source==b.source&&a.asset==b.asset&&a.fingerprint==b.fingerprint;}
OriginalWardrobeProjection seed(const OriginalWardrobeSource& source){source_valid(source);OriginalWardrobeProjection out;out.body_str_id=source.body_str_id;constexpr std::array<std::size_t,4> fields{{1,12,13,14}};for(std::size_t n=0;n<4;++n)out.source_fields[n]=at(source,fields[n]);for(std::size_t n=0;n<5;++n)out.seed[n]=at(source,30+n);return out;}
}
OriginalWardrobeProjection prepare_original_wardrobe_upgrade(const OriginalWardrobeSource& source,const std::vector<WardrobeCatalogAsset>& values){
    auto out=seed(source);const auto requested=plan(out);out.strings=requested.padded;
    const bool generate=std::any_of(requested.queries.begin(),requested.queries.end(),[](const auto& q){return q.has_value();});if(generate)catalog(values);
    for(std::size_t n=0;n<5;++n)if(requested.queries[n]){const auto& query=*requested.queries[n];auto texture_query=query;texture_query.literal+=skin(out);
        const auto& mesh=select(values,WardrobeAssetKind::appearance,query);const auto& texture=select(values,WardrobeAssetKind::texture,texture_query);
        WardrobeChoice choice{mesh.name+",BODY="+texture.name,mesh,texture};out.strings[n]=choice.literal;out.chosen[n]=std::move(choice);
    }
    validate_original_wardrobe_projection(out);return out;
}
void validate_original_wardrobe_projection(const OriginalWardrobeProjection& value){
    if(value.rule_profile!=1||!value.body_str_id)throw FormatError("wardrobe rule profile/BodySTR id unsupported");
    for(const auto& s:value.source_fields)if(s)text(*s,wardrobe_max_string_bytes);
    bool missing=false;for(const auto& s:value.seed){if(s){text(*s,wardrobe_max_string_bytes);if(missing)throw FormatError("wardrobe raw seed presence is not a serialized prefix");}else missing=true;}
    for(std::size_t n=1;n<4;++n)if(value.source_fields[n]&&!value.source_fields[n-1])throw FormatError("wardrobe source field presence is impossible");
    if(value.seed[0]&&!value.source_fields[3])throw FormatError("wardrobe raw seed lacks prior source fields");
    const auto expected=plan(value);
    for(std::size_t n=0;n<5;++n){if(!value.strings[n])throw FormatError("wardrobe padded runtime slot absent");text(*value.strings[n],wardrobe_max_string_bytes);
        if(!expected.queries[n]){if(value.chosen[n]||value.strings[n]!=expected.padded[n])throw FormatError("wardrobe unchanged/custom/ADDED slot altered without producer authority");continue;}
        if(!value.chosen[n])throw FormatError("wardrobe generated slot lacks selected resources");const auto& selected=*value.chosen[n];asset(selected.appearance);asset(selected.texture);
        if(selected.appearance.kind!=WardrobeAssetKind::appearance||selected.texture.kind!=WardrobeAssetKind::texture||!matched(selected.appearance.name,*expected.queries[n]))throw FormatError("wardrobe selected appearance does not match source query");
        auto texture_query=*expected.queries[n];texture_query.literal+=skin(value);
        if(!matched(selected.texture.name,texture_query)||selected.literal!=selected.appearance.name+",BODY="+selected.texture.name||value.strings[n]!=std::optional<std::string>{selected.literal})throw FormatError("wardrobe selected texture/literal does not match source query");
    }
}
void validate_original_wardrobe_source(const OriginalWardrobeProjection& value,const OriginalWardrobeSource& source){
    validate_original_wardrobe_projection(value);const auto pristine=seed(source);
    if(value.body_str_id!=pristine.body_str_id||value.source_fields!=pristine.source_fields||value.seed!=pristine.seed)throw FormatError("wardrobe immutable BodySTR source changed");
}
void validate_original_wardrobe_assets(const OriginalWardrobeProjection& value,const WardrobeAssetResolver& resolve){
    validate_original_wardrobe_projection(value);
    for(const auto& choice:value.chosen)if(choice)for(const auto* receipt:{&choice->appearance,&choice->texture}){
        if(!resolve)throw FormatError("wardrobe selected resource resolver absent");const auto current=resolve(*receipt);
        if(!current||!same_asset(*receipt,*current))throw FormatError("wardrobe selected resource missing or content/member changed");
    }
}
} // namespace odh
