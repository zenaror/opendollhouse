#include "odh/pose.hpp"
#include "resource.hpp"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace {
template<class Values> void values(std::ostream& out,const Values& v) {
    out<<'[';bool first=true;for(const auto x:v){if(!first)out<<',';first=false;out<<x;}out<<']';
}
void properties(std::ostream& out,const std::vector<odh::AnimationProperties>& groups) {
    out<<'[';for(std::size_t g=0;g<groups.size();++g){if(g)out<<',';out<<'[';
        for(std::size_t p=0;p<groups[g].size();++p){if(p)out<<',';
            out<<'['<<json_bytes(groups[g][p].first)<<','<<json_bytes(groups[g][p].second)<<']';}
        out<<']';}out<<']';
}
std::string xml(const std::string& s) {
    std::ostringstream out;for(unsigned char c:s){switch(c){case '&':out<<"&amp;";break;case '<':out<<"&lt;";break;
        case '>':out<<"&gt;";break;case '"':out<<"&quot;";break;case '\'':out<<"&apos;";break;
        default:if(c>=32&&c<127)out<<static_cast<char>(c);else out<<"&#xfffd;";}}
    return out.str();
}
void svg(const odh::SkeletonPose& pose,const std::string& path) {
    std::vector<std::array<double,2>> points;double xmin=0,xmax=0,ymin=0,ymax=0;
    for(const auto& b:pose.bones){const auto& p=b.absolute_position;
        const std::array<double,2> q{double(p[0])-p[2],(double(p[0])+p[2])*0.5-p[1]};
        if(!std::isfinite(q[0])||!std::isfinite(q[1]))throw odh::FormatError("nonfinite SVG joint position");
        if(points.empty()){xmin=xmax=q[0];ymin=ymax=q[1];}
        else{xmin=std::min(xmin,q[0]);xmax=std::max(xmax,q[0]);ymin=std::min(ymin,q[1]);ymax=std::max(ymax,q[1]);}points.push_back(q);}
    const double extent=std::max(xmax-xmin,ymax-ymin),scale=extent>0?576/extent:1;
    for(auto& p:points){p[0]=320+(p[0]-(xmin+xmax)*0.5)*scale;p[1]=320+(p[1]-(ymin+ymax)*0.5)*scale;}
    std::ostringstream out;out<<std::setprecision(std::numeric_limits<double>::max_digits10)
        <<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"640\" height=\"640\" viewBox=\"0 0 640 640\">\n"
        <<"<title>Skeletal diagnostic / no mesh or game rendering: "<<xml(pose.skeleton)<<"</title>\n"
        <<"<desc>Provisional diagnostic convention: isometric X-Z plane with Y vertical.</desc>\n"
        <<"<rect width=\"640\" height=\"640\" fill=\"#fafafa\"/>\n<g stroke=\"#374151\" stroke-width=\"2\">\n";
    for(std::size_t i=0;i<pose.bones.size();++i){const auto parent=pose.bones[i].parent_index;if(parent<0)continue;
        if(static_cast<std::size_t>(parent)>=points.size())throw odh::FormatError("invalid SVG parent index");
        out<<"<line x1=\""<<points[parent][0]<<"\" y1=\""<<points[parent][1]<<"\" x2=\""<<points[i][0]<<"\" y2=\""<<points[i][1]<<"\"/>\n";}
    out<<"</g>\n<g fill=\"#2563eb\">\n";
    for(std::size_t i=0;i<points.size();++i)out<<"<circle cx=\""<<points[i][0]<<"\" cy=\""<<points[i][1]<<"\" r=\"4\"><title>"<<xml(pose.bones[i].name)<<"</title></circle>\n";
    out<<"</g>\n</svg>\n";
    std::ofstream file(path,std::ios::binary|std::ios::trunc);if(!file)throw odh::FormatError("cannot open SVG output");
    file<<out.str();file.close();if(!file)throw odh::FormatError("cannot write SVG output");
}
std::string folded(std::string s) {for(auto& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c+('a'-'A'));return s;}
double frame_number(const std::string& s) {
    if(s.empty()||std::isspace(static_cast<unsigned char>(s.front())))throw odh::FormatError("invalid frame number");
    std::size_t end=0;const double n=std::stod(s,&end);
    if(end!=s.size()||!std::isfinite(n))throw odh::FormatError("frame must be finite with no trailing characters");return n;
}
}
int main(int argc,char** argv) {
    try {
        if(argc<4)throw odh::FormatError("Usage: odh-pose FAR SKELETON_ENTRY SKELETON_NAME [--animation FAR BCF_ENTRY CLIP CFP_ENTRY] [--frame NUMBER] [--svg OUTPUT_PATH]");
        std::string animation_far,bcf_entry,clip,cfp_entry,svg_path;bool has_animation=false,has_frame=false,has_svg=false;double frame=0;
        for(int i=4;i<argc;){const std::string option=argv[i++];
            if(option=="--animation"){if(has_animation||argc-i<4)throw odh::FormatError("duplicate or incomplete --animation");has_animation=true;
                animation_far=argv[i++];bcf_entry=argv[i++];clip=argv[i++];cfp_entry=argv[i++];}
            else if(option=="--frame"){if(has_frame||i==argc)throw odh::FormatError("duplicate or incomplete --frame");has_frame=true;frame=frame_number(argv[i++]);}
            else if(option=="--svg"){if(has_svg||i==argc)throw odh::FormatError("duplicate or incomplete --svg");has_svg=true;svg_path=argv[i++];}
            else throw odh::FormatError("unknown option");}
        if(has_frame&&!has_animation)throw odh::FormatError("--frame requires --animation");
        std::optional<odh::SkeletonCollection> skeletons;with_far_entry(argv[1],argv[2],[&](const auto& v){skeletons=odh::read_bcf_skeletons(v);});
        const odh::Skeleton* selected=nullptr;for(const auto& s:skeletons->skeletons)if(s.name==argv[3]){
            if(selected)throw odh::FormatError("ambiguous skeleton name");selected=&s;}
        if(!selected)throw odh::FormatError("exact skeleton name not found");
        std::optional<odh::AnimationCollection> animations;const odh::AnimationHeader* animation=nullptr;std::optional<odh::CfpPlanes> cfp;
        if(has_animation){with_far_entry(animation_far,bcf_entry,[&](const auto& v){animations=odh::read_bcf_animations(v);});
            for(const auto& a:animations->animations)if(a.name==clip){if(animation)throw odh::FormatError("ambiguous animation name");animation=&a;}
            if(!animation)throw odh::FormatError("exact animation name not found");
            if(folded(cfp_entry)!=folded(animation->xskill+".cfp"))throw odh::FormatError("CFP entry must match selected animation xskill");
            with_far_entry(animation_far,cfp_entry,[&](const auto& v){cfp=odh::read_cfp(v,animation->translations,animation->rotations);});}
        const auto pose=odh::sample_pose(*selected,animation,cfp?&*cfp:nullptr,frame);
        std::ostringstream out;out<<std::setprecision(std::numeric_limits<double>::max_digits10)
            <<"{\"evidence\":\"ODH_skeletal_pose_reference_profile\",\"sources\":{\"skeleton_far\":"<<json_bytes(argv[1])
            <<",\"skeleton_entry\":"<<json_bytes(argv[2]);
        if(has_animation)out<<",\"animation_far\":"<<json_bytes(animation_far)<<",\"animation_entry\":"<<json_bytes(bcf_entry)<<",\"cfp_entry\":"<<json_bytes(cfp_entry);
        out<<"},\"skeleton\":"<<json_bytes(pose.skeleton)<<",\"animation\":"<<json_bytes(pose.animation)<<",\"requested_frame\":"<<pose.requested_frame
            <<",\"frame\":"<<pose.frame<<",\"fraction\":"<<pose.fraction<<",\"completed\":"<<(pose.completed?"true":"false")<<",\"missing_motion_bones\":[";
        for(std::size_t i=0;i<pose.missing_motion_bones.size();++i){if(i)out<<',';out<<json_bytes(pose.missing_motion_bones[i]);}out<<"],\"bones\":[";
        if(pose.bones.size()!=selected->bones.size())throw odh::FormatError("pose bone count mismatch");
        for(std::size_t i=0;i<pose.bones.size();++i){if(i)out<<',';const auto& b=pose.bones[i];const auto& raw=selected->bones[i];
            out<<"{\"name\":"<<json_bytes(b.name)<<",\"parent_index\":"<<b.parent_index<<",\"translation\":";values(out,b.translation);
            out<<",\"rotation\":";values(out,b.rotation);out<<",\"absolute_matrix\":";values(out,b.absolute_matrix);
            out<<",\"absolute_position\":";values(out,b.absolute_position);out<<",\"raw_translation_bits\":";values(out,raw.translation_bits);
            out<<",\"raw_rotation_bits\":";values(out,raw.rotation_bits);out<<",\"can_translate\":"<<raw.can_translate<<",\"can_rotate\":"<<raw.can_rotate
                <<",\"can_blend\":"<<raw.can_blend<<",\"wiggle_value_bits\":"<<raw.wiggle_value_bits<<",\"wiggle_power_bits\":"<<raw.wiggle_power_bits<<",\"properties\":";
            properties(out,raw.properties);out<<'}';}out<<"]}\n";
        if(has_svg)svg(pose,svg_path);std::cout<<out.str();return 0;
    }catch(const std::exception& e){std::cerr<<"odh-pose: "<<e.what()<<'\n';return 2;}
}
