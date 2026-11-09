// SPDX-License-Identifier: MPL-2.0
#include "odh/ts1_make_character.hpp"
#include <algorithm>
#include <set>
#include <utility>
namespace odh {
namespace {
std::string fold(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return s;}
bool ends(const std::string& s,const char* suffix){const std::string v(suffix);return s.size()>=v.size()&&s.compare(s.size()-v.size(),v.size(),v)==0;}
void bounded(const std::string& s,std::size_t limit,const char* field,bool empty=true){
    if(s.size()>limit||(!empty&&s.empty())||s.find('\0')!=std::string::npos)throw FormatError(std::string("Make19 invalid ")+field);
}
void input(const Ts1MakeCharacterInput& in){
    if(in.avatar_type>2||in.language<1||in.language>20)throw FormatError("Make19 avatar type/language unsupported");
    bounded(in.source_name,4096,"source name");
    if(in.source_body_strings.size()>wardrobe_max_source_entries)throw FormatError("Make19 source BodySTR entry budget");
    std::size_t bytes=0;std::set<std::uint16_t> seen;
    for(const auto& s:in.source_body_strings){bounded(s.bytes,wardrobe_max_string_bytes,"source BodySTR");
        if(!seen.insert(s.index).second||s.index>=wardrobe_max_source_entries||s.bytes.size()>wardrobe_max_source_bytes-bytes)throw FormatError("Make19 source BodySTR extent/duplicate");bytes+=s.bytes.size();}
    if(in.source_person_data.size()>256)throw FormatError("Make19 unused receiver personal bank exceeds original backing bound");
}
std::string prefix(const std::string& name){const auto p=name.find('_');return name.substr(0,p);}
std::uint64_t draw(const std::function<std::uint64_t(std::uint64_t)>& next,std::size_t count){
    if(!count||!next)throw FormatError("Make19 random selection lacks collection or RNG");
    const auto n=next(count);if(n>=count)throw FormatError("Make19 RNG result exceeds requested source bound");return n;
}
void put(TemplatePersonRequest& r,std::uint16_t n,std::string s){r.body_strings.push_back({n,std::move(s)});}
}
Ts1CharacterCollections::Ts1CharacterCollections(const std::vector<WardrobeCatalogAsset>& assets,
    const std::vector<std::uint32_t>& appearances,const std::vector<std::uint32_t>& textures):assets_(&assets){
    if(assets.size()>wardrobe_max_catalog_entries||appearances.size()>assets.size()||textures.size()>assets.size()||appearances.size()+textures.size()!=assets.size())throw FormatError("Make19 catalog/order budget or coverage");
    std::size_t bytes=0;std::set<std::pair<WardrobeAssetKind,std::string>> names;
    for(const auto& a:assets){
        if(a.kind!=WardrobeAssetKind::appearance&&a.kind!=WardrobeAssetKind::texture)throw FormatError("Make19 catalog kind");
        bounded(a.name,wardrobe_max_name_bytes,"catalog name",false);bounded(a.source,wardrobe_max_locator_bytes,"catalog source",false);bounded(a.asset,wardrobe_max_locator_bytes,"catalog member");
        const auto size=a.name.size()+a.source.size()+a.asset.size();
        if(size>wardrobe_max_catalog_bytes-bytes||a.name.find_first_of("/\\:")!=std::string::npos||!std::any_of(a.fingerprint.begin(),a.fingerprint.end(),[](auto x){return x!=0;})||!names.emplace(a.kind,fold(a.name)).second)throw FormatError("Make19 catalog content/name/budget authority");bytes+=size;
    }
    std::vector<bool> seen(assets.size());
    const auto check=[&](const auto& order,WardrobeAssetKind kind){for(const auto n:order){if(n>=assets.size()||seen[n]||assets[n].kind!=kind)throw FormatError("Make19 provider order identity/kind mismatch");seen[n]=true;}};
    check(appearances,WardrobeAssetKind::appearance);check(textures,WardrobeAssetKind::texture);
    texture_lookup_.reserve(textures.size());
    for(std::size_t i=0;i<textures.size();++i)texture_lookup_.push_back({textures[i],static_cast<std::uint32_t>(i)});
    std::sort(texture_lookup_.begin(),texture_lookup_.end(),[&](const auto& a,const auto& b){return fold(assets[a.first].name)<fold(assets[b.first].name);});
    for(const auto n:appearances){const auto name=fold(assets[n].name);
        if(name.size()<4||!std::all_of(name.begin()+1,name.begin()+4,[](char c){return c>='0'&&c<='9';}))continue;
        const auto end=name.find('_');const auto stop=end==std::string::npos?name.size():end;
        if(stop<4)continue;auto type=name.substr(4,stop-4);
        if(ends(type,"lgt")||ends(type,"med")||ends(type,"drk"))type.resize(type.size()-3);
        collections_[name.substr(0,1)+":"+type].push_back(n);
        if(name[0]=='b'&&(ends(type,"fat")||ends(type,"fit")||ends(type,"skn")))collections_["b:"+type.substr(0,type.size()-3)].push_back(n);
    }
    clone_collections_=collections_;
}
std::optional<std::uint32_t> Ts1CharacterCollections::texture(const std::string& wanted,bool exact)const{
    const auto begin=std::lower_bound(texture_lookup_.begin(),texture_lookup_.end(),wanted,[&](const auto& entry,const auto& key){return fold((*assets_)[entry.first].name)<key;});
    std::optional<std::pair<std::uint32_t,std::uint32_t>> selected;
    for(auto i=begin;i!=texture_lookup_.end();++i){const auto name=fold((*assets_)[i->first].name);
        if(exact?name!=wanted:name.compare(0,wanted.size(),wanted)!=0)break;
        // A sorted index accelerates lookup, but never replaces FirstOrDefault
        // in the original provider's insertion order with lexical precedence.
        if(!selected||i->second<selected->second)selected=*i;
    }
    return selected?std::optional<std::uint32_t>{selected->first}:std::nullopt;
}
bool Ts1CharacterCollections::appearance(const std::string& wanted)const{
    return std::any_of(assets_->begin(),assets_->end(),[&](const auto& a){return a.kind==WardrobeAssetKind::appearance&&fold(a.name)==wanted;});
}
std::vector<Ts1CharacterCollections::Choice> Ts1CharacterCollections::filter(const std::string& key,const std::string& skin){
    const auto f=collections_.find(key);if(f==collections_.end())throw Ts1CharacterUnavailable("Make19 source clothing collection absent");
    auto& list=f->second;std::vector<Choice> valid;valid.reserve(list.size());
    std::map<std::string,std::optional<std::uint32_t>> matches;
    std::size_t retained=0;
    for(const auto n:list){const auto query=prefix(fold((*assets_)[n].name))+skin;
        auto found=matches.find(query);if(found==matches.end())found=matches.emplace(query,texture(query)).first;
        const auto t=found->second;if(t){list[retained++]=n;valid.push_back({n,*t});}}
    list.resize(retained);return valid;
}
Ts1MakeCharacterPlan Ts1CharacterCollections::prepare(const Ts1MakeCharacterInput& in,const std::function<std::uint64_t(std::uint64_t)>& next){
    input(in);const bool clone=in.age<0;
    const auto source_field=[&](std::uint16_t index)->const std::string&{
        for(const auto& s:in.source_body_strings)if(s.index==index)return s.bytes;
        throw FormatError("Make19 clone required authenticated receiver BodySTR field absent");
    };
    bool female=in.gender!=0,child=in.avatar_type==0&&in.age<18;
    std::uint8_t species=in.avatar_type;std::string skin,raw_skin;
    if(clone){
        const auto sex=fold(source_field(12));
        if(sex=="male"||sex=="female"){species=0;female=sex=="female";}
        else if(sex=="dogmale"||sex=="dogfemale"){species=1;female=sex=="dogfemale";}
        else if(sex=="catmale"||sex=="catfemale"){species=2;female=sex=="catfemale";}
        else throw FormatError("Make19 clone sex outside authenticated original profile");
        // Original getter parses one-basedSTR14, then setter emits class9/27.
        // Preserve C atoi's bounded sign/whitespace/numeric-prefix behavior.
        const auto& text=source_field(13);std::size_t i=0;
        while(i<text.size()&&(text[i]==' '||text[i]=='\t'||text[i]=='\n'||text[i]=='\r'||text[i]=='\f'||text[i]=='\v'))++i;
        bool negative=false;if(i<text.size()&&(text[i]=='+'||text[i]=='-'))negative=text[i++]=='-';
        std::uint64_t value=0;const std::uint64_t limit=negative?2147483648ULL:2147483647ULL;
        for(;i<text.size()&&text[i]>='0'&&text[i]<='9';++i){const auto n=static_cast<unsigned>(text[i]-'0');if(value>(limit-n)/10)throw FormatError("Make19 clone original age atoi overflow unsupported");value=value*10+n;}
        child=(negative?-static_cast<std::int64_t>(value):static_cast<std::int64_t>(value))<18;
        raw_skin=source_field(14);skin=fold(raw_skin);
        if(skin!="lgt"&&skin!="med"&&skin!="drk")throw FormatError("Make19 clone skin outside authenticated source profile");
    }else{
        if(in.color<0||in.color>2)throw FormatError("Make19 color is outside admitted source table");
        raw_skin=skin=std::array<const char*,3>{"lgt","med","drk"}[in.color];
    }
    Ts1MakeCharacterPlan result;result.template_guid=in.avatar_type==0?0x7fd96b54u:in.avatar_type==1?0x4a70df92u:0x7bea0977u;
    auto& out=result.character;out.language=in.language;out.name=in.source_name;
    put(out,0,species==0?(child?"child":"adult"):(species==1?"dog":"kat"));
    put(out,12,species==0?(female?"female":"male"):std::string(species==1?"dog":"cat")+(female?"female":"male"));
    put(out,13,child?"9":"27");put(out,14,raw_skin);
    const auto type=std::string(female?"f":"m")+(child?"c":"a");
    std::optional<Choice> selected_body;
    if(clone){
        // 472fca/455edc parse only the first semicolon component; 473098
        // compares BOTH fields with length+ASCII folding. Keep BODY=/HEAD-HEAD=
        // in the second field. No trimming or bare-texture comparison.
        const auto pair=[](const std::string& literal){auto s=literal.substr(0,literal.find(';'));const auto comma=s.find(',');
            return std::make_pair(fold(s.substr(0,comma)),comma==std::string::npos?std::string{}:fold(s.substr(comma+1)));};
        const auto pick=[&](const std::string& key,std::uint16_t slot,const std::string& clause)->std::optional<Choice>{
            const auto wanted=pair(source_field(slot));const auto collection=clone_collections_.find(key);
            const auto clause_key=fold(clause);if(collection==clone_collections_.end()||wanted.second.compare(0,clause_key.size(),clause_key)!=0)return {};
            // Normal selection chooses the first matching texture. Clone instead
            // searches the FULL admitted pair collection: any admitted texture
            // variant can be the exact pair already named by the receiver.
            const auto exact_texture=texture(wanted.second.substr(clause_key.size()),true);
            if(!exact_texture)return {};
            for(const auto n:collection->second)if(wanted.first==fold((*assets_)[n].name)){
                const auto required=prefix(fold((*assets_)[n].name))+(species==0?skin:std::string{});
                if(fold((*assets_)[*exact_texture].name).compare(0,required.size(),required)==0)return Choice{n,*exact_texture};
            }
            return {}; // Source getter -1; corresponding setter writes empty.
        };
        selected_body=pick(species==0?"b:"+type+(child?"chd":""):(species==1?"b:dog":"b:cat"),1,"BODY=");
        const auto selected_head=pick(species==0?"c:"+type:(species==1?"c:dog":"c:cat"),2,"HEAD-HEAD=");
        put(out,1,selected_body?(*assets_)[selected_body->appearance].name+",BODY="+(*assets_)[selected_body->texture].name:std::string{});
        put(out,2,selected_head?(*assets_)[selected_head->appearance].name+",HEAD-HEAD="+(*assets_)[selected_head->texture].name:std::string{});
    }else if(species){
        if(in.pet_outfit_index< -1)throw FormatError("Make19 pet outfit selector unsupported");
        auto choices=filter(species==1?"b:dog":"b:cat","");
        if(choices.empty())throw Ts1CharacterUnavailable("Make19 pet source collection has no textured outfit");
        const auto n=in.pet_outfit_index== -1?draw(next,choices.size()):static_cast<std::uint64_t>(in.pet_outfit_index);
        if(n>=choices.size())throw FormatError("Make19 pet outfit selector exceeds source collection");
        selected_body=choices[n];put(out,1,(*assets_)[selected_body->appearance].name+",BODY="+(*assets_)[selected_body->texture].name);result.reset_pet_outfit_index=true;
    }else{
        // Original age/sex class selects ma/fa or mc/fc, not C#'s mm/fm typo.
        auto heads=filter("c:"+type,skin);auto bodies=filter("b:"+type+(child?"chd":""),skin);
        if(heads.empty()||bodies.empty())throw Ts1CharacterUnavailable("Make19 human source collection has no textured outfit");
        const auto& head=heads[draw(next,heads.size())];selected_body=bodies[draw(next,bodies.size())];
        put(out,1,(*assets_)[selected_body->appearance].name+",BODY="+(*assets_)[selected_body->texture].name);
        put(out,2,(*assets_)[head.appearance].name+",HEAD-HEAD="+(*assets_)[head.texture].name);
    }
    if(species==0&&selected_body){
        const auto bname=fold((*assets_)[selected_body->appearance].name);const auto underscore=bname.find('_');
        auto hand=underscore==std::string::npos?bname:bname.substr(underscore);
        auto hand_texture=texture("huao"+hand,true);if(!hand_texture)hand_texture=texture("huao"+skin,true);
        if(!hand_texture)throw FormatError("Make19 selected human hand texture absent");
        // Intermediate base: pinned SimTemplateCreateInfo constructor and
        // MakeNewCharacter hand overrides. Original afbd equivalence is NOT
        // asserted; retain these exact rules until a demonstrated divergence.
        if(underscore==std::string::npos||underscore<3)throw FormatError("Make19 selected body lacks source constructor shape suffix");
        const auto shape=bname.substr(underscore-3,3);const auto sex=std::string(female?"f":"m");
        const auto code=type+shape,unisex=child?"uchd":sex+shape;
        put(out,15,"n"+unisex+"_01,BODY=n"+unisex+skin+"_01");
        put(out,16,"n"+unisex+"_01,BODY=u"+sex+shape+skin+(child?"undies":"lguard")+"_01");
        put(out,27,female?"ff"+shape+"_01,BODY=ff"+shape+skin+"_01":",BODY=f"+sex+shape+skin+"_01");
        put(out,28,female?"HfLO,HAND=gfao_bear1":"HmLO,HAND=gmao_yeti");
        put(out,29,female?"HfRO,HAND=gfao_bear1":"HmRO,HAND=gmao_yeti");
        put(out,30,child?"ADDED":"f100"+code+"_original,BODY=f100"+code+skin+"_original");
        put(out,31,"s100"+code+"_original,BODY=s100"+code+skin+"_original");
        put(out,32,"l100"+code+"_original,BODY=l100"+code+skin+"_original");
        put(out,33,"w100"+code+"_original,BODY=w100"+code+skin+"_original");
        put(out,34,child?"ADDED":"h533"+code+"_zoot,BODY=h533"+code+skin+"_zoot");
        const auto h=std::string("h")+(child?"u":(female?"f":"m"));
        const std::array<std::string,6> hands={h+"lo",h+"ro",h+"lp",h+"rp",h+"lo",h+"rc"};
        for(std::size_t i=0;i<hands.size();++i){if(!appearance(hands[i]))throw FormatError("Make19 selected human hand appearance absent");put(out,static_cast<std::uint16_t>(17+i),hands[i]+",HAND="+(*assets_)[*hand_texture].name);}
    }
    std::sort(out.body_strings.begin(),out.body_strings.end(),[](const auto& a,const auto& b){return a.index<b.index;});
    return result;
}
} // namespace odh
