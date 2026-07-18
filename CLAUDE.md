# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

# 12window2clear

Windows 窗口透明度 + 毛玻璃效果工具集。纯 Python + ctypes（零外部依赖），辅以原生 C++ 组件提供系统级模糊。

搭配 [DWMBlurGlass](https://github.com/Maplespe/DWMBlurGlass) 实现标题栏模糊（系统级 DWM 注入）。

---

## 运行

```bash
# 托盘应用（主入口 — 透明度控制 + 全局热键）
& "D:\Program Files\python\python1210\python.exe" main.py

# 毛玻璃 CLI（独立于托盘应用）
& "D:\Program Files\python\python1210\python.exe" -m window2clear.blur --title "Obsidian"
& "D:\Program Files\python\python1210\python.exe" -m window2clear.blur --list

# 交互式模糊测试（键盘切换模糊模式，实时预览）
& "D:\Program Files\python\python1210\python.exe" -m window2clear.blur_test

# 后台自动模糊服务（激活窗口停稳后自动模糊）
& "D:\Program Files\python\python1210\python.exe" -m window2clear.acrylic_service

# 原生 D3D11 毛玻璃演示窗口
& "D:\Program Files\python\python1210\python.exe" -m window2clear.frosted_demo
```

> **注意**：`acrylic_blur.py`（根目录）与 `window2clear/blur.py` 功能完全相同——是后者的独立副本，用于不依赖 `window2clear` 包即可运行的场景。修改其中之一时需同步另一个。

---

## 架构

```
main.py                          → 入口（一行：import tray_app, run）
window2clear/
  __init__.py                    → 包说明
  tray_app.py                    → 系统托盘 + 全局热键 + 设置对话框（tkinter）
  transparency.py                → SetLayeredWindowAttributes 封装（透明度的核心 API）
  config.py                      → config.ini 读写
  window_utils.py                → 窗口枚举工具（EnumWindows 回调）

  blur.py                        → SetWindowCompositionAttribute CLI（无文档记录的 API）
  blur_test.py                   → 交互式模糊模式测试（1-7 键切换预设）
  frosted_demo.py                → 纯 Win32 毛玻璃演示窗口（绕过 tkinter 不透明限制）
  acrylic_service.py             → 后台自动模糊服务（WinEvent hook + settle 延迟）

native/
  src/d3d_backdrop.cpp           → Route E: D3D11 Desktop Duplication + CPU box blur
  build/blur_backdrop.exe        → 编译产物
  dwm_inject/
    src/dllmain.cpp              → DWM hook DLL（MinHook + SymFromName，挂钩 DWM 内部模糊）
    src/injector.c               → DLL 注入器
    src/test_inject.py           → 注入测试脚本
    build/libfrosted_dwm.dll     → 编译产物
    build/injector.exe           → 编译产物
```

### 模糊 API 路径

| 路径 | API | 用于 | 依赖 | 闪烁 |
|------|-----|------|------|------|
| **A — 透明度** | `SetLayeredWindowAttributes` | `tray_app.py` | 无 | 零 |
| **C — Acrylic 模糊** | `SetWindowCompositionAttribute` | `tray_app.py`, `blur.py`, `blur_controller.py` | 无 (Win10+) | 零 |
| **E — DDA 捕获** | Desktop Duplication + CPU blur | `native/d3d_backdrop.cpp` (实验) | GPU+D3D11 | 1帧 |
| **F — DWM Hook** | MinHook + 符号劫持 | `native/dwm_inject/` (实验) | Admin | 零 |

路径 A + C = 零闪毛玻璃（半透明窗口 + Acrylic 背景模糊）。主力方案。

DWMBlurGlass (第三方 DLL 注入) 作为可选高级引擎：提供自定义模糊半径，通过 `config.ini` 控制。需要 admin 安装一次，之后 DLL 常驻 dwm.exe。

---

## 原生组件构建

```bash
# blur_backdrop.exe — D3D11 桌面捕获 + CPU 模糊
cd native/build && cmake -G "MinGW Makefiles" .. && make

# libfrosted_dwm.dll + injector.exe — DWM hook 注入
cd native/dwm_inject/build && cmake -G "MinGW Makefiles" .. && make
```

---

## DWMBlurGlass 配置约定

| 场景 | 效果类型 | 原因 |
|------|---------|------|
| **发行版** | **Acrylic** | 最好看，Win10 Fluent Design 风格 |
| **开发/测试** | **Blur** | 轻量，闪烁最少，不干扰调试 |

模糊实际由 DWMBlurGlass GUI 控制，我们的 Python 工具通过 `DwmEnableBlurBehindWindow` 触发（路径 B）。发行版打包时附带 DWMBlurGlass 安装程序和预设配置。

---

## 核心 API

```python
from window2clear.transparency import set_opacity_percent

set_opacity_percent(hwnd, 85)  # → SetLayeredWindowAttributes(hwnd, 0, 217, LWA_ALPHA)
```

---

## 快捷键

| 热键 | 功能 |
|------|------|
| ALT+← | 增加透明度（-step%） |
| ALT+→ | 减少透明度（+step%） |
| ALT+↑ | 切换开关（不透明 ↔ 上次透明值） |
| ALT+↓ | 切换亚克力模糊 |
| 托盘右键 → 设置 | 调整步长（1-20%），查看当前快捷键绑定 |
| 托盘右键 → 退出 | 恢复所有已修改窗口为不透明，移除模糊，退出 |

---

## 配置

`config.ini`（自动创建，与 exe 同目录）：

```ini
[Settings]
TransparencyStep=5

[Hotkeys]
TransparencyUpModifiers=1    ; ALT
TransparencyUpKey=37         ; VK_LEFT
TransparencyDownModifiers=1  ; ALT
TransparencyDownKey=39       ; VK_RIGHT
TransparencyToggleModifiers=1; ALT
TransparencyToggleKey=38     ; VK_UP
AcrylicToggleModifiers=1     ; ALT
AcrylicToggleKey=40          ; VK_DOWN

[Switches]
EnableTransparencyUp=1
EnableTransparencyDown=1
EnableTransparencyToggle=1
EnableAcrylicToggle=1
```

修饰键编码：1=ALT, 2=CTRL, 4=SHIFT, 8=WIN。虚拟键码为标准 Windows VK_* 值。

---

## 依赖

零外部 Python 依赖。ctypes 调 Win32 API，tkinter 为 Python 自带。

原生组件需要 MinGW-w64 + CMake + D3D11/MinHook。
