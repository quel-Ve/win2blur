# 不同窗口不同模糊半径 — 现状、困境、已尝试方案与反馈

> 日期：2026-08-12。目的：攻克「不同窗口不同模糊半径」前的现状盘点（环境/通路/困境/历史尝试），供方案设计使用。
> 相关：[[2026-08-09-crisp-acrylic-dilemma]]（保字+模糊大困境，路线 A-K）、`v2.7-vs-v3.0-features.md`（42 项功能矩阵）

## 目标（2026-08-12 用户定义）

不同窗口（不同应用）可以设置**不同模糊半径**——如 Obsidian r=12、Cherry Studio r=40、记事本 r=5——而非当前全局一个值。

## 系统与软件环境

| 项 | 值 |
|----|----|
| OS | Windows 10 Pro 10.0.19045 |
| 工具链 | C++17，MinGW-w64 (g++ 13/16, winlibs UCRT)，CMake，零第三方依赖 |
| 控制对象 | 任意第三方窗口（不可改对方渲染代码） |
| 项目 | 12window2clear（2026-08-12 改名 win2blur；repo `quel-Ve/win2blur`） |
| 版本线 | v2.7 已发布（GitHub Release，win2blur.exe）；v2.8 = 本地运行版（dist，2026-08-12 部署）；v3.0 = 开发中（native/src） |

## 两种效果模式的半径通路（源码证据，2026-08-12 探索代理分析）

### Crisp 模式（CPU 模糊引擎）—— ✅ 每窗口独立半径已实现

```
tray_app.cpp:770-773   launch_overlay() 传 argv:  <hwnd> <wash> <tint> <radius> <circle...>
                         radius 从 g_winAlpha[hwnd].radius 读取（每窗口 map）
crisp_overlay.cpp:672  g_radius = atoi(argv[4])，clamp [1,120]
crisp_overlay.cpp:357  baseR = (int)(g_radius * g_curFocusF)   // 焦点差异化：失焦 ×0.6
```

- 每个窗口 = 独立 crisp_overlay 进程 = 独立 `g_radius`（文件作用域静态）→ **每窗口半径天然独立**
- 半径变化走缓存帧引擎：`g_renderDirty=1` → 从缓存重模糊，**不重新捕获、不闪烁**
- 局限：CPU 模糊；背景是捕获帧（动态壁纸不实时）；双隐藏捕获的竞态（v2.8 遗留，v3.0 已用缓存帧缓解）

### Acrylic 模式（DWM 原生）—— ❌ 半径全局，无法按窗口

```
tray_app.cpp:45-46     g_blurPresets[] = {2,4,8,12,15,20,30,40,50}   // 全局滑块 0-8
tray_app.cpp:384-392   set_blur_radius(idx) → 写 C:\Temp\frosted_dwm_config.txt
dllmain_msvc.cpp:23    static float g_blurRadius = 30.0f;            // DLL 内单一全局变量
dllmain_msvc.cpp:32-54 ReadBlurFromFile() → 每次 BuildEffect/FillEffect 读文件（200ms 节流）
dllmain_msvc.cpp:102   kx->SetValue(0, g_blurRadius)                 // 写进所有效果内核
```

- DLL 注入 dwm.exe 一次，hook `CCustomBlur::BuildEffect` / `CD2DContext::FillEffect`
- **无窗口归属概念**：所有启用 acrylic 的窗口共用同一个 `g_blurRadius`
- `acrylic_overlay.exe` 命令行只有 `<hwnd> <tint>`，**无半径参数**（tray_app.cpp:775-777）
- profile 里 radius>0 在 acrylic 路径语义 = 「要模糊」（wantBlur 开关），不是半径值（tray_app.cpp:579-586 注释明确 "radius is DWM-fixed — documented limit"）

## 无法实现的困境（为什么 Acrylic 路径做不了每窗口半径）

