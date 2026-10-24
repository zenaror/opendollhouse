// Synthetic odh-person UI -> persistent overlay -> refreshed map integration.
#include "linux_family_overlay_store.hpp"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;
using Clock = std::chrono::steady_clock;
void require(bool value, const std::string& reason) {
    if (!value) throw std::runtime_error(reason);
}
void integer(Bytes& out, std::uint32_t value, unsigned width = 4, bool big = false) {
    for (unsigned i = 0; i < width; ++i)
        out.push_back(static_cast<std::uint8_t>(value >> (8 * (big ? width - 1 - i : i))));
}
void append(Bytes& out, const std::string& value) {
    out.insert(out.end(), value.begin(), value.end());
}
void append(Bytes& out, const Bytes& value) { out.insert(out.end(), value.begin(), value.end()); }
void write(const fs::path& path, const Bytes& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    require(bool(output), "cannot write synthetic fixture " + path.string());
}
Bytes read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "cannot read " + path.string());
    return Bytes(std::istreambuf_iterator<char>(input), {});
}
Bytes chunk(const std::string& kind, unsigned id, const Bytes& data) {
    Bytes result; append(result, kind);
    integer(result, static_cast<std::uint32_t>(76 + data.size()), 4, true);
    integer(result, id, 2, true); integer(result, 0, 2); result.resize(76);
    append(result, data); return result;
}
Bytes iff(const Bytes& chunks = {}) {
    Bytes result;
    append(result, "IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1");
    result.resize(64); append(result, chunks); return result;
}
struct Temp {
    fs::path path;
    Temp() {
        char pattern[] = "/tmp/odh-move-out-integration-XXXXXX";
        const auto created = ::mkdtemp(pattern);
        require(created, "cannot create private fixture directory"); path = created;
    }
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
};

std::vector<odh::FamilyEntry> make_game(const fs::path& root) {
    std::vector<odh::FamilyEntry> families;
    Bytes neighborhood, locations;
    integer(locations, 0xfffd, 2); integer(locations, 2, 2);
    for (unsigned house : {7U, 8U}) {
        odh::Family family;
        family.version = 9; family.house_number = house; family.family_number = 900 + house;
        family.budget = 7000; family.value_in_arch = 2300;
        family.member_guids = {0x12345670U + house};
        families.push_back({static_cast<std::uint16_t>(200 + house), family});
        Bytes data;
        for (auto value : {0U, 9U, 0x46414d49U, house, 900 + house, 7000U, 2300U,
                           0U, 0U, 1U, 0x12345670U + house}) integer(data, value);
        append(neighborhood, chunk("FAMI", 200 + house, data));
        locations.push_back(1);
        append(locations, std::to_string(house) + (house == 7 ? ",200,200" : ",600,200"));
        locations.push_back(0); locations.push_back(0);
        // Valid empty IFFs identify selectable lots. No executable lot content
        // is needed: this test always cancels at the occupied-lot choice.
        write(root / "UserData/Houses" / ("House0" + std::to_string(house) + ".iff"), iff());
    }
    write(root / "UserData/Neighborhood.iff", iff(neighborhood));
    write(root / "UserData/LotLocations.iff", iff(chunk("STR#", 1, locations)));

    // Uncompressed 24-bit black 800x600 BMPs for both layers used by mode 1.
    Bytes bitmap; append(bitmap, "BM"); integer(bitmap, 54 + 800 * 600 * 3);
    integer(bitmap, 0); integer(bitmap, 54); integer(bitmap, 40);
    integer(bitmap, 800); integer(bitmap, 600); integer(bitmap, 1, 2); integer(bitmap, 24, 2);
    bitmap.resize(54 + 800 * 600 * 3);
    const std::string base_name = "Nbhd\\NScreen.BMP";
    const std::string layer_name = "Nbhd\\DiffN1-N2_8.bmp";
    Bytes archive; append(archive, "FAR!byAZ"); integer(archive, 1);
    integer(archive, static_cast<std::uint32_t>(16 + bitmap.size() * 2));
    append(archive, bitmap); append(archive, bitmap);
    integer(archive, 2);
    for (const auto& [name, offset] : std::array<std::pair<std::string, std::uint32_t>, 2>{
             std::pair{base_name, std::uint32_t{16}},
             std::pair{layer_name, static_cast<std::uint32_t>(16 + bitmap.size())}}) {
        integer(archive, bitmap.size()); integer(archive, bitmap.size());
        integer(archive, offset); integer(archive, name.size()); append(archive, name);
    }
    write(root / "UIGraphics/UIGraphics.far", archive);
    return families;
}

