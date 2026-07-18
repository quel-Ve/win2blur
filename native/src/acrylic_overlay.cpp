/**
 * Acrylic Overlay — Zero-flicker frosted glass without DWM injection
 * ====================================================================
 * Inserts a WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP acrylic window
 * directly BELOW the target in z-order. DWM handles the blur natively.
 *
 * No DWM hooking, no PDB symbols, no DWMBlurGlass dependency.
 * Works on Win10 1803+.
 *
 * Usage: acrylic_overlay.exe <hwnd_hex> [tint_hex]
 *        tint_hex: gradient color (0x00RRGGBB), default 0x00000000 (pure blur)
 *                 0x80FFFFFF = 50% white (standard acrylic)
 *                 0x00000000 = pure blur, no tint (clearest)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <cstdio>
#include <cstdlib>

#ifndef DWMWA_NCRENDERING_POLICY
#define DWMWA_NCRENDERING_POLICY 2
#define DWMNCRP_ENABLED 2
#endif

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000
#endif

// Undocumented SetWindowCompositionAttribute (same as blur.py uses)
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

static SetWindowCompositionAttribute_t g_setAccent = nullptr;
static HWND g_target = nullptr;
static HWND g_overlay = nullptr;
static HWINEVENTHOOK g_hook = nullptr;

// WinEvent callback: refresh z-order when target regains focus
void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                           LONG, LONG, DWORD, DWORD) {
    if (event == EVENT_SYSTEM_FOREGROUND && hwnd == g_target && g_overlay) {
        SetWindowPos(g_overlay, g_target, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    }
}

void apply_accent(HWND hwnd, int state, unsigned int tint = 0x00000000) {
    if (!g_setAccent) {
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        g_setAccent = (SetWindowCompositionAttribute_t)GetProcAddress(u32, "SetWindowCompositionAttribute");
        if (!g_setAccent) return;
    }
    AccentPolicy policy = {};
    policy.AccentState = state;
    policy.GradientColor = tint;
    WinCompAttrData data = {19, &policy, sizeof(policy)};  // 19 = WCA_ACCENT_POLICY
    g_setAccent(hwnd, &data);
}

RECT get_frame(HWND hwnd) {
    RECT r = {};
    GetWindowRect(hwnd, &r);
    // Try DwmGetWindowAttribute for real frame bounds
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
        auto fn = (HRESULT(WINAPI*)(HWND,DWORD,PVOID,DWORD))GetProcAddress(dwm, "DwmGetWindowAttribute");
        if (fn) {
            RECT f = {};
            if (SUCCEEDED(fn(hwnd, 9, &f, sizeof(f))) && f.right > f.left)
                r = f;
        }
        FreeLibrary(dwm);
    }
    return r;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: acrylic_overlay.exe <hwnd_hex> [tint_hex]\n");
        fprintf(stderr, "  tint: 0x00000000 = pure blur  0x80FFFFFF = 50%% white\n");
        return 1;
    }
    g_target = (HWND)(ULONG_PTR)strtoull(argv[1], nullptr, 16);
    if (!IsWindow(g_target)) {
        fprintf(stderr, "[x] Invalid HWND\n");
        return 1;
    }
    unsigned int tint = 0x00000000;  // default: pure blur, no tint
    if (argc >= 3) {
        tint = (unsigned int)strtoull(argv[2], nullptr, 16);
    }

    // Create overlay window
    const wchar_t* CN = L"AcrylicOverlayClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = CN;
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);  // Don't paint!
    RegisterClassW(&wc);

    RECT r = get_frame(g_target);
    g_overlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        CN, L"", WS_POPUP,
        r.left, r.top, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );

    if (!g_overlay) {
        fprintf(stderr, "[x] Failed to create overlay\n");
        return 1;
    }

    // 1. Enable DWM frame extension into client area
    int policy = DWMNCRP_ENABLED;
    DwmSetWindowAttribute(g_overlay, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));

    // 2. Apply acrylic blur with custom tint
    apply_accent(g_overlay, ACCENT_ENABLE_ACRYLICBLURBEHIND, tint);
    wprintf(L"    Tint: 0x%08X\n", tint);

    // 3. Position overlay BELOW target in z-order
    SetWindowPos(g_overlay, g_target, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    wchar_t title[256];
    GetWindowTextW(g_target, title, 256);
    wprintf(L"[*] Acrylic Overlay active\n");
    wprintf(L"    Target: %s\n", title);
    wprintf(L"    Overlay: %dx%d at (%d,%d)\n", r.right-r.left, r.bottom-r.top, r.left, r.top);
    wprintf(L"    Ctrl+C to stop\n");
    fflush(stdout);

    // Register WinEvent hook: refresh z-order when target regains focus
    g_hook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, WinEventProc, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    // Main loop: track target position/size
    MSG msg;
    bool run = true;
    RECT lastRect = r;
    while (run) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { run = false; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!run) break;

        if (!IsWindow(g_target)) {
            wprintf(L"[!] Target destroyed, exiting\n");
            break;
        }

        r = get_frame(g_target);
        if (r.left != lastRect.left || r.top != lastRect.top ||
            r.right != lastRect.right || r.bottom != lastRect.bottom) {
            lastRect = r;
            SetWindowPos(g_overlay, g_target,
                r.left, r.top, r.right - r.left, r.bottom - r.top,
                SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOZORDER);
        }

        Sleep(5);  // ~180 fps — match 180Hz display
    }

    if (g_hook) UnhookWinEvent(g_hook);

    apply_accent(g_overlay, ACCENT_DISABLED);
    DestroyWindow(g_overlay);
    wprintf(L"[*] Overlay destroyed\n");
    return 0;
}
