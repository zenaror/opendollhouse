// SPDX-License-Identifier: MPL-2.0
#include "linux_cas_preview.hpp"
#include "linux_cas_catalog.hpp"
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
    if(argc!=3||std::string(argv[1])!="--game-root")return 77;
    try{
        const auto catalog=odh_host::scan_linux_cas_catalog(argv[2]);
        odh_host::LinuxCasPreview preview(argv[2]);
        for(bool child:{false,true})for(bool female:{false,true}){
            const auto type=std::string(female?"f":"m")+(child?"c":"a");
            const auto bodies=catalog.bodies(type+(child?"chd":""),"lgt");
            const auto heads=catalog.heads(type,"lgt");
            if(bodies.empty()||heads.empty())throw std::runtime_error("CAS reference choices unavailable: "+type);
            odh_host::SimitoneCasPersonInput person;person.child=child;person.female=female;
            person.skin="lgt";person.body_appearance=bodies[0].appearance;person.body_texture=bodies[0].texture;
            person.body_type=bodies[0].body_type;person.handgroup_texture=bodies[0].handgroup_texture;
            person.head_appearance=heads[0].appearance;person.head_texture=heads[0].texture;
            for(bool portrait:{false,true}){
                const auto* image=preview.image(person,portrait);
                if(!image)throw std::runtime_error(type+": "+preview.diagnostic());
                if(image->width!=(portrait?74U:100U)||image->height!=(portrait?74U:220U))
                    throw std::runtime_error("CAS preview dimensions mismatch");
                std::size_t coverage=0;for(const auto& p:image->pixels)
                    coverage+=!(p[0]==255&&p[1]==0&&p[2]==255);
                if(coverage<100||coverage==image->pixels.size())throw std::runtime_error("CAS silhouette is empty or opaque");
                if(preview.image(person,portrait)!=image)throw std::runtime_error("unchanged CAS choice did not reuse bounded image");
            }
        }
        std::cout<<"CAS installed adult/child appearance previews passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
