#include "linux_window.hpp"
#include "odh/formats.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <cstdlib>
#include <chrono>
#include <limits>
#include <thread>

namespace odh_host {
namespace {

struct Channel {
    unsigned long mask = 0;
    unsigned shift = 0;
    std::uint64_t maximum = 0;
};

Channel channel(unsigned long mask, unsigned bits) {
    if (!mask || bits > 32 || (static_cast<std::uint64_t>(mask) >> bits))
        throw odh::FormatError("X11: unsupported RGB visual mask");
    Channel result;
    result.mask = mask;
    while (!(mask & 1UL)) { ++result.shift; mask >>= 1; }
    unsigned count = 0;
    while (mask & 1UL) { ++count; mask >>= 1; }
    if (mask || count > 16)
        throw odh::FormatError("X11: RGB masks must be contiguous and at most 16 bits");
    result.maximum = (std::uint64_t{1} << count) - 1;
    return result;
}

unsigned long encode(std::uint8_t value, const Channel& c) {
    return static_cast<unsigned long>((value * c.maximum + 127) / 255) << c.shift;
}

std::uint8_t decode(unsigned long value, const Channel& c) {
    return static_cast<std::uint8_t>((((value & c.mask) >> c.shift) * 255ULL +
                                    c.maximum / 2) / c.maximum);
}

struct ImageDeleter {
    void operator()(XImage* image) const {
        if (image) XDestroyImage(image);
    }
};
using ImagePtr = std::unique_ptr<XImage, ImageDeleter>;

void validate_dimensions(std::uint32_t width, std::uint32_t height) {
    if (!width || !height || width > 2048 || height > 2048 ||
        static_cast<std::uint64_t>(width) * height > 1048576)
        throw odh::FormatError("X11: viewport exceeds 2048 per axis or 1048576 pixels");
}

ImagePtr create_image(Display* display, Visual* visual, int depth,
                      std::uint32_t width, std::uint32_t height) {
    ImagePtr image(XCreateImage(display, visual, static_cast<unsigned>(depth), ZPixmap,
                                0, nullptr, width, height, 32, 0));
    if (!image) throw odh::FormatError("X11: cannot create native image");
    if (image->format != ZPixmap || image->xoffset != 0 ||
        image->width != static_cast<int>(width) || image->height != static_cast<int>(height) ||
        image->depth != depth ||
        (image->bits_per_pixel != 16 && image->bits_per_pixel != 24 && image->bits_per_pixel != 32) ||
        (image->byte_order != LSBFirst && image->byte_order != MSBFirst) ||
        image->bytes_per_line < static_cast<int>(width * (image->bits_per_pixel / 8)) ||
        image->red_mask != visual->red_mask || image->green_mask != visual->green_mask ||
        image->blue_mask != visual->blue_mask)
        throw odh::FormatError("X11: unsupported native image format");
    const auto stride = static_cast<std::size_t>(image->bytes_per_line);
    if (stride > std::numeric_limits<std::size_t>::max() / height ||
        stride > static_cast<std::uint64_t>(width) * 4 + 4)
        throw odh::FormatError("X11: native image allocation exceeds bounds");
    image->data = static_cast<char*>(std::calloc(height, stride));
    if (!image->data) throw odh::FormatError("X11: cannot allocate image buffer");
    return image;
}

} // namespace

struct LinuxWindow::Impl {
    Display* display = nullptr;
    Window window = 0;
    GC gc = nullptr;
    XImage* image = nullptr;
    Atom delete_message = 0, protocols = 0;
    std::uint32_t width = 0, height = 0;
    Channel red, green, blue;
    std::array<bool,256> key_down{};
    bool alive = false, quit = false;

    ~Impl() {
        if (image) XDestroyImage(image);
        if (display) {
            if (gc) XFreeGC(display, gc);
            // Closing this private connection destroys its remaining windows.
            // Avoid a separate destroy request racing an already queued
            // DestroyNotify; no process-global X error handler is installed.
            XCloseDisplay(display);
        }
    }

    void require_alive() const {
        if (!alive) throw odh::FormatError("X11: window has been destroyed");
    }

