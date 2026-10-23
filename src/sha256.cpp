#include "odh/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace odh {
namespace {
constexpr std::array<std::uint32_t,64> round_constants{{
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
}};
constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
}
std::uint32_t read_be32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
        (static_cast<std::uint32_t>(bytes[1]) << 16) |
        (static_cast<std::uint32_t>(bytes[2]) << 8) |
        static_cast<std::uint32_t>(bytes[3]);
}
}

Sha256::Sha256() : state_{{0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                          0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u}} {}

void Sha256::transform(const std::uint8_t* block) {
    std::array<std::uint32_t,64> words{};
    for (std::size_t i = 0; i < 16; ++i) words[i] = read_be32(block + i * 4);
    for (std::size_t i = 16; i < words.size(); ++i) {
        const auto s0 = rotate_right(words[i - 15],7) ^ rotate_right(words[i - 15],18) ^ (words[i - 15] >> 3);
        const auto s1 = rotate_right(words[i - 2],17) ^ rotate_right(words[i - 2],19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    auto a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    auto e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto sum1 = rotate_right(e,6) ^ rotate_right(e,11) ^ rotate_right(e,25);
        const auto choice = (e & f) ^ (~e & g);
        const auto temp1 = h + sum1 + choice + round_constants[i] + words[i];
        const auto sum0 = rotate_right(a,2) ^ rotate_right(a,13) ^ rotate_right(a,22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temp2 = sum0 + majority;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(const void* input, std::size_t size) {
    if (finished_) throw FormatError("SHA-256 state is already finalized");
    if (size != 0 && input == nullptr) throw FormatError("SHA-256 input is null");
    if (size > std::numeric_limits<std::uint64_t>::max() - total_bytes_)
        throw FormatError("SHA-256 input length exceeds limit");
    const auto* data = static_cast<const std::uint8_t*>(input);
    total_bytes_ += static_cast<std::uint64_t>(size);
    while (size != 0) {
        const auto count = std::min(size, buffer_.size() - buffered_bytes_);
        std::memcpy(buffer_.data() + buffered_bytes_, data, count);
        buffered_bytes_ += count;
        data += count;
        size -= count;
        if (buffered_bytes_ == buffer_.size()) {
            transform(buffer_.data());
            buffered_bytes_ = 0;
        }
    }
}

std::array<std::uint8_t,32> Sha256::finish() {
    if (finished_) throw FormatError("SHA-256 state is already finalized");
    if (total_bytes_ > std::numeric_limits<std::uint64_t>::max() / 8u)
        throw FormatError("SHA-256 bit length exceeds limit");
    const auto bit_length = total_bytes_ * 8u;
    buffer_[buffered_bytes_++] = 0x80;
    if (buffered_bytes_ > 56) {
        std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_bytes_), buffer_.end(), 0);
        transform(buffer_.data());
        buffered_bytes_ = 0;
    }
    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_bytes_), buffer_.begin() + 56, 0);
    for (unsigned i = 0; i < 8; ++i)
        buffer_[63 - i] = static_cast<std::uint8_t>(bit_length >> (i * 8));
    transform(buffer_.data());
    finished_ = true;
    std::array<std::uint8_t,32> result{};
    for (std::size_t i = 0; i < state_.size(); ++i) {
        result[i * 4] = static_cast<std::uint8_t>(state_[i] >> 24);
        result[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
        result[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
        result[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
    }
    return result;
}

std::array<std::uint8_t,32> sha256(const void* data, std::size_t size) {
    Sha256 digest;
    digest.update(data, size);
    return digest.finish();
}

} // namespace odh
