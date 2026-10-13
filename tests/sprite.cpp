#include "odh/sprite.hpp"
#include <iostream>
#include <sstream>

namespace {
int checks=0;
void require(bool value,const char* why) { ++checks; if(!value)throw std::runtime_error(why); }
template<class F>void rejects(F f) {
    bool failed=false; try { f(); } catch(const odh::FormatError&) { failed=true; }
    require(failed,"malformed, unsupported or truncated sprite rejected");
}
void set(std::string& bytes,std::size_t at,std::uint32_t value,unsigned length=4) {
    for(unsigned i=0;i<length;++i)bytes.at(at+i)=static_cast<char>((value>>(i*8))&255);
}
std::string word(unsigned code,unsigned count) {
    std::string result(2,'\0'); set(result,0,(code<<13)|count,2); return result;
}
std::string octets(std::initializer_list<unsigned> values) {
    std::string result; for(auto n:values)result+=static_cast<char>(n); return result;
}
std::string row(const std::string& packets) { return word(0,packets.size()+2)+packets; }
std::string palt(unsigned entries=3) {
    std::string bytes(16+entries*3,'\0'); set(bytes,0,1); set(bytes,4,entries);
    for(unsigned n=0;n<8;++n)bytes[8+n]=static_cast<char>(n+91);
    for(unsigned n=0;n<entries;++n) {
        bytes[16+n*3]=static_cast<char>(10+n*40);
        bytes[17+n*3]=static_cast<char>(20+n*40);
        bytes[18+n*3]=static_cast<char>(30+n*40);
    }
    return bytes;
}
odh::Palette palette(const std::string& bytes=palt(),odh::SpriteLimits limits={}) {
    std::istringstream stream(bytes); return odh::read_palt(odh::View(stream,bytes.size()),limits);
}
std::string frame(unsigned width,unsigned height,unsigned flags,const std::string& data) {
    std::string result(16,'\0'); set(result,0,width,2); set(result,2,height,2);
    set(result,4,flags); set(result,8,9,2); set(result,10,0,2);
    set(result,12,32767,2); set(result,14,32768,2); return result+data;
}
std::string sprite(const std::string& body) {
    std::string result(16,'\0'); set(result,0,1000); set(result,4,1);
    set(result,8,0x12345678U); set(result,12,16); return result+body;
}
odh::Spr2Frame decode(const std::string& bytes,odh::SpriteLimits limits={},
                      unsigned index=0,const odh::Palette& colors=palette()) {
    std::istringstream stream(bytes);
    return odh::read_spr2_frame(odh::View(stream,bytes.size()),index,colors,limits);
}
std::string mixed() {
    const auto packets=word(1,2)+octets({10,0,20,1})+
        word(2,3)+octets({30,2,0,40,1,15,50,0,31,237})+
        word(3,1)+word(6,1)+octets({2,239});
    return sprite(frame(8,5,7,row(packets)+word(4,2)+row("")+word(5,8191)));
}
}

