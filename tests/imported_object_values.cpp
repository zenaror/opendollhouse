// SPDX-License-Identifier: MPL-2.0
#include "imported_object_values.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
using namespace odh;using namespace odh_host;
void check(bool value){if(!value)throw std::runtime_error("imported object values assertion failed");}
template<class F>void rejects(F action){try{action();}catch(const FormatError&){return;}throw std::runtime_error("expected importer rejection");}
void at(std::string& bytes,std::size_t offset,std::uint32_t value,unsigned n=2,bool big=false){
    for(unsigned i=0;i<n;++i)bytes.at(offset+i)=char(value>>(8*(big?n-1-i:i)));
}
struct Def {std::uint16_t chunk;std::string bytes;};
Def def(unsigned chunk,unsigned guid,unsigned master,unsigned sub,unsigned price,unsigned lead=0){
    std::string bytes(190,'\0');at(bytes,0,138,4);at(bytes,18,4);at(bytes,20,master);at(bytes,22,sub);
    at(bytes,28,guid,4);at(bytes,36,price);at(bytes,126,lead);return {std::uint16_t(chunk),std::move(bytes)};
}
std::string iff(const std::vector<Def>& defs){
    std::string bytes="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";bytes.resize(64,'\0');
    for(const auto& d:defs){std::string chunk(76,'\0');chunk.replace(0,4,"OBJD");at(chunk,4,76+d.bytes.size(),4,true);at(chunk,8,d.chunk,2,true);bytes+=chunk+d.bytes;}
    return bytes;
}
void write(const std::filesystem::path& file,const std::string& bytes){std::ofstream out(file,std::ios::binary);out.write(bytes.data(),bytes.size());check(bool(out));}
struct Temp {std::filesystem::path root;Temp(){root=std::filesystem::temp_directory_path()/
    ("odh-import-values-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(root);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(root,error);}};
std::vector<DefinitionCandidate> candidates(const std::filesystem::path& file,const std::vector<Def>& defs,std::string asset={}){
    std::vector<DefinitionCandidate> result;for(const auto& d:defs){std::istringstream in(d.bytes);
        result.push_back({file.string(),asset,d.chunk,read_object_definition(View(in,d.bytes.size()))});}return result;
}
SavedWorld imported(const DefinitionIndex& index,bool multipart=true){
    SavedWorld saved;saved.world=std::make_unique<World>();
    const auto add=[&](int id,unsigned guid,int x,int y,int level){
        const auto* c=index.unique(guid);check(c);const auto* master=multipart?index.unique_master(*c):nullptr;
        SavedWorldEntity r{};r.saved.object_id=id;r.saved.guid=guid;r.definition=c->definition;
        r.saved.prefix.object_data[3]=-1;r.saved.prefix.object_data[11]=id;r.saved.prefix.object_data[41]=999;
        r.saved.prefix.object_data[43]=1; // Runtime Disabled is independent of this saved word.
        if(multipart)r.tail.multitile=ObjmMultitileState{0,0,0,x,y,level,static_cast<std::int16_t>(id==5?0:5)};
        check(saved.world->create_saved_entity(id,EntityKind::object,guid,{}, {},{},r.saved.prefix.object_data,{},0,
            master?std::optional<std::uint32_t>{master->definition.guid}:std::nullopt));
        r.imported_generation=saved.world->find(id)->generation;saved.records.push_back(r);
    };
    if(multipart){add(5,105,0,0,0);add(3,103,2,-3,1);add(1,102,-1,2,-1);
        saved.multitile_groups.push_back({5,{}, {5,3,1}});}
    else add(8,201,0,0,0);
    return saved;
}
void cases(){
    Temp temp;const auto file=temp.root/"group.iff";
    const std::vector<Def> defs{def(10,101,42,0xffff,25),def(11,102,42,0,1),
        def(20,104,77,0xffff,65),def(21,103,77,0x02fd,2,7),def(12,105,42,0,3),def(30,201,0,0,37)};
    const auto bytes=iff(defs);write(file,bytes);DefinitionIndex index(candidates(file,defs));auto saved=imported(index);
    auto key=[](const std::string&){return std::string("Objects/group.iff");};
    const auto result=prepare_imported_object_values(saved,index,key);check(result.version==2&&result.objects.empty()&&result.groups.size()==1&&result.revision==1);
    const auto& group=result.groups[0];check(group.multitile&&group.initial_price==25&&group.base&&group.base->id==3);
    // The authenticated source chain is 5 -> 3 -> 1, not numeric ID order.
    check(group.members.size()==3&&group.members[0].object.id==5&&group.members[1].object.id==3&&group.members[2].object.id==1);
    check(group.members[1].offset_x==32&&group.members[1].offset_y==-48&&group.members[1].offset_level==1);
    check(group.members[2].offset_x==-16&&group.members[2].offset_y==32&&group.members[2].offset_level==-1);
    check(group.members[1].object.master_guid==104&&group.members[2].object.master_guid==101);
    check(saved.multitile_groups[0].members==std::vector<std::int16_t>({5,3,1}));
    auto different_order=imported(index);different_order.multitile_groups[0].members={5,1,3};
    const auto reordered=prepare_imported_object_values(different_order,index,key);
    check(reordered.groups[0].members[0].object.id==5&&reordered.groups[0].members[1].object.id==1&&
        reordered.groups[0].members[2].object.id==3&&reordered.groups[0].members[1].offset_x==-16&&
        reordered.groups[0].members[2].offset_y==-48&&reordered.groups[0].base->id==3&&
        reordered.groups[0].initial_price==25&&reordered.groups[0].price_origin.objd_sha256==group.price_origin.objd_sha256);
    check(group.price_origin.guid==101&&group.price_origin.source=="Objects/group.iff"&&group.price_origin.kind==SessionObjectPriceSource::fresh_import_objd);
    check(group.price_origin.objd_sha256==sha256(defs[0].bytes.data(),defs[0].bytes.size()));
    for(const auto& member:group.members){check(member.wear_known&&member.wear==0&&member.disabled==0);check(saved.world->find(member.object.id)->object_words.at(41)==999&&saved.world->find(member.object.id)->object_words.at(43)==1);}
    auto single=imported(index,false);const auto singleton=prepare_imported_object_values(single,index,key);
    check(singleton.groups.size()==1&&!singleton.groups[0].multitile&&singleton.groups[0].initial_price==37&&singleton.groups[0].base->id==8);
    rejects([&]{prepare_imported_object_values(saved,index,{});});
    rejects([&]{prepare_imported_object_values(saved,index,[](auto&){return std::string("../bad");});});
    auto stale=imported(index);++stale.records[0].imported_generation;rejects([&]{prepare_imported_object_values(stale,index,key);});
    auto incomplete=imported(index);incomplete.multitile_groups[0].members.pop_back();rejects([&]{prepare_imported_object_values(incomplete,index,key);});
    auto repeated=imported(index);repeated.multitile_groups[0].members={5,3,3};rejects([&]{prepare_imported_object_values(repeated,index,key);});
    auto bad_parent=imported(index);bad_parent.records[1].tail.multitile->parent_id=1;rejects([&]{prepare_imported_object_values(bad_parent,index,key);});
    auto excessive=imported(index);excessive.records[0].tail.multitile->group_x=100000;rejects([&]{prepare_imported_object_values(excessive,index,key);});
    auto changed=defs;at(changed[1].bytes,28,999,4);write(file,iff(changed));rejects([&]{prepare_imported_object_values(saved,index,key);});
    write(file,bytes);auto ambiguous=candidates(file,defs);ambiguous.push_back(ambiguous[0]);DefinitionIndex no_master(ambiguous);
    auto uncertain=imported(no_master);const auto unknown=prepare_imported_object_values(uncertain,no_master,key);
    check(!unknown.groups[0].initial_price&&unknown.groups[0].price_origin.kind==SessionObjectPriceSource::unknown);
    // Missing MyLead on the FIRST source member cannot establish BaseObject,
    // even though the later member 3 has MyLead=7 and member 5 has zero offsets.
    // Known price and Wear are not discarded.
    auto truncated=defs;for(auto& d:truncated)if(d.chunk==12)d.bytes.resize(120);
    write(file,iff(truncated));DefinitionIndex short_index(candidates(file,truncated));auto short_saved=imported(short_index);
    const auto partial=prepare_imported_object_values(short_saved,short_index,key);
    check(!partial.groups[0].base&&partial.groups[0].initial_price==25);
    auto later_truncated=defs;for(auto& d:later_truncated)if(d.chunk==11)d.bytes.resize(120);
    write(file,iff(later_truncated));DefinitionIndex later_index(candidates(file,later_truncated));auto later_saved=imported(later_index);
    const auto later_partial=prepare_imported_object_values(later_saved,later_index,key);
    check(later_partial.groups[0].base&&later_partial.groups[0].base->id==3&&later_partial.groups[0].initial_price==25);
    const auto archive=temp.root/"objects.far";std::string far(16,'\0');far.replace(0,8,"FAR!byAZ");at(far,8,1,4);far+=bytes;at(far,12,far.size(),4);
    std::string manifest(20,'\0');at(manifest,0,1,4);at(manifest,4,bytes.size(),4);at(manifest,8,bytes.size(),4);at(manifest,12,16,4);
    const std::string asset="Group.iff";at(manifest,16,asset.size(),4);far+=manifest+asset;write(archive,far);
    DefinitionIndex packed(candidates(archive,defs,asset));auto packed_saved=imported(packed);
    check(prepare_imported_object_values(packed_saved,packed,key).groups[0].initial_price==25);
    DefinitionIndex missing(candidates(archive,defs,"Missing.iff"));auto missing_saved=imported(missing);
    rejects([&]{prepare_imported_object_values(missing_saved,missing,key);});
}
void constructed_cases(){
    Temp temp;const auto file=temp.root/"constructed.iff";
    // Actual IFF order differs from numeric runtime IDs, and only the abstract
    // requested master supplies the group's single initial catalog price.
    std::vector<Def> defs{def(11,302,42,0xff02,3),def(10,301,42,0xffff,80),def(12,303,42,0,7,1)};
    at(defs[1].bytes,80,1);write(file,iff(defs));DefinitionIndex index(candidates(file,defs));
    World world;check(world.create_runtime_object(8,302,{0,0,0},{},301)&&world.create_runtime_object(2,303,{0,0,0},{},301));
    ObjectGroup physical{301,true,2,{{8,world.find(8)->generation,-16,32,0},{2,world.find(2)->generation,0,0,0}}};
    auto key=[](const std::string&){return std::string("Objects/constructed.iff");};
    const auto group=prepare_constructed_object_values(world,index,physical,key);
    check(group.group_id==0&&group.multitile&&group.initial_price==80&&group.base&&group.base->id==2);
    check(group.price_origin.kind==SessionObjectPriceSource::constructed_catalog&&group.price_origin.guid==301&&
        group.price_origin.objd_sha256==sha256(defs[1].bytes.data(),defs[1].bytes.size()));
    check(group.members[0].object.id==8&&group.members[0].object.guid==302&&group.members[1].object.id==2&&
        group.members[0].offset_x==-16&&group.members[0].offset_y==32&&group.members[1].lead_object==1);
    for(const auto& member:group.members)check(member.wear_known&&member.wear==0&&member.disabled==0&&member.object.master_guid==301&&
        member.object.source=="Objects/constructed.iff"&&member.object.asset.empty());
    const auto raw_before=world.find(8)->object_words.at(41);
    at(defs[1].bytes,80,0);write(file,iff(defs));
    check(prepare_constructed_object_values(world,index,physical,key).initial_price==0&&world.find(8)->object_words.at(41)==raw_before);
    // Raw69 alone lists the source item too; this is not the buy UI's category
    // filter. Raw16 excludes it even when a category exists.
    at(defs[1].bytes,138,1);write(file,iff(defs));
    DefinitionIndex build_index(candidates(file,defs));
    check(prepare_constructed_object_values(world,build_index,physical,key).initial_price==80);
    at(defs[1].bytes,32,1);write(file,iff(defs));
    const auto excluded=prepare_constructed_object_values(world,build_index,physical,key);
    check(excluded.initial_price==0&&excluded.members[0].disabled==0&&excluded.members[1].disabled==0);
    at(defs[1].bytes,32,0);at(defs[1].bytes,138,0);at(defs[1].bytes,80,1);write(file,iff(defs));
    auto reordered=physical;std::reverse(reordered.members.begin(),reordered.members.end());
    rejects([&]{prepare_constructed_object_values(world,index,reordered,key);});
    auto incomplete=physical;incomplete.members.pop_back();incomplete.base_id=8;
    rejects([&]{prepare_constructed_object_values(world,index,incomplete,key);});
    auto stale=physical;++stale.members[0].generation;rejects([&]{prepare_constructed_object_values(world,index,stale,key);});
    auto ambiguous=candidates(file,defs);ambiguous.push_back(ambiguous[1]);DefinitionIndex duplicate(ambiguous);
    rejects([&]{prepare_constructed_object_values(world,duplicate,physical,key);});
    // Direct creation of a subpart keeps its own raw offsets and no master
    // override; its catalog entry is known absent despite a nonzero raw price.
    World direct;check(direct.create_runtime_object(9,302,{0,0,0},{},{}));
    ObjectGroup subpart{302,false,9,{{9,direct.find(9)->generation,-16,32,0}}};
    const auto selected=prepare_constructed_object_values(direct,index,subpart,key);
    check(!selected.multitile&&selected.members.size()==1&&!selected.members[0].object.master_guid&&selected.initial_price==0);
    auto singles=std::vector<Def>{def(20,401,0,0,53)};at(singles[0].bytes,80,1);at(singles[0].bytes,8,1);
    write(file,iff(singles));DefinitionIndex single_index(candidates(file,singles));World singleton;
    check(singleton.create_runtime_object(10,401,{0,0,0},{},{}));ObjectGroup one{401,false,10,{{10,singleton.find(10)->generation,0,0,0}}};
    check(prepare_constructed_object_values(singleton,single_index,one,key).initial_price==53);
    at(singles[0].bytes,8,0);write(file,iff(singles));
    check(prepare_constructed_object_values(singleton,single_index,one,key).initial_price==0);
}
}
int main(){try{cases();constructed_cases();std::cout<<"imported object values checks passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
