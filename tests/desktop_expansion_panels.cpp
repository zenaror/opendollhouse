/* SPDX-License-Identifier: MPL-2.0 */
#include "app/ui/DesktopExpansionPanels.h"
#include "catalog_info_text.hpp"
#include "linux_text_renderer.hpp"
#include <stdexcept>
#include <climits>
#include <iostream>
using namespace simapp::original;
void require(bool condition){if(!condition)throw std::runtime_error("PC expansion contract failed");}
namespace {
std::vector<odh_host::TextOverlayStyle> text_draws;
std::string last_text;
odh_host::TextOverlayExtent source_metrics(std::string_view value,std::uint8_t,int font,int width){
    if(value=="Mixed é العربية")return {width,63};
    if(width>0)return {width,font==13?133:102};
    return {100,font==13?19:17};
}
std::vector<odh_host::TextOverlayLineRange> source_ranges(std::string_view value,std::uint8_t,int,int){
    if(value=="Mixed é العربية")return {{0,19},{19,44},{44,63}};
    std::vector<odh_host::TextOverlayLineRange> ranges;
    for(int n=0;n<7;++n)ranges.push_back({n*19,(n+1)*19});return ranges;
}
odh_host::TextOverlayResult record_draw(odh::RasterFrame&,std::string_view value,std::uint8_t,odh_host::TextOverlayStyle style){
    last_text=std::string(value);text_draws.push_back(style);return {};
}
}
int main() {
    // Original PC token routing is independent of GUID/OBJD heuristics.
    const DesktopInventorySort sorts[]{DesktopInventorySort::gifts,DesktopInventorySort::magic,DesktopInventorySort::other};
    for(int type=-1;type<12;++type)for(auto sort:sorts) {
        const bool expected=sort==sorts[0]?(type==0||type==4||type==5):sort==sorts[1]?type==7:type==8;
        require(DesktopInventoryIncludes(sort,type)==expected);
    }
    DesktopInventorySnapshot snapshot;snapshot.has_selected_person=true;snapshot.neighbor_id=31;snapshot.items.emplace();
    const std::uint32_t guids[]{0x00000100u,0x80000000u,0xffffffffu,0x7fffffffu};
    for(std::size_t n=0;n<4;++n){DesktopInventoryItem item;item.source_index=n;item.guid=guids[n];item.type=0;item.count=1;snapshot.items->push_back(item);}
    DesktopInventoryItem hidden;hidden.type=2;hidden.guid=0x1000;hidden.count=0;hidden.source_index=4;snapshot.items->push_back(hidden);
    DesktopInventorySelection state;state.Reconcile(snapshot,2);
    require((state.VisibleItems(snapshot)==std::vector<std::size_t>{1,2,0,3}));
    require(snapshot.items->size()==5&&snapshot.items->back().type==2&&snapshot.items->back().count==0);
    require(!state.Select(4,snapshot));require(state.Select(1,snapshot)&&state.popup_open());
    require(state.Select(1,snapshot)&&!state.popup_open());
    require(state.Page(INT_MAX,snapshot,2)&&state.page()==1);
    require(!state.Page(1,snapshot,2)&&state.page()==1);
    require(state.Page(INT_MIN,snapshot,2)&&state.page()==0);
    require(state.Select(1,snapshot));
    (*snapshot.items)[1].count=2;state.Reconcile(snapshot,2);
    require(!state.selected_source()&&!state.popup_open()&&state.page()==0);
    state.Select(1,snapshot);snapshot.neighbor_id=32;state.Reconcile(snapshot,2);
    require(!state.selected_source()&&!state.popup_open());
    state.Sort(DesktopInventorySort::magic);state.Reconcile(snapshot,2);
    require(state.VisibleItems(snapshot).empty()&&!state.Page(1,snapshot,2));
    snapshot.items.reset();state.Reconcile(snapshot,0);
    require(state.VisibleItems(snapshot).empty()&&state.page()==0);
    for(const auto width:{800,1024,1280}) {
        const auto layout=DesktopOriginalInventoryLayout(width,768,10);
        require(bool(layout)&&layout->sorts[0].X==522&&layout->sorts[0].Y==743);
        // Resource IDs and filter enums do not determine physical row order.
        // Original top is Magic/type7, middle Other/type8, bottom Gift/0,4,5.
        require(layout->sorts[int(DesktopInventorySort::magic)].Y==701);
        require(layout->sorts[int(DesktopInventorySort::other)].Y==722);
        require(layout->sorts[int(DesktopInventorySort::gifts)].Y==743);
        for(const auto& sort:layout->sorts)require(sort.X==522&&sort.W==20&&sort.H==20);
        require(layout->cells.size()==(width>=1024?9:5));
        // Native37x37 catalog frames center at567,699 and609,699.
        require(layout->cells[0].X+2==567&&layout->cells[0].Y==699);
        require(layout->cells[1].X+2==609&&layout->cells[1].Y==699);
        const auto fame=DesktopOriginalFameLayout(width,768,10);
        require(bool(fame)&&fame->stars.W==80&&fame->stars.H==16);
        require(fame->skill_bars[0].X==732&&fame->skill_bars[5].Y==750);
    }
    require(!DesktopOriginalFameLayout(1024,768,15));
    require(!DesktopOriginalInventoryLayout(1024,768,20));
    odh_host::CatalogInfoContent text_popup;text_popup.name="Original help";
    text_popup.description="Description";text_popup.text_only_live=true;
    const auto live_popup=odh_host::catalog_info_layout(1024,768,text_popup,[](std::string_view,int){return 11;});
    require(live_popup.panel.width==559&&live_popup.panel.height==127);
    require(live_popup.body.x-live_popup.panel.x==15&&live_popup.body.width==530);
    text_popup.text_only_live=false;
    const auto catalog=odh_host::catalog_info_layout(1024,768,text_popup,[](std::string_view,int){return 11;});
    require(catalog.body.x-catalog.panel.x==178&&catalog.body.width==373);
    DesktopInventorySnapshot aliases;aliases.has_selected_person=true;aliases.neighbor_id=40;aliases.items.emplace();
    // Deliberately shuffled source order: original traverses0,4,5, then the
    // type5 count wins on the shared GUID tile; occurrences still fill pages.
    for(const auto type:{5,0,4}){DesktopInventoryItem item;item.type=type;item.guid=123;item.count=type+1;
        item.source_index=aliases.items->size();aliases.items->push_back(item);}
    DesktopInventorySelection alias_state;alias_state.Reconcile(aliases,2);
    require((alias_state.VisibleItems(aliases)==std::vector<std::size_t>{0,0,0}));
    require(aliases.items->size()==3&&(*aliases.items)[0].count==6&&(*aliases.items)[1].count==1&&(*aliases.items)[2].count==5);
    require(alias_state.Page(1,aliases,2)&&alias_state.page()==1);
    require(alias_state.Select(0,aliases)&&!alias_state.Select(1,aliases));
    DesktopExpansionPopupViewport popup;
    popup.open=true;popup.layout.panel={0,0,100,100};
    require(!ScrollDesktopExpansionPopup(popup,1,50,50));
    popup.visible=true;require(ScrollDesktopExpansionPopup(popup,1,50,50));
    popup.Close();require(!popup.visible&&!popup.open&&!ScrollDesktopExpansionPopup(popup,1,50,50));
    DesktopFameSelection fame_state;fame_state.level_open=true;fame_state.popup.open=true;fame_state.popup.visible=true;
    fame_state.ClosePopup();require(!fame_state.level_open&&!fame_state.popup.open&&!fame_state.popup.visible);
    const auto previous=odh_host::set_text_overlay_backend({source_metrics,record_draw,source_ranges});
    const auto live_measure=odh_host::catalog_info_text_measure(14,true);
    require(live_measure("Paragraph",530)==121); //7 lines at17px advances retain the full19px shaped glyph viewport.
    require(odh_host::catalog_info_text_measure(14)("Paragraph",530)==102);
    text_popup.text_only_live=true;text_popup.description="Paragraph";
    const auto observed=odh_host::catalog_info_layout(1024,768,text_popup,live_measure);
    require(observed.panel.x==465&&observed.panel.y==507&&observed.panel.width==559&&observed.panel.height==164);
    require(observed.name.x==480&&observed.name.y==519&&observed.body.x==480&&observed.body.y==539);
    odh::RasterFrame frame;frame.width=1024;frame.height=768;frame.pixels.resize(1024*768);
    odh_host::catalog_info_text_painter(14,true)(frame,"Paragraph",observed.body,false);
    require(text_draws.size()==7);
    for(std::size_t n=0;n<text_draws.size();++n){const auto& draw=text_draws[n];
        require(draw.font_size_px==13&&draw.y==539+int(n)*17&&draw.height==19);
        require(draw.pixel_scroll_y==int(n)*19&&draw.foreground==std::array<std::uint8_t,4>{195,205,205,255});}
    text_draws.clear();
    odh_host::catalog_info_scroll_painter(14,true)(frame,"Paragraph",{480,539,530,40},false,20);
    require(text_draws.size()==3&&text_draws[0].y==539&&text_draws[0].height==16&&text_draws[0].pixel_scroll_y==22);
    require(text_draws.back().y==570&&text_draws.back().height==9);
    text_draws.clear();
    odh_host::catalog_info_text_painter(14)(frame,"Paragraph",observed.body,false);
    require(text_draws.size()==1&&text_draws[0].font_size_px==11&&!text_draws[0].pixel_scroll_y);
    require(text_draws[0].foreground==std::array<std::uint8_t,4>{245,245,235,255});
    // Mixed fallback heights must retain the actual full shaped document;
    // a backend without optional ranges remains substitutable as well.
    require(live_measure("Mixed é العربية",530)==63);
    text_draws.clear();odh_host::catalog_info_scroll_painter(14,true)(frame,"Mixed é العربية",observed.body,false,20);
    require(text_draws.size()==1&&text_draws[0].font_size_px==13&&text_draws[0].pixel_scroll_y==20&&last_text=="Mixed é العربية");
    odh_host::set_text_overlay_backend({source_metrics,record_draw});
    require(live_measure("Paragraph",530)==133);
    text_draws.clear();odh_host::catalog_info_text_painter(14,true)(frame,"Paragraph",observed.body,false);
    require(text_draws.size()==1&&!text_draws[0].pixel_scroll_y);
    if(previous.measure&&previous.draw)odh_host::set_text_overlay_backend(previous);
    std::cout<<"PC expansion data/selection contracts PASS\n";
}
