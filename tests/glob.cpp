#include "odh/formats.hpp"

#include <iostream>
#include <sstream>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const odh::FormatError&) { rejected = true; }
    require(rejected, message);
}
template<class F> void use(const std::string& bytes, F operation) {
    std::istringstream input(bytes, std::ios::binary);
    operation(odh::View(input, bytes.size()));
}
std::string pascal(const std::string& name) {
    return std::string(1, static_cast<char>(name.size())) + name;
}
} // namespace

int main() {
    try {
        const auto bytes = pascal("SyntheticGlobals");
        use(bytes, [](const odh::View& v) {
            const auto g = odh::read_glob(v);
            require(g.name == "SyntheticGlobals" && g.remaining_bytes == 0,
                    "Pascal name needs no NUL terminator");
        });
        for (std::size_t n = 0; n < bytes.size(); ++n)
            use(bytes.substr(0, n), [](const odh::View& v) {
                rejects([&] { odh::read_glob(v); }, "every Pascal truncation rejected");
            });
        use(bytes + std::string(242, static_cast<char>(0xa3)), [](const odh::View& v) {
            const auto g = odh::read_glob(v);
            require(g.name == "SyntheticGlobals" && g.remaining_bytes == 242,
                    "unknown filler extent preserved");
        });
        use(std::string(1, '\0') + "tail", [](const odh::View& v) {
            const auto g = odh::read_glob(v);
            require(g.name.empty() && g.remaining_bytes == 4, "zero Pascal count preserved");
        });
        const std::string raw{"../x\\y\0\x80\xff", 10};
        use(pascal(raw), [&](const odh::View& v) {
            require(odh::read_glob(v).name == raw, "raw path NUL and non-ASCII bytes preserved");
        });
        const std::string maximum(255, static_cast<char>(0xfe));
        use(pascal(maximum), [&](const odh::View& v) {
            const auto g = odh::read_glob(v);
            require(g.name == maximum && g.remaining_bytes == 0, "Pascal inherent 255-byte bound");
        });
        use("prefix" + bytes + "outside", [&](const odh::View& v) {
            const auto bounded = v.subview(6, bytes.size());
            require(odh::read_glob(bounded).remaining_bytes == 0, "subview excludes surrounding data");
            rejects([&] { odh::read_glob(v.subview(6, bytes.size() - 1)); },
                    "Pascal cannot consume bytes outside subview");
        });
        {
            std::istringstream input(bytes.substr(0, bytes.size() - 1), std::ios::binary);
            const odh::View declared(input, bytes.size());
            rejects([&] { odh::read_glob(declared); }, "physical Pascal stream truncation");
        }
        constexpr auto c_string = odh::GlobNameEncoding::c_string;
        use(std::string("SemiGlobals\0tail", 16), [&](const odh::View& v) {
            const auto g = odh::read_glob(v, c_string);
            require(g.name == "SemiGlobals" && g.remaining_bytes == 4, "explicit C string and tail");
            rejects([&] { odh::read_glob(v); }, "default does not guess C string");
        });
        use(std::string(1, '\0') + "tail", [&](const odh::View& v) {
            const auto g = odh::read_glob(v, c_string);
            require(g.name.empty() && g.remaining_bytes == 4, "empty terminated C string");
        });
        const std::string raw_c{"./a\\b\x80\xff", 7};
        use(raw_c + '\0', [&](const odh::View& v) {
            require(odh::read_glob(v, c_string).name == raw_c, "C string raw bytes preserved");
        });
        use(std::string(256, 'x') + '\0', [&](const odh::View& v) {
            require(odh::read_glob(v, c_string).name.size() == 256, "C string maximum name accepted");
        });
        use(std::string(257, 'x') + '\0', [&](const odh::View& v) {
            rejects([&] { odh::read_glob(v, c_string); }, "C string over allocation limit");
        });
        for (std::size_t n = 0; n <= 256; ++n)
            use(std::string(n, 'x'), [&](const odh::View& v) {
                rejects([&] { odh::read_glob(v, c_string); }, "missing C string terminator");
            });
        use(std::string("abc\0", 4), [&](const odh::View& v) {
            rejects([&] { odh::read_glob(v.subview(0, 3), c_string); },
                    "C terminator outside view is unavailable");
        });
        {
            std::istringstream input("abc", std::ios::binary);
            const odh::View declared(input, 4);
            rejects([&] { odh::read_glob(declared, c_string); }, "physical C stream truncation");
        }
        use(bytes, [](const odh::View& v) {
            rejects([&] { odh::read_glob(v, static_cast<odh::GlobNameEncoding>(99)); },
                    "unknown explicit encoding rejected");
        });
        std::cout << checks << " checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failed: " << error.what() << '\n';
        return 1;
    }
}
