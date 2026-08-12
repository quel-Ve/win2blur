# win2blur 技术困境：保字 + 背景模糊的不可兼得？

> 日期：2026-08-09。目的：向更强的大模型完整描述困境，寻求第三条技术路线。

## 目标（用户需求）

对一个任意第三方窗口（记事本、文件管理器、VS Code 等），实现：
1. **窗口文字/图标 100% 清晰锐利**（保字，ClearType 不失效）
2. **窗口背后的背景呈模糊毛玻璃效果**（透过窗口看到模糊背景）
3. 无闪烁、无窗口状态破坏（拖动/任务栏排序/交互全部正常）
4. 窗口自身内容（文字）之上**没有**模糊蒙版污染（不能"字发光"）

用户公式："1.0 窗口 + 0.2 背景 when transparency=80%"

## 现有两条路线及各自困境

### 路线 A：Acrylic（DWM 原生，v2.7/v2.8 已有）

**实现**：`acrylic_overlay.exe` 在目标窗口**下方**插入一个 WS_EX_LAYERED 窗口，用 `SetWindowCompositionAttribute(WCA_ACCENT_POLICY, ACCENT_ENABLE_ACRYLICBLURBEHIND)` 让 DWM 原生模糊该区域。目标窗口自身用 `SetLayeredWindowAttributes` 设透明度（如 80%）。

**效果**：窗口半透明 + 背后背景被 DWM 模糊。零闪烁、任务栏正常、交互正常。

**困境（保字失败）**：
- 目标窗口用 **WS_EX_LAYERED + LWA_ALPHA 整体透明** → **文字也随窗口一起透明** → 文字发虚
- **Layered 窗口失去 ClearType 亚像素渲染**（ClearType 只对不透明窗口生效）→ 文字边缘彩色锯齿
- 用户实测："Acrylic 低透明度时文字发虚"（v2.8 引入 Crisp 的初衷）

### 路线 B：Crisp（DDA 捕获 + 独立 overlay，v2.8/v3.0）

**实现**：目标窗口**保持 100% 不透明**（保字 ✓）。独立进程 `crisp_overlay.exe`：
1. **双隐藏**：ShowWindow(overlay, HIDE) + ShowWindow(target, HIDE) → 短暂隐藏目标和 overlay
2. **DDA 捕获**（Desktop Duplication API）：捕获桌面帧中目标区域 = **目标背后的背景**
3. **CPU box blur** 模糊背景
4. **UpdateLayeredWindow** 把模糊背景以 wash alpha（如 120/255≈47%）叠在目标**上方**

**效果（用户认可的部分）**：窗口 100% 不透明、文字清晰锐利、背景模糊可见。"保字成功，黑色背景消失，字清晰"。

**困境（双隐藏代价）**：
1. **任务栏排序破坏**：explorer 窗口被 SW_HIDE 后，任务栏把它当"关闭"，恢复时**重排到末尾**（用户："软件在任务栏的排序来到最后，像最近打开的"）
2. **拖动后闪烁**：双隐藏一瞬（隐藏+恢复），拖动停止时肉眼可见闪几帧
3. **偶发自模糊**：DWM 合成滞后，捕获帧有时含目标自身 → "字发光"（模糊蒙版污染了文字）
4. **对 Shell 窗口（explorer）有副作用**：隐藏/显示触发 Shell 状态变化

## 为什么"A 透明 + B 模糊"走不通（关键矛盾）

| 需求 | 路线 A (窗口透明) | 路线 B (overlay 叠层) |
|------|-------------------|----------------------|
| 文字清晰 | ❌ 文字随窗口透明 + ClearType 失效 | ✅ 窗口 100% 不透明 |
| 背景模糊 | ✅ DWM 原生 | ✅ DDA + CPU blur |
| 无闪烁/无状态破坏 | ✅ | ❌ 双隐藏代价 |

