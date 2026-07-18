"""
Window2Clear Lite — pure transparency only, no blur overlay.
"""
import ctypes
from ctypes import wintypes
import sys
import os
import threading
import tkinter as tk
from tkinter import ttk

from window2clear.config import load, save, CONFIG_PATH

if sys.stdout is not None:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

# ============================================================
# Windows API
# ============================================================
user32 = ctypes.windll.user32
shell32 = ctypes.windll.shell32
kernel32 = ctypes.windll.kernel32

user32.DefWindowProcW.argtypes = [wintypes.HWND, ctypes.c_uint, wintypes.WPARAM, wintypes.LPARAM]
user32.DefWindowProcW.restype = wintypes.LPARAM

WM_CREATE = 0x0001
WM_DESTROY = 0x0002
WM_HOTKEY = 0x0312
WM_TRAYICON = 0x8001
WM_COMMAND = 0x0111

ID_UP = 1; ID_DOWN = 2; ID_TOGGLE = 3
NIM_ADD = 0; NIM_DELETE = 2
NIF_ICON = 0x00000002; NIF_MESSAGE = 0x00000001; NIF_TIP = 0x00000004
MF_STRING = 0x00000000; MF_SEPARATOR = 0x00000800
ID_SETTINGS = 1001; ID_EXIT = 1002
WS_EX_LAYERED = 0x00080000
GWL_EXSTYLE = -20
LWA_ALPHA = 0x00000002

class NOTIFYICONDATAW(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD), ("hWnd", wintypes.HWND),
        ("uID", ctypes.c_uint), ("uFlags", ctypes.c_uint),
        ("uCallbackMessage", ctypes.c_uint), ("hIcon", wintypes.HICON),
        ("szTip", ctypes.c_wchar * 128), ("dwState", wintypes.DWORD),
        ("dwStateMask", wintypes.DWORD), ("szInfo", ctypes.c_wchar * 256),
        ("uTimeoutOrVersion", ctypes.c_uint), ("szInfoTitle", ctypes.c_wchar * 64),
        ("dwInfoFlags", wintypes.DWORD),
    ]

