// SPDX-License-Identifier: MPL-2.0
#include "odh/buy_mode.hpp"
#include <iostream>
#include <stdexcept>
namespace {
using namespace odh;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F fn){try{fn();}catch(const FormatError&){return;}throw std::runtime_error("invalid catalog was accepted");}
std::vector<BuyCatalogItem> catalog(){return {{11,100,0,1,"Chair A","Description"},{12,80,0,2,"Chair B","Other"},{13,80,0,4,"Chair C",{}},{14,1000,1,1,"Table",{}}};}
void purchase_and_cancel(){BuyModeController ui(catalog(),2);require(!ui.active()&&!ui.take_command(),"buy mode initially has no authority or intent");ui.open(90);
    auto page=ui.visible();require(page.size()==2&&page[0]->guid==12&&page[1]->guid==13&&ui.page_count()==2,"stable source price ordering and bounded pages");
    require(ui.select(0)&&!ui.place()&&ui.issue()==BuyModeIssue::no_ground&&!ui.take_command(),"catalog selection does not charge or place before a world point");
    ui.hover(EntityPosition{392,440,2,0});ui.rotate(1);require(ui.held()->position->direction==2&&ui.place(),"pure preview and quarter rotation produce one source command");
    const auto command=ui.take_command();require(command&&command->guid==12&&command->kind==BuyModeCommandKind::purchase&&command->position==EntityPosition{392,440,2,2},"exact selected GUID and pose reach the existing transaction service");
    require(!ui.take_command()&&!ui.place()&&!ui.sell(),"already dispatched command cannot be repeated by held mouse/keyboard");ui.cancel();require(ui.phase()==BuyModePhase::awaiting_command,"Escape cannot undo a command after dispatch");
    ui.command_rejected("actual source placement rejected");require(ui.phase()==BuyModePhase::holding_purchase&&ui.held()->direction==2&&ui.issue()==BuyModeIssue::operation_failed,"real failure keeps selection and does not manufacture success");
    ui.cancel();require(ui.phase()==BuyModePhase::browsing&&!ui.held()&&!ui.take_command(),"cancel discards only isolated preview");ui.cancel();require(!ui.active(),"second cancel returns to live mode");
    ui.open(90);ui.page(1);require(ui.visible().front()->guid==11&&ui.select(0),"page navigation selects data rather than a fixed demo object");ui.hover(EntityPosition{8,8,1,0});require(!ui.place()&&ui.issue()==BuyModeIssue::insufficient_funds&&!ui.take_command(),"read-only affordability feedback cannot debit funds");
    ui.cancel();ui.category(1);require(ui.visible().size()==1&&ui.visible()[0]->guid==14,"source catalog categories select their own complete set");ui.set_page_size(16);ui.page(INT32_MAX);require(ui.page()==0,"page clamping has no overflow or phantom entries");}
void existing_move_sell(){BuyModeController ui(catalog());ui.open(665);BuyModeExistingObject original{{42,99},11,{392,440,1,6},3,{1,2}};
    require(ui.select_existing(original),"existing object uses actual identity and graphic");ui.hover(EntityPosition{408,456,1,0});ui.rotate(-1);require(ui.place(),"existing object move/rotation is previewed without a World write");auto command=ui.take_command();
    require(command&&command->kind==BuyModeCommandKind::move&&command->target.id==42&&command->target.generation==99&&command->position.direction==4&&original.position==EntityPosition{392,440,1,6},"move emits exact generation; original source pose is unchanged");
    ui.command_rejected("blocked");ui.cancel();require(ui.select_existing(original)&&ui.sell(),"sell uses selected source object rather than a catalog purchase");command=ui.take_command();require(command&&command->kind==BuyModeCommandKind::sell&&command->target.id==42,"sale routes the original identity once");
    ui.command_rejected("in use");ui.close();require(!ui.active()&&!ui.held()&&!ui.take_command(),"closing rejected preview never emits another transaction");ui.open(665);
    auto stale=original;stale.identity.generation=0;require(!ui.select_existing(stale)&&!ui.held(),"unknown generation cannot become a property command");
    require(ui.select(0),"pick a purchase again");ui.hover(EntityPosition{1,2,1,0},BuyPreviewValidity::blocked);require(!ui.place(),"known blocked visual point is not submitted");
    ui.hover(EntityPosition{1,2,1,0},BuyPreviewValidity::unknown);require(ui.place(),"unproven preview delegates final validation to actual native transaction");}
void group_preview_and_clicked_target(){
    BuyModePreviewGroup group;group.base_index=1;
    group.parts={{201,0,0,0,3,{1}},{202,16,0,0,4,{}},{203,16,16,1,5,{0,1}}};
    for(const std::uint8_t direction:{0,2,4,6}){
        const auto positions=buy_mode_group_positions(group,{32,48,1,direction});
        EntityPosition first{16,48,1,direction},last{32,64,2,direction};
        if(direction==2){first={32,32,1,direction};last={16,48,2,direction};}
        if(direction==4){first={48,48,1,direction};last={32,32,2,direction};}
        if(direction==6){first={32,64,1,direction};last={48,48,2,direction};}
        require(positions.size()==3&&positions[0]==first&&positions[1]==EntityPosition{32,48,1,direction}&&positions[2]==last,
            "all preview parts rotate around the actual base using source placement coordinates");
    }
    auto wrap=group;wrap.parts[2].offset_x=32;
    require(buy_mode_group_positions(wrap,{32760,48,127,0})[2]==EntityPosition{-32760,64,-128,0},
        "preview reproduces source signed-word/signed-byte wrap without undefined overflow");
    BuyModeController ui(catalog());ui.open(500);
    BuyModeExistingObject clicked{{42,7},201,{32,48,1,0},3,{1},group};
    require(ui.select_existing(clicked)&&ui.held()->preview_group&&ui.held()->preview_group->base_index==1,
        "holder retains complete preview group while keeping the clicked identity distinct from its base");
    clicked.preview_group->parts.clear();
    require(ui.held()->preview_group->parts.size()==3,"holder owns metadata rather than borrowing a mutable group");
    ui.hover(EntityPosition{80,96,1,0});ui.rotate(1);require(ui.place(),"complete preview submits one move");
    auto move=ui.take_command();require(move&&move->target.id==42&&move->target.generation==7&&move->guid==201&&
        move->position==EntityPosition{80,96,1,2},"one group move preserves clicked target and independent base anchor");
    auto bad=group;bad.base_index=3;rejects([&]{buy_mode_group_positions(bad,{32,48,1,0});});
    bad=group;bad.parts.resize(65);rejects([&]{buy_mode_group_positions(bad,{32,48,1,0});});
    bad=group;bad.parts[0].dynamic_sprite_flags.resize(4097);rejects([&]{buy_mode_group_positions(bad,{32,48,1,0});});
}

