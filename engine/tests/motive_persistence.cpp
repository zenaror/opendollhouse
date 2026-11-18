// SPDX-License-Identifier: MPL-2.0
// Synthetic persistence boundaries only; no installation or saved game required.
#include "simantics/utils/VMTS1MotivePersistence.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace FSO::SimAntics::Engine::Utils::MotivePersistence;
using FSO::SimAntics::Model::VMMotive;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class F> static void Rejects(F action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected, message);
}

static void LegacyMigration() {
    // Frozen ODHI v3-8 fields, not a claim to exercise complete saved graphs
    // for those versions. Native graph continuation needs separate fixtures.
    std::array<int16_t,16> motives{};
    std::array<LegacyChange,16> changes{};
    std::array<int16_t,7> decay{};
    for (size_t i = 0; i < changes.size(); ++i) changes[i].motive = uint32_t(i);
    motives[7] = 20;
    changes[7] = {180,90,7,true,.75};
    decay[0] = 250;
    auto state = Migrate(motives,changes,decay,900);
    Require(state.current[7] == 20.5f, "pending change and decay must each be consumed once");
    Require(state.old == state.current && state.lastTick == 900,
            "legacy migration must initialize a conservative snapshot and phase");
    Require(state.changes.size() == 1 && state.changes[0].TickDelta == .1f,
            "installed legacy rate must survive migration");

    changes[7].residual = 500.75;
    decay.fill(0);
    Require(Migrate(motives,changes,decay,900).current[7] == 20,
            "whole-unit accumulated debt must be discarded");
    changes[7].residual = .75;
    motives[7] = 91;
    Require(Migrate(motives,changes,decay,900).current[7] == 91,
            "above-stop residual must not become saved credit");
    motives[7] = 90;
    Require(Migrate(motives,changes,decay,900).current[7] == 90,
            "at-stop residual must not become saved credit");
    motives[7] = 20;
    changes[7].rate = 0;
    Require(Migrate(motives,changes,decay,900).current[7] == 20,
            "cleared change must discard its retained residual");
    changes[7] = {-180,-90,7,true,-.75};
    Require(Migrate(motives,changes,decay,900).current[7] == 19.25f,
            "active negative sub-unit progress must survive");
    changes[7].ticked = false;
    auto reinstalled = Migrate(motives,changes,decay,900);
    Require(reinstalled.current[7] == 20 && reinstalled.changes.size() == 1 &&
            reinstalled.changes[0].TickDelta == -.1f,
            "unticked reinstall must keep its rate but discard ambiguous carry");
    changes[7].ticked = true;
    changes[7].residual = .75;
    Require(Migrate(motives,changes,decay,900).current[7] == 20,
            "residual with the wrong sign must be discarded");
    changes[5] = {2200,90,5,false,0};
    Require(Migrate(motives,changes,decay,900).changes[0].TickDelta == 2200.f / 1800.f,
            "persisted Energy rate must not be arbitrarily unscaled");
    decay[0] = 1200;
    Require(Migrate(motives,changes,decay,900).current[7] == 20,
            "whole-unit decay debt must not be applied");
    changes[7].residual = std::numeric_limits<double>::infinity();
    Rejects([&] { Migrate(motives,changes,decay,900); }, "non-finite legacy residual accepted");
}

static void PrecisionMarker() {
    // Independent literal schema1 fixture: hashes 0x44332211/0x88776655,
    // native64B, actor2, GUID0x12345678, neighbour7, phase59. No test encoder.
    const std::vector<uint8_t> bytes = {
        1,0,0,0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,64,0,0,0,
        1,0,0,0,2,0,0x78,0x56,0x34,0x12,7,0,59,0,0,0,0,0,0,0
    };
    auto read = [&](const std::vector<uint8_t>& data) {
        return ReadMarker(data,0x44332211,0x88776655,64,60);
    };
    auto marker = read(bytes);
    Require(marker.matching && marker.schema == 1 && marker.actors.size() == 1 && marker.actors.at(2).lastTick == 59 &&
            marker.actors.at(2).guid == 0x12345678 && marker.actors.at(2).neighbour == 7,
            "literal precision marker did not retain identity and phase");
    Require(!ReadMarker(bytes,1,0x88776655,64,60).matching, "changed ObjM binding was not stale");
    Require(!ReadMarker(bytes,0x44332211,2,64,60).matching, "changed ODHI binding was not stale");
    Require(!ReadMarker(bytes,0x44332211,0x88776655,65,60).matching,
            "changed ODHI extent was not stale");
    Rejects([&] { ReadMarker(bytes,0x44332211,0x88776655,64,58); }, "future phase accepted");
    for (size_t length = 0; length < bytes.size(); ++length) {
        auto truncated = bytes;
        truncated.resize(length);
        Rejects([&] { read(truncated); }, "truncated matching marker accepted");
    }
    auto extra = bytes;
    extra.push_back(0);
    Rejects([&] { read(extra); }, "trailing marker byte accepted");
    auto schema = bytes;
    schema[0] = 2;
    Require(read(schema).matching && read(schema).schema == 2,
            "DVD-order precision marker was not recognized");
    schema[0] = 3;
    Rejects([&] { read(schema); }, "unsupported matching schema accepted");
    Require(!ReadMarker(schema,1,0x88776655,64,60).matching,
            "stale marker incorrectly selected its schema");
    auto duplicate = bytes;
    duplicate[16] = 2;
    duplicate.insert(duplicate.end(),bytes.begin() + 20,bytes.end());
    Rejects([&] { read(duplicate); }, "duplicate actor identity accepted");
    auto overflow = bytes;
    overflow[35] = 0x80;
    Rejects([&] { read(overflow); }, "overflowed phase accepted");
    auto count = bytes;
    count[16] = count[17] = count[18] = count[19] = 0xff;
    Rejects([&] { read(count); }, "unbounded participant count accepted");
    auto identity = bytes;
    identity[20] = identity[21] = 0;
    Rejects([&] { read(identity); }, "zero actor identity accepted");
    identity = bytes;
    identity[22] = identity[23] = identity[24] = identity[25] = 0;
    Rejects([&] { read(identity); }, "zero actor GUID accepted");
}

static void ClassicInputEnvelope() {
    Rejects([&] { ValidateChange({VMMotive(16),0,0}); }, "invalid motive ID accepted");
    Rejects([&] { ValidateChange({VMMotive::Energy,std::numeric_limits<float>::quiet_NaN(),0}); },
            "non-finite rate accepted");
    Rejects([&] { ValidateChange({VMMotive::Energy,0,std::numeric_limits<float>::infinity()}); },
            "non-finite stop accepted");
    Rejects([&] { ValidateChange({VMMotive::Energy,100.f,90.f}); }, "oversized finite rate accepted");
    Rejects([&] { ValidateChange({VMMotive::Energy,.1f,40000.f}); }, "oversized finite stop accepted");
    ValidateChange({VMMotive::Energy,32767.f / 1800.f,90.25f});
    ValidateChange({VMMotive::Energy,-32768.f / 1800.f,-80.5f});
}

int main() {
    try {
        LegacyMigration();
        PrecisionMarker();
        ClassicInputEnvelope();
        std::cout << "motive persistence boundaries passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
