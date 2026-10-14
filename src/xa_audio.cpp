#include "odh/xa_audio.hpp"

#include <algorithm>
#include <limits>

namespace odh {
namespace {
constexpr std::array<std::array<std::int64_t, 2>, 4> coefficients{{
    {{0, 0}}, {{240, 0}}, {{460, -208}}, {{392, -220}}
}};

// Match arithmetic right shift using defined C++17 division. Inputs here are
// bounded far away from INT64_MIN; no signed shift or wrapping cast is needed.
std::int64_t floor_divide(std::int64_t value, std::int64_t divisor) {
    return value / divisor - (value % divisor < 0 ? 1 : 0);
}

void validate_block(const std::array<std::uint8_t, 30>& block, std::uint16_t channels) {
    for (std::uint16_t channel = 0; channel < channels; ++channel)
        if (block[channel] / 16 >= coefficients.size())
            throw FormatError("unsupported XA predictor code");
}

struct History { std::int64_t current = 0, previous = 0; };

std::int16_t decode_nibble(unsigned nibble, std::uint8_t control, History& history) {
    const auto signed_nibble = static_cast<std::int64_t>(nibble) - (nibble >= 8 ? 16 : 0);
    const auto divisor = std::int64_t{1} << (8 + (control & 15));
    const auto residual = floor_divide(signed_nibble * 268435456LL, divisor);
    const auto& predictor = coefficients[control / 16];
    const auto predicted = floor_divide(residual + history.current * predictor[0] +
        history.previous * predictor[1] + 128, 256);
    const auto sample = std::clamp(predicted, std::int64_t{-32768}, std::int64_t{32767});
    history.previous = history.current;
    history.current = sample;
    return static_cast<std::int16_t>(sample);
}
} // namespace

XaAudio read_xa(const View& view, XaAudioLimits limits) {
    if (view.size() > limits.input_bytes)
        throw FormatError("XA input exceeds configured byte budget");
    std::array<std::uint8_t, 24> header{};
    view.read(0, header.data(), header.size());
    if (header[0] != 'X' || header[1] != 'A' ||
        (header[2] != 'I' && header[2] != 'J' && header[2] != 0) || header[3] != 0)
        throw FormatError("unsupported XA signature");
    const std::uint64_t pcm_bytes = view.le32(4);
    const auto channels = view.le16(10);
    const auto sample_rate = view.le32(12);
    const auto alignment = view.le16(20);
    if (view.le16(8) != 1 || (channels != 1 && channels != 2) || view.le16(22) != 16)
        throw FormatError("unsupported XA PCM tag, channels or bit depth");
    if (sample_rate < 8000 || sample_rate > 192000)
        throw FormatError("unsupported XA sample rate");
    if (alignment != channels * 2 || view.le32(16) != std::uint64_t{sample_rate} * alignment)
        throw FormatError("inconsistent XA PCM alignment or byte rate");
    if (pcm_bytes % alignment != 0)
        throw FormatError("XA declared PCM size is not a whole frame");
    XaAudio result{sample_rate, channels, {}, 0};
    const auto sample_count = pcm_bytes / 2;
    if (pcm_bytes > limits.pcm_bytes || sample_count > result.samples.max_size() ||
        sample_count > std::numeric_limits<std::size_t>::max())
        throw FormatError("XA PCM exceeds configured allocation budget");
    const auto frames = pcm_bytes / alignment;
    const auto blocks = frames / 28 + (frames % 28 != 0 ? 1 : 0);
    const std::uint64_t block_bytes = 15 * channels;
    // pcm_bytes is a uint32 header field, so these uint64 extents cannot wrap.
    const auto body_bytes = blocks * block_bytes;
    if (body_bytes > view.size() - header.size())
        throw FormatError("truncated XA compressed blocks");
    const auto consumed = header.size() + body_bytes;
    std::array<std::uint8_t, 30> block{};
    for (std::uint64_t index = 0; index < blocks; ++index) {
        view.read(header.size() + index * block_bytes, block.data(), static_cast<std::size_t>(block_bytes));
        validate_block(block, channels);
    }
    result.samples.resize(static_cast<std::size_t>(sample_count));
    std::array<History, 2> histories{};
    std::size_t output = 0;
    for (std::uint64_t index = 0; index < blocks; ++index) {
        view.read(header.size() + index * block_bytes, block.data(), static_cast<std::size_t>(block_bytes));
        validate_block(block, channels);
        for (unsigned frame = 0; frame < 28; ++frame) {
            for (std::uint16_t channel = 0; channel < channels; ++channel) {
                const auto packed = block[channels + (frame / 2) * channels + channel];
                const unsigned nibble = frame % 2 == 0 ? packed / 16 : packed & 15;
                const auto sample = decode_nibble(nibble, block[channel], histories[channel]);
                if (index * 28 + frame < frames) result.samples[output++] = sample;
            }
        }
    }
    result.remaining_bytes = view.size() - consumed;
    return result;
}

} // namespace odh