**核心矛盾**：
- 要"看到背景"，窗口必须**透出背景**（A：窗口透明）或**叠背景在上**（B：overlay 盖窗口）
- A 的透明 = 文字也透明（无法只透明背景）
- B 的 overlay 盖在窗口上 = 文字被模糊污染（除非 overlay 只在窗口**后面**——但窗口不透明，完全遮住 overlay，什么都看不到）
- B 想捕获"窗口背后的背景"必须**隐藏窗口**（DDA 捕获时窗口挡着）→ 双隐藏副作用

## 已尝试的优化（未根治）

1. 双隐藏前等待 DWM 合成（96ms 多次 DwmFlush）→ 改善自模糊但 explorer 合成慢仍偶发
2. reanchor 每 tick 检查 z-order → 修复"一半一半"（overlay 时上时下）
3. WS_EX_TRANSPARENT → 修复点击穿透（能交互了）
4. 捕获失败退避 800ms → 拖动后更新 <1s
5. DDA AcquireNextFrame 超时 16→100ms → 减少捕获失败

## 想请教的问题

1. **有没有第三条路线**：窗口保持 100% 不透明（保字）+ 背景模糊 + 不隐藏窗口 + 无闪烁？
   - 例如：DDA 捕获含目标自身的帧后，用**图像 inpainting** 重建目标区域为"背后的背景"？（不隐藏窗口，从全屏帧中推断目标区域应该是什么）
   - 或者：利用 DWM 的其他 API（BackdropBrush / DWMWA_SYSTEMBACKDROP_TYPE / Mica）？
2. **Windows 10 的 Mica/Acrylic 系统应用**（文件资源管理器、设置）是如何做到"背景模糊 + 文字清晰"的？它们的窗口是不透明的（文字不透明），但背景区域是模糊材质 —— 这是**应用自己绘制**的效果还是 DWM 原生支持？第三方应用能否复用？
3. **SetWindowCompositionAttribute 的 ACCENT_ENABLE_ACRYLICBLURBEHIND** 能否做到"窗口内容不透明 + 仅背景模糊"？（当前实现是整体透明）
4. 如果必须双隐藏，有没有办法**不破坏任务栏排序**（隐藏时保留任务栏位置）？
5. Windows 10 上有没有 **GDI/组合层 API** 能在不隐藏窗口的情况下捕获"窗口背后的内容"？

## 环境

- Windows 10 Pro 10.0.19045
- C++17 MinGW，无第三方库依赖（可接受引入小依赖）
- 目标是控制**任意第三方窗口**（非自家应用，无法修改对方渲染代码）

---

# 实测验证记录（2026-08-08 ~ 2026-08-10，全部实测非推测）

## 路线 C：LWA_ALPHA=0 透明隐藏（替代 SW_HIDE）— ✅ 部分成功

**做法**：capture 时不用 ShowWindow(SW_HIDE)，改为：
```cpp
SetWindowLongW(target, GWL_EXSTYLE, ex | WS_EX_LAYERED);
SetLayeredWindowAttributes(target, 0, 0, LWA_ALPHA);  // 视觉消失，IsWindowVisible 仍 TRUE
// capture...
// 恢复原 layered 状态
```

**实测结果**：
- ✅ 任务栏排序不再变（SW_HIDE 触发 HSHELL_WINDOWDESTROYED 导致任务栏按钮重排；LWA_ALPHA=0 不触发）
- ✅ 自模糊消除（字不再发光）—— 配合 DWM 合成等待（DwmFlush ×5 + 40ms）
- ✅ 交互正常（WS_EX_TRANSPARENT 修复点击穿透）
- ❌ **拖动停止后 opacity 闪回几帧**（LWA_ALPHA 0→255 切换瞬间窗口闪现不透明）
- ❌ 动态壁纸不实时（静止时背景是静态快照）

**修复尝试**：5s 定时重捕获（`lastBgRefresh` 强制 moved=true）→ **动态壁纸能更新了，但每次刷新都闪几帧 opaque**（透明→恢复的切换本身有闪）

