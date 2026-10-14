#include "odh/xa_audio.hpp"

#include <iostream>
#include <sstream>

namespace {
int checks = 0;
void require(bool value, const char* why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
void set(std::string& bytes, std::size_t at, std::uint32_t value, unsigned length = 4) {
    for (unsigned i = 0; i < length; ++i)
        bytes.at(at + i) = static_cast<char>((value >> (i * 8)) & 255);
}
std::string header(std::uint32_t frames, std::uint16_t channels = 1,
                   std::uint32_t rate = 11025, char kind = 'I') {
    std::string bytes(24, '\0');
    bytes[0] = 'X'; bytes[1] = 'A'; bytes[2] = kind;
    set(bytes, 4, frames * channels * 2);
    set(bytes, 8, 1, 2); set(bytes, 10, channels, 2);
    set(bytes, 12, rate); set(bytes, 16, rate * channels * 2);
    set(bytes, 20, channels * 2, 2); set(bytes, 22, 16, 2);
    return bytes;
}
using Nibbles = std::array<int, 28>;
std::string mono(std::uint8_t control, const Nibbles& nibbles) {
    std::string block(15, '\0'); block[0] = static_cast<char>(control);
    for (unsigned i = 0; i < 14; ++i)
        block[i + 1] = static_cast<char>(((nibbles[i * 2] & 15) * 16) |
                                       (nibbles[i * 2 + 1] & 15));
    return block;
}
std::string stereo(std::uint8_t left, std::uint8_t right,
                   const Nibbles& l, const Nibbles& r) {
    const auto a = mono(left, l), b = mono(right, r);
    std::string block(30, '\0'); block[0] = a[0]; block[1] = b[0];
    for (unsigned i = 1; i < 15; ++i) {
        block[i * 2] = a[i]; block[i * 2 + 1] = b[i];
    }
    return block;
}
auto decode(const std::string& bytes, odh::XaAudioLimits limits = {}) {
    std::istringstream input(bytes);
    return odh::read_xa(odh::View(input, bytes.size()), limits);
}
template<class F> void rejects(F action, const std::string& expected = {}) {
    bool failed = false;
    try { action(); } catch (const odh::FormatError& error) {
        failed = expected.empty() || std::string(error.what()).find(expected) != std::string::npos;
    }
    require(failed, "malformed, unsupported, truncated or over-budget XA rejected");
}
void truncations(const std::string& bytes) {
    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        rejects([&] { decode(bytes.substr(0, cut)); });
        rejects([&] {
            std::istringstream input(bytes.substr(0, cut));
            odh::read_xa(odh::View(input, bytes.size()));
        });
    }
}
}

