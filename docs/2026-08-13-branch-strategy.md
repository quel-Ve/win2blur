# win2blur 分支策略（2026-08-13 用户确认）

> 用户学习 git 分支工作流的产物：从共同基点 `v3-base`（= main HEAD ea82926）分两条需求线平行前进，**谁先成熟谁先 merge → tag → GitHub Release**（与 v2.7 同一条流水线）。用 branch 不用 fork：同一仓库、同一产品、可能合并回主线的演进方向不需要独立仓库副本。

## 布局

```
main        — 稳定线（当前 = 2.8 本地运行版 + 3.0 开发树导入状态）
 ├─ v3-profile  ← 方向 A：每窗口 profile / 每窗口模糊半径（现有 3.0 代码，待部署验证+修复）
 └─ v3-perf     ← 方向 B：性能自适应 + 液晶体玻璃/Mica 动态取色 + 暗色感知 tint（v3.0 设计文档推迟项，未动工）
```

- 共同基点 tag：`v3-base`（ea82926）
- 已 push：origin/v3-profile、origin/v3-perf、v3-base

## 方向 A（v3-profile）— 现状与阻塞

- 代码：`native/` v3.0 开发树（per-app profile `[Apps]` 表、crisp 每窗口半径 argv[4]、焦点差异化 ×0.6、鼠标模糊圈、缓存帧引擎、崩溃守卫）
- 阻塞：**从未端到端验证**（verification-plan 16 项中 8 项待用户验证）；AutoFrost 走 `[Apps]` 表（2.8 走 `[AutoFrost]` 管道格式，配置格式不兼容）；build5 部署缺 config.ini → 首启 seed 空表 → 无自动应用
- 下一步：部署验证（提权启动 + `[Apps]` 配置 seed + DDA 环境确认）→ 修复 → merge main → tag v3.0 → Release

## 方向 B（v3-perf）— 未动工

- 内容（v3.0 设计文档「非目标 → v3.1」清单）：性能自适应降级（GPU/电池感知）、液晶体玻璃/Mica 动态取色、暗色感知 tint
- 起步时从本分支 HEAD（= v3-base）开始，独立演进

## 工作流规则

1. 两条线各自在自己的分支上 commit + push
2. 方向 A/B 任一线成熟（功能验证通过 + 用户确认）：merge 回 main → tag 版本号 → GitHub Release（照 v2.7 流程）
3. 两线共享的 bugfix 放在 main，再 merge 到两条分支（避免 cherry-pick 混乱）
4. 同时开发两条线时用 git worktree 检出到独立目录（历史模式：老 v2.7-src/v2.8-src）

## 注意

- `dist_release_3.1/` 是误命名产物（内容 = v3.0 构建，与 build5 哈希一致），不是版本——测试时勿当 3.1
- 旧提交哈希（b2bfe8f、9b88759 等，docs 引用的）已随迁移丢失——现 git 历史从 v2.1 远程恢复 + 导入提交开始
