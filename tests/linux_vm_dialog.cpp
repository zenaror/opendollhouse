#include "linux_vm_dialog.hpp"
#include "odh/vm.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}

int main(){
    try{
        require(!odh_host::vm_message_dialog_layout(200,600,{},14),"too-narrow frame accepted");
        auto layout=odh_host::vm_message_dialog_layout(800,600,{},14);
        require(layout.has_value(),"ordinary game frame rejected");
        {
            odh::RasterFrame frame;frame.width=800;frame.height=600;
            frame.pixels.assign(800*600,{20,30,40,255});
            odh::BitmapImage tiles{};tiles.width=tiles.height=36;tiles.bit_count=24;
            for(unsigned y=0;y<36;++y)for(unsigned x=0;x<36;++x)
                tiles.pixels.push_back({std::uint8_t(y/12*80),std::uint8_t(x/12*60),std::uint8_t(50+x%12),255});
            odh_host::VmDialogAppearance appearance;appearance.panel_tiles=&tiles;
            odh::DialogState state;
            odh_host::draw_vm_dialog(frame,state,14,0,appearance);
            const auto pixel=[&](int x,int y){return frame.pixels[std::size_t(y)*800+x];};
            require(pixel(layout->x+3,layout->y+3)==tiles.pixels[3*36+3],
                "popup corner comes from the original atlas");
            require(pixel(layout->x+13,layout->y+1)==pixel(layout->x+25,layout->y+1),
                "popup edge repeats its source tile without stretching");
            require(pixel(layout->x+30,layout->y+80)==std::array<std::uint8_t,4>{80,60,56,255},
                "popup body uses the center source tile instead of debug gray");
            tiles.width=35;bool rejected=false;
            try{odh_host::draw_vm_dialog(frame,state,14,0,appearance);}
            catch(const odh::FormatError&){rejected=true;}
            require(rejected,"malformed popup atlas must be rejected");
        }
        require(2*layout->x+layout->width>=799&&2*layout->y+layout->height>=599&&
            layout->ok_x==(800-layout->ok_width)/2,
            "dialog panel is not centered at the bounded size");
        require(odh_host::vm_message_dialog_ok_hit(*layout,layout->ok_x,layout->ok_y),
            "OK button top-left was not accepted");
        require(!odh_host::vm_message_dialog_ok_hit(*layout,layout->x,layout->y),
            "panel area was confused with the OK button");
        require(odh_host::expand_vm_dialog_local_references(
            "That will be $$Local:1.",{0,99})=="That will be $99.",
            "game dialog Local reference or escaped dollar was not expanded");
        bool unknown_marker_rejected=false;
        try{(void)odh_host::expand_vm_dialog_local_references("$Unknown:1",{});}
        catch(const std::invalid_argument&){unknown_marker_rejected=true;}
        require(unknown_marker_rejected,"unknown game string marker was silently changed");

        odh::RasterFrame frame{};frame.width=800;frame.height=600;
        frame.pixels.assign(800U*600U,{128,128,128,255});
        frame.depth.assign(frame.pixels.size(),1.0f);
        odh::DialogState pizza;pizza.prompt.title="SimCity Pizza";
        pizza.prompt.message="That will be $99. Your pizza should arrive soon.";
        layout=odh_host::vm_message_dialog_layout(800,600,pizza,1);
        odh_host::draw_vm_message_dialog(frame,"SimCity Pizza",
            "That will be $99. Your pizza should arrive soon.","OK",1);
        const auto pixel_at=[&](std::int32_t x,std::int32_t y)->const auto&{
            return frame.pixels[static_cast<std::size_t>(y)*frame.width+static_cast<std::size_t>(x)];
        };
        require(pixel_at(layout->x+5,layout->y+10)==std::array<std::uint8_t,4>{68,72,75,255},
            "dialog panel background was not drawn");
        require(pixel_at(layout->ok_x+4,layout->ok_y+4)==std::array<std::uint8_t,4>{102,76,43,255},
            "dialog OK button was not drawn");
        require(pixel_at(0,0)==std::array<std::uint8_t,4>{128,128,128,255},
            "dialog must preserve the original scene outside its envelope");

        {
            odh::BitmapImage strip{};strip.width=260;strip.height=33;strip.bit_count=24;
            strip.pixels.assign(260*33,{200,0,0});
            for(unsigned y=0;y<33;++y)for(unsigned x=0;x<65;++x)
                strip.pixels[y*260+x]={12,45,78};
            strip.pixels[0]={255,0,255};
            odh_host::VmDialogAppearance appearance{&strip,"OK","Sim","Não","Cancelar"};
            odh::DialogState state;state.instruction.operands[5]=2;
            state.prompt.message="Teste";state.selected_button=1;
            layout=odh_host::vm_message_dialog_layout(800,600,state,14,appearance);
            auto localized=frame;
            odh_host::draw_vm_dialog(localized,state,14,0,appearance);
            for(unsigned n=0;n<3;++n){
                int left=layout->x;
                while(left<layout->x+layout->width&&odh_host::vm_dialog_button_hit(*layout,2,left,layout->ok_y)!=n)++left;
                require(left<layout->x+layout->width,"missing skinned response");
                const auto offset=static_cast<std::size_t>(layout->ok_y)*frame.width+left;
                require(localized.pixels[offset+4]==std::array<std::uint8_t,4>{12,45,78,255}&&
                    localized.pixels[offset]!=std::array<std::uint8_t,4>{255,0,255,255},
                    "each clickable response uses resting strip pixels and keys out magenta");
            }
            state.prompt.yes="Sim";state.prompt.no="Não";state.prompt.cancel="Cancelar";
            auto authored=frame;
            appearance.yes="Other";appearance.no="Different";appearance.cancel="Ignored";
            odh_host::draw_vm_dialog(authored,state,14,0,appearance);
            require(authored.pixels==localized.pixels,"authored labels take precedence over supplied defaults");
            std::string long_label;
            for(unsigned n=0;n<2048;++n)long_label+="ã";
            state.prompt.no=long_label;
            auto extended=frame;
            odh_host::draw_vm_dialog(extended,state,14,0,appearance);
            bool changed=false;
            for(int y=0;y<static_cast<int>(extended.height);++y)for(int x=0;x<static_cast<int>(extended.width);++x){
                const auto i=static_cast<std::size_t>(y)*extended.width+x;
                if(extended.pixels[i]==authored.pixels[i])continue;
                require(odh_host::vm_dialog_button_hit(*layout,2,x,y)==1,"long response spills beyond its own button");
                changed=true;
            }
            require(changed&&state.prompt.no==long_label,"full 4096-byte translated response renders without rewriting it");
            strip.width=259;bool rejected=false;
            try{odh_host::draw_vm_dialog(authored,state,14,0,appearance);}
            catch(const odh::FormatError&){rejected=true;}
            require(rejected,"malformed response strip must be rejected");
        }
        {
            odh::DialogState state;state.instruction={36,254,255,{}};state.instruction.operands[5]=2;
            state.prompt.title="Choose";state.prompt.message="Keep this choice?";state.prompt.yes="Yes";state.prompt.no="No";state.prompt.cancel="Cancel";state.selected_button=1;
            layout=odh_host::vm_message_dialog_layout(800,600,state,1);
            odh_host::draw_vm_dialog(frame,state,1);
            require(odh_host::vm_dialog_button_count(2)==3,"YesNoCancel exposes all3 buttons");
            for(unsigned code=0;code<3;++code){bool found=false;
                for(int x=layout->x;x<layout->x+layout->width;++x){const auto hit=odh_host::vm_dialog_button_hit(*layout,2,x,layout->ok_y+4);if(hit&&*hit==code){found=true;break;}}
                require(found,"each rendered dialog choice has a distinct hit target");
            }
            state.instruction.operands[5]=3;state.input="Typed name";
            odh_host::draw_vm_dialog(frame,state,1);
            require(odh_host::vm_dialog_button_count(3)==1,"text entry has sourceOK only");
        }
        {
            odh::DialogState state;state.instruction.operands[5]=5;
            for(int i=0;i<12;++i)state.prompt.choices.push_back({"Destino "+std::to_string(i),100+i*3});
            require(odh_host::vm_dialog_choice_index(state,-1)==11,"previous from first reaches last destination");
            state.input="130";
            require(odh_host::vm_dialog_choice_index(state)==10,"saved value restores its row");
            require(odh_host::vm_dialog_choice_index(state,2)==0,"destination navigation wraps");
            layout=odh_host::vm_message_dialog_layout(800,600,state,14);
            odh_host::draw_vm_dialog(frame,state,14);
            bool found=false;
            for(int y=layout->y;y<layout->ok_y;++y){
                const auto index=odh_host::vm_dialog_choice_hit(*layout,state,layout->x+24,y);
                if(index&&*index==10)found=true;
            }
            require(found,"selected destination is visible and clickable after reopening");
            require(!odh_host::vm_dialog_choice_hit(*layout,state,layout->ok_x,layout->ok_y),"confirm is not a catalog row");
            odh::RasterFrame small;small.width=240;small.height=160;
            small.pixels.resize(240*160);
            state.prompt.title="Centro";state.prompt.message="Escolha o destino.";
            odh_host::draw_vm_dialog(small,state,14);
            state.prompt.choices.clear();state.prompt.cancel="Cancelar";
            odh_host::draw_vm_dialog(small,state,14);
            require(!odh_host::vm_dialog_choice_hit(*layout,state,layout->x+20,layout->ok_y-8),
                "a cancel-only selector offers no clickable destination");
        }
        {
            odh::DialogState state;state.instruction={36,254,255,{}};state.instruction.operands[5]=2;
            state.prompt.title="Ajuda";state.prompt.yes="Sim";state.prompt.no="Não";state.prompt.cancel="Cancelar";
            for(unsigned n=0;n<12;++n)state.prompt.message+="Linha "+std::to_string(n)+": mensagem completa com acentuação.\n";
            state.prompt.message+="Última linha: texto final acessível.";
            state.selected_button=1;state.ticket=71;
            layout=odh_host::vm_message_dialog_layout(800,600,state,14);
            require(layout->height>odh_host::vm_message_dialog_layout(800,600,pizza,1)->height,
                "longer text enlarges the popup before pagination");
            auto beginning=frame;beginning.pixels.assign(beginning.pixels.size(),{128,128,128,255});
            const auto first_page=odh_host::draw_vm_dialog(beginning,state,14);
            require(first_page.line_count>=13&&first_page.visible_lines>1&&first_page.has_more&&first_page.max_first_line>0,
                "long authored message must wrap into multiple visible lines and expose remaining pages");
            bool lower_text=false;
            for(int y=layout->y+80;y<layout->ok_y-28;++y)for(int x=layout->x+24;x<layout->x+layout->width-24;++x){
                const auto& pixel=beginning.pixels[static_cast<std::size_t>(y)*beginning.width+static_cast<std::size_t>(x)];
                if(pixel[0]>180&&pixel[1]>180&&pixel[2]>180)lower_text=true;
            }
            require(lower_text,"message box must contain real glyph pixels below the first line");
            auto ending=frame;ending.pixels.assign(ending.pixels.size(),{128,128,128,255});
            const auto last_page=odh_host::draw_vm_dialog(ending,state,14,std::numeric_limits<std::size_t>::max());
            require(last_page.first_line==last_page.max_first_line&&!last_page.has_more&&
                last_page.first_line+last_page.visible_lines==last_page.line_count,
                "overscroll clamps to final page with final authored line accessible");
            require(beginning.pixels!=ending.pixels&&state.ticket==71&&state.selected_button==1&&state.prompt.yes=="Sim"&&state.prompt.no=="Não",
                "scroll changes presentation without answering or replacing provided localized buttons");
            state.instruction.operands[5]=3;state.input="Nome digitado";
            const auto input_page=odh_host::draw_vm_dialog(ending,state,14,1);
            require(input_page.visible_lines>0&&state.input=="Nome digitado"&&state.ticket==71,
                "scrolling name-entry dialog preserves editable input and response identity");
            odh::RasterFrame small{};small.width=240;small.height=160;
            small.pixels.assign(240U*160U,{128,128,128,255});small.depth.assign(small.pixels.size(),1.0f);
            require(odh_host::draw_vm_dialog(small,state,14).visible_lines>0,
                "bounded compact text-entry dialog retains message pagination");
            require(std::all_of(small.depth.begin(),small.depth.end(),[](float value){return value==1.0f;}),
                "dialog pagination must not change scene depth");
        }

        {
            odh::BitmapImage art{};art.width=64;art.height=48;art.bit_count=24;art.pixels.assign(64*48,{80,124,18});
            odh_host::VmDialogAppearance appearance;appearance.illustration=&art;
            const auto bounds=odh_host::vm_message_dialog_layout(800,600,pizza,1,appearance);
            auto painted=frame;
            odh_host::draw_vm_dialog(painted,pizza,1,0,appearance);
            require(painted.pixels[(bounds->y+19)*800+bounds->x+19]==
                std::array<std::uint8_t,4>{80,124,18,255},"resolved artwork occupies its own area");
        }
        {
            odh::DialogState state;state.prompt.title="Notice";
            state.prompt.message="This original illustrated notice keeps its actual picture size and its own response.";
            for(const unsigned width:{133U,145U}){
                odh::BitmapImage art{width,103,24,true,
                    std::vector<std::array<std::uint8_t,4>>(width*103,{80,124,18,255}),0};
                art.pixels.back()={14,220,90,255};
                odh_host::VmDialogAppearance appearance;appearance.illustration=&art;
                auto bounds=odh_host::vm_message_dialog_layout(800,600,state,1,appearance);
                require(bounds&&bounds->illustration.width==static_cast<int>(width)&&bounds->illustration.height==103,
                    "original133x103 portrait frame and145x103 indexed art keep authored desktop dimensions");
                require(bounds->illustration.y+bounds->illustration.height<bounds->ok_y,
                    "natural-size artwork fits above real response hit targets");
                auto painted=frame;
                odh_host::draw_vm_dialog(painted,state,1,0,appearance);
                const auto last=std::size_t(bounds->illustration.y+102)*800+bounds->illustration.x+width-1;
                require(painted.pixels[last]==std::array<std::uint8_t,4>{14,220,90,255}&&
                    odh_host::vm_dialog_button_hit(*bounds,0,bounds->ok_x+1,bounds->ok_y+1)==0,
                    "painting retains the last authored image texel while response drawing and picking share layout");
                appearance.center_title=true;
                const auto centered_bounds=odh_host::vm_message_dialog_layout(800,600,state,1,appearance);
                require(centered_bounds->x==bounds->x&&centered_bounds->width==bounds->width&&
                    centered_bounds->ok_x==bounds->ok_x&&centered_bounds->ok_y==bounds->ok_y&&
                    centered_bounds->illustration.width==bounds->illustration.width,
                    "centering a VM notice title does not move artwork or response hit targets");
                auto centered=frame;odh_host::draw_vm_dialog(centered,state,1,0,appearance);
                bool title_changed=false;
                for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x){
                    const auto pixel=std::size_t(y)*frame.width+x;
                    if(centered.pixels[pixel]==painted.pixels[pixel])continue;
                    require(y>=static_cast<unsigned>(bounds->y+12)&&y<static_cast<unsigned>(bounds->y+38),
                        "optional title centering changes only title pixels, preserving existing dialog content");
                    title_changed=true;
                }
                require(title_changed&&painted.depth==frame.depth&&centered.depth==frame.depth,
                    "VM title opt-in is visible and dialog composition preserves scene depth");
                for(const bool input:{false,true}){
                    state.instruction.operands[5]=input?3:0;
                    odh::RasterFrame compact;compact.width=240;compact.height=160;
                    compact.pixels.assign(240*160,{128,128,128,255});compact.depth.assign(240*160,1);
                    const auto small=odh_host::vm_message_dialog_layout(240,160,state,1,appearance);
                    require(small&&small->illustration.width>0&&small->illustration.width<static_cast<int>(width)&&
                        small->illustration.height>0&&small->illustration.height<103&&
                        small->illustration.y+small->illustration.height<small->ok_y,
                        "compact viewport reduces the image proportionally inside the same bounded layout");
                    require(odh_host::draw_vm_dialog(compact,state,1,0,appearance).visible_lines>0&&
                        odh_host::vm_dialog_button_hit(*small,state.instruction.operands[5],small->ok_x+1,small->ok_y+1)==0,
                        "compact illustrated message and text entry keep readable text and a usable real response");
                }
                state.instruction.operands[5]=0;
            }
        }
        {
            odh::BitmapImage art{133,103,24,true,
                std::vector<std::array<std::uint8_t,4>>(133*103,{80,124,18,255}),0};
            odh_host::VmDialogAppearance appearance;appearance.illustration=&art;appearance.center_title=true;
            odh::DialogState state;state.prompt.title="Aviso";state.ticket=37;
            for(unsigned n=0;n<6;++n)state.prompt.message+="Texto completo com palavras que continuam depois da figura. ";
            state.prompt.message+="Fim da mensagem.";
            const auto original=state.prompt.message;
            const auto bounds=odh_host::vm_message_dialog_layout(800,600,state,14,appearance);
            auto painted=frame;
            const auto page=odh_host::draw_vm_dialog(painted,state,14,0,appearance);
            require(!page.has_more&&page.max_first_line==0&&page.line_count==page.visible_lines,
                "illustrated notice uses width beneath the picture so complete fitting text needs no debug pagination");
            bool below_image=false;
            for(int y=bounds->illustration.y+bounds->illustration.height+2;y<bounds->ok_y-6;++y)
                for(int x=bounds->illustration.x+2;x<bounds->illustration.x+bounds->illustration.width-2;++x){
                    const auto& p=painted.pixels[std::size_t(y)*painted.width+x];
                    if(p[0]>180&&p[1]>180&&p[2]>180)below_image=true;
                }
            require(below_image&&painted.pixels[std::size_t(bounds->illustration.y+102)*800+bounds->illustration.x+132]==
                std::array<std::uint8_t,4>{80,124,18,255},
                "text visibly flows into the left area beneath the intact authored illustration");
            require(state.prompt.message==original&&state.ticket==37&&painted.depth==frame.depth,
                "flow wrapping changes only presentation, preserving text, response identity and scene depth");
            state.prompt.message="One\nTwo\n\nThree\nFour\nFive\nSix\nSeven\nEight";
            const auto multiline=odh_host::draw_vm_dialog(painted,state,1,0,appearance);
            require(!multiline.has_more&&multiline.line_count==9&&multiline.visible_lines==9,
                "illustrated flow preserves explicit paragraphs and their blank line using actual one-line font metrics");
            state.prompt.message.clear();
            for(unsigned n=0;n<110;++n)state.prompt.message+="Long text retains every word. ";
            const auto overflow=odh_host::draw_vm_dialog(painted,state,1,0,appearance);
            const auto last=odh_host::draw_vm_dialog(painted,state,1,std::numeric_limits<std::size_t>::max(),appearance);
            require(overflow.has_more&&last.first_line==last.max_first_line&&!last.has_more&&
                last.first_line+last.visible_lines==last.line_count&&state.ticket==37,
                "genuine illustrated overflow keeps complete existing scroll access instead of silently dropping trailing text");
        }
        const auto waiting_vm=[](){
            odh::Instruction request{36,254,255,{}};
            odh::Bhav routine{0x8002,{request},{},odh::Bhav::Metadata{0,4,0,0}};
            const odh::VmPrimitive primitive=[](const auto&,const auto& instruction,const auto&,const auto&){
                return instruction.opcode==36?odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}}:
                    odh::PrimitiveOutcome{};
            };
            return odh::Vm({{odh::routine_key(8312),std::move(routine)}},{},{},primitive);
        };
        auto accepted=waiting_vm();
        accepted.start(odh::routine_key(8312),{105,105,136,0});
        require(accepted.run(1)==odh::VmStatus::waiting&&accepted.pending(),
            "private dialog did not preserve the VM while awaiting a response");
        const auto ticket=accepted.pending()->ticket;
        require(!accepted.complete(ticket+1,true),"dialog accepted a stale VM response ticket");
        require(accepted.complete(ticket,true)&&accepted.status()==odh::VmStatus::returned_true,
            "OK response did not resume the blocked BHAV through its true branch");
        auto declined=waiting_vm();
        declined.start(odh::routine_key(8312),{105,105,136,0});
        require(declined.run(1)==odh::VmStatus::waiting&&declined.pending()&&
            declined.complete(declined.pending()->ticket,false)&&
            declined.status()==odh::VmStatus::returned_false,
            "false dialog response did not take the BHAV failure branch");
        std::cout<<"linux_vm_dialog ok\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"linux_vm_dialog: "<<error.what()<<'\n';
        return 1;
    }
}