    void validate_image(const XImage& value, int depth, const Visual& visual,
                        std::uint32_t expected_width, std::uint32_t expected_height) const {
        if (value.format != ZPixmap || value.xoffset != 0 ||
            value.width != static_cast<int>(expected_width) ||
            value.height != static_cast<int>(expected_height) || value.depth != depth ||
            (value.bits_per_pixel != 16 && value.bits_per_pixel != 24 && value.bits_per_pixel != 32) ||
            (value.byte_order != LSBFirst && value.byte_order != MSBFirst) ||
            value.bytes_per_line < static_cast<int>(expected_width * (value.bits_per_pixel / 8)) ||
            value.red_mask != visual.red_mask || value.green_mask != visual.green_mask ||
            value.blue_mask != visual.blue_mask)
            throw odh::FormatError("X11: unsupported native image format");
    }
};

LinuxWindow::LinuxWindow(std::uint32_t width, std::uint32_t height, const std::string& title)
    : impl_(std::make_unique<Impl>()) {
    validate_dimensions(width, height);
    auto& p = *impl_;
    p.width = width; p.height = height;
    p.display = XOpenDisplay(nullptr);
    if (!p.display) throw odh::FormatError("X11: cannot open DISPLAY; a running X server is required");
    const int screen = DefaultScreen(p.display);
    Visual* visual = DefaultVisual(p.display, screen);
    const int depth = DefaultDepth(p.display, screen);
    if (visual->c_class != TrueColor ||
        (depth != 15 && depth != 16 && depth != 24 && depth != 30 && depth != 32) ||
        (visual->red_mask & visual->green_mask) || (visual->red_mask & visual->blue_mask) ||
        (visual->green_mask & visual->blue_mask))
        throw odh::FormatError("X11: a supported TrueColor visual is required");
    p.red = channel(visual->red_mask, static_cast<unsigned>(depth));
    p.green = channel(visual->green_mask, static_cast<unsigned>(depth));
    p.blue = channel(visual->blue_mask, static_cast<unsigned>(depth));
    auto image = create_image(p.display, visual, depth, width, height);
    p.image = image.release();
    p.window = XCreateSimpleWindow(p.display, RootWindow(p.display, screen), 0, 0,
                                   width, height, 0, 0, 0);
    if (!p.window) throw odh::FormatError("X11: cannot create window");
    p.alive = true;
    p.gc = XCreateGC(p.display, p.window, 0, nullptr);
    if (!p.gc) throw odh::FormatError("X11: cannot create graphics context");
    XSelectInput(p.display, p.window, KeyPressMask | KeyReleaseMask | ButtonPressMask |
                 FocusChangeMask | StructureNotifyMask | ExposureMask);
    XSizeHints hints{};
    hints.flags = PMinSize | PMaxSize;
    hints.min_width = hints.max_width = static_cast<int>(width);
    hints.min_height = hints.max_height = static_cast<int>(height);
    XSetWMNormalHints(p.display, p.window, &hints);
    p.delete_message = XInternAtom(p.display, "WM_DELETE_WINDOW", False);
    p.protocols = XInternAtom(p.display, "WM_PROTOCOLS", False);
    XSetWMProtocols(p.display, p.window, &p.delete_message, 1);
    set_title(title);
    XMapWindow(p.display, p.window);
    XSync(p.display, False);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
        XWindowAttributes attributes{};
        if (!XGetWindowAttributes(p.display, p.window, &attributes))
            throw odh::FormatError("X11: cannot inspect own window mapping state");
        if (attributes.map_state == IsViewable) break;
        if (std::chrono::steady_clock::now() >= deadline)
            throw odh::FormatError("X11: own window did not become viewable within two seconds");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

LinuxWindow::~LinuxWindow() = default;

void LinuxWindow::resize(std::uint32_t width, std::uint32_t height) {
    auto& p = *impl_;
    p.require_alive();
    validate_dimensions(width, height);
    if (width == p.width && height == p.height) return;

    const int screen = DefaultScreen(p.display);
    Visual* visual = DefaultVisual(p.display, screen);
    const int depth = DefaultDepth(p.display, screen);
    auto replacement = create_image(p.display, visual, depth, width, height);

    XSizeHints hints{};
    hints.flags = PMinSize | PMaxSize;
    hints.min_width = hints.max_width = static_cast<int>(width);
    hints.min_height = hints.max_height = static_cast<int>(height);
    XSetWMNormalHints(p.display, p.window, &hints);
    XResizeWindow(p.display, p.window, width, height);
    XSync(p.display, False);

    XWindowAttributes attributes{};
    if (!XGetWindowAttributes(p.display, p.window, &attributes) ||
        attributes.width != static_cast<int>(width) ||
        attributes.height != static_cast<int>(height)) {
        XSizeHints old_hints{};
        old_hints.flags = PMinSize | PMaxSize;
        old_hints.min_width = old_hints.max_width = static_cast<int>(p.width);
        old_hints.min_height = old_hints.max_height = static_cast<int>(p.height);
        XSetWMNormalHints(p.display, p.window, &old_hints);
        XResizeWindow(p.display, p.window, p.width, p.height);
        XSync(p.display, False);
        throw odh::FormatError("X11: native window rejected requested viewport size");
    }

    XImage* previous = p.image;
    p.image = replacement.release();
    p.width = width;
    p.height = height;
    XDestroyImage(previous);
    XFlush(p.display);
}

std::uint32_t LinuxWindow::width() const noexcept { return impl_->width; }

std::uint32_t LinuxWindow::height() const noexcept { return impl_->height; }

void LinuxWindow::present(const odh::RasterFrame& frame) {
    auto& p = *impl_;
    p.require_alive();
    const std::size_t count = static_cast<std::size_t>(p.width) * p.height;
    if (frame.width != p.width || frame.height != p.height ||
        frame.pixels.size() != count || frame.depth.size() != count)
        throw odh::FormatError("X11: frame dimensions and arrays must exactly match viewport");
    for (const auto& pixel : frame.pixels)
        if (pixel[3] != 255) throw odh::FormatError("X11: frame pixels must be opaque");
    const unsigned bytes = static_cast<unsigned>(p.image->bits_per_pixel / 8);
    for (std::uint32_t y = 0; y < p.height; ++y) {
        auto* row = reinterpret_cast<unsigned char*>(p.image->data) +
                    static_cast<std::size_t>(y) * p.image->bytes_per_line;
        for (std::uint32_t x = 0; x < p.width; ++x) {
            const auto& pixel = frame.pixels[static_cast<std::size_t>(y) * p.width + x];
            const unsigned long native = encode(pixel[0], p.red) | encode(pixel[1], p.green) |
                                         encode(pixel[2], p.blue);
            for (unsigned b = 0; b < bytes; ++b) {
                const unsigned shift = 8 * (p.image->byte_order == LSBFirst ? b : bytes - 1 - b);
                row[x * bytes + b] = static_cast<unsigned char>((native >> shift) & 255UL);
            }
        }
    }
    XPutImage(p.display, p.window, p.gc, p.image, 0, 0, 0, 0, p.width, p.height);
    XFlush(p.display);
}

WindowInput LinuxWindow::poll() {
    auto& p = *impl_;
    WindowInput result;
    for (unsigned count = 0; count < 4096 && XPending(p.display); ++count) {
        XEvent event{};
        XNextEvent(p.display, &event);
        if (event.xany.window != p.window) continue;
        if (event.type == FocusOut) p.key_down.fill(false);
        if (event.type == DestroyNotify) { p.alive = false; p.quit = true; }
        if (event.type == ClientMessage && event.xclient.message_type == p.protocols &&
            event.xclient.format == 32 &&
            static_cast<Atom>(event.xclient.data.l[0]) == p.delete_message) p.quit = true;
        if (event.type == ButtonPress && event.xbutton.button == Button1) {
            result.action = true;
            result.primary_click=std::array<std::int32_t,2>{event.xbutton.x,event.xbutton.y};
        }
        if (event.type == ConfigureNotify) result.redraw = true;
        if (event.type == Expose && p.alive && event.xexpose.count == 0) {
            result.redraw = true;
            XPutImage(p.display, p.window, p.gc, p.image, 0, 0, 0, 0, p.width, p.height);
        }
        if (event.type == KeyRelease) {
            bool repeat_pair = false;
            if (XPending(p.display)) {
                XEvent next{};
                XPeekEvent(p.display, &next);
                repeat_pair = next.type == KeyPress && next.xkey.window == p.window &&
                              next.xkey.keycode == event.xkey.keycode &&
                              next.xkey.time == event.xkey.time;
            }
            if (!repeat_pair && event.xkey.keycode < p.key_down.size())
                p.key_down[event.xkey.keycode] = false;
            continue;
        }
        if (event.type != KeyPress) continue;
        const bool held = event.xkey.keycode < p.key_down.size() && p.key_down[event.xkey.keycode];
        if (event.xkey.keycode < p.key_down.size()) p.key_down[event.xkey.keycode] = true;
        KeySym key = XLookupKeysym(&event.xkey, (event.xkey.state & ShiftMask) ? 1 : 0);
        if (key == NoSymbol) key = XLookupKeysym(&event.xkey, 0);
        switch (key) {
        case XK_Escape: p.quit = true; break;
        case XK_BackSpace: if (!held) result.interrupt = true; break;
        case XK_u: case XK_U: if (!held) result.stand = true; break;
        case XK_m: case XK_M: if (!held) result.neighborhood = true; break;
        case XK_s: case XK_S: if (!held) result.save = true; break;
        case XK_space: case XK_Return: case XK_KP_Enter: if (!held) result.action = true; break;
        case XK_Tab: result.interaction_selection_delta += (event.xkey.state & ShiftMask) ? -1 : 1; break;
        case XK_ISO_Left_Tab: --result.interaction_selection_delta; break;
        case XK_p: case XK_P: if (!held) result.pause = true; break;
        case XK_Left: --result.rotation; break;
        case XK_Right: ++result.rotation; break;
        case XK_plus: case XK_equal: case XK_KP_Add: case XK_Up: ++result.zoom; break;
        case XK_minus: case XK_underscore: case XK_KP_Subtract: case XK_Down: --result.zoom; break;
        default: break;
        }
    }
    result.quit = p.quit;
    XFlush(p.display);
    return result;
}

void LinuxWindow::set_title(const std::string& title) {
    impl_->require_alive();
    if (title.size() > 4096 || title.find('\0') != std::string::npos)
        throw odh::FormatError("X11: title must be at most 4096 bytes without embedded NUL");
    Xutf8SetWMProperties(impl_->display,impl_->window,const_cast<char*>(title.c_str()),
        const_cast<char*>(title.c_str()),nullptr,0,nullptr,nullptr,nullptr);
    const Atom name_property=XInternAtom(impl_->display,"_NET_WM_NAME",False);
    const Atom utf8_type=XInternAtom(impl_->display,"UTF8_STRING",False);
    XChangeProperty(impl_->display,impl_->window,name_property,utf8_type,8,PropModeReplace,
        reinterpret_cast<const unsigned char*>(title.data()),static_cast<int>(title.size()));
    XFlush(impl_->display);
}

std::vector<std::array<std::uint8_t,4>> LinuxWindow::readback() const {
    const auto& p = *impl_;
    p.require_alive();
    XSync(p.display, False);
    XWindowAttributes attributes{};
    if (!XGetWindowAttributes(p.display, p.window, &attributes) || attributes.map_state != IsViewable ||
        attributes.width < static_cast<int>(p.width) || attributes.height < static_cast<int>(p.height))
        throw odh::FormatError("X11: own window must be viewable and contain the viewport for pixel readback");
    ImagePtr image(XGetImage(p.display, p.window, 0, 0, p.width, p.height, AllPlanes, ZPixmap));
    if (!image) throw odh::FormatError("X11: cannot read window image");
    const int screen = DefaultScreen(p.display);
    p.validate_image(*image, DefaultDepth(p.display, screen), *DefaultVisual(p.display, screen),
                      p.width, p.height);
    std::vector<std::array<std::uint8_t,4>> result(static_cast<std::size_t>(p.width) * p.height);
    for (std::uint32_t y = 0; y < p.height; ++y)
        for (std::uint32_t x = 0; x < p.width; ++x) {
            const auto pixel = XGetPixel(image.get(), static_cast<int>(x), static_cast<int>(y));
            result[static_cast<std::size_t>(y) * p.width + x] =
                std::array<std::uint8_t,4>{decode(pixel, p.red), decode(pixel, p.green),
                                          decode(pixel, p.blue), 255};
        }
    return result;
}

unsigned long LinuxWindow::native_id() const { return impl_->window; }

} // namespace odh_host
