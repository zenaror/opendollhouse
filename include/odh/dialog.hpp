#pragma once
#include "odh/world.hpp"
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace odh {
enum class DialogStringScope : std::uint8_t {private_resource,global_resource,semiglobal_resource};
struct DialogOperands {
    std::uint8_t cancel=0,icon=0,message=0,yes=0,no=0,type=0,title=0,flags=0;
    bool blocking() const noexcept {return (flags&1)==0;}
};
DialogOperands parse_dialog_operands(const std::array<std::uint8_t,8>&) noexcept;
struct DialogChoice {std::string label;std::int32_t value=0;};
struct DialogPrompt {
    std::string title,message,icon_name,yes,no,cancel;
    // A selector must supply genuine catalog choices; an arbitrary integer
    // typed into a neighborhood/phone/skin dialog is not a selected resource.
    std::vector<DialogChoice> choices;
};
struct DialogState {
    WorldEntityReference thread_owner;
    VmFrame source;
    Instruction instruction;
    std::uint64_t ticket=0,run_generation=0;
    DialogPrompt prompt;
    std::string input;
    std::uint8_t selected_button=0;
};
struct DialogResponse {std::uint8_t code=0;std::string text;};
struct DialogServices {
    std::function<std::optional<WorldEntityReference>(const Vm&)> thread_owner;
    std::function<std::optional<DialogPrompt>(DialogStringScope,const VmFrame&,DialogOperands,std::string&)> prepare;
    // Display/record only. No automatic answer; headless may expose a genuine
    // pending prompt for explicit user/test input or native persistence.
    std::function<bool(const DialogState&,bool blocking,std::string&)> show;
    std::function<bool(WorldEntityReference,const std::string&,std::string&)> rename;
};
using DialogSubstitution=std::function<std::optional<std::string>(std::string_view,const std::vector<std::uint16_t>&)>;
std::string expand_dialog_text(std::string_view,const DialogSubstitution&);
bool dialog_selector_type(std::uint8_t) noexcept;
bool dialog_state_valid(const DialogState&) noexcept;
bool dialog_matches_vm_snapshot(const DialogState&,const VmSnapshot&) noexcept;
std::vector<std::uint8_t> encode_dialog_state(const DialogState&);
DialogState decode_dialog_state(const std::vector<std::uint8_t>&);
// One TS1 global blocking dialog. Vm lifetime belongs to the host; cancel its
// dialog before destroying that thread. Snapshot contains no pointers.
class DialogController {
public:
    explicit DialogController(World&,DialogServices={});
    PrimitiveOutcome primitive(const VmFrame&,const Instruction&,const Ts1Temps&,const VmDispatch&);
    const std::optional<DialogState>& pending() const noexcept {return pending_;}
    Vm* pending_vm() const noexcept {return vm_;}
    bool respond(std::uint64_t ticket,const DialogResponse&,std::string& diagnostic);
    bool restore(const DialogState&,Vm&,std::string& diagnostic);
    bool update_draft(std::uint64_t ticket,const std::string&,std::uint8_t selected_button);
    void cancel_for(const Vm*) noexcept;
private:
    bool source_matches() const;
    World& world_;DialogServices services_;Vm* vm_=nullptr;std::optional<DialogState> pending_;
};
VmPrimitive dialog_primitive(DialogController&);
} // namespace odh
