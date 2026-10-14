#include "odh/floor.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
int checks = 0;
using Pixel = std::array<std::uint8_t, 4>;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void close(float value, double expected, const char* message) {
    require(std::abs(double(value) - expected) < 0.0001, message);
}
template<class F> void rejects(F operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const odh::FormatError&) { rejected = true; }
    require(rejected, message);
}
odh::Spr2Frame frame(unsigned width, unsigned height, std::vector<Pixel> pixels) {
    odh::Spr2Frame result{};
    result.version = 1000;
    result.flags = 1;
    result.width = static_cast<std::uint16_t>(width);
    result.height = static_cast<std::uint16_t>(height);
    result.pixels = std::move(pixels);
    return result;
}
double area(const odh::RasterTriangle& triangle) {
    const auto& a = triangle.vertices[0].position;
    const auto& b = triangle.vertices[1].position;
    const auto& c = triangle.vertices[2].position;
    return std::abs((double(b[0])-a[0])*(double(c[1])-a[1])-
                    (double(b[1])-a[1])*(double(c[0])-a[0])) / 2;
}
bool same(const odh::RasterVertex& a, const odh::RasterVertex& b) {
    return a.position == b.position && a.texcoord == b.texcoord && a.normal == b.normal;
}
} // namespace

int main() {
    try {
        const Pixel red{255, 0, 0, 255}, blue{0, 0, 255, 255};
        const Pixel clear{81, 92, 103, 0};
        auto odd = frame(3, 2, {clear, red, clear, blue, red, blue});
        odd.indices = {1, 2, 3, 4, 5, 6};
        odd.z = {6, 5, 4, 3, 2, 1};
        odd.x = -17; odd.y = 28; odd.remaining_bytes = 5;
        const auto before = odd;
        const auto padded = odh::floor_texture(odd);
        require(padded.width == 4 && padded.height == 2 && padded.bit_count == 32 &&
                padded.top_down && padded.remaining_bytes == 0, "FloorCopy owns normalized bitmap metadata");
        require(padded.pixels == std::vector<Pixel>{blue, red, blue, red, blue, red, blue, red},
                "odd FloorCopy appends transparent black then wraps both axes");
        require(odd.pixels == before.pixels && odd.indices == before.indices && odd.z == before.z &&
                odd.width == before.width && odd.x == before.x && odd.y == before.y &&
                odd.remaining_bytes == before.remaining_bytes, "FloorCopy leaves source metadata/channels unchanged");
        auto even = frame(2, 2, {red, blue, clear, clear});
        const auto copied = odh::floor_texture(even);
        require(copied.width == 2 && copied.pixels == std::vector<Pixel>{red, blue, blue, red},
                "even FloorCopy needs no width padding");
        const auto fully_opaque = odh::floor_texture(frame(2, 2, {red, blue, red, blue}));
        require(fully_opaque.pixels == std::vector<Pixel>{blue, red, blue, red},
                "opaque peer replaces even an opaque source");
        auto threshold = frame(2, 2, {red, red, Pixel{1, 2, 3, 253}, Pixel{4, 5, 6, 253}});
        require(odh::floor_texture(threshold).pixels == std::vector<Pixel>(4, red),
                "alpha253 peer ignored and covered by opposite opaque peer");
        threshold.pixels[3][3] = 254;
        rejects([&] { odh::floor_texture(threshold); }, "alpha254 peer replaces source and then fails opaque profile");
        rejects([&] { odh::floor_texture(frame(2, 2, std::vector<Pixel>(4, clear))); },
                "residual transparency rejected");
        rejects([&] { odh::floor_texture(frame(0, 0, {})); }, "empty floor rejected");
        rejects([&] { odh::floor_texture(frame(2, 2, {red})); }, "source extent mismatch rejected");
        auto bad = even; bad.flags = 2;
        rejects([&] { odh::floor_texture(bad); }, "missing pixel flag rejected");
        bad = even; bad.flags = 9;
        rejects([&] { odh::floor_texture(bad); }, "unknown flags rejected");
        bad = even; bad.version = 1001;
        rejects([&] { odh::floor_texture(bad); }, "unknown frame version rejected");
        odh::BitmapLimits budget; budget.width = 3;
        rejects([&] { odh::floor_texture(odd, budget); }, "padded width budget checked");
        budget = {}; budget.pixels = 7;
        rejects([&] { odh::floor_texture(odd, budget); }, "padded pixel budget checked");
        budget = {}; budget.height = 1;
        rejects([&] { odh::floor_texture(odd, budget); }, "source height budget checked");
        budget = {}; budget.pixels = 5;
        rejects([&] { odh::floor_texture(odd, budget); }, "source pixel budget checked");

        odh::WorldProjection camera;
        odh::BitmapImage texture{128, 64, 32, true, std::vector<Pixel>(8192, red), 0};
        const double root = std::sqrt(3.0 / 8.0);
        const std::array<std::array<int, 4>, 4> signs{{
            {1,-1,1,1}, {-1,-1,1,-1}, {-1,1,-1,-1}, {1,1,-1,1}}};
        const std::array<std::array<double, 2>, 4> base_uv{{{.5,0},{1,.5},{.5,1},{0,.5}}};
        const std::array<std::array<int, 2>, 4> xy{{{0,0},{1,0},{1,1},{0,1}}};
        for (unsigned rotation = 0; rotation < 4; ++rotation) {
            camera.rotation = static_cast<std::uint8_t>(rotation);
            for (unsigned zoom = 1; zoom <= 3; ++zoom) {
                camera.zoom = static_cast<std::uint8_t>(zoom);
                const double width = 32U << (zoom - 1);
                const auto whole = odh::floor_triangles(camera, 0, 0, 1, texture, 7, odh::FloorHalf::whole, false);
                require(whole.size() == 2 && whole[0].texture == 7 && whole[1].texture == 7,
                        "whole tile produces two triangles using supplied texture index");
                const std::array<odh::RasterVertex, 4> vertices{
                    whole[0].vertices[0], whole[0].vertices[1], whole[0].vertices[2], whole[1].vertices[1]};
                for (unsigned i = 0; i < 4; ++i) {
                    close(vertices[i].position[0], 200 + width/2*(xy[i][0]*signs[rotation][0]+xy[i][1]*signs[rotation][1]),
                          "corner projection horizontal analytic");
                    close(vertices[i].position[1], 200 + width/4*(xy[i][0]*signs[rotation][2]+xy[i][1]*signs[rotation][3]),
                          "corner projection vertical analytic");
                    close(vertices[i].position[2], -3*root*(xy[i][0]*signs[rotation][2]+xy[i][1]*signs[rotation][3]),
                          "corner affine depth analytic");
                    for (unsigned axis = 0; axis < 2; ++axis)
                        close(vertices[i].texcoord[axis], base_uv[i][axis ^ (rotation & 1U)], "rotation swaps global diamond UV");
                    require(vertices[i].normal == std::array<float, 3>{0,1,0}, "floor up normal explicit");
                }
                require(same(whole[0].vertices[0], whole[1].vertices[2]) &&
                        same(whole[0].vertices[2], whole[1].vertices[0]), "quad diagonal shares exact vertex data");
                require(std::abs(area(whole[0])+area(whole[1])-width*width/4) < .001,
                        "whole projected diamond area analytic");
                const std::array<odh::FloorHalf, 4> halves{odh::FloorHalf::horizontal_first,
                    odh::FloorHalf::horizontal_second, odh::FloorHalf::vertical_first, odh::FloorHalf::vertical_second};
                const std::array<std::array<unsigned, 3>, 4> indices{{{1,2,3},{0,1,3},{0,1,2},{2,3,0}}};
                for (unsigned h = 0; h < 4; ++h) {
                    const auto part = odh::floor_triangles(camera, 0, 0, 1, texture, 7, halves[h], false);
                    require(part.size() == 1 && std::abs(area(part[0])-width*width/8) < .001,
                            "each floor half has half diamond area");
                    for (unsigned i = 0; i < 3; ++i)
                        require(same(part[0].vertices[i], vertices[indices[h][i]]), "horizontal/vertical half indices follow wall enum");
                }
            }
        }
        camera = {};
        const auto downstairs = odh::floor_triangles(camera, 2, 3, 1, texture, 0);
        const auto upstairs = odh::floor_triangles(camera, 2, 3, 2, texture, 0);
        for (unsigned t = 0; t < 2; ++t) for (unsigned i = 0; i < 3; ++i) {
            close(upstairs[t].vertices[i].position[1]-downstairs[t].vertices[i].position[1], -128*root*2.95,
                  "story height projected with tile world conversion");
            close(upstairs[t].vertices[i].position[2]-downstairs[t].vertices[i].position[2], -1.5*2.95,
                  "story depth offset");
            require(upstairs[t].vertices[i].texcoord == downstairs[t].vertices[i].texcoord,
                    "UV alignment uses flattened projection independent of story height");
        }
        const auto slope = odh::floor_triangles(camera, 0, 0, 1, texture, 0, odh::FloorHalf::whole, true, {0,1,2,3});
        const auto flat = odh::floor_triangles(camera, 0, 0, 1, texture, 0);
        const std::array<unsigned, 3> alt{0,1,2};
        for (unsigned i = 0; i < 3; ++i) {
            close(slope[0].vertices[i].position[1]-flat[0].vertices[i].position[1], -128*root*alt[i],
                  "explicit per corner altitude projects geometry");
            require(slope[0].vertices[i].texcoord == flat[0].vertices[i].texcoord,
                    "explicit altitude does not alter flattened UV alignment");
        }
        // Odd viewport makes uv*size+screen exactly half-integral. Texture sizes
        // 2x2 distinguish even/odd ties rather than merely testing integer UVs.
        odh::BitmapImage tiny{2, 2, 32, true, std::vector<Pixel>(4, red), 0};
        camera.width = 401; camera.height = 301;
        const auto tied = odh::floor_triangles(camera, 0, 0, 1, tiny, 0);
        close(tied[0].vertices[0].texcoord[0], .75, "positive odd lower tie rounds up to even");
        close(tied[0].vertices[0].texcoord[1], -.25, "positive even lower tie rounds down to even");
        camera.center_tile = {4,0,0};
        const auto negative = odh::floor_triangles(camera, 0, 0, 1, tiny, 0);
        close(negative[0].vertices[0].texcoord[0], .75, "negative odd lower tie rounds toward even");
        // Adjacent quads compute the same global UVs and projected positions.
        camera = {};
        const auto left = odh::floor_triangles(camera, 0, 0, 1, tiny, 0);
        const auto right = odh::floor_triangles(camera, 1, 0, 1, tiny, 0);
        require(same(left[0].vertices[1], right[0].vertices[0]) &&
                same(left[0].vertices[2], right[1].vertices[1]), "adjacent tile edge shares exact UV/position");
        auto broken = texture; broken.pixels[0][3] = 254;
        rejects([&] { odh::floor_triangles(camera, 0, 0, 1, broken, 0); }, "nonopaque geometry texture rejected");
        broken = texture; broken.pixels.pop_back();
        rejects([&] { odh::floor_triangles(camera, 0, 0, 1, broken, 0); }, "geometry texture extent mismatch rejected");
        broken = texture; broken.width = 2049;
        rejects([&] { odh::floor_triangles(camera, 0, 0, 1, broken, 0); }, "geometry texture width budget rejected");
        rejects([&] { odh::floor_triangles(camera, 0, 0, 0, texture, 0); }, "level zero rejected");
        rejects([&] { odh::floor_triangles(camera, 0, 0, 3, texture, 0); }, "unsupported level rejected");
        rejects([&] { odh::floor_triangles(camera, 32768, 0, 1, texture, 0); }, "tile range rejected");
        rejects([&] { odh::floor_triangles(camera, std::numeric_limits<float>::quiet_NaN(), 0, 1, texture, 0); }, "nonfinite tile rejected");
        rejects([&] { odh::floor_triangles(camera, 0, 0, 1, texture, 0, static_cast<odh::FloorHalf>(17)); }, "unknown half rejected");
        rejects([&] { odh::floor_triangles(camera, 0, 0, 1, texture, 0, odh::FloorHalf::whole, true,
            {0,0,std::numeric_limits<float>::infinity(),0}); }, "nonfinite altitude rejected");
        camera.rotation = 4;
        rejects([&] { odh::floor_triangles(camera, 0, 0, 1, texture, 0); }, "bad rotation rejected");
        camera = {}; camera.zoom = 0;
        rejects([&] { odh::floor_triangles(camera, 0, 0, 1, texture, 0); }, "bad zoom rejected");
        camera = {}; camera.width = 0;
        rejects([&] { odh::floor_triangles(camera, 0, 0, 1, texture, 0); }, "empty viewport rejected");
        std::cout << checks << " floor checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "floor test failure: " << error.what() << '\n';
        return 1;
    }
}
