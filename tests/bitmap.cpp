#include "odh/bitmap.hpp"
#include <iostream>
#include <sstream>

namespace {
int checks=0;
void require(bool value,const char* why) {++checks;if(!value)throw std::runtime_error(why);}
void set(std::string& bytes,std::size_t at,std::uint32_t value,unsigned length=4) {
    for(unsigned i=0;i<length;++i)bytes.at(at+i)=static_cast<char>((value>>(i*8))&255);
}
auto decode(const std::string& bytes,odh::BitmapLimits limits={}) {
    std::istringstream stream(bytes);return odh::read_bmp(odh::View(stream,bytes.size()),limits);
}
template<class F>void rejects(F f) {
    bool failed=false;try{f();}catch(const odh::FormatError&){failed=true;}
    require(failed,"malformed, unsupported or truncated BMP rejected");
}
std::string fixture(unsigned bits,bool top_down=false,unsigned palette_count=3,unsigned gap=0) {
    const unsigned width=3,height=2;
    const unsigned palette=bits==8?palette_count:0;
    const unsigned stride=((width*bits+31)/32)*4;
    const unsigned offset=54+palette*4+gap;
    std::string bytes(offset+stride*height,'\0');
    set(bytes,0,0x4D42,2);set(bytes,2,bytes.size());set(bytes,10,offset);set(bytes,14,40);
    set(bytes,18,width);set(bytes,22,top_down?0xFFFFFFFEU:height);
    set(bytes,26,1,2);set(bytes,28,bits,2);set(bytes,46,palette);
    // RGB triples red, green, blue; reserved palette byte deliberately nonzero.
    for(unsigned i=0;i<palette;++i) {
        const auto at=54+i*4;bytes[at+(2-i%3)]=static_cast<char>(255);
        bytes[at+3]=static_cast<char>(97+i);
    }
    for(unsigned y=0;y<height;++y) {
        const unsigned logical_y=top_down?y:height-1-y;
        for(unsigned x=0;x<width;++x) {
            const auto index=(x+logical_y)%3;
            const auto at=offset+y*stride+x*(bits/8);
            if(bits==8)bytes[at]=static_cast<char>(index);
            else bytes[at+2-index]=static_cast<char>(255);
        }
        // Nonzero row padding must not become a pixel.
        for(unsigned p=width*(bits/8);p<stride;++p)bytes[offset+y*stride+p]=static_cast<char>(231);
    }
    return bytes;
}
}
int main() {try {
    const std::vector<std::array<std::uint8_t,4>> expected={
        {255,0,0,255},{0,255,0,255},{0,0,255,255},
        {0,255,0,255},{0,0,255,255},{255,0,0,255}};
    for(const auto bits:{8U,24U})for(const bool top_down:{false,true}) {
        const auto bytes=fixture(bits,top_down);const auto image=decode(bytes);
        require(image.width==3 && image.height==2 && image.bit_count==bits &&
                image.top_down==top_down && image.remaining_bytes==0,"BMP source metadata retained");
        require(image.pixels==expected,"BGR, row padding and orientation produce opaque RGBA");
        auto exact=bytes;set(exact,34,bytes.size()-(bits==8?66:54));
        require(decode(exact).pixels==expected,"exact nonzero image size accepted");
        require(decode(bytes+"tail").remaining_bytes==4,"tail measured from end of pixels");
        require(decode(fixture(bits,top_down,3,7)).pixels==expected,"offset gap accepted");
        for(std::size_t cut=0;cut<bytes.size();++cut)
            rejects([&]{decode(bytes.substr(0,cut));});
        // Also detect real stream truncation hidden behind a declared View size.
        for(std::size_t cut=0;cut<bytes.size();++cut)rejects([&]{
            std::istringstream stream(bytes.substr(0,cut));
            odh::read_bmp(odh::View(stream,bytes.size()));
        });
        for(unsigned budget=0;budget<3;++budget) {
            odh::BitmapLimits limits;
            if(budget==0)limits.width=2;
            if(budget==1)limits.height=1;
            if(budget==2)limits.pixels=5;
            rejects([&]{decode(bytes,limits);});
        }
        require(decode(bytes,{3,2,6}).pixels==expected,"exact dimension and pixel budgets accepted");
        for(const auto [at,value]:std::vector<std::pair<unsigned,std::uint32_t>>{
            {0,0},{2,0},{2,static_cast<std::uint32_t>(bytes.size()-1)},
            {2,static_cast<std::uint32_t>(bytes.size()+1)}, {10,53}, {10,0xFFFFFFFFU},
            {14,12},{14,108},{18,0},{18,0xFFFFFFFFU},{18,0x80000000U},
            {22,0},{22,0x80000000U},{26,2},{30,1},{30,2},{30,3},{34,1}}) {
            auto bad=bytes;set(bad,at,value,(at==0 || at==26)?2:4);
            rejects([&]{decode(bad);});
        }
        for(unsigned reserved:{6U,8U}) {
            auto bad=bytes;set(bad,reserved,1,2);rejects([&]{decode(bad);});
        }
        for(unsigned unsupported:{1U,4U,16U,32U,0U}) {
            auto bad=bytes;set(bad,28,unsupported,2);rejects([&]{decode(bad);});
        }
        for(const auto [width,height]:std::vector<std::pair<std::uint32_t,std::uint32_t>>{
                {0x7FFFFFFFU,0x7FFFFFFFU},{0x7FFFFFFFU,1U},{1U,0x7FFFFFFFU}}) {
            auto bad=bytes;set(bad,18,width);set(bad,22,height);
            odh::BitmapLimits huge{0xFFFFFFFFU,0xFFFFFFFFU,0xFFFFFFFFU};
            rejects([&]{decode(bad,huge);});
        }
        auto metadata=bytes;set(metadata,38,0xFFFFFFFFU);set(metadata,42,0x80000000U);
        set(metadata,50,0xFFFFFFFFU);
        require(decode(metadata).pixels==expected,"resolution and important-color metadata do not alter pixels");
    }
    auto indexed=fixture(8);set(indexed,10,65);rejects([&]{decode(indexed);});
    indexed=fixture(8);set(indexed,46,257);rejects([&]{decode(indexed);});
    indexed=fixture(8);indexed[66]=3;rejects([&]{decode(indexed);});
    indexed=fixture(8);set(indexed,46,2);rejects([&]{decode(indexed);});
    auto full_palette=fixture(8,false,256);set(full_palette,46,0);
    require(decode(full_palette).pixels==expected,"zero clrUsed selects full 256-entry palette");
    auto single=fixture(8,false,1);
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<3;++x)single[58+y*4+x]=0;
    require(decode(single).pixels==std::vector<std::array<std::uint8_t,4>>(6,{255,0,0,255}),
            "one-entry indexed palette accepted");
    auto rgb=fixture(24);set(rgb,46,1);rejects([&]{decode(rgb);});
    auto with_tail=fixture(24)+"abcd";set(with_tail,2,with_tail.size());
    require(decode(with_tail).remaining_bytes==4,"declared file tail remains visible");
    std::cout<<checks<<" BMP checks passed (synthetic fixtures only)\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"BMP test failed: "<<e.what()<<'\n';return 1;}}
