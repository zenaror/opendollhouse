#include "odh/avatar_body.hpp"
#include <charconv>
#include <cctype>

namespace odh {
AvatarBodyStrings effective_avatar_body(const AvatarBodyStrings& original,const Entity& entity){
    auto result=original;
    if(entity.avatar_appearance&&entity.avatar_appearance->body){
        const auto& outfit=*entity.avatar_appearance->body;
        if(outfit.kind!=AvatarOutfitKind::literal)
            throw FormatError("live avatar outfit requires unavailable numeric/null outfit content");
        result.body=outfit.literal;result.hands=outfit.hands;
    }
    return result;
}
std::vector<std::string> effective_avatar_accessories(const Entity& entity){
    if(entity.avatar_appearance&&entity.avatar_appearance->accessory_assets)
        return *entity.avatar_appearance->accessory_assets;
    return entity.bound_appearances;
}

namespace {
std::vector<std::string> us_strings(const StringTable& table) {
    if(table.state!=StringTableState::decoded)
        throw FormatError("avatar body requires decoded US strings");
    if(table.entries.size()>65536)throw FormatError("avatar body table exceeds bounded entry profile");
    std::vector<std::string> result;
    std::size_t bytes=0;
    for(const auto& entry:table.entries)if(entry.language==0||entry.language==1){
        if(result.size()==50 || entry.value.size()>avatar_body_text_bytes-bytes)
            throw FormatError("avatar body US strings exceed bounded profile");
        bytes+=entry.value.size();result.push_back(entry.value);
    }
    if(result.empty())throw FormatError("avatar body requires a US string group");
    return result;
}
std::vector<std::string> split_names(const std::string& input) {
    std::vector<std::string> result;
    std::size_t start=0;
    while(start<input.size()){
        const auto end=input.find(';',start);
        auto value=input.substr(start,end==std::string::npos?end:end-start);
        if(!value.empty()){
            if(result.size()==avatar_body_names)throw FormatError("avatar names exceed bounded profile");
            result.push_back(std::move(value));
        }
        if(end==std::string::npos)break;
        start=end+1;
    }
    if(!input.empty()&&result.empty())throw FormatError("avatar names contain only delimiters");
    return result;
}
std::int16_t age_word(const std::string& value) {
    // Convert.ToInt16 accepts surrounding whitespace and an optional sign.
    std::size_t first=0,last=value.size();
    while(first<last&&std::isspace(static_cast<unsigned char>(value[first])))++first;
    while(last>first&&std::isspace(static_cast<unsigned char>(value[last-1])))--last;
    if(first<last&&value[first]=='+'){
        ++first;
        if(first<last&&(value[first]=='-'||value[first]=='+'))
            throw FormatError("avatar age has multiple signs");
    }
    int age=0;
    const auto parsed=std::from_chars(value.data()+first,value.data()+last,age);
    if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+last||age<-32768||age>32767)
        throw FormatError("avatar age requires a signed 16-bit decimal");
    return static_cast<std::int16_t>(age);
}
std::optional<std::int16_t> gender_word(std::string value) {
    for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if(value=="male")return 0;
    if(value=="female")return 1;
    if(value=="dogmale")return 8;
    if(value=="dogfemale")return 9;
    if(value=="catmale")return 16;
    if(value=="catfemale")return 17;
    return {};
}
void validate_body(const AvatarBodyStrings& body) {
    if(!avatar_body_type(body.avatar_type))throw FormatError("avatar body has an unsupported BodySTR type");
    std::size_t bytes=0;
    const auto consume=[&](const std::string& value){
        if(value.size()>avatar_body_text_bytes-bytes)throw FormatError("avatar body text exceeds bounded profile");
        bytes+=value.size();
    };
    consume(body.avatar_type);consume(body.body);consume(body.head);consume(body.raw_names);
    for(const auto& hand:body.hands)consume(hand);
    if(!body.age || body.names!=split_names(body.raw_names))throw FormatError("avatar body requires parsed age and names");
    if(body.gender&&*body.gender!=0&&*body.gender!=1&&*body.gender!=8&&*body.gender!=9&&*body.gender!=16&&*body.gender!=17)
        throw FormatError("avatar body gender outside supported values");
}
}
AvatarBodyStrings parse_avatar_body_strings(const StringTable& table) {
    const auto strings=us_strings(table);
    if(strings.size()<23)throw FormatError("avatar body US group requires indices 0..22");
    AvatarBodyStrings result{};
    result.avatar_type=strings[0];
    result.age=age_word(strings[13]);result.gender=gender_word(strings[12]);
    result.body=strings[1].substr(0,strings[1].find(';'));result.head=strings[2];
    for(std::size_t i=0;i<result.hands.size();++i)result.hands[i]=strings[17+i];
    result.raw_names=strings[11];result.names=split_names(result.raw_names);
    return result;
}
AvatarBodyDescriptor derive_avatar_body_descriptor(const World& world,std::int16_t actor,
    const RandomState& random,const std::optional<AvatarBodyStrings>& body,
    const StringTable& global_walk,const StringTable& global_swim,
    const std::optional<StringTable>& object_walk) {
    const auto* entity=world.find(actor);
    if(!entity||entity->kind!=EntityKind::avatar||entity->person_data.size()<66||entity->motives.size()!=16)
        throw FormatError("avatar body descriptor requires PersonData age/gender fields and Motives[16]");
    if(body)validate_body(*body);
    const auto walk=us_strings(global_walk),swim=us_strings(global_swim);
    const auto special=object_walk?us_strings(*object_walk):std::vector<std::string>{};
    AvatarBodyDescriptor result{};result.body=body;
    if(body)result.body_type=avatar_body_type(body->avatar_type);
    const auto age=body?*body->age:entity->person_data[58];
    result.child=age<18;result.global_walk_id=result.child?151:150;result.global_swim_id=result.child?160:158;
    if(body)result.phenotype=AvatarPhenotype::ts1_light;
    for(std::size_t i=0;i<walk.size();++i)result.walk[i]=walk[i];
    for(std::size_t i=0;i<swim.size();++i)result.swim[i]=swim[i];
    for(std::size_t i=0;i<special.size();++i)if(!special[i].empty())result.walk[i]=special[i];
    auto candidate=random;
    if(body&&!body->names.empty())result.selected_name=body->names[static_cast<std::size_t>(candidate.next(body->names.size()))];
    return result;
}
AvatarBodyDescriptor apply_avatar_body(World& world,std::int16_t actor,RandomState& random,
    const std::optional<AvatarBodyStrings>& body,const StringTable& global_walk,
    const StringTable& global_swim,const std::optional<StringTable>& object_walk) {
    const auto* entity=world.find(actor);
    if(!entity||entity->kind!=EntityKind::avatar||entity->person_data.size()<101||entity->motives.size()!=16)
        throw FormatError("avatar body requires avatar PersonData[101] and Motives[16]");
    auto result=derive_avatar_body_descriptor(world,actor,random,body,global_walk,global_swim,object_walk);
    auto candidate=random;
    if(body&&!body->names.empty())candidate.next(body->names.size());
    // These existing fixed-size banks have been checked above. Their setters
    // cannot allocate or fail, so every failure path precedes publication.
    if(body){world.set_person_data(actor,58,*body->age);if(body->gender)world.set_person_data(actor,65,*body->gender);}
    for(std::int16_t i=0;i<16;++i)world.set_motive(actor,i,i==11?0:100);
    world.set_person_data(actor,7,1000);
    random=candidate;
    return result;
}
}
