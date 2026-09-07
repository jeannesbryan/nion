# NiOn `main.c` 拆分 Plan

> 工作分支: `refactor/split-main-c`
> 上游: `jeannesbryan/nion` (fork: `kaluluosi/nion`)
> 目标: 把 `src/main.c` 的上帝文件按职责边界拆成独立模块
> 状态: 草案 / 待执行

## 1. 为什么拆

`src/main.c` 当前 9114 行,占整个 `src/` 目录(10525 行)的 **86%**,包含约 **104 个去重顶层函数**。它几乎是"单文件浏览器"——UI、导航、会话、下载、书签、权限、Tor 启动、隐私、内容过滤、崩溃恢复、私密窗口、查找、缩放、打印……几乎**所有职责**都塞进一个文件。

这带来三个不可承受的代价:

1. **改一处、看全局**:任何一个小功能改动,都需要在 9000 行里翻找相关逻辑,容易误伤。
2. **边界无法被守住**:项目核心哲学是 **fail-closed**(Tor 失败绝不回落直连)、**Private Window 隔离**——这些安全边界恰恰依赖清晰的模块边界。上帝文件让边界模糊,安全逻辑和 UI 逻辑混杂。
3. **无法独立验证**:没有独立单元,无法单独编译测试一块;任何改动都只能是"整文件重编译"的黑盒验证。

作者 Jeanne 已经在主动演化:最近的提交 `4dc2cc3 Refactor: split main.c into smaller modules` 表明**她也在做同样的拆分**,已抽出 `navigation.c`、`util.c`、`types.h`、`permission.c`、`privacy.c`、`content-filter.c`、`per-site.c`。本 plan 是在此基础上,顺着她的命名/边界风格,把剩余的大头补齐。

## 2. 参考:已拆出的模块

| 模块 | 行数 | 职责 |
|------|------|------|
| `src/util.c` | 129 | 通用工具函数 |
| `src/navigation.c` | 358 | 纯 URI/host 谓词、校验 |
| `src/per-site.c` | 695 | 按站点安全策略 |
| `src/content-filter.c` | 57 | 内容过滤 |
| `src/permission.c` | 95 | 权限默认门(camera/mic/geo...) |
| `src/privacy.c` | 77 | 隐私相关 |

这些已经是**清晰、独立**的模块,是拆分的**参照范本**——每个文件职责单一、小而精。这是我们继续拆的纪律基调。

## 3. 拆分模块地图(从 main.c 提取)

基于 ast-grep 对 `main.c` 顶层函数名的聚类(按 `nion_<领域>_<动作>` 命名规律),天然形成以下可拆分的模块:

| 建议模块 | 代表函数 | 职责 | 预估行数 |
|---------|---------|------|---------|
| `session.c` | `schedule_session_save` / `restore_saved_session` / `start_pending_restores` / `show_crash_recovery_prompt` | 会话保存/恢复、崩溃恢复 | ~300 |
| `download.c` | `download_update_panel_visibility` / `download_free` / `download_remove_row` / `load_download_history` | 下载管理、历史 | ~400 |
| `bookmark.c` | `load_bookmarks` / `save_bookmarks` / `show_bookmarks` / `refresh_bookmarks_window` / `update_bookmark_button` | 书签读写/窗口 | ~250 |
| `private.c` | `private_apply_close_request` / `private_cleanup_partial_downloads` / `private_clear_closed_tabs` / `sync_private_windows_tor` | 私密窗口隔离 | ~200 |
| `tor.c` | `start_tor` / `stop_tor_gracefully` / `prepare_network` / `apply_network_proxy` / `tor_log_suggests_*` | Tor 生命周期、网络 | ~400 |
| `find.c` | `find_open` / `find_close` / `find_run` | 页面内查找 | ~80 |
| `preferences.c` | `load_preferences` / `save_preferences` | 首选项持久化 | ~150 |
| `content-filter`(并入已有) | `prepare_content_filter` / `apply_content_filter_to_window` | 内容过滤增强 | — |
| `ui.c` | `build_ui` / `update_controls` / `update_onion_button` / `update_site_info` / `update_tab_*` | UI 构建与状态刷新 | ~600 |
| `app.c` | `prepare_dirs` / `prepare_appimage_webkit_sandbox` / 全局状态 | App 初始化、全局状态机 | 保留主控 |

