// SPDX-License-Identifier: MPL-2.0
// Original-format synthetic strips verify actions and artwork state selection;
// installed-game captures remain the authority for visual acceptance.
#include "original_settings_view.hpp"
#include "linux_text_renderer.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace odh_host;using Pixel=std::array<std::uint8_t,4>;
constexpr Pixel clear{7,9,11,255};
void need(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
void put(std::string& s,unsigned v,unsigned n=4){for(unsigned i=0;i<n;++i)s.push_back(static_cast<char>(v>>(8*i)));}
void at(std::string& s,unsigned p,unsigned v,unsigned n=4){for(unsigned i=0;i<n;++i)s.at(p+i)=static_cast<char>(v>>(8*i));}
Pixel color(unsigned state){return {static_cast<std::uint8_t>(20+state*25),40,80,255};}
std::string bmp(unsigned width,unsigned height,bool slider=false,unsigned variant=0){
    const auto stride=(width*3+3)&~3u;std::string s(54+stride*height,'\0');
    at(s,0,0x4d42,2);at(s,2,unsigned(s.size()));at(s,10,54);at(s,14,40);at(s,18,width);at(s,22,height);at(s,26,1,2);at(s,28,24,2);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const auto p=54+(height-y-1)*stride+x*3;
        auto rgb=slider?color(variant+(x>=35?2:0)):color(x/20);
        if((!slider&&(x%20==0||y==0))||(slider&&(x==33||x==34||x==43)))rgb={255,0,255,255};
        s[p]=static_cast<char>(rgb[2]);s[p+1]=static_cast<char>(rgb[1]);s[p+2]=static_cast<char>(rgb[0]);
    }return s;
}
std::string far(const std::vector<std::pair<std::string,std::string>>& entries){
    std::string s="FAR!byAZ";put(s,1);put(s,0);std::vector<unsigned> offsets;
    for(const auto& entry:entries){offsets.push_back(unsigned(s.size()));s+=entry.second;}
    at(s,12,unsigned(s.size()));put(s,unsigned(entries.size()));
    for(unsigned i=0;i<entries.size();++i){const auto& e=entries[i];put(s,unsigned(e.second.size()));put(s,unsigned(e.second.size()));
        put(s,offsets[i]);put(s,unsigned(e.first.size()));s+=e.first;}
    return s;
}
std::string ui_strings(){
    std::vector<std::string> labels(84);labels[28]="Terrain detail";labels[31]="Character detail long";
    labels[39]="Low";labels[40]="Med";labels[41]="High";
    std::string payload;put(payload,0xfffd,2);put(payload,unsigned(labels.size()),2);
    for(const auto& label:labels){put(payload,1,1);payload+=label;payload.append(2,'\0');}
    std::string out(64,'\0');const std::string signature="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    out.replace(0,signature.size(),signature);out+="STR#";
    const auto size=unsigned(payload.size()+76);for(int shift:{24,16,8,0})out.push_back(char(size>>shift));
    out.push_back(0);out.push_back(char(145));out.append(66,'\0');return out+payload;
}
void write(const std::filesystem::path& path,const std::string& data){std::ofstream f(path,std::ios::binary);f<<data;need(bool(f),"write fixture");}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("odh-settings-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){need(std::filesystem::create_directories(path/"UIGraphics"),"create fixture");}
    ~Temp(){std::error_code ec;std::filesystem::remove_all(path,ec);}
};
std::array<int,2> center(BuyUiRect rect){return {rect.x+rect.width/2,rect.y+rect.height/2};}
const Pixel& pixel(const odh::RasterFrame& f,std::array<int,2> p){return f.pixels[std::size_t(p[1])*f.width+p[0]];}
void cases(){
    Temp temp;const auto archive=temp.path/"UIGraphics/UIGraphics.far";
    const std::vector<std::pair<std::string,std::string>> assets{
        {"CPanel/Buttons/optcheckbox.bmp",bmp(120,21)},{"CPanel/Buttons/optradio.bmp",bmp(120,20)},
        {"CPanel/Buttons/optslideroff.bmp",bmp(44,14,true)},{"CPanel/Buttons/optslideron.bmp",bmp(44,14,true,1)}};
    write(archive,far(assets));std::filesystem::create_directory(temp.path/"GameData");
    write(temp.path/"GameData/UIText.iff",ui_strings());OriginalSettingsView view(temp.path);
    OriginalSettingsState state;for(auto& value:state.values)value=0;
    OriginalSettingsCapabilities capabilities;capabilities.fill(true);
    for(const auto dimensions:std::array<std::array<unsigned,2>,3>{{{800,600},{1024,768},{320,240}}})
        for(const auto page:{OriginalSettingsPage::graphics,OriginalSettingsPage::sound,OriginalSettingsPage::play}){
            const auto layout=view.layout(dimensions[0],dimensions[1],page);
            need(!layout.controls.empty(),"original page has real controls");
            if(page==OriginalSettingsPage::graphics){
                need(layout.controls.size()==10&&layout.labels.size()==9,
                    "Complete has four checkboxes, six radios, six labels and three shared headings");
                for(std::size_t id=39;id<=41;++id){
                    unsigned count=0;for(const auto& label:layout.labels)if(label.string_index==id)++count;
                    need(count==1,"detail column headings occur once, not once per radio row");
                }
            }
            odh::RasterFrame base{};base.width=dimensions[0];base.height=dimensions[1];base.pixels.assign(base.width*base.height,clear);
            base.depth.assign(base.pixels.size(),7);base.owner_ids.assign(base.pixels.size(),31);
            need(!view.hit(layout,0,0,state,capabilities),"outside settings has no action");
            for(std::size_t n=0;n<layout.controls.size();++n){
                const auto& control=layout.controls[n];const auto p=center(control.rect);const auto id=static_cast<std::size_t>(control.id);
                need(control.rect.width>0&&control.rect.height>0&&control.rect.x>=0&&control.rect.y>=0&&
                    control.rect.x+control.rect.width<=int(base.width)&&control.rect.y+control.rect.height<=int(base.height),"bounded compact controls");
                need(view.hit(layout,p[0],p[1],state,capabilities)==n,"drawn control and hit share geometry");
                auto disabled=capabilities;disabled[id]=false;
                need(!view.hit(layout,p[0],p[1],state,disabled)&&!view.activate(layout,n,p[0],state,disabled),"unsupported consumer never emits action");
                auto unknown=state;unknown.values[id].reset();
                need(!view.hit(layout,p[0],p[1],unknown,capabilities)&&!view.activate(layout,n,p[0],unknown,capabilities),"unknown host state never emits action");
                auto invalid=state;invalid.values[id]=control.kind==OriginalSettingControlKind::slider?101:3;
                need(!view.activate(layout,n,p[0],invalid,capabilities),"invalid host state cannot be toggled");
                const auto event=view.activate(layout,n,p[0],state,capabilities);
                need(event&&event->id==control.id,"event has exact setting identity");
                if(control.kind==OriginalSettingControlKind::slider){
                    need(event->value>=45&&event->value<=55,"slider midpoint uses drawn thumb travel");
                    need(view.activate(layout,n,control.rect.x-500,state,capabilities)->value==0&&
                        view.activate(layout,n,control.rect.x+control.rect.width+500,state,capabilities)->value==100,"captured slider drag clamps both endpoints");
                    auto half=state;half.values[id]=50;auto painted=base;view.draw(painted,page,half,capabilities,p,n);
                    need(pixel(painted,p)!=clear&&painted.depth==base.depth&&painted.owner_ids==base.owner_ids,"original slider paints without changing depth/picking");
                }else{
                    need(event->value==(control.kind==OriginalSettingControlKind::checkbox?1:control.value),"checkbox toggles/radio selects authored value");
                    auto local=state;local.values[id]=control.kind==OriginalSettingControlKind::radio?control.value:0;
                    auto idle=base;view.draw(idle,page,local,capabilities);
                    const bool selected=control.kind==OriginalSettingControlKind::radio;
                    need(pixel(idle,p)==color(selected?1:0),"original normal off/on strip cell");
                    auto hover=base;view.draw(hover,page,local,capabilities,p,n);
                    need(pixel(hover,p)==color(selected?3:2),"hover/pressed uses authored highlight cells");
                    auto no=base;view.draw(no,page,local,disabled,p,n);
                    need(pixel(no,p)==color(selected?5:4),"unsupported control paints disabled selected/unselected art");
                    need(hover.depth==base.depth&&hover.owner_ids==base.owner_ids,"settings art preserves scene buffers");
                }
            }
            need(!view.activate(layout,layout.controls.size(),0,state,capabilities),"invalid captured control fails closed");
        }
    OriginalGraphicsMetrics metrics;metrics.detail_title_width=137;metrics.header_text_height=16;
    const auto graphics=original_settings_layout(1024,768,OriginalSettingsPage::graphics,metrics);
    for(unsigned row=0;row<4;++row){
        const auto& control=graphics.controls[row];const auto& label=graphics.labels[row];
        need(control.rect.x==465&&control.rect.y==675+int(row)*20&&control.rect.width==20&&control.rect.height==21,
            "authored checkbox grid includes interface effects in the fourth20px row");
        need(label.rect.x==495&&label.rect.y==677+int(row)*20,"authored checkbox captions have separate baseline origin");
    }
    for(unsigned row=0;row<2;++row){
        const auto& title=graphics.labels[4+row];
        need(title.rect.x==563&&title.rect.x+title.rect.width==700&&title.rect.y==698+int(row)*26&&
            title.right_aligned&&!title.centered,"detail captions use common measured width with fixed right edge");
        for(unsigned choice=0;choice<3;++choice){const auto& radio=graphics.controls[4+row*3+choice];
            need(radio.rect.x==705+int(choice)*30&&radio.rect.y==698+int(row)*26&&radio.value==int(choice),
                "original80px radio group places20px choices at offsets0,30,60");
        }
    }
    for(unsigned column=0;column<3;++column){const auto& header=graphics.labels[6+column];
        need(header.rect.x==697+int(column)*31&&header.rect.y==675&&header.rect.width==31&&header.rect.height==14&&header.centered,
            "shared heading thirds use original first-group and measured text-height geometry");
    }
    auto wider_metrics=metrics;wider_metrics.detail_title_width=181;wider_metrics.header_text_height=20;
    const auto wider=original_settings_layout(1024,768,OriginalSettingsPage::graphics,wider_metrics);
    need(wider.labels[4].rect.x==519&&wider.labels[4].rect.width==181&&wider.labels[6].rect.y==671&&
        wider.controls[4].rect.x==graphics.controls[4].rect.x,"font metrics move labels while preserving radio locations");
    auto language_metrics=metrics;language_metrics.original_language_code=0x11;
    const auto code17=original_settings_layout(1024,768,OriginalSettingsPage::graphics,language_metrics);
    need(code17.controls[0].rect.y==679&&code17.labels[0].rect.y==681&&code17.labels[4].rect.y==698,
        "authenticated executable code17 offsets first captions and checkboxes only");
    language_metrics.original_language_code=0x13;
    const auto code19=original_settings_layout(1024,768,OriginalSettingsPage::graphics,language_metrics);
    need(code19.controls[0].rect.y==675&&code19.labels[0].rect.y==671&&code19.labels[4].rect.y==695&&code19.controls[4].rect.y==698,
        "authenticated executable code19 adjusts captions without shifting input controls");
    const auto actual=view.layout(1024,768,OriginalSettingsPage::graphics);
    const auto measured=measure_text_overlay("Character detail long",1,11);
    need(actual.labels[4].rect.width==measured.width&&actual.labels[5].rect.width==measured.width,
        "view measures actual localized captions using the same font size as painting");
    need(actual.labels[4].right_aligned&&actual.labels[4].rect.x+actual.labels[4].rect.width==700,
        "real view supplies measured graphics layout to host hit and drawing consumers");
    const auto desktop=original_settings_layout(800,600,OriginalSettingsPage::play),wide=original_settings_layout(1024,768,OriginalSettingsPage::play);
    need(desktop.controls.size()==wide.controls.size(),"desktop original control count");
    for(unsigned i=0;i<desktop.controls.size();++i)need(desktop.controls[i].rect.x==wide.controls[i].rect.x&&
        wide.controls[i].rect.y-desktop.controls[i].rect.y==168,"desktop original size anchored to footer");
    auto malformed=assets;malformed[0].second=bmp(100,21);write(archive,far(malformed));
    bool rejected=false;try{OriginalSettingsView bad(temp.path);}catch(const odh::FormatError&){rejected=true;}
    need(rejected,"wrong original strip shape rejected");
}
}
int main(){try{cases();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
