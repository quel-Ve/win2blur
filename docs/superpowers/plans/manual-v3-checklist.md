# win2blur v3.0 — 手动验收清单 (Manual Release Checklist)

> 版本：**v3.0**（2026-08-07）｜范围：per-window profiles、focus differentiation、mouse-follow blur circle、crash guard
> 对应：SDD 计划 Task 9 Step 2 —— 完整手动回归走查
>
> ## ⚠️ 状态：待用户执行 (PENDING-USER-EXECUTION)
> 本清单需要**人类在桌面端逐行走查**（真实视觉验证 blur/circle/focus 过渡 + 进程行为验证）。
> 自动代理**不执行**本清单：运行前必须退出当前部署实例（托盘退出，**勿强杀**——实例持有 win2blur_single 互斥锁和 10+ 活跃叠加层）。
> 每行结果请记录到底部表格；失败项作为后续修复提交处理。

---

## 0. 运行前准备

- [ ] **P1 退出旧实例**：托盘图标右键 → 退出（勿用 taskkill，勿留僵尸进程）。确认 `tasklist | findstr win2blur` 无输出。
- [ ] **P2 启动新构建**：从 `win2blur\dist\` 启动最新 `win2blur.exe`（或 `dist_release_3.0\win2blur.exe`）。托盘出现、无报错弹窗。
- [ ] **P3 确认日志**：`C:\Temp\win2blur_debug.log` 存在且持续追加（后续步骤以此为证据源）。

---

## A. Crisp 保字模式（核心）

- [ ] **A1 Crisp 基本效果 + 文字锐利度**（brief row 1）
  *验证*：Settings → 效果模式选 **Crisp (保字)**，对一个文字密集窗口（如记事本/资源管理器）应用。确认：目标窗口本身 100% 不透明、文字无发虚（ClearType 正常），背景呈模糊 wash（`1.0 目标 + x% 背景`）。调整透明度热键（ALT+←/→）→ wash 比例实时变化且文字始终锐利。

- [ ] **A2 模糊半径滑块 · 按 profile 生效**（brief row 2）
  *验证*：Settings → 模糊半径滑块（全局，blur slider）拖动 → Crisp 窗口背景模糊程度实时变化。再通过 profile（见 D3/D2）给某应用设 radius=40 → 该窗口模糊明显强于全局值；profile **不带** radius 时回落到全局滑块值。

- [ ] **A3 焦点降级 (focus reduce) + 300ms 过渡**（brief row 3 + Task 4 延期项）
  *验证*：
  1. **聚焦窗口显示 FULL 参数**：让目标窗口处于前台 → 背景模糊、tint 均为完整 profile 值（tint = 配置值，非 5/6）。
  2. **点击离开**（点击桌面/其他窗口）→ 目标窗口的模糊和 wash 在 **~300ms** 内平滑减弱（ease-out-cubic，无跳变）；tint 独立降为 ×0.5。
  3. **点击回来** → 同样 ~300ms 平滑恢复 FULL 参数。
  4. **移动窗口不误触发**：拖动聚焦窗口后，模糊**不应**自动降级（FOCUS_CAPTURE_HOLD 1500ms 抑制窗口生效）；短暂停留后点击回来仍正常。
  *注意*：真实点击离开后 1.5s 内的抑制窗口属预期行为（fix round 0 的取舍）。

- [ ] **A4 鼠标跟随模糊圈：滑块实时 + 平滑边缘 + 空闲淡入淡出**（brief row 4 + Task 5 延期项）
  *验证*：
  1. Settings → 勾选某 profile 的 "Circle follow"（或临时 profile circle=on）→ 鼠标移入目标窗口，出现跟随光标的模糊圈。
  2. **圈内模糊明显强于圈外**（boost 生效），随光标平滑移动（无拖尾/卡顿）。
  3. **滑块实时**：Circle radius / boost / band 拖动 + Apply → 圈的大小/强度/边缘过渡带实时变化（重启后 live 生效）。
  4. **边缘平滑**：圈边无锯齿、**无 1/4 缩放大小的接缝/阶梯**（downscale upscale 路径正确）。
  5. **空闲淡出**：光标停在圈内静止 >1.5s → 圈 ~300ms 淡出；移动光标 → ~300ms 淡入且圈回到新光标位置。

- [ ] **A5 未毛玻璃窗口上的圈 = 局部窥视孔 (local peephole)**（brief row 5）
  *验证*：对**未应用效果**的窗口（无 alpha 修改）开启 circle=on → 只有光标周围一圈出现模糊（局部窥视背景），圈外保持完全清晰原样。

## B. Acrylic 模式

- [ ] **B1 Acrylic 焦点降级 = 仅 tint ×0.5**（brief row 6）
  *验证*：效果模式选 **Acrylic (DWM)**。聚焦时 tint 为完整 profile 值；点击离开 → tint 平滑降至 ×0.5（模糊不变——DWM 半径固定，仅 tint 降级）；点击回来 → 恢复。窗口自身 alpha 无变化。

## C. 崩溃保护 (Crash Guard)

- [ ] **C1 崩溃自动重启 + 3x/30s 熔断**（brief row 7 + Task 8 延期 Step 5）
  *验证*（需要提升的 shell，如管理员 cmd）：
  1. 对窗口应用 Crisp（确认 overlay 进程存在：`tasklist | findstr crisp_overlay`）。
  2. `taskkill //F //IM crisp_overlay.exe` → **~2s 内自动重启**。证据：`C:\Temp\win2blur_debug.log` 出现 `crashguard relaunch hwnd=... attempt=1`。
  3. 30 秒内连杀 4 次 → 第 4 次后**不再重启**，托盘弹出 balloon "overlay crash loop"（"overlay crashed 3x in 30s — stopped"），日志出现 `crashguard cap hit`。
  4. 等 30 秒窗口过后再 kill → 重启恢复（计数清零，`attempt=1` 重新开始）。