## 路线 D：Magnification API 排除捕获 — ❌ 失败（API 被系统保留）

**做法**：MagSetImageScalingCallback + MagSetWindowFilterList(MW_FILTERMODE_EXCLUDE, 目标)
**实测结果**：
- `MagSetImageScalingCallback` → **错误 50 (ERROR_NOT_SUPPORTED)** —— 该回调只对系统放大镜开放，普通程序被拒
- 退路（可见 mag 窗口 + BitBlt 抓取）：`MagSetWindowSource` 也失败（err=0 无效果），排除列表无法验证
**结论**：Magnification API 的"排除捕获"能力是**系统放大镜专用**，第三方程序不可用

## 路线 E：WDA_EXCLUDEFROMCAPTURE（Kimi K3 方案）— ❌ 失败（所有权限制）

**做法**：`SetWindowDisplayAffinity(target, WDA_EXCLUDEFROMCAPTURE=0x11)` 让目标窗口"屏幕上照常显示，但从所有捕获流（DDA/BitBlt/PrintWindow）中剔除"—— 理论上是完美的"零隐藏捕获背景"原语
**实测结果**：
- 普通权限（Medium IL）调用 → **错误 5 (ERROR_ACCESS_DENIED)**
- **管理员权限（High IL）调用 → 仍然错误 5** ← 关键！
**根因**：`SetWindowDisplayAffinity` 的跨进程调用**只允许窗口所有者进程**（或更高权限且同会话？实测 High IL 也被拒）—— Windows 的安全设计：防第三方让窗口"从截屏消失"（截屏绕过/欺诈）
**结论**：对**控制第三方窗口**的场景此路不通（Kimi 方案在"控制自家窗口"时有效）

## 路线 F：LWA_COLORKEY 抠背景色 — ❌ 失败（explorer 无效果）

**做法**：采样窗口背景色，`SetLayeredWindowAttributes(target, bgColor, 0, LWA_COLORKEY)` 只抠同色像素透明
**实测结果**：explorer 上**完全无效果**（背景未透明）—— ColorKey 对现代渲染（DirectComposition/硬件加速）无效，且 explorer 暗色背景非纯色
**结论**：仅对极简单的纯色 GDI 窗口有效，不通用

## 路线 G：DWM 注入（Route F）深化 — ⚠️ 部分可行，限制明确

**现状**：注入 dwmcore.dll hook `CD2DContext::FillEffect`，能改**所有** DWM 模糊的**半径**（StandardDeviation/KernelRangeFactor）
**局限**：
- 只能改半径（全局），**无法按窗口指定模糊区域/强度** —— FillEffect 无窗口归属概念
- 无法让**不透明 RGB 窗口**（第三方普通窗口）获得背景模糊 —— DWM 只为"声明了 Acrylic 的窗口"做模糊，普通窗口没有透明区域可填材质
**结论**：注入能证明合成层能力强大，但"按窗口控制第三方窗口模糊"需要更深层的 DWM 内部操作（CVisual 反查，未验证高风险）

## 路线 H：系统应用为什么能做到"背景模糊+文字清晰"？

**答案**：**应用内部分层绘制**（非 DWM 魔法）：
- 系统应用（设置/UWP/文件资源管理器 Win11）用 `Windows.UI.Composition` 的 Acrylic/Mica brush 作为**背景视觉层**，文字是叠在上面的**不透明视觉层**
- Compositor 实时采样并模糊桌面作背景材质；文字是不透明上层 layer，天然不参与模糊、ClearType 正常
- **关键**：这需要**应用自己用框架绘制**。第三方传统应用（记事本等）画的是 100% 不透明 RGB，没有"背景层"概念
- 第三方**自家窗口**可复用（ICompositorDesktopInterop）；**别人的窗口无法复用**（无法让对方把文字画到我们的材质层之上）

---

# "不计代价"路线分析（2026-08-10）

