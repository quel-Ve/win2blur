/**
 * Acrylic Overlay — Zero-flicker frosted glass without DWM injection
 * ====================================================================
 * Inserts a WS_EX_LAYERED acrylic window directly BELOW the target in
 * z-order. DWM handles the blur natively.
 *
 * No DWM hooking, no PDB symbols, no DWMBlurGlass dependency.
 * Works on Win10 1803+ and Win11.
 *
 * Usage: acrylic_overlay.exe <hwnd_hex> [tint_hex]
 *        tint_hex: gradient color (0x00RRGGBB), default 0x00000000 (pure blur)
 *                 0x80FFFFFF = 50% white (standard acrylic)
 *                 0x00000000 = pure blur, no tint (clearest)
 */

#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <tlhelp32.h>   // watch_parent_exit: 父进程看护 (托盘强杀 → 叠加层自毁)
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
using std::min;
using std::max;

#ifndef DWMWA_NCRENDERING_POLICY
#define DWMWA_NCRENDERING_POLICY 2
#define DWMNCRP_ENABLED 2
#endif

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14   // DwmGetWindowAttribute: cloak state (Win8+)
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000
#endif

// Win10 1607+ cloaked events (apps hide windows via cloaking instead of SW_HIDE)
#ifndef EVENT_OBJECT_CLOAKED
#define EVENT_OBJECT_CLOAKED 0x8017
#define EVENT_OBJECT_UNCLOAKED 0x8018
#endif

// Undocumented SetWindowCompositionAttribute
struct AccentPolicy {
    int AccentState;
    int AccentFlags;
    unsigned int GradientColor;  // ARGB
    int AnimationId;
};
struct WinCompAttrData {
    int Attribute;       // WCA_ACCENT_POLICY = 19
    void* Data;
    int SizeOfData;
};
enum AccentState {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};

typedef BOOL (WINAPI *SetWindowCompositionAttribute_t)(HWND, WinCompAttrData*);

// Globals — loaded once, used across main loop
static SetWindowCompositionAttribute_t g_setAccent = nullptr;
static HMODULE g_dwm = nullptr;
static decltype(&DwmGetWindowAttribute) g_pDwmGetWindowAttr = nullptr;
static decltype(&DwmEnableBlurBehindWindow) g_pDwmEnableBlurBehind = nullptr;
static HWND g_target = nullptr;
static HWND g_overlay = nullptr;
static HWINEVENTHOOK g_hookObj = nullptr;  // target object lifecycle events
static bool g_running = true;
static bool g_isWin11 = false;
static bool g_overlayHidden = false;       // target hidden/minimized -> overlay hidden
static DWORD g_targetPid = 0;              // captured at start — HWND-recycling guard

// v2.8 z-order strategy (reworked 2026-08-29):
// - The poll loop re-checks the stack every 60ms and re-anchors the overlay
//   below the target ONLY when the guard finds drift. This repairs any
//   z-order change (apps self-raising without activation, DWM churn,
//   interleaved windows) within one repair beat, and it replaced the old
//   EVENT_SYSTEM_FOREGROUND anchoring, which both missed non-activation
//   drift and re-anchored mid-animation on every focus switch (the 1-frame
//   mask loss). The idempotence guard keeps a healthy stack free of churn.
// - Target lifecycle handled event-driven (HIDE/SHOW/DESTROY/CLOAKED) with a
//   poll fallback, plus PID verification against HWND recycling.
static DWORD g_lastZcheck = 0;
#define ZORDER_CHECK_MS 60

RECT get_frame(HWND hwnd);  // forward decl (defined later; used by scan_lyric_lines)
static bool target_shell_hidden();  // forward decl (defined after is_valid_frame)

// ---- Lyric mode (cloudmusic DesktopLyrics) — diff-based text tracking ----
// Usage: acrylic_overlay.exe --lyric <hwnd_hex> <tint_hex> <corner> <padding>
// Places a rounded-corner frosted panel BEHIND the target, sized to the
// lyric text bounding box + padding. The target is a per-pixel-alpha window
// over an arbitrary desktop, and PrintWindow returns an opaque blank, so the
// text must be read from the composed screen.
//
// Why frame DIFFS, not a color test (2026-08-29 rewrite): a color test cannot
// work — a bright-pink wallpaper passes every "red text" filter (verified
// live: the detector saw one giant blob spanning the window), and the karaoke
// fill makes the text two-toned. But inside the lyric window the only pixels
// that ever CHANGE are the lyrics themselves: karaoke sweep, line scroll,
// line swap, panel-freeing fades. A frame diff therefore isolates the text
// rows regardless of wallpaper, skin and colors. Boxes never expire on their
// own (a waiting line sits still for minutes), they are replaced only when
// new activity claims their rows, and the whole panel set dies with the
// overlay when the window hides.
static bool g_lyricMode = false;
static int  g_cornerRadius = 6;
static int  g_padding = 2;
static DWORD g_lastScan = 0;
#define MAX_LYR_LINES 4
static HWND g_lyrPanels[MAX_LYR_LINES] = {};  // lyric panels; [0] aliases g_overlay

