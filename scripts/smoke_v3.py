"""v3.0 smoke test: launch crisp_overlay against a tkinter window, assert lifecycle."""
import ctypes, subprocess, sys, time, os
import tkinter as tk
user32 = ctypes.windll.user32
# 64-bit HWNDs: without restype, FindWindowW/GetParent return truncated 32-bit ints
user32.FindWindowW.restype = ctypes.c_void_p
user32.GetParent.restype = ctypes.c_void_p
user32.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
CRISP_CLS = "CrispOverlayClass"
BUILD = os.path.join(os.path.dirname(__file__), "..", "native", "build5", "crisp_overlay.exe")

def find_overlay():
    return user32.FindWindowW(CRISP_CLS, None)

def pump(target, seconds):
    """Sleep while pumping the tk message queue.

    crisp's dual-hide capture calls ShowWindow(target, SW_HIDE) synchronously,
    which blocks until the target thread pumps messages — a non-pumping target
    hangs crisp forever. All tkinter calls stay on the main thread.
    """
    deadline = time.time() + seconds
    while time.time() < deadline:
        target.update()
        time.sleep(0.02)

def close_case(proc, overlay, target):
    """WM_CLOSE then 0.5s grace; hard-kill only if still alive after the poll."""
    if overlay: user32.PostMessageW(overlay, 0x0010, 0, 0)  # WM_CLOSE
    if proc.poll() is None:
        time.sleep(0.5)  # grace: let the WM_CLOSE shutdown path exit naturally
        if proc.poll() is None: proc.terminate()
    target.destroy()

def run_case(args, expect_alive=True, wait=1.5, hwnd_override=None):
    target = tk.Tk(); target.geometry("300x200+100+100"); target.update()
    target_hwnd = user32.GetParent(ctypes.c_void_p(target.winfo_id())) or target.winfo_id()
    hwnd_arg = hwnd_override if hwnd_override is not None else "0x%08X" % target_hwnd
    proc = subprocess.Popen([BUILD, hwnd_arg] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    pump(target, wait)
    overlay = find_overlay()
    alive = proc.poll() is None
    ok = (alive == expect_alive)
    print(("PASS" if ok else "FAIL"), "case", args, "overlay=%s alive=%s" % (bool(overlay), alive))
    close_case(proc, overlay, target)
    return ok

def run_move_case(args, wait=1.5, move_wait=1.0):
    """Target window moves programmatically mid-flight; overlay must follow and stay alive."""
    target = tk.Tk(); target.geometry("300x200+100+100"); target.update()
    target_hwnd = user32.GetParent(ctypes.c_void_p(target.winfo_id())) or target.winfo_id()
    proc = subprocess.Popen([BUILD, "0x%08X" % target_hwnd] + args,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    pump(target, wait)                                     # initial capture cycle
    target.geometry("300x200+300+300"); target.update()    # move between captures
    pump(target, move_wait)                                # settle + recapture cycle
    overlay = find_overlay()
    alive = proc.poll() is None
    ok = alive  # overlay must survive the move + recapture cycle
    print(("PASS" if ok else "FAIL"), "case", args, "(move case) overlay=%s alive=%s" % (bool(overlay), alive))
    close_case(proc, overlay, target)
    return ok

def run_mouse_case(args, wait=1.5):
    """Cursor placed over the target from launch; circle-tracking must keep overlay alive.

    SetCursorPos before the pump: the overlay's very first ticks see the
    cursor inside the target rect, so the circle activates during the initial
    capture + settle cycle. Assertion is overlay-alive (the tracking/fade code
    must not crash the poll loop).
    """
    target = tk.Tk(); target.geometry("300x200+100+100"); target.update()
    target_hwnd = user32.GetParent(ctypes.c_void_p(target.winfo_id())) or target.winfo_id()
    proc = subprocess.Popen([BUILD, "0x%08X" % target_hwnd] + args,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    user32.SetCursorPos(200, 200)                          # inside target rect (100,100..400,300)
    pump(target, wait)                                     # capture + circle-tracking cycles
    overlay = find_overlay()
    alive = proc.poll() is None
    ok = alive  # overlay must survive cursor-tracking renders
    print(("PASS" if ok else "FAIL"), "case", args, "(mouse case) overlay=%s alive=%s" % (bool(overlay), alive))
    close_case(proc, overlay, target)
    return ok

if __name__ == "__main__":
    results = [
        run_case(["120"]),                           # pure 2-arg back-compat (hwnd + wash)
        run_case(["120", "15"]),                     # 3-arg (hwnd + wash + tint)
        run_case(["120", "20", "15", "50", "200", "30", "60"]),  # wash tint radius circle boost band focus
        run_case(["120", "20", "40", "50", "200", "30", "60"]),  # circle 50px active (radius 40)
        run_case(["120", "15"], expect_alive=False, hwnd_override="0x0"),  # invalid hwnd -> exit
        run_move_case(["120", "15"]),                # move target -> overlay survives recapture
        run_mouse_case(["120", "20", "40", "50", "200", "30", "60"]),  # cursor over target -> circle tracking
    ]
    print("ALL PASS" if all(results) else "SOME FAILED")
    sys.exit(0 if all(results) else 1)
