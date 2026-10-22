#include "odh/lot_locations.hpp"
#include <charconv>
#include <unordered_set>
namespace odh {
namespace {
std::int32_t number(const std::string& line, std::size_t begin, std::size_t end) {
    while (begin < end && (line[begin] == ' ' || line[begin] == '\t')) ++begin;
    while (end > begin && (line[end-1] == ' ' || line[end-1] == '\t')) --end;
    std::int32_t result = 0;
    const auto parsed = std::from_chars(line.data()+begin,line.data()+end,result);
    if (begin == end || parsed.ec != std::errc{} || parsed.ptr != line.data()+end)
        throw FormatError("malformed LotLocations integer");
    return result;
}
}
bool neighborhood_mode_supported(unsigned mode) {
    if (mode < 1 || mode > 7) throw FormatError("neighborhood mode outside 1..7");
    return mode != 6;
}
LotLocations read_lot_locations(const View& view, std::uint8_t language,
                                         const LotLocationLimits& limits) {
    if (limits.coordinate_magnitude < 0) throw FormatError("invalid LotLocations coordinate limit");
    if (view.size() > limits.payload_bytes) throw FormatError("LotLocations payload exceeds limit");
    AnimationLimits strings;
    strings.strings = limits.rows;
    strings.text_bytes = 1024 * 1024;
    const auto table = read_strings(view,strings);
    if (table.state != StringTableState::decoded || table.format != -3)
        throw FormatError("LotLocations requires decoded STR# format -3");
    std::vector<LotLocation> result;
    std::unordered_set<std::uint16_t> houses;
    bool found = false;
    for (const auto& row : table.entries) {
        if (row.language != language) continue;
        found = true;
        if (row.value.size() > limits.row_bytes) throw FormatError("LotLocations row exceeds limit");
        const auto a = row.value.find(',');
        const auto b = a == std::string::npos ? a : row.value.find(',',a+1);
        if (a == std::string::npos || b == std::string::npos || row.value.find(',',b+1) != std::string::npos)
            throw FormatError("malformed LotLocations CSV row");
        const auto house = number(row.value,0,a);
        const auto x = number(row.value,a+1,b), y = number(row.value,b+1,row.value.size());
        if (house < 1 || house > limits.max_house || x < -limits.coordinate_magnitude ||
            x > limits.coordinate_magnitude || y < -limits.coordinate_magnitude || y > limits.coordinate_magnitude)
            throw FormatError("LotLocations field outside configured range");
        const auto id = static_cast<std::uint16_t>(house);
        if (!houses.insert(id).second) throw FormatError("duplicate LotLocations house");
        if (id != 99) result.push_back({id,x,y});
    }
    if (!found) throw FormatError("LotLocations language group absent");
    return {std::move(result),table.remaining_bytes};
}
}
