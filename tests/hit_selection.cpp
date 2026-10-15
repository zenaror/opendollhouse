#include "odh/hit_selection.hpp"
#include "odh/sound.hpp"
#include <iostream>
#include <limits>

namespace {
int checks = 0;
void require(bool condition, const char* reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}
struct Fixture {
    odh::HitFile hit;
    odh::Hot hot;
    std::array<std::int32_t, 29> vars{};
    Fixture() {
        hit.major_version = 1;
        hit.bytes.resize(16);
        for (auto id : {100U, 200U, 300U}) hot.patches[id] = {"synthetic", "synthetic.xa", false, false};
    }
    std::uint32_t pc() const { return static_cast<std::uint32_t>(hit.bytes.size()); }
    void emit(std::initializer_list<std::uint8_t> bytes) { hit.bytes.insert(hit.bytes.end(), bytes); }
    void word(std::uint32_t bits) {
        for (unsigned shift = 0; shift < 32; shift += 8) hit.bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
    }
    void load(std::uint8_t dest, std::uint32_t bits) { emit({5, dest}); word(bits); }
    void note(std::uint32_t patch = 100, std::uint8_t dest = 6) { load(50, patch); emit({2, dest}); }
    void branch(std::uint8_t op, std::uint32_t id) { emit({op}); word(id); }
    void target(std::uint32_t id, std::uint32_t at) { hot.track_data[id] = at; hit.entry_points[id] = at; }
    odh::HitNotePlan run(const odh::HitChooser& chooser = {}, const odh::HitSelectionLimits& limits = {},
                         const odh::HotTrack* track = nullptr, std::uint32_t start = 16,
                         odh::HitJumpOperand operand = odh::HitJumpOperand::FreeSoHeuristic,
                         const odh::HitSourceFieldReader& reader = {},
                         std::optional<odh::HitArguments> args = {},
                         std::optional<odh::HitNativeControls> controls = {}) const {
        return odh::select_hit_note(hit, hot, start, vars, chooser, limits, track, operand, reader,
                                    args, std::move(controls));
    }
};
template<class F> void rejected(F&& operation, const char* reason) {
    bool failed = false;
    try { operation(); } catch (const odh::FormatError& e) {
        failed = true;
        require(std::string(e.what()).find("HIT selection") != std::string::npos, "errors identify selection boundary");
    }
    require(failed, reason);
}
bool predicate(unsigned op, int value) {
    const bool zero = value == 0, sign = value < 0;
    return op == 62 ? zero : op == 63 ? !zero : op == 64 ? !sign && !zero :
           op == 65 ? sign : op == 66 ? !sign : sign || zero;
}
}
int main() { try {
    for(const auto a:{std::numeric_limits<std::int32_t>::min(),0,std::numeric_limits<std::int32_t>::max()})
    for(const auto b:{-123,0,123}) for(const auto c:{-1,0,1}) {
        Fixture f; f.emit({6,4,1,6,5,2,6,6,3});f.note(100,7);
        const odh::HitArguments args{{a,b,c}};
        const auto p=f.run({}, {},nullptr,16,odh::HitJumpOperand::FreeSoHeuristic,{},args);
        require(p.initial_arguments==args && p.registers[4]==a && p.registers[5]==b && p.registers[6]==c,
                "bytecode reads all three exact signed invocation arguments");
        require(p.registers[0]==0 && p.registers[7]==0 && p.registers[15]==0 && p.locals[0]==0,
                "arguments do not seed unrelated registers/locals or NoteOn destination");
    }
    {
        Fixture f;f.note();const auto p=f.run();
        require(!p.initial_arguments && p.registers[1]==12 && p.registers[2]==0 && p.registers[3]==0,
                "omitted tuple retains reference initialization");
        odh::HitArguments args{{0,0,0}};
        const auto seeded=f.run({}, {},nullptr,16,odh::HitJumpOperand::FreeSoHeuristic,{},args);
        args[0]=123;
        require(seeded.initial_arguments==odh::HitArguments{{0,0,0}} && seeded.registers[1]==0,
                "explicit zero tuple has no default fallback and plan owns its initial arguments");
    }
    {
        odh::World world;require(world.create(1,odh::EntityKind::avatar,{0,66,0,0}) &&
                                world.create(2,odh::EntityKind::object,{1,0,0,0}),"create actual World source banks");
        require(world.set_person_data(1,7,300),"set World neatness source");
        odh::VmFrame frame{odh::routine_key(4096),0,{2,2,2},{},{}};
        require(world.write(frame,0,0,-7),"set World object attribute source");
        const std::map<std::int16_t,std::uint64_t> generations{{1,world.find(1)->generation},{2,world.find(2)->generation}};
        const odh::HitSourceFieldReader reader=[&](std::int32_t source,std::int32_t field)->std::optional<std::int32_t>{
            if(source<=0 || source>32767 || field<0)return std::nullopt;
            const auto id=static_cast<std::int16_t>(source);const auto expected=generations.find(id);
            const auto entity=world.find(id);
            if(!entity || expected==generations.end() || entity->generation!=expected->second)return std::nullopt;
            const auto fields=odh::sound_source_variables(*entity);
            if(!fields || static_cast<std::uint32_t>(field)>=fields->fields.size())return std::nullopt;
            return fields->fields[static_cast<std::size_t>(field)];
        };
        Fixture f;f.vars[20]=999;f.emit({72,4,1,2});f.note();
        const auto p=f.run({}, {},nullptr,16,odh::HitJumpOperand::FreeSoHeuristic,reader,odh::HitArguments{{1,20,0}});
        require(p.registers[4]==30 && p.trace[0].source_object_value==1 && p.trace[0].field_index==20,
                "invocation args query World entity1 without aliasing reference source12");
        const auto q=f.run({}, {},nullptr,16,odh::HitJumpOperand::FreeSoHeuristic,reader,odh::HitArguments{{2,1,0}});
        require(q.registers[4]==-7 && q.trace[0].source_object_value==2 && q.trace[0].sign_flag,
                "same bytecode queries another World source's signed object attribute");
        require(world.set_person_data(1,7,660),"update live World source");
        require(f.run({}, {},nullptr,16,odh::HitJumpOperand::FreeSoHeuristic,reader,odh::HitArguments{{1,20,0}}).registers[4]==66,
                "subsequent invocation reads current bank");
        require(world.remove(1) && world.create(1,odh::EntityKind::avatar,{0,66,0,0}),"reuse World source ID");
        rejected([&]{f.run({}, {},nullptr,16,odh::HitJumpOperand::FreeSoHeuristic,reader,odh::HitArguments{{1,20,0}});},
                 "World provider can reject stale entity incarnation rather than binding reused ID");
    }
    for (unsigned op = 62; op <= 67; ++op) for (int value : {-1, 0, 1}) {
        Fixture f;
        f.emit({4, 0, static_cast<std::uint8_t>(value)});
        f.branch(static_cast<std::uint8_t>(op), 0x122);
        f.note(200, 6);
        f.target(0x122, f.pc());
        f.note(100, 7);
        const auto p = f.run();
        const bool take = predicate(op, value);
        require(p.patch_id == (take ? 100U : 200U) && p.note_destination == (take ? 7 : 6), "all six conditional branches use zero/sign flags");
        require(p.trace[1].branch_taken == take && p.trace[1].branch_id == 0x122 &&
                (take ? p.trace[1].branch_pc == f.hot.track_data[0x122] : !p.trace[1].branch_pc), "taken branch target remapping recorded");
        require(p.steps == p.trace.size() && p.trace.back().opcode == 2 && p.pc == p.trace.back().pc, "first NoteOn counted/traced without audio effect");
        require(p.registers[p.note_destination] == 0, "NoteOn destination not written");
    }
    for (unsigned op = 62; op <= 67; ++op) for (const std::int32_t pan : {-512, 0, 512}) {
        Fixture f;
        f.branch(static_cast<std::uint8_t>(op), 0x122);
        f.note(200, 6);
        f.target(0x122, f.pc());
        f.note(100, 7);
        odh::HotTrack track; track.hitlist_id = 0; track.patch_id = 100;
        track.arg_type = 2; track.volume = 1024; track.ducking_priority = 0;
        track.sound_pressure_level = 20;
        const odh::HitArguments raw{{5, 0, 0}};
        const auto controls = odh::build_native_hit_controls(track, raw, 1024, pan);
        const auto p = f.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                             {}, raw, controls);
        const bool taken = predicate(op, pan);
        require(p.initial_controls && p.initial_controls->initial_zero_flag == (pan == 0) &&
                p.initial_controls->initial_sign_flag == (pan < 0) &&
                p.trace[0].zero_flag == (pan == 0) && p.trace[0].sign_flag == (pan < 0) &&
                p.trace[0].branch_taken == taken &&
                p.patch_id == (taken ? 100U : 200U),
                "first native branch consumes final spatial pan flags");
        if (op == 62 && pan == 0) {
            const auto legacy = f.run({}, {}, &track);
            require(!legacy.initial_controls && !legacy.trace[0].zero_flag &&
                    !legacy.trace[0].sign_flag && legacy.trace[0].branch_taken == false &&
                    legacy.patch_id == 200,
                    "omitted native profile retains legacy clear initial flags");
        }
    }
    for (const std::uint8_t operand : {std::uint8_t{0}, std::uint8_t{1}}) {
        Fixture f; f.emit({69, operand}); f.note();
        odh::HotTrack track; track.hitlist_id = 0; track.patch_id = 100;
        track.arg_type = 2; track.volume = 1024; track.ducking_priority = 0;
        track.sound_pressure_level = 20;
        const odh::HitArguments raw{{1, 0, 0}};
        const auto controls = odh::build_native_hit_controls(track, raw, 1024, 512);
        bool refused = false;
        try {
            f.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic, {}, raw, controls);
        } catch (const odh::FormatError& error) {
            const std::string message = error.what();
            refused = message.find("native opcode 69 requires controller registry") != std::string::npos &&
                      message.find("cursor 18") != std::string::npos;
        }
        require(refused, "native offline opcode69 rejects after consuming operand with explicit missing-contract diagnostic");
        const auto legacy = f.run();
        require(legacy.sequence_group_kills == std::vector<std::uint8_t>({operand}) &&
                legacy.trace[0].sequence_group_kill == operand,
                "omitted native controls preserve legacy offline group request and trace");
    }
    {
        Fixture f; f.emit({69});
        odh::HotTrack track; track.hitlist_id = 0; track.patch_id = 100;
        track.arg_type = 2; track.volume = 1024; track.ducking_priority = 0;
        track.sound_pressure_level = 20;
        const odh::HitArguments raw{{1, 0, 0}};
        const auto controls = odh::build_native_hit_controls(track, raw, 1024, 512);
        bool truncated = false;
        try {
            f.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic, {}, raw, controls);
        } catch (const odh::FormatError& error) {
            const std::string message = error.what();
            truncated = message.find("address outside HIT code region") != std::string::npos &&
                        message.find("cursor 17") != std::string::npos;
        }
        require(truncated, "native opcode69 missing operand retains truncated-bytecode diagnostic");
    }
    {
        Fixture f; f.emit({4, 2, 255, 6, 3, 2, 14, 3}); f.note();
        const auto p = f.run();
        require(p.registers[1] == 12 && p.registers[2] == -1 && p.registers[3] == -1, "initial register one, sign extension and Set");
        require(p.trace[0].sign_flag && p.trace[1].sign_flag && p.trace[2].sign_flag, "LoadB/Set/Test flags");
        require(p.locals[0] == 0 && p.locals[53] == 0, "54 locals initialized to zero");
    }
    {
        Fixture f; f.load(15, 0xffffffffU); f.emit({6, 69, 15, 14, 69}); f.note(100, 69);
        const auto p = f.run();
        require(p.registers[15] == -1 && p.locals[53] == -1 && p.note_destination == 69,
                "highest supported register/local locations work including NoteOn destination validation");
        require(p.trace[2].sign_flag && p.locals[53] == -1, "Test reads high local without NoteOn overwriting it");
    }
    for (const auto& pair : {std::pair<std::uint32_t, std::uint32_t>{0x7fffffffU, 0xffffffffU},
                           {0x80000000U, 1U}, {0xffffffffU, 0xffffffffU}}) {
        Fixture f; f.load(0, pair.first); f.load(2, pair.second); f.emit({20, 0, 2}); f.note();
        const auto p = f.run();
        const auto bits = pair.first - pair.second;
        require(p.trace[2].zero_flag == (bits == 0) && p.trace[2].sign_flag == ((bits & 0x80000000U) != 0), "Cmp wraps int32 subtraction like unchecked C#");
        require(static_cast<std::uint32_t>(p.registers[0]) == pair.first, "Cmp preserves operands");
    }
    for (std::uint8_t field = 0; field < 29; ++field) {
        Fixture f; for (std::size_t n = 0; n < f.vars.size(); ++n) f.vars[n] = static_cast<std::int32_t>(n) * 17 - 200;
        f.emit({4, 2, field, 72, 3, 1, 2}); f.note();
        const auto p = f.run();
        require(p.registers[3] == f.vars[field] && p.trace[1].field_index == field && p.trace[1].field_value == f.vars[field], "GetSrcDataField uses every direct object-variable index");
        require(p.trace[1].source_object_value == 12, "GetSrcDataField source variable read and recorded");
        if (field == 20) require(p.registers[3] != f.vars[14], "field20 is objectvar20, not age14");
    }
    for (const auto source : {0U,12U,999U,0x80000000U}) {
        for (const auto field : {0U,20U,29U,0xffffffffU,0x7fffffffU}) {
            Fixture f; f.vars[20]=555; f.load(1,source); f.load(2,field); f.emit({72,3,1,2}); f.note();
            const auto expected_source=source==0x80000000U ? std::numeric_limits<std::int32_t>::min() : static_cast<std::int32_t>(source);
            const auto expected_field=field==0xffffffffU ? -1 : static_cast<std::int32_t>(field);
            int calls=0;
            const auto p=f.run({}, {}, nullptr,16,odh::HitJumpOperand::FreeSoHeuristic,
                [&](std::int32_t s,std::int32_t n)->std::optional<std::int32_t>{
                    ++calls;require(s==expected_source&&n==expected_field,"reader receives exact signed source and field register values");
                    return s==12 ? 0 : -123;
                });
            require(calls==1 && p.registers[3]==(source==12?0:-123) && p.trace[2].source_object_value==expected_source &&
                    p.trace[2].field_index==expected_field && p.trace[2].zero_flag==(source==12),"custom reader replaces array lookup and retains valid zero/sign flags");
        }
    }
    {
        Fixture f; f.vars[20]=555; f.emit({4,2,20,72,3,1,2}); f.note();
        rejected([&]{f.run({}, {}, nullptr,16,odh::HitJumpOperand::FreeSoHeuristic,
                          [](auto,auto)->std::optional<std::int32_t>{return std::nullopt;});},"missing explicit source field never falls back to default array");
        int calls=0;f.hit.bytes[20]=70; // invalid destination at GetSrcDataField
        rejected([&]{f.run({}, {}, nullptr,16,odh::HitJumpOperand::FreeSoHeuristic,
                          [&](auto,auto)->std::optional<std::int32_t>{++calls;return 0;});},"invalid destination rejects before source callback");
        require(calls==0,"invalid destination cannot query source provider");
        Fixture cyclic;cyclic.emit({4,2,20,72,3,1,2});cyclic.branch(13,0x122);cyclic.target(0x122,19);
        odh::HitSelectionLimits bound;bound.instructions=5;
        rejected([&]{cyclic.run({},bound,nullptr,16,odh::HitJumpOperand::FreeSoHeuristic,
                               [&](auto,auto)->std::optional<std::int32_t>{++calls;return 0;});},"source-callback cycles obey instruction budget");
        require(calls==2,"budget bounds source callback invocations");
    }
    for (std::uint32_t field : {0xffffffffU, 29U, 0x7fffffffU}) {
        Fixture f; f.load(2, field); f.emit({72, 3, 1, 2}); f.note();
        rejected([&] { f.run(); }, "invalid field index rejected without zero invention/overflow");
    }
    {
        Fixture f; f.hot.hitlists[77].ids = {200, 100, 300};
        f.load(0, 77); f.emit({68, 0, 4, 2, 255, 69, 0, 69, 44, 39, 50, 2, 7, 255});
        int calls = 0;
        const auto choose = [&](auto id, const auto& ids) -> std::size_t {
            ++calls; require(id == 77 && ids == std::vector<std::uint32_t>({200, 100, 300}), "chooser receives list ID and complete entries"); return 1;
        };
        const auto p = f.run(choose);
        require(calls == 1 && p.patch_id == 100 && p.active_hitlist == 77, "explicit chooser returns index, not patch ID");
        require(p.sign_flag && !p.zero_flag && p.trace[5].sign_flag, "SmartChoose preserves flags despite positive chosen ID");
        require(p.trace[5].choice->index == 1 && p.trace[5].choice->patch_id == 100 && p.trace[5].patch_replaced == true, "choice and patch write side effect recorded");
        require(p.sequence_group_kills == std::vector<std::uint8_t>({0, 44}) && p.trace[3].sequence_group_kill == 0, "sequence-group kill requests are ordered data");
        require(p.pc + 2 < f.hit.bytes.size() && p.note_destination == 7 && p.registers[7] == 0, "execution ends before NoteOn destination write and ignores following unsupported opcode");
        rejected([&] { f.run(); }, "missing chooser rejected");
        rejected([&] { f.run([](auto, const auto&) { return 3U; }); }, "out-of-range chooser index rejected");
        rejected([&] { f.run([](auto, const auto&) { return std::numeric_limits<std::size_t>::max(); }); }, "maximum chooser index rejected");
        odh::HitSelectionLimits limits; limits.group_kills = 1;
        rejected([&] { f.run(choose, limits); }, "group-kill request budget enforced");
        limits.group_kills = 64; limits.choice_ids = 3;
        require(f.run(choose, limits).patch_id == 100, "choice entry budget exact boundary accepted");
        const auto previous_calls = calls;
        limits.choice_ids = 2;
        rejected([&] { f.run(choose, limits); }, "over-limit choice snapshot rejected");
        require(calls == previous_calls, "choice entry budget rejects before callback");
        auto empty = f; empty.hot.hitlists[77].ids.clear();
        rejected([&] { empty.run(choose); }, "empty list rejected");
        auto missing = f; missing.hot.hitlists.clear();
        rejected([&] { missing.run(choose); }, "missing list rejected");
        auto unsupported = f; unsupported.hot.hitlists[77].unsupported_reason = "synthetic unsupported encoding";
        rejected([&] { unsupported.run(choose); }, "unsupported list rejected");
    }
    {
        Fixture f; f.hot.hitlists[71].ids = {200, 300}; f.hot.hitlists[72].ids = {100};
        f.load(0, 72); f.emit({68, 0, 39, 18, 2, 4});
        odh::HotTrack track; track.hitlist_id = 71; track.patch_id = 0;
        std::vector<std::uint32_t> calls;
        const auto chooser = [&](auto list, const auto&) -> std::size_t { calls.push_back(list); return list == 71 ? 1 : 0; };
        const auto p = f.run(chooser, {}, &track);
        require(calls == std::vector<std::uint32_t>({71, 72}) && p.initial_choice->patch_id == 300 && p.patch_id == 100, "SetTrack makes initial choice before bytecode switches list");
        require(p.locals[2] == 100 && p.locals[34] == 0, "0x12 patch variable side effect, independent of variable50");
        require(f.hot.tracks.empty() && track.patch_id == 0 && f.hot.hitlists[71].ids[1] == 300, "selection does not mutate HOT track/hitlist resources");
        rejected([&] { f.run({}, {}, &track); }, "initial track selection requires explicit chooser");
        track.hitlist_id.reset();
        rejected([&] { f.run(chooser, {}, &track); }, "unresolved initial track rejects");
    }
    {
        Fixture f; f.hot.hitlists[77].ids = {100};
        f.load(0, 77); f.emit({68, 0, 4, 2, 0, 39, 50, 2, 6});
        int calls = 0;
        const auto p = f.run([&](auto, const auto&) { ++calls; return 0U; });
        require(calls == 1 && p.zero_flag && !p.sign_flag, "one-element list still invokes chooser and SmartChoose preserves zero flag");
        auto invalid = f; invalid.hot.hitlists[77].ids = {999};
        rejected([&] { invalid.run([](auto, const auto&) { return 0U; }); }, "chosen nonexistent patch fails at NoteOn without prior patch");
        invalid.hit.bytes.insert(invalid.hit.bytes.begin() + 16, {5, 18, 200, 0, 0, 0});
        require(invalid.run([](auto, const auto&) { return 0U; }).patch_id == 200,
                "SmartChoose invalid patch write preserves prior patch under FreeSO side effect rule");
    }
    {
        Fixture f; f.emit({2, 5});
        odh::HotTrack track; track.hitlist_id = 0; track.patch_id = 200;
        const auto p = f.run({}, {}, &track);
        require(p.patch_id == 200 && !p.initial_choice && p.locals[2] == 0 && p.locals[34] == 0, "initial patch selected without writing locals or invoking chooser");
        track.unsupported_reason = "unresolved synthetic metadata";
        rejected([&] { f.run({}, {}, &track); }, "unsupported initial track rejects");
    }
    for (auto dest : {18, 50}) {
        Fixture f; f.load(static_cast<std::uint8_t>(dest), 200); f.load(static_cast<std::uint8_t>(dest), 999); f.emit({2, 3});
        const auto p = f.run();
        require(p.patch_id == 200 && p.locals[static_cast<std::size_t>(dest - 16)] == 999 && p.trace[1].patch_replaced == false, "failed patch lookup preserves previous valid patch while storing invalid ID");
        Fixture no_patch; no_patch.load(static_cast<std::uint8_t>(dest), 999); no_patch.emit({2, 3});
        rejected([&] { no_patch.run(); }, "NoteOn cannot use nonexistent patch");
    }
    {
        Fixture f; f.emit({14, 19, 6, 4, 18, 5, 18, 200, 0, 0, 0, 2, 5});
        odh::HotTrack track; track.hitlist_id = 0; track.patch_id = 100;
        track.arg_type = 2; track.volume = 777; track.ducking_priority = 4;
        track.sound_pressure_level = 30;
        const odh::HitArguments raw{{-7, 0, 0}};
        auto controls = odh::build_native_hit_controls(track, raw, 900, 600);
        const auto p = f.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                             {}, raw, controls);
        require(p.initial_controls && p.initial_controls->locals[18 - 16] == 777 &&
                p.initial_controls->locals[19 - 16] == 900 &&
                p.initial_controls->locals[20 - 16] == 600,
                "initial receipt snapshots native controls before first instruction");
        require(p.initial_arguments == raw && p.registers[1] == -7 && p.registers[2] == 0 &&
                p.registers[3] == 0 && p.trace[0].opcode == 14 && !p.trace[0].zero_flag,
                "raw registers remain unchanged and first script sees post-spatial extvol");
        require(p.patch_id == 100 && p.locals[18 - 16] == 200 &&
                !p.trace[2].patch_replaced && p.trace[2].write_location == 18,
                "native write18 changes volume without selecting matching patch200");
        require(controls.locals[18 - 16] == 777,
                "owned initial state and caller's controls remain independent of runtime writes");
        rejected([&] { f.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                             {}, odh::HitArguments{{-7, 1, 0}}, controls); },
                 "mismatched raw tuple rejected before execution");
        rejected([&] { f.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                             {}, {}, controls); }, "native profile requires explicit raw tuple");
        track.arg_type = 1;
        rejected([&] { f.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                             {}, raw, controls); }, "native profile rejects different track style");
        track.arg_type = 2;
        track.volume = 778;
        rejected([&] { f.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                             {}, raw, controls); }, "native profile rejects different track volume");
        track.volume = 777;
        auto inconsistent = controls;
        inconsistent.initial_zero_flag = true;
        rejected([&] { f.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                             {}, raw, inconsistent); }, "tampered initial flags reject before script");
        const auto rejects_mutation = [&](const odh::HitNativeControls& mutated) {
            rejected([&] { f.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                                 {}, raw, mutated); },
                     "mutated native startup snapshot rejects before script");
        };
        inconsistent = controls; inconsistent.locals[16 - 16] = 1; rejects_mutation(inconsistent);
        inconsistent = controls; inconsistent.locals[17 - 16].reset(); rejects_mutation(inconsistent);
        inconsistent = controls; inconsistent.locals[19 - 16] = 899; rejects_mutation(inconsistent);
        inconsistent = controls; inconsistent.locals[46 - 16] = 0; rejects_mutation(inconsistent);
        inconsistent = controls; inconsistent.locals[47 - 16] = -8; rejects_mutation(inconsistent);
        inconsistent = controls; inconsistent.locals[50 - 16] = 200; rejects_mutation(inconsistent);
        inconsistent = controls; inconsistent.locals[48 - 16] = 0; rejects_mutation(inconsistent);
        inconsistent = controls; inconsistent.locals[52 - 16] = 0; rejects_mutation(inconsistent);
        inconsistent = controls; inconsistent.locals[55 - 16] = 1; rejects_mutation(inconsistent);
        inconsistent = controls; inconsistent.locals[57 - 16] = 31; rejects_mutation(inconsistent);
        inconsistent = controls; inconsistent.locals[63 - 16] = 0; rejects_mutation(inconsistent);
        track.sound_pressure_level = 31;
        rejected([&] { f.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                             {}, raw, controls); },
                 "changed Track.Spl rejects a stale native snapshot");
        track.sound_pressure_level = 30;
        track.sound_pressure_level.reset();
        rejected([&] { f.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                             {}, raw, controls); },
                 "missing Track.Spl rejects before execution");
        track.sound_pressure_level = 30;
        odh::HitNativeControllerValues controllers{};
        controllers[18 - 17] = 56;
        controllers[25 - 17] = -40;
        const auto overridden = odh::build_native_hit_controls(track, raw, 900, 600, controllers);
        const auto with_controllers = f.run({}, {}, &track, 16,
                                            odh::HitJumpOperand::FreeSoHeuristic,
                                            {}, raw, overridden);
        require(with_controllers.initial_controls->locals[18 - 16] == 56 &&
                with_controllers.initial_controls->locals[25 - 16] == -40,
                "valid controller overrides for volume and ducking priority remain accepted");
        track.volume = 0x80000000U;
        track.ducking_priority = 0x80000000U;
        const auto defaults_after_unset = odh::build_native_hit_controls(track, raw, 900, 600);
        const auto with_unset_track = f.run({}, {}, &track, 16,
                                             odh::HitJumpOperand::FreeSoHeuristic,
                                             {}, raw, defaults_after_unset);
        require(with_unset_track.initial_controls->locals[18 - 16] == 1024 &&
                with_unset_track.initial_controls->locals[25 - 16] == 0 &&
                with_unset_track.initial_controls->raw_track_volume ==
                    std::numeric_limits<std::int32_t>::min() &&
                with_unset_track.initial_controls->raw_track_ducking_priority ==
                    std::numeric_limits<std::int32_t>::min(),
                "Selection accepts unset Track controls and sees proven bank defaults");
    }
    {
        Fixture f; f.emit({14, 48, 2, 5});
        odh::HotTrack track; track.hitlist_id = 0; track.patch_id = 100;
        track.arg_type = 2; track.volume = 1024; track.ducking_priority = 0;
        track.sound_pressure_level = 20;
        const odh::HitArguments raw{{0, 0, 0}};
        const auto controls = odh::build_native_hit_controls(track, raw, 1024, 512);
        rejected([&] { f.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                             {}, raw, controls); }, "unknown native ID48 read cannot invent zero");
        Fixture initialized; initialized.load(50, 200); initialized.emit({14, 50, 2, 5});
        const auto p = initialized.run({}, {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                                       {}, raw, controls);
        require(p.patch_id == 200 && p.trace[0].patch_replaced == true &&
                p.locals[50 - 16] == 200,
                "native script write50 retains patch side effect and makes local readable");
        Fixture chosen; chosen.hot.hitlists[77].ids = {100};
        chosen.emit({14, 50, 2, 5});
        track.hitlist_id = 77; track.patch_id = 0;
        const auto zero_patch_controls = odh::build_native_hit_controls(track, raw, 1024, 512);
        int choices = 0;
        const auto selected = chosen.run([&](auto, const auto&) { ++choices; return 0U; },
                                         {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                                         {}, raw, zero_patch_controls);
        require(choices == 1 && selected.patch_id == 100 && selected.initial_choice &&
                selected.locals[50 - 16] == 0 && selected.initial_controls->locals[50 - 16] == 0 &&
                selected.trace[0].zero_flag,
                "raw track patch50 remains zero while initial list chooser selects patch100 once");
        track.patch_id = 0x80000000U;
        const auto sentinel_patch_controls = odh::build_native_hit_controls(track, raw, 1024, 512);
        const auto sentinel_selected = chosen.run([&](auto, const auto&) { ++choices; return 0U; },
                                                  {}, &track, 16,
                                                  odh::HitJumpOperand::FreeSoHeuristic,
                                                  {}, raw, sentinel_patch_controls);
        require(choices == 2 && sentinel_selected.patch_id == 100 &&
                sentinel_selected.initial_choice && sentinel_selected.locals[50 - 16] == 0 &&
                sentinel_selected.initial_controls->raw_track_patch ==
                    std::numeric_limits<std::int32_t>::min(),
                "unset raw patch uses normalized local50 zero and one initial hitlist choice");
        track.patch_id = 0;
        track.sound_pressure_level.reset();
        rejected([&] { chosen.run([&](auto, const auto&) { ++choices; return 0U; },
                                  {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                                  {}, raw, zero_patch_controls); },
                 "missing Spl rejects before an initial chooser callback");
        require(choices == 2, "missing Spl has no chooser effect");
        track.sound_pressure_level = 20;
        rejected([&] { chosen.run([&](auto, const auto&) { ++choices; return 0U; },
                                  {}, &track, 16, odh::HitJumpOperand::FreeSoHeuristic,
                                  {}, raw, controls); }, "mismatched raw patch profile rejects before chooser");
        require(choices == 2, "invalid native profile has no chooser effect");
    }
    {
        Fixture f; f.branch(13, 0x122); f.emit({255}); f.target(0x122, f.pc()); f.note();
        const auto p = f.run();
        require(p.trace[0].branch_id == 0x122 && p.trace[0].next_pc == f.hot.track_data[0x122] && p.steps == 3, "literal Jump remaps and skips dead bytes");
    }
    for (unsigned low = 0; low < 256; ++low) {
        Fixture f; const auto id = 0x100U + low;
        f.load(3, 999); // Deliberately unrelated: Literal32 never reads register3.
        f.branch(13, id); f.emit({255}); f.target(id, f.pc()); f.note();
        const auto p = f.run({}, {}, nullptr, 16, odh::HitJumpOperand::Literal32);
        require(p.patch_id == 100 && p.steps == 4 && p.trace[1].branch_id == id &&
                p.trace[1].next_pc == f.hot.track_data[id], "literal32 accepts every low byte without register or target probe heuristics");
    }
    for (unsigned length = 0; length < 4; ++length) {
        Fixture f; f.emit({13});
        for (unsigned i = 0; i < length; ++i) f.emit({0});
        rejected([&] { f.run({}, {}, nullptr, 16, odh::HitJumpOperand::Literal32); },
                 "literal32 rejects every truncated four-byte operand");
    }
    {
        Fixture f; f.branch(13, 259); f.note();
        rejected([&] { f.run({}, {}, nullptr, 16, odh::HitJumpOperand::Literal32); }, "literal32 requires mapped branch ID");
        f.target(259, 0);
        rejected([&] { f.run({}, {}, nullptr, 16, odh::HitJumpOperand::Literal32); }, "literal32 rejects header target");
        f.target(259, 16); odh::HitSelectionLimits limit; limit.instructions = 3;
        rejected([&] { f.run({}, limit, nullptr, 16, odh::HitJumpOperand::Literal32); }, "literal32 cycles respect budget");
        rejected([&] { f.run({}, {}, nullptr, 16, static_cast<odh::HitJumpOperand>(99)); }, "unknown Jump profile rejected");
    }
    for (bool zero_probe : {false, true}) {
        Fixture f; f.load(3, 0x122); f.emit({13, 3, 255});
        const auto target = f.pc(); f.target(0x122, target);
        if (zero_probe) f.emit({0, 255, 255});
        f.note();
        const auto p = f.run();
        require(p.trace[1].next_pc == target + (zero_probe ? 3U : 0U), "register Jump probes at target and zero byte skips THREE target bytes");
        require(p.trace[1].branch_pc == p.trace[1].next_pc && p.trace[1].branch_id == 0x122, "register Jump target trace includes probe adjustment");
    }
    {
        // The low byte 3 is ambiguous as a literal. The pinned FreeSO rule
        // selects register3=0, so it must fail instead of silently using259.
        Fixture f; f.branch(13, 259); f.target(259, f.pc()); f.note();
        rejected([&] { f.run(); }, "low-byte literal collision never receives a hidden fallback");
    }
    {
        Fixture f; f.load(3, 123); f.emit({13, 3}); f.target(123, f.pc()); f.emit({0, 0, 0});
        rejected([&] { f.run(); }, "register Jump probe skip cannot land at code end");
    }
    for (unsigned invalid : {0U, 11U, 12U, 1U, 3U, 15U, 24U, 70U, 96U, 255U}) {
        Fixture f; f.emit({static_cast<std::uint8_t>(invalid)}); f.note();
        rejected([&] { f.run(); }, "unknown/NOP/WaitSamp/End opcodes never silently continue");
    }
    for (unsigned location : {70U, 99U, 100U, 135U, 136U, 255U}) {
        for (unsigned op : {4U, 5U, 6U, 14U, 20U, 39U, 68U, 72U, 2U}) {
            Fixture f;
            if (op == 4) f.emit({4, static_cast<std::uint8_t>(location), 1});
            if (op == 5) f.load(static_cast<std::uint8_t>(location), 1);
            if (op == 6) f.emit({6, 0, static_cast<std::uint8_t>(location)});
            if (op == 14 || op == 39 || op == 68 || op == 2) f.emit({static_cast<std::uint8_t>(op), static_cast<std::uint8_t>(location)});
            if (op == 20) f.emit({20, 0, static_cast<std::uint8_t>(location)});
            if (op == 72) f.emit({72, 0, static_cast<std::uint8_t>(location), 1});
            f.note();
            rejected([&] { f.run(); }, "unmapped/global variable locations rejected");
        }
    }
    {
        Fixture f; f.emit({39, 0}); f.note();
        rejected([&] { f.run([](auto, const auto&) { return 0U; }); }, "SmartChoose without list rejects");
        Fixture absent; absent.emit({2, 0});
        rejected([&] { absent.run(); }, "NoteOn without selected patch rejects");
    }
    {
        Fixture f; f.branch(13, 0x122); f.target(0x122, 16);
        odh::HitSelectionLimits limits; limits.instructions = 3;
        rejected([&] { f.run({}, limits); }, "cyclic Jump terminates at instruction budget");
        limits.instructions = 50; limits.trace_entries = 2;
        rejected([&] { f.run({}, limits); }, "trace budget bounds cycle");
        limits.instructions = 0;
        rejected([&] { f.run({}, limits); }, "zero instruction budget rejects");
        limits.instructions = 1; limits.trace_entries = 0;
        rejected([&] { f.run({}, limits); }, "zero trace budget rejects");
    }
    {
        Fixture f; f.note();
        odh::HitSelectionLimits limits; limits.instructions = 2; limits.trace_entries = 2;
        require(f.run({}, limits).steps == 2, "exact first-note instruction/trace budget accepted");
        limits.instructions = 1;
        rejected([&] { f.run({}, limits); }, "budget before first-note fails");
        limits.instructions = 2; limits.trace_entries = 1;
        rejected([&] { f.run({}, limits); }, "trace budget before first-note fails");
        for (std::size_t size = 16; size < f.hit.bytes.size(); ++size) {
            auto truncated = f; truncated.hit.bytes.resize(size);
            rejected([&] { truncated.run(); }, "every truncated first-note fixture prefix rejects");
        }
        for (unsigned start : {0U, 15U, f.pc(), std::numeric_limits<std::uint32_t>::max()})
            rejected([&] { f.run({}, {}, nullptr, start); }, "start PC outside code region rejects");
        auto table = f; table.hit.entry_table_offset = 16;
        rejected([&] { table.run(); }, "invalid empty code before table rejects");
        table.hit.entry_table_offset = f.pc() + 1;
        rejected([&] { table.run(); }, "table outside file rejects");
        auto version = f; version.hit.major_version = 2;
        rejected([&] { version.run(); }, "unsupported HIT major version rejects");
        version = f; version.hit.minor_version = 1;
        rejected([&] { version.run(); }, "unsupported HIT minor version rejects");
        version.hit.minor_version = 8;
        require(version.run().patch_id == 100, "supported minor8 accepted");
    }
    for (auto op : {13, 62, 63, 64, 65, 66, 67}) {
        Fixture f;
        const auto value = static_cast<std::uint8_t>(op == 62 || op == 67 ? 0 : op == 65 ? 255 : 1);
        f.emit({4, 0, value}); f.branch(static_cast<std::uint8_t>(op), 0x122); f.note();
        rejected([&] { f.run(); }, "missing taken branch remap rejects");
        f.target(0x122, f.pc() - 8);
        f.hit.entry_points[0x122] = 16;
        rejected([&] { f.run(); }, "TrackData/ENTP misbinding rejects");
        f.hit.entry_points.clear();
        for (unsigned target : {0U, 15U, f.pc(), std::numeric_limits<std::uint32_t>::max()}) {
            f.hot.track_data[0x122] = target;
            rejected([&] { f.run(); }, "branch outside code rejects");
        }
        f.hot.track_data[0x122] = f.pc() - 8;
        f.hit.entry_table_offset = f.pc() - 8;
        rejected([&] { f.run(); }, "branch to ENTP/header space rejects");
    }
    for (auto op : {62, 63, 64, 65, 66, 67}) {
        Fixture f;
        const int value = op == 62 || op == 65 || op == 67 ? 1 : op == 63 || op == 64 ? 0 : -1;
        f.emit({4, 0, static_cast<std::uint8_t>(value)});
        f.branch(static_cast<std::uint8_t>(op), 0xffffffffU);
        f.note();
        const auto p = f.run();
        require(p.patch_id == 100 && p.trace[1].branch_taken == false && !p.trace[1].branch_pc,
                "untaken missing branch ID remains unresolved and execution continues");
        require(p.trace[1].zero_flag == (value == 0) && p.trace[1].sign_flag == (value < 0), "untaken branch preserves flags");
        // Still consume the complete four-byte target, even when untaken.
        f.hit.bytes.resize(22);
        rejected([&] { f.run(); }, "truncated untaken conditional operand rejects");
    }
    std::cout << checks << " HIT selection checks passed (synthetic; offline first-note plan only)\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "HIT selection test failed: " << e.what() << '\n';
    return 1;
} }
