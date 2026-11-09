#include "odh/family.hpp"
#include <iostream>
#include <limits>
#include <sstream>

namespace {
int checks=0;
void require(bool value,const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f,const char* message) {
    bool rejected=false; try { f(); } catch(const odh::FormatError&) { rejected=true; }
    require(rejected,message);
}
void u32(std::string& s,std::uint32_t n) { for (int i=0;i<4;++i) s+=static_cast<char>(n>>(i*8)); }
void patch(std::string& s,std::size_t offset,std::uint32_t n) { for(int i=0;i<4;++i) s.at(offset+static_cast<std::size_t>(i))=static_cast<char>(n>>(i*8)); }
std::string payload(std::uint32_t count=2) {
    std::string s;
    u32(s,0xdeadbeef);u32(s,9);s+="IMAF";
    for (auto n:{7U,0xffffffffU,0x80000000U,0x7fffffffU,0x87654321U,0x12345678U})u32(s,n);
    u32(s,count); for(std::uint32_t i=0;i<count;++i)u32(s,0xffffffffU-i);
    return s;
}
odh::Family read(const std::string& s,const odh::FamiLimits& limits={}) {
    std::istringstream input(s,std::ios::binary);return odh::read_fami(odh::View(input,s.size()),limits);
}
odh::FamilyEntry entry(std::uint16_t id,std::int32_t house,std::uint32_t guid=0xffffffffU) {
    auto f=read(payload(0));f.house_number=house;f.member_guids={guid};return {id,std::move(f)};
}
std::string encoded(const odh::Family& value, const odh::FamiLimits& limits={}) {
    const auto bytes=odh::encode_fami(value,limits);
    return std::string(bytes.begin(),bytes.end());
}
std::string encoded_simitone(const odh::Family& value, const odh::FamiLimits& limits={}) {
    const auto bytes=odh::encode_simitone_create_family_fami(value,limits);
    return std::string(bytes.begin(),bytes.end());
}
void original_v9_tail() {
    auto raw=payload();std::string tail;
    u32(tail,0x87654321U);
    const std::array<std::int16_t,6> words{-32768,-1,0,1,12345,32767};
    for(const auto word:words){const auto bits=static_cast<std::uint16_t>(word);tail+=static_cast<char>(bits);tail+=static_cast<char>(bits>>8);}
    tail+="XYZ";raw+=tail;
    auto family=read(raw);
    require(family.remaining_bytes==19&&family.tail_bytes==std::vector<std::uint8_t>(tail.begin(),tail.end()),
        "complete raw suffix retains known and unknown byte extents");
    require(family.original_v9_extension&&family.original_v9_extension->extension_dword==0x87654321U&&
        family.original_v9_extension->spell_words==words,"FAMI9 decodes exact signed six-word extension");
    auto encode=[](const odh::Family& value,const odh::FamiLimits& limits=odh::FamiLimits{}){
        const auto bytes=odh::encode_preserved_fami(value,limits);return std::string(bytes.begin(),bytes.end());
    };
    require(encode(family)==raw,"preserved encoder roundtrips known and unknown suffix exactly");
    const auto original_tail=family.tail_bytes;
    family.original_v9_extension->extension_dword=7;family.original_v9_extension->spell_words[0]=32767;
    const auto patched=read(encode(family));
    require(patched.original_v9_extension&&patched.original_v9_extension->extension_dword==7&&
        patched.original_v9_extension->spell_words[0]==32767&&patched.tail_bytes[16]=='X'&&
        patched.tail_bytes[17]=='Y'&&patched.tail_bytes[18]=='Z'&&family.tail_bytes==original_tail,
        "typed alterations patch output without discarding extras or modifying original suffix");
    rejects([&]{encoded(family);},"default tail-free encoder cannot silently discard typed extension");
    auto mismatched=family;--mismatched.remaining_bytes;rejects([&]{encode(mismatched);},"tail extent mismatch rejects");
    mismatched=family;mismatched.version=8;rejects([&]{encode(mismatched);},"typed extension cannot migrate to another wire version");
    auto limit=odh::FamiLimits{};limit.payload_bytes=raw.size();require(encode(family,limit).size()==raw.size(),"preserved suffix counts exact payload boundary");
    --limit.payload_bytes;rejects([&]{encode(family,limit);},"preserved suffix counts against configured payload limit");
    for(std::size_t count=0;count<16;++count){const auto incomplete=read(payload()+tail.substr(0,count));
        require(!incomplete.original_v9_extension&&incomplete.remaining_bytes==count&&encode(incomplete)==payload()+tail.substr(0,count),
            "incomplete v9 suffix stays raw without invented zero spell data");}
    for(const auto version:{8U,10U,0xffffffffU}){auto other=raw;patch(other,4,version);const auto decoded=read(other);
        require(!decoded.original_v9_extension&&encode(decoded)==other,"legacy and unknown tails preserve without assuming v9 schema");}
}
}
int main() {
 try {
    original_v9_tail();
    const auto raw=payload();const auto f=read(raw);
    require(f.reserved==0xdeadbeef&&f.version==9,"header metadata retained");
    require(f.house_number==7&&f.family_number==-1,"house and alternate family number retained");
    require(f.budget==std::numeric_limits<std::int32_t>::min()&&f.value_in_arch==std::numeric_limits<std::int32_t>::max(),"signed extrema retained");
    require(f.family_friends==-2023406815&&f.unknown==0x12345678,"signed friends and raw unknown retained");
    require(f.member_guids==std::vector<std::uint32_t>({0xffffffffU,0xfffffffeU})&&f.remaining_bytes==0,"high GUIDs remain unsigned");
    {odh::Family created{};created.version=9;created.budget=20000;created.family_number=1;created.unknown=24;created.member_guids={0x12345678};
     const auto result=read(encoded(created));require(result.version==9&&result.house_number==0&&result.budget==20000&&result.family_number==1&&result.unknown==24&&result.member_guids==created.member_guids,"new synthetic family encodes structurally");}
    {
        odh::Family created{};created.version=9;created.budget=20000;created.family_number=1;
        created.unknown=24;created.member_guids={0x12345678,0x87654321};
        const auto bytes=encoded_simitone(created);
        require(bytes.size()==64&&bytes.substr(bytes.size()-16)==std::string(16,'\0'),
                "Simitone CreateFamily profile appends exactly four zero dwords");
        const auto decoded=read(bytes);
        require(decoded.version==9&&decoded.house_number==0&&decoded.family_number==created.family_number&&
                decoded.budget==created.budget&&decoded.value_in_arch==0&&decoded.unknown==24&&
                decoded.member_guids==created.member_guids&&decoded.remaining_bytes==16,
                "Simitone CreateFamily profile structurally decodes without inventing tail semantics");
        auto supplied_values=created;supplied_values.family_number=-7;supplied_values.budget=-1234;
        const auto preserved=read(encoded_simitone(supplied_values));
        require(preserved.family_number==-7&&preserved.budget==-1234,
                "Simitone profile serializes supplied family number and budget without inventing values");
        auto bad_profile=created;bad_profile.remaining_bytes=1;
        rejects([&]{encoded_simitone(bad_profile);},"Simitone CreateFamily rejects unpreserved input tail");
        bad_profile=created;bad_profile.version=8;
        rejects([&]{encoded_simitone(bad_profile);},"Simitone CreateFamily rejects unexpected version");
        bad_profile=created;bad_profile.house_number=1;
        rejects([&]{encoded_simitone(bad_profile);},"Simitone CreateFamily rejects nonzero house");
        bad_profile=created;bad_profile.value_in_arch=1;
        rejects([&]{encoded_simitone(bad_profile);},"Simitone CreateFamily rejects nonzero architecture value");
        bad_profile=created;bad_profile.unknown=23;
        rejects([&]{encoded_simitone(bad_profile);},"Simitone CreateFamily rejects unexpected unknown field");
        bad_profile=created;bad_profile.member_guids.clear();
        rejects([&]{encoded_simitone(bad_profile);},"Simitone CreateFamily requires at least one member");
        require(encoded(created)!=bytes,"tail-free default encoder remains distinct from Simitone profile");
        auto constrained=odh::FamiLimits{};constrained.payload_bytes=64;
        require(encoded_simitone(created,constrained)==bytes,"Simitone CreateFamily accepts exact payload limit");
        constrained.payload_bytes=63;
        rejects([&]{encoded_simitone(created,constrained);},"Simitone CreateFamily counts its 16-byte tail against payload limit");
        constrained={};constrained.members=1;
        rejects([&]{encoded_simitone(created,constrained);},"Simitone CreateFamily observes member limit");
    }
    require(encoded(f)==raw,"FAMI exact structural byte roundtrip including signed extrema");
    require(encoded(read(payload(0)))==payload(0),"empty FAMI encodes");
    require(encoded(read(payload(256)))==payload(256),"maximum default member count encodes");
    for(auto version:{0U,8U,9U,0xffffffffU}){auto candidate=f;candidate.version=version;require(read(encoded(candidate)).version==version,"FAMI raw version preserved");}
    {auto candidate=f;candidate.remaining_bytes=1;rejects([&]{encoded(candidate);},"FAMI unpreserved tail rejects");}
    {auto l=odh::FamiLimits{};l.members=1;rejects([&]{encoded(f,l);},"FAMI encode member limit");l={};l.payload_bytes=raw.size()-1;rejects([&]{encoded(f,l);},"FAMI encode payload limit");l.payload_bytes=39;rejects([&]{encoded(read(payload(0)),l);},"FAMI prefix limit");}
    for (auto version:{0U,8U,9U,0xffffffffU}) {
        auto s=raw;patch(s,4,version);require(read(s).version==version,"version metadata has no invented whitelist");
    }
    for (std::size_t n=0;n<raw.size();++n)rejects([&]{read(raw.substr(0,n));},"all required prefixes reject");
    for (std::size_t tail=0;tail<=17;++tail) {
        const auto decoded=read(raw+std::string(tail,'\x81'));
        require(decoded.remaining_bytes==tail&&decoded.member_guids==f.member_guids,"tail extent retained without optional zero defaults");
    }
    auto bad=raw;bad[8]='x';rejects([&]{read(bad);},"invalid signature");
    for(auto count:{0xffffffffU,0x80000000U,0x7fffffffU,3U}) {
        auto s=raw;patch(s,36,count);rejects([&]{read(s);},"negative/impossible count rejected before allocation");
    }
    odh::FamiLimits exact;exact.payload_bytes=raw.size();exact.members=2;
    require(read(raw,exact).member_guids.size()==2,"exact limits accepted");
    auto limit=exact;--limit.payload_bytes;rejects([&]{read(raw,limit);},"payload budget");
    limit=exact;--limit.members;rejects([&]{read(raw,limit);},"member budget");
    limit={};limit.members=0;require(read(payload(0),limit).member_guids.empty(),"empty family allowed");
    require(read(payload(256)).member_guids.size()==256,"default member boundary");
    rejects([&]{read(payload(257));},"default member cap");
    rejects([&]{read(payload(0)+std::string(65536-39,'x'));},"default payload cap includes tails");
    {
        std::istringstream input("prefix"+raw+"tail");odh::View view(input,6+raw.size()+4);
        require(odh::read_fami(view.subview(6,raw.size())).remaining_bytes==0,"bounded subview");
        rejects([&]{odh::read_fami(view.subview(6,raw.size()-1));},"outside subview member unavailable");
    }
    {
        std::istringstream input(raw.substr(0,raw.size()-1));odh::View view(input,raw.size());
        rejects([&]{odh::read_fami(view);},"physical truncation never caught as optional tail");
    }
    std::vector<odh::FamilyEntry> source{entry(65535,7),entry(9,32768),entry(2,-1),entry(1,65543,0x80000000U)};
    odh::FamilyContext context(source);
    require(context.source()[0].chunk_id==65535&&context.source()[3].chunk_id==1,"serialized ordering retained");
    require(context.by_house(7)->chunk_id==1&&context.by_house(7)->family.house_number==65543,"last serialized same low16 house wins without narrowing raw field");
    require(context.by_house(-32768)->chunk_id==9&&context.by_house(-1)->chunk_id==2,"signed16 house cast profile");
    require(context.by_id(65535)->family.family_number==-1,"unsigned chunk identity distinct from FamilyNumber");
    require(!context.by_id(8)&&!context.by_house(8),"missing identity/house is absent");
    require(odh::FamilyContext::contains(*context.by_id(65535),0xffffffffU)&&!odh::FamilyContext::contains(*context.by_id(65535),0x80000000U),"membership uses exact unsigned GUIDs");
    const auto source_budget=source[3].family.budget;
    source[0].family.member_guids.clear();source[3].family.house_number=8;source.clear();
    const auto original_budget=context.by_house(7)->family.budget;
    const std::int32_t changed_budget=12345;
    require(changed_budget!=original_budget,"owned-copy test uses a distinct representable budget");
    context.by_house(7)->family.budget=changed_budget;
    require(context.by_house(7)->chunk_id==1&&context.by_house(7)->family.budget==changed_budget&&
            source_budget!=context.by_house(7)->family.budget&&
            odh::FamilyContext::contains(*context.by_id(65535),0xffffffffU),
            "runtime family budget changes only the owned copy of original family data");
    std::vector<odh::FamilyEntry> reversed{entry(1,7),entry(65535,7)};
    require(odh::FamilyContext(reversed).by_house(7)->chunk_id==65535,"serialized reverse chooses different winner");
    reversed[1].chunk_id=1;rejects([&]{odh::FamilyContext c(reversed);},"duplicate chunk IDs ambiguous even across houses");
    auto wide=entry(3,std::numeric_limits<std::int32_t>::min());
    require(odh::FamilyContext({wide}).by_house(0)->family.house_number==std::numeric_limits<std::int32_t>::min(),"int32 minimum exact low16 mapping");
    wide.family.house_number=std::numeric_limits<std::int32_t>::max();
    require(odh::FamilyContext({wide}).by_house(-1)->chunk_id==3,"int32 maximum exact low16 mapping");
    wide.family.member_guids.resize(257);rejects([&]{odh::FamilyContext c({wide});},"runtime per-family member cap");
    std::vector<odh::FamilyEntry> bounded;
    for(std::uint16_t id=0;id<4096;++id)bounded.push_back(entry(id,id));
    require(odh::FamilyContext(bounded).source().size()==4096,"runtime exact family cap");
    bounded.push_back(entry(4096,4096));rejects([&]{odh::FamilyContext c(bounded);},"runtime family count cap");
    bounded.resize(257);for(auto& e:bounded)e.family.member_guids.resize(256);
    rejects([&]{odh::FamilyContext c(bounded);},"runtime cumulative member cap");
    bounded.resize(256);require(odh::FamilyContext(bounded).source().size()==256,"runtime exact cumulative member cap");
    {
        std::vector<odh::FamilyEntry> tails;
        for(std::uint16_t id=0;id<65;++id){auto e=entry(id,id);e.family.tail_bytes.resize(64*1024);e.family.remaining_bytes=e.family.tail_bytes.size();tails.push_back(std::move(e));}
        rejects([&]{odh::FamilyContext too_many(tails);},"runtime cumulative preserved tails bounded before copying");
        tails.pop_back();require(odh::FamilyContext(tails).source().size()==64,"runtime exact4MiB preserved tail boundary");
        tails.resize(1);tails[0].family.tail_bytes.push_back(0);rejects([&]{odh::FamilyContext too_large(tails);},"runtime per-family preserved tail limit");
    }
    require(odh::FamilyContext({}).source().empty(),"empty runtime context");
    std::cout<<checks<<" family checks passed\n";
 } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
