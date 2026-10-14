#include "odh/animation.hpp"
#include "resource.hpp"
#include "json.hpp"
#include <iostream>
namespace {
std::string normalized(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c+('a'-'A'));return s;}
std::string normalized_basename(std::string s){
    for(auto& c:s){if(c=='\\')c='/';if(c>='A'&&c<='Z')c=static_cast<char>(c+('a'-'A'));}
    const auto separator=s.find_last_of('/');if(separator!=std::string::npos)s.erase(0,separator+1);return s;
}
void pairs(const odh::AnimationProperties& p){std::cout<<'[';for(std::size_t n=0;n<p.size();++n){if(n)std::cout<<',';
    std::cout<<'['<<json_bytes(p[n].first)<<','<<json_bytes(p[n].second)<<']';}std::cout<<']';}
void animation(const odh::AnimationHeader& a){std::cout<<"{\"name\":"<<json_bytes(a.name)<<",\"xskill\":"<<json_bytes(a.xskill)
    <<",\"duration_bits\":"<<a.duration_bits<<",\"distance_bits\":"<<a.distance_bits<<",\"moving\":"<<a.moving
    <<",\"translations\":"<<a.translations<<",\"rotations\":"<<a.rotations<<",\"motions\":[";
    for(std::size_t n=0;n<a.motions.size();++n){if(n)std::cout<<',';const auto& m=a.motions[n];std::cout<<"{\"bone\":"<<json_bytes(m.bone)
        <<",\"frames\":"<<m.frames<<",\"duration_bits\":"<<m.duration_bits<<",\"has_translation\":"<<m.has_translation
        <<",\"has_rotation\":"<<m.has_rotation<<",\"first_translation\":"<<m.first_translation<<",\"first_rotation\":"<<m.first_rotation<<",\"properties\":[";
        for(std::size_t p=0;p<m.properties.size();++p){if(p)std::cout<<',';pairs(m.properties[p]);}std::cout<<"],\"time_properties\":[";
        for(std::size_t g=0;g<m.time_properties.size();++g){if(g)std::cout<<',';std::cout<<'[';
            for(std::size_t e=0;e<m.time_properties[g].size();++e){if(e)std::cout<<',';const auto& t=m.time_properties[g][e];
                std::cout<<"{\"id\":"<<t.id<<",\"pairs\":";pairs(t.pairs);std::cout<<'}';}std::cout<<']';}std::cout<<"]}";}
    std::cout<<"]}";
}
}
int main(int argc,char** argv){if(argc!=3&&argc!=4&&argc!=6){std::cerr<<"Usage: odh-animation FAR BCF_ENTRY [ANIMATION_NAME [--cfp CFP_ENTRY]]\n";return 2;}
    try{std::optional<odh::AnimationCollection> table;with_far_entry(argv[1],argv[2],[&](const auto& view){table=odh::read_bcf_animations(view);});
        const odh::AnimationHeader* selected=nullptr;if(argc>=4)for(const auto& a:table->animations)if(normalized(a.name)==normalized(argv[3])){
            if(selected)throw odh::FormatError("ambiguous animation name");selected=&a;}
        if(argc>=4&&!selected)throw odh::FormatError("animation not found in selected BCF");
        std::optional<odh::CfpPlanes> cfp;
        if(argc==6){if(std::string(argv[4])!="--cfp" || normalized_basename(argv[5])!=normalized_basename(selected->xskill+".cfp"))
                throw odh::FormatError("CFP entry must match selected animation xskill");
            with_far_entry(argv[1],argv[5],[&](const auto& view){cfp=odh::read_cfp(view,selected->translations,selected->rotations);});}
        std::cout<<"{\"evidence\":\"animation_metadata_only\",\"remaining_bytes\":"<<table->remaining_bytes<<",\"animations\":[";
        bool first=true;for(const auto& a:table->animations){if(selected&&&a!=selected)continue;if(!first)std::cout<<',';first=false;animation(a);}
        std::cout<<']';
        if(cfp){std::cout<<",\"cfp\":{\"remaining_bytes\":"<<cfp->remaining_bytes<<",\"raw_float_planes\":[";
            for(std::size_t p=0;p<7;++p){if(p)std::cout<<',';std::cout<<'[';
                for(std::size_t n=0;n<cfp->bits[p].size();++n){if(n)std::cout<<',';std::cout<<cfp->bits[p][n];}std::cout<<']';}std::cout<<"]}";}
        std::cout<<"}\n";return 0;
    }catch(const std::exception& e){std::cerr<<"odh-animation: "<<e.what()<<'\n';return 2;}}
