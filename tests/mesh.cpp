#include "odh/mesh.hpp"
#include <iostream>
#include <sstream>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void put(std::string& b,std::uint32_t value){for(unsigned i=0;i<4;++i)b+=static_cast<char>((value>>(i*8))&255);}
void replace(std::string& b,std::size_t at,std::uint32_t value){
    for(unsigned i=0;i<4;++i)b.at(at+i)=static_cast<char>((value>>(i*8))&255);}
void text(std::string& b,const std::string& value){b+=static_cast<char>(value.size());b+=value;}
auto mesh(const std::string& b,odh::MeshLimits limits={}){
    std::istringstream stream(b);return odh::read_bmf(odh::View(stream,b.size()),limits);}
auto appearances(const std::string& b,odh::MeshLimits limits={}){
    std::istringstream stream(b);return odh::read_bcf_appearances(odh::View(stream,b.size()),limits);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const odh::FormatError&){failed=true;}
    require(failed,"invalid or truncated mesh/appearance rejected");}
struct BmfFixture {
    std::string bytes;
    std::size_t bones,faces,bindings,real,blend,vertex_count2;
};
BmfFixture bmf(){BmfFixture f{};auto& b=f.bytes;
    text(b,std::string("s\0",2));text(b,"t");f.bones=b.size();put(b,2);text(b,"ROOT");text(b,"B");
    f.faces=b.size();put(b,1);put(b,0xFFFFFFFFU);put(b,0x80000000U);put(b,999);
    f.bindings=b.size();put(b,1);
    for(auto field:{0xFFFFFFFFU,0x80000000U,999U,0xFFFFFFFEU,777U})put(b,field);
    f.real=b.size();put(b,2);
    for(auto bits:{0x80000000U,0x7FC12345U,1U,0x7F800000U})put(b,bits);
    f.blend=b.size();put(b,2);put(b,0x80000000U);put(b,0xFFFFFFFFU);put(b,65537);put(b,999);
    f.vertex_count2=b.size();put(b,0xFFFFFFFFU);
    for(unsigned vertex=0;vertex<4;++vertex)
        for(auto bits:{0x80000000U,0x7FA12345U,0xFF800000U,1U,0x7F800000U,0x3F800000U})put(b,bits+vertex);
    return f;
}
struct BcfFixture {std::string bytes;std::size_t count,first_bindings,second_bindings,animation;};
BcfFixture bcf(){BcfFixture f{};auto& b=f.bytes;put(b,0);f.count=b.size();put(b,2);
    for(unsigned a=0;a<2;++a){text(b,"appearance");put(b,0x80000000U+a);put(b,0xFFFFFFFFU);
        if(a==0)f.first_bindings=b.size();else f.second_bindings=b.size();
        put(b,1);text(b,"ROOT");text(b,"body");put(b,0x80000000U+a);put(b,7+a);}
    f.animation=b.size();put(b,0);return f;
}
}
int main(){try{
    const auto f=bmf();const auto result=mesh(f.bytes);
    require(result.skin_name==std::string("s\0",2) && result.texture_name=="t" &&
            result.bones==std::vector<std::string>{"ROOT","B"},"Pascal names and embedded zero preserved");
    require(result.faces==std::vector<std::array<std::int32_t,3>>{{-1,(-2147483647-1),999}},"signed face indices preserved without semantic validation");
    const auto& binding=result.bindings.at(0);
    require(binding.bone_index==-1 && binding.first_real==(-2147483647-1) && binding.real_count==999 &&
            binding.first_blend==-2 && binding.blend_count==777,"raw binding indices and ranges preserved");
    require(result.real_vertices.size()==2 && result.blend_vertices.size()==2 && result.blend_data.size()==2,"real and blend arrays follow separate counts");
    require(result.real_vertices[0].texcoord_bits==std::array<std::uint32_t,2>{0x80000000U,0x7FC12345U} &&
            result.real_vertices[1].texcoord_bits==std::array<std::uint32_t,2>{1U,0x7F800000U},"raw UV signed zero, NaN, subnormal and infinity retained");
    for(unsigned vertex=0;vertex<4;++vertex){const auto& v=vertex<2?result.real_vertices[vertex]:result.blend_vertices[vertex-2];
        require(v.position_bits==std::array<std::uint32_t,3>{0x80000000U+vertex,0x7FA12345U+vertex,0xFF800000U+vertex} &&
                v.normal_bits==std::array<std::uint32_t,3>{1U+vertex,0x7F800000U+vertex,0x3F800000U+vertex},"real then blend transforms retain exact float bits");}
    require(result.blend_vertices[0].texcoord_bits==std::array<std::uint32_t,2>{0,0},"unstored blend UV has explicit zero representation");
    require(result.blend_data[0].weight==(-2147483647-1) && result.blend_data[0].other_vertex==-1 &&
            result.blend_data[1].weight==65537 && result.blend_data[1].other_vertex==999,"unnormalized blend weight and reference preserved");
    require(result.vertex_count2==-1 && result.remaining_bytes==0 && mesh(f.bytes+"tail").remaining_bytes==4,"raw count2 and trailing extent preserved");
    for(auto metadata:{0U,4U,687U,0x80000000U}){auto b=f.bytes;replace(b,f.vertex_count2,metadata);
        const auto expected=metadata<0x80000000U?static_cast<std::int32_t>(metadata):(-2147483647-1);
        require(mesh(b).vertex_count2==expected && mesh(b).blend_vertices.size()==2,"count2 is metadata independent of transform extents");}
    for(std::size_t cut=0;cut<f.bytes.size();++cut)rejects([&]{mesh(f.bytes.substr(0,cut));});
    for(const auto at:{f.bones,f.faces,f.bindings,f.real,f.blend})for(auto bad_count:{0xFFFFFFFFU,0x80000000U,0x7FFFFFFFU}){
        auto b=f.bytes;replace(b,at,bad_count);rejects([&]{mesh(b);});
        odh::MeshLimits huge;huge.bones=huge.faces=huge.bindings=huge.vertices=huge.blends=0xFFFFFFFFU;
        rejects([&]{mesh(b,huge);});}
    for(unsigned budget=0;budget<5;++budget){odh::MeshLimits l;
        if(budget==0)l.bones=1;
        if(budget==1)l.faces=0;
        if(budget==2)l.bindings=0;
        if(budget==3)l.vertices=1;
        if(budget==4)l.blends=1;
        rejects([&]{mesh(f.bytes,l);});}
    odh::MeshLimits exact;exact.bones=2;exact.faces=1;exact.bindings=1;exact.vertices=2;exact.blends=2;exact.text_bytes=8;
    require(mesh(f.bytes,exact).real_vertices.size()==2,"exact independent count and cumulative text budgets accepted");
    --exact.text_bytes;rejects([&]{mesh(f.bytes,exact);});
    std::string empty;text(empty,"");text(empty,"");for(unsigned i=0;i<6;++i)put(empty,0);
    odh::MeshLimits zero;zero.bones=zero.faces=zero.bindings=zero.vertices=zero.blends=0;zero.text_bytes=0;
    require(mesh(empty,zero).real_vertices.empty() && mesh(empty,zero).vertex_count2==0,"empty BMF accepted at zero budgets");
    rejects([&]{mesh("skin\ntexture\n0\n");});

    const auto a=bcf();const auto collection=appearances(a.bytes);const auto& app=collection.appearances.at(0);
    require(collection.appearances.size()==2 && app.name=="appearance" && app.type==(-2147483647-1) && app.zero==-1,"appearance order and raw metadata retained");
    require(app.bindings[0].bone=="ROOT" && app.bindings[0].mesh_name=="body" &&
            app.bindings[0].censor_flags==(-2147483647-1) && app.bindings[0].zero==7 &&
            collection.appearances[1].bindings[0].zero==8,"binding names, flags and nonzero reserved metadata retained");
    require(collection.remaining_bytes==0 && appearances(a.bytes+"tail").remaining_bytes==4,"BCF trailer consumed and trailing extent reported");
    for(std::size_t cut=0;cut<a.bytes.size();++cut)rejects([&]{appearances(a.bytes.substr(0,cut));});
    for(auto at:{a.count,a.first_bindings,a.second_bindings})for(auto count:{0xFFFFFFFFU,0x80000000U,0x7FFFFFFFU}){
        auto b=a.bytes;replace(b,at,count);rejects([&]{appearances(b);});
        odh::MeshLimits huge;huge.appearances=huge.appearance_bindings=0xFFFFFFFFU;
        rejects([&]{appearances(b,huge);});}
    for(auto at:{std::size_t{0},a.animation})for(auto count:{1U,0xFFFFFFFFU}){
        auto b=a.bytes;replace(b,at,count);rejects([&]{appearances(b);});}
    exact={};exact.appearances=1;rejects([&]{appearances(a.bytes,exact);});
    exact={};exact.appearance_bindings=1;rejects([&]{appearances(a.bytes,exact);});
    exact.appearance_bindings=2;exact.appearances=2;exact.text_bytes=36;
    require(appearances(a.bytes,exact).appearances.size()==2,"exact cumulative binding and text budgets accepted across appearances");
    --exact.text_bytes;rejects([&]{appearances(a.bytes,exact);});
    std::string empty_bcf;put(empty_bcf,0);put(empty_bcf,0);put(empty_bcf,0);
    zero.appearances=zero.appearance_bindings=0;
    require(appearances(empty_bcf,zero).appearances.empty(),"empty appearance collection accepted at zero budgets");
    std::string empty_app;put(empty_app,0);put(empty_app,1);text(empty_app,"");put(empty_app,0);put(empty_app,0);put(empty_app,0);put(empty_app,0);
    zero.appearances=1;
    require(appearances(empty_app,zero).appearances[0].bindings.empty(),"empty appearance and binding list accepted");
    std::cout<<checks<<" mesh resource checks passed (synthetic fixtures only)\n";return 0;
}catch(const std::exception& e){std::cerr<<"Mesh test failed: "<<e.what()<<'\n';return 1;}}
