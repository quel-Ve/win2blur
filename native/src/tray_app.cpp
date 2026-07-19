/**
 * win2dist Tray App — native Win32 C++ rewrite
 * =============================================
 * System tray + global hotkeys + transparency + acrylic overlay management.
 * Replaces the Python tray_app.py entirely. No Python, no tkinter.
 *
 * Build: g++ tray_app.cpp -o win2dist.exe -ldwmapi -mwindows -O2 -s
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cstdlib>
using std::max;
using std::min;

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

// ==================== Constants ====================
#define WM_TRAYICON (WM_APP + 1)
#define ID_UP 1
#define ID_DOWN 2
#define ID_TOGGLE 3
#define ID_ACRYLIC 4
#define ID_SETTINGS 1001
#define ID_EXIT_RESTORE 1002
#define ID_EXIT_KEEP 1003
#define SETTINGS_TIMER 10
#define HOTKEY_POLL 50
#define TINT 0x1A000000  // 10% black
#define DEFAULT_STEP 5

// ==================== Globals ====================
static HWND g_hwnd = nullptr;
static NOTIFYICONDATAW g_nid = {};
static int g_step = DEFAULT_STEP;
static std::set<HWND> g_modified;
static std::map<HWND, int> g_lastAlpha;
static std::map<HWND, PROCESS_INFORMATION> g_overlays;
static std::wstring g_configPath;
static std::wstring g_overlayPath;
static std::wstring g_welcomePath;
static std::wstring g_tempDir;  // temp dir for extracted resources
static HICON g_hIcon = nullptr;
static int g_transpMod = MOD_ALT, g_transpKeyUp = VK_LEFT, g_transpKeyDown = VK_RIGHT;
static int g_toggleMod = MOD_ALT, g_toggleKey = VK_UP;
static int g_acrylicMod = MOD_ALT, g_acrylicKey = VK_DOWN;
static bool g_enableUp = true, g_enableDown = true, g_enableToggle = true, g_enableAcrylic = true;

// ==================== Resource extraction ====================
std::wstring extract_resource(int resId, const wchar_t* name) {
    if (!g_tempDir.empty()) {
        std::wstring path = g_tempDir + L"\\" + name;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            return path;  // already extracted
    }
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), (LPCWSTR)RT_RCDATA);
    if (!hRes) return L"";
    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData) return L"";
    DWORD size = SizeofResource(nullptr, hRes);
    void* data = LockResource(hData);
    if (!data) return L"";

    // Create temp dir
    if (g_tempDir.empty()) {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        wchar_t dir[MAX_PATH];
        wsprintfW(dir, L"%swin2dist_%u\\", tmp, GetCurrentProcessId());
        CreateDirectoryW(dir, nullptr);
        g_tempDir = dir;
    }
    std::wstring path = g_tempDir + name;
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return L"";
    DWORD written;
    WriteFile(hFile, data, size, &written, nullptr);
    CloseHandle(hFile);
    return path;
}

void cleanup_temp() {
    if (!g_tempDir.empty()) {
        DeleteFileW((g_tempDir + L"acrylic_overlay.exe").c_str());
        DeleteFileW((g_tempDir + L"welcome_demo.exe").c_str());
        RemoveDirectoryW(g_tempDir.c_str());
    }
}

// ==================== Config ====================
std::wstring config_path() {
    if (!g_configPath.empty()) return g_configPath;
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* last = wcsrchr(path, L'\\');
    if (last) *(last + 1) = 0;
    g_configPath = std::wstring(path) + L"config.ini";
    return g_configPath;
}

std::wstring exe_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* last = wcsrchr(path, L'\\');
    if (last) *(last + 1) = 0;
    return std::wstring(path);
}

int read_int(const wchar_t* key, int def) {
    return GetPrivateProfileIntW(L"Settings", key, def, config_path().c_str());
}
void write_int(const wchar_t* key, int val) {
    wchar_t buf[32];
    wsprintfW(buf, L"%d", val);
    WritePrivateProfileStringW(L"Settings", key, buf, config_path().c_str());
}
int read_hotkey(const wchar_t* section, const wchar_t* key, int def) {
    return GetPrivateProfileIntW(section, key, def, config_path().c_str());
}
bool read_switch(const wchar_t* key, bool def) {
    return GetPrivateProfileIntW(L"Switches", key, def ? 1 : 0, config_path().c_str()) != 0;
}

void load_config() {
    g_step = read_int(L"TransparencyStep", DEFAULT_STEP);
    if (g_step < 1 || g_step > 20) g_step = DEFAULT_STEP;
    g_transpMod = read_hotkey(L"Hotkeys", L"TransparencyUpModifiers", MOD_ALT);
    g_transpKeyUp = read_hotkey(L"Hotkeys", L"TransparencyUpKey", VK_LEFT);
    g_transpKeyDown = read_hotkey(L"Hotkeys", L"TransparencyDownKey", VK_RIGHT);
    g_toggleMod = read_hotkey(L"Hotkeys", L"TransparencyToggleModifiers", MOD_ALT);
    g_toggleKey = read_hotkey(L"Hotkeys", L"TransparencyToggleKey", VK_UP);
    g_acrylicMod = read_hotkey(L"Hotkeys", L"AcrylicToggleModifiers", MOD_ALT);
    g_acrylicKey = read_hotkey(L"Hotkeys", L"AcrylicToggleKey", VK_DOWN);
    g_enableUp = read_switch(L"EnableTransparencyUp", true);
    g_enableDown = read_switch(L"EnableTransparencyDown", true);
    g_enableToggle = read_switch(L"EnableTransparencyToggle", true);
    g_enableAcrylic = read_switch(L"EnableAcrylicToggle", true);
}

// ==================== Session save/restore ====================
void save_session(bool clear) {
    std::wstring cfg = config_path();
    if (clear) {
        WritePrivateProfileStringW(L"Session", nullptr, nullptr, cfg.c_str());
        return;
    }
    // Remove existing session
    WritePrivateProfileStringW(L"Session", nullptr, nullptr, cfg.c_str());
    int i = 0;
    for (auto hwnd : g_modified) {
        wchar_t buf[256], key[32];
        int alpha = 255;
        BYTE b; DWORD flags;
        if (GetLayeredWindowAttributes(hwnd, nullptr, &b, &flags) && (flags & LWA_ALPHA))
            alpha = b;
        int tint = g_overlays.count(hwnd) ? 0 : -1;  // always tint index 0 (2% black)
        wchar_t title[128] = {}, cls[64] = {};
        GetWindowTextW(hwnd, title, 127);
        GetClassNameW(hwnd, cls, 63);
        wsprintfW(buf, L"%s|%s|%d|%d", title, cls, alpha, tint);
        wsprintfW(key, L"window_%d", i++);
        WritePrivateProfileStringW(L"Session", key, buf, cfg.c_str());
    }
}

struct SessionItem { std::wstring title, cls; int alpha, tint; };
static std::vector<SessionItem> g_sessionItems;

void load_session_items() {
    g_sessionItems.clear();
    wchar_t buf[512];
    auto cfg = config_path();
    for (int i = 0; i < 50; i++) {
        wchar_t key[32];
        wsprintfW(key, L"window_%d", i);
        if (!GetPrivateProfileStringW(L"Session", key, L"", buf, 512, cfg.c_str()) || !buf[0]) break;
        std::wstring s(buf);
        auto p1 = s.find(L'|'); if (p1 == s.npos) continue;
        auto p2 = s.find(L'|', p1+1); if (p2 == s.npos) continue;
        auto p3 = s.find(L'|', p2+1);
        SessionItem si;
        si.title = s.substr(0, p1);
        si.cls = s.substr(p1+1, p2-p1-1);
        si.alpha = _wtoi(s.substr(p2+1, p3-p2-1).c_str());
        si.tint = (p3 != s.npos) ? _wtoi(s.substr(p3+1).c_str()) : -1;
        g_sessionItems.push_back(si);
    }
}

HWND find_window_by_title_class(const wchar_t* title, const wchar_t* cls) {
    struct Ctx { const wchar_t* title; const wchar_t* cls; HWND found; } ctx = {title, cls, nullptr};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = (Ctx*)lp;
        if (!IsWindowVisible(h)) return TRUE;
        wchar_t t[256], cl[64];
        GetWindowTextW(h, t, 255);
        GetClassNameW(h, cl, 63);
        if (wcscmp(t, c->title) == 0 && wcscmp(cl, c->cls) == 0) {
            c->found = h; return FALSE;
        }
        return TRUE;
    }, (LPARAM)&ctx);
    return ctx.found;
}

void restore_session() {
    load_session_items();
    if (g_sessionItems.empty()) return;
    int restored = 0;
    for (auto& si : g_sessionItems) {
        HWND h = find_window_by_title_class(si.title.c_str(), si.cls.c_str());
        if (!h) continue;
        LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
        if (!(ex & WS_EX_LAYERED))
            SetWindowLongW(h, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetLayeredWindowAttributes(h, 0, (BYTE)si.alpha, LWA_ALPHA);
        g_modified.insert(h);
        if (si.tint >= 0) {
            wchar_t cmd[512], arg[32];
            wsprintfW(arg, L"0x%08X 0x%08X", (DWORD)(ULONG_PTR)h, TINT);
            wcscpy(cmd, L"\"");
            wcscat(cmd, g_overlayPath.c_str());
            wcscat(cmd, L"\" ");
            wcscat(cmd, arg);
            STARTUPINFOW si = {sizeof(si)};
            si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {};
            if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                g_overlays[h] = pi;
            }
        }
        restored++;
    }
    save_session(true);
}

// ==================== Transparency ====================
void apply_transparency(HWND hwnd, int alpha) {
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_LAYERED))
        SetWindowLongW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd, 0, (BYTE)alpha, LWA_ALPHA);
    g_modified.insert(hwnd);
}

// ==================== Acrylic Overlay ====================
void launch_overlay(HWND hwnd) {
    if (g_overlayPath.empty()) {
        g_overlayPath = extract_resource(101, L"acrylic_overlay.exe");
    }
    if (g_overlays.count(hwnd)) return;
    wchar_t cmd[512], arg[32];
    wsprintfW(arg, L"0x%08X 0x%08X", (DWORD)(ULONG_PTR)hwnd, TINT);
    wcscpy(cmd, L"\"");
    wcscat(cmd, g_overlayPath.c_str());
    wcscat(cmd, L"\" ");
    wcscat(cmd, arg);
    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        g_overlays[hwnd] = pi;
    }
}

void kill_overlay(HWND hwnd) {
    auto it = g_overlays.find(hwnd);
    if (it == g_overlays.end()) return;
    TerminateProcess(it->second.hProcess, 0);
    CloseHandle(it->second.hProcess);
    CloseHandle(it->second.hThread);
    g_overlays.erase(it);
}

void toggle_acrylic(HWND hwnd) {
    if (g_overlays.count(hwnd)) {
        kill_overlay(hwnd);
    } else {
        launch_overlay(hwnd);
    }
}

// ==================== Hotkey handler ====================
void on_hotkey(int id) {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return;

    BYTE alpha = 255;
    DWORD flags;
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED)
        GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags);
    if (alpha == 0) alpha = 255;

    int step = g_step * 255 / 100;

    switch (id) {
    case ID_UP:
        alpha = (BYTE)max(10, (int)alpha - step);
        apply_transparency(hwnd, alpha);
        break;
    case ID_DOWN:
        alpha = (BYTE)min(255, (int)alpha + step);
        apply_transparency(hwnd, alpha);
        break;
    case ID_TOGGLE:
        if (alpha < 255) {
            g_lastAlpha[hwnd] = alpha;
            apply_transparency(hwnd, 255);
        } else {
            int last = g_lastAlpha.count(hwnd) ? g_lastAlpha[hwnd] : 255 - step;
            if (last >= 255) last = 255 - step;
            apply_transparency(hwnd, last);
        }
        break;
    case ID_ACRYLIC:
        toggle_acrylic(hwnd);
        break;
    }
}

// ==================== Settings Dialog ====================
INT_PTR CALLBACK SettingsDlg(HWND hDlg, UINT msg, WPARAM wp, LPARAM) {
    static int* pStep = nullptr;
    switch (msg) {
    case WM_INITDIALOG: {
        pStep = (int*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
        SetDlgItemInt(hDlg, 100, *pStep, FALSE);
        SendDlgItemMessageW(hDlg, 101, TBM_SETRANGE, TRUE, MAKELONG(1, 20));
        SendDlgItemMessageW(hDlg, 101, TBM_SETPOS, TRUE, *pStep);
        wchar_t buf[64];
        wsprintfW(buf, L"ALT+LEFT   more transparent\r\nALT+RIGHT  less transparent\r\nALT+UP     toggle on/off\r\nALT+DOWN   acrylic blur");
        SetDlgItemTextW(hDlg, 102, buf);
        return TRUE;
    }
    case WM_HSCROLL:
        if (LOWORD(wp) == TB_THUMBPOSITION || LOWORD(wp) == TB_ENDTRACK) {
            int v = (int)SendDlgItemMessageW(hDlg, 101, TBM_GETPOS, 0, 0);
            SetDlgItemInt(hDlg, 100, v, FALSE);
        }
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            int v = GetDlgItemInt(hDlg, 100, nullptr, FALSE);
            if (v >= 1 && v <= 20) {
                *pStep = v;
                write_int(L"TransparencyStep", v);
            }
            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wp) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;
    }
    return FALSE;
}

void show_settings(HINSTANCE hInst) {
    DialogBoxParamW(hInst, MAKEINTRESOURCEW(200), g_hwnd, SettingsDlg, (LPARAM)&g_step);
}

// ==================== Tray menu ====================
void show_tray_menu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_SETTINGS, L"Settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT_RESTORE, L"Restore && Exit");
    AppendMenuW(menu, MF_STRING, ID_EXIT_KEEP, L"Keep && Exit");
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
}

// ==================== Shutdown ====================
void shutdown(bool restore) {
    if (restore) {
        for (auto hwnd : g_modified) {
            LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
            if (ex & WS_EX_LAYERED) {
                SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
                SetWindowLongW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
            }
        }
        g_modified.clear();
        for (auto& kv : g_overlays) {
            TerminateProcess(kv.second.hProcess, 0);
            CloseHandle(kv.second.hProcess);
            CloseHandle(kv.second.hThread);
        }
        g_overlays.clear();
        save_session(true);
    } else {
        save_session(false);
    }
    UnregisterHotKey(g_hwnd, ID_UP);
    UnregisterHotKey(g_hwnd, ID_DOWN);
    UnregisterHotKey(g_hwnd, ID_TOGGLE);
    UnregisterHotKey(g_hwnd, ID_ACRYLIC);
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    cleanup_temp();
    DestroyWindow(g_hwnd);
}

// ==================== Window proc ====================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_nid.cbSize = sizeof(NOTIFYICONDATAW);
        g_nid.hWnd = hwnd; g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon = g_hIcon ? g_hIcon : LoadIcon(nullptr, IDI_APPLICATION);
        wcscpy(g_nid.szTip, L"win2dist");
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        if (g_enableUp) RegisterHotKey(hwnd, ID_UP, g_transpMod, g_transpKeyUp);
        if (g_enableDown) RegisterHotKey(hwnd, ID_DOWN, g_transpMod, g_transpKeyDown);
        if (g_enableToggle) RegisterHotKey(hwnd, ID_TOGGLE, g_toggleMod, g_toggleKey);
        if (g_enableAcrylic) RegisterHotKey(hwnd, ID_ACRYLIC, g_acrylicMod, g_acrylicKey);
        // Extract both resources before launching welcome
        g_overlayPath = extract_resource(101, L"acrylic_overlay.exe");
        g_welcomePath = extract_resource(102, L"welcome_demo.exe");
        if (!g_welcomePath.empty()) {
            STARTUPINFOW si = {sizeof(si)};
            PROCESS_INFORMATION pi = {};
            CreateProcessW(g_welcomePath.c_str(), nullptr, nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
            if (pi.hProcess) {
                WaitForSingleObject(pi.hProcess, INFINITE);
                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            }
        }
        // Restore session
        restore_session();
        return 0;
    }
    case WM_HOTKEY:
        on_hotkey((int)wp);
        return 0;
    case WM_TRAYICON:
        if (LOWORD(lp) == WM_RBUTTONUP) show_tray_menu();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_SETTINGS) {
            show_settings((HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        } else if (LOWORD(wp) == ID_EXIT_RESTORE) {
            shutdown(true); PostQuitMessage(0);
        } else if (LOWORD(wp) == ID_EXIT_KEEP) {
            shutdown(false); PostQuitMessage(0);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ==================== WinMain ====================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    InitCommonControls();
    g_hIcon = LoadIconW(hInst, L"APP_ICON");
    load_config();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.lpszClassName = L"win2distTray";
    if (!RegisterClassW(&wc)) return 1;

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
