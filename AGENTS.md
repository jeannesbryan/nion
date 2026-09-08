# AGENTS.md — NiOn 协作指南

> 面向任何 agent / 协作者。NiOn 是一个**极简 Tor-only Linux 浏览器**,核心是**边界与 fail-closed**。改代码前先读懂这里。

## 1. 项目是什么

NiOn 用 C + GTK 4 + WebKitGTK 6 构建。它**只通过自带 Tor 运行时**访问明网和 `.onion` 站点,设计上 **fail closed**——Tor 失败时**绝不**偷偷回落直连。

- **不是** Tor Browser,不承诺 Tor Browser 级匿名/抗指纹(作者在 README/PRIVACY.md 里如实声明)。
- 定位: 小型、可审计、边界清晰的浏览器。

## 2. 安全铁律(不可违反)

这是 NiOn 的**灵魂**,任何改动都不得破坏:

1. **fail-closed**: Tor 启动/运行失败时,浏览器**必须**进入错误状态并**阻止 reload/navigation**,绝不回落到直连。有自动化测试 `scripts/test-fail-closed.sh` 守护。
2. **Private Window 隔离**: 私密窗口的会话/下载/权限数据与常规窗口物理隔离。对应测试 `test-private-window-stage2.sh`、`test-private-session-audit-stage4.sh`、`test-private-downloads-stage3.sh`。
3. **权限默认全禁**: camera/mic/geolocation/notifications 默认全部拒绝,per-origin 临时授权、用完即清。见 `src/permission.c`。
4. **URI 协议边界**: `file`/`javascript`/`data`/`blob`/`about` 内部协议封死;`mailto:`/`magnet:` 等外部协议必须**用户手势 + 显式确认**,并警告外部 app 可能绕过 Tor。见 `src/navigation.c`。
5. **本地网络锁死**: 内网/loopback/链路本地地址全封。NiOn 是 Tor-only 浏览器,**不是本地网络浏览器**。

> 改这些相关的函数时,务必**只移动、不改语义**。拆模块时尤其如此——拆错了安全行为,自动化测试会抓,但更重要的是守住边界。

## 3. 构建(Build)

NiOn 用 **Meson** 构建,目标环境是 **GNU/Linux x86_64**。

```bash
# 安装依赖(在 Debian/Ubuntu 系)
./scripts/install-deps-debian.sh

# 配置 & 编译(在项目根目录)
meson setup build
meson compile -C build
```

### ⚠️ 关键: `meson.build` 的源文件列表是"机械开关"

`meson.build` 里 `executable('nion', 'src/main.c', 'src/util.c', ...)` 的**每个 `.c` 文件都要列进去**。

**每新增/拆分一个 `.c` 模块,必须同步把新文件加进这个列表**,否则会直接编译报未定义引用。这是拆分工作里最容易踩的坑。

## 4. 测试(Test)

真实测试在 **Linux 环境**运行(GTK4/WebKitGTK,依赖 `ss`、`/proc`、进程模型)。**本地 Windows 无法跑 GUI 运行时测试**——这是环境限制,不是代码问题。可用的测试在 `scripts/` 下,按功能域划分:

- 安全: `test-fail-closed.sh`、`test-https-first-stage1.sh`、`test-escape-guards-stage2.sh`、`test-hardening-*.sh`
- 隐私窗口: `test-private-window-stage2.sh`、`test-private-session-audit-stage4.sh`、`test-private-downloads-stage3.sh`
- 书签: `test-bookmarks-stage2.sh`
- 下载: `test-downloads-stage5.sh`
- 标签/标签页: `test-tabs-stage1.sh`
- 站点控制/数据: `test-site-controls-stage1.sh`、`test-site-data-stage3.sh`

> **拆分后验证行为等价**: 拆完某个模块,在 Linux 上跑对应测试脚本。测试过了 ≈ 行为没被破坏。

## 5. 质量流程(拆模块时务必遵守)

### 5.1 拆分铁律

1. **每抽一个模块,同步更新 `meson.build` 的 `executable` 源列表**(见 §3)。漏改 = 编译失败。
2. **只移动代码,不改逻辑**: 拆分不等于重写。保持函数签名、行为完全不变。不顺手修 bug、不做功能增强。
3. **头文件先行**: 每个模块先建 `.h`(对外接口),再建 `.c`(实现)。
4. **依赖单向**: UI 依赖业务,业务不依赖 UI。下游模块不得反向依赖上游 UI 模块。
5. **一次只拆一个模块**: 拆完立即编译验证,攒着一起拆 = 出错难定位。

### 5.2 子代理分头协作的铁律(避免冲突)

当多个 agent/子代理**并行拆不同模块**时,用一个**共享文件**(如 `docs/`)记录每个子代理负责的模块、函数清单,避免重复拆同一个函数。

**关键冲突点: `meson.build`(源列表)是共享文件。**

- 方案 A(推荐): 子代理**只产出** `.h`/`.c` 文件 + 修改 `main.c`,**不碰 `meson.build`**。由主 agent 在合并每个子代理产出后,统一更新源列表。
- 方案 B: 每个子代理在自己的**独立 git 分支**上工作,最后合并。`meson.build` 冲突由主 agent 解决。

### 5.3 提交规范

- 每个模块一个独立 commit,遵循: `Refactor: extract <module> from main.c`
- 便于独立回退: `git revert <commit>` 只回退那个模块。

## 6. 目录结构

```
src/            源码(main.c / navigation.c / permission.c / privacy.c / ...)
scripts/        构建 + 测试脚本(test-fail-closed.sh 等)
data/           资源(gresource、图标、desktop)
runtime/        Tor 运行时说明(fetch-tor-runtime.sh)
packaging/      AppImage/Linux 打包
release/        版本清单(NION_VERSION 等)
docs/           拆分 plan 等技术文档
```

## 7. 当前进行中

**`main.c` 上帝文件拆分**(分支 `refactor/split-main-c`)。

- 现状: `src/main.c` 9114 行,占 `src/` 约 86%,约 104 个去重顶层函数。
- 目标: 按职责边界拆成独立模块,详见 `docs/split-main-c-plan.md` 和 `docs/main-c-function-inventory.md`。
- 原则: 沿用作者已拆出 `navigation.c`/`permission.c` 等模块的清晰边界纪律,守住 §2 安全铁律。

---
*本文件由 IDES agent (rush·暗河) 为 NiOn fork 协作而建,遵循项目本体纪律。*
