// Isolated test: does SetWindowRgn round an accent-blur window WITHOUT
// WS_EX_LAYERED? Same creation parameters as the lyric panels.
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000
#endif

struct AccentPolicy { int state, flags; unsigned color; int anim; };
struct WinCompAttrData { int attr; void* data; int size; };

LRESULT CALLBACK BgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT r; GetClientRect(h, &r);
        FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    WNDCLASSW bg = {}; bg.lpfnWndProc = BgProc; bg.hInstance = hi;
    bg.lpszClassName = L"CornerIsoBg"; bg.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    RegisterClassW(&bg);
    WNDCLASSW wc = {}; wc.lpfnWndProc = Proc; wc.hInstance = hi;
    wc.lpszClassName = L"CornerIsoTest";
    RegisterClassW(&wc);

    HWND bgwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"CornerIsoBg", L"", WS_POPUP | WS_VISIBLE,
        1800, 600, 500, 300, nullptr, nullptr, hi, nullptr);

    HWND hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW |
                                WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        L"CornerIsoTest", L"", WS_POPUP, 1900, 700, 400, 60,
        nullptr, nullptr, hi, nullptr);
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    auto SetAccent = (BOOL (WINAPI*)(HWND, WinCompAttrData*))GetProcAddress(u32, "SetWindowCompositionAttribute");
    AccentPolicy ap = { 4, 0, 0x50000000, 0 };
    WinCompAttrData d = { 19, &ap, sizeof(ap) };
    SetAccent(hwnd, &d);
    HRGN rgn = CreateRoundRectRgn(0, 0, 401, 61, 12, 12);
    SetWindowRgn(hwnd, rgn, TRUE);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    DwmFlush();
    Sleep(6000);
    DestroyWindow(hwnd);
    DestroyWindow(bgwnd);
    return 0;
}
