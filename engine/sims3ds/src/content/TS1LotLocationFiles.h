/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <string>
#include <vector>

namespace FSO { namespace Content { namespace TS1 {
class TS1NeighborhoodProvider;
// Existing LotLocations.iff sources, in the provider's precedence order.
// Each STR# table belongs to the first source containing that table.
std::vector<std::string> LotLocationFiles(const TS1NeighborhoodProvider& provider);
}}}
