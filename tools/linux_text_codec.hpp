#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

namespace odh_host {

// Convert one legacy STR# value to UTF-8 at the Linux presentation boundary.
// The game data and the portable ODH core continue to keep their original bytes.
std::string legacy_text_to_utf8(std::string_view bytes,std::uint8_t language,
                                std::size_t output_limit=1024*1024);

} // namespace odh_host