#define LYR_SCAN_MS      300    // diff cadence
#define LYR_DIFF_THRESH  45     // per-pixel channel-sum delta = "changed"
#define LYR_ROW_MIN      3      // changed px to call a row active
#define LYR_BAND_GAP     8      // merge row bands separated by less
#define LYR_MIN_BAND_H   14     // a glyph line is at least this tall
#define LYR_NEW_BAND_PX  40     // changed px to found a NEW box
#define LYR_MATCH_OVERLAP 40    // band/box row-overlap % to call it a match
#define LYR_HYST         3      // panel must move/resize by more than this (px)
#define LYRQ2(v) ((v) & ~1)     // quantize to 2px grid (AA jitter absorber)

struct LyrBox { RECT r; unsigned bandPx; int hits; };
static LyrBox g_boxes[MAX_LYR_LINES] = {};
static int g_boxCount = 0;
// Rects where OUR panel motion explains screen diffs; ignored by the next
// two scans (panel edges move under DWM for a frame or two).
static RECT g_suppress[4 * MAX_LYR_LINES];
static int g_suppressCount = 0;
static int g_suppressScans = 0;
static RECT g_lastBoxes[MAX_LYR_LINES];     // applied boxes, window coords
static int g_lastBoxCount = 0;              // for drag reposition between scans

static void suppress_rect(const RECT& r) {
    if (g_suppressCount < (int)(sizeof(g_suppress) / sizeof(g_suppress[0])))
        g_suppress[g_suppressCount++] = r;
}

