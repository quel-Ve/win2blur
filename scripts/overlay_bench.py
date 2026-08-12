"""Overlay test bench — drive crisp_overlay.exe directly against a target
window, bypassing tray/AutoFrost/crash-guard entirely. Any crash only affects
the test window.

Usage:
  python scripts/overlay_bench.py list                    # list candidate windows
  python scripts/overlay_bench.py start <hwnd> <wash> <tint> <radius> [circle] [boost] [band] [focus]
  python scripts/overlay_bench.py stop                    # stop the bench overlay
  python scripts/overlay_bench.py restart <args...>       # stop + start with new args
"""
import ctypes, subprocess, sys, time, os
from ctypes import wintypes

u32 = ctypes.windll.user32
k32 = ctypes.windll.kernel32
u32.GetClassNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]
u32.GetWindowTextW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]
u32.IsWindowVisible.argtypes = [ctypes.c_void_p]
u32.GetParent.argtypes = [ctypes.c_void_p]
u32.GetWindowTextLengthW.argtypes = [ctypes.c_void_p]
u32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]
u32.GetWindowTextLengthW.restype = ctypes.c_int
BUILD = r"D:\Garage\Software\ccproject\12window2clear\native\build5\crisp_overlay.exe"
PROC = None  # current overlay process


def exe_of(h):
    p = wintypes.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p))
    hp = k32.OpenProcess(0x1000, False, p.value)
    if not hp: return ""
    buf = ctypes.create_unicode_buffer(260); sz = ctypes.c_ulong(260)
    k32.QueryFullProcessImageNameW(hp, 0, buf, ctypes.byref(sz))
    k32.CloseHandle(hp)
    return buf.value.lower().rsplit("\\", 1)[-1]


def title_of(h):
    n = u32.GetWindowTextLengthW(h)
    if n <= 0: return ""
    buf = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, buf, n + 1)
    return buf.value


def cls_of(h):
    buf = ctypes.create_unicode_buffer(64)
    u32.GetClassNameW(h, buf, 64)
    return buf.value


def cmd_list():
    rows = []
    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def cb(h, lp):
        if not u32.IsWindowVisible(h): return True
        if u32.GetParent(h): return True
        exe = exe_of(h)
        if exe in ("explorer.exe", "notepad.exe", "cmd.exe", "windowsterminal.exe", "mspaint.exe"):
            rows.append((hex(h), exe, cls_of(h), title_of(h)))
        return True
    u32.EnumWindows(cb, 0)
    if not rows:
        print("(no candidate windows found — open a File Explorer window)")
        return
    for h, exe, cls, title in rows:
        print(f"{h}  {exe:22s} cls={cls:24s} \"{title}\"")


def cmd_start(args):
    global PROC
    if not args: print("usage: start <hwnd> <wash> <tint> <radius> [circle boost band focus]"); return
    hwnd = args[0]
    params = args[1:]
    if PROC and PROC.poll() is None:
        PROC.terminate(); time.sleep(0.4)
    cmd = [BUILD, hwnd] + params
    PROC = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.6)
    alive = PROC.poll() is None
    print(f"overlay started for {hwnd} args={params} -> {'ALIVE' if alive else 'DIED IMMEDIATELY'}")
    if not alive:
        print("  (if it died: capture failed or window unusable — try another window)")


def cmd_stop():
    global PROC
    if PROC and PROC.poll() is None:
        PROC.terminate(); time.sleep(0.4)
        print("overlay stopped")
    else:
        print("(no overlay running)")
    PROC = None


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    op = sys.argv[1]
    if op == "list": cmd_list()
    elif op == "start": cmd_start(sys.argv[2:])
    elif op == "stop": cmd_stop()
    elif op == "restart":
        cmd_stop(); cmd_start(sys.argv[2:])
    else:
        print(__doc__)
