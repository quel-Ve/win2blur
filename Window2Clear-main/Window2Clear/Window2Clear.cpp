#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

// 常量定义
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_SETTINGS 1002
#define ID_HOTKEY_TRANSPARENCY_UP 1
#define ID_HOTKEY_TRANSPARENCY_DOWN 2
#define ID_HOTKEY_CENTER_WINDOW 3
#define ID_HOTKEY_SHAKE_WINDOW 4
#define CONFIG_FILE L".\\config.ini"
#define MAX_PATH_LEN 260
#define APP_VERSION L"v0.2.0"

// 控件ID
#define IDC_TRANSPARENCY_UP_BUTTON 2001
#define IDC_TRANSPARENCY_DOWN_BUTTON 2002
#define IDC_CENTER_BUTTON 2003
#define IDC_SHAKE_BUTTON 2004
#define IDC_TRANSPARENCY_SLIDER 2005
#define IDC_SAVE_BUTTON 2006
#define IDC_TRANSPARENCY_LABEL 2007
#define IDC_TRANSPARENCY_UP_DISPLAY 2008
#define IDC_TRANSPARENCY_DOWN_DISPLAY 2009
#define IDC_CENTER_DISPLAY 2010
#define IDC_SHAKE_DISPLAY 2011
#define IDC_TRANSPARENCY_ENABLE 2012
#define IDC_CENTER_ENABLE 2013
#define IDC_SHAKE_ENABLE 2014

// 全局变量
HWND g_hMainWnd = NULL;           // 主窗口句柄
HWND g_hSettingsWnd = NULL;       // 设置窗口句柄
NOTIFYICONDATA g_nid = { 0 };     // 系统托盘图标数据
int g_transparencyStep = 10;      // 透明度调整步长（默认10%）

// 透明度热键设置
UINT g_transparencyUpModifiers = MOD_ALT;    // 透明度增加修饰键
UINT g_transparencyUpKey = VK_LEFT;          // 透明度增加按键
UINT g_transparencyDownModifiers = MOD_ALT;  // 透明度减少修饰键
UINT g_transparencyDownKey = VK_RIGHT;       // 透明度减少按键
BOOL g_enableTransparencyUp = TRUE;          // 是否启用透明度增加功能
BOOL g_enableTransparencyDown = TRUE;        // 是否启用透明度减少功能

// 窗口居中热键设置
UINT g_centerModifiers = MOD_CONTROL;        // 窗口居中修饰键
UINT g_centerKey = VK_NUMPAD5;               // 窗口居中按键
BOOL g_enableCenter = FALSE;                 // 是否启用窗口居中功能

// 窗口抖动热键设置
UINT g_shakeModifiers = MOD_ALT;             // 窗口抖动修饰键
UINT g_shakeKey = VK_DOWN;                   // 窗口抖动按键
BOOL g_enableShake = FALSE;                  // 是否启用窗口抖动功能

// 热键监听状态
BOOL g_isListeningHotkey = FALSE;     // 是否正在监听热键输入
int g_currentListeningType = 0;       // 当前监听类型: 0=无, 1=透明度增加, 2=透明度减少, 3=居中, 4=抖动
HWND g_hCurrentButton = NULL;         // 当前正在设置的按钮句柄
HWND g_hCurrentDisplay = NULL;        // 当前正在设置的显示框句柄
DWORD g_listeningStartTime = 0;       // 监听开始时间（用于超时检测）

// 函数声明
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);         // 主窗口消息处理函数
LRESULT CALLBACK SettingsProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);       // 设置窗口消息处理函数
void CreateTrayIcon(HWND hwnd);                   // 创建系统托盘图标
void RemoveTrayIcon();                            // 移除系统托盘图标
void ShowContextMenu(HWND hwnd);                  // 显示右键菜单
void ShowSettingsWindow();                        // 显示设置窗口
void RegisterHotKeys(HWND hwnd);                  // 注册全局热键
void UnregisterHotKeys(HWND hwnd);                // 注销全局热键
void AdjustWindowTransparency(BOOL increase);     // 调整窗口透明度
void CenterWindow();                              // 将窗口居中显示
void ShakeWindow();                               // 窗口抖动效果
void LoadConfig();                                // 从配置文件加载设置
void SaveConfig();                                // 保存设置到配置文件
HWND GetTopMostWindow();                          // 获取最上层窗口
void SetWindowTransparency(HWND hwnd, int alpha); // 设置窗口透明度
int GetWindowTransparency(HWND hwnd);             // 获取窗口透明度
wchar_t* GetModifierName(UINT modifiers);         // 获取修饰键名称
wchar_t* GetKeyName(UINT vkCode);                 // 获取按键名称