> 注: 上述"预估行数"为估算,实际以 ast-grep 精确审计为准。合并策略见 §5。

## 4. 拆分铁律(不能违反)

1. **每抽一个模块,必须同步更新 `meson.build` 的 `executable('nion', ...)` 源列表**。漏改 = 编译报未定义引用。
2. **守住安全边界骨架**: fail-closed(Tor 失败绝不回落直连)、Private Window 隔离、权限默认全禁——这些是 nion 的灵魂,拆的时候**只移动、不改逻辑**。
3. **只搬代码,不重构语义**: 拆分不等于重写。抽离模块时保持函数签名、行为完全不变。不做功能增强,不顺手改 bug。
4. **头文件先行**: 每个模块先建 `.h`(声明该模块对外接口),再建 `.c`(实现)。`main.c` 里的 `#include` 收敛到只包含真正用到的头文件。
5. **依赖方向单向**: 下游模块(如 `session.c`)不得反向依赖上游 UI 模块(`ui.c`)。保持"UI 依赖业务,业务不依赖 UI"。
6. **每次拆一个模块就编译验证一次**(见 §6 验收),不攒着一口气拆完再验证。

## 5. 拆分策略(分期执行)

### 一期: 高内聚、低耦合的"纯逻辑"模块(最先拆)

这些模块是纯数据/逻辑,不依赖 GTK 主循环,依赖最少,拆分风险最低:

1. `session.c` —— 会话保存/恢复/崩溃恢复
2. `download.c` —— 下载管理(数据部分)
3. `bookmark.c` —— 书签读写
4. `preferences.c` —— 首选项持久化

### 二期: 中耦合、涉及 Tor/网络生命周期的模块

5. `tor.c` —— Tor 启动/停止/网络代理
6. `private.c` —— 私密窗口隔离

### 三期: UI 构建与状态刷新(高耦合,最后拆)

7. `ui.c` —— UI 构建、控件状态刷新
8. `find.c` —— 页面内查找

> 每期拆完跑一次验收(§6)。三期都可回退: 任何一期编译失败或行为改变,`git revert` 该期提交即可。

## 5.5 子代理分工与协作铁律(给 Jeanne 的 IDES 组织形态示范)

> 本计划不仅是把 `main.c` 拆小,同时是 **IDES 用子代理做重构的示范**——主 agent 当指挥官,按活配接子代理,每个子代理带着主 agent 的记忆快照去并行干活。可以把它看作是"IDES 如何组织一支 agent 队伍拆大文件"的活样本。

**为什么用子代理**:`main.c` 9000+ 行,主 agent 的上下文有限。与其主 agent 一口气硬啃所有模块,不如**把相互独立的模块分给子代理并行拆**,每次派活只聚焦一个模块,避免上下文爆炸。这就是"镜像分身"机制——子代理继承主 agent 的记忆、知识、纪律快照,是"另一个我"去并行干活。

**分工原则(按模块维度物理隔离)**:

| 模块 | 复杂度 | 谁能拆 | 关键依赖 |
|------|--------|--------|---------|
| `session.c` | 低(纯数据核心) | 主 agent 先示范 | util.h/types.h,零 tab/UI 依赖 |
| `download.c`(纯数据部分) | 中 | 子代理 | 下载历史持久化,依赖较轻 |
| `bookmark.c` | 中 | 子代理 | 书签读写,相对独立 |
| `preferences.c` | 低 | 子代理 | 首选项持久化,独立 |
| `tor.c` | 高 | 主 agent 把关 | Tor 生命周期,深缠网络 |
| `private.c` | 中 | 子代理 | 私密窗口隔离,可能触 UI |
| `ui.c` | 高(最后拆) | 主 agent | UI 构建,深缠 GTK |

**子代理拆分的四条铁律(从 session 试点学到的硬教训)**:

