#include "odh/household_arrival.hpp"
#include "odh/formats.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
unsigned checks = 0;
void require(bool value, const char* diagnostic) {
    ++checks;
    if (!value) throw std::runtime_error(diagnostic);
}
using Gate = odh::CandidateCellGateStatus;
using Status = odh::AvatarCandidatePreflightStatus;
odh::HouseholdArrivalAssessment clear() {
    return {{Gate::allowed, Gate::allowed, Gate::allowed, Gate::allowed}, Gate::allowed};
}
void make_mailbox(odh::World& world, std::int16_t id, std::uint32_t guid) {
    require(world.create(id, odh::EntityKind::object) && world.define_type(id, guid) &&
            world.set_position(id, {168, 168, 1, 0}), "mailbox fixture construction");
}
}
int main() {
    try {
        odh::World source;
        make_mailbox(source, 1, 0xef121974U);
        require(source.create(5, odh::EntityKind::avatar, {0, 101, 3, 16}) &&
                source.define_type(5, 0xaaaa) && source.set_position(5, {32, 48, 1, 2}),
                "saved primary fixture construction");
        const auto before = source.snapshot();
        odh::NeighborRecord neighbor{};
        neighbor.version = 4; neighbor.person_mode = 1;
        neighbor.neighbor_id = 42; neighbor.guid = 0xbbbb;
        neighbor.person_data = std::vector<std::int16_t>(80, 17);
        auto staged = odh::stage_household_members(before,
            {{0xaaaa, nullptr}, {0xbbbb, &neighbor}}, 123,
            [](odh::World&, std::int16_t, std::uint32_t) { return true; });
        const auto id = staged.runtime_ids.at(0xbbbb);
        require(id == 2 && staged.runtime_ids.at(0xaaaa) == 5,
                "new identity avoids mailbox and preserves primary identity");
        odh::inherit_staged_household_member(*staged.candidate, id, neighbor, 123);
        require(staged.candidate->find(id)->person_data[31] == 42 &&
                staged.candidate->find(id)->person_data[61] == 123,
                "arrival receives inherited NBRS and FAMI identities");
        const auto candidates = odh::household_spawn_candidates({168,168,1,0}, 24, 24);
        const std::array<Gate,3> gates{Gate::allowed, Gate::blocked, Gate::unsupported};
        for (unsigned combination = 0; combination < 243; ++combination) {
            auto value = combination;
            std::array<Gate,5> input{};
            bool blocked = false, unknown = false;
            for (auto& gate : input) {
                gate = gates[value % 3]; value /= 3;
                blocked = blocked || gate == Gate::blocked;
                unknown = unknown || gate == Gate::unsupported;
            }
            const odh::HouseholdArrivalAssessment assessment{
                {input[0],input[1],input[2],input[3]}, input[4]};
            const auto result = odh::preflight_staged_household_arrival(*staged.candidate, id, 24, 24,
                [&](const odh::World& world, std::int16_t actor, const odh::EntityPosition&) {
                    require(&world == staged.candidate.get() && actor == id && !world.find(id)->position,
                            "assessor sees the unpositioned private candidate");
                    return assessment;
                });
            const auto expected = blocked ? Status::blocked : unknown ? Status::unsupported : Status::clear;
            require(result.status == expected, "five-gate arrival truth table");
            require(result.attempts == (blocked ? candidates.size() : 1),
                    "known blocker continues, unknown stops, clear accepts");
            require(result.position.has_value() == (expected == Status::clear),
                    "unsupported or blocked arrival never publishes a position");
            require(result.mailbox_id == std::optional<std::int16_t>{1}, "exact mailbox identity retained");
            if (result.position) require(*result.position == candidates.front(), "first known clear candidate selected");
        }
        unsigned attempts = 0;
        auto result = odh::preflight_staged_household_arrival(*staged.candidate, id, 24, 24,
            [&](const auto&, auto, const auto&) {
                auto assessment = clear();
                if (++attempts <= 4) assessment.cell.floor = Gate::blocked;
                return assessment;
            });
        require(result.status == Status::clear && result.position == std::optional<odh::EntityPosition>{candidates[4]} &&
                attempts == 5, "blocked mailbox facings advance in deterministic ring order");
        attempts = 0;
        result = odh::preflight_staged_household_arrival(*staged.candidate, id, 24, 24,
            [&](const auto&, auto, const auto&) {
                auto assessment = clear();
                if (++attempts == 1) assessment.position_change = Gate::unsupported;
                return assessment;
            });
        require(result.status == Status::unsupported && attempts == 1 && !result.position,
                "unknown PositionChange cannot be skipped to claim a later arrival");
        result = odh::preflight_staged_household_arrival(*staged.candidate, id, 24, 24, {});
        require(result.status == Status::unsupported && result.attempts == 0, "missing assessor fails closed");
        result = odh::preflight_staged_household_arrival(*staged.candidate, 5, 24, 24,
            [](const auto&, auto, const auto&) { return clear(); });
        require(result.status == Status::unsupported && result.attempts == 0, "positioned saved primary is not an arrival target");
        try {
            (void)odh::preflight_staged_household_arrival(*staged.candidate, id, 24, 24,
                [](const auto&, auto, const auto&) -> odh::HouseholdArrivalAssessment {
                    throw std::runtime_error("assessment failed");
                });
            require(false, "assessment exception must propagate");
        } catch (const std::runtime_error& error) {
            require(std::string(error.what()) == "assessment failed", "original assessment error retained");
        }
        require(!staged.candidate->find(id)->position &&
                staged.candidate->find(5)->position == source.find(5)->position &&
                source.snapshot().entities.size() == before.entities.size() && !source.find(id),
                "planning and assessment failure preserve staged position, saved primary and source");
        make_mailbox(*staged.candidate, 77, 0x1d95c9b0U);
        result = odh::preflight_staged_household_arrival(*staged.candidate, id, 24, 24,
            [](const auto&, auto, const auto&) { return clear(); });
        require(result.status == Status::unsupported && !result.position && result.attempts == 0,
                "two supported mailbox identities are conservatively ambiguous");
        require(staged.candidate->remove(1), "remove first mailbox in private fixture");
        result = odh::preflight_staged_household_arrival(*staged.candidate, id, 24, 24,
            [](const auto&, auto, const auto&) { return clear(); });
        require(result.status == Status::clear && result.mailbox_id == std::optional<std::int16_t>{77},
                "alternate TS1 mailbox GUID supported");
        require(staged.candidate->remove(77), "remove second mailbox in private fixture");
        result = odh::preflight_staged_household_arrival(*staged.candidate, id, 24, 24,
            [](const auto&, auto, const auto&) { return clear(); });
        require(result.status == Status::unsupported && !result.position && result.attempts == 0,
                "missing mailbox fails closed");
        make_mailbox(*staged.candidate, 1, 0xef121974U);
        require(staged.candidate->set_position(1, {8,8,1,0}), "move mailbox to lot edge");
        const auto edge_candidates = odh::household_spawn_candidates({8,8,1,0}, 24, 24);
        const auto first_in_bounds = std::find_if(edge_candidates.begin() + 4, edge_candidates.end(),
            [](const auto& candidate) {
                return candidate.x >= 0 && candidate.y >= 0 &&
                    candidate.x / 16 < 24 && candidate.y / 16 < 24;
            });
        require(first_in_bounds != edge_candidates.end(), "edge mailbox has later in-bounds arrival");
        unsigned bounded_assessments = 0;
        result = odh::preflight_staged_household_arrival(*staged.candidate, id, 24, 24,
            [&](const auto&, auto, const auto&) {
                ++bounded_assessments;
                auto assessment = clear();
                if (bounded_assessments <= 4) assessment.cell.floor = Gate::blocked;
                return assessment;
            });
        const auto attempts_to_first_in_bounds = static_cast<std::size_t>(
            std::distance(edge_candidates.begin(), first_in_bounds)) + 1;
        require(result.status == Status::clear && result.position == *first_in_bounds &&
                result.attempts == attempts_to_first_in_bounds && bounded_assessments == 5,
                "out-of-bounds candidates count in order but bypass the placement assessor");
        require(staged.candidate->remove(1), "remove positioned edge mailbox");
        require(staged.candidate->create(1, odh::EntityKind::object) &&
                staged.candidate->define_type(1, 0xef121974U),
                "create unpositioned mailbox fixture");
        bounded_assessments = 0;
        result = odh::preflight_staged_household_arrival(*staged.candidate, id, 24, 24,
            [&](const auto&, auto, const auto&) {
                ++bounded_assessments;
                return clear();
            });
        require(result.status == Status::unsupported && result.attempts == 0 &&
                bounded_assessments == 0,
                "recognized mailbox without a position fails closed");
        require(staged.candidate->remove(1), "remove unpositioned mailbox fixture");
        make_mailbox(*staged.candidate, 1, 0xef121974U);
        odh::EntityLayout container_layout;
        container_layout.slots = 1;
        require(staged.candidate->create(77, odh::EntityKind::object, container_layout) &&
                staged.candidate->define_type(77, 0xcccc) &&
                staged.candidate->set_position(77, {184,184,1,0}) &&
                staged.candidate->attach(77, 0, 1),
                "attach mailbox fixture");
        bounded_assessments = 0;
        result = odh::preflight_staged_household_arrival(*staged.candidate, id, 24, 24,
            [&](const auto&, auto, const auto&) {
                ++bounded_assessments;
                return clear();
            });
        require(result.status == Status::unsupported && result.attempts == 0 &&
                bounded_assessments == 0,
                "recognized attached mailbox fails closed");
        std::cout << "PASS household arrival infrastructure: 243 gate combinations; " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