class Window2ClearLite:
    """System tray + hotkeys — transparency only."""

    def __init__(self):
        self.cfg = load()
        self.step = int(self.cfg["Settings"]["TransparencyStep"])
        self.hwnd = None
        self.running = False
        self._wnd_proc_cb = None
        self._last_alpha: dict[int, int] = {}
        self._modified: set[int] = set()

        self.up_mod = int(self.cfg["Hotkeys"]["TransparencyUpModifiers"])
        self.up_key = int(self.cfg["Hotkeys"]["TransparencyUpKey"])
        self.down_mod = int(self.cfg["Hotkeys"]["TransparencyDownModifiers"])
        self.down_key = int(self.cfg["Hotkeys"]["TransparencyDownKey"])
        self.toggle_mod = int(self.cfg["Hotkeys"]["TransparencyToggleModifiers"])
        self.toggle_key = int(self.cfg["Hotkeys"]["TransparencyToggleKey"])

    def _wnd_proc(self, hwnd, msg, wparam, lparam):
        if msg == WM_CREATE:
            self._add_tray_icon(hwnd); self._register_hotkeys(hwnd); return 0
        elif msg == WM_HOTKEY:
            self._on_hotkey(wparam); return 0
        elif msg == WM_TRAYICON:
            if lparam == 0x0205: self._show_menu(hwnd)
            return 0
        elif msg == WM_COMMAND:
            if wparam == ID_SETTINGS: self._open_settings()
            elif wparam == ID_EXIT: self._shutdown()
            return 0
        elif msg == WM_DESTROY:
            user32.PostQuitMessage(0); return 0
        return user32.DefWindowProcW(hwnd, msg, wparam, lparam)

    def _add_tray_icon(self, hwnd):
        nid = NOTIFYICONDATAW()
        nid.cbSize = ctypes.sizeof(NOTIFYICONDATAW)
        nid.hWnd = hwnd; nid.uID = 1
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP
        nid.uCallbackMessage = WM_TRAYICON
        nid.hIcon = user32.LoadIconW(0, 32512)
        nid.szTip = "Window2Clear Lite"
        shell32.Shell_NotifyIconW(NIM_ADD, ctypes.byref(nid))

    def _remove_tray_icon(self, hwnd):
        nid = NOTIFYICONDATAW()
        nid.cbSize = ctypes.sizeof(NOTIFYICONDATAW)
        nid.hWnd = hwnd; nid.uID = 1
        shell32.Shell_NotifyIconW(NIM_DELETE, ctypes.byref(nid))

    def _register_hotkeys(self, hwnd):
        if self.cfg["Switches"].getboolean("EnableTransparencyUp", True):
            user32.RegisterHotKey(hwnd, ID_UP, self.up_mod, self.up_key)
        if self.cfg["Switches"].getboolean("EnableTransparencyDown", True):
            user32.RegisterHotKey(hwnd, ID_DOWN, self.down_mod, self.down_key)
        if self.cfg["Switches"].getboolean("EnableTransparencyToggle", True):
            user32.RegisterHotKey(hwnd, ID_TOGGLE, self.toggle_mod, self.toggle_key)

    def _on_hotkey(self, hotkey_id):
        hwnd = user32.GetForegroundWindow()
        if not hwnd: return
        style = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
        if not (style & WS_EX_LAYERED):
            user32.SetWindowLongW(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED)

        alpha_byte = ctypes.c_ubyte(255)
        user32.GetLayeredWindowAttributes(hwnd, ctypes.byref(wintypes.COLORREF()),
                                          ctypes.byref(alpha_byte), ctypes.byref(wintypes.DWORD()))
        alpha = alpha_byte.value
        if alpha == 0: alpha = 255

        step = self.step * 255 // 100
        if hotkey_id == ID_UP:
            alpha = max(10, alpha - step)
        elif hotkey_id == ID_DOWN:
            alpha = min(255, alpha + step)
        elif hotkey_id == ID_TOGGLE:
            if alpha < 255:
                self._last_alpha[hwnd] = alpha; alpha = 255
            else:
                alpha = self._last_alpha.get(hwnd, 255 - step * 255 // 100)
                if alpha >= 255: alpha = 255 - step * 255 // 100

        user32.SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA)
        self._modified.add(hwnd)

    def _show_menu(self, hwnd):
        menu = user32.CreatePopupMenu()
        user32.AppendMenuW(menu, MF_STRING, ID_SETTINGS, "Settings")
        user32.AppendMenuW(menu, MF_SEPARATOR, 0, None)
        user32.AppendMenuW(menu, MF_STRING, ID_EXIT, "Exit")
        pt = wintypes.POINT()
        user32.GetCursorPos(ctypes.byref(pt))
        user32.SetForegroundWindow(hwnd)
        user32.TrackPopupMenu(menu, 0, pt.x, pt.y, 0, hwnd, None)
        user32.DestroyMenu(menu)

    def _open_settings(self):
        def _run_dialog():
            root = tk.Tk()
            root.title("Window2Clear Lite — Settings")
            root.geometry("280x180"); root.resizable(False, False)
            frame = ttk.Frame(root, padding=16)
            frame.pack(fill=tk.BOTH, expand=True)
            ttk.Label(frame, text="Transparency step (%)").pack(anchor=tk.W)
            step_var = tk.IntVar(value=self.step)
            ttk.Scale(frame, from_=1, to=20, variable=step_var,
                      orient=tk.HORIZONTAL).pack(fill=tk.X, pady=4)
            step_lbl = ttk.Label(frame, text=f"{self.step}%")
            step_lbl.pack(anchor=tk.E)
            step_var.trace_add("write", lambda *_: step_lbl.config(text=f"{step_var.get()}%"))
            ttk.Separator(frame).pack(fill=tk.X, pady=8)
            ttk.Label(frame, text="ALT+← more transparent\nALT+→ less transparent\nALT+↑ toggle on/off").pack()
            btn = ttk.Frame(frame); btn.pack(side=tk.BOTTOM, fill=tk.X, pady=(12,0))
            def _ok():
                self.step = step_var.get()
                self.cfg["Settings"]["TransparencyStep"] = str(self.step)
                save(self.cfg); root.destroy()
            ttk.Button(btn, text="Save", command=_ok).pack(side=tk.RIGHT, padx=4)
            ttk.Button(btn, text="Cancel", command=root.destroy).pack(side=tk.RIGHT, padx=4)
            root.mainloop()
        threading.Thread(target=_run_dialog, daemon=True).start()

    def _shutdown(self):
        self.running = False
        for hwnd in self._modified:
            try:
                style = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
                if style & WS_EX_LAYERED:
                    user32.SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)
                    user32.SetWindowLongW(hwnd, GWL_EXSTYLE, style & ~WS_EX_LAYERED)
            except Exception: pass
        self._modified.clear()
        if self.hwnd:
            user32.UnregisterHotKey(self.hwnd, ID_UP)
            user32.UnregisterHotKey(self.hwnd, ID_DOWN)
            user32.UnregisterHotKey(self.hwnd, ID_TOGGLE)
            self._remove_tray_icon(self.hwnd)
            user32.DestroyWindow(self.hwnd)

    def run(self):
        self.running = True
        WNDPROC = ctypes.WINFUNCTYPE(wintypes.LPARAM, wintypes.HWND, ctypes.c_uint, wintypes.WPARAM, wintypes.LPARAM)
        class WNDCLASSW(ctypes.Structure):
            _fields_ = [
                ("style", ctypes.c_uint), ("lpfnWndProc", WNDPROC),
                ("cbClsExtra", ctypes.c_int), ("cbWndExtra", ctypes.c_int),
                ("hInstance", wintypes.HINSTANCE), ("hIcon", wintypes.HICON),
                ("hCursor", wintypes.HANDLE), ("hbrBackground", wintypes.HBRUSH),
                ("lpszMenuName", wintypes.LPCWSTR), ("lpszClassName", wintypes.LPCWSTR),
            ]
        self._wnd_proc_cb = WNDPROC(self._wnd_proc)
        hinst = kernel32.GetModuleHandleW(None)
        wc = WNDCLASSW()
        wc.style = 0; wc.lpfnWndProc = self._wnd_proc_cb
        wc.hInstance = hinst; wc.lpszClassName = "Window2ClearLite"
        if not user32.RegisterClassW(ctypes.byref(wc)): return
        self.hwnd = user32.CreateWindowExW(0, "Window2ClearLite", "", 0, 0, 0, 0, 0, 0, 0, hinst, None)
        if not self.hwnd: return
        msg = wintypes.MSG()
        while self.running:
            if user32.GetMessageW(ctypes.byref(msg), 0, 0, 0) <= 0: break
            user32.TranslateMessage(ctypes.byref(msg))
            user32.DispatchMessageW(ctypes.byref(msg))


def main():
    Window2ClearLite().run()

if __name__ == '__main__':
    main()
