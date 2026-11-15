// SPDX-License-Identifier: MPL-2.0
#include "reference_patch_setup.hpp"
#include "odh/sha256.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace odh_host;
namespace fs=std::filesystem;
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void write(const fs::path& p,const std::string& b){std::ofstream f(p,std::ios::binary);f<<b;f.close();need(bool(f),"write fixture");}
std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
std::string hash(const std::string& b){std::string out;for(auto v:odh::sha256(b.data(),b.size())){
    out+="0123456789abcdef"[v>>4];out+="0123456789abcdef"[v&15];}return out;}
template<class F>void rejects(F f,const char* why){try{f();}catch(const std::exception&){std::cout<<"PASS "<<why<<'\n';return;}throw std::runtime_error(why);}
std::map<std::string,std::string> tree(const fs::path& root){
    std::map<std::string,std::string> out;for(auto& e:fs::recursive_directory_iterator(root))
        out[e.path().lexically_relative(root).generic_string()]=e.is_directory()?"directory":read(e.path());return out;}
int main(int argc,char** argv){try{
    need(argc==2 || argc==3,"pass a disposable directory prefix; optional second argument is private real source");
    fs::path base;
    for(unsigned i=0;i<128;++i){auto candidate=fs::path(std::string(argv[1])+"-"+std::to_string(i));
        if(fs::create_directory(candidate)){base=candidate;break;}}
    need(!base.empty(),"test needs an unused disposable directory slot");
    const auto save=base/"profile";const auto source=base/"source";
    fs::create_directory(save);fs::create_directory(source);
    auto ids=reference_patch_identities();std::array<std::string,4> payload;
    for(size_t i=0;i<4;++i){payload[i]="synthetic PIFF test "+std::to_string(i);ids[i].bytes=payload[i].size();ids[i].sha256=hash(payload[i]);write(source/ids[i].name,payload[i]);}
    const auto before=tree(source);
    need(prepare_reference_patches(save,{}, {},ids).root.empty(),"absence of a selection is explicit and unpatched");
    const auto selection=prepare_reference_patches(save,source,{},ids);
    need(!selection.root.empty() && tree(source)==before,"import authenticates and copies without editing source");
    for(size_t i=0;i<4;++i)need(read(selection.root/ids[i].name)==payload[i],"copied bytes are exact");
    need(prepare_reference_patches(save,{}, {},ids).root==selection.root,"reopen needs no source/import argument");
    const auto persisted=tree(save);
    need(prepare_reference_patches(save,source,{},ids).root==selection.root && tree(save)==persisted,"reimport is idempotent");
    const auto relocated=base/"moved-profile";fs::copy(save,relocated,fs::copy_options::recursive);
    need(prepare_reference_patches(relocated,{}, {},ids).root==fs::canonical(relocated/".opendollhouse/resource-patches/reference-v1"),"relative selection survives profile relocation");
    const auto override=base/"external";fs::create_directory(override);
    need(prepare_reference_patches(save,{},override,ids).root==fs::canonical(override) && tree(save)==persisted,"override is per run and preserves selection");
    rejects([&]{prepare_reference_patches(save,source,override,ids);},"import and override conflict refuses without loss");
    need(tree(save)==persisted,"conflict did not modify profile");
    write(source/ids[0].name,"bad");
    rejects([&]{prepare_reference_patches(save,source,{},ids);},"wrong-size import refuses");
    need(tree(save)==persisted,"invalid import preserves previously selected pack");
    write(source/ids[0].name,std::string(payload[0].size(),'x'));
    rejects([&]{prepare_reference_patches(save,source,{},ids);},"same-size changed SHA refuses");
    need(tree(save)==persisted,"hash refusal preserves complete profile");
    write(source/ids[0].name,payload[0]);write(source/"unexpected.piff","extra");
    rejects([&]{prepare_reference_patches(save,source,{},ids);},"unknown PIFF name refuses instead of extending known pack");
    fs::remove(source/"unexpected.piff"); // synthetic fixture owned by this test
    fs::rename(selection.root/ids[0].name,selection.root/"removed-test-member");
    const auto damaged=tree(save);
    rejects([&]{prepare_reference_patches(save,{}, {},ids);},"missing selected member never falls back unpatched");
    rejects([&]{prepare_reference_patches(save,source,{},ids);},"reimport refuses a divergent existing destination");
    need(tree(save)==damaged,"divergent destination is preserved intact");
    need(prepare_reference_patches(save,{},override,ids).root==fs::canonical(override),"explicit override remains usable despite unavailable persisted pack");
    fs::rename(selection.root/"removed-test-member",selection.root/ids[0].name);
    write(selection.root/ids[1].name,std::string(payload[1].size(),'x'));
    rejects([&]{prepare_reference_patches(save,{}, {},ids);},"altered selected bytes are diagnosed on reopen");
    write(selection.root/ids[1].name,payload[1]);
    const auto selector=save/".opendollhouse/reference-patches.txt";write(selector,"../escape\n");
    const auto malformed=tree(save);
    rejects([&]{prepare_reference_patches(save,{}, {},ids);},"malformed selection refuses");
    rejects([&]{prepare_reference_patches(save,source,{},ids);},"import does not silently repair malformed selection");
    need(tree(save)==malformed,"malformed selection refusal preserves bytes");
    const auto repo=base/"repo";fs::create_directories(repo/"Client/Simitone/Simitone.Client/Content");
    fs::copy(source,repo/"Client/Simitone/Simitone.Client/Content/TS1Patch",fs::copy_options::recursive);
    const auto other=base/"other-profile";fs::create_directory(other);
    need(!prepare_reference_patches(other,repo,{},ids).root.empty(),"chosen repository root resolves only the known nested source");
    // Refusal must precede any metadata/staging writes into a chosen source.
    const auto direct_before=tree(source);
    rejects([&]{prepare_reference_patches(source,source,{},ids);},"source equal to save root refuses");
    need(tree(source)==direct_before,"equal-root refusal leaves source byte-for-byte unchanged");
    rejects([&]{validate_reference_patch_source(source/"not-created/profile",source,ids);},"prospective descendant profile refuses before frontend mkdir");
    need(tree(source)==direct_before,"preflight refusal never creates a directory inside source");
    fs::create_directory(source/"nested-profile");
    const auto nested_before=tree(source);
    rejects([&]{prepare_reference_patches(source/"nested-profile",source,{},ids);},"save root below direct source refuses");
    need(tree(source)==nested_before,"descendant refusal leaves chosen source unchanged");
    const auto whole_before=tree(base);
    rejects([&]{prepare_reference_patches(base,source,{},ids);},"save root containing source refuses");
    need(tree(base)==whole_before,"ancestor refusal does not create profile metadata");
    fs::create_directory(repo/"profile-outside-patch-folder");
    const auto repo_before=tree(repo);
    rejects([&]{prepare_reference_patches(repo/"profile-outside-patch-folder",repo,{},ids);},"profile inside chosen repo but outside TS1Patch refuses");
    need(tree(repo)==repo_before,"chosen whole repository remains unchanged on refusal");
    const auto nested_patch=repo/"Client/Simitone/Simitone.Client/Content/TS1Patch";
    rejects([&]{prepare_reference_patches(nested_patch,repo,{},ids);},"profile equal to resolved nested TS1Patch refuses");
    need(tree(repo)==repo_before,"resolved-source refusal leaves repository unchanged");
    // Optional PRIVATE gate: no commercial/patch bytes in tracked fixtures.
    if(argc==3){const fs::path real=base/"private-profile";fs::create_directory(real);
        const auto selected=prepare_reference_patches(real,argv[2]);
        need(!selected.root.empty() && prepare_reference_patches(real).root==selected.root,"private pinned files import and reopen with production identities");
        std::cout<<"PASS private production four-blob gate\n";
    }else std::cout<<"NOT RUN private production four-blob gate\n";
    std::cout<<"PASS reference patch import/reopen/relocation/override/refusal matrix\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