// 主函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // 初始化通用控件
    InitCommonControls();

    // 检查是否已有实例运行
    HWND existingWnd = FindWindow(L"Window2ClearClass", NULL);
    if (existingWnd) {
        MessageBox(NULL, L"Window2Clear 已在运行中！", L"提示", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // 注册窗口类
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"Window2ClearClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINDOW2CLEAR));

    if (!RegisterClass(&wc)) {
        MessageBox(NULL, L"注册窗口类失败！", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 加载配置
    LoadConfig();

    // 创建隐藏的主窗口
    g_hMainWnd = CreateWindow(
        L"Window2ClearClass",
        L"Window2Clear",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        400, 300,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hMainWnd) {
        MessageBox(NULL, L"创建主窗口失败！", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 创建系统托盘图标
    CreateTrayIcon(g_hMainWnd);

    // 注册全局热键
    RegisterHotKeys(g_hMainWnd);

    // 显示启动提示
    wchar_t startupMsg[500];
    swprintf_s(startupMsg, 500, L"Window2Clear %s 已启动！\n\n默认热键：\n- Alt+←/→ 调整窗口透明度\n- Ctrl+数字键5 窗口居中（需开启）\n- Alt+↓ 窗口抖动（需开启）\n\n右键点击托盘图标进行设置", APP_VERSION);
    MessageBox(NULL, startupMsg, L"Window2Clear 启动成功", MB_OK | MB_ICONINFORMATION);

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理资源
    UnregisterHotKeys(g_hMainWnd);
    RemoveTrayIcon();

    return (int)msg.wParam;
}

// 主窗口过程
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_CREATE:
        break;

    case WM_HOTKEY:
        switch (wParam) {
        case ID_HOTKEY_TRANSPARENCY_UP:
            AdjustWindowTransparency(TRUE); // 增加透明度（减少不透明度）
            break;
        case ID_HOTKEY_TRANSPARENCY_DOWN:
            AdjustWindowTransparency(FALSE); // 减少透明度（增加不透明度）
            break;
        case ID_HOTKEY_CENTER_WINDOW:
            CenterWindow(); // 窗口居中
            break;
        case ID_HOTKEY_SHAKE_WINDOW:
            ShakeWindow(); // 窗口抖动
            break;
        }
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            ShowContextMenu(hwnd);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_SETTINGS:
            ShowSettingsWindow();
            break;
        case ID_TRAY_EXIT:
            PostQuitMessage(0);
            break;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// 设置窗口过程
LRESULT CALLBACK SettingsProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static HWND hTransparencySlider, hSaveButton, hTransparencyLabel;
    static HWND hTransparencyUpDisplay, hTransparencyDownDisplay, hCenterDisplay, hShakeDisplay;
    static HWND hTransparencyUpButton, hTransparencyDownButton, hCenterButton, hShakeButton;
    static HWND hTransparencyEnable, hCenterEnable, hShakeEnable;

    switch (uMsg) {
    case WM_CREATE:
    {
        // 打开设置窗口时，先取消所有热键监听
        UnregisterHotKeys(g_hMainWnd);

        int yPos = 20;

        // 透明度功能区
        CreateWindow(L"STATIC", L"透明度控制:",
            WS_VISIBLE | WS_CHILD,
            20, yPos, 120, 20,
            hwnd, NULL, GetModuleHandle(NULL), NULL);

        hTransparencyEnable = CreateWindow(L"BUTTON", L"启用",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            150, yPos, 60, 20,
            hwnd, (HMENU)IDC_TRANSPARENCY_ENABLE, GetModuleHandle(NULL), NULL);
        SendMessage(hTransparencyEnable, BM_SETCHECK, (g_enableTransparencyUp || g_enableTransparencyDown) ? BST_CHECKED : BST_UNCHECKED, 0);

        yPos += 30;

        // 透明度增加热键
        CreateWindow(L"STATIC", L"增加透明度:",
            WS_VISIBLE | WS_CHILD,
            30, yPos, 80, 20,
            hwnd, NULL, GetModuleHandle(NULL), NULL);

        hTransparencyUpDisplay = CreateWindow(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
            120, yPos, 140, 20,
            hwnd, (HMENU)IDC_TRANSPARENCY_UP_DISPLAY, GetModuleHandle(NULL), NULL);
        // 设置当前热键显示
        wchar_t keyText[256];
        swprintf(keyText, 256, L"%s+%s", GetModifierName(g_transparencyUpModifiers), GetKeyName(g_transparencyUpKey));
        SetWindowText(hTransparencyUpDisplay, keyText);

        hTransparencyUpButton = CreateWindow(L"BUTTON", L"设置",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            270, yPos, 50, 20,
            hwnd, (HMENU)IDC_TRANSPARENCY_UP_BUTTON, GetModuleHandle(NULL), NULL);

        yPos += 30;

        // 透明度减少热键
        CreateWindow(L"STATIC", L"减少透明度:",
            WS_VISIBLE | WS_CHILD,
            30, yPos, 80, 20,
            hwnd, NULL, GetModuleHandle(NULL), NULL);

        hTransparencyDownDisplay = CreateWindow(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
            120, yPos, 140, 20,
            hwnd, (HMENU)IDC_TRANSPARENCY_DOWN_DISPLAY, GetModuleHandle(NULL), NULL);
        // 设置当前热键显示
        swprintf(keyText, 256, L"%s+%s", GetModifierName(g_transparencyDownModifiers), GetKeyName(g_transparencyDownKey));
        SetWindowText(hTransparencyDownDisplay, keyText);

        hTransparencyDownButton = CreateWindow(L"BUTTON", L"设置",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            270, yPos, 50, 20,
            hwnd, (HMENU)IDC_TRANSPARENCY_DOWN_BUTTON, GetModuleHandle(NULL), NULL);

        yPos += 40;

        // 窗口居中功能区
        CreateWindow(L"STATIC", L"窗口居中:",
            WS_VISIBLE | WS_CHILD,
            20, yPos, 80, 20,
            hwnd, NULL, GetModuleHandle(NULL), NULL);

        hCenterEnable = CreateWindow(L"BUTTON", L"启用",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            150, yPos, 60, 20,
            hwnd, (HMENU)IDC_CENTER_ENABLE, GetModuleHandle(NULL), NULL);
        SendMessage(hCenterEnable, BM_SETCHECK, g_enableCenter ? BST_CHECKED : BST_UNCHECKED, 0);

        yPos += 30;

        hCenterDisplay = CreateWindow(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
            30, yPos, 140, 20,
            hwnd, (HMENU)IDC_CENTER_DISPLAY, GetModuleHandle(NULL), NULL);
        // 设置当前热键显示
        swprintf(keyText, 256, L"%s+%s", GetModifierName(g_centerModifiers), GetKeyName(g_centerKey));
        SetWindowText(hCenterDisplay, keyText);

        hCenterButton = CreateWindow(L"BUTTON", L"设置",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            180, yPos, 50, 20,
            hwnd, (HMENU)IDC_CENTER_BUTTON, GetModuleHandle(NULL), NULL);

        yPos += 40;

        // 窗口抖动功能区
        CreateWindow(L"STATIC", L"窗口抖动:",
            WS_VISIBLE | WS_CHILD,
            20, yPos, 80, 20,
            hwnd, NULL, GetModuleHandle(NULL), NULL);

        hShakeEnable = CreateWindow(L"BUTTON", L"启用",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            150, yPos, 60, 20,
            hwnd, (HMENU)IDC_SHAKE_ENABLE, GetModuleHandle(NULL), NULL);
        SendMessage(hShakeEnable, BM_SETCHECK, g_enableShake ? BST_CHECKED : BST_UNCHECKED, 0);

        yPos += 30;

        hShakeDisplay = CreateWindow(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
            30, yPos, 140, 20,
            hwnd, (HMENU)IDC_SHAKE_DISPLAY, GetModuleHandle(NULL), NULL);
        // 设置当前热键显示
        swprintf(keyText, 256, L"%s+%s", GetModifierName(g_shakeModifiers), GetKeyName(g_shakeKey));
        SetWindowText(hShakeDisplay, keyText);

        hShakeButton = CreateWindow(L"BUTTON", L"设置",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            180, yPos, 50, 20,
            hwnd, (HMENU)IDC_SHAKE_BUTTON, GetModuleHandle(NULL), NULL);

        yPos += 40;

        // 透明度步长设置
        hTransparencyLabel = CreateWindow(L"STATIC", L"透明度步长: 10%",
            WS_VISIBLE | WS_CHILD,
            20, yPos, 200, 20,
            hwnd, (HMENU)IDC_TRANSPARENCY_LABEL, GetModuleHandle(NULL), NULL);

        yPos += 25;

        hTransparencySlider = CreateWindow(TRACKBAR_CLASS, L"",
            WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_AUTOTICKS,
            20, yPos, 250, 30,
            hwnd, (HMENU)IDC_TRANSPARENCY_SLIDER, GetModuleHandle(NULL), NULL);

        SendMessage(hTransparencySlider, TBM_SETRANGE, TRUE, MAKELONG(1, 50));
        SendMessage(hTransparencySlider, TBM_SETPOS, TRUE, g_transparencyStep);
        SendMessage(hTransparencySlider, TBM_SETTICFREQ, 5, 0);

        yPos += 50;

        // 保存按钮
        hSaveButton = CreateWindow(L"BUTTON", L"保存设置",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            20, yPos, 100, 30,
            hwnd, (HMENU)IDC_SAVE_BUTTON, GetModuleHandle(NULL), NULL);

        // 关闭按钮（放在保存按钮右侧）
        CreateWindow(L"BUTTON", L"关闭",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            140, yPos, 100, 30,
            hwnd, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL);

        break;
    }

    case WM_HSCROLL:
        if ((HWND)lParam == hTransparencySlider) {
            int pos = (int)SendMessage(hTransparencySlider, TBM_GETPOS, 0, 0);
            wchar_t text[50];
            swprintf(text, 50, L"透明度步长: %d%%", pos);
            SetWindowText(hTransparencyLabel, text);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_TRANSPARENCY_UP_BUTTON:
        {
            if (g_isListeningHotkey && g_currentListeningType == 1) {
                g_isListeningHotkey = FALSE;
                g_currentListeningType = 0;
                SetWindowText(hTransparencyUpButton, L"设置");
                // 恢复原来的热键显示
                wchar_t keyText[256];
                swprintf(keyText, 256, L"%s+%s", GetModifierName(g_transparencyUpModifiers), GetKeyName(g_transparencyUpKey));
                SetWindowText(hTransparencyUpDisplay, keyText);
                ReleaseCapture();
            }
            else {
                g_isListeningHotkey = TRUE;
                g_currentListeningType = 1;
                g_hCurrentButton = hTransparencyUpButton;
                g_hCurrentDisplay = hTransparencyUpDisplay;
                g_listeningStartTime = GetTickCount();
                SetWindowText(hTransparencyUpButton, L"取消");
                SetWindowText(hTransparencyUpDisplay, L"请按下组合键...");
                SetFocus(hwnd);
                // SetCapture(hwnd);
            }
            break;
        }
        case IDC_TRANSPARENCY_DOWN_BUTTON:
        {
            if (g_isListeningHotkey && g_currentListeningType == 2) {
                g_isListeningHotkey = FALSE;
                g_currentListeningType = 0;
                SetWindowText(hTransparencyDownButton, L"设置");
                // 恢复原来的热键显示
                wchar_t keyText[256];
                swprintf(keyText, 256, L"%s+%s", GetModifierName(g_transparencyDownModifiers), GetKeyName(g_transparencyDownKey));
                SetWindowText(hTransparencyDownDisplay, keyText);
                ReleaseCapture();
            }
            else {
                g_isListeningHotkey = TRUE;
                g_currentListeningType = 2;
                g_hCurrentButton = hTransparencyDownButton;
                g_hCurrentDisplay = hTransparencyDownDisplay;
                g_listeningStartTime = GetTickCount();
                SetWindowText(hTransparencyDownButton, L"取消");
                SetWindowText(hTransparencyDownDisplay, L"请按下组合键...");
                SetFocus(hwnd);
                // SetCapture(hwnd);
            }
            break;
        }
        case IDC_CENTER_BUTTON:
        {
            if (g_isListeningHotkey && g_currentListeningType == 3) {
                g_isListeningHotkey = FALSE;
                g_currentListeningType = 0;
                SetWindowText(hCenterButton, L"设置");
                // 恢复原来的热键显示
                wchar_t keyText[256];
                swprintf(keyText, 256, L"%s+%s", GetModifierName(g_centerModifiers), GetKeyName(g_centerKey));
                SetWindowText(hCenterDisplay, keyText);
                ReleaseCapture();
            }
            else {
                g_isListeningHotkey = TRUE;
                g_currentListeningType = 3;
                g_hCurrentButton = hCenterButton;
                g_hCurrentDisplay = hCenterDisplay;
                g_listeningStartTime = GetTickCount();
                SetWindowText(hCenterButton, L"取消");
                SetWindowText(hCenterDisplay, L"请按下组合键...");
                SetFocus(hwnd);
                // SetCapture(hwnd);
            }
            break;
        }
        case IDC_SHAKE_BUTTON:
        {
            if (g_isListeningHotkey && g_currentListeningType == 4) {
                g_isListeningHotkey = FALSE;
                g_currentListeningType = 0;
                SetWindowText(hShakeButton, L"设置");
                // 恢复原来的热键显示
                wchar_t keyText[256];
                swprintf(keyText, 256, L"%s+%s", GetModifierName(g_shakeModifiers), GetKeyName(g_shakeKey));
                SetWindowText(hShakeDisplay, keyText);
                ReleaseCapture();
            }
            else {
                g_isListeningHotkey = TRUE;
                g_currentListeningType = 4;
                g_hCurrentButton = hShakeButton;
                g_hCurrentDisplay = hShakeDisplay;
                g_listeningStartTime = GetTickCount();
                SetWindowText(hShakeButton, L"取消");
                SetWindowText(hShakeDisplay, L"请按下组合键...");
                SetFocus(hwnd);
                // SetCapture(hwnd);
            }
            break;
        }
        case IDC_TRANSPARENCY_ENABLE:
        {
            BOOL transparencyEnabled = (SendMessage(hTransparencyEnable, BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_enableTransparencyUp = transparencyEnabled;
            g_enableTransparencyDown = transparencyEnabled;
            break;
        }
        case IDC_CENTER_ENABLE:
            g_enableCenter = (SendMessage(hCenterEnable, BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        case IDC_SHAKE_ENABLE:
            g_enableShake = (SendMessage(hShakeEnable, BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        case IDC_SAVE_BUTTON:
        {
            // 获取透明度步长
            g_transparencyStep = (int)SendMessage(hTransparencySlider, TBM_GETPOS, 0, 0);

            // 保存配置
            SaveConfig();

            // 重新注册热键
            UnregisterHotKeys(g_hMainWnd);
            RegisterHotKeys(g_hMainWnd);

            MessageBox(hwnd, L"设置已保存！", L"提示", MB_OK | MB_ICONINFORMATION);
            break;
        }
        case IDCANCEL:
            // 取消设置时，重新注册所有热键
            RegisterHotKeys(g_hMainWnd);
            DestroyWindow(hwnd);
            g_hSettingsWnd = NULL;
            break;
        }
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (g_isListeningHotkey) {
            // 检查超时（10秒）
            if (GetTickCount() - g_listeningStartTime > 10000) {
                g_isListeningHotkey = FALSE;
                SetWindowText(g_hCurrentButton, L"设置");
                SetWindowText(g_hCurrentDisplay, L"监听超时，请重试");
                g_currentListeningType = 0;
                ReleaseCapture();
                break;
            }

            // 检测修饰键
            UINT modifiers = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
            if (GetKeyState(VK_MENU) & 0x8000) modifiers |= MOD_ALT;
            if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= MOD_SHIFT;
            if (GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000) modifiers |= MOD_WIN;

            // 如果没有修饰键，忽略单独按键（除了功能键）
            if (modifiers == 0 && wParam >= 'A' && wParam <= 'Z') {
                break;
            }

            // 验证热键有效性：必须有修饰键或者是功能键
            if (modifiers == 0 && !(wParam >= VK_F1 && wParam <= VK_F24) &&
                wParam != VK_INSERT && wParam != VK_DELETE && wParam != VK_HOME &&
                wParam != VK_END && wParam != VK_PRIOR && wParam != VK_NEXT &&
                wParam != VK_SPACE && wParam != VK_TAB && wParam != VK_RETURN && wParam != VK_ESCAPE) {
                SetWindowText(g_hCurrentDisplay, L"请使用修饰键组合");
                break;
            }

            // 处理功能键、方向键、字母键和数字键
            if ((wParam >= VK_F1 && wParam <= VK_F24) ||
                (wParam >= VK_LEFT && wParam <= VK_DOWN) ||
                (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9) ||
                (wParam >= 'A' && wParam <= 'Z') ||
                (wParam >= '0' && wParam <= '9') ||
                wParam == VK_INSERT || wParam == VK_DELETE ||
                wParam == VK_HOME || wParam == VK_END ||
                wParam == VK_PRIOR || wParam == VK_NEXT ||
                wParam == VK_SPACE || wParam == VK_TAB ||
                wParam == VK_RETURN || wParam == VK_ESCAPE) {

                // 根据当前监听类型更新对应的全局变量
                switch (g_currentListeningType) {
                case 1: // 透明度增加
                    g_transparencyUpModifiers = modifiers;
                    g_transparencyUpKey = (UINT)wParam;
                    break;
                case 2: // 透明度减少
                    g_transparencyDownModifiers = modifiers;
                    g_transparencyDownKey = (UINT)wParam;
                    break;
                case 3: // 窗口居中
                    g_centerModifiers = modifiers;
                    g_centerKey = (UINT)wParam;
                    break;
                case 4: // 窗口抖动
                    g_shakeModifiers = modifiers;
                    g_shakeKey = (UINT)wParam;
                    break;
                }

                // 显示设置的热键
                wchar_t hotkeyText[100] = L"";
                if (modifiers & MOD_CONTROL) wcscat_s(hotkeyText, 100, L"CTRL+");
                if (modifiers & MOD_ALT) wcscat_s(hotkeyText, 100, L"ALT+");
                if (modifiers & MOD_SHIFT) wcscat_s(hotkeyText, 100, L"SHIFT+");
                if (modifiers & MOD_WIN) wcscat_s(hotkeyText, 100, L"WIN+");

                // 添加按键名称
                switch (wParam) {
                case VK_UP: wcscat_s(hotkeyText, 100, L"UP"); break;
                case VK_DOWN: wcscat_s(hotkeyText, 100, L"DOWN"); break;
                case VK_LEFT: wcscat_s(hotkeyText, 100, L"LEFT"); break;
                case VK_RIGHT: wcscat_s(hotkeyText, 100, L"RIGHT"); break;
                case VK_NUMPAD0: wcscat_s(hotkeyText, 100, L"NUM0"); break;
                case VK_NUMPAD1: wcscat_s(hotkeyText, 100, L"NUM1"); break;
                case VK_NUMPAD2: wcscat_s(hotkeyText, 100, L"NUM2"); break;
                case VK_NUMPAD3: wcscat_s(hotkeyText, 100, L"NUM3"); break;
                case VK_NUMPAD4: wcscat_s(hotkeyText, 100, L"NUM4"); break;
                case VK_NUMPAD5: wcscat_s(hotkeyText, 100, L"NUM5"); break;
                case VK_NUMPAD6: wcscat_s(hotkeyText, 100, L"NUM6"); break;
                case VK_NUMPAD7: wcscat_s(hotkeyText, 100, L"NUM7"); break;
                case VK_NUMPAD8: wcscat_s(hotkeyText, 100, L"NUM8"); break;
                case VK_NUMPAD9: wcscat_s(hotkeyText, 100, L"NUM9"); break;
                case VK_INSERT: wcscat_s(hotkeyText, 100, L"INSERT"); break;
                case VK_DELETE: wcscat_s(hotkeyText, 100, L"DELETE"); break;
                case VK_HOME: wcscat_s(hotkeyText, 100, L"HOME"); break;
                case VK_END: wcscat_s(hotkeyText, 100, L"END"); break;
                case VK_PRIOR: wcscat_s(hotkeyText, 100, L"PAGEUP"); break;
                case VK_NEXT: wcscat_s(hotkeyText, 100, L"PAGEDOWN"); break;
                case VK_SPACE: wcscat_s(hotkeyText, 100, L"SPACE"); break;
                case VK_TAB: wcscat_s(hotkeyText, 100, L"TAB"); break;
                case VK_RETURN: wcscat_s(hotkeyText, 100, L"ENTER"); break;
                case VK_ESCAPE: wcscat_s(hotkeyText, 100, L"ESC"); break;
                default:
                    if (wParam >= VK_F1 && wParam <= VK_F24) {
                        wchar_t fKey[8];
                        swprintf(fKey, 8, L"F%d", (int)(wParam - VK_F1 + 1));
                        wcscat_s(hotkeyText, 100, fKey);
                    }
                    else if (wParam >= 'A' && wParam <= 'Z') {
                        wchar_t letter[2] = { (wchar_t)wParam, L'\0' };
                        wcscat_s(hotkeyText, 100, letter);
                    }
                    else if (wParam >= '0' && wParam <= '9') {
                        wchar_t digit[2] = { (wchar_t)wParam, L'\0' };
                        wcscat_s(hotkeyText, 100, digit);
                    }
                    else {
                        wchar_t keyName[32];
                        swprintf(keyName, 32, L"KEY_%d", (int)wParam);
                        wcscat_s(hotkeyText, 100, keyName);
                    }
                    break;
                }

                SetWindowText(g_hCurrentDisplay, hotkeyText);

                // 停止监听
                g_isListeningHotkey = FALSE;
                g_currentListeningType = 0;
                SetWindowText(g_hCurrentButton, L"设置");
                ReleaseCapture();
            }
        }
        break;

    case WM_CLOSE:
        // 关闭设置窗口时，重新注册所有热键
        RegisterHotKeys(g_hMainWnd);
        DestroyWindow(hwnd);
        g_hSettingsWnd = NULL;
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// 创建系统托盘图标
void CreateTrayIcon(HWND hwnd)
{
    ZeroMemory(&g_nid, sizeof(NOTIFYICONDATA));
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;

    // 直接加载图标资源
    g_nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_WINDOW2CLEAR));
    if (!g_nid.hIcon) {
        // 如果无法加载自定义图标，使用默认应用程序图标
        g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }

    swprintf_s(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(wchar_t), L"Window2Clear %s - 窗口透明度控制", APP_VERSION);

    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

// 移除系统托盘图标
void RemoveTrayIcon()
{
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

// 显示右键菜单
void ShowContextMenu(HWND hwnd)
{
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"设置");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");

    POINT pt;
    GetCursorPos(&pt);

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// 显示设置窗口
void ShowSettingsWindow()
{
    if (g_hSettingsWnd) {
        SetForegroundWindow(g_hSettingsWnd);
        return;
    }

    // 注册设置窗口类
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"SettingsWindowClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_WINDOW2CLEAR));

    RegisterClass(&wc);

    // 计算窗口居中位置
    int windowWidth = 380;
    int windowHeight = 420;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;

    // 创建设置窗口
    wchar_t windowTitle[100];
    swprintf_s(windowTitle, 100, L"Window2Clear %s 设置", APP_VERSION);
    g_hSettingsWnd = CreateWindow(
        L"SettingsWindowClass",
        windowTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y,
        windowWidth, windowHeight,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    if (g_hSettingsWnd) {
        ShowWindow(g_hSettingsWnd, SW_SHOW);
        UpdateWindow(g_hSettingsWnd);
    }
}

// 注册全局热键
void RegisterHotKeys(HWND hwnd)
{
    // 注销所有现有热键
    UnregisterHotKeys(hwnd);

    // 根据开关状态注册热键
    if (g_enableTransparencyUp) {
        if (!RegisterHotKey(hwnd, ID_HOTKEY_TRANSPARENCY_UP, g_transparencyUpModifiers, g_transparencyUpKey)) {
            MessageBox(hwnd, L"透明度增加热键注册失败，可能与其他程序冲突", L"警告", MB_OK | MB_ICONWARNING);
        }
    }
    if (g_enableTransparencyDown) {
        if (!RegisterHotKey(hwnd, ID_HOTKEY_TRANSPARENCY_DOWN, g_transparencyDownModifiers, g_transparencyDownKey)) {
            MessageBox(hwnd, L"透明度减少热键注册失败，可能与其他程序冲突", L"警告", MB_OK | MB_ICONWARNING);
        }
    }
    if (g_enableCenter) {
        if (!RegisterHotKey(hwnd, ID_HOTKEY_CENTER_WINDOW, g_centerModifiers, g_centerKey)) {
            MessageBox(hwnd, L"窗口居中热键注册失败，可能与其他程序冲突", L"警告", MB_OK | MB_ICONWARNING);
        }
    }
    if (g_enableShake) {
        if (!RegisterHotKey(hwnd, ID_HOTKEY_SHAKE_WINDOW, g_shakeModifiers, g_shakeKey)) {
            MessageBox(hwnd, L"窗口抖动热键注册失败，可能与其他程序冲突", L"警告", MB_OK | MB_ICONWARNING);
        }
    }
}

// 注销全局热键
void UnregisterHotKeys(HWND hwnd)
{
    UnregisterHotKey(hwnd, ID_HOTKEY_TRANSPARENCY_UP);
    UnregisterHotKey(hwnd, ID_HOTKEY_TRANSPARENCY_DOWN);
    UnregisterHotKey(hwnd, ID_HOTKEY_CENTER_WINDOW);
    UnregisterHotKey(hwnd, ID_HOTKEY_SHAKE_WINDOW);
}

// 调整窗口透明度
void AdjustWindowTransparency(BOOL increase)
{
    HWND hwnd = GetTopMostWindow();
    if (!hwnd || hwnd == g_hMainWnd || hwnd == g_hSettingsWnd) {
        return;
    }

    int currentAlpha = GetWindowTransparency(hwnd);
    int newAlpha;

    if (increase) {
        // 增加透明度（减少alpha值）
        newAlpha = currentAlpha - (255 * g_transparencyStep / 100);
        if (newAlpha < 25) newAlpha = 25; // 最小透明度限制
    }
    else {
        // 减少透明度（增加alpha值）
        newAlpha = currentAlpha + (255 * g_transparencyStep / 100);
        if (newAlpha > 255) newAlpha = 255; // 完全不透明
    }

    SetWindowTransparency(hwnd, newAlpha);
}

// 获取最上层窗口
HWND GetTopMostWindow()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        hwnd = GetWindow(GetDesktopWindow(), GW_CHILD);
    }
    return hwnd;
}

// 设置窗口透明度
void SetWindowTransparency(HWND hwnd, int alpha)
{
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (alpha < 255) {
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd, 0, (BYTE)alpha, LWA_ALPHA);
    }
    else {
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
    }
}

// 获取窗口透明度
int GetWindowTransparency(HWND hwnd)
{
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_LAYERED) {
        BYTE alpha;
        COLORREF colorKey;
        DWORD flags;
        if (GetLayeredWindowAttributes(hwnd, &colorKey, &alpha, &flags)) {
            return (int)alpha;
        }
    }
    return 255; // 默认完全不透明
}

// 窗口居中
void CenterWindow()
{
    HWND hwnd = GetTopMostWindow();
    if (!hwnd || hwnd == g_hMainWnd || hwnd == g_hSettingsWnd) {
        return;
    }

    // 获取屏幕尺寸
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // 获取窗口尺寸
    RECT rect;
    GetWindowRect(hwnd, &rect);
    int windowWidth = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;

    // 计算居中位置
    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;

    // 移动窗口到居中位置
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

// 窗口抖动
void ShakeWindow()
{
    HWND hwnd = GetTopMostWindow();
    if (!hwnd || hwnd == g_hMainWnd || hwnd == g_hSettingsWnd) {
        return;
    }

    // 获取窗口当前位置
    RECT rect;
    GetWindowRect(hwnd, &rect);
    int originalX = rect.left;
    int originalY = rect.top;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    // 抖动效果：快速移动窗口几次
    const int shakeDistance = 5;
    const int shakeTimes = 6;
    const int shakeDelay = 50; // 毫秒

    for (int i = 0; i < shakeTimes; i++) {
        int offsetX = (i % 2 == 0) ? shakeDistance : -shakeDistance;
        int offsetY = (i % 4 < 2) ? shakeDistance : -shakeDistance;

        SetWindowPos(hwnd, NULL, originalX + offsetX, originalY + offsetY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        Sleep(shakeDelay);
    }

    // 恢复到原始位置
    SetWindowPos(hwnd, NULL, originalX, originalY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

// 加载配置
void LoadConfig()
{
    g_transparencyStep = GetPrivateProfileInt(L"Settings", L"TransparencyStep", 10, CONFIG_FILE);

    // 加载热键配置
    g_transparencyUpModifiers = GetPrivateProfileInt(L"Hotkeys", L"TransparencyUpModifiers", MOD_ALT, CONFIG_FILE);
    g_transparencyUpKey = GetPrivateProfileInt(L"Hotkeys", L"TransparencyUpKey", VK_LEFT, CONFIG_FILE);
    g_transparencyDownModifiers = GetPrivateProfileInt(L"Hotkeys", L"TransparencyDownModifiers", MOD_ALT, CONFIG_FILE);
    g_transparencyDownKey = GetPrivateProfileInt(L"Hotkeys", L"TransparencyDownKey", VK_RIGHT, CONFIG_FILE);
    g_centerModifiers = GetPrivateProfileInt(L"Hotkeys", L"CenterModifiers", MOD_CONTROL, CONFIG_FILE);
    g_centerKey = GetPrivateProfileInt(L"Hotkeys", L"CenterKey", VK_NUMPAD5, CONFIG_FILE);
    g_shakeModifiers = GetPrivateProfileInt(L"Hotkeys", L"ShakeModifiers", MOD_ALT, CONFIG_FILE);
    g_shakeKey = GetPrivateProfileInt(L"Hotkeys", L"ShakeKey", VK_DOWN, CONFIG_FILE);

    // 加载热键开关状态
    g_enableTransparencyUp = GetPrivateProfileInt(L"Switches", L"EnableTransparencyUp", 1, CONFIG_FILE);
    g_enableTransparencyDown = GetPrivateProfileInt(L"Switches", L"EnableTransparencyDown", 1, CONFIG_FILE);
    g_enableCenter = GetPrivateProfileInt(L"Switches", L"EnableCenter", 0, CONFIG_FILE);
    g_enableShake = GetPrivateProfileInt(L"Switches", L"EnableShake", 0, CONFIG_FILE);

    // 限制透明度步长范围
    if (g_transparencyStep < 1) g_transparencyStep = 1;
    if (g_transparencyStep > 50) g_transparencyStep = 50;
}

// 保存配置
void SaveConfig()
{
    wchar_t value[32];

    // 保存透明度步长
    swprintf(value, 32, L"%d", g_transparencyStep);
    WritePrivateProfileString(L"Settings", L"TransparencyStep", value, CONFIG_FILE);

    // 保存热键配置
    swprintf(value, 32, L"%d", g_transparencyUpModifiers);
    WritePrivateProfileString(L"Hotkeys", L"TransparencyUpModifiers", value, CONFIG_FILE);
    swprintf(value, 32, L"%d", g_transparencyUpKey);
    WritePrivateProfileString(L"Hotkeys", L"TransparencyUpKey", value, CONFIG_FILE);
    swprintf(value, 32, L"%d", g_transparencyDownModifiers);
    WritePrivateProfileString(L"Hotkeys", L"TransparencyDownModifiers", value, CONFIG_FILE);
    swprintf(value, 32, L"%d", g_transparencyDownKey);
    WritePrivateProfileString(L"Hotkeys", L"TransparencyDownKey", value, CONFIG_FILE);
    swprintf(value, 32, L"%d", g_centerModifiers);
    WritePrivateProfileString(L"Hotkeys", L"CenterModifiers", value, CONFIG_FILE);
    swprintf(value, 32, L"%d", g_centerKey);
    WritePrivateProfileString(L"Hotkeys", L"CenterKey", value, CONFIG_FILE);
    swprintf(value, 32, L"%d", g_shakeModifiers);
    WritePrivateProfileString(L"Hotkeys", L"ShakeModifiers", value, CONFIG_FILE);
    swprintf(value, 32, L"%d", g_shakeKey);
    WritePrivateProfileString(L"Hotkeys", L"ShakeKey", value, CONFIG_FILE);

    // 保存热键开关状态
    swprintf(value, 32, L"%d", g_enableTransparencyUp ? 1 : 0);
    WritePrivateProfileString(L"Switches", L"EnableTransparencyUp", value, CONFIG_FILE);
    swprintf(value, 32, L"%d", g_enableTransparencyDown ? 1 : 0);
    WritePrivateProfileString(L"Switches", L"EnableTransparencyDown", value, CONFIG_FILE);
    swprintf(value, 32, L"%d", g_enableCenter ? 1 : 0);
    WritePrivateProfileString(L"Switches", L"EnableCenter", value, CONFIG_FILE);
    swprintf(value, 32, L"%d", g_enableShake ? 1 : 0);
    WritePrivateProfileString(L"Switches", L"EnableShake", value, CONFIG_FILE);
}

// 获取修饰键名称
wchar_t* GetModifierName(UINT modifiers)
{
    static wchar_t modifierText[64];
    modifierText[0] = L'\0';

    if (modifiers & MOD_CONTROL) {
        wcscat_s(modifierText, 64, L"CTRL");
    }
    if (modifiers & MOD_ALT) {
        if (wcslen(modifierText) > 0) wcscat_s(modifierText, 64, L"+");
        wcscat_s(modifierText, 64, L"ALT");
    }
    if (modifiers & MOD_SHIFT) {
        if (wcslen(modifierText) > 0) wcscat_s(modifierText, 64, L"+");
        wcscat_s(modifierText, 64, L"SHIFT");
    }
    if (modifiers & MOD_WIN) {
        if (wcslen(modifierText) > 0) wcscat_s(modifierText, 64, L"+");
        wcscat_s(modifierText, 64, L"WIN");
    }

    return modifierText;
}

// 获取按键名称
wchar_t* GetKeyName(UINT vkCode)
{
    static wchar_t keyText[32];

    switch (vkCode) {
        // 功能键
    case VK_F1:
        wcscpy_s(keyText, 32, L"F1");
        return keyText;
    case VK_F2:
        wcscpy_s(keyText, 32, L"F2");
        return keyText;
    case VK_F3:
        wcscpy_s(keyText, 32, L"F3");
        return keyText;
    case VK_F4:
        wcscpy_s(keyText, 32, L"F4");
        return keyText;
    case VK_F5:
        wcscpy_s(keyText, 32, L"F5");
        return keyText;
    case VK_F6:
        wcscpy_s(keyText, 32, L"F6");
        return keyText;
    case VK_F7:
        wcscpy_s(keyText, 32, L"F7");
        return keyText;
    case VK_F8:
        wcscpy_s(keyText, 32, L"F8");
        return keyText;
    case VK_F9:
        wcscpy_s(keyText, 32, L"F9");
        return keyText;
    case VK_F10:
        wcscpy_s(keyText, 32, L"F10");
        return keyText;
    case VK_F11:
        wcscpy_s(keyText, 32, L"F11");
        return keyText;
    case VK_F12:
        wcscpy_s(keyText, 32, L"F12");
        return keyText;

        // 方向键
    case VK_LEFT:
        wcscpy_s(keyText, 32, L"LEFT");
        return keyText;
    case VK_RIGHT:
        wcscpy_s(keyText, 32, L"RIGHT");
        return keyText;
    case VK_UP:
        wcscpy_s(keyText, 32, L"UP");
        return keyText;
    case VK_DOWN:
        wcscpy_s(keyText, 32, L"DOWN");
        return keyText;

        // 数字键盘
    case VK_NUMPAD0:
        wcscpy_s(keyText, 32, L"NUM0");
        return keyText;
    case VK_NUMPAD1:
        wcscpy_s(keyText, 32, L"NUM1");
        return keyText;
    case VK_NUMPAD2:
        wcscpy_s(keyText, 32, L"NUM2");
        return keyText;
    case VK_NUMPAD3:
        wcscpy_s(keyText, 32, L"NUM3");
        return keyText;
    case VK_NUMPAD4:
        wcscpy_s(keyText, 32, L"NUM4");
        return keyText;
    case VK_NUMPAD5:
        wcscpy_s(keyText, 32, L"NUM5");
        return keyText;
    case VK_NUMPAD6:
        wcscpy_s(keyText, 32, L"NUM6");
        return keyText;
    case VK_NUMPAD7:
        wcscpy_s(keyText, 32, L"NUM7");
        return keyText;
    case VK_NUMPAD8:
        wcscpy_s(keyText, 32, L"NUM8");
        return keyText;
    case VK_NUMPAD9:
        wcscpy_s(keyText, 32, L"NUM9");
        return keyText;

        // 特殊键
    case VK_INSERT:
        wcscpy_s(keyText, 32, L"INSERT");
        return keyText;
    case VK_DELETE:
        wcscpy_s(keyText, 32, L"DELETE");
        return keyText;
    case VK_HOME:
        wcscpy_s(keyText, 32, L"HOME");
        return keyText;
    case VK_END:
        wcscpy_s(keyText, 32, L"END");
        return keyText;
    case VK_PRIOR:
        wcscpy_s(keyText, 32, L"PAGEUP");
        return keyText;
    case VK_NEXT:
        wcscpy_s(keyText, 32, L"PAGEDOWN");
        return keyText;
    case VK_SPACE:
        wcscpy_s(keyText, 32, L"SPACE");
        return keyText;
    case VK_TAB:
        wcscpy_s(keyText, 32, L"TAB");
        return keyText;
    case VK_RETURN:
        wcscpy_s(keyText, 32, L"ENTER");
        return keyText;
    case VK_ESCAPE:
        wcscpy_s(keyText, 32, L"ESC");
        return keyText;

        // 字母和数字键
    default:
        if (vkCode >= 'A' && vkCode <= 'Z') {
            swprintf_s(keyText, 32, L"%c", (wchar_t)vkCode);
            return keyText;
        }
        if (vkCode >= '0' && vkCode <= '9') {
            swprintf_s(keyText, 32, L"%c", (wchar_t)vkCode);
            return keyText;
        }
        swprintf_s(keyText, 32, L"KEY%d", vkCode);
        return keyText;
    }
}