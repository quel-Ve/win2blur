# win2dist

Window transparency + Acrylic frosted glass. Zero flicker, portable, no admin.

## Quick start

1. Double-click `win2dist.exe`
2. Click any window, then use the hotkeys:

| Shortcut | Effect |
|----------|--------|
| `ALT + ←` | More transparent |
| `ALT + →` | Less transparent |
| `ALT + ↑` | Toggle transparency on/off |
| `ALT + ↓` | Toggle Acrylic blur |

Right-click the tray icon for settings and exit options.

## Start Menu shortcut

Double-click `install.bat` to add win2dist to the Start Menu.
Double-click `uninstall.bat` to remove it.

## Exit modes

- **Restore && Exit** — undo all effects, clean quit
- **Keep && Exit** — keep effects active, auto-restore on next launch

## Requirements

- Windows 10 (version 1803+) or Windows 11
- No installation, no admin privileges needed

## Uninstall

Delete the `win2dist` folder. If you created a desktop shortcut, run `uninstall.bat` first.

---

win2dist uses the native Windows DWM compositor to create a zero-flicker
acrylic overlay behind your windows — no DLL injection, no system hooks.
