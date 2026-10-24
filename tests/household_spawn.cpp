#include "odh/household_spawn.hpp"
#include "odh/formats.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F fn, const char* message) {
    try { fn(); } catch (const odh::FormatError&) { return; }
    throw std::runtime_error(message);
}
}

int main() {
    try {
        const auto candidates = odh::household_spawn_candidates({168, 168, 1, 0}, 24, 24);
        require(!candidates.empty(), "mailbox has in-lot arrival candidates");
        require(candidates[0] == odh::EntityPosition{168, 168, 1, 0} &&
                candidates[1] == odh::EntityPosition{168, 168, 1, 2} &&
                candidates[2] == odh::EntityPosition{168, 168, 1, 4} &&
                candidates[3] == odh::EntityPosition{168, 168, 1, 6},
                "mailbox is tried first in the four cardinal directions");
        const std::vector<odh::EntityPosition> first_ring{
            {152,152,1,0},{152,184,1,0},{152,152,1,2},{152,184,1,2},
            {152,152,1,4},{152,184,1,4},{152,152,1,6},{152,184,1,6},
            {168,152,1,0},{168,184,1,0},{168,152,1,2},{168,184,1,2},
            {168,152,1,4},{168,184,1,4},{168,152,1,6},{168,184,1,6},
            {184,152,1,0},{184,184,1,0},{184,152,1,2},{184,184,1,2},
            {184,152,1,4},{184,184,1,4},{184,152,1,6},{184,184,1,6},
            {152,168,1,0},{184,168,1,0},{152,168,1,2},{184,168,1,2},
            {152,168,1,4},{184,168,1,4},{152,168,1,6},{184,168,1,6}};
        require(candidates.size() == 1444,
                "nine full expanding rings produce the expected bounded candidate count");
        for (std::size_t i = 0; i < first_ring.size(); ++i)
            require(candidates[4+i] == first_ring[i],
                    "first square ring matches the source's exact candidate order");
        for (const auto& candidate : candidates)
            require(candidate.x >= 0 && candidate.y >= 0 && candidate.x / 16 < 24 &&
                    candidate.y / 16 < 24 && candidate.level == 1 && candidate.direction <= 6,
                    "candidate stays inside the lot and has a cardinal facing");

        const auto edge = odh::household_spawn_candidates({8, 8, 1, 0}, 2, 2);
        require(edge.size() == 1444,
                "edge mailbox preserves all source attempts, including out-of-bounds positions");
        require(edge[4] == odh::EntityPosition{-8,-8,1,0} &&
                edge[5] == odh::EntityPosition{-8,24,1,0},
                "out-of-bounds ring attempts retain source order");
        require(edge.back() == odh::EntityPosition{152,136,1,6},
                "edge mailbox retains the full ninth ring");

        std::size_t attempts = 0;
        const auto accepted_position = candidates[40];
        const auto first_accepted = odh::first_household_spawn_candidate(
            {168, 168, 1, 0}, 24, 24, [&](const auto& candidate) {
                ++attempts;
                return candidate == accepted_position;
            });
        require(first_accepted == accepted_position && attempts == 41,
                "search stops at the first position accepted by the placement validator");
        attempts = 0;
        require(!odh::first_household_spawn_candidate({168, 168, 1, 0}, 24, 24,
                    [&](const auto&) { ++attempts; return false; }) &&
                attempts == candidates.size(),
                "search reports failure after exhausting all bounded candidates");
        rejects([] { (void)odh::household_spawn_candidates({-1, 8, 1, 0}, 2, 2); },
                "negative mailbox coordinate rejected");
        rejects([] { (void)odh::household_spawn_candidates({8, 8, 3, 0}, 2, 2); },
                "unsupported mailbox floor rejected");
        rejects([] { (void)odh::household_spawn_candidates({8, 8, 1, 0}, 0, 2); },
                "empty lot dimensions rejected");
        rejects([] { (void)odh::first_household_spawn_candidate({8, 8, 1, 0}, 2, 2, {}); },
                "missing placement validator rejected");
        std::cout << "PASS TS1 mailbox arrival candidates and lot bounds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