1. **DWM 合成器的模糊半径是全局状态**：dwm.exe 一个进程服务整个桌面，`BuildEffect` 调用不携带窗口身份（Route G 实测结论：`FillEffect` 无窗口归属概念）
2. **DWM 只为「声明了 Acrylic 的窗口」做模糊**：普通不透明窗口没有透明区域可填材质——即使按窗口区分了效果参数，第三方窗口本体仍是 100% 不透明 RGB（无背景层概念，见 Route H）
3. **CVisual 反查（Route J）**：dwm.exe 内 visual 树反查窗口句柄（+0xE30F0）理论上可做到按窗口指定效果——**未验证、高风险**（dwm 崩溃 = 黑屏 + explorer 重启；私有结构随系统更新变化），v3.0 设计文档明确排除
4. **Windows 安全边界**：跨进程 SetWindowDisplayAffinity（WDA_EXCLUDEFROMCAPTURE）= 错误 5（High IL 也拒，所有权限制）；Magnification 回调 = 错误 50（系统放大镜专用）——「零隐藏捕获背景」原语对第三方窗口不可用

## 已尝试的解决方案与反馈

| # | 方案 | 结果 | 反馈/结论 |
|---|------|------|-----------|
| G | DWM 注入改半径（Route F 现状） | ⚠️ 部分可行 | 能改**所有**窗口半径（全局），无法按窗口；是 v2.7/v2.8 的半径滑块基础 |
| J | CVisual 反查按窗口控制 | ❌ 未尝试（高风险） | v3.0 设计排除；列为最后手段 |
| — | Crisp 每窗口半径（v3.0 缓存帧引擎） | ✅ 已实现 | probe 实测半径 5/20/60 量化差异正确（verification-plan）；用户对 Crisp 保字效果认可 |
| — | 双隐藏替代（LWA_ALPHA=0） | ✅ 部分成功 | 任务栏排序不再乱、自模糊消除；拖动停止闪几帧 + 动态壁纸刷新闪——v3.0 缓存帧引擎缓解 |
| — | 焦点差异化 ease | ✅ 已实现 | baseR = radius × focusFactor，300ms ease；失焦窗口自动降档（×0.6） |
| — | 鼠标跟随模糊圆 | ✅ 已实现 | **同一窗口内**双半径 radial mask（圆内 ×2）——窗口内半径差异已有先例 |
| K | inpainting 背景重建 | ❌ 未尝试 | 零权限兜底候选，未到优先级 |
| — | 半径 0 语义 | ❌ 设计限制 | profile radius 解析 [1,120]，0 → -1（继承全局）；「此窗口无模糊」不可表达 |

## 候选攻击方向（2026-08-12，待 brainstorm）

1. **前台窗口动态切换全局 DWM 半径**（tray 侧调度，零 DWM 风险）
   - 每窗口 profile 半径记录在 g_winAlpha；前台切换时 `set_blur_radius()` 写全局
   - 同一时刻只有一个前台窗口被注意 → 视觉等效「每窗口不同半径」
   - 代价：后台 acrylic 窗口半径跟着变（可接受性待用户实测）
2. **Crisp 承接全部半径差异化**：radius 差异化 profile 强制走 crisp（CPU 每窗口半径），acrylic 仅承担透明
   - 已有实现，改动 = profile 路由策略；代价 = 多窗口 CPU 成本、背景帧特性
3. **Route J DWM visual 树反查**：真正按窗口控制 DWM 效果；风险最高（黑屏），列为最后手段
4. **混合**：Acrylic 前台窗口用方案 1，radius>N 的重度模糊窗口自动切 Crisp

## 验证基线（probe 数据，v3.0-verification-plan）

- 锐度指标：baseline 24.4 → wash39 r5 = 8.1 → wash255 r60 = 1.1（半径/强度量化正常）
- probe_shots/：tint0/50 + w0-255 组合截图（含 `w0_t0_ANOMALY.png` 待复查）

---

# 探讨结论（2026-08-12 晚）：按窗口半径路线推演 — 进展笔记

> 本节为探讨性进展记录（**只探讨，不实施、不测试**，用户 2026-08-12 明确）。上文为事实盘点，本节为判断与结论。

## 需求确认过程（三个决策）

