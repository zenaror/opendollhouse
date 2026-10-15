#include "xa_patch_catalog.hpp"
#include "json.hpp"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
template<class F>auto read(const std::string& path,F parser){
    std::ifstream in(path,std::ios::binary);if(!in)throw odh::FormatError("cannot open resource");
    return parser(odh::View(in,std::filesystem::file_size(path)));
}
int main(int argc,char** argv){try{
    if(argc<6 || (argc-4)%2)throw odh::FormatError("usage: odh-xa-patch HSM HOT PATCH_ID (--far FILE|--xa FILE)...");
    std::uint32_t id{};const std::string text=argv[3];
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),id);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())throw odh::FormatError("invalid decimal patch ID");
    // argv4 is the first source option; each option has exactly one path.
    std::vector<odh_host::XaPatchSource> sources;
    for(int n=4;n<argc;n+=2){
        const std::string option=argv[n];
        if(n+1>=argc || (option!="--far" && option!="--xa"))throw odh::FormatError("invalid XA source option");
        if(sources.size()>=16)throw odh::FormatError("source budget exceeded");
        sources.push_back({argv[n+1],option=="--far"?odh_host::XaSourceKind::far:odh_host::XaSourceKind::loose});
    }
    const auto hsm=read(argv[1],[](const auto& view){return odh::read_hsm(view);});
    const auto hot=read(argv[2],[&](const auto& view){return odh::read_hot(view,&hsm);});
    odh_host::XaPatchCatalog catalog(sources);const auto asset=catalog.load(hot,id);
    const auto usage=catalog.usage();
    std::cout<<"{\"evidence\":\"HOT_patch_XA_decode_only\",\"patch\":"<<id<<",\"key\":"<<json_bytes(asset.key)
       <<",\"source\":"<<json_bytes(asset.source.path)<<",\"entry\":"<<json_bytes(asset.entry)
       <<",\"offset\":"<<asset.offset<<",\"length\":"<<asset.length<<",\"rate\":"<<asset.audio.sample_rate
       <<",\"channels\":"<<asset.audio.channels<<",\"frames\":"<<asset.audio.samples.size()/asset.audio.channels
       <<",\"remaining_bytes\":"<<asset.audio.remaining_bytes<<",\"indexed_entries\":"<<usage.entries
       <<",\"xa_entries\":"<<usage.xa_entries<<",\"name_bytes\":"<<usage.name_bytes<<"}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"odh-xa-patch: "<<e.what()<<'\n';return 2;}}
