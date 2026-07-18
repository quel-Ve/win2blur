"""
Window2Clear — Windows window transparency controller.
=======================================================
System tray app with global hotkeys for real-time window opacity control.

Usage:
    python main.py                       # Launch system tray app
    python -m window2clear.tray_app      # Same as above
    python -m window2clear.blur          # CLI: apply Acrylic blur (experimental)
    python -m window2clear.blur_test     # Interactive blur mode test (experimental)
    python -m window2clear.frosted_demo  # Win32 frosted glass demo (experimental)
"""
from window2clear.tray_app import Window2Clear


def main():
    app = Window2Clear()
    app.run()


if __name__ == '__main__':
    main()
