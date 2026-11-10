#include "odh/sha256.hpp"
#include "session_resource_manifest.hpp"
#include <algorithm>

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
    // Resource admission and preimage checks must hash the same bytes as the
    // portable reference, independent of EVP/fallback and update boundaries.
    for(std::size_t length=0;length<=129;++length){
        std::string bytes(length,'\0');
        for(std::size_t i=0;i<length;++i)bytes[i]=static_cast<char>((i*37u+11u)&255u);
        const auto expected=odh::sha256(bytes.data(),bytes.size());
        odh_host::ResourceSha256 resource;
        resource.update(nullptr,0);
        for(std::size_t at=0;at<length;){const auto n=std::min<std::size_t>(13,length-at);
            resource.update(bytes.data()+at,n);at+=n;}
        check(resource.finish()==expected,"resource digest preserves all SHA padding boundaries");
        rejects([&]{(void)resource.finish();},"resource digest rejects repeated finish");
        rejects([&]{resource.update(nullptr,0);},"resource digest rejects update after finish");
    }
    const std::string resource_bytes(65537,'x');
    odh_host::ResourceSha256 resource_chunks;
    resource_chunks.update(resource_bytes.data(),65536);
    resource_chunks.update(resource_bytes.data()+65536,1);
    check(resource_chunks.finish()==odh::sha256(resource_bytes.data(),resource_bytes.size()),
        "resource streaming block plus tail preserves fingerprint");
    odh_host::ResourceSha256 invalid_resource;
    rejects([&]{invalid_resource.update(nullptr,1);},"resource digest rejects null nonempty input");
    invalid_resource.update("abc",3);
    check(hex(invalid_resource.finish())=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "rejected null update does not corrupt resource digest preimage");
    std::cout << "sha256: portable known answers and shared resource backend parity passed\n";
}
