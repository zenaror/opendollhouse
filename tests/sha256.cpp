#include "odh/sha256.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
template<class Function>
void rejects(Function&& function, const char* message) {
    try { function(); }
    catch (const odh::FormatError&) { return; }
    check(false, message);
}
std::string hex(const std::array<std::uint8_t,32>& digest) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : digest) output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}
}

int main() {
    check(hex(odh::sha256("", 0)) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "empty input matches SHA-256 known answer");
    check(hex(odh::sha256("abc", 3)) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "abc matches SHA-256 known answer");

    odh::Sha256 incremental;
    incremental.update("a", 1);
    incremental.update("b", 1);
    incremental.update("c", 1);
    check(hex(incremental.finish()) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "incremental updates match one-shot digest");
    rejects([&] { (void)incremental.finish(); }, "second finalization rejects");
    rejects([&] { incremental.update("x", 1); }, "update after finalization rejects");

    odh::Sha256 million_a;
    const std::string block(1000, 'a');
    for (unsigned i = 0; i < 1000; ++i) million_a.update(block.data(), block.size());
    check(hex(million_a.finish()) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "million repeated bytes match SHA-256 known answer");
    std::cout << "sha256: 6 checks passed\n";
}
