#pragma once

#include "odh/formats.hpp"

namespace odh {

struct XaAudioLimits {
    std::uint64_t pcm_bytes = 4 * 1024 * 1024;
    std::uint64_t input_bytes = 16 * 1024 * 1024;
};

struct XaAudio {
    std::uint32_t sample_rate;
    std::uint16_t channels;
    std::vector<std::int16_t> samples; // Signed PCM16, interleaved L/R for stereo.
    std::uint64_t remaining_bytes;
};

// Maxis XAI\0/XAJ\0/XA\0\0, PCM tag 1, mono/stereo, 16 bits, 8000..192000 Hz.
// Predictor codes 0..3 only: (0,0), (240,0), (460,-208), (392,-220).
// Complete 28-frame blocks (15 bytes/channel) must back the declared PCM size.
// The last block is consumed completely but output is trimmed to whole declared
// frames. Bytes beyond those blocks are counted, never decoded or validated.
// Budgets include the entire input view and declared PCM bytes, respectively.
// Headers and required blocks are validated before PCM allocation. Malformed,
// truncated, unsupported or over-budget inputs throw FormatError; allocation
// exceptions propagate. No playback, resampling or event resolution is implied.
XaAudio read_xa(const View&, XaAudioLimits = {});

} // namespace odh
