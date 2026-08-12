"""Multi-overlay kill isolation: two crisp overlays on two targets; kill one by
its exact PID (as the fixed tray kill_overlay does); the other must survive.
Regression test for the v3.0 crash cascade (FindWindowW matched the FIRST
overlay of the class globally, so killing one window's overlay closed another's
-> crash-guard relaunch loop -> 3x/30s fuse -> windows stuck hidden)."""
import ctypes, subprocess, sys, time
import tkinter as tk

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
BUILD = r"D:\Garage\Software\ccproject\12window2clear\native\build5\crisp_overlay.exe"

def proc_alive(pid):
    h = kernel32.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
    if not h: return False
    code = ctypes.c_ulong()
    kernel32.GetExitCodeProcess(h, ctypes.byref(code))
    kernel32.CloseHandle(h)
    return code.value == 259  # STILL_ACTIVE

def pump(tkwin, secs):
    end = time.time() + secs
    while time.time() < end:
        tkwin.update()
        time.sleep(0.02)

def overlay_of(pid):
    found = []
    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def cb(h, lp):
        wpid = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(h, ctypes.byref(wpid))
        if wpid.value == pid: found.append(h)
        return True
    user32.EnumWindows(cb, 0)
    return found

ok = True
t1 = tk.Tk(); t1.geometry("300x200+100+100"); t1.update()
t2 = tk.Tk(); t2.geometry("300x200+500+100"); t2.update()
h1 = user32.GetParent(ctypes.c_void_p(t1.winfo_id())) or t1.winfo_id()
h2 = user32.GetParent(ctypes.c_void_p(t2.winfo_id())) or t2.winfo_id()
time.sleep(0.3)
p1 = subprocess.Popen([BUILD, "0x%08X" % h1, "120"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
p2 = subprocess.Popen([BUILD, "0x%08X" % h2, "120"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
pump(t1, 2.0); pump(t2, 0.1)
print("alive: t1=%s t2=%s" % (proc_alive(p1.pid), proc_alive(p2.pid)))
if not (proc_alive(p1.pid) and proc_alive(p2.pid)): ok = False; print("FAIL: both should start alive")

ov1 = overlay_of(p1.pid); ov2 = overlay_of(p2.pid)
print("overlay windows: t1=%d t2=%d" % (len(ov1), len(ov2)))
if not (len(ov1) >= 1 and len(ov2) >= 1): ok = False; print("FAIL: at least 1 overlay window each")

# The fixed kill path: WM_CLOSE to THIS window's overlay only (by PID).
for h in ov1: user32.PostMessageW(h, 0x0010, 0, 0)   # WM_CLOSE
time.sleep(1.2)
a1, a2 = proc_alive(p1.pid), proc_alive(p2.pid)
print("after kill t1: t1=%s (want False) t2=%s (want True)" % (a1, a2))
if a1: ok = False; print("FAIL: t1 overlay must exit")
if not a2: ok = False; print("FAIL: t2 overlay must SURVIVE — the fix")

for h in ov2: user32.PostMessageW(h, 0x0010, 0, 0)
time.sleep(0.8)
t1.destroy(); t2.destroy()
print("RESULT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
