"""Split effect verification with QUANTIFIED pixel diffs (no eyeballing):
Run: python scripts/effect_probe.py <case>
Cases: tint wash radius circle focus
Each case measures center-region average RGB change caused by the parameter."""
import ctypes, subprocess, sys, time, os
import tkinter as tk
from PIL import ImageGrab

u32 = ctypes.windll.user32
k32 = ctypes.windll.kernel32
u32.GetParent.restype = ctypes.c_void_p
u32.CreateWindowExW.restype = ctypes.c_void_p
BUILD = r"D:\Garage\Software\ccproject\12window2clear\native\build5\crisp_overlay.exe"
OUT = r"D:\Garage\Software\ccproject\12window2clear\probe_shots"
os.makedirs(OUT, exist_ok=True)

class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]

def win_rect(hwnd):
    r = RECT()
    u32.GetWindowRect(ctypes.c_void_p(hwnd), ctypes.byref(r))
    return r.left, r.top, r.right - r.left, r.bottom - r.top

def pump(tkwin, secs):
    end = time.time() + secs
    while time.time() < end:
        tkwin.update()
        time.sleep(0.02)

def center_avg(hwnd, region=(0.3, 0.3, 0.7, 0.7)):
    """Average RGB of the central region of the target window's screen area."""
    x, y, w, h = win_rect(hwnd)
    img = ImageGrab.grab(bbox=(x, y, x + w, y + h)).convert("RGB")
    W, H = img.size
    x0, y0 = int(W * region[0]), int(H * region[1])
    x1, y1 = int(W * region[2]), int(H * region[3])
    px = list(img.getdata())
    sel = [px[y * W + x] for y in range(y0, y1) for x in range(x0, x1)]
    return tuple(sum(c[i] for c in sel) // len(sel) for i in range(3))

def launch(args, target):
    target_hwnd = u32.GetParent(ctypes.c_void_p(target.winfo_id())) or target.winfo_id()
    return subprocess.Popen([BUILD, "0x%08X" % target_hwnd] + args,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL), target_hwnd

def close(proc, target):
    proc.terminate()
    time.sleep(0.5)
    target.destroy()

def run_case(args, target, secs=2.5):
    p, h = launch(args, target)
    pump(target, secs)
    avg = center_avg(h)
    close(p, target)
    return avg

case = sys.argv[1] if len(sys.argv) > 1 else "tint"

if case == "tint":
    # Same wash/radius; tint 0 vs 10 vs 30 vs 60 — quantified visible delta
    base_bg = "#4080C0"  # (64,128,192)
    for tint in (0, 10, 30, 60):
        t = tk.Tk(); t.configure(bg=base_bg); t.geometry("400x300+150+150"); t.update(); t.attributes("-topmost", False); t.lower()
        avg = run_case(["120", str(tint), "10"], t)
        print(f"tint={tint:3d} wash=120 radius=10 -> center avg {avg}")

elif case == "wash":
    base_bg = "#404040"
    for wash in (60, 120, 255):
        t = tk.Tk(); t.configure(bg=base_bg); t.geometry("400x300+150+150"); t.update(); t.attributes("-topmost", False); t.lower()
        avg = run_case([str(wash), "0", "10"], t)
        print(f"wash={wash:3d} tint=0 radius=10 -> center avg {avg}")

elif case == "radius":
    base_bg = "#4080C0"
    for r in (5, 20, 60):
        t = tk.Tk(); t.configure(bg=base_bg); t.geometry("400x300+150+150"); t.update(); t.attributes("-topmost", False); t.lower()
        avg = run_case(["120", "0", str(r)], t)
        print(f"radius={r:3d} wash=120 tint=0 -> center avg {avg}")

elif case == "circle":
    # Mouse over center vs mouse outside: circle boost should change center region
    base_bg = "#4080C0"
    t = tk.Tk(); t.configure(bg=base_bg); t.geometry("400x300+150+150"); t.update(); t.attributes("-topmost", False); t.lower()
    th = u32.GetParent(ctypes.c_void_p(t.winfo_id())) or t.winfo_id()
    x, y, w, h = win_rect(th)
    u32.SetCursorPos(50, 50)  # far away
    p = subprocess.Popen([BUILD, "0x%08X" % th, "120", "0", "10", "60", "200", "30", "60"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pump(t, 2.5)
    avg_out = center_avg(th)
    u32.SetCursorPos(x + w // 2, y + h // 2)  # into the circle
    pump(t, 1.5)
    avg_in = center_avg(th)
    close(p, t)
    print(f"circle: mouse-outside avg {avg_out} -> mouse-inside avg {avg_in}")

elif case == "focus":
    # Same args; target background (another window foreground) vs foreground
    base_bg = "#4080C0"
    t = tk.Tk(); t.configure(bg=base_bg); t.geometry("400x300+150+150"); t.update(); t.attributes("-topmost", False); t.lower()
    other = tk.Tk(); other.geometry("200x150+900+150"); other.update()
    
    pump(other, 0.5)
    avg_bg = run_case(["120", "0", "30"], t, secs=3.5)
    other.destroy()
    t = tk.Tk(); t.configure(bg=base_bg); t.geometry("400x300+150+150"); t.update(); t.attributes("-topmost", False); t.lower()
    
    avg_fg = run_case(["120", "0", "30"], t, secs=3.5)
    print(f"focus: background avg {avg_bg} vs foreground avg {avg_fg}")

print("done")
