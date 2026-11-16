//========================================================================
// GLFW on PeOS: the Null platform's window in Starlight, peos_gl contexts
//------------------------------------------------------------------------
// Copyright (c) 2026 PeOS
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software. Permission is granted to anyone
// to use this software for any purpose, including commercial applications,
// and to alter it and redistribute it freely (the zlib license, as GLFW).
//========================================================================
// See peos_platform.h. A program has one window on PeOS: Starlight gives an
// app one body, sized by Starlight (the size asked for is a wish, the one
// given comes as a resize). Events come on standard input (SLUI), so
// waiting for one is waiting on descriptor 0: no polling while idle.

#include "internal.h"

#if defined(__peos__)

#include <peos/peos.h>
#include <peos/starlight_ui.h>
#include <stdio.h>
#include <string.h>

static struct
{
    _GLFWwindow* window;        // the one window
    uint32_t     surfW, surfH;  // the pixels asked for (a little larger)
    int          pixelsReady;
    unsigned     buttons;       // the pointer's buttons held (SLUI's bits)
    int          entered;
    double       lastX, lastY;
    int          clipboardHere;
    int          cursorMode;    // the window's GLFW_CURSOR mode
    struct peosCursor* cursor;  // the image cursor set, or NULL (Starlight's arrow)
} peos = { .cursorMode = GLFW_CURSOR_NORMAL };

// ---- cursors made from an image ----

// Its pixels premultiplied, 0xAARRGGBB, drawn over the frame at each swap.
struct peosCursor
{
    _GLFWcursor* owner;
    uint32_t*    px;
    int          w, h, hx, hy;
};

#define PEOS_CURSORS 32
static struct peosCursor cursors[PEOS_CURSORS];

static struct peosCursor* cursorOf(const _GLFWcursor* owner)
{
    for (int i = 0;  owner && i < PEOS_CURSORS;  i++)
    {
        if (cursors[i].owner == owner)
            return &cursors[i];
    }
    return NULL;
}

// What Starlight shows over the pixels: nothing when the mode hides the
// pointer or when an image cursor is drawn into the frame.
static void updatePointer(void)
{
    const int shown = peos.cursorMode == GLFW_CURSOR_NORMAL ||
                      peos.cursorMode == GLFW_CURSOR_CAPTURED;
    slui_pixels_cursor(shown && !peos.cursor ? SLUI_CURSOR_ARROW : SLUI_CURSOR_NONE);
}

