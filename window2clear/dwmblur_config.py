"""
Control DWMBlurGlass blur radius programmatically.
Writes config.ini and sends WM_APP+20 Refresh to the extension DLL in dwm.exe.
"""
import ctypes
import configparser
import os
from ctypes import wintypes

WM_APP = 0x8000
WM_APP_20 = WM_APP + 20
HWND_MESSAGE = -3  # HWND_MESSAGE

# MHostNotifyType enum from DWMBlurGlass Common.h
REFRESH = 0
SHUTDOWN = 1
ENABLE_TRANSPARENCY = 2

CONFIG_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "local", "DWMBlurGlass.2.3.2_Beta3_x64", "Release", "data", "config.ini"
)
EXT_NOTIFY_CLASS = "MDWMBlurGlassExtNotify"

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)


def restart_dwmblurglass_gui():
    """Kill and relaunch DWMBlurGlass GUI to apply config changes."""
    import subprocess
    GUI_EXE = os.path.join(
        os.path.dirname(CONFIG_PATH), "..", "DWMBlurGlassGUI.exe"
    )
    GUI_EXE = os.path.normpath(GUI_EXE)
    subprocess.run(["taskkill", "/f", "/im", "DWMBlurGlassGUI.exe"],
                   capture_output=True)
    subprocess.Popen([GUI_EXE])
    print("[dwmblur_config] DWMBlurGlass GUI restarted — config applied")


def set_blur_radius(radius: float):
    """Set global blur radius and trigger DWMBlurGlass refresh."""
    # 1. Write config
    cfg = configparser.ConfigParser()
    if os.path.exists(CONFIG_PATH):
        cfg.read(CONFIG_PATH)
    if "config" not in cfg:
        cfg["config"] = {}
    cfg["config"]["customamount"] = "true"
    cfg["config"]["custombluramount"] = str(int(radius))
    cfg["config"]["bluramount"] = str(int(radius))
    cfg["config"]["blurmethod"] = "0"  # CustomBlur
    with open(CONFIG_PATH, "w") as f:
        cfg.write(f)
    print(f"[dwmblur_config] Config written: custombluramount={int(radius)}")

    # 2. Restart GUI to apply config
    restart_dwmblurglass_gui()
    return True


def get_current_radius() -> float:
    """Read current custombluramount from config.ini."""
    cfg = configparser.ConfigParser()
    if not os.path.exists(CONFIG_PATH):
        return 30.0
    cfg.read(CONFIG_PATH)
    try:
        return cfg.getfloat("config", "custombluramount", fallback=30.0)
    except ValueError:
        return 30.0


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1:
        r = float(sys.argv[1])
        set_blur_radius(r)
    else:
        print(f"Current: {get_current_radius()}")
        print("Usage: python dwmblur_config.py <radius>")
