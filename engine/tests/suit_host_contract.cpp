// SPDX-License-Identifier: MPL-2.0
#include "content/TS1SkinCatalog.h"
#include "vitaboy/content/TS1AvatarProvider.h"
#include "vitaboy/Appearance.h"
#include "vitaboy/Avatar.h"
#include "odh/ts1_texture_name.hpp"
#include "saved_appearance.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace FSO::Content::TS1;
namespace {
struct Fixture {
    fs::path root = fs::temp_directory_path() / ("odh-suit-contract-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path skins = root/"GameData/Skins", anim = root/"GameData/Animation/Animation.far", tex = root/"GameData/Textures/Textures.far";
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
};
void check(bool ok, const char* detail) { if (!ok) throw std::runtime_error(detail); }
void file(const fs::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    check(bool(out), "test resource write failed");
}
void u32(std::string& data, unsigned value) {
    for (int k=0; k<4; ++k) data.push_back(static_cast<char>(value>>(8*k)));
}
void far(const fs::path& path, const std::vector<std::pair<std::string,std::string>>& files) {
    std::string body, table;
    u32(table, static_cast<unsigned>(files.size()));
    for (const auto& [name,data] : files) {
        u32(table,data.size()); u32(table,data.size()); u32(table,16+body.size()); u32(table,name.size());
        table+=name; body+=data;
    }
    std::string archive="FAR!byAZ";
    u32(archive,1); u32(archive,16+body.size());
    file(path,archive+body+table);
}
// Tiny valid texture so both the renderer and native bitmap loader exercise
// the file that was actually selected for each binding.
void bitmap(const fs::path& path, unsigned char red) {
    std::string b="BM";u32(b,58);u32(b,0);u32(b,54);u32(b,40);u32(b,1);u32(b,1);
    b.append("\1\0\30\0",4);u32(b,0);u32(b,4);
    for(int i=0;i<4;++i)u32(b,0);
    b.append(2,'\0');b.push_back(static_cast<char>(red));b.push_back('\0');file(path,b);
}
struct TestAvatar : FSO::Vitaboy::Avatar {
    TestAvatar() : Avatar(nullptr) {}
    using Avatar::AddAppearance;
};
std::string cmx(const std::vector<std::string>& names) {
    std::string out="version 300\n0\n"+std::to_string(names.size())+"\n";
    for(const auto& name:names) out+=name+"\n0\n0\n1\nHEAD\nfixture-mesh\n0\n0\n";
    return out+"0\n";
}
}
int main() {
    try {
        Fixture f;
        fs::create_directories(f.skins);fs::create_directories(f.anim.parent_path());fs::create_directories(f.tex.parent_path());
        far(f.anim,{{"rig.bcf",std::string(12,'\0')},{"c728fa_dual.cmx.bcf",std::string(12,'\0')}});
        far(f.tex,{});
        file(f.skins/"c725fa_style.cmx",cmx({"C725FAlgt_Style"}));
        file(f.skins/"c726fa_multi.cmx",cmx({"C726FAlgt_Multi","C726FAmed_Multi"}));
        file(f.skins/"c727fa_wrong.cmx",cmx({"C727MAlgt_Wrong"}));
        file(f.skins/"c728fa_dual.cmx",cmx({"C728FAlgt_Dual"}));
        file(f.skins/"c730fa_empty.cmx",cmx({}));
        file(f.skins/"c729fa_exact.cmx",cmx({"C729FAlgt_Exact"}));
        file(f.skins/"somewhere.cmx",cmx({"C729FA_Exact","b001fafit_plain"}));
        file(f.skins/"fixture-mesh.skn","fixture-mesh\nfixture-texture\n1\nHEAD\n1\n0 1 2\n1\n0 0 3 0 0\n3\n0 0\n0 0\n0 0\n0\n3\n0 0 0 0 1 0\n1 0 0 0 1 0\n0 1 0 0 1 0\n");
        for(auto n:{"fixture-texture","c725falgt_style","c725famed_style","c726falgt_multi","c726famed_multi","b001fafitlgt_plain"})
            file(f.skins/(std::string(n)+".bmp"),"fixture");
        const std::string vertices="\n1\nHEAD\n1\n0 1 2\n1\n0 0 3 0 0\n3\n0 0\n0 0\n0 0\n0\n3\n0 0 0 0 1 0\n1 0 0 0 1 0\n0 1 0 0 1 0\n";
        file(f.skins/"goggles.skn","goggles\ngoggles"+vertices);
        file(f.skins/"face.skn","face\nfaceLGt.BmP"+vertices);
        file(f.skins/"outfit.cmx","version 300\n0\n1\noutfit\n0\n0\n2\nHEAD\ngoggles\n0\n0\nHEAD\nface\n0\n0\n0\n");
        bitmap(f.skins/"goggles.bmp",43);bitmap(f.skins/"facelgt.bmp",101);bitmap(f.skins/"facemed.bmp",167);
        TS1AvatarContent::SetBasePath(f.root.string());
        auto provider=TS1AvatarContent::Get();
        CAS::CASCatalog catalog;
        check(catalog.Build(),"fixture catalog failed");
        SavedAppearanceResources renderer(f.anim.string(),f.tex.string(),"rig.bcf","adult",f.skins.string(),{f.anim.string()});
        auto resolve=[&](const char* name) {
            odh::AvatarBodyStrings body{};body.body="b001fafit_plain";body.head=name;
            return renderer.resolve_body(body).parts.at(1);
        };
        auto test=[&](const char* name,const char* wanted) {
            auto native=provider->BCF.GetAppearance(name);
            check(wanted ? native && native->Name==wanted : !native,"native lookup selected the wrong suit");
            if(wanted)check(resolve(name).appearance_name==wanted,"renderer selected the wrong suit");
            else {
                bool refused=false;
                try { resolve(name); } catch(const odh::FormatError&){refused=true;}
                check(refused,"renderer accepted an ambiguous or incompatible suit");
            }
        };
        test("c725fa_style","C725FAlgt_Style");
        test("c726fa_multi",nullptr);
        test("c726famed_multi","C726FAmed_Multi");
        test("c727fa_wrong","C727MAlgt_Wrong");
        test("c727fa_approximate",nullptr);
        test("c728fa_dual",nullptr);
        test("c730fa_empty",nullptr);
        test("c729fa_exact","C729FA_Exact");
        check(!provider->BCF.SkinHostBCF.count("c725fa_style"),"reading a legacy alias published it as a new suit");
        const auto& light=catalog.Heads(CAS::CASAge::Adult,CAS::CASGender::Female,CAS::CASSkin::Light);
        const auto& medium=catalog.Heads(CAS::CASAge::Adult,CAS::CASGender::Female,CAS::CASSkin::Medium);
        auto has=[](const auto& list,const char* mesh,const char* texture){
            for(const auto& o:list)if(o.Mesh==mesh&&o.Texture==texture)return true;
            return false;
        };
        check(has(light,"c725falgt_style","c725falgt_style"),"generated records must use the original declared suit");
        check(has(medium,"c726famed_multi","c726famed_multi"),"catalog lost the declared medium variant");
        check(!has(medium,"c725falgt_style","c725famed_style"),"tone-specific mesh leaked into the other tone");
        auto& textures=provider->AvatarTextures;
        auto skin=textures.Get("FaceMED.BmP"), glasses=textures.Get("GOGGLES");
        check(skin && glasses && skin!=glasses && textures.Get("facemed")==skin,"logical and literal texture keys");
        check(odh::ts1_override_matches("X","requested") && !odh::ts1_override_matches("x.bmp","requested"),
              "a real texture filename does not become the single-letter placeholder");
        check(!textures.Get("facemed.bmp.bmp"),"double extension cannot alias a different physical file");
        check(odh::ts1_bmp_filename("face.bmp.bmp")=="face.bmp.bmp","literal double extension stays exact");
        auto outfit=provider->BCF.GetAppearance("outfit");check(outfit!=nullptr,"two-part fixture appearance missing");
        TestAvatar avatar;
        auto native=avatar.AddAppearance(outfit,"facemed.bmp");
        check(native->Bindings.size()==2 && native->Bindings[0]->Texture==glasses &&
              native->Bindings[1]->Texture==skin,"native override changed accessory or missed face");
        odh::AvatarBodyStrings model{};
        model.body="b001fafit_plain";
        model.head="outfit,HEAD-HEAD=facemed.bmp";
        auto changed=renderer.resolve_body(model);
        check(fs::path(changed.parts.at(1).texture_entry).filename()=="goggles.bmp" &&
              fs::path(changed.parts.at(2).texture_entry).filename()=="facemed.bmp","rendered face and accessory texture selection");
        model.head="outfit";
        check(fs::path(renderer.resolve_body(model).parts.at(2).texture_entry).filename()=="facelgt.bmp",
              "literal mesh default without an override");
        model.head="outfit,HEAD-HEAD=facemed.bmp.bmp";
        bool refused=false;try{renderer.resolve_body(model);}catch(const odh::FormatError&){refused=true;}
        check(refused,"renderer silently removed a second extension");
        std::cout<<"Suit collection, logical selection and historical compatibility passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