1. **痛点 = Acrylic 模式按 app 分半径**——Crisp 模式已每窗口独立半径（非痛点）
2. **后台窗口必须各自保持自己的半径**——直接否决「前台窗口动态切换全局 DWM 半径」方案
3. 原定「探测先行两阶段」→ 当晚改为**只探讨总结，不实施**（本篇即产出）

## 方案排除分析（含本次与历史）

| 方案 | 排除原因 |
|------|----------|
| ❌ 前台窗口动态切换全局 DWM 半径 | 全局机制下后台窗口必然跟随前台值——与「各自保持」硬冲突（用户否决） |
| ❌ WDA_EXCLUDEFROMCAPTURE（跨进程） | 错误 5，所有权限制，High IL 也拒——「零隐藏捕获背景」原语对第三方窗口不可用 |
| ❌ Magnification API 排除捕获 | 错误 50，回调系统放大镜专用 |
| ❌ LWA_COLORKEY 抠色 | 现代渲染（DirectComposition/硬件加速）路径无效 |
| ⚠️ Route G（DWM 注入改半径）现状 | 能改全局半径（v2.7→v3.0 全程在用），但 FillEffect/BuildEffect 无窗口归属概念 |

## 可行路线推演

### 路线 A'：DWM 效果对象 → 窗口映射（唯一同时满足「Acrylic 原生 + 各自保持」）

- **关键观察**：DWM 为每个启用 acrylic 的窗口构建独立的效果对象；`CCustomBlur::BuildEffect` hook 的 `this` 指针**每窗口不同**——存在天然的 per-window 粒度
- **思路**：建立 `this → HWND` 映射表；tray 侧每窗口半径经 SHM 传入；DLL 按窗口应用半径
- **未验证点**（不做测试的前提下保持开放）：
  - `this → HWND` 反查路径：visual 链反查（+0xE30F0，Route J 遗留）、调用栈回溯、候选窗口表比对（tray 提供当前 acrylic 窗口 hwnd 列表，探针比对）
  - 效果对象生命周期（窗口关闭/重建时映射失效清理）
- **风险**：dwm 崩溃 = 黑屏 + explorer 重启；私有偏移 19045 专属，系统更新即失效；实施代码需 MSVC `__try/__except` 全包
- **实施节奏**（若未来启动）：阶段 1 零行为探针（只读 + 日志，1-2 天桌面日志验证映射率）→ 阶段 2 per-window 应用

### 路线 B'：Crisp 承接（已实现，零风险回退）

- v3.0 crisp_overlay 每窗口独立 radius（argv[4] + 缓存帧引擎）已量化验证（probe 锐度数据）
- 代价：CPU 模糊、背景为捕获帧（动态壁纸不实时）、效果模式混用（非 DWM 原生 acrylic 视觉——含 DWM 材质的 tint/光照特性）

### 路线 C'：混合

- A' 能识别窗口身份的部分按窗口应用；识别失败/高风险窗口自动走 B' Crisp
- 复杂度最高，仅在 A' 部分成功时考虑

## 历史经验总结（为什么「Acrylic 按窗口半径」难）

1. **Windows 安全边界**是根本墙：跨进程控制第三方窗口的显示/捕获属性全部被拒（WDA 所有权、Magnification 系统保留）
2. **DWM 合成器全局状态**：模糊半径是 dwm.exe 进程级全局量，效果对象不暴露窗口身份（私有结构反查 = Route J 未验证高风险区）
3. **保字 + 背景模糊**（路线 A-K 全集教训）：窗口要么整体透明（文字随透 + ClearType 失效）、要么盖 overlay（文字被模糊污染）；DWM 只为「声明 Acrylic 的窗口」做模糊——第三方普通窗口没有背景层概念

## 下一步（未定，等用户决定时机）

- 若实施：路线 A' 阶段 1 探针起步（零行为改变，验证映射率后再定阶段 2）
- 若放弃 DWM 路线：路线 B'（Crisp 承接），改动集中在 profile 路由策略（radius 差异化 → 强制 crisp mode）
