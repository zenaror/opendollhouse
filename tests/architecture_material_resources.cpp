// SPDX-License-Identifier: MPL-2.0
// Synthetic archive metadata and payload-view ownership; no rendering claim.
#include "architecture_material_resources.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& action,const char* message){
    bool rejected=false;try{action();}catch(const odh::FormatError&){rejected=true;}
    require(rejected,message);
}
void le(std::string& bytes,std::uint32_t value){
    for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<char>(value>>(i*8)));
}
void be_at(std::string& bytes,std::size_t position,std::uint32_t value,unsigned count){
    for(unsigned i=0;i<count;++i)bytes[position+i]=static_cast<char>(value>>((count-i-1)*8));
}
std::string iff(const std::string& payload){
    std::string bytes="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    bytes.resize(64,0);std::string chunk(76,0);chunk.replace(0,4,"TEST");
    be_at(chunk,4,76+payload.size(),4);be_at(chunk,8,7,2);
    return bytes+chunk+payload;
}
std::string far(const std::vector<std::pair<std::string,std::string>>& entries){
    std::string bytes="FAR!byAZ";le(bytes,1);le(bytes,0);
    std::vector<std::uint32_t> offsets;
    for(const auto& entry:entries){offsets.push_back(static_cast<std::uint32_t>(bytes.size()));bytes+=entry.second;}
    const auto manifest=static_cast<std::uint32_t>(bytes.size());
    for(unsigned i=0;i<4;++i)bytes[12+i]=static_cast<char>(manifest>>(i*8));
    le(bytes,static_cast<std::uint32_t>(entries.size()));
    for(std::size_t i=0;i<entries.size();++i){
        le(bytes,static_cast<std::uint32_t>(entries[i].second.size()));
        le(bytes,static_cast<std::uint32_t>(entries[i].second.size()));
        le(bytes,offsets[i]);le(bytes,static_cast<std::uint32_t>(entries[i].first.size()));bytes+=entries[i].first;
    }
    return bytes;
}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("odh-material-source-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){std::filesystem::create_directory(path);}
    ~Temp(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
void write(const std::filesystem::path& path,const std::string& bytes){
    std::ofstream stream(path,std::ios::binary);stream.write(bytes.data(),bytes.size());
    require(bool(stream),"fixture write failed");
}
std::string payload(const ArchitectureMaterialAsset& asset){
    const auto& view=asset.require("TEST",7);std::string result(view.size(),0);
    view.read(0,result.data(),result.size());return result;
}
}
int main(){try{
    using namespace odh;
    Temp temp;const auto file=temp.path/"materials.far";
    const auto original=far({{"a.flr",iff("floor")},{"b.wll",iff("wall")}});write(file,original);
    const auto catalog=discover_architecture_resources({{file.string(),ArchitectureResourceSourceKind::far1_archive}});
    require(catalog.resources.size()==2,"fixture catalog shape");
    ArchitectureMaterialResources resources(catalog);
    const auto& floor=resources.record(ArchitectureResourceFamily::floor,256);
    const auto& wall=resources.record(ArchitectureResourceFamily::wall,256);
    auto first=resources.open(floor,16);require(payload(first)=="floor","first payload differs");
    for(unsigned i=0;i<8;++i)require(payload(resources.open(i%2?floor:wall,16))==(i%2?"floor":"wall"),"cached archive locator selected wrong material");
    auto stats=resources.cache_stats();
    require(stats.manifest_reads==1&&stats.manifest_hits==8&&stats.retained_entries==2&&stats.retained_name_bytes==10,
        "unchanged archive was indexed more than once or accounting differs");
    require(payload(first)=="floor","other opens invalidated an existing payload View");
    auto wrong=floor;wrong.source.entry_name="z.flr";
    rejects([&]{resources.open(wrong,16);},"cached name mismatch accepted");
    wrong=floor;++wrong.source.offset;
    rejects([&]{resources.open(wrong,16);},"cached offset mismatch accepted");
    wrong=floor;++wrong.source.size;
    rejects([&]{resources.open(wrong,16);},"cached size mismatch accepted");
    wrong=floor;wrong.source.entry_index=100;
    rejects([&]{resources.open(wrong,16);},"cached entry index mismatch accepted");
    rejects([&]{resources.open(floor,0);},"cached manifest bypassed per-open IFF chunk limit");

    // Force distinct modification times; no sleep or filesystem timestamp
    // granularity assumption is needed for a same-size replacement.
    const auto initial_time=std::filesystem::last_write_time(file);
    write(file,far({{"z.flr",iff("floor")},{"b.wll",iff("wall")}}));
    require(std::filesystem::file_size(file)==original.size(),"same-size replacement fixture differs");
    std::filesystem::last_write_time(file,initial_time+std::chrono::seconds(2));
    rejects([&]{resources.open(floor,16);},"same-size changed manifest reused stale locator");
    require(resources.cache_stats().manifest_reads==2,"same-size modified source was not reindexed");
    write(file,original);std::filesystem::last_write_time(file,initial_time+std::chrono::seconds(4));
    require(payload(resources.open(floor,16))=="floor"&&resources.cache_stats().manifest_reads==3,"restored manifest not revalidated");
    write(file,far({{"a.flr",iff("floor")},{"b.wll",iff("wall")},{"extra.iff",iff("new")}}));
    // Keep the previous timestamp: size alone must invalidate the cache.
    std::filesystem::last_write_time(file,initial_time+std::chrono::seconds(4));
    require(payload(resources.open(floor,16))=="floor"&&resources.cache_stats().manifest_reads==4&&resources.cache_stats().retained_entries==3,
        "changed size did not invalidate/account for the manifest");
    write(file,"not a FAR");
    rejects([&]{resources.open(floor,16);},"malformed replacement accepted");
    require(resources.cache_stats().retained_entries==0&&resources.cache_stats().retained_name_bytes==0,"failed replacement retained stale manifest");

    write(file,original);
    ArchitectureMaterialResources entry_budget(catalog,{1,1024});
    require(payload(entry_budget.open(floor,16))=="floor"&&payload(entry_budget.open(wall,16))=="wall",
        "small cache rejected a valid material instead of parsing uncached");
    require(entry_budget.cache_stats().retained_entries==0&&entry_budget.cache_stats().manifest_reads==2,
        "oversized per-cache entry index was retained");
    ArchitectureMaterialResources name_budget(catalog,{100,1});
    require(payload(name_budget.open(floor,16))=="floor","small name cache rejected a valid material");
    require(name_budget.cache_stats().retained_name_bytes==0,"oversized per-cache names were retained");

    const auto one=temp.path/"one.far",two=temp.path/"two.far";
    write(one,far({{"one.flr",iff("one")}}));write(two,far({{"two.wll",iff("two")}}));
    const auto two_catalog=discover_architecture_resources({{one.string(),ArchitectureResourceSourceKind::far1_archive},
        {two.string(),ArchitectureResourceSourceKind::far1_archive}});
    ArchitectureMaterialResources cumulative(two_catalog,{1,1024});
    require(payload(cumulative.open(two_catalog.resources[0],16))=="one","first cache source failed");
    require(payload(cumulative.open(two_catalog.resources[1],16))=="two"&&cumulative.cache_stats().retained_entries==1,
        "cumulative capacity did not evict before accepting another valid source");
    require(payload(cumulative.open(two_catalog.resources[1],16))=="two"&&cumulative.cache_stats().manifest_hits==1,
        "newly admitted source was not cached");
    require(payload(cumulative.open(two_catalog.resources[0],16))=="one"&&cumulative.cache_stats().manifest_reads==3&&
        cumulative.cache_stats().retained_entries==1,"evicted source did not reparse within bounded capacity");

    const auto loose=temp.path/"loose.flr";write(loose,iff("loose"));
    const auto loose_catalog=discover_architecture_resources({{loose.string(),ArchitectureResourceSourceKind::loose_file}});
    ArchitectureMaterialResources loose_resources(loose_catalog);
    require(payload(loose_resources.open(loose_catalog.resources[0],16))=="loose"&&loose_resources.cache_stats().manifest_reads==0,
        "loose material incorrectly used FAR cache");
    write(loose,iff("different length"));
    rejects([&]{loose_resources.open(loose_catalog.resources[0],16);},"changed loose extent accepted");

    // Assets retain their own stream, independently from cache/provider life.
    ArchitectureMaterialAsset surviving;
    {ArchitectureMaterialResources temporary(catalog);surviving=temporary.open(floor,16);}
    require(payload(surviving)=="floor","provider destruction invalidated owned chunk stream");
    std::cout<<"architecture material manifest reuse, replacement, budgets and view lifetime passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