1. **绝不让子代理用手写括号配对切函数**:这是 session 试点踩的坑——子代理用启发式括号配对切函数,风险极高、容易切错。正确做法:**主 agent 先把每个函数的精确边界用 ast-grep 扫出来**,存进 `docs/main-c-function-boundaries.md`,子代理直接引用这张精确边界表切,不靠猜。
2. **按"依赖闭包"切,不是按函数名切**:纯逻辑、依赖干净的模块(如 session 保存/调度)能独立拆;深缠 GTK/tab/UI 构造的函数(如崩溃恢复弹窗、UI 刷新)必须留到最后,单独归进 UI 模块。强行把 UI 函数抽出来会编译不过。
3. **子代理独立分支 + 只产出 `.c`/`.h` + 改 `main.c`,不碰 `meson.build`**:AGENTS.md §5.2 方案 A——`meson.build`(源列表)是共享文件,由主 agent 在合并每个子代理产出后统一更新,避免并行冲突。
4. **每个模块一个独立 commit**:遵循 `Refactor: extract <module> from main.c`,便于 `git revert` 独立回退单个模块。

**主 agent 与子代理的协作流程**:

```
主 agent: 用 ast-grep 扫出各模块函数精确边界 → 存入 docs/ 作为子代理的"地图"
   ↓
主 agent: 派活(把边界表 + AGENTS.md 铁律 + 该模块目标函数清单交给子代理)
   ↓
子代理: 建 .h/.c → 搬函数(只搬不改逻辑)→ 改 main.c(删被搬走的函数) → 提交
   ↓
主 agent: 合并子代理产出 → 更新 meson.build 源列表 → 编译验证
   ↓
主 agent: 通过后,再派下一个模块
```

**给 Jeanne 的示范价值**:这套流程展示了 IDES 的"组织形态"——不是单 agent 孤军奋战,而是**主 agent 当指挥官、按活配接、分而治之**。它把"拆 9000 行上帝文件"这种高风险活,拆解成一个个可验证、可回退的小步。这是 IDES 对"大型重构"这类问题的组织化回答。

## 6. 验收标准(Acceptance Criteria)

拆分成功的定义,按优先级:

- [ ] **编译通过**: `meson setup build && meson compile -C build` 无 error 无 warning(或 warning 不新增)。
- [ ] **行为等价**: 拆分前后,核心功能行为一致——Tor fallback 仍 fail-closed、Private Window 仍隔离、权限仍默认全禁。
- [ ] **行数收敛**: `main.c` 从 9114 行显著下降(理想目标 <3000 行),各新模块各司其职。
- [ ] **边界清晰**: 每个新模块头文件只暴露该模块对外接口,`main.c` 不再包含被抽出模块的代码。
- [ ] **git 历史干净**: 每个模块一个独立 commit,commit message 遵循 `Refactor: extract <module> from main.c` 风格,便于回退。

## 7. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 拆分改变行为 | 只移动不改逻辑; 每期拆完编译 + 行为对比 |
| 漏改 meson.build | 铁律 §4.1,每拆一个模块强制更新源列表 |
| 模块间依赖混乱 | 铁律 §4.5 单向依赖(UI 依赖业务) |
| 破坏安全边界 | 铁律 §4.2,拆分时写 diff 时人工 review 安全相关函数 |
| 上下文爆炸 | 用 ast-grep 结构化分析,不直接读整文件 |

## 8. 执行记录

- [x] 2026-09-07: 创建本 plan,切换分支 `refactor/split-main-c`
- [x] 2026-09-07: 删除 fork 冗余目录 `nion/nion`,工作区干净
- [x] 2026-09-07: 生成 `docs/main-c-function-inventory.md`(104 个去重函数清单)
- [x] 2026-09-07: 建立 `AGENTS.md`(质量流程 + 铁律)
- [x] 2026-09-07: 生成 `docs/main-c-function-boundaries.md`(ast-grep 精确函数边界表,子代理分工的地图)
- [x] 2026-09-07: **一期 `session.c` 核心拆分完成并提交**(`6d3a212`)——nion_save_session / nion_schedule_session_save / nion_session_save_timeout 抽出,零 UI 依赖
- [x] 2026-09-07: 更新本 plan,新增 §5.5 子代理分工与协作铁律(给 Jeanne 的 IDES 组织形态示范)
- [ ] 待办: WSL 编译验证 session 拆分(依赖安装中)
- [ ] 待办: 修 `release/manifest` 缺失导致的本地编译坑(已补最小可用版)

---
*本文档由 IDES agent (rush·暗河) 协助生成,遵循 NiOn 既有拆分纪律。*