struct Child {
    pid_t pid = -1;
    fs::path log;
    Child(const char* binary, const fs::path& root, const fs::path& output) : log(output) {
        pid = ::fork(); require(pid >= 0, "cannot fork odh-person");
        if (pid == 0) {
            const int fd = ::open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd < 0 || ::dup2(fd, STDOUT_FILENO) < 0 || ::dup2(fd, STDERR_FILENO) < 0) ::_exit(126);
            if (fd > STDERR_FILENO) ::close(fd);
            ::execl(binary, binary, "--game-root", root.c_str(), "--mode", "1",
                    "--language", "1", static_cast<char*>(nullptr));
            ::_exit(127);
        }
    }
    ~Child() {
        if (pid < 0) return;
        ::kill(pid, SIGKILL); ::waitpid(pid, nullptr, 0);
        std::ifstream input(log);
        std::cerr << "odh-person output: " << input.rdbuf() << '\n';
    }
    void expect_cancelled() {
        int status = 0;
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        pid_t result;
        while ((result = ::waitpid(pid, &status, WNOHANG)) == 0) {
            require(Clock::now() < deadline, "odh-person did not exit after cancellation");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        require(result == pid, "cannot collect odh-person exit status");
        pid = -1;
        const auto bytes = read(log);
        const std::string output(bytes.begin(), bytes.end());
        require(WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
            output.find("\"stage\":\"occupied-lot-choice\"") != std::string::npos,
            "expected cancellation before lot runtime; output: " + output);
    }
};

