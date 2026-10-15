#include "xa_patch_catalog.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const odh::FormatError&){failed=true;}require(failed,"expected XA catalog rejection");}
void put(std::string& b,std::uint32_t value){for(unsigned n=0;n<4;++n)b.push_back(static_cast<char>((value>>(n*8))&255));}
std::string far(const std::vector<std::pair<std::string,std::string>>& entries){
    std::string b="FAR!byAZ";put(b,1);put(b,0);std::vector<std::uint32_t> offsets;
    for(const auto& e:entries){offsets.push_back(static_cast<std::uint32_t>(b.size()));b+=e.second;}
    const auto index=static_cast<std::uint32_t>(b.size());
    for(unsigned n=0;n<4;++n)b[12+n]=static_cast<char>((index>>(n*8))&255);
    put(b,static_cast<std::uint32_t>(entries.size()));
    for(std::size_t n=0;n<entries.size();++n){const auto& e=entries[n];put(b,static_cast<std::uint32_t>(e.second.size()));put(b,static_cast<std::uint32_t>(e.second.size()));put(b,offsets[n]);put(b,static_cast<std::uint32_t>(e.first.size()));b+=e.first;}
    return b;
}
void write(const std::string& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();if(!out)throw std::runtime_error("fixture write failed");}
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("test requires private fixture directory");
    const auto root=std::filesystem::path(argv[1]);
    const auto a=(root/"a.far").string(),b=(root/"b.far").string(),loose=(root/"CASE.Xa").string();
    std::string pcm="XAI";pcm.push_back(0);put(pcm,6);
    pcm+=std::string("\1\0\1\0",4);put(pcm,22050);put(pcm,44100);pcm+=std::string("\2\0\20\0",4);
    pcm.push_back(0);pcm.push_back(0x12);pcm.push_back(0x30);pcm+=std::string(12,'\0');
    const auto archive=far({{"Voice\\Case.XA",pcm+"tail"},{"ignored.bin","not audio"}});
    write(a,archive);write(b,far({{"other/CASE.xa",pcm}}));write(loose,pcm);
    odh::Hot hot;hot.patches[1]={"case","sounddata\\voice\\case.xa",false,false};
    using namespace odh_host;
    const std::vector<XaPatchSource> one{{a,XaSourceKind::far}};
    XaPatchCatalog catalog(one);
    const auto usage=catalog.usage();
    require(usage.sources==1 && usage.entries==2 && usage.xa_entries==1,"bounded full FAR index, only XA candidates");
    auto asset=catalog.load(hot,1);
    rejects([&]{catalog.load(hot,1,5);});rejects([&]{catalog.load(hot,1,0);});
    require(catalog.load(hot,1,6).audio.samples==asset.audio.samples,"caller admission allowance accepts exact PCM budget");
    require(asset.audio.samples==std::vector<std::int16_t>({4096,8192,12288}),"exact declared PCM samples, not block padding");
    require(asset.audio.sample_rate==22050 && asset.audio.channels==1 && asset.audio.remaining_bytes==4,"decode metadata/tail");
    require(asset.key=="case.xa" && asset.entry=="Voice\\Case.XA" && asset.offset==16 && asset.length==pcm.size()+4 && asset.source.path==a,"archive provenance retained");
    hot.patches[1].filename="unrelated/prefix/CASE.Xa";
    require(catalog.load(hot,1).audio.samples==asset.audio.samples,"both slash styles and case folded basename");
    XaPatchCatalog files({{loose,XaSourceKind::loose}});
    const auto raw=files.load(hot,1);
    require(raw.entry.empty() && raw.offset==0 && raw.length==pcm.size(),"loose source provenance");
    const auto cwd=std::filesystem::current_path();
    struct RestoreCwd {std::filesystem::path path;~RestoreCwd(){std::error_code ec;std::filesystem::current_path(path,ec);}} restore{cwd};
    std::filesystem::current_path(root);
    XaPatchCatalog relative({{"a.far",XaSourceKind::far}});
    std::filesystem::current_path(cwd);
    require(relative.load(hot,1).audio.samples==asset.audio.samples,"relative input remains bound after cwd change");
    for(bool reverse:{false,true}){
        XaPatchCatalog duplicate(reverse?std::vector<XaPatchSource>{{b,XaSourceKind::far},{a,XaSourceKind::far}}:
            std::vector<XaPatchSource>{{a,XaSourceKind::far},{b,XaSourceKind::far}});
        rejects([&]{duplicate.load(hot,1);});
    }
    XaPatchCatalog mixed({{a,XaSourceKind::far},{loose,XaSourceKind::loose}});
    rejects([&]{mixed.load(hot,1);});
    write(b,far({{"same/CASE.xa",pcm},{"again/case.XA",pcm},{"unique.xa",pcm}}));
    XaPatchCatalog internal({{b,XaSourceKind::far}});
    rejects([&]{internal.load(hot,1);});
    hot.patches[2]={"unique","unique.xa",false,false};
    require(internal.load(hot,2).audio.samples==asset.audio.samples,"unrelated duplicates do not block unique lookup");
    rejects([&]{catalog.load(hot,99);});
    for(const auto& name:std::vector<std::string>{"missing.xa","a.wav","folder/","",std::string("case.xa\0",8)}){
        hot.patches[3]={"bad",name,false,false};rejects([&]{catalog.load(hot,3);});
    }
    XaPatchCatalogLimits bounds;
    bounds.audio.pcm_bytes=5;XaPatchCatalog small(one,bounds);rejects([&]{small.load(hot,1);});
    rejects([&]{small.load(hot,1,1000);});
    bounds={};bounds.audio.input_bytes=pcm.size()+3;XaPatchCatalog short_input(one,bounds);rejects([&]{short_input.load(hot,1);});
    bounds={};bounds.sources=0;rejects([&]{XaPatchCatalog bad(one,bounds);});
    bounds={};bounds.entries=1;rejects([&]{XaPatchCatalog bad(one,bounds);});
    bounds={};bounds.name_bytes=usage.name_bytes-1;rejects([&]{XaPatchCatalog bad(one,bounds);});
    bounds.name_bytes=usage.name_bytes;require(XaPatchCatalog(one,bounds).usage().name_bytes==usage.name_bytes,"exact aggregate name budget");
    bounds={};bounds.name_length=1;rejects([&]{XaPatchCatalog bad(one,bounds);});
    rejects([&]{XaPatchCatalog bad({});});
    rejects([&]{XaPatchCatalog bad({{a,static_cast<XaSourceKind>(99)}});});
    rejects([&]{XaPatchCatalog bad({{a,XaSourceKind::loose}});});
    write(b,far({{"bad.xa","truncated"}}));XaPatchCatalog malformed({{b,XaSourceKind::far}});
    hot.patches[4]={"bad","bad.xa",false,false};rejects([&]{malformed.load(hot,4);});
    write(a,archive+"x");rejects([&]{catalog.load(hot,1);});write(a,archive);
    require(asset.audio.samples.size()==3,"loaded PCM independent of later source changes");
    std::cout<<checks<<" XA patch catalog checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