int main() { try {
    const auto colors=palette();
    require(colors.version==1 && colors.colors==std::vector<std::array<std::uint8_t,4>>{
        {10,20,30,255},{50,60,70,255},{90,100,110,255}},"PALT RGB order and opaque alpha");
    require(colors.reserved==std::array<std::uint8_t,8>{91,92,93,94,95,96,97,98} &&
            colors.remaining_bytes==0,"PALT raw reserved bytes retained");
    for(unsigned version:{0U,1000U,0xFFFFFFFFU}) {
        auto bytes=palt(); set(bytes,0,version);
        require(palette(bytes).version==version,"PALT version is raw structural metadata");
    }
    require(palette(palt()+"tail").remaining_bytes==4,"PALT trailing extent retained");
    require(palette(palt(1)).colors.size()==1 && palette(palt(256)).colors.size()==256,
            "PALT supported cardinality endpoints");
    for(unsigned n:{0U,257U,0xFFFFFFFFU}) {
        auto bytes=palt();set(bytes,4,n);rejects([&]{palette(bytes);});
    }
    auto limits=odh::SpriteLimits{};limits.palette_entries=2;
    rejects([&]{palette(palt(),limits);});
    for(std::size_t cut=0;cut<palt().size();++cut) {
        rejects([&]{palette(palt().substr(0,cut));});
        rejects([&]{std::istringstream stream(palt().substr(0,cut));
            odh::read_palt(odh::View(stream,palt().size()));});
    }

    const auto bytes=mixed(); const auto image=decode(bytes);
    require(image.version==1000 && image.default_palette_id==0x12345678U &&
        image.palette_id==9 && image.effective_palette_id==0x5678 &&
        image.width==8 && image.height==5 && image.flags==7 && image.transparent_index==0 &&
        image.x==-32768 && image.y==32767,"SPR2 raw metadata and v1000 palette override");
    std::vector<std::array<std::uint8_t,4>> expected(40,{0,0,0,0});
    const std::vector<std::array<std::uint8_t,4>> first={
        {10,20,30,255},{50,60,70,255},{90,100,110,0},{50,60,70,123},
        {10,20,30,255},{10,20,30,0},{90,100,110,255},{0,0,0,0}};
    for(unsigned n=0;n<8;++n)expected[n]=first[n];
    for(unsigned n=8;n<24;++n)expected[n]={10,20,30,0};
    std::vector<std::uint8_t> indices(40,0),z(40,0);
    indices[1]=1;indices[2]=2;indices[3]=1;indices[6]=2;
    const std::array<std::uint8_t,8> first_z={10,20,30,40,50,255,0,255};
    for(unsigned n=0;n<8;++n)z[n]=first_z[n];
    for(unsigned n=8;n<32;++n)z[n]=255;
    require(image.pixels==expected && image.indices==indices && image.z==z,
        "all row commands, odd pads, skipped rows, row tail and untouched early-END row");
    require(image.ended_with_marker && image.remaining_bytes==0,"explicit END ignores its count");
    require(decode(bytes+"tail").remaining_bytes==4,"END leaves a visible frame tail");
    for(unsigned budget=0;budget<8;++budget) {
        limits={};limits.commands=budget;rejects([&]{decode(bytes,limits);});
    }
    limits={};limits.commands=8;require(decode(bytes,limits).pixels==expected,"exact command budget");
    for(unsigned field=0;field<5;++field) {
        limits={};
        if(field==0)limits.frames=0;
        if(field==1)limits.width=7;
        if(field==2)limits.height=4;
        if(field==3)limits.pixels=39;
        if(field==4)limits.palette_entries=2;
        rejects([&]{decode(bytes,limits);});
    }
    for(std::size_t cut=0;cut<bytes.size();++cut) {
        rejects([&]{decode(bytes.substr(0,cut));});
        rejects([&]{std::istringstream stream(bytes.substr(0,cut));
            odh::read_spr2_frame(odh::View(stream,bytes.size()),0,colors);});
    }

    std::string alpha_data;
    for(unsigned n=0;n<32;++n)alpha_data+=octets({n+100,1,n});
    const auto alphas=decode(sprite(frame(32,1,7,row(word(2,32)+alpha_data))));
    for(unsigned n=0;n<32;++n)
        require(alphas.pixels[n]==std::array<std::uint8_t,4>{50,60,70,
                    static_cast<std::uint8_t>(n*255/31)} && alphas.z[n]==n+100,
                "all five-bit alpha values retain RGB and independent Z");
    require(!alphas.ended_with_marker && alphas.remaining_bytes==0,"EOF after completed height accepted");
    const auto opaque_packets=word(6,2)+octets({0,2});
    for(unsigned flags:{1U,3U,5U,7U}) {
        const auto complete=sprite(frame(2,1,flags,row(opaque_packets)));
        const auto solid=decode(complete);
        require(solid.pixels==std::vector<std::array<std::uint8_t,4>>{
            {10,20,30,255},{90,100,110,255}},"indexed command keeps transparent index opaque");
        require(solid.indices==std::vector<std::uint8_t>{0,2},"palette indices retained without Z");
        require(solid.z==((flags&2U)?std::vector<std::uint8_t>{0,0}:std::vector<std::uint8_t>{}),
                "Z storage exists only with its flag");
        const auto skipped=decode(sprite(frame(2,1,flags,word(4,0)+word(4,1)+word(5,0))));
        require(skipped.pixels==std::vector<std::array<std::uint8_t,4>>(2,{10,20,30,0}),
                "transparent skipped rows also work without alpha flag; skip zero makes progress");
        require(skipped.z==((flags&2U)?std::vector<std::uint8_t>{255,255}:std::vector<std::uint8_t>{}),
                "skipped row Z is maximal when present");
    }
    for(unsigned length:{1U,2U}) {
        auto packet=word(2,length);for(unsigned n=0;n<length;++n)packet+=octets({7,1,16});
        if(length&1U)packet+=octets({251});
        require(decode(sprite(frame(length,1,7,row(packet)))).pixels==
            std::vector<std::array<std::uint8_t,4>>(length,{50,60,70,131}),"odd/even alpha padding");
    }
    const auto empty=decode(sprite(frame(0,0,1,"")));
    require(empty.pixels.empty() && empty.indices.empty() && empty.z.empty() && !empty.ended_with_marker,
            "zero-sized empty frame accepted without allocating pixels");
    require(decode(sprite(frame(2,2,3,word(5,0)))).z==std::vector<std::uint8_t>(4,0),
            "immediate END preserves all zero defaults");
    const auto zero_packets=sprite(frame(2,1,7,row(word(1,0)+word(2,0)+word(3,0)+word(6,0))));
    limits={};limits.commands=5;
    require(decode(zero_packets,limits).width==2,"zero-length packets consume exact command budget");
    limits.commands=4;rejects([&]{decode(zero_packets,limits);});
    auto nonzero_transparent=sprite(frame(3,1,3,row(word(3,1)+word(6,1)+octets({2,241}))));
    set(nonzero_transparent,26,2,2);
    const auto nonzero=decode(nonzero_transparent);
    require(nonzero.pixels==std::vector<std::array<std::uint8_t,4>>{
            {90,100,110,0},{90,100,110,255},{0,0,0,0}} &&
            nonzero.indices==std::vector<std::uint8_t>{2,2,0} &&
            nonzero.z==std::vector<std::uint8_t>{255,0,255},
            "transparent RGB/index differs from opaque same index and unencoded row tail");
    auto final_index=sprite(frame(2,1,3,row(word(1,1)+octets({17,255})+word(3,1))));
    set(final_index,26,255,2);
    const auto full=decode(final_index,{},0,palette(palt(256)));
    require(full.indices==std::vector<std::uint8_t>{255,255} &&
            full.pixels[0][3]==255 && full.pixels[1][3]==0 &&
            full.z==std::vector<std::uint8_t>{17,255},"full palette supports index 255");

    // Offset order remains the original frame table. Aliases share one extent.
    const auto small=frame(1,1,1,row(word(6,1)+octets({1,253})));
    const auto large=frame(2,1,1,row(opaque_packets));
    std::string aliases(24,'\0');set(aliases,0,1000);set(aliases,4,3);set(aliases,8,2);
    set(aliases,12,24+small.size());set(aliases,16,24);set(aliases,20,24+small.size());
    aliases+=small+large;
    require(decode(aliases,{},0).width==2 && decode(aliases,{},1).width==1 &&
            decode(aliases,{},2).pixels==decode(aliases,{},0).pixels,
            "unsorted alias offsets preserve original indices and bound unique extents");
    auto overlaps=aliases;set(overlaps,16,25+small.size());rejects([&]{decode(overlaps);});
    auto unselected=aliases;set(unselected,16,aliases.size()-15);rejects([&]{decode(unselected);});
    rejects([&]{decode(aliases,{},3);});

    for(unsigned flags:{0U,2U,4U,6U,8U,9U,0xFFFFFFFFU})
        rejects([&]{decode(sprite(frame(2,1,flags,row(opaque_packets))));});
    for(unsigned flags:{1U,3U,5U})
        rejects([&]{decode(sprite(frame(1,1,flags,row(word(2,1)+octets({2,1,31,0})))));});
    for(unsigned flags:{1U,5U})
        rejects([&]{decode(sprite(frame(1,1,flags,row(word(1,1)+octets({2,1})))));});
    for(unsigned operation:{0U,4U,5U,7U})
        rejects([&]{decode(sprite(frame(1,1,7,row(word(operation,0)))));});
    for(unsigned operation:{1U,2U,3U,6U,7U})
        rejects([&]{decode(sprite(frame(1,1,7,word(operation,0))));});
    for(const auto& data:std::vector<std::string>{
            "",word(0,0),word(0,1),word(0,3)+octets({0}),word(4,2),
            row(word(3,2)),row(word(1,1)+octets({7})),
            row(word(2,1)+octets({7,1,32,0})),row(word(2,1)+octets({7,1,31})),
            row(word(6,1)+octets({1})),row(word(6,1)+octets({3,0})),
            row(word(1,1)+octets({7,3})),row(word(3,1)+octets({0})),
            word(0,6)+word(3,1),row(word(3,1))+row("")})
        rejects([&]{decode(sprite(frame(1,1,7,data)));});
    for(unsigned transparent:{3U,256U,65535U}) {
        auto bad=bytes;set(bad,26,transparent,2);rejects([&]{decode(bad);});
    }
    auto missing=colors;missing.colors.clear();rejects([&]{decode(bytes,{},0,missing);});
    auto oversized=colors;oversized.colors.resize(257);rejects([&]{decode(bytes,{},0,oversized);});
    for(const auto [at,value]:std::vector<std::pair<unsigned,std::uint32_t>>{
        {0,0},{0,1001},{4,0},{4,0xFFFFFFFFU},{12,15},{12,0xFFFFFFFFU}}) {
        auto bad=bytes;set(bad,at,value);rejects([&]{decode(bad);});
    }
    std::cout<<checks<<" SPR2/PALT checks passed (synthetic fixtures only)\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"SPR2/PALT test failed: "<<e.what()<<'\n'; return 1; }}
