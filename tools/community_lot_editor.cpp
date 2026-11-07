// SPDX-License-Identifier: MPL-2.0
#include "community_lot_editor.hpp"
#include "community_lot_edit_session.hpp"
#include "linux_vm_dialog.hpp"
#include "linux_cas_layout.hpp"
#include "original_ui_art.hpp"
#include <limits>
#include "architecture_edit_resources.hpp"
#include "architecture_edit_session_adapter.hpp"
#include "build_mode_session.hpp"
#include "linux_text_renderer.hpp"
#include "linux_ui_strings.hpp"
#include "linux_window.hpp"
#include "lot_camera.hpp"
#include "saved_lot_renderer.hpp"
#include "saved_resources.hpp"
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
#include "linux_text_codec.hpp"
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <thread>

namespace odh_host {
namespace {
std::uint8_t editor_dialog(LinuxWindow& window,odh::DialogState state,std::uint8_t language,
        const VmDialogAppearance& appearance){
    odh::RasterFrame backdrop;backdrop.width=window.width();backdrop.height=window.height();
    backdrop.pixels=window.readback();backdrop.depth.assign(backdrop.pixels.size(),std::numeric_limits<float>::infinity());
    const auto layout=vm_message_dialog_layout(backdrop.width,backdrop.height,state,language,appearance);
    if(!layout)throw odh::FormatError("editor confirmation window is too small");
    auto frame=backdrop;(void)draw_vm_dialog(frame,state,language,0,appearance);window.present(frame);
    CasPointer pointer;
    const auto hit=[&](int x,int y){return vm_dialog_button_hit(*layout,state.instruction.operands[5],x,y);};
    const auto finish=[&](std::uint8_t answer){window.present(backdrop);return answer;};
    for(;;){
        const auto input=window.poll();
        if(input.quit){
            if(input.quit_reason==WindowQuitReason::escape_key){window.clear_escape_quit_reason();return finish(2);}
            if(input.quit_reason==WindowQuitReason::window_manager_close&&window.acknowledge_window_close_request())return finish(2);
            return 2;
        }
        if(input.confirm||(input.action&&!input.primary_click))return finish(0);
        if(const auto click=pointer.click_on(input,hit))if(const auto response=hit((*click)[0],(*click)[1]))return finish(*response);
        if(input.redraw)window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
}
CommunityLotEditorResult show_community_lot_editor(LinuxWindow&window,const CommunityLotEditorOptions&o,
        const std::function<void(const std::string&)>& log){
    if(o.language<1||o.language>20)throw odh::FormatError("community editor language invalid");
    const auto identity=community_lot_identity(o);
    CommunityLotStoreOptions store=o;store.expected_identity=identity;
    auto saved=load_private_community_lot(store);
    std::vector<SavedDefinitionSource> sources;
    for(const auto&p:o.definition_resources){auto extension=p.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
        sources.push_back({p.string(),extension!=".far"});}
    auto definitions=saved_definition_index(sources);
    auto source_records=saved_world_records(o.lot_path.string(),*definitions);
    auto world=odh::make_saved_world(std::move(source_records),*definitions,nullptr,
        odh::installed_lot_limits().saved_world);
    const auto architecture=saved_architecture_resource(o.lot_path.string());
    SavedLotMaterialOptions materials;materials.catalog=odh::discover_architecture_resources(o.architecture_sources);
    materials.preparation=o.preparation;materials.restored_architecture=saved?std::optional<odh::ArchitectureSnapshot>{saved->document.architecture}:std::nullopt;
    ArchitectureEditResources resources(o.build,o.floors,o.walls,materials.catalog,o.language);
    std::vector<BuildModeMaterial> palette;
    for(const auto&d:resources.descriptors()){
        auto name=d.name;
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
        name=legacy_text_to_utf8(name,o.language,4096);
#endif
        palette.push_back({d.kind,d.id,std::move(name),d.price,d.default_pattern});
    }
    auto panel=std::make_shared<OriginalControlPanelArt>(o.game_root);
    BuildModeText text;
    if(o.language==14)text.tools={{"Parede","Remover parede","Retângulo de paredes","Revestimento de parede","Preencher paredes","Piso","Preencher piso"}};
    GameUiStrings labels(o.game_root);text.cancel=labels.get(129,4,o.language,"Cancel");
    text.failure=labels.get(137,12,o.language,"These changes could not be applied.");
    const auto failure_message=text.failure;
    BuildModeSession session(o.game_root,panel,std::move(palette),std::move(text),
        [&](auto kind,auto id){return resources.thumbnail(kind,id);});session.open();
    if(window.width()!=800||window.height()!=600)window.resize(800,600);
    SavedLotView view;view.level=1;view.terrain_theme=odh::TerrainTheme::grass;
    std::unique_ptr<SavedLotRenderer> renderer;
    const auto rebuild=[&]{renderer=std::make_unique<SavedLotRenderer>(architecture,*world,*definitions,
        o.floors.string(),o.walls.string(),view,materials);
        if(!renderer->prepared_architecture()||!renderer->runtime_room_map())throw odh::FormatError("community editor geometry/material identity unavailable");};
    rebuild();
    auto baseline=saved?std::move(saved->document):CommunityLotDocument{identity,
        odh::bind_architecture_snapshot(*renderer->prepared_architecture(),materials.catalog)};
    CommunityLotEditSession edits(std::move(baseline),saved?std::optional<CommunityLotCheckpointHead>{saved->head}:std::nullopt,
        [&](const auto& expected,const CommunityLotDocument& document){
            auto publication=publish_private_community_lot(store,expected,document);
            return CommunityLotEditPublication{publication.head,publication.checkpoint};
        });
    OriginalUiArt dialog_art(o.game_root,{"Shared/Sys/WinBtn.bmp","CPanel/Backgrounds/PopupInfoTiles.bmp"});
    VmDialogAppearance dialog_appearance;
    dialog_appearance.button_strip=dialog_art.find("Shared/Sys/WinBtn.bmp");
    dialog_appearance.panel_tiles=dialog_art.find("CPanel/Backgrounds/PopupInfoTiles.bmp");
    const auto yes=labels.get(142,2,o.language,"Yes"),no=labels.get(142,3,o.language,"No"),cancel=labels.get(142,1,o.language,"Cancel");
    dialog_appearance.yes=yes;dialog_appearance.no=no;dialog_appearance.cancel=cancel;
    odh::WorldProjection camera;camera.width=window.width();camera.height=window.height();camera.zoom=1;
    camera.center_tile={float(renderer->render_architecture().width)/2,float(renderer->render_architecture().height)/2,0};
    const auto painter=[&](odh::RasterFrame&frame,std::string_view value,const BuyUiRect&box,bool emphasis){
        TextOverlayStyle style;style.x=box.x;style.y=box.y;style.width=box.width;style.height=box.height;
        style.padding=1;style.font_size_px=emphasis?13:12;style.centered=true;style.background={0,0,0,0};
        (void)draw_text_overlay(frame,value,o.language,style);
    };
    const auto render=[&]{auto frame=renderer->render(camera,std::vector<SavedLotAvatarLayer>{},false,true).image;
        session.draw(frame,camera,painter,{true,view.walls_down,true,{}});window.present(frame);};
    BuildModeServices picks;
    picks.ground_pick=[&](int x,int y)->std::optional<BuildModePoint>{
        auto point=odh::unproject_tile(camera,float(x),float(y),float((view.level-1)*2.95));
        for(unsigned n=0;n<4;++n){
            if(point[0]<0||point[1]<0||point[0]>=renderer->render_architecture().width||point[1]>=renderer->render_architecture().height)return {};
            const odh::EntityPosition position{std::int32_t(point[0]*16),std::int32_t(point[1]*16),view.level,0};
            const auto sampled=renderer->preview_tile_position(position);
            point=odh::unproject_tile(camera,float(x),float(y),sampled[2]);
        }
        return BuildModePoint{point[0],point[1],std::int8_t(view.level)};
    };
    CommunityLotEditorResult result;
    window.set_title("OpenDollhouse | "+std::string(o.language==14?"Editar lote comunitário":"Edit community lot"));
    if(log)log("community editor open mode="+std::to_string(o.mode)+" house="+std::to_string(o.house)+" household_actors=0 scheduler_ticks=0");
    render();
    const auto execute=[&](const std::vector<odh::ArchitectureEditCommand>& commands){
        try{
            if(!same_community_lot(identity,community_lot_identity(o)))throw odh::FormatError("community source changed while editing");
            const auto snapshot=world->world->snapshot();
            ArchitectureEditValidationContext context;context.world=&snapshot;
            context.terrain=&renderer->render_architecture();context.prepared=&*renderer->prepared_architecture();
            context.rooms=&*renderer->runtime_room_map();context.definitions=definitions.get();
            context.rectangle_clip={0,0,std::int32_t(context.prepared->width),std::int32_t(context.prepared->height),2};
            context.material=[&](auto kind,auto id){return resources.material(kind,id);};
            const auto prepared=odh::prepare_architecture_edit(*context.prepared,commands,make_architecture_edit_services(context));
            if(prepared.status!=odh::ArchitectureEditStatus::ready||!prepared.candidate)throw odh::FormatError(prepared.diagnostic.empty()?"community architecture command rejected":prepared.diagnostic);
            const auto candidate=odh::bind_architecture_snapshot(*prepared.candidate,materials.catalog);
            auto candidate_materials=materials;candidate_materials.restored_architecture=candidate;
            SavedLotRenderer verified(architecture,*world,*definitions,o.floors.string(),o.walls.string(),view,candidate_materials);
            if(!verified.runtime_room_map())throw odh::FormatError("edited community geometry cannot reopen");
            edits.stage_architecture(candidate);materials.restored_architecture=edits.current().architecture;
            rebuild();result.accepted_commands+=commands.size();
            const auto tool=session.tool();session.reject("");session.select_tool(tool);
            if(log)log("community architecture staged commands="+std::to_string(commands.size())+" cost_ignored_no_family="+std::to_string(prepared.cost));
        }catch(const std::exception&e){session.reject(failure_message);if(log)log("community architecture rejected: "+std::string(e.what()));}
    };
    const auto save=[&]{
        try{
            if(const auto publication=edits.save()){
                result.checkpoint=publication->checkpoint;result.changed=true;
                if(log)log("community edit saved explicit checkpoint");
            }else if(log)log("community edit clean save no-op");
            return true;
        }catch(const std::exception& error){
            if(log)log("community edit save rejected: "+std::string(error.what()));
            odh::DialogState notice;notice.prompt.title=o.language==14?"Salvar":"Save";
            notice.prompt.message=o.language==14?"Não foi possível salvar. Suas alterações continuam abertas.":
                "Could not save. Your changes are still open.";
            (void)editor_dialog(window,notice,o.language,dialog_appearance);return false;
        }
    };
    const auto leave=[&](bool application){
        odh::DialogState question;question.instruction.operands[5]=2;
        question.prompt.title=labels.get(153,application?1:3,o.language,application?"Exit game?":"Return to neighborhood?");
        question.prompt.message=labels.get(153,application?6:4,o.language,"Would you like to save your changes?");
        question.prompt.yes=yes;question.prompt.no=no;question.prompt.cancel=cancel;
        const auto answer=editor_dialog(window,std::move(question),o.language,dialog_appearance);
        if(answer==2){if(log)log("community edit exit cancelled");return false;}
        if(answer==0&&!save())return false;
        if(answer==1){edits.discard();if(log)log("community edit draft discarded");}
        result.exit_application=application;return true;
    };
    for(;;){
        const auto input=window.poll();
        if(input.quit||input.neighborhood){
            bool application=input.quit_reason==WindowQuitReason::window_manager_close;
            if(input.quit_reason==WindowQuitReason::window_destroyed||
               (application&&!window.acknowledge_window_close_request())){result.exit_application=true;return result;}
            if(input.quit_reason==WindowQuitReason::escape_key){
                window.clear_escape_quit_reason();
                if(session.drawing()){session.abort_gesture();render();continue;}
            }
            session.abort_gesture();
            if(leave(application))return result;
            if(!session.active())session.open();render();continue;
        }
        if(input.save){(void)save();render();continue;}
        bool redraw=input.redraw;
        if(input.pointer_cancelled||input.pointer_overflow){session.abort_gesture();redraw=true;}
        if(input.rotation){camera.rotation=std::uint8_t((int(camera.rotation)+input.rotation+4)%4);session.abort_gesture();redraw=true;}
        if(input.zoom){camera.zoom=std::uint8_t(std::clamp(int(camera.zoom)+input.zoom,1,3));session.abort_gesture();redraw=true;}
        if(input.walls_down&&*input.walls_down!=view.walls_down){view.walls_down=*input.walls_down;rebuild();redraw=true;}
        if(input.camera_steps[0]||input.camera_steps[1]){scroll_lot_camera(camera,renderer->render_architecture().width,renderer->render_architecture().height,
            float(input.camera_steps[0]),float(input.camera_steps[1]));session.abort_gesture();redraw=true;}
        for(const auto&event:input.pointer_events){
            if(event.kind==WindowPointerKind::press){
                const auto action=panel->hit(original_control_panel_layout(camera.width,camera.height),event.x,event.y);
                if(action!=OriginalPanelAction::none){
                    session.abort_gesture();
                    switch(action){
                    case OriginalPanelAction::rotate_camera_left:camera.rotation=std::uint8_t((camera.rotation+3)%4);break;
                    case OriginalPanelAction::rotate_camera_right:camera.rotation=std::uint8_t((camera.rotation+1)%4);break;
                    case OriginalPanelAction::zoom_in:camera.zoom=std::uint8_t(std::min(3,int(camera.zoom)+1));break;
                    case OriginalPanelAction::zoom_out:camera.zoom=std::uint8_t(std::max(1,int(camera.zoom)-1));break;
                    case OriginalPanelAction::walls_up:view.walls_down=false;rebuild();break;
                    case OriginalPanelAction::walls_down:view.walls_down=true;rebuild();break;
                    default:break; // Actorless edit sessions remain paused.
                    }
                    redraw=true;continue;
                }
            }
            BuildModeInput command;command.press=event.kind==WindowPointerKind::press;command.release=event.kind==WindowPointerKind::release;
            command.motion=event.kind==WindowPointerKind::motion;command.x=event.x;command.y=event.y;command.shift=event.shift;command.control=event.control;
            if(const auto batch=session.handle(command,camera,picks))execute(*batch);redraw=true;
        }
        if(!session.active()){
            if(leave(false))return result;
            session.open();redraw=true;
        }
        if(redraw)render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
}