void _glfwPeOSCreateCursor(_GLFWcursor* cursor, const GLFWimage* image, int xhot, int yhot)
{
    struct peosCursor* c = NULL;
    for (int i = 0;  !c && i < PEOS_CURSORS;  i++)
    {
        if (!cursors[i].owner)
            c = &cursors[i];
    }
    if (!c || !image || image->width <= 0 || image->height <= 0 ||
        image->width > 256 || image->height > 256)
        return;   // no room or no image: Starlight's arrow for it

    uint32_t* px = _glfw_calloc((size_t) image->width * (size_t) image->height, sizeof(uint32_t));
    if (!px)
        return;
    const unsigned char* src = image->pixels;
    for (int i = 0;  i < image->width * image->height;  i++)
    {
        const uint32_t a = src[i * 4 + 3];
        const uint32_t r = (src[i * 4 + 0] * a + 127) / 255;
        const uint32_t g = (src[i * 4 + 1] * a + 127) / 255;
        const uint32_t b = (src[i * 4 + 2] * a + 127) / 255;
        px[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    c->owner = cursor;
    c->px = px;
    c->w = image->width;
    c->h = image->height;
    c->hx = xhot;
    c->hy = yhot;
}

void _glfwPeOSDestroyCursor(_GLFWcursor* cursor)
{
    struct peosCursor* c = cursorOf(cursor);
    if (!c)
        return;
    if (peos.cursor == c)
    {
        peos.cursor = NULL;
        updatePointer();
    }
    _glfw_free(c->px);
    memset(c, 0, sizeof(*c));
}

void _glfwPeOSSetCursor(_GLFWwindow* window, _GLFWcursor* cursor)
{
    (void) window;
    struct peosCursor* c = cursorOf(cursor);
    if (c == peos.cursor)
        return;
    peos.cursor = c;
    updatePointer();
}

// The image cursor over the frame just copied, where the pointer is.
static void drawCursor(uint32_t* px, uint32_t pitch, int fw, int fh)
{
    const struct peosCursor* c = peos.cursor;
    if (!c || !peos.entered ||
        (peos.cursorMode != GLFW_CURSOR_NORMAL && peos.cursorMode != GLFW_CURSOR_CAPTURED))
        return;

    const int x0 = (int) peos.lastX - c->hx, y0 = (int) peos.lastY - c->hy;
    for (int y = 0;  y < c->h;  y++)
    {
        const int fy = y0 + y;
        if (fy < 0 || fy >= fh)
            continue;
        uint32_t* row = (uint32_t*) ((unsigned char*) px + (size_t) fy * pitch);
        const uint32_t* src = c->px + (size_t) y * (size_t) c->w;
        for (int x = 0;  x < c->w;  x++)
        {
            const int fx = x0 + x;
            const uint32_t s = src[x], a = s >> 24;
            if (fx < 0 || fx >= fw || a == 0)
                continue;
            if (a == 255)
            {
                row[fx] = s & 0x00FFFFFFu;
                continue;
            }
            const uint32_t d = row[fx], k = 255 - a;
            const uint32_t r = ((s >> 16) & 0xFF) + (((d >> 16) & 0xFF) * k + 127) / 255;
            const uint32_t g = ((s >> 8) & 0xFF) + (((d >> 8) & 0xFF) * k + 127) / 255;
            const uint32_t b = (s & 0xFF) + ((d & 0xFF) * k + 127) / 255;
            row[fx] = (r << 16) | (g << 8) | b;
        }
    }
}

// ---- keys ----

// PC set-1 scan codes (PeOS's key codes; 0xE0xx the extended ones, and the
// arrows and the like also without the 0xE0) to GLFW's keys.
static int keyOf(uint16_t code)
{
    static const short plain[0x59] =
    {
        [0x01] = GLFW_KEY_ESCAPE, [0x02] = GLFW_KEY_1, [0x03] = GLFW_KEY_2, [0x04] = GLFW_KEY_3,
        [0x05] = GLFW_KEY_4, [0x06] = GLFW_KEY_5, [0x07] = GLFW_KEY_6, [0x08] = GLFW_KEY_7,
        [0x09] = GLFW_KEY_8, [0x0A] = GLFW_KEY_9, [0x0B] = GLFW_KEY_0, [0x0C] = GLFW_KEY_MINUS,
        [0x0D] = GLFW_KEY_EQUAL, [0x0E] = GLFW_KEY_BACKSPACE, [0x0F] = GLFW_KEY_TAB,
        [0x10] = GLFW_KEY_Q, [0x11] = GLFW_KEY_W, [0x12] = GLFW_KEY_E, [0x13] = GLFW_KEY_R,
        [0x14] = GLFW_KEY_T, [0x15] = GLFW_KEY_Y, [0x16] = GLFW_KEY_U, [0x17] = GLFW_KEY_I,
        [0x18] = GLFW_KEY_O, [0x19] = GLFW_KEY_P, [0x1A] = GLFW_KEY_LEFT_BRACKET,
        [0x1B] = GLFW_KEY_RIGHT_BRACKET, [0x1C] = GLFW_KEY_ENTER, [0x1D] = GLFW_KEY_LEFT_CONTROL,
        [0x1E] = GLFW_KEY_A, [0x1F] = GLFW_KEY_S, [0x20] = GLFW_KEY_D, [0x21] = GLFW_KEY_F,
        [0x22] = GLFW_KEY_G, [0x23] = GLFW_KEY_H, [0x24] = GLFW_KEY_J, [0x25] = GLFW_KEY_K,
        [0x26] = GLFW_KEY_L, [0x27] = GLFW_KEY_SEMICOLON, [0x28] = GLFW_KEY_APOSTROPHE,
        [0x29] = GLFW_KEY_GRAVE_ACCENT, [0x2A] = GLFW_KEY_LEFT_SHIFT, [0x2B] = GLFW_KEY_BACKSLASH,
        [0x2C] = GLFW_KEY_Z, [0x2D] = GLFW_KEY_X, [0x2E] = GLFW_KEY_C, [0x2F] = GLFW_KEY_V,
        [0x30] = GLFW_KEY_B, [0x31] = GLFW_KEY_N, [0x32] = GLFW_KEY_M, [0x33] = GLFW_KEY_COMMA,
        [0x34] = GLFW_KEY_PERIOD, [0x35] = GLFW_KEY_SLASH, [0x36] = GLFW_KEY_RIGHT_SHIFT,
        [0x37] = GLFW_KEY_KP_MULTIPLY, [0x38] = GLFW_KEY_LEFT_ALT, [0x39] = GLFW_KEY_SPACE,
        [0x3A] = GLFW_KEY_CAPS_LOCK, [0x3B] = GLFW_KEY_F1, [0x3C] = GLFW_KEY_F2, [0x3D] = GLFW_KEY_F3,
        [0x3E] = GLFW_KEY_F4, [0x3F] = GLFW_KEY_F5, [0x40] = GLFW_KEY_F6, [0x41] = GLFW_KEY_F7,
        [0x42] = GLFW_KEY_F8, [0x43] = GLFW_KEY_F9, [0x44] = GLFW_KEY_F10,
        [0x45] = GLFW_KEY_NUM_LOCK, [0x46] = GLFW_KEY_SCROLL_LOCK,
        // PeOS gives the navigation keys also without the 0xE0
        [0x47] = GLFW_KEY_HOME, [0x48] = GLFW_KEY_UP, [0x49] = GLFW_KEY_PAGE_UP,
        [0x4A] = GLFW_KEY_KP_SUBTRACT, [0x4B] = GLFW_KEY_LEFT, [0x4C] = GLFW_KEY_KP_5,
        [0x4D] = GLFW_KEY_RIGHT, [0x4E] = GLFW_KEY_KP_ADD, [0x4F] = GLFW_KEY_END,
        [0x50] = GLFW_KEY_DOWN, [0x51] = GLFW_KEY_PAGE_DOWN, [0x52] = GLFW_KEY_INSERT,
        [0x53] = GLFW_KEY_DELETE, [0x56] = GLFW_KEY_WORLD_1, [0x57] = GLFW_KEY_F11,
        [0x58] = GLFW_KEY_F12,
    };

    if (code < 0x59u)
        return plain[code] ? plain[code] : GLFW_KEY_UNKNOWN;

    switch (code)
    {
        case 0xE01C: return GLFW_KEY_KP_ENTER;
        case 0xE01D: return GLFW_KEY_RIGHT_CONTROL;
        case 0xE035: return GLFW_KEY_KP_DIVIDE;
        case 0xE038: return GLFW_KEY_RIGHT_ALT;
        case 0xE047: return GLFW_KEY_HOME;
        case 0xE048: return GLFW_KEY_UP;
        case 0xE049: return GLFW_KEY_PAGE_UP;
        case 0xE04B: return GLFW_KEY_LEFT;
        case 0xE04D: return GLFW_KEY_RIGHT;
        case 0xE04F: return GLFW_KEY_END;
        case 0xE050: return GLFW_KEY_DOWN;
        case 0xE051: return GLFW_KEY_PAGE_DOWN;
        case 0xE052: return GLFW_KEY_INSERT;
        case 0xE053: return GLFW_KEY_DELETE;
        case 0xE05B: return GLFW_KEY_LEFT_SUPER;
        case 0xE05C: return GLFW_KEY_RIGHT_SUPER;
        case 0xE05D: return GLFW_KEY_MENU;
        default:     return GLFW_KEY_UNKNOWN;
    }
}

static int modsOf(uint16_t mods)
{
    return ((mods & SLUI_MOD_SHIFT) ? GLFW_MOD_SHIFT : 0) |
           ((mods & SLUI_MOD_CTRL) ? GLFW_MOD_CONTROL : 0) |
           ((mods & SLUI_MOD_ALT) ? GLFW_MOD_ALT : 0) |
           ((mods & SLUI_MOD_SUPER) ? GLFW_MOD_SUPER : 0);
}

// ---- the window's body ----

// One SLUI frame: the pixels over the whole body once they exist.
static void sendFrame(void)
{
    _GLFWwindow* window = peos.window;
    uint32_t w = window ? (uint32_t) window->null.width : 0;
    uint32_t h = window ? (uint32_t) window->null.height : 0;

    slui_begin_frame();
    if (peos.pixelsReady && w && h)
        slui_pixels(0, 0, w, h, 0, 0);
    else
        slui_rect(0, 0, w ? w : 1, h ? h : 1, 18, 20, 30);
    slui_end_frame();
}

// Starlight said the body's size: the window, the framebuffer, the pixels.
static void layout(void)
{
    _GLFWwindow* window = peos.window;
    uint32_t w = 0, h = 0;

    slui_viewport(&w, &h);
    if (!window || !w || !h)
        return;

    if (w > peos.surfW || h > peos.surfH || !peos.pixelsReady)
    {
        if (w > peos.surfW)
            peos.surfW = w + 64;
        if (h > peos.surfH)
            peos.surfH = h + 64;
        peos.pixelsReady = 0;
        slui_pixels_open(peos.surfW, peos.surfH);
    }

    if ((int) w != window->null.width || (int) h != window->null.height)
    {
        window->null.width = (int) w;
        window->null.height = (int) h;
        if (window->context.peos.handle)
        {
            peos_gl_resize(window->context.peos.handle, (int) w, (int) h);
            window->context.peos.width = (int) w;
            window->context.peos.height = (int) h;
        }
        _glfwInputWindowSize(window, (int) w, (int) h);
        _glfwInputFramebufferSize(window, (int) w, (int) h);
    }

    sendFrame();
}

static void pointer(const slui_event_t* ev)
{
    _GLFWwindow* window = peos.window;
    const double x = (double) ev->value, y = (double) ev->value2;
    const int mods = modsOf(ev->mods);

    if (!peos.entered)
    {
        peos.entered = 1;
        _glfwInputCursorEnter(window, GLFW_TRUE);
    }

    if (ev->id == SLUI_POINTER_WHEEL)
    {
        _glfwInputScroll(window, 0.0, (double) ev->wheel);
        return;
    }

    if (x != peos.lastX || y != peos.lastY)
    {
        // The Null platform's cursor, for glfwGetCursorPos
        _glfw.null.xcursor = window->null.xpos + (int) x;
        _glfw.null.ycursor = window->null.ypos + (int) y;
        if (window->cursorMode == GLFW_CURSOR_DISABLED)
            _glfwInputCursorPos(window,
                                window->virtualCursorPosX + (x - peos.lastX),
                                window->virtualCursorPosY + (y - peos.lastY));
        else
            _glfwInputCursorPos(window, x, y);
        peos.lastX = x;
        peos.lastY = y;
    }

    // SLUI's bits (0 left, 1 right, 2 middle) are GLFW's button numbers
    const unsigned held = ev->keycode & 7u;
    const unsigned changed = held ^ peos.buttons;
    for (int b = 0;  b < 3;  b++)
    {
        if (changed & (1u << b))
            _glfwInputMouseClick(window, b, (held & (1u << b)) ? GLFW_PRESS : GLFW_RELEASE, mods);
    }
    peos.buttons = held;
}

static void handle(const slui_event_t* ev)
{
    _GLFWwindow* window = peos.window;

    switch (ev->type)
    {
        case SLUI_EVENT_RESIZE:
            layout();
            break;
        case SLUI_EVENT_PIXELS:
            peos.pixelsReady = ev->value > 0 && ev->value2 > 0;
            sendFrame();
            if (window)
                _glfwInputWindowDamage(window);
            break;
        case SLUI_EVENT_KEY_DOWN:
        case SLUI_EVENT_KEY_REPEAT:
        case SLUI_EVENT_KEY_UP:
            if (window)
            {
                const int key = keyOf(ev->keycode);
                const int action = ev->type == SLUI_EVENT_KEY_UP ? GLFW_RELEASE :
                                   ev->type == SLUI_EVENT_KEY_REPEAT ? GLFW_REPEAT : GLFW_PRESS;
                const int scancode = key != GLFW_KEY_UNKNOWN && _glfw.null.scancodes[key] != 0xFF ?
                                     _glfw.null.scancodes[key] : -1;
                _glfwInputKey(window, key, scancode, action, modsOf(ev->mods));
            }
            break;
        case SLUI_EVENT_CHAR:
            if (window && ev->value >= 32)
                _glfwInputChar(window, ev->value, modsOf(ev->mods), GLFW_TRUE);
            break;
        case SLUI_EVENT_POINTER:
            if (window)
                pointer(ev);
            break;
        case SLUI_EVENT_CLIPBOARD:
            _glfw_free(_glfw.null.clipboardString);
            _glfw.null.clipboardString = _glfw_strdup(ev->text ? ev->text : "");
            peos.clipboardHere = 1;
            break;
        default:
            break;
    }
}

static int pumpOnce(void)
{
    slui_event_t ev;
    int n = 0;

    while (slui_poll_event(&ev))
    {
        handle(&ev);
        n++;
    }
    return n;
}

void _glfwPeOSPump(double timeout)
{
    if (pumpOnce() > 0 || timeout == 0.0)
        return;

    // Nothing yet: wait on standard input, where Starlight writes
    peos_wait_item_t item = { 0, PEOS_WAIT_READ, 0 };
    const uint64_t ns = timeout < 0.0 ? PEOS_WAIT_FOREVER : (uint64_t) (timeout * 1e9);
    if (peos_wait(&item, 1, ns) > 0)
        pumpOnce();
}

void _glfwPeOSWindowCreated(_GLFWwindow* window)
{
    peos.window = window;
    peos.entered = 0;
    peos.buttons = 0;

    // Hello to Starlight: it answers with the body's size, then the pixels
    sendFrame();
    window->null.width = window->null.height = 0;
    for (int i = 0;  i < 300 && (!window->null.width || !peos.pixelsReady);  i++)
        _glfwPeOSPump(0.01);

    if (!window->null.width)
    {
        // No Starlight (started from a shell): the size asked for
        window->null.width = 640;
        window->null.height = 480;
    }
}

void _glfwPeOSWindowDestroyed(_GLFWwindow* window)
{
    if (peos.window == window)
        peos.window = NULL;
}

void _glfwPeOSSetCursorMode(_GLFWwindow* window, int mode)
{
    // Shown with NORMAL and CAPTURED (only kept inside the window): Rift
    // Ascend captures it for the whole match. Starlight's arrow, or the image
    // cursor drawn into the frame.
    peos.cursorMode = mode;
    updatePointer();
    if (mode == GLFW_CURSOR_DISABLED)
    {
        window->virtualCursorPosX = peos.lastX;
        window->virtualCursorPosY = peos.lastY;
    }
}

void _glfwPeOSSetClipboardString(const char* string)
{
    slui_clipboard_copy(string, strlen(string));
}

void _glfwPeOSGetClipboardString(void)
{
    // Starlight answers with SLUI_EVENT_CLIPBOARD: wait a little for it
    peos.clipboardHere = 0;
    slui_clipboard_request();
    const uint64_t until = peos_time_ns() + 200000000ull;
    while (!peos.clipboardHere && peos_time_ns() < until)
        _glfwPeOSPump(0.02);
}

// ---- the context: peos_gl (Mesa iris on the Intel GPU) ----

static void makeContextCurrentPeOS(_GLFWwindow* window)
{
    peos_gl_make_current(window ? window->context.peos.handle : NULL);
    _glfwPlatformSetTls(&_glfw.contextSlot, window);
}

// A swap slower than this is told on stderr (the kernel's log), split in
// its parts: Rift Ascend on the MacBook stalled ~140 ms in "swap" every
// ~1.6 s (2026-11-16) and nothing said where.
#define PEOS_SLOW_SWAP_NS 50000000ull

static void swapBuffersPeOS(_GLFWwindow* window)
{
    _GLFWcontextPeOS* c = &window->context.peos;
    const uint64_t t0 = peos_time_ns();
    uint32_t sw = 0, sh = 0, pitch = 0;
    uint32_t* px = peos.pixelsReady ? slui_pixels_buffer(&sw, &sh, &pitch) : NULL;
    const uint64_t t1 = peos_time_ns();
    uint64_t t2 = t1, t3 = t1;

    if (px && sw >= (uint32_t) c->width && sh >= (uint32_t) c->height)
    {
        peos_gl_swap(c->handle, px, (int) pitch);
        t2 = peos_time_ns();
        drawCursor(px, pitch, c->width, c->height);
        slui_pixels_damage(0, 0, (uint32_t) c->width, (uint32_t) c->height);
        t3 = peos_time_ns();
    }
    else
    {
        peos_gl_swap(c->handle, NULL, 0);   // the frame still ends
        t2 = t3 = peos_time_ns();
    }

    // The swap interval: no vertical blank to wait for; frames of 1/60 s,
    // so a game asking for 1 does not draw (and burn) faster than that
    if (c->interval > 0)
    {
        const uint64_t period = 16666667ull * (uint64_t) c->interval;
        const uint64_t now = peos_time_ns();
        if (c->lastSwapNs && now - c->lastSwapNs + 1000000ull < period)
            peos_sleep((period - (now - c->lastSwapNs)) / 1000000ull);   // milliseconds
        c->lastSwapNs = peos_time_ns();
    }

    const uint64_t t4 = peos_time_ns();
    uint64_t fence = 0, map = 0, copy = 0;
    peos_gl_swap_times(c->handle, &fence, &map, &copy);

    // Every 10 s, the whole game long: frames, the slow ones (over 33 ms
    // from one swap to the next), the worst swap and the time spent
    // waiting for the GPU (2026-11-16: "2 s fine, 2 s of lag")
    static uint64_t sum_since, sum_last, sum_frames, sum_slow, sum_worst, sum_fence;
    if (!sum_since)
        sum_since = t0;
    if (sum_last && t4 - sum_last > 33333333ull)
        sum_slow++;
    sum_last = t4;
    sum_frames++;
    sum_fence += fence;
    if (t4 - t0 > sum_worst)
        sum_worst = t4 - t0;
    if (t4 - sum_since >= 10000000000ull)
    {
        fprintf(stderr, "glfw: 10 s: %llu frames, %llu over 33 ms, worst swap %llu ms, GPU waits %llu ms in all\n",
                (unsigned long long) sum_frames, (unsigned long long) sum_slow,
                (unsigned long long) (sum_worst / 1000000ull), (unsigned long long) (sum_fence / 1000000ull));
        sum_since = t4;
        sum_frames = sum_slow = sum_worst = sum_fence = 0;
    }

    static unsigned slow_said;
    if (t4 - t0 >= PEOS_SLOW_SWAP_NS && slow_said < 60)
    {
        slow_said++;
        fprintf(stderr, "glfw: slow swap %llu ms at %llu ms: buffer %llu, gl %llu (fence %llu, map %llu, copy %llu), "
                "cursor+damage %llu, pacing %llu\n",
                (unsigned long long) ((t4 - t0) / 1000000ull), (unsigned long long) (t0 / 1000000ull),
                (unsigned long long) ((t1 - t0) / 1000000ull), (unsigned long long) ((t2 - t1) / 1000000ull),
                (unsigned long long) (fence / 1000000ull), (unsigned long long) (map / 1000000ull),
                (unsigned long long) (copy / 1000000ull), (unsigned long long) ((t3 - t2) / 1000000ull),
                (unsigned long long) ((t4 - t3) / 1000000ull));
    }
}

static void swapIntervalPeOS(int interval)
{
    _GLFWwindow* window = _glfwPlatformGetTls(&_glfw.contextSlot);
    if (window)
        window->context.peos.interval = interval;
}

static int extensionSupportedPeOS(const char* extension)
{
    (void) extension;
    return GLFW_FALSE;   // no window-system extensions (the GL ones come from GL)
}

static GLFWglproc getProcAddressPeOS(const char* procname)
{
    return (GLFWglproc) peos_gl_get_proc_address(procname);
}

static void destroyContextPeOS(_GLFWwindow* window)
{
    if (window->context.peos.handle)
    {
        peos_gl_destroy(window->context.peos.handle);
        window->context.peos.handle = NULL;
    }
}

GLFWbool _glfwCreateContextPeOS(_GLFWwindow* window,
                                const _GLFWctxconfig* ctxconfig,
                                const _GLFWfbconfig* fbconfig)
{
    unsigned flags = 0;

    if (ctxconfig->client != GLFW_OPENGL_API)
    {
        _glfwInputError(GLFW_API_UNAVAILABLE, "PeOS: OpenGL only (no OpenGL ES)");
        return GLFW_FALSE;
    }
    if (ctxconfig->share)
    {
        _glfwInputError(GLFW_VERSION_UNAVAILABLE, "PeOS: no shared contexts");
        return GLFW_FALSE;
    }

    if (ctxconfig->profile == GLFW_OPENGL_CORE_PROFILE ||
        ctxconfig->major > 3 || (ctxconfig->major == 3 && ctxconfig->minor >= 2))
        flags |= PEOS_GL_CORE;
    if (fbconfig->depthBits > 0 || fbconfig->stencilBits > 0)
        flags |= PEOS_GL_DEPTH;
    if (ctxconfig->debug)
        flags |= PEOS_GL_DEBUG;

    window->context.peos.width = window->null.width;
    window->context.peos.height = window->null.height;
    window->context.peos.handle = peos_gl_create(window->null.width, window->null.height,
                                                 ctxconfig->major, ctxconfig->minor, flags);
    if (!window->context.peos.handle)
    {
        _glfwInputError(GLFW_VERSION_UNAVAILABLE, "PeOS: %s", peos_gl_error());
        return GLFW_FALSE;
    }

    window->context.makeCurrent = makeContextCurrentPeOS;
    window->context.swapBuffers = swapBuffersPeOS;
    window->context.swapInterval = swapIntervalPeOS;
    window->context.extensionSupported = extensionSupportedPeOS;
    window->context.getProcAddress = getProcAddressPeOS;
    window->context.destroy = destroyContextPeOS;
    return GLFW_TRUE;
}

#endif // __peos__
