/* SPDX-License-Identifier: MPL-2.0 */
// Actual neighborhood wire roundtrip + original contact eligibility/protocol.
// No commercial assets and no avatar/relationship mutations are required.
#include "files/formats/iff/chunks/NBRS.h"
#include "core/io.h"
#include "app/ui/OriginalDialogButtons.h"
#include "phonebook_candidates.hpp"
#include "linux_vm_dialog.hpp"
#include "app/ui/OriginalTextBackend.h"
#include "app/LotSession.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMDialogOperand.h"
#include <iostream>
#include <stdexcept>
using namespace odh_host;
using FSO::Files::Formats::IFF::Chunks::Neighbour;
static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static std::vector<uint8_t> encode(Neighbour& n){st::Writer out(st::Endian::Little);n.Save(out);return out.ToArray();}
static void wire_order(){
    Neighbour original;original.Version=4;original.Name="Wire";original.PersonMode=1;original.PersonData.resize(88);original.NeighbourID=9;original.GUID=0x12345678;
    original.Relationships={{34,{4,-7,900}},{2,{-100,50}},{19,{0}}};original.RelationshipOrder={34,2,19};
    const auto bytes=encode(original);st::Reader input(bytes,st::Endian::Little);Neighbour read(input);
    require(read.OrderedRelationshipKeys()==std::vector<int32_t>({34,2,19}),"NBRS phone contacts lost authored wire order");
    require(read.Relationships==original.Relationships,"NBRS roundtrip changed relationship values");
    read.Relationships[11]={1,2,3};read.RelationshipOrder.push_back(11);read.Relationships.erase(2);
    read.RelationshipOrder.push_back(34);read.RelationshipOrder.push_back(999); // stale bookkeeping never manufactures wire entries
    const auto next=encode(read);st::Reader second(next,st::Endian::Little);Neighbour reopened(second);
    require(reopened.OrderedRelationshipKeys()==std::vector<int32_t>({34,19,11}),"NBRS new relation/dropped-key order wrong");
    require(reopened.Relationships==read.Relationships&&encode(reopened)==next,"NBRS second roundtrip changed keys/values");
    original.RelationshipOrder.clear();const auto observed=original.OrderedRelationshipKeys();
    const auto compatibility=encode(original);st::Reader oldInput(compatibility,st::Endian::Little);Neighbour old(oldInput);
    require(old.OrderedRelationshipKeys()==observed&&encode(old)==compatibility,"legacy programmatic relationship enumeration did not stabilize on save");
}
static void contacts_and_response(){
    PhonebookSnapshot snapshot;snapshot.caller_current=snapshot.relationships_current=true;
    snapshot.relationship_keys={34,2,19,4,5};
    snapshot.neighbors={{34,340,true,8,1},{2,20,true,8,0},{19,190,true,0,0},{4,40,true,8,2},{5,50,false,0,0}};
    const auto names=[](int16_t id,uint32_t)->std::optional<std::string>{return "Person "+std::to_string(id);};
    const auto sourceKeys=snapshot.relationship_keys;
    const auto selected=select_phonebook_choices(snapshot,names);
    require(selected.ready&&selected.choices.size()==2&&selected.choices[0].value==34&&selected.choices[1].value==2,"phone book must keep directional source order and filter only unassigned/pets");
    require(snapshot.relationship_keys==sourceKeys,"phone book changed source authority");
    const auto call=phonebook_response(selected,size_t(1));const auto cancel=phonebook_response(selected,{});
    require(call&&call->code==1&&call->text=="2","Alex selected phone contact response is code1/NID");
    require(cancel&&cancel->code==0&&cancel->text=="-1","Alex phone cancellation is code0/text-1");
    require(!phonebook_response(selected,size_t(2)),"arbitrary unoffered contact accepted");
    auto broken=snapshot;broken.neighbors.erase(broken.neighbors.begin());
    auto absent=select_phonebook_choices(broken,names);require(!absent.ready&&absent.choices.empty(),"missing relationship neighbor leaked a partial contact list");
    auto alias=snapshot;alias.relationship_keys.push_back(65536+34);require(!select_phonebook_choices(alias,names).ready,"signed-short contact aliases were accepted");
    auto stale=snapshot;stale.caller_current=false;require(!select_phonebook_choices(stale,names).ready,"stale caller authority accepted");
    const auto unnamed=select_phonebook_choices(snapshot,[](int16_t id,uint32_t)->std::optional<std::string>{if(id==2)return {};return "Known";});
    require(!unnamed.ready&&unnamed.choices.empty(),"missing admitted personal name exposed partial contacts");
    PhonebookSnapshot empty;empty.caller_current=empty.relationships_current=true;
    const auto none=select_phonebook_choices(empty,names);require(none.ready&&none.choices.empty()&&!phonebook_response(none,size_t(0)),"empty phonebook manufactured a contact");
    require(phonebook_response(none,{})->text=="-1"&&phonebook_response(absent,{})->text=="-1","cancel unavailable phonebook got stuck");
}
static void message_buttons(){
    using simapp::original::DialogButtonLabels;
    const std::string unrelated="Nice one, Ace! Repairs cost $2000.";
    const auto promotion=DialogButtonLabels(0,"",unrelated,"also unused","OK","Yes","No","Cancel");
    require(promotion.size()==1&&promotion[0].Label=="OK"&&promotion[0].Code==0,"Message interpreted unused No/Cancel strings as buttons");
    const auto named=DialogButtonLabels(0,"Continue",unrelated,"unused","OK","Yes","No","Cancel");
    require(named.size()==1&&named[0].Label=="Continue","Message lost its authored primary label");
    const auto choice=DialogButtonLabels(2,"","","","OK","Sim","Não","Cancelar");
    require(choice.size()==3&&choice[1].Label=="Não"&&choice[2].Code==2,"YesNoCancel defaults/codes changed");
    const auto input=DialogButtonLabels(3,"",unrelated,"unused","OK","Yes","No","Cancel");
    require(input.size()==1&&input[0].Code==0,"TextEntry gained unrelated buttons");
}
static void selector_buttons_and_ticket(){
    simapp::original::InstallTextBackendIfNeeded();
    odh::BitmapImage strip;strip.width=260;strip.height=33;strip.bit_count=32;strip.pixels.assign(260*33,{10,20,30,255});
    for(int y=0;y<33;++y)for(int x=195;x<260;++x)strip.pixels[size_t(y)*260+x]={80,90,100,255};
    for(int contacts:{0,1})for(bool picked:{false,true}){
        if(!contacts&&picked)continue;
        odh::DialogState state;state.instruction.operands[5]=10;state.prompt.title="Call Neighbour";state.prompt.yes="Call";state.prompt.cancel="Cancel";
        if(contacts)state.prompt.choices.push_back({"Actual offered person",34});
        if(picked)state.input="34";
        VmDialogAppearance original;original.button_strip=&strip;original.phonebook_cancel_button=true;
        const auto layout=vm_message_dialog_layout(800,600,state,1,original);require(bool(layout)&&layout->response_count==2,"PhoneBook lost explicit Call+Cancel");
        const auto call=vm_dialog_button_rect(*layout,10,0),cancel=vm_dialog_button_rect(*layout,10,1);
        require(call.x+call.width<=cancel.x,"PhoneBook Call and Cancel overlap");
        const auto hit=vm_dialog_button_hit(*layout,10,cancel.x+cancel.width/2,cancel.y+cancel.height/2);
        require(hit&&*hit==1,"painted Cancel is not the Cancel pointer hit");
        odh::RasterFrame image;image.width=800;image.height=600;image.pixels.assign(800*600,{0,0,0,255});
        draw_vm_dialog(image,state,1,0,original);
        const auto pixel=image.pixels[size_t(call.y+1)*800+call.x+1];
        require(pixel[0]==(picked?10:80),"Call must use disabled original frame until explicit selection, including zero contacts");
        const auto cancelPixel=image.pixels[size_t(cancel.y+1)*800+cancel.x+1];require(cancelPixel[0]==10,"Cancel must remain enabled with zero contacts");
        PhonebookSelection offered;offered.ready=true;if(contacts)offered.choices.push_back({"Actual offered person",34});
        const auto response=phonebook_button_response(offered,*hit,picked?std::optional<size_t>(0):std::nullopt);
        require(response&&response->code==0&&response->text=="-1","Cancel pointer invoked a call instead of original code0/text-1");
        require(bool(phonebook_button_response(offered,0,picked?std::optional<size_t>(0):std::nullopt))==picked,"Call accepted an implicit/unoffered selection");
        const auto legacy=vm_message_dialog_layout(800,600,state,1);require(legacy&&legacy->response_count==1,"legacy generic selector contract changed without opting in");
    }
    using FSO::SimAntics::Engine::VMThread;using FSO::SimAntics::Primitives::VMDialogResult;
    auto thread=std::make_shared<VMThread>(nullptr,nullptr,16),replacementThread=std::make_shared<VMThread>(nullptr,nullptr,16);
    auto state=std::make_shared<VMDialogResult>(),replacementState=std::make_shared<VMDialogResult>();
    simapp::PendingDialog dialog;dialog.Block=true;dialog.ChoiceCallerGUID=340;dialog.ChoiceCallerNID=34;dialog.ChoiceCallerThread=thread.get();dialog.ChoiceCallerState=state;dialog.ChoiceSourceResolved=true;
    require(dialog.MatchesChoiceSource(340,34,thread.get(),state),"current phonebook callback rejected");
    require(!dialog.MatchesChoiceSource(20,34,thread.get(),state)&&!dialog.MatchesChoiceSource(340,2,thread.get(),state),"reused object identity accepted stale phonebook");
    require(!dialog.MatchesChoiceSource(340,34,replacementThread.get(),state)&&!dialog.MatchesChoiceSource(340,34,thread.get(),replacementState),"old Call/Cancel could answer a replacement thread/dialog ticket");
    dialog.ChoiceSourceResolved=false;require(!dialog.MatchesChoiceSource(340,34,thread.get(),state),"unbound phonebook source accepted");
    dialog.ChoiceCallerState.reset();dialog.ChoiceSourceResolved=false;
    dialog.ResolveChoiceSourceAfterUpdate({}); // First update reset/deleted the original wait.
    dialog.ResolveChoiceSourceAfterUpdate(replacementState); // A later Type10 cannot adopt the old ticket.
    require(dialog.ChoiceSourceResolved&&dialog.ChoiceCallerState.expired()&&!dialog.MatchesChoiceSource(340,34,thread.get(),replacementState),"unbound old phonebook captured a later replacement wait");
    require(!state->Responded&&!replacementState->Responded,"checking phonebook authority changed VM dialog state");
}
int main(){try{wire_order();contacts_and_response();message_buttons();selector_buttons_and_ticket();std::cout<<"phonebook/dialog grouped contract passed: NBRS order+values, eligibility, authority, response/cancel, Message operands\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