## 核心结论：唯一被实测拒绝的是"跨进程调用 SetWindowDisplayAffinity"

所有路线的失败本质都是 **Windows 的安全边界**：
- Magnification 回调：系统保留（错误 50）
- WDA：所有权检查（错误 5，High IL 也拒）
- ColorKey：渲染路径不通用
- DWM 注入：无窗口归属概念

## 路线 I：DLL 注入目标进程 + 进程内调用 SetWindowDisplayAffinity（最有希望）

**思路**：错误 5 的根源是"非窗口所有者调用"。**如果我们 DLL 注入目标进程，从进程内（= 窗口所有者上下文）调用 SetWindowDisplayAffinity(target, WDA_EXCLUDEFROMCAPTURE)**，就绕过了所有权检查 —— 因为调用者就是所有者本人。

**这是合法用法**：窗口所有者有权设置自己窗口的显示亲和性；注入只是让我们"借用所有者身份"调用它自己的合法 API（不涉及提权/绕过安全机制本身 —— 是让目标进程执行它本来就允许的操作）。

**为什么这是"不计代价"路线的关键**：
- WDA_EXCLUDEFROMCAPTURE 是**持续性**属性（不是一次性隐藏）→ 设置后 DDA 每帧捕获时目标窗口自动消失 → **背景实时可见（180Hz 无闪烁）**
- 屏幕显示完全不受影响（窗口照常渲染、任务栏不动、焦点不动、ClearType 不动）
- **保字 100%**（窗口从不透明）
- 不再需要 LWA_ALPHA 透明切换 → **无闪帧**
- 不再需要定时刷新 → **动态壁纸实时**

**所需组件**（我们已有基础设施）：
1. **注入器**（已有多进程注入经验：injector.exe → dwm.exe）改为注入目标应用进程（CreateRemoteThread + LoadLibraryW）
2. **DLL**：DllMain 里 SetWindowDisplayAffinity(targetHwnd, WDA_EXCLUDEFROMCAPTURE)；收到目标 HWND 作为参数
3. **崩溃残留处理**（Kimi 提醒的关键坑）：若注入进程被杀，目标窗口永久"截屏不可见" → 需要：
   - 正常退出时 DLL 恢复 WDA_NONE
   - watchdog：检测注入进程死亡 → 遍历注入列表恢复（或持久化注入列表到文件，下次启动补救）
4. **降级链**：注入失败（目标进程受保护/高 IL）→ 回退现有 LWA_ALPHA 方案

**风险**：
- 注入第三方进程有崩溃风险（DLL 兼容性）→ 选低风险目标（记事本/文件管理器）验证
- 目标窗口在用户**录屏/截图/会议共享**时不可见（WDA 语义）→ 需做成可开关选项
- explorer 是 shell，注入崩溃 = 桌面重启 → 谨慎

## 路线 J：DWM 注入深化（CVisual 反查按窗口控制）

- 在 dwm.exe 内通过 visual 树反查窗口句柄（+0xE30F0，未验证）
- 能做到"按窗口设置模糊/特效"（真正系统级）
- **风险极高**：dwm 崩溃 = 黑屏 + explorer 重启；私有结构随系统更新变化
- 列为最后手段

## 路线 K：图像 inpainting 背景重建（零权限兜底）

- 不隐藏窗口，DDA 捕获含目标帧 → 用目标矩形外圈像素做 Laplace 扩散填充（Gauss-Seidel 迭代）猜测背景
- 时间累积：维护背景缓存，未遮挡区实时更新，遮挡区读旧值
- 经 box blur 后误差几乎不可见
- 缺点：遮挡期间动态内容不更新；CPU 成本
- 作为注入失败时的降级

## 推荐执行顺序

1. **路线 I 原型验证**（注入 + 进程内 WDA）—— 成本中等，收益最大（可能一次性解决全部困境）
2. 失败 → 路线 J（DWM 深化）或路线 K（inpainting）
3. 所有路线验证完再定最终架构
