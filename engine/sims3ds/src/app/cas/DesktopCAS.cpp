// SPDX-License-Identifier: MPL-2.0
// Original PC design screens: native artwork and coordinates, one canvas.
#include "app/cas/CASScreen.h"
#include "app/front/FrontUI.h"
#include "app/front/TS1Font.h"
#include <algorithm>
#include <cstdlib>
#ifndef __3DS__
#include "cas_presentation.hpp"
#include "linux_vm_dialog.hpp"
#include "app/ui/OriginalPresentation.h"
#include "app/ui/OriginalDialogArt.h"
#include "app/ui/OriginalInput.h"
#include "app/ui/OriginalTextEncoding.h"
#endif

namespace simapp { namespace cas {
namespace {
using namespace front;
const uint32_t Label=RGBA(187,198,195),Disabled=RGBA(62,89,86);
#ifndef __3DS__
struct OriginalCASDialog {
    odh::DialogState State;
    odh_host::VmDialogAppearance Appearance;
    OriginalCASDialog(const std::string& title,const std::string& message,const std::vector<std::string>& labels){
        State.prompt.title=original::UIString(title);State.prompt.message=original::UIString(message);
        State.instruction.operands[5]=labels.size()==1?0:labels.size()==2?1:2;
        if(!labels.empty())State.prompt.yes=original::UIString(labels[0]);if(labels.size()>1)State.prompt.no=original::UIString(labels[1]);if(labels.size()>2)State.prompt.cancel=original::UIString(labels[2]);
        Appearance=original::DialogAppearance();
        if(!labels.empty())Appearance.ok=State.prompt.yes;
    }
};
std::string CasID(const odh_host::CasWidget& widget){
    using C=odh_host::CasControl;
    switch(widget.control){
    case C::family_name:return "familyname";case C::add:return "add";case C::remove:return "delete";
    case C::edit:return "edit";case C::done:return "done";case C::cancel:return "cancel";
    case C::member:return "member"+std::to_string(widget.index);case C::name:return "firstname";case C::bio:return "bio";
    case C::adult:return "adult";case C::child:return "child";case C::female:return "female";case C::male:return "male";
    case C::dark:return "dark";case C::medium:return "medium";case C::light:return "light";
    case C::body_previous:return "bodyprev";case C::body_next:return "bodynext";
    case C::head_previous:return "headprev";case C::head_next:return "headnext";
    case C::trait:return "trait"+std::to_string(widget.index);
    }
    return {};
}
#endif
size_t PreviousUTF8(const std::string& s,size_t at){if(!at)return 0;--at;while(at&&(uint8_t(s[at])&0xc0)==0x80)--at;return at;}
size_t NextUTF8(const std::string& s,size_t at){if(at>=s.size())return s.size();++at;while(at<s.size()&&(uint8_t(s[at])&0xc0)==0x80)++at;return at;}
void Slice(Canvas& dst,const Canvas& src,const Rect& from,int x,int y) {
    if(from.X<0 || from.Y<0 || from.Right()>src.Width || from.Bottom()>src.Height) return;
    for(int row=0;row<from.H;++row)
        dst.Blit(src.Px.data()+size_t(from.Y+row)*src.Width+from.X,from.W,1,x,y+row,true);
}
}

#ifndef __3DS__
struct CASScreen::OriginalDesktopPresentation {
    std::unique_ptr<odh_host::OriginalUiArt> Art;
    odh_host::CasPointer Pointer;
    std::string Controls;
    std::vector<odh::BitmapImage> Faces;
    odh::BitmapImage Preview;
};
void CASScreen::OpenDesktopQuestion(odh_host::CasQuestion question,std::function<void(int)> answer){
    const auto text=odh_host::cas_question_text(question);bool ok=odh_host::cas_question_uses_ok_cancel(question);
    OpenDialog(_Art.Str(text.character?130:129,int(text.title),""),_Art.Str(text.character?130:129,int(text.message),""),
        {_Art.Str(152,ok?0:2,ok?"OK":"Yes"),_Art.Str(152,ok?1:3,ok?"Cancel":"No")},std::move(answer));
}
void CASScreen::UpdateDesktopPointer(const AppInput& in){
    if(!_OriginalDesktop)_OriginalDesktop=std::make_shared<OriginalDesktopPresentation>();auto& p=*_OriginalDesktop;
    std::string signature;for(const auto& c:_Controls)signature+=c.Id+":";
    if(signature!=p.Controls){p.Pointer.reset();p.Controls=signature;_Pressed.clear();_Touching=false;}
    auto input=original::PointerInput(in);
    if(input.pointer_cancelled||input.pointer_overflow){p.Pointer.reset();_Pressed.clear();_Touching=_Dragging=false;_MouseX=_MouseY=-1;return;}
    auto hit=[&](int x,int y)->std::optional<size_t>{for(size_t i=_Controls.size();i-->0;)if(_Controls[i].Enabled&&_Controls[i].R.Contains(x,y))return i;return {};};
    if(in.TouchDown){_Touching=true;_Dragging=false;_TouchX0=_TouchLastX=in.TouchX;auto index=hit(in.TouchX,in.TouchY);_Pressed=index?_Controls[*index].Id:std::string();
}
    else if(in.Touching&&_Touching){_TouchLastX=in.TouchX;}
    auto click=p.Pointer.click_on(input,hit);
    if(click&&!_Dragging){auto index=hit((*click)[0],(*click)[1]);if(index){auto id=_Controls[*index].Id;_Pressed.clear();OnTap(id,(*click)[0],(*click)[1]);}}
    if(in.TouchUp){_Touching=_Dragging=false;_Pressed.clear();}
}
#endif

std::string* CASScreen::DesktopText() {
    if(_TextField=="familyname") return &_Family.LastName;
    if(_TextField=="firstname") return &_Edit.FirstName;
    if(_TextField=="bio") return &_Edit.Bio;
    return nullptr;
}
void CASScreen::EndDesktopText(bool cancel) {
    if(auto* value=DesktopText()) {
        if(cancel) *value=_TextBefore;
        else if(_TextField!="bio") {
            size_t a=value->find_first_not_of(" \t\r\n"),b=value->find_last_not_of(" \t\r\n");
            *value=a==std::string::npos?std::string():value->substr(a,b-a+1);
        }
    }
    _TextField.clear(); _TextBefore.clear(); _TextCursor=0;
}
void CASScreen::UpdateDesktopText(const AppInput& in) {
    using Event=AppInput::TextEvent;
    auto edits=in.TextEvents;
    if(edits.empty()) {
        if(!in.TextUTF8.empty()) edits.push_back({Event::Type::Insert,in.TextUTF8});
        if(in.TextBackspace) edits.push_back({Event::Type::Backspace,{}});
        if(in.TextDelete) edits.push_back({Event::Type::Delete,{}});
        if(in.TextHome) edits.push_back({Event::Type::Home,{}});
        if(in.TextEnd) edits.push_back({Event::Type::End,{}});
        if(in.TextLeft) edits.push_back({Event::Type::Left,{}});
        if(in.TextRight) edits.push_back({Event::Type::Right,{}});
        if(in.TextAccept) edits.push_back({Event::Type::Accept,{}});
        if(in.TextCancel) edits.push_back({Event::Type::Cancel,{}});
    }
    for(const auto& event:edits) {
        auto* value=DesktopText(); if(!value) break;
        _TextCursor=std::min(_TextCursor,value->size());
        switch(event.Kind) {
        case Event::Type::Insert: {
            auto text=event.Text;
            if(_TextField!="bio") text.erase(std::remove(text.begin(),text.end(),'\n'),text.end());
            size_t limit=_TextField=="bio"?200:20;
            size_t characters=0;for(size_t at=0;at<value->size();at=NextUTF8(*value,at))++characters;
            size_t keep=0;while(keep<text.size()&&characters<limit){keep=NextUTF8(text,keep);++characters;}text.resize(keep);
            value->insert(_TextCursor,text); _TextCursor+=text.size(); break;
        }
        case Event::Type::Backspace: if(_TextCursor){auto prev=PreviousUTF8(*value,_TextCursor);value->erase(prev,_TextCursor-prev);_TextCursor=prev;}break;
        case Event::Type::Delete: if(_TextCursor<value->size())value->erase(_TextCursor,NextUTF8(*value,_TextCursor)-_TextCursor);break;
        case Event::Type::Home: _TextCursor=0; break;
        case Event::Type::End: _TextCursor=value->size(); break;
        case Event::Type::Left: _TextCursor=PreviousUTF8(*value,_TextCursor);break;
        case Event::Type::Right: _TextCursor=NextUTF8(*value,_TextCursor);break;
        case Event::Type::Accept:
            if(_TextField=="bio" && value->size()<200) value->insert(_TextCursor++,1,'\n');
            else EndDesktopText(false);
            break;
        case Event::Type::Cancel: EndDesktopText(true); break;
        }
    }
}

void CASScreen::ScrollDesktopDialog(int wheel) {
    _DialogFirstLine=std::max(0,_DialogFirstLine-wheel*3);
}

void CASScreen::LayoutDesktop() {
    _Controls.clear();
    auto add=[this](int x,int y,int w,int h,const std::string& id,bool enabled=true) {
        _Controls.push_back({Rect(_DesktopX+x,_DesktopY+y,w,h),id,enabled});
    };
    if(_Dialog.Open) {
#ifndef __3DS__
        OriginalCASDialog presentation(_Dialog.Title,_Dialog.Text,_Dialog.Buttons);
        auto layout=odh_host::vm_message_dialog_layout(_DesktopWidth,_DesktopHeight,presentation.State,original::TextLanguage(),presentation.Appearance);
        if(layout)for(size_t i=0;i<_Dialog.Buttons.size();++i){
            auto r=odh_host::vm_dialog_button_rect(*layout,presentation.State.instruction.operands[5],i);
            _Controls.push_back({Rect(r.x,r.y,r.width,r.height),"dlg"+std::to_string(i),true});
        }
#endif
        return;
    }
#ifndef __3DS__
    bool character=_Page==Page::Sim;
    for(const auto& widget:odh_host::cas_layout(character,_Family.Members.size())){
        const auto& r=widget.box;
        bool enabled=odh_host::cas_control_enabled(widget.control,character,!_Family.LastName.empty(),!_Edit.FirstName.empty(),
            _Family.Members.size(),_Selected>=0&&_Selected<int(_Family.Members.size()));
        add(r.x,r.y,r.width,r.height,CasID(widget),enabled);
    }
#endif
}

void CASScreen::DrawDesktop(Canvas& c) {
    using namespace front;
    _DesktopWidth=c.Width;_DesktopHeight=c.Height;
    _DesktopX=std::max(0,(c.Width-800)/2);_DesktopY=std::max(0,(c.Height-600)/2);LayoutDesktop();
    c.Clear(RGBA(0,0,45));
#ifndef __3DS__
    if(!_OriginalDesktop)_OriginalDesktop=std::make_shared<OriginalDesktopPresentation>();auto& p=*_OriginalDesktop;
    if(!p.Art)p.Art=std::make_unique<odh_host::OriginalUiArt>(_Cfg.DataRoot,odh_host::cas_art_keys());
    if(!_Ready){TextCentered(c,TS1Size::S12,Rect(20,c.Height/2-30,c.Width-40,60),Error,Label);return;}
    odh_host::CasPresentation view;bool sim=_Page==Page::Sim;view.character=sim;
    view.family_name=_Family.LastName;view.name=_Edit.FirstName;view.biography=_Edit.Bio;
    for(size_t i=0;i<view.traits.size();++i)view.traits[i]=_Edit.Traits[i];
    view.controls=odh_host::cas_layout(sim,_Family.Members.size());
    for(int i=0;i<(sim?28:15);++i)view.labels.push_back(original::LegacyToUTF8(_Art.Str(sim?130:129,i,"")));
    view.cursor=_TextCursor;
    view.selected=_Selected>=0?std::optional<size_t>{size_t(_Selected)}:std::nullopt;
    if(!_Dialog.Open&&_MouseX>=0&&_MouseY>=0)view.hovered=odh_host::cas_widget_at(view.controls,_MouseX-_DesktopX,_MouseY-_DesktopY);
    for(size_t i=0;i<view.controls.size();++i){auto id=CasID(view.controls[i]);if(_TextField==id)view.focus=i;if(_Touching&&_Pressed==id)view.pressed=i;}
    view.enabled=[&](odh_host::CasControl control){return odh_host::cas_control_enabled(control,sim,!_Family.LastName.empty(),!_Edit.FirstName.empty(),_Family.Members.size(),_Selected>=0&&_Selected<int(_Family.Members.size()));};
    view.on=[&](odh_host::CasControl control){using C=odh_host::CasControl;return (control==C::male&&_Edit.Gender==CASGender::Male)||(control==C::female&&_Edit.Gender==CASGender::Female)||
        (control==C::adult&&_Edit.Age==CASAge::Adult)||(control==C::child&&_Edit.Age==CASAge::Child)||(control==C::light&&_Edit.Skin==CASSkin::Light)||
        (control==C::medium&&_Edit.Skin==CASSkin::Medium)||(control==C::dark&&_Edit.Skin==CASSkin::Dark);};
    p.Faces.clear();for(size_t i=0;i<_Family.Members.size();++i){view.members.push_back(_Family.Members[i].FirstName);p.Faces.push_back(i<_DesktopFaces.size()?original::Bitmap(_DesktopFaces[i]):odh::BitmapImage{});}
    view.portrait=[&](size_t i)->const odh::BitmapImage*{return i<p.Faces.size()&&p.Faces[i].width?&p.Faces[i]:nullptr;};
    view.body_preview=[&]()->const odh::BitmapImage*{
        Canvas preview(800,600);const auto& box=odh_host::cas_preview_box;
        // The preserved CAS painter keys out magenta to retain the authored
        // gradient. Canvas defaults to opaque black, which hides that backdrop.
        preview.Clear(RGBA(255,0,255));
        _Preview.DrawOriginalBody(preview,Rect(box.x,box.y,box.width,box.height),_Rot,_Cfg.Bilinear);
        Canvas cropped;Crop(preview,cropped,box.x,box.y,box.width,box.height);p.Preview=original::Bitmap(cropped);return &p.Preview;
    };
    auto frame=view.draw(*p.Art,original::TextLanguage());Canvas surface(800,600);original::Copy(frame,surface);c.Blit(surface.Px.data(),800,600,_DesktopX,_DesktopY,false);
#endif
#ifndef __3DS__
    if(_Dialog.Open){
        OriginalCASDialog presentation(_Dialog.Title,_Dialog.Text,_Dialog.Buttons);
        if(_MouseX>=0&&_MouseY>=0)presentation.Appearance.response_pointer=std::array<int,2>{{_MouseX,_MouseY}};
        if(_Touching&&_Pressed.compare(0,3,"dlg")==0)presentation.Appearance.pressed_response=std::size_t(std::atoi(_Pressed.c_str()+3));
        const auto layout=odh_host::vm_message_dialog_layout(c.Width,c.Height,presentation.State,original::TextLanguage(),presentation.Appearance);
        if(layout){
            const auto result=original::PaintDialog(c,presentation.State,original::TextLanguage(),_DialogFirstLine,presentation.Appearance,*layout);
            _DialogFirstLine=std::min(_DialogFirstLine,int(result.max_first_line));
        }
    }
#endif
}

}}
