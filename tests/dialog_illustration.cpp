// SPDX-License-Identifier: MPL-2.0
// Synthetic IFF/FAR resources exercise the original selector rules, bounded
// decoding and identity checks. They do not certify installed-game artwork.
#include "dialog_illustration.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace odh;using namespace odh_host;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void at(std::string& bytes,std::size_t offset,std::uint32_t value,unsigned width,bool big=false){
    for(unsigned n=0;n<width;++n)bytes.at(offset+n)=static_cast<char>(value>>(8*(big?width-1-n:n)));
}
void put(std::string& bytes,std::uint32_t value,unsigned width){
    for(unsigned n=0;n<width;++n)bytes.push_back(static_cast<char>(value>>(8*n)));
}
std::string bitmap(bool embedded=true){
    std::string bytes(62,'\0');at(bytes,0,0x4d42,2);at(bytes,2,embedded?0:62,4);
    at(bytes,10,54,4);at(bytes,14,40,4);at(bytes,18,2,4);at(bytes,22,1,4);at(bytes,26,1,2);at(bytes,28,24,2);
    bytes[54]=120;bytes[55]=100;bytes[56]=80;bytes[57]=60;bytes[58]=40;bytes[59]=20;return bytes;
}
std::string chunk(std::uint16_t id,const std::string& payload){
    std::string bytes(76,'\0');bytes.replace(0,4,"BMP_");at(bytes,4,static_cast<std::uint32_t>(76+payload.size()),4,true);
    at(bytes,8,id,2,true);return bytes+payload;
}
std::string iff(const std::vector<std::string>& chunks){
    std::string bytes="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    bytes.resize(64,'\0');for(const auto& value:chunks)bytes+=value;return bytes;
}
std::string far(const std::vector<std::pair<std::string,std::string>>& entries){
    std::string bytes="FAR!byAZ";put(bytes,1,4);put(bytes,0,4);std::vector<std::uint32_t> offsets;
    for(const auto& entry:entries){offsets.push_back(static_cast<std::uint32_t>(bytes.size()));bytes+=entry.second;}
    at(bytes,12,static_cast<std::uint32_t>(bytes.size()),4);put(bytes,static_cast<std::uint32_t>(entries.size()),4);
    for(std::size_t n=0;n<entries.size();++n){const auto& entry=entries[n];put(bytes,static_cast<std::uint32_t>(entry.second.size()),4);
        put(bytes,static_cast<std::uint32_t>(entry.second.size()),4);put(bytes,offsets[n],4);
        put(bytes,static_cast<std::uint32_t>(entry.first.size()),4);bytes+=entry.first;}
    return bytes;
}
void write(const std::filesystem::path& path,const std::string& bytes){
    std::ofstream stream(path,std::ios::binary);stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    need(bool(stream),"write isolated dialog illustration fixture");
}
struct TemporaryDirectory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("odh-dialog-illustration-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory(){need(std::filesystem::create_directory(path),"create isolated dialog illustration fixture");}
    ~TemporaryDirectory(){std::error_code ec;std::filesystem::remove_all(path,ec);}
};
void cases(){
    using Kind=DialogIllustrationKind;
    TemporaryDirectory temp;World world;
    need(world.create(1,EntityKind::object)&&world.define_type(1,0x1234),"actual automatic icon subject");
    const auto revision=world.entity_identity_revision();const auto generation=world.find(1)->generation;
    DialogState state;state.instruction.opcode=36;state.instruction.operands[7]=6;
    state.prompt.icon_name="../../this-display-string-must-never-be-opened.iff";
    const auto plan=plan_dialog_illustration(state,world);
    need(plan.kind==Kind::indexed_bitmap&&plan.bitmap_id==5000,"indexed zero selects actual BMP_5000");
    const auto file=temp.path/"scoped.iff",archive=temp.path/"scoped.far";
    const auto bytes=iff({chunk(5000,bitmap()),chunk(5003,bitmap()),chunk(5255,bitmap()),chunk(5002,"unrelated malformed bitmap")});
    write(file,bytes);write(archive,far({{"Exact.iff",bytes}}));
    for(const auto opcode:{36,38,39})for(const auto icon:{0,3,255}){
        state.instruction.opcode=static_cast<std::uint16_t>(opcode);state.instruction.operands[1]=static_cast<std::uint8_t>(icon);
        const auto result=prepare_dialog_illustration(state,world,{},DialogIllustrationResource{file,""});
        need(result.plan.kind==Kind::indexed_bitmap&&result.plan.bitmap_id==5000+icon&&result.image&&
            result.image->width==2&&result.image->height==1&&
            result.image->pixels[0]==std::array<std::uint8_t,4>{80,100,120,255},
            "all dialog scopes use exact indexed chunk, embedded BMP extent and owned pixels");
        const auto packed=prepare_dialog_illustration(state,world,{},DialogIllustrationResource{archive,"Exact.iff"});
        need(packed.image&&packed.image->pixels==result.image->pixels,"same bounded decoder handles exact FAR member");
    }
    state.instruction.operands[1]=1;
    auto result=prepare_dialog_illustration(state,world,{},DialogIllustrationResource{file,""});
    need(result.plan.kind==Kind::unavailable&&!result.image,"missing selected bitmap never falls back to another icon");
    state.instruction.operands[1]=0;
    need(prepare_dialog_illustration(state,world,{},std::nullopt).plan.kind==Kind::unavailable,
        "indexed image requires authenticated scope resource link");
    need(prepare_dialog_illustration(state,world,{},DialogIllustrationResource{archive,"exact.iff"}).plan.kind==Kind::unavailable,
        "FAR image lookup never guesses another member or case-folded precedence");
    need(prepare_dialog_illustration(state,world,{},DialogIllustrationResource{archive,std::string("x\0y",3)}).plan.kind==Kind::unavailable,
        "resource member with embedded NUL cannot be interpreted as a shorter host path");
    auto limits=DialogIllustrationLimits{};limits.bitmap.width=1;
    need(!prepare_dialog_illustration(state,world,{},DialogIllustrationResource{file,""},limits).image,
        "bitmap dimensions obey caller resource budget");
    limits=DialogIllustrationLimits{};limits.iff_bytes=64;
    need(!prepare_dialog_illustration(state,world,{},DialogIllustrationResource{file,""},limits).image,
        "whole selected IFF obeys caller resource budget");
    write(file,iff({chunk(5000,bitmap()),chunk(5000,bitmap())}));
    need(prepare_dialog_illustration(state,world,{},DialogIllustrationResource{file,""}).plan.kind==Kind::unavailable,
        "duplicate exact bitmap ID is ambiguous");
    write(file,iff({chunk(5000,"broken selected bitmap")}));
    need(!prepare_dialog_illustration(state,world,{},DialogIllustrationResource{file,""}).image,
        "malformed selected bitmap cannot become an image");
    const DialogIllustrationResource frozen{temp.path/"not-on-disk.far","Exact.iff",std::make_shared<const std::string>(bytes)};
    const auto pinned=prepare_dialog_illustration(state,world,{},frozen);
    need(pinned.image&&pinned.image->pixels[0]==std::array<std::uint8_t,4>{80,100,120,255}&&
        !std::filesystem::exists(frozen.archive),"authenticated frozen IFF is decoded without reopening its provenance path");
    limits=DialogIllustrationLimits{};limits.iff_bytes=64;
    need(!prepare_dialog_illustration(state,world,{},frozen,limits).image,
        "frozen IFF budget is checked before constructing an owned input stream");
    write(archive,far({{"Exact.iff",bytes},{"Exact.iff",bytes}}));
    need(!prepare_dialog_illustration(state,world,{},DialogIllustrationResource{archive,"Exact.iff"}).image,
        "duplicate FAR identity cannot become an image");
    state.instruction.operands[7]=2;
    need(prepare_dialog_illustration(state,world,{},DialogIllustrationResource{temp.path/"absent","bad"}).plan.kind==Kind::none,
        "explicit no-icon mode does not require or open any resource");
    state.instruction.operands[7]=0;
    need(plan_dialog_illustration(state,world).kind==Kind::unsupported,"automatic unknown is not a fabricated StackObject");
    DialogIllustrationSource source;source.automatic_known=true;
    need(plan_dialog_illustration(state,world,source).kind==Kind::none,"known absent automatic subject remains absent");
    source.automatic_entity=WorldEntityReference{1,generation};
    result=prepare_dialog_illustration(state,world,source,std::nullopt);
    need(result.plan.kind==Kind::entity&&result.plan.entity==source.automatic_entity&&!result.image,
        "actual automatic object returns typed portrait request without creating anything");
    ++source.automatic_entity->generation;
    need(plan_dialog_illustration(state,world,source).kind==Kind::unavailable,"stale automatic generation is rejected");
    source.automatic_neighbor_id=31;
    need(plan_dialog_illustration(state,world,source).kind==Kind::unsupported,"automatic subject cannot ambiguously name both a World entity and neighbor");
    source.automatic_entity.reset();
    need(plan_dialog_illustration(state,world,source).kind==Kind::neighbor&&
        plan_dialog_illustration(state,world,source).neighbor_id==31,
        "automatic $Neighbor requests original character art without inventing a live entity");
    source.automatic_neighbor_id=-1;
    need(plan_dialog_illustration(state,world,source).kind==Kind::unavailable,"invalid automatic neighbor is rejected");
    source.automatic_neighbor_id.reset();
    state.instruction.operands[7]=4;
    need(plan_dialog_illustration(state,world,source).kind==Kind::unsupported,"neighbor mode needs actual neighbor provenance");
    source.neighbor_known=true;
    need(plan_dialog_illustration(state,world,source).kind==Kind::none,"known absent neighbor remains absent");
    source.neighbor_id=31;
    need(plan_dialog_illustration(state,world,source).kind==Kind::neighbor&&
        plan_dialog_illustration(state,world,source).neighbor_id==31,"neighbor remains a typed existing character lookup");
    source.neighbor_id=-1;
    need(plan_dialog_illustration(state,world,source).kind==Kind::unavailable,"invalid neighbor identity is rejected");
    for(const auto mode:{4,5,6,7}){
        state.instruction.operands[7]=static_cast<std::uint8_t>(mode*2);
        need(prepare_dialog_illustration(state,world,source,DialogIllustrationResource{temp.path/"absent",""}).plan.kind==Kind::unsupported,
            "named/unknown icon modes never turn displayed text into a filesystem path");
    }
    state.instruction.opcode=40;state.instruction.operands[7]=6;
    need(plan_dialog_illustration(state,world,source).kind==Kind::unsupported,"opcode40 is not a Dialog primitive");
    need(world.entity_identity_revision()==revision&&world.find(1)->generation==generation,
        "illustration preparation never constructs, removes or mutates World entities");
}
}
int main(){try{cases();std::cout<<"dialog illustration selector/resource checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
