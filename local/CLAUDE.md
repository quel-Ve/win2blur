# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

# 12window2clear

Windows 窗口透明度 + 毛玻璃效果工具集。**v2.6 起零外部依赖**：自研 MSVC DLL 注入 dwm.exe 控制模糊半径，DWMBlurGlass 已移除（仅 `local/` 留作参考源码）。

> ⚠️ 本文档前身描述的是 v1.x/v2.5 的 Python + DWMBlurGlass 架构。**当前真相源是项目根 `README.md`** 和 `native/dwm_inject/` 下的代码。以下旧内容保留仅供考古。

---

## 当前架构 (v2.6)

```
12window2clear/
├── native/
│   ├── src/
│   │   ├── tray_app.cpp            # ★ 主程序 — 托盘+热键+透明度+overlay+模糊滑块+Auto-frost 监视线程
│   │   ├── acrylic_overlay.cpp     # Acrylic 叠加层 (零闪主力)
│   │   ├── welcome_demo.cpp        # 启动欢迎窗口
│   │   ├── d3d_backdrop.cpp        # Route E: DDA + CPU blur (实验)
│   │   └── resource.rc / app.ico
│   ├── dwm_inject/
│   │   ├── src/
│   │   │   ├── dllmain_msvc.cpp    # ★ 注入 DLL (MSVC, 真 ID2D1Effect COM)
│   │   │   ├── injector.c          # 注入器 (CreateRemoteThread + 自动卸载)
│   │   │   └── shm_writer.cpp      # 共享内存测试 (已弃用 → 文件 IPC)
│   │   ├── build_msvc.bat          # ★ MSVC 构建脚本
│   │   └── build/                  # injector.exe + libfrosted_dwm.dll
│   └── build5/                     # win2dist.exe 构建目录
├── dist_release_2.6/               # ★ 发行版 (5 文件, 零依赖)
├── local/                          # 参考: DWMBlurGlass / Window2Clear
├── window2clear/                   # Python v1.x 模块 (参考)
└── README.md                       # ★ 项目文档真相源
```

## 模糊半径链路 (v2.6)

```
win2dist Settings 滑块 (2/4/8/12/15/20/30/40/50)
  → 写 C:\Temp\frosted_dwm_config.txt
  → dwm.exe 内 DLL 每次 BuildEffect 读文件
  → DirBlurKernelX/Y->SetValue(0, radius)
```

**关键技术事实**（踩坑记录，见 brief.md v2.6 节）：
1. PDB 符号查找不可行 — `CCustomBlur::*` 是私有符号
2. DWMBlurGlass 偏移来自 `.DWMBlur` 共享段（GUI 启动才填充）
3. 偏移硬编码：BuildEffect +0x40380 / DetermineOutputScale +0x40304 / FillEffect +0xCE6C0 / DirBlurKernelX/Y 成员 +0x30/+0x38（Win10 19045）
4. MinGW 手写 vtable 失败 → 必须 MSVC 编译
5. `Local\`/`Global\` 命名共享内存被 dwm (SYSTEM/PPL) 拒 (err=5) → 文件 IPC
6. SetValue 必须在**每次** BuildEffect 执行（不能有调用次数限制）

## 构建

```bash
# MSVC DLL (需 VS Build Tools, D:\Program Files\VSBuildTools)
native/dwm_inject/build_msvc.bat

# win2dist.exe (MinGW)
cd native/build5 && cmake --build .
# 然后复制 injector.exe + libfrosted_dwm.dll 到 build5/
```

## 运行

```bash
native/build5/win2dist.exe
# 首次启动 UAC 一次 (注入 dwm.exe)
```

## 快捷键

| 热键 | 功能 |
|------|------|
| ALT+← / ALT+→ | 透明度 +/- step% |
| ALT+↑ | 透明度切换开/关 |
| ALT+↓ | Acrylic 模糊切换 |
| 托盘右键 → Settings | 步长 + 模糊半径滑块 (Candara 字体, Apply 不关窗) |

### Auto-frost (v2.7)

- **30s 轮询**：后台监视线程每 30s 枚举全部可见窗口，匹配 `config.ini [AutoFrost]` 节中的应用（8 项预置：Obsidian/WindowsTerminal/msedge/explorer+`CabinetWClass`/cloudmusic/WeChat/CherryStudio/Code），自动应用默认效果（`DefaultAlpha` 透明 + `DefaultBlur` 模糊 overlay）
- **每窗口一次**：已处理窗口记入 `g_modified`，之后轮询跳过（手动调回 100% 不会被抢回）；关窗由 `prune_stale` 清理，重开再应用
- **配置热读**：每轮轮询在锁内重新 `load_autofrost()` 再快照，改 Settings/ini 无需重启即生效；格式 `App_N=exe|cls`（`|` 后为可选类名）
- **Settings 管理**：Enable auto-frost 开关、Default transparency 滑块（50-100%）、Blur 勾选、应用列表 Add current（当前前台窗口）/ Remove，Apply 写回 config 且不关窗
- **线程安全**：`g_fxLock` 临界区保护监视线程与 Settings/热键路径对 `g_modified`/`g_overlays`/`g_auto` 的并发访问（无死锁：锁内零窗口 API 调用）

---

## 旧架构 (v2.5 及以前 — 仅供参考)

### 运行（Python 版已废弃）

```bash
& "D:\Program Files\python\python1210\python.exe" main.py
```

### 模糊 API 路径 (历史)

| 路径 | API | 用于 | 依赖 | 闪烁 |
|------|-----|------|------|------|
| **A — 透明度** | `SetLayeredWindowAttributes` | `tray_app.py` | 无 | 零 |
| **C — Acrylic 模糊** | `SetWindowCompositionAttribute` | `tray_app.py`, `blur.py` | 无 (Win10+) | 零 |
| **E — DDA 捕获** | Desktop Duplication + CPU blur | `native/d3d_backdrop.cpp` | GPU+D3D11 | 1帧 |
| **F — DWM Hook** | MinHook + 符号劫持 | `native/dwm_inject/` | Admin | 零 |

v2.5 曾依赖 DWMBlurGlass (config.ini + GUI 重启) 控制模糊半径 — 已废弃。