void native_selected_preview(){
    BuyModePreviewGroup group;group.base_index=1;group.native_head_heading_delta=2;group.native_selected_heading=0;
    group.parts={{201,123,-456,7,3,{1}},{202,-300,99,-3,4,{}},{203,4,5,6,5,{0,1}}};
    group.parts[0].native=BuyModeNativePreviewPart{{10,20,4},2,NativeObjectGroupOffset{5,7,10}};
    group.parts[1].native=BuyModeNativePreviewPart{{11,20,5},0,NativeObjectGroupOffset{6,9,11}};
    group.parts[2].native=BuyModeNativePreviewPart{{11,22,6},6,NativeObjectGroupOffset{9,9,13}};
    const auto before=buy_mode_group_positions(group,{80,96,3,0});
    require(before==std::vector<EntityPosition>({{64,64,2,2},{80,96,3,0},{128,96,5,6}}),
        "zero-turn native preview preserves deliberately different imported current triples and existing relative headings");
    BuyModeController ui(catalog());ui.open(500);BuyModeExistingObject clicked{{42,7},202,{32,48,3,0},4,{},group};
    require(ui.select_existing(clicked),"native selected non-head/non-price-base preview admitted");
    ui.hover(EntityPosition{80,96,3,0});ui.rotate(4);
    require(buy_mode_group_positions(*ui.held()->preview_group,*ui.held()->position)==before,
        "full turn back to baseline preserves imported current fields exactly as backend delta0");
    ui.rotate(1);
    const auto rotated=buy_mode_group_positions(*ui.held()->preview_group,*ui.held()->position);
    require(rotated==std::vector<EntityPosition>({{96,96,2,4},{80,96,3,2},{80,64,4,0}}),
        "native quarter-turn preview rotates head basis and each body while keeping selected anchor fixed");
    require(ui.place(),"native selected preview emits real command intent");const auto command=ui.take_command();
    require(command&&command->target.id==42&&command->target.generation==7&&command->position==rotated[1]&&
        !(command->position==rotated[2]),"native command and preview agree on selected anchor independently of another part's price-base position");
    const auto odd=buy_mode_group_positions(group,{80,96,3,1});
    require(odd==std::vector<EntityPosition>({{80,96,2,3},{80,96,3,1},{80,96,4,7}}),
        "native odd head direction zeroes XY but preserves relative levels and individual headings");
    auto wrap=group;wrap.native_head_heading_delta=0;wrap.native_selected_heading=1;
    wrap.parts[0].native=BuyModeNativePreviewPart{{INT32_MIN,INT32_MAX,-32768},0,NativeObjectGroupOffset{5,7,10}};
    wrap.parts[1].native=BuyModeNativePreviewPart{{INT32_MAX,INT32_MIN,32767},0,NativeObjectGroupOffset{6,9,11}};
    require(buy_mode_group_positions(wrap,{32760,-32760,127,0})[0]==EntityPosition{-32760,32760,-128,0},
        "full int32 authored differences match final native coordinate wrap without overflow or early narrowing");
    auto bad=group;bad.parts[2].native.reset();rejects([&]{buy_mode_group_positions(bad,{80,96,3,0});});
    bad=group;bad.native_head_heading_delta.reset();rejects([&]{buy_mode_group_positions(bad,{80,96,3,0});});
    bad=group;bad.native_head_heading_delta=8;rejects([&]{buy_mode_group_positions(bad,{80,96,3,0});});
    bad=group;bad.parts[2].native->heading_delta=8;rejects([&]{buy_mode_group_positions(bad,{80,96,3,0});});
    bad=group;bad.parts[1].native->heading_delta=1;rejects([&]{buy_mode_group_positions(bad,{80,96,3,0});});
    bad=group;bad.native_selected_heading.reset();rejects([&]{buy_mode_group_positions(bad,{80,96,3,0});});
    bad=group;bad.native_selected_heading=8;rejects([&]{buy_mode_group_positions(bad,{80,96,3,0});});
    bad=group;bad.parts[2].native->current.reset();rejects([&]{buy_mode_group_positions(bad,{80,96,3,2});});
}

}
int main(){try{purchase_and_cancel();existing_move_sell();group_preview_and_clicked_target();native_selected_preview();rejects([]{BuyModeController bad({{1,10,0,0,"A",{}},{1,20,0,0,"B",{}}});});rejects([]{BuyModeController bad(catalog(),0);});std::cout<<"buy mode controller fixtures passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