std::set<Window> windows(Display* display, Window parent) {
    Window root, returned_parent, *children = nullptr; unsigned count = 0;
    std::set<Window> result;
    if (XQueryTree(display, parent, &root, &returned_parent, &children, &count)) {
        for (unsigned i = 0; i < count; ++i) {
            result.insert(children[i]);
            const auto descendants = windows(display, children[i]);
            result.insert(descendants.begin(), descendants.end());
        }
        if (children) XFree(children);
    }
    return result;
}
std::string title(Display* display, Window window) {
    char* value = nullptr; std::string result;
    if (XFetchName(display, window, &value) && value) { result = value; XFree(value); }
    return result;
}
unsigned long window_pid(Display* display, Window window) {
    const Atom property = XInternAtom(display, "_NET_WM_PID", True);
    if (property == None) return 0;
    Atom type = None; int format = 0; unsigned long count = 0, remaining = 0;
    unsigned char* bytes = nullptr;
    const auto status = XGetWindowProperty(display, window, property, 0, 1, False,
        XA_CARDINAL, &type, &format, &count, &remaining, &bytes);
    unsigned long result = 0;
    if (status == Success && type == XA_CARDINAL && format == 32 && count == 1 && bytes)
        result = *reinterpret_cast<unsigned long*>(bytes);
    if (bytes) XFree(bytes);
    return result;
}
template<class Predicate> void await(Predicate predicate, const std::string& reason) {
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        require(Clock::now() < deadline, "timed out: " + reason);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
Window new_map(Display* display, const std::set<Window>& before, pid_t expected_pid) {
    Window selected = 0;
    await([&] {
        std::vector<Window> matches;
        for (auto window : windows(display, DefaultRootWindow(display))) {
            const auto window_title = title(display, window);
            if (!before.count(window) &&
                    window_title.rfind("OpenDollhouse | Neighborhood |", 0) == 0 &&
                    window_pid(display, window) == static_cast<unsigned long>(expected_pid))
                matches.push_back(window);
        }
        require(matches.size() <= 1, "ambiguous new odh-person windows; use an isolated X display");
        if (!matches.empty()) selected = matches.front();
        return selected != 0;
    }, "new odh-person neighborhood window");
    return selected;
}
void wait_title(Display* display, Window window, const std::string& expected) {
    await([&] { return title(display, window).find(expected) != std::string::npos; }, expected);
}
void wait_exact_title(Display* display, Window window, const std::string& expected) {
    await([&] { return title(display, window) == expected; }, expected);
}
void key(Display* display, Window window, KeySym symbol) {
    for (int type : {KeyPress, KeyRelease}) {
        XEvent event{}; event.xkey.type = type; event.xkey.display = display;
        event.xkey.window = window; event.xkey.root = DefaultRootWindow(display);
        event.xkey.same_screen = True; event.xkey.keycode = XKeysymToKeycode(display, symbol);
        require(event.xkey.keycode && XSendEvent(display, window, False,
            type == KeyPress ? KeyPressMask : KeyReleaseMask, &event), "cannot send UI key");
        XSync(display, False);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}
void click(Display* display, Window window, int x) {
    XEvent event{}; event.xbutton.type = ButtonPress; event.xbutton.display = display;
    event.xbutton.window = window; event.xbutton.root = DefaultRootWindow(display);
    event.xbutton.same_screen = True; event.xbutton.button = Button1;
    event.xbutton.x = x; event.xbutton.y = 200;
    require(XSendEvent(display, window, False, ButtonPressMask, &event), "cannot send lot click");
    XSync(display, False);
}
unsigned long channel(unsigned value, unsigned long mask) {
    if (!mask) return 0;
    unsigned shift = 0;
    while (!(mask & 1)) { mask >>= 1; ++shift; }
    return ((value * mask + 127) / 255) << shift;
}
void marker(Display* display, Window window, int x, bool occupied) {
    unsigned long last_pixel = 0, last_expected = 0;
    unsigned long red_mask = 0, green_mask = 0, blue_mask = 0;
    int map_state = -1, width = 0, height = 0;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < deadline) {
        XWindowAttributes attributes{};
        if (XGetWindowAttributes(display, window, &attributes)) {
            map_state = attributes.map_state;
            width = attributes.width;
            height = attributes.height;
        }
        XImage* image = XGetImage(display, window, x, 200, 1, 1, AllPlanes, ZPixmap);
        if (image) {
            red_mask = image->red_mask;
            green_mask = image->green_mask;
            blue_mask = image->blue_mask;
            last_pixel = XGetPixel(image, 0, 0);
            last_expected = occupied ?
            channel(245, image->red_mask) | channel(185, image->green_mask) | channel(30, image->blue_mask) :
            channel(40, image->red_mask) | channel(220, image->green_mask) | channel(120, image->blue_mask);
            XDestroyImage(image);
            if (last_pixel == last_expected) return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    throw std::runtime_error(std::string("timed out: ") +
        (occupied ? "occupied map marker" : "vacant map marker after move-out") +
        "; pixel=" + std::to_string(last_pixel) +
        ", expected=" + std::to_string(last_expected) +
        ", masks=" + std::to_string(red_mask) + "/" + std::to_string(green_mask) +
        "/" + std::to_string(blue_mask) + ", window=" + std::to_string(width) + "x" +
        std::to_string(height) + ", map_state=" + std::to_string(map_state) +
        ", title='" + title(display, window) + "'");
}
}

int main(int argc, char** argv) {
    if (!std::getenv("DISPLAY")) { std::cout << "DISPLAY unavailable: skipped\n"; return 77; }
    Display* display = XOpenDisplay(nullptr);
    if (!display) { std::cout << "X11 unavailable: skipped\n"; return 77; }
    // Querying a window tree can race windows disappearing in other clients.
    XSetErrorHandler([](Display*, XErrorEvent*) { return 0; });
    try {
        require(argc == 2, "expected odh-person executable path");
        Temp temp;
        for (const auto* variable : {"HOME", "XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_DATA_HOME", "ODH_STATE_DIR"}) {
            const auto isolated = temp.path / variable;
            fs::create_directory(isolated);
            require(::setenv(variable, isolated.c_str(), 1) == 0, "cannot isolate player profile");
        }
        const auto game = temp.path / "game";
        const auto families = make_game(game);
        const auto neighborhood = game / "UserData/Neighborhood.iff";
        const auto source = read(neighborhood);
        const auto identity = odh_host::linux_neighborhood_identity(neighborhood);
        const auto overlay_path = odh_host::linux_family_overlay_path(identity);
        require(!fs::exists(overlay_path), "new profile must have no family overlay");
        auto before = windows(display, DefaultRootWindow(display));
        {
            Child app(argv[1], game, temp.path / "first.log");
            const auto window = new_map(display, before, app.pid);
            key(display, window, XK_c);
            wait_exact_title(display, window, "OpenDollhouse | Create family");
            key(display, window, XK_Escape);
            wait_exact_title(display, window,
                "OpenDollhouse | Neighborhood | Neighborhood | C: Create family | Esc: Cancel");
            key(display, window, XK_c);
            wait_exact_title(display, window, "OpenDollhouse | Create family");
            key(display, window, XK_Escape);
            wait_exact_title(display, window,
                "OpenDollhouse | Neighborhood | Neighborhood | C: Create family | Esc: Cancel");
            marker(display, window, 200, true); marker(display, window, 600, true);
            click(display, window, 200);
            wait_title(display, window, "Enter: OK | Esc: Cancel");
            key(display, window, XK_Return);
            wait_title(display, window, "House 7 | Enter: enter | E: move out");
            key(display, window, XK_e);
            wait_title(display, window, "confirm move-out | House 7 | refund 2300 | budget after 9300");
            require(!fs::exists(overlay_path), "confirmation must not write before Enter");
            key(display, window, XK_Return);
            wait_title(display, window, "Neighborhood");
            marker(display, window, 200, false); marker(display, window, 600, true);
            const auto saved = odh_host::read_linux_family_overlay(overlay_path);
            require(saved.has_value(), "confirmed UI move-out must persist an overlay");
            const auto expected = odh::plan_family_move_out_overlay(families, identity, nullptr, 207);
            require(odh::encode_family_overlay(*saved) == odh::encode_family_overlay(expected),
                "UI save must contain exactly the identity-bound move-out, refund and final budget");
            click(display, window, 600);
            wait_title(display, window, "Enter: OK | Esc: Cancel");
            key(display, window, XK_Return);
            wait_title(display, window, "House 8 | Enter: enter | E: move out");
            key(display, window, XK_Escape); app.expect_cancelled();
        }
        const auto saved_bytes = read(overlay_path);
        before = windows(display, DefaultRootWindow(display));
        {
            Child app(argv[1], game, temp.path / "reopen.log");
            const auto window = new_map(display, before, app.pid);
            marker(display, window, 200, false); marker(display, window, 600, true);
            click(display, window, 600);
            wait_title(display, window, "Enter: OK | Esc: Cancel");
            key(display, window, XK_Return);
            wait_title(display, window, "House 8 | Enter: enter | E: move out");
            key(display, window, XK_Escape); app.expect_cancelled();
        }
        require(read(overlay_path) == saved_bytes, "reopening and cancelling must preserve overlay bytes");
        require(read(neighborhood) == source, "source neighborhood must remain unchanged");
        require(!fs::exists(temp.path / "XDG_DATA_HOME/opendollhouse/saves"), "must cancel before simulation saves");
        std::cout << "Synthetic Linux move-out: map -> E -> Enter -> saved overlay -> refreshed map -> House08; reopen persisted map passed\n";
        XCloseDisplay(display); return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; XCloseDisplay(display); return 1;
    }
}