// Grab the target's on-screen rectangle into a 32bpp top-down buffer.
static bool capture_region(HWND target, std::vector<unsigned>& px, int& w, int& h, RECT& f) {
    f = get_frame(target);
    w = f.right - f.left; h = f.bottom - f.top;
    if (w <= 0 || h <= 0 || w > 4000 || h > 2000) return false;
    HDC dc = GetDC(nullptr);
    if (!dc) return false;
    HDC mdc = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    if (!mdc || !bmp) { if (mdc) DeleteDC(mdc); ReleaseDC(nullptr, dc); return false; }
    HGDIOBJ oldBmp = SelectObject(mdc, bmp);
    BOOL ok = BitBlt(mdc, 0, 0, w, h, dc, f.left, f.top, SRCCOPY | CAPTUREBLT);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    px.assign((size_t)w * h, 0);
    int got = ok ? GetDIBits(mdc, bmp, 0, h, px.data(), &bmi, DIB_RGB_COLORS) : 0;
    SelectObject(mdc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(nullptr, dc);
    return got != 0;
}

// Diff a tight capture pair; emit per-lyric-line boxes in WINDOW coordinates.
void scan_lyric_lines(HWND target, std::vector<RECT>& lines) {
    lines.clear();
    RECT f;
    int w = 0, h = 0;
    std::vector<unsigned> ca, cb;
    if (!capture_region(target, ca, w, h, f)) return;
    // Short diff window: a karaoke tick still lights up hundreds of pixels,
    // while a smooth line scroll only smears its band by a few px (a long
    // window would span the whole scroll path and fatten the box).
    Sleep(70);
    if (!capture_region(target, cb, w, h, f)) return;

    // Suppression rects in window coords (our own panel motion)
    RECT sup[4 * MAX_LYR_LINES];
    int supN = 0;
    for (int i = 0; i < g_suppressCount; i++) {
        RECT s = g_suppress[i];
        sup[supN++] = RECT{ s.left - f.left, s.top - f.top, s.right - f.left, s.bottom - f.top };
    }

    // Per-row changed-pixel stats
    std::vector<int> rowCnt(h, 0), rowMinX(h, w), rowMaxX(h, -1);
    for (int y = 0; y < h; y++) {
        const unsigned* a = &ca[(size_t)y * w];
        const unsigned* b = &cb[(size_t)y * w];
        for (int x = 0; x < w; x++) {
            unsigned pa = a[x], pb = b[x];
            int d = abs((int)((pa >> 16) & 255) - (int)((pb >> 16) & 255))
                  + abs((int)((pa >> 8) & 255) - (int)((pb >> 8) & 255))
                  + abs((int)(pa & 255) - (int)(pb & 255));
            if (d <= LYR_DIFF_THRESH) continue;
            bool blocked = false;
            for (int i = 0; i < supN && !blocked; i++)
                blocked = (x >= sup[i].left - 2 && x < sup[i].right + 2 &&
                           y >= sup[i].top - 2 && y < sup[i].bottom + 2);
            if (blocked) continue;
            rowCnt[y]++;
            if (x < rowMinX[y]) rowMinX[y] = x;
            if (x > rowMaxX[y]) rowMaxX[y] = x;
        }
    }
    if (g_suppressScans > 0 && --g_suppressScans == 0) g_suppressCount = 0;

    // Row bands = candidate text lines (quiet gaps up to LYR_BAND_GAP rows
    // are absorbed — glyph interiors and AA can dip below the row threshold)
    struct Band { int top, bottom, px, minX, maxX; };
    std::vector<Band> bands;
    {
        int ys = -1, tot = 0, mnX = w, mxX = -1;
        int y = 0;
        while (y < h) {
            if (rowCnt[y] >= LYR_ROW_MIN) {
                if (ys < 0) { ys = y; tot = 0; mnX = w; mxX = -1; }
                tot += rowCnt[y];
                if (rowMinX[y] < mnX) mnX = rowMinX[y];
                if (rowMaxX[y] > mxX) mxX = rowMaxX[y];
                y++;
            } else if (ys >= 0) {
                int z = y;
                while (z < h && rowCnt[z] < LYR_ROW_MIN) z++;
                if (z < h && z - y <= LYR_BAND_GAP) {
                    y = z;   // absorb the quiet gap, band continues
                } else {
                    if (y - ys >= LYR_MIN_BAND_H) bands.push_back({ ys, y, tot, mnX, mxX });
                    ys = -1;
                    y = z;
                }
            } else {
                y++;
            }
        }
        if (ys >= 0 && h - ys >= LYR_MIN_BAND_H) bands.push_back({ ys, h, tot, mnX, mxX });
    }

    // Match bands to boxes. Boxes persist (no timeout — a waiting line sits
    // still for minutes) and grow along the evidence; union is safe here
    // because the 70ms diff window keeps bands tight (no scroll-path smears),
    // and panel/self-motion diffs are suppressed. A brand-new box needs
    // strong evidence or two hits before it earns a panel, so single-frame
    // glitches from a blinking background never materialize as panels.
    for (auto& bd : bands) {
        RECT br{ bd.minX, bd.top, bd.maxX + 1, bd.bottom };
        int best = -1, bestOv = 0;
        for (int i = 0; i < g_boxCount; i++) {
            int ov = min(g_boxes[i].r.bottom, br.bottom) - max(g_boxes[i].r.top, br.top);
            if (ov > bestOv) { bestOv = ov; best = i; }
        }
        if (best >= 0 && bestOv * 100 >= LYR_MATCH_OVERLAP * min(br.bottom - br.top,
                                                                  g_boxes[best].r.bottom - g_boxes[best].r.top)) {
            RECT& r = g_boxes[best].r;
            bool disjoint = br.right + LYR_HYST < r.left || br.left > r.right + LYR_HYST;
            if (disjoint) r = br;   // same rows, different text — replace
            else {
                r.left = min(r.left, br.left);    r.top = min(r.top, br.top);
                r.right = max(r.right, br.right); r.bottom = max(r.bottom, br.bottom);
            }
            g_boxes[best].bandPx = bd.px;
            g_boxes[best].hits++;
        } else if (bd.px >= LYR_NEW_BAND_PX) {
            int slot = g_boxCount;
            if (slot >= MAX_LYR_LINES) {   // recycle the weakest-evidence box
                slot = 0;
                for (int i = 1; i < g_boxCount; i++)
                    if (g_boxes[i].bandPx < g_boxes[slot].bandPx) slot = i;
            } else g_boxCount++;
            // A big band (full line scrolled in) is proof on its own; weaker
            // bands need a second confirming scan before showing a panel.
            int hits = bd.px >= 300 ? 2 : 1;
            g_boxes[slot] = { br, bd.px, hits };
        }
    }

    // Emit sorted by top, only boxes confirmed at least twice
    for (int i = 0; i < g_boxCount; i++)
        for (int j = i + 1; j < g_boxCount; j++)
            if (g_boxes[j].r.top < g_boxes[i].r.top) std::swap(g_boxes[i], g_boxes[j]);
    for (int i = 0; i < g_boxCount; i++) {
        if (g_boxes[i].r.right <= g_boxes[i].r.left || g_boxes[i].hits < 2) continue;
        lines.push_back(g_boxes[i].r);
        if ((int)lines.size() >= MAX_LYR_LINES) break;
    }
}

// Size/round per-line panels to hug each detected text line. Panel i covers
// line i; extra panels are hidden; no lines = all hidden (no text = no blur).
// Screen coords = frame + box - pad, quantized to the 2px grid, with a small
// hysteresis so sub-3px detection jitter never moves a panel (that churn is
// what read as "panels reshuffling every 0.5s").
void set_lyric_panels(const RECT& frame, const std::vector<RECT>& lines) {
    if (!g_lyricMode) return;
    g_lastBoxCount = 0;
    for (int i = 0; i < (int)lines.size() && i < MAX_LYR_LINES; i++) g_lastBoxes[i] = lines[i];
    g_lastBoxCount = (int)lines.size();
    if (g_overlayHidden) {
        for (int i = 0; i < MAX_LYR_LINES; i++)
            if (g_lyrPanels[i] && IsWindow(g_lyrPanels[i]) && IsWindowVisible(g_lyrPanels[i]))
                ShowWindow(g_lyrPanels[i], SW_HIDE);
        return;
    }
    int n = (int)lines.size();
    for (int i = 0; i < MAX_LYR_LINES; i++) {
        HWND p = g_lyrPanels[i];
        if (!p || !IsWindow(p)) continue;
        if (i < n) {
            const RECT& bb = lines[i];
            if (bb.right <= bb.left || bb.bottom <= bb.top) {
                if (IsWindowVisible(p)) ShowWindow(p, SW_HIDE);
                continue;
            }
            int x = LYRQ2(frame.left + bb.left - g_padding);
            int y = LYRQ2(frame.top  + bb.top  - g_padding);
            int w = LYRQ2((bb.right - bb.left) + 2 * g_padding);
            int hh = LYRQ2((bb.bottom - bb.top) + 2 * g_padding);
            if (w <= 0 || hh <= 0) { if (IsWindowVisible(p)) ShowWindow(p, SW_HIDE); continue; }
            RECT cur = {};
            GetWindowRect(p, &cur);
            bool moved = abs(cur.left - x) > LYR_HYST || abs(cur.top - y) > LYR_HYST ||
                         abs((cur.right - cur.left) - w) > LYR_HYST ||
                         abs((cur.bottom - cur.top) - hh) > LYR_HYST;
            if (moved) {
                suppress_rect(cur);
                suppress_rect(RECT{ x, y, x + w, y + hh });
                g_suppressScans = 2;
                SetWindowPos(p, g_target, x, y, w, hh,
                    SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOZORDER);
                HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, hh + 1,
                    g_cornerRadius * 2, g_cornerRadius * 2);
                SetWindowRgn(p, rgn, TRUE);
            }
            if (!IsWindowVisible(p)) {
                suppress_rect(RECT{ x, y, x + w, y + hh });
                g_suppressScans = 2;
                ShowWindow(p, SW_SHOWNOACTIVATE);
                SetWindowPos(p, g_target, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW | SWP_NOACTIVATE);
            }
        } else {
            if (IsWindowVisible(p)) {
                RECT cur = {};
                GetWindowRect(p, &cur);
                suppress_rect(cur);
                g_suppressScans = 2;
                ShowWindow(p, SW_HIDE);
            }
        }
    }
}

