#pragma once
#include "odh/headline.hpp"

namespace odh {
// Version1, bounded4096 states; canonical target-ID order, no pointers or UI.
// Invalid snapshots/wire throw FormatError. World identities are checked by
// BalloonHeadlines::restore_snapshot, not by this owned-value codec.
std::vector<std::uint8_t> encode_balloon_headline_snapshot(const BalloonHeadlineSnapshot&);
BalloonHeadlineSnapshot decode_balloon_headline_snapshot(const std::vector<std::uint8_t>&);
} // namespace odh