int main() { try {
    const Nibbles mixed{{0,1,2,3,4,5,6,7,-8,-7,-6,-5,-4,-3,-2,-1,
                        7,-8,1,-1,2,-2,3,-3,4,-4,5,-5}};
    const std::vector<std::int16_t> expected{
        0,4096,8192,12288,16384,20480,24576,28672,-32768,-28672,-24576,-20480,
        -16384,-12288,-8192,-4096,28672,-32768,4096,-4096,8192,-8192,12288,
        -12288,16384,-16384,20480,-20480};
    const Nibbles zero{};
    const auto mono_bytes = header(28) + mono(0, mixed);
    auto audio = decode(mono_bytes);
    require(audio.sample_rate == 11025 && audio.channels == 1 && audio.remaining_bytes == 0,
            "XA source metadata retained");
    require(audio.samples == expected, "signed high then low nibbles, predictor zero");
    require(decode(header(28,1,11025,'J') + mono(0,mixed)).samples == expected,
            "XAJ signature uses the same explicit decoder profile");
    require(decode(header(28,1,11025,'\0') + mono(0,mixed)).samples == expected,
            "native XA double-NUL signature uses the same decoder profile");
    truncations(mono_bytes);

    Nibbles impulse{}; impulse[0] = 1;
    const auto history_bytes = header(56) + mono(0x10,impulse) + mono(0x10,zero);
    const std::vector<std::int16_t> history{
        4096,3840,3600,3375,3164,2966,2781,2607,2444,2291,2148,2014,1888,1770,
        1659,1555,1458,1367,1282,1202,1127,1057,991,929,871,817,766,718,
        673,631,592,555,520,488,458,429,402,377,353,331,310,291,273,256,
        240,225,211,198,186,174,163,153,143,134,126,118};
    require(decode(history_bytes).samples == history,
            "predictor one rounding and history survive block boundaries");
    Nibbles clipped{};
    for (unsigned i = 0; i < 28; ++i)
        clipped[i] = i < 10 ? 7 : i < 20 ? -8 : i % 2 == 0 ? 7 : -8;
    require(decode(header(28) + mono(0x20,clipped)).samples ==
        std::vector<std::int16_t>{28672,32767,32767,32767,32767,32767,32767,32767,
        32767,32767,-513,-32768,-32768,-32768,-32768,-32768,-32768,-32768,
        -32768,-32768,-3584,-12584,8972,-6422,9843,-9863,2952,-19450},
        "predictor two saturates both signs and stores clipped history");
    const Nibbles third{{1,-1,7,-8,2,-2,3,-3,4,-4,5,-5,6,-6,
                        1,-1,7,-8,2,-2,3,-3,4,-4,5,-5,6,-6}};
    require(decode(header(28) + mono(0x38,third)).samples ==
        std::vector<std::int16_t>{16,9,112,36,-9,-77,-62,-77,-1,1,82,45,94,9,
        -51,-102,0,-40,-29,-42,9,2,59,25,68,3,42,-34},
        "predictor three includes negative second coefficient and rounding");
    const Nibbles small{{-8,7,-1,1,-8,7,-1,1,-8,7,-1,1,-8,7,-1,1,
                        -8,7,-1,1,-8,7,-1,1,-8,7,-1,1}};
    require(decode(header(28) + mono(15,small)).samples ==
        std::vector<std::int16_t>{-1,1,0,0,-1,1,0,0,-1,1,0,0,-1,1,0,0,
        -1,1,0,0,-1,1,0,0,-1,1,0,0}, "largest shift retains signed rounding");
    for (const auto [shift,negative,positive,one] :
         std::vector<std::array<int,4>>{{13,-4,4,1},{14,-2,2,0}}) {
        const auto result = decode(header(28) + mono(static_cast<std::uint8_t>(shift),small));
        for (unsigned i = 0; i < 28; i += 4)
            require(result.samples[i] == negative && result.samples[i + 1] == positive &&
                    result.samples[i + 2] == 0 && result.samples[i + 3] == one,
                    "fractional residual shifts round signed ties toward positive infinity");
    }
    for (unsigned shift = 0; shift <= 12; ++shift) {
        const auto value = decode(header(28) + mono(static_cast<std::uint8_t>(shift),mixed));
        for (unsigned i = 0; i < 28; ++i)
            require(value.samples[i] == mixed[i] * (4096 >> shift),
                    "every exact-power residual shift produces known PCM");
    }

    Nibbles negative{}; negative[0] = -1;
    const auto stereo_bytes = header(56,2) + stereo(0x10,0x10,impulse,negative) +
        stereo(0x10,0x10,zero,zero);
    const auto both = decode(stereo_bytes);
    require(both.channels == 2 && both.samples.size() == 112, "stereo output extent");
    const std::vector<std::int16_t> negative_history{
        -4096,-3840,-3600,-3375,-3164,-2966,-2781,-2607,-2444,-2291,-2148,-2014,
        -1888,-1770,-1659,-1555,-1458,-1367,-1282,-1202,-1127,-1057,-991,-929,
        -871,-817,-766,-718,-673,-631,-592,-555,-520,-487,-457,-428,-401,-376,
        -352,-330,-309,-290,-272,-255,-239,-224,-210,-197,-185,-173,-162,-152,
        -142,-133,-125,-117};
    for (unsigned i = 0; i < 56; ++i)
        require(both.samples[i * 2] == history[i] && both.samples[i * 2 + 1] == negative_history[i],
                "independent signed stereo histories and frame interleaving");
    truncations(stereo_bytes);
    const auto different_controls = decode(header(28,2) + stereo(0,1,mixed,mixed));
    for (unsigned i = 0; i < 28; ++i)
        require(different_controls.samples[i * 2] == expected[i] &&
                different_controls.samples[i * 2 + 1] == expected[i] / 2,
                "stereo channel control bytes select separate residual shifts");

    for (const auto channels : {std::uint16_t{1},std::uint16_t{2}}) {
        for (const auto frames : {1U,27U,28U,29U,55U,56U}) {
            const auto blocks = (frames + 27) / 28;
            auto bytes = header(frames,channels);
            for (unsigned b = 0; b < blocks; ++b)
                bytes += channels == 1 ? mono(0,mixed) : stereo(0,0,mixed,mixed);
            const auto result = decode(bytes + "\xF0tail");
            require(result.samples.size() == frames * channels && result.remaining_bytes == 5,
                    "whole blocks consumed while output trims to declared whole frames");
            for (unsigned f = 0; f < frames; ++f)
                for (unsigned c = 0; c < channels; ++c)
                    require(result.samples[f * channels + c] == expected[f % 28],
                            "last block padding is excluded from output");
            require(decode(bytes,{frames * channels * 2,bytes.size()}).samples == result.samples,
                    "exact input and PCM budgets accepted");
            rejects([&] { decode(bytes,{frames * channels * 2 - 1,bytes.size()}); },"allocation budget");
            rejects([&] { decode(bytes,{frames * channels * 2,bytes.size() - 1}); },"byte budget");
        }
        const auto empty = header(0,channels);
        const auto result = decode(empty + "\xF0",{0,25});
        require(result.samples.empty() && result.channels == channels && result.remaining_bytes == 1,
                "zero declared frames consume no block and preserve unknown tail");
        truncations(empty);
    }
    { std::istringstream stream("prefix" + mono_bytes + "suffix");
      const odh::View all(stream, mono_bytes.size() + 12);
      require(odh::read_xa(all.subview(6,mono_bytes.size())).samples == expected,
              "decoder honors bounded subview with independent surrounding bytes"); }

    for (unsigned code = 4; code < 16; ++code) {
        rejects([&] { decode(header(28) + mono(static_cast<std::uint8_t>(code * 16),zero)); },"predictor");
        for (unsigned channel = 0; channel < 2; ++channel) {
            auto bytes = stereo_bytes;
            bytes[54 + channel] = static_cast<char>(code * 16);
            rejects([&] { decode(bytes); },"predictor");
        }
    }
    for (const auto [at,value,length] : std::vector<std::array<std::uint32_t,3>>{
            {0,'Y',1},{1,'B',1},{2,'K',1},{3,1,1},{8,0,2},{8,2,2},{10,0,2},
            {10,3,2},{10,65535,2},{12,0,4},{12,7999,4},{12,192001,4},
            {12,0xFFFFFFFFU,4},{16,0,4},{16,22051,4},{20,0,2},{20,4,2},
            {22,0,2},{22,8,2},{22,24,2},{4,1,4},{4,55,4}}) {
        auto bytes = mono_bytes; set(bytes,at,value,length);
        rejects([&] { decode(bytes); });
    }
    for (auto pcm : {2U,6U,111U}) {
        auto bytes = header(28,2) + stereo(0,0,zero,zero); set(bytes,4,pcm);
        rejects([&] { decode(bytes); },"whole frame");
    }
    for (auto rate : {8000U,192000U})
        require(decode(header(28,1,rate) + mono(0,mixed)).sample_rate == rate,
                "finite rate profile endpoints accepted");
    { auto bytes = mono_bytes; set(bytes,4,4 * 1024 * 1024 + 2);
      rejects([&] { decode(bytes); },"allocation budget"); }
    { std::istringstream stream(mono_bytes);
      rejects([&] { odh::read_xa(odh::View(stream,16 * 1024 * 1024 + 1)); },"byte budget"); }
    for (auto pcm : {0xFFFFFFFEU,0xFFFFFFFCU}) {
        auto bytes = header(28,pcm == 0xFFFFFFFEU ? 1 : 2); set(bytes,4,pcm);
        // A 32-bit target may reject its vector extent before checking the body.
        rejects([&] { decode(bytes,{0xFFFFFFFFULL,0xFFFFFFFFULL}); });
    }
    std::cout << checks << " XA audio checks passed (synthetic fixtures; no playback)\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "XA audio test failed: " << error.what() << '\n'; return 1;
} }