// 父进程看护 (2026-08-12): 托盘被强杀 (taskkill /f) 时无法执行 WM_DESTROY 的
// 子进程清理 → 叠加层变孤儿永久残留 (16 个堆积事故的根因)。独立线程等父进程
// 句柄, 父死即 g_running=false, 叠加层自行退出销毁。
static void watch_parent_exit() {
    DWORD parent = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {sizeof(pe)};
        if (Process32FirstW(snap, &pe)) {
            do {
                if (pe.th32ProcessID == GetCurrentProcessId()) {
                    parent = pe.th32ParentProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    if (!parent) return;
    HANDLE hp = OpenProcess(SYNCHRONIZE, FALSE, parent);
    if (!hp) return;
    WaitForSingleObject(hp, INFINITE);   // 父进程死亡 (含强杀) → 信号
    CloseHandle(hp);
    g_running = false;
}

// ---- Helpers ----

bool detect_win11() {
    // RtlGetVersion is not affected by manifest compatibility shims
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    auto RtlGetVersion = (LONG (WINAPI*)(PRTL_OSVERSIONINFOW))
        GetProcAddress(ntdll, "RtlGetVersion");
    if (!RtlGetVersion) return false;
    RTL_OSVERSIONINFOW vi = {sizeof(vi)};
    if (RtlGetVersion(&vi) != 0) return false;
    return vi.dwBuildNumber >= 22000;  // Win11 starts at build 22000
}

// Walk from `from` toward the top of the z-order, skipping invisible windows
// (each thread's Default IME sits invisibly between a window and the one
// below it). Returns the first VISIBLE window above `from`, or null.
static HWND first_visible_above(HWND from) {
    HWND cur = from;
    for (int i = 0; i < 8 && cur; i++) {
        cur = GetWindow(cur, GW_HWNDPREV);
        if (!cur) break;
        if (IsWindowVisible(cur)) return cur;
    }
    return nullptr;
}

void do_set_window_pos() {
    if (g_lyricMode) {
        // If the first visible window directly below the target is already one
        // of our visible panels, the stack is intact — skip (SetWindowPos
        // churn re-renders the accent surface at full tint = visible "dark
        // pulse"). Otherwise repair by stacking every visible panel directly
        // below the target.
        HWND below = GetWindow(g_target, GW_HWNDNEXT);
        for (int i = 0; i < 8 && below; i++) {
            if (IsWindowVisible(below)) break;
            below = GetWindow(below, GW_HWNDNEXT);
        }
        for (int i = 0; i < MAX_LYR_LINES; i++)
            if (g_lyrPanels[i] && IsWindowVisible(g_lyrPanels[i]) && g_lyrPanels[i] == below) return;
        for (int i = 0; i < MAX_LYR_LINES; i++) {
            HWND p = g_lyrPanels[i];
            if (p && IsWindow(p) && IsWindowVisible(p))
                SetWindowPos(p, g_target, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW | SWP_NOACTIVATE);
        }
        return;
    }
    // Idempotence guard: if no VISIBLE window sits between the target and the
    // overlay, the stack is intact — skip entirely. Every SetWindowPos forces
    // DWM to re-composite the accent surface, whose first frame renders the
    // tint at full strength (visible "dark pulse"). Invisible windows (Default
    // IME) between them are normal and must not defeat the guard, else we
    // churn a SetWindowPos every 60ms forever.
    if (first_visible_above(g_overlay) == g_target) return;
    SetWindowPos(g_overlay, g_target, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW | SWP_NOACTIVATE);  // synchronous
}

void hide_overlay() {
    if (g_overlayHidden) return;
    g_overlayHidden = true;
    if (g_lyricMode) {
        for (int i = 0; i < MAX_LYR_LINES; i++)
            if (g_lyrPanels[i] && IsWindow(g_lyrPanels[i]) && IsWindowVisible(g_lyrPanels[i]))
                ShowWindow(g_lyrPanels[i], SW_HIDE);
        return;
    }
    if (g_overlay && IsWindow(g_overlay)) ShowWindow(g_overlay, SW_HIDE);
}
void show_overlay() {
    if (!g_overlayHidden) return;
    g_overlayHidden = false;
    if (g_lyricMode) return;   // panels reappear on the next scan (<=300ms)
    if (g_overlay && IsWindow(g_overlay)) {
        ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
        do_set_window_pos();
    }
}

void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                           LONG, LONG, DWORD, DWORD) {
    if (!g_overlay) return;
    // Z-order is NOT anchored here anymore: the poll-level guarded check
    // (every 60ms) repairs drift from any cause, and anchoring on focus
    // switches re-anchored mid-DWM-animation — the 1-frame mask loss.
    if (hwnd == g_target) {
        switch (event) {
        case EVENT_OBJECT_DESTROY: g_running = false; break;
        case EVENT_OBJECT_HIDE: hide_overlay(); break;
        case EVENT_OBJECT_CLOAKED:
            // Hide for minimized and SHELL/INHERITED cloaks (dismissed views);
            // ignore APP cloak (occlusion) — see target_shell_hidden().
            if (target_shell_hidden()) hide_overlay();
            break;
        case EVENT_OBJECT_SHOW:
        case EVENT_OBJECT_UNCLOAKED:
            // Guard: a SHOW while still shell-hidden (spurious or re-cloaked)
            // must not un-hide the overlay ahead of the 5ms poll.
            if (IsWindow(g_target) && !target_shell_hidden()) show_overlay();
            break;
        }
    }
}

void apply_accent(HWND hwnd, int state, unsigned int tint = 0x00000000) {
    if (!g_setAccent) {
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        g_setAccent = (SetWindowCompositionAttribute_t)
            GetProcAddress(u32, "SetWindowCompositionAttribute");
        if (!g_setAccent) return;
    }
    // Acrylic renders NOTHING at gradient alpha 0 (alpha = effect opacity).
    // Floor at v2.7's 0x1A (10% black) so blur is always visible, no matter
    // who launches us (tray slider, session restore, manual test).
    if (state == ACCENT_ENABLE_ACRYLICBLURBEHIND && ((tint >> 24) & 0xFF) == 0)
        tint = 0x1A000000;
    AccentPolicy policy = {};
    policy.AccentState = state;
    policy.GradientColor = tint;
    WinCompAttrData data = {19, &policy, sizeof(policy)};  // 19 = WCA_ACCENT_POLICY
    g_setAccent(hwnd, &data);
}

void disable_blur_behind(HWND hwnd) {
    if (!g_pDwmEnableBlurBehind) return;
    DWM_BLURBEHIND bb = {};
    bb.dwFlags = DWM_BB_ENABLE;
    bb.fEnable = FALSE;
    g_pDwmEnableBlurBehind(hwnd, &bb);
}

RECT get_frame(HWND hwnd) {
    RECT r = {};
    if (!GetWindowRect(hwnd, &r)) {
        // Window may be in a transitional state — return empty rect
        return r;
    }

    // Try DwmGetWindowAttribute for extended frame bounds (more accurate)
    if (g_pDwmGetWindowAttr) {
        RECT f = {};
        HRESULT hr = g_pDwmGetWindowAttr(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                          &f, sizeof(f));
        if (SUCCEEDED(hr) && f.right > f.left && f.bottom > f.top) {
            r = f;
        }
    }
    return r;
}

bool is_valid_frame(const RECT& r) {
    LONG w = r.right - r.left;
    LONG h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return false;
    // Sanity check: overlay shouldn't exceed screen bounds unreasonably
    LONG sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    LONG sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (w > sw * 2 || h > sh * 2) return false;
    return true;
}

// True when the target is invisible on screen even though WS_VISIBLE stays
// set. Three distinct cases:
//   - hidden / minimized: classic checks (IsWindowVisible / IsIconic);
//   - SHELL/INHERITED cloak (2/4): the shell or the app dismissed the window
//     (UWP views "close" this way — e.g. the Sticky Notes list that flashes at
//     startup, then cloaks instead of being destroyed). The screen region is
//     NOT occluded, so an overlay left shown there is a permanent ownerless
//     blur patch; we must hide.
//   - APP cloak (1) is deliberately NOT hidden: that is DWM's occlusion
//     optimization for fully covered windows. The overlay sits right below
//     the target, so it is occluded too — hiding it would buy nothing visible
//     but reintroduces the 1-2 frame mask gap on uncover (fixed in d9e2ca6).
static bool target_shell_hidden() {
    if (!IsWindow(g_target)) return true;
    if (!IsWindowVisible(g_target) || IsIconic(g_target)) return true;
    if (g_pDwmGetWindowAttr) {
        int cloaked = 0;
        if (SUCCEEDED(g_pDwmGetWindowAttr(g_target, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
            && (cloaked == 2 || cloaked == 4))   // DWM_CLOAKED_SHELL / _INHERITED
            return true;
    }
    return false;
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST:
        // Click-through: never intercept mouse input. If the overlay ever ends
        // up above the target, clicks still reach the target -> it activates ->
        // the foreground event repairs the z-order. Without this, a stuck
        // overlay swallows every click and the repair can never trigger
        // ("blur stuck on top" deadlock, only fixable by minimize+restore).
        return HTTRANSPARENT;
    case WM_CLOSE:
        g_running = false;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- Main ----

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: acrylic_overlay.exe <hwnd_hex> [tint_hex]\n");
        fprintf(stderr, "       acrylic_overlay.exe --lyric <hwnd_hex> <tint_hex> <corner> <padding>\n");
        fprintf(stderr, "  tint: 0x00000000 = pure blur  0x80FFFFFF = 50%% white\n");
        return 1;
    }
    g_lyricMode = (argc >= 2 && strcmp(argv[1], "--lyric") == 0);
    int ai = g_lyricMode ? 2 : 1;
    g_target = (HWND)(ULONG_PTR)strtoull(argv[ai], nullptr, 16);
    if (!IsWindow(g_target)) {
        fprintf(stderr, "[x] Invalid HWND\n");
        return 1;
    }
    GetWindowThreadProcessId(g_target, &g_targetPid);   // HWND-recycling guard
    unsigned int tint = 0x00000000;  // default: pure blur, no tint
    if (argc >= ai + 1) {
        tint = (unsigned int)strtoull(argv[ai + 1], nullptr, 16);
    }
    if (g_lyricMode) {
        g_cornerRadius = argc >= ai + 2 ? atoi(argv[ai + 2]) : 14;
        g_padding      = argc >= ai + 3 ? atoi(argv[ai + 3]) : 20;
    }

    // ---- Load DLLs once ----
    g_dwm = LoadLibraryW(L"dwmapi.dll");
    if (g_dwm) {
        g_pDwmGetWindowAttr = (decltype(&DwmGetWindowAttribute))
            GetProcAddress(g_dwm, "DwmGetWindowAttribute");
        g_pDwmEnableBlurBehind = (decltype(&DwmEnableBlurBehindWindow))
            GetProcAddress(g_dwm, "DwmEnableBlurBehindWindow");
    }
    g_isWin11 = detect_win11();

    // ---- Create overlay window ----
    const wchar_t* CN = L"AcrylicOverlayClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = CN;
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);  // Don't paint!
    RegisterClassW(&wc);

    RECT r = get_frame(g_target);
    if (!is_valid_frame(r)) {
        // Fallback: try GetWindowRect directly
        if (GetWindowRect(g_target, &r) && !is_valid_frame(r)) {
            fprintf(stderr, "[x] Cannot determine target window frame\n");
            if (g_dwm) FreeLibrary(g_dwm);
            return 1;
        }
    }

    // Lyric mode: pre-detect the text lines so panels are placed immediately
    // (never flashed full-size) and created hidden, sized per-line below.
    std::vector<RECT> lyLines;
    if (g_lyricMode) scan_lyric_lines(g_target, lyLines);

    LONG ow = r.right - r.left, oh = r.bottom - r.top, ox = r.left, oy = r.top;
    if (g_lyricMode) { ow = 1; oh = 1; }   // panel[0] sized per-line by set_lyric_panels
    // Lyric panels deliberately omit WS_EX_LAYERED: they never use layered
    // attributes, and SetWindowRgn (rounded corners) is ignored on layered
    // windows — with the flag the panels render with square corners.
    //
    // WS_EX_TRANSPARENT is load-bearing, not cosmetic: it is the only click
    // guarantee that survives a cross-process target. WM_NCHITTEST ->
    // HTTRANSPARENT (see OverlayWndProc) is documented to hand the hit to
    // underlying windows *in the same thread* only, so when the overlay ends
    // up above a target owned by another process — the target is topmost, or
    // the 60ms z-order repair loses a race against a freshly created window —
    // the overlay swallows every click. The window can then never be
    // activated, so keyboard looks dead too and the app reads as "frozen"
    // while its content stays visible. (v2.7 fixed this with exactly this
    // flag — docs/crisp-acrylic-dilemma.md; the v2.8 rewrite dropped it in
    // favour of WM_NCHITTEST alone and regressed.)
    g_overlay = CreateWindowExW(
        (g_lyricMode ? (DWORD)(WS_EX_TOPMOST) : (DWORD)(WS_EX_LAYERED))
            | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
            | WS_EX_TRANSPARENT,
        CN, L"", WS_POPUP,
        ox, oy, ow, oh,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );

    if (!g_overlay) {
        fprintf(stderr, "[x] Failed to create overlay (err=%lu)\n", GetLastError());
        if (g_dwm) FreeLibrary(g_dwm);
        return 1;
    }

    // ---- Apply blur to a panel ----
    // 1. Enable DWM frame extension into client area
    // 2. On Win11, ensure the window does NOT use redirection surface
    //    (WS_EX_NOREDIRECTIONBITMAP alone may be insufficient on Win11 24H2+)
    // 3. Apply acrylic blur via undocumented API
    auto setup_blur = [&](HWND p) {
        int policy = DWMNCRP_ENABLED;
        DwmSetWindowAttribute(p, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
        if (g_isWin11 && g_pDwmEnableBlurBehind) {
            // Explicitly disable blur-behind first (prevents DWM from
            // applying blur to the entire redirection surface)
            disable_blur_behind(p);
        }
        apply_accent(p, ACCENT_ENABLE_ACRYLICBLURBEHIND, tint);
    };
    setup_blur(g_overlay);

    if (g_lyricMode) {
        // Multi-line lyric panels: up to MAX_LYR_LINES, all created hidden and
        // sized/positioned by set_lyric_panels. panel[0] aliases g_overlay.
        g_lyrPanels[0] = g_overlay;
        for (int i = 1; i < MAX_LYR_LINES; i++) {
            HWND p = CreateWindowExW(
                WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW
                    | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
                CN, L"", WS_POPUP,
                0, 0, 1, 1,
                nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
            if (p) { setup_blur(p); g_lyrPanels[i] = p; }
        }
    }

    // 4. Position overlay BELOW target in z-order (non-lyric: show now;
    //    lyric panels are shown by set_lyric_panels as their lines are found)
    if (!g_lyricMode) {
        SetWindowPos(g_overlay, g_target, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    // Lyric mode: place the pre-detected panels immediately (no full-frame flash).
    if (g_lyricMode) {
        set_lyric_panels(r, lyLines);
        g_lastScan = GetTickCount() + 500;
    }

    wchar_t title[256];
    GetWindowTextW(g_target, title, 256);
    wprintf(L"[*] Acrylic Overlay active\n");
    wprintf(L"    Target: %s\n", title);
    if (g_lyricMode)
        wprintf(L"    Lyric panels: %d max, pre-detected lines: %zu, pad=%d corner=%d\n",
            MAX_LYR_LINES, lyLines.size(), g_padding, g_cornerRadius);
    else
        wprintf(L"    Overlay: %dx%d at (%d,%d)\n", ow, oh, r.left, r.top);
    wprintf(L"    OS: %s\n", g_isWin11 ? L"Windows 11" : L"Windows 10");
    wprintf(L"    Tint: 0x%08X\n", tint);
    fflush(stdout);

    // WinEvent hook (v2.8): object range — instant lifecycle response
    // (hide/show/destroy/cloak). Z-order is maintained by the poll loop.
    g_hookObj = SetWinEventHook(
        EVENT_OBJECT_HIDE, EVENT_OBJECT_UNCLOAKED,
        nullptr, WinEventProc, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    // 父进程看护：托盘被强杀时叠加层自毁，避免孤儿堆积
    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        watch_parent_exit(); return 0;
    }, nullptr, 0, nullptr);

    // ---- Main loop ----
    MSG msg;
    RECT lastRect = r;
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running) break;

        // Lifecycle (events above are primary; poll catches missed ones and
        // keeps hide/show consistent: a shell-cloaked or iconic target must
        // not get its overlay re-shown here — that race is what left the
        // ownerless blur patch behind dismissed UWP windows).
        if (!IsWindow(g_target)) {
            wprintf(L"[!] Target destroyed, exiting\n");
            break;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(g_target, &pid);
        if (pid != g_targetPid) {
            wprintf(L"[!] Target HWND recycled, exiting\n");
            break;
        }
        if (target_shell_hidden()) {
            if (g_lyricMode) {
                // Lyric panel is only meaningful while lyrics are shown. Exit
                // (don't hide) so no orphaned panel lingers when cloudmusic
                // toggles desktop lyrics off; the tray re-launches on re-show.
                wprintf(L"[!] Lyric target hidden, exiting\n");
                break;
            }
            hide_overlay();   // hidden / minimized / shell-cloaked (dismissed)
        } else {
            show_overlay();
            // Guarded z-order re-check on a steady beat: repairs drift from
            // any cause (self-raising apps, DWM churn, interleaved windows)
            // within one 60ms beat; the guard makes a healthy stack free.
            DWORD now = GetTickCount();
            if (now - g_lastZcheck >= ZORDER_CHECK_MS) {
                g_lastZcheck = now;
                do_set_window_pos();
            }
            r = get_frame(g_target);
            if (is_valid_frame(r) &&
                (r.left != lastRect.left || r.top != lastRect.top ||
                 r.right != lastRect.right || r.bottom != lastRect.bottom)) {
                lastRect = r;
                if (!g_lyricMode) {
                    SetWindowPos(g_overlay, g_target,
                        r.left, r.top, r.right - r.left, r.bottom - r.top,
                        SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOZORDER);
                } else if (g_lastBoxCount) {
                    // Drag/move between scans: reposition panels from the last
                    // known boxes so they track the window at frame rate.
                    std::vector<RECT> keep(g_lastBoxes, g_lastBoxes + g_lastBoxCount);
                    set_lyric_panels(r, keep);
                }
            }
            if (g_lyricMode && GetTickCount() >= g_lastScan) {
                g_lastScan = GetTickCount() + LYR_SCAN_MS;   // diff cadence
                std::vector<RECT> lines;
                scan_lyric_lines(g_target, lines);
                set_lyric_panels(r, lines);
                // No lines (no activity yet) -> panels keep their last boxes;
                // they only change when new text activity claims the rows.
            }
        }

        Sleep(5);  // ~180 fps — match 180Hz display
    }

    // ---- Cleanup ----
    if (g_hookObj) UnhookWinEvent(g_hookObj);

    // Disable all blur state before destroying windows (critical for Win11)
    int panelCount = g_lyricMode ? MAX_LYR_LINES : 1;
    for (int i = 0; i < panelCount; i++) {
        HWND p = (i == 0) ? g_overlay : g_lyrPanels[i];
        if (!p || !IsWindow(p)) continue;
        apply_accent(p, ACCENT_DISABLED);
        if (g_pDwmEnableBlurBehind) disable_blur_behind(p);
    }
    // Let DWM finish processing the state changes
    DwmFlush();
    Sleep(50);

    for (int i = 0; i < panelCount; i++) {
        HWND p = (i == 0) ? g_overlay : g_lyrPanels[i];
        if (p && IsWindow(p)) DestroyWindow(p);
    }
    if (g_dwm) FreeLibrary(g_dwm);
    wprintf(L"[*] Overlay destroyed\n");
    return 0;
}