## D. 配置 / 会话 / Settings UI

- [ ] **D1 会话保存/恢复含 profile 值**（brief row 8）
  *验证*：给窗口应用 Crisp（radius/tint/circle 走 profile 或全局）→ 托盘 Keep & Exit → 重新启动 → 窗口自动恢复同参数（含**按窗口的 radius/tint/circle**，非仅 alpha）。对比 Settings 值一致。

- [ ] **D2 notepad profile 端到端**（Task 6 延期 Step 7）
  *验证*：在配置 `[Apps]` 段添加：
  `App_0=notepad.exe = mode=crisp alpha=65 radius=40 tint=20 circle=on`
  → 打开记事本 → 自动应用，且启动命令应为：
  `crisp_overlay.exe <hwnd> 190 20 40 50 200 30 60`（radius=40、tint=20、circle=50px 圈）
  核对方法：`wmic process where "name='crisp_overlay.exe'" get commandline`。
  再验证：删掉该行的 `radius=` 字段 → 记事本自动回落使用**全局模糊滑块**值。测试后移除此测试行。

- [ ] **D3 Settings UI 点击走查**（Task 7 延期 Step 7）
  *验证*：
  1. 打开 Settings → 应用列表点选一项 → 右侧编辑器填充该 app 的 mode/alpha/radius/tint/circle/Inherit 状态。
  2. 编辑 + "Save to profile" → 配置写入 `[Apps]` 行（仅显式字段）；重开 Settings → 值回读一致（**round-trip**）。
  3. **Inherit global 勾选**：全继承时勾选且控件禁用；保存为全 -1；非全继承时取消勾选、-1 字段显示全局当前值。
  4. 四个全局滑块（Circle radius/boost/band、Focus factor）拖动 + Apply → 配置持久化；**关闭重开 Settings → 数值保持**。
  5. **Circle radius 修改 + Apply → 已存在的 Crisp 叠加层 live 重启**（无需重新应用窗口，旧圈参数即被替换）。
  6. 布局目检：右侧列无控件重叠/遮挡（Apply/Close、Add/Remove 区域互不侵犯），165px 宽的 trackbar 拖动手感正常，Save 按钮完整可见（含无 blur 控制的配置）。

## E. v2.8 回归

- [ ] **E1 热键**（brief row 9）：ALT+←/→ 透明度增减（步进 5% 档）、ALT+↑ 开关、ALT+↓ tint 循环——全部照旧工作。
- [ ] **E2 Acrylic 模式 + z-order**（brief row 9）：Acrylic 叠加层正常跟随/重锚（切换前台窗口、拖动目标窗口后叠加层不盖内容、不沉底、无卡死）；关窗（含缩托盘软件如 cloudmusic）后叠加层随 HIDE/DESTROY 事件消失。
- [ ] **E3 tint=0**（brief row 9）：tint 滑块拖到 **0** → 纯模糊、无黑色 tint；Settings 重启后仍为 0。

---

## 结果记录

| 行 | 通过? (✓/✗) | 备注 / 失败描述 |
|----|------------|----------------|
| P1 | | |
| P2 | | |
| P3 | | |
| A1 | | |
| A2 | | |
| A3 | | |
| A4 | | |
| A5 | | |
| B1 | | |
| C1 | | |
| D1 | | |
| D2 | | |
| D3 | | |
| E1 | | |
| E2 | | |
| E3 | | |

**全部通过** → 走查人签名：____________ 日期：____________
有任何失败 → 作为后续修复提交处理（当日按 daily rule 合并）。
