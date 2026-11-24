# EarLab 交接文档（面向 Codex / 接手开发者）

> 后续重构状态见 [REFACTOR-2026-11-21.md](REFACTOR-2026-11-21.md)。本文保留原发布交接历史；测试数量、EQ 快照、异步预检及历史分页以该记录为准。

- 项目：EarLab（耳力训练 / 听力辨识训练桌面应用）
- 版本：0.6.1（CMake `project(EarLab VERSION 0.6.1)`）
- 仓库：`C:\deepseek\earlab`（本地 git，无远端）
- 交接基线：`HEAD = c5ac653`（tag `v0.6.1`，之后的文档提交不影响二进制）
- 平台：Windows 10/11 x64；C++20；JUCE 8.0.15（vendored）；MSVC v143；NMake
- 文档日期：2026-11-21
- 关联文档：[BUILD.md](../app/BUILD.md)（构建/安装/历史修复）、[PROJECT-INVENTORY.md](PROJECT-INVENTORY.md)（逐文件清单）、[PRD-v0.3.md](../PRD-v0.3.md)（产品需求）

> 这份文档假设读者没有参与此前开发。读完第 1–3 节即可开始工作；第 4–13 节是系统说明书与坑位；第 14–16 节是接手守则与路线图。

---

## 1. 项目是什么

EarLab 是面向音频工程师/混音师的听力训练工具：播放同一段素材的“干声/处理后”两路信号，让用户在 EQ 频段、压缩量、VST3 插件处理等维度上做辨识判断，用阶梯算法与 Elo 追踪能力，并提供 12 题能力测评与成绩历史。

当前功能（6 个页面）：

| 页面 | 作用 |
|---|---|
| 首页 | 概览：三项能力卡（EQ/压缩/插件）、Elo 趋势图、快捷入口 |
| 素材 | 载入音频、白/粉噪声、A/B 试听、等响 trim 状态 |
| 训练 | 10 题一轮的分项训练（EQ 频段定位 / 压缩量判断），阶梯难度 + Elo，本轮小结 |
| VST 训练 | 本机 VST3 插件训练：扫描插件 → 加载 → 自动测参出题 → 判定，含插件记忆 |
| 测评 | 12 题固定测评（6 EQ + 6 压缩），出分、等级、JND、历史记录、插件分项入口 |
| 设置 | 音频设备、档案路径、导出报告（CSV/HTML）、关于 |

核心业务流程：`选素材 → 校准（等响 trim / 压缩阈值）→ 出题 → 试听 A/B → 作答 → 反馈 → 下一题 → 小结/出分 → 存档`。

---

## 2. 当前状态快照（接手先确认）

| 项 | 值 |
|---|---|
| HEAD | `c5ac653`（`v0.6.1` tag；文档提交在其后） |
| 工作树 | 干净（`git status` 空） |
| 构建 | `scripts/build.ps1` 绿色；Release exe 已构建并签名 |
| 测试 | DSP 10、Engine 114、Plugin 42（带 Pro-Q 4）/ 18（默认）；ctest 3/3 |
| 自检 | `EarLab Engine.exe --check-trims` → `PASS` |
| 安装包 | `dist/EarLab-0.6.1-win64-setup.exe`（4,008,128 B，Valid + RFC3161 时间戳） |
| 便携包 | `dist/EarLab-0.6.1-win64-portable.zip`（含 install/uninstall 脚本） |
| 发布审查 | P0/P1 全修（见 §12）；视觉 P0/P1/P2 已验收（见 §11） |
| 未完成项 | setup.exe 本机被火绒 HIPS 拦截，端到端未在本机验证；清洁机/VM 验收未做 |

构建产物位置：`app/build-nmake/EarLabEngine_artefacts/Release/EarLab Engine.exe`。

---

## 3. Codex 快速开始

```powershell
# 0) 只看不跑：先读 docs/HANDOVER-CODEX.md、app/BUILD.md、docs/PROJECT-INVENTORY.md
cd C:\deepseek\earlab

# 1) 确认环境（VS Build Tools 2022 + Windows SDK + JUCE/cmake 已就位）
powershell -ExecutionPolicy Bypass -File scripts\verify-toolchain.ps1

# 2) 构建 + 测试 + 签名（约 4 分钟；改过头文件会触发全量重建）
#    先确保没有正在运行的 EarLab，否则链接会 LNK1104
taskkill /F /IM "EarLab Engine.exe" 2>nul
powershell -ExecutionPolicy Bypass -File ./scripts/build.ps1

# 3) 独立跑测试
tools\cmake\bin\ctest.exe --test-dir app\build-nmake --output-on-failure

# 4) 插件完整回归（本机装有 FabFilter Pro-Q 4 时）
app\build-nmake\EarLabPluginTests_artefacts\Release\EarLabPluginTests.exe `
  --file="C:\Program Files\Common Files\VST3\FabFilter\FabFilter Pro-Q 4.vst3"

# 5) 启动应用（带素材可直达训练/测评）
"app\build-nmake\EarLabEngine_artefacts\Release\EarLab Engine.exe" artifacts\test-loop-44k.wav --page=assessment
```

**改代码后的最低验收标准**（每次提交前）：

1. `build.ps1` 退出码 0，输出 `GREEN: 构建与测试全部通过`；
2. `ctest` 3/3；
3. `--check-trims` PASS；
4. 若动了 UI：按 §11 的关键数值做截图回归（不要回退已验收的视觉参数）；
5. 若动了安装/脚本：`scripts/test-installer.ps1` 60/60。

---

## 4. 架构总览

```
app/src/
  Main.cpp               应用入口、窗口、CLI 解析、子进程模式、单实例、导出
  dsp/                   响度计（ITU-R BS.1770 近似）
  engine/                音频引擎与训练/测评业务
  plugin/                VST3 宿主、扫描隔离、任务生成、插件记忆
  ui/                    JUCE 界面（自绘主题 + 单 MainComponent 状态机）
  util/                  原子写文件、单实例锁
app/tests/               DSP / Engine / Plugin 三套断言测试
scripts/                 构建、签名、打包、截图、环境工具
installer/               安装器源文件（Inno .iss + 便携安装脚本 + 许可）
docs/                    PRD 历史评审、视觉评审、本交接文档
third_party/JUCE/        JUCE 8.0.15（gitignored，脚本下载）
tools/cmake/             便携 CMake（gitignored，脚本下载）
```

### 4.1 音频链路

- `AudioEngine` 持有 `AudioDeviceManager`、`MaterialPlayer`（干声）、`DualChain`（干/湿交叉淡化）、`MultiBandEq`、`CompressorProcessor`、内置/插件处理器。
- 播放时不可直接改链路：插件挂载/换题用 `AudioSuspension`（`removeAudioCallback`/`addAudioCallback`，计数式嵌套）暂停回调后再改。
- 参数跨线程：`MultiBandEq` 用多页 `CoeffSet` + acquire/release 发布；音频线程块首取快照，整块读同一页。**约束**：一个音频块内发布次数不能超过页数−1（当前页数 3，UI 侧已按 60 Hz 合并拖动发布）。
- 等响：`LoudnessMatcher` 对素材做 10 s 窗口的干/湿 LUFS 测量，算 ±30 dB 内 trim；`DualChain` 用 trim 对齐 A/B 响度。
- 压缩题：`CompressorCalibration` 用原始缓冲二分校准阈值到目标 GR；出题前必须关闭用户 HP/LP（否则“正确答案”与听到的不一致，已修）。
- 插件处理：`PluginTrainableProcessor` 按插件声道数 prepare，`process()` 把任意长度输入**按 maxBlock 切块**（历史 P0/P1 修复，不要改回一次性调用）。

### 4.2 VST3 子系统

- 扫描：`PluginCatalog::scanDetached()` 在 worker 线程枚举 `.vst3`，每个插件起独立子进程（`--vst3-scan-one/--vst3-scan-out`），Job Object 限制进程树；崩溃/超时记为失败。
- 预检：`--vst3-preflight` 子进程真实实例化一次；GUI 加载前必跑。
- 加载：`PluginHost::create()` 预热（`prepareAndWarmUp`）——预热缓冲声道数必须 `max(2, max(in, out))`，并用扫描元数据兜底 0 声道（0-in/64-out 插件否则越界写崩主进程，历史 P0）。
- **无同进程回退**：子进程空结果/预检不可用时拒绝加载并提示，不允许退回主进程（历史 P1）。
- 任务：`PluginTaskGenerator` 按实测参数生成“频段/量级/方向”题；`PluginMemory` 记录每插件题型的对错统计。

### 4.3 线程模型

| 线程 | 职责 | 约束 |
|---|---|---|
| 音频线程 | `AudioEngine::audioDeviceIOCallback` | 不加锁、不分配、不打日志、不做插件扫描 |
| 消息线程 | UI、60 Hz Timer（前 12 帧≈5 Hz 刷新文本）、大量同步 I/O | 避免长阻塞；加载插件最长约 45 s 是已知问题 |
| 扫描 worker | `PluginCatalog::scanDetached` | 只产出不可变 `ScanOutcome`，消息线程发布 |
| 扫描子进程 | 插件元数据/预检 | 每个插件独立进程，崩溃隔离 |

### 4.4 UI 状态机（最容易出错的地方）

- 单一 `MainComponent` 管理全部页面与训练/测评状态；页面枚举：home/material/training/plugin/assessment/settings。
- 会话（训练或测评）**严格绑定宿主页**：训练在训练页、插件训练在 VST 页、测评在测评页。离开宿主页必须确认并中止（`showPage()` 的离开确认 + `abortTraining()`）。
- 纯逻辑在 `ui/SessionRouting.h`（`sessionHostedOnPage`、`canAcceptAnswerKey`、`canActivateSummaryWithEnter`），有 EngineTest 断言，**改状态机先改这里并补测试**。
- 键盘：`1–4` 作答、`Space` 播放/停止、`Enter` 下一题/小结再练一轮、`A/B` 试听；键盘只在会话宿主页生效。
- 退出/中止：会话卡内有「中止本轮」；测量中切换档案会重置。

---

## 5. 目录与文件地图

完整逐文件说明见 [PROJECT-INVENTORY.md](PROJECT-INVENTORY.md)。高频改动文件：

| 文件 | 说明 |
|---|---|
| `app/src/ui/MainComponent.cpp/.h` | 页面布局、可见性、状态机、键盘、测评/训练流程（约 4.5k 行，改动最多） |
| `app/src/ui/Theme.h` | 颜色/间距/字号/圆角 token（视觉唯一来源） |
| `app/src/ui/EarLabLookAndFeel.cpp/.h` | 按钮/下拉/菜单/计时器绘制、焦点环、过渡 |
| `app/src/engine/AudioEngine.cpp/.h` | 设备、播放、A/B、插件挂载、等响 trim |
| `app/src/engine/TrainingTask.cpp/.h` | EQ/压缩出题、选项、阶梯、Elo |
| `app/src/engine/Assessment.cpp/.h` | 测评评分、等级、历史存取（原子写/备份） |
| `app/src/plugin/PluginHost.cpp/.h` | 插件实例创建/预热/加载规则 |
| `app/src/plugin/PluginScanRunner.cpp/.h` | 子进程扫描/预检/Job Object |
| `app/src/plugin/PluginTask.cpp/.h` | 插件题生成与测量 |
| `app/src/util/AtomicFile.h` | 原子写 + `.bak`（坏档恢复时跳过备份） |
| `app/tests/EngineTest.cpp` | 业务断言（评分/存档/状态机/插件规则），改逻辑必须补这里 |

---

## 6. 构建系统

### 6.1 依赖

- Visual Studio 2022 Build Tools（MSVC v143 + Windows SDK），必须含 C++ 桌面工作负载。
- CMake 3.22+：仓库自带便携版 `tools/cmake/bin/cmake.exe`（由 `scripts/fetch_cmake.py` 下载）。
- JUCE 8.0.15：`third_party/JUCE`（由 `scripts/fetch_juce.py` 下载）。二者都被 `.gitignore` 忽略，不在 git 里。
- 若 `python` 是 Microsoft Store 占位符，用 `py -3` 或真实 Python 全路径运行 fetch 脚本。

### 6.2 `scripts/build.ps1` 做了什么

1. 找 VS 环境（vcvars64）与便携 CMake；
2. 检查**头文件 mtime 是否比 `app/build-nmake/.stamp` 新**——新则整包 clean 重建（防止旧 `.obj` 与类布局不一致导致随机 0xC0000005）；
3. `cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release` + `nmake`；
4. 跑三个测试 exe（DSP/Engine/Plugin 默认无插件）；
5. 跑 `EarLab Engine.exe --check-trims`；
6. `scripts/sign-app.ps1` 签名（证书缺失时跳过，不阻断本地构建；发布打包是 fail-closed）。

注意：

- `sign-app.ps1` 现在会尝试 RFC3161 时间戳，TSA 不可达时回退无时间戳（不阻断构建）。
- 构建前必须关闭正在运行的 EarLab，否则链接报 `LNK1104`。
- 脚本含中文，`build.ps1` **必须保留 UTF-8 BOM**，否则 PowerShell 5.1 解析报错。
- PowerShell 5.1 里 `$` 在部分 shell 包装下会被吞；复杂命令请写成 `.ps1` 文件再执行。

### 6.3 测试

| 目标 | 源 | 断言 | 说明 |
|---|---|---|---|
| `EarLabDspTests` | `app/tests/LoudnessTest.cpp` | 10 | 响度计/多段 EQ 基础 |
| `EarLabEngineTests` | `app/tests/EngineTest.cpp` | 114 | 出题、阶梯、Elo、测评评分/存档/坏档恢复、单实例锁、SessionRouting、导出失败、插件预热声道规则等 |
| `EarLabPluginTests` | `app/tests/PluginTest.cpp` | 42（带 `--file`）/ 18（默认） | 插件目录缓存、选项构建、参数索引、探针、等响块、预检规则；本机无插件时只跑 18 项 |

运行：`tools/cmake/bin/ctest.exe --test-dir app/build-nmake --output-on-failure`。
新增行为请先写失败断言（红）再修（绿），报告里附日志。

---

## 7. 业务规则

### 7.1 训练（10 题/轮）

- 技能：`eqBandCenter`（EQ 频段定位）、`compAmount`（压缩量判断）、`pluginBand`（插件）。
- 难度：阶梯算法（3-down/1-up 类），每轮 10 题；Elo 更新；首页趋势图取最近 12 轮。
- 选项：EQ 频率固定 4 个升序候选（100/200/630/4000 Hz）；压缩选项间距 ≥1 dB。
- 小结：正确数、反转次数、Elo、再练一轮/回首页。

### 7.2 测评（12 题固定）

- 6 题 EQ（增益按 trial 递增难度）+ 6 题压缩；答完出分。
- 分数 = 正确率（0–100）；等级：`入门级 <50`、`进阶级 50–69`、`熟练级 70–84`、`专业级 ≥85`。
- JND = 答对的最小增益（dB）；分项正确率与 JND 存历史。
- 结果写入 `assessments-<profile>.json`（legacy `assessments.json` 只读回退），最多保留 200 条。
- 作答后题号/圆点保持当前题，点「下一题」才推进（视觉验收要求）。

### 7.3 插件题

- 题型：`bandAmount`（频段+量级）、`levelAmount`（量级）、`bandDirection`（方向）。
- 生成：实测 magnitude → 二分校准到目标增益；候选参数集合生成 4 选项。
- 记忆：`plugin-memory.json` 按插件+题型记录；缓存 `plugins.json`。

### 7.4 数据与持久化

| 文件（默认 `%APPDATA%\EarLab\`） | 内容 |
|---|---|
| `profile.json` | 训练档案：会话记录、技能 Elo/统计、设置 |
| `assessments-<profile-stem>.json` | 测评历史（评分/分项/JND）；`--profile=` 时在同目录派生 |
| `plugins.json` | VST3 扫描目录缓存（含失败/黑名单） |
| `plugin-memory.json` | 插件题型记忆 |

写入走 `AtomicFile.h`：`.tmp` → 替换 + `.bak`；**从 `.bak` 恢复后的第一次保存会跳过备份**（否则会用坏主文件覆盖唯一好备份）。单实例锁按档案目录加锁，防止两个 GUI 并发整表覆盖；无窗口 CLI 模式不加锁。

---

## 8. 命令行（自动化/便携）

```
EarLab Engine.exe [素材路径] [选项]
  --autostart            载入后直接开始训练
  --assessment           直接开始 12 题测评
  --page=home|material|training|vst|assessment|settings
  --noise=white|pink     使用内置噪声
  --skill=eq|comp|plugin 指定技能
  --profile=<path>       指定档案（测评/插件缓存按同级派生）
  --plugin=<vst3 path>   预选插件
  --qa-auto              自动作答（QA 用）
  --qa-kind=amount|level|direction
  --export-report=<path> 导出报告后退出（失败返回非零）
  --check-trims          等响 trim 自检后退出
  --vst3-scan-one=<path> --vst3-scan-out=<json>   扫描子进程模式
  --vst3-preflight=<path>                         预检子进程模式
```

---

## 9. UI 设计系统（不要随意改）

- 主题：石墨监听台，`Theme.h` 唯一 token 源（canvas/surface/text1-3/accent/accentStrong/accentWash/success/warn/danger、8pt 间距、radius、type ramp）。
- 圆角：卡片 12、控件 8、徽章 6、胶囊 = 高/2；控件高度只用 32/40/48。
- 测评页：内容列 `max-width 960` 居中；考试卡 252 高垂直居中；成绩卡 176；历史表行 40、分隔线全宽、末行无线、页脚 32；空态隐藏成绩卡、历史空态 120。
- 主操作每屏 ≤1 个 `accentStrong` 面；`accentWash` 只表示“选中/当前”。
- 焦点环：按钮/选项/导航 2px accent；Tab 可达。
- 过渡：`FadeBase` 120ms ease-out；按钮 hover/按下；考试卡描边 accent@0.30。
- 中文一律 `juce::String::fromUTF8`；字体 `Microsoft YaHei UI` + `Consolas`（数字）。

已验收关键值（回归会查）：考试卡 top≈337（1000×880）、960 列（1600 宽）、成绩卡 176、历史分隔线 728/728、空态 120、CTA 160×40、分数列 76、JND 右对齐、徽标 1–4、提示行 810–959。

---

## 10. 交付与打包（A7）

### 10.1 产物

| 文件 | 说明 |
|---|---|
| `dist/EarLab-0.6.1-win64-setup.exe` | Inno Setup 6，per-user，`PrivilegesRequired=lowest`，已签名 + RFC3161 时间戳 |
| `dist/EarLab-0.6.1-win64-portable.zip` | 便携包 + `install.ps1`/`uninstall.ps1`（HKCU 卸载项 + 开始菜单） |
| `dist/SHA256SUMS.txt` | 三文件哈希 |
| `dist/BUILD-INFO.txt` | commit、版本、依赖路径、签名、哈希 |

安装位置 `%LOCALAPPDATA%\Programs\EarLab`（无 UAC）；开始菜单 `EarLab.lnk` 直指 exe；HKCU Uninstall 项 `DisplayVersion=0.6.1`；随包 `msvcp140.dll`/`vcruntime140.dll`/`vcruntime140_1.dll`；许可文件见 `installer/`。

### 10.2 重新打包

```powershell
powershell -ExecutionPolicy Bypass -File ./scripts/build-installer.ps1
powershell -ExecutionPolicy Bypass -File ./scripts/test-installer.ps1
```

`build-installer.ps1` 幂等、失败非零退出；会校验 exe 签名、复制 VC 运行库、生成 setup/zip/sums、签名 setup。`test-installer.ps1` 60 项（布局/版本/签名/开始菜单/HKCU/启动/trim/卸载残留/数据保留）。

### 10.3 已知限制

- 本机火绒 HIPS 拦截 setup.exe 创建 `%LOCALAPPDATA%\Programs\EarLab`（Error 5），setup 端到端未在本机验证；便携安装器已验证。换无拦截机器执行同一静默命令即可。
- 自签名证书（`CN=EarLab Dev`）只在本机受信，目标机 SmartScreen 仍会提示；对外发布需 CA 证书。
- JUCE 8 采用 AGPLv3/商业双授权；`installer/THIRD-PARTY-NOTICES.txt` 已写，**闭源对外分发需购买商业授权**。
- 未做清洁机/VM 验收；无应用图标。

---

## 11. 已完成的审查与视觉验收（2026-10 至 11）

三轮视觉改造（P0/P1/P2）均已独立验收，报告在仓库 `artifacts/`（gitignored，交接包里有副本）：

- P0：考试卡居中、进度点三态、主操作强调、键位 1–4（`verify-p0-report.md`）
- P1：字号刻度/层级、成绩卡 176、历史表全宽线、空态、960 列、焦点环（`verify-p1-report.md`）
- P2：数字排版/等级统一/尺寸体系/考试卡描边/hover 过渡/JND 释义（`verify-p2-report.md`）

发布前整体审查（5 份报告，`review-*.md`）：音频、VST3、数据、UI、发布。修复提交 `6da5187`，覆盖：

- P0：预热缓冲越界（多输出插件崩溃）、坏档覆盖 `.bak`、多实例丢记录；
- P1：插件超块、同进程扫描回退、预检/加载配置漂移、压缩题 HP/LP 污染、测评串档、静默中止、导出 exit 0、坏档无提示、无中止入口、会话跨页泄漏、隐藏小结 Enter、插件扫描 UAF。

修复后：DSP 10 / Engine 114 / Plugin 42 全绿；`--check-trims` PASS；打包与安装自测 60/60；独立 QA：便携安装器 PASS、setup 本机 HIPS 受限。

---

## 12. 已知问题与待办（按优先级）

### P1（建议尽快）

1. **setup.exe 端到端验收**：在没有火绒/白名单机器上安装、启动、卸载并记录；当前唯一 HOLD。
2. **清洁机/VM 验收**：确认 VC 运行库随包可启动、注册表/快捷方式正确。
3. **插件加载阻塞 UI**（最长约 45 s）：加载/预检改异步 + 进度提示。
4. **历史表在 1000×880 只显示约 6 行**：加滚动或分页（当前上限 200 条）。

### P2（打磨）

- 状态胶囊长文件名的省略号；AbilityBars 徽章压轨道 36 px。
- 其余页面字号迁移到 P1 的新 type ramp（全局放大 30% 需单独回归）。
- 应用图标、`LegalCopyright`/`OriginalFilename`、产品名统一（`EarLab` vs `EarLab Engine`）。
- About/状态栏 3 处版本号硬编码改为单一来源（`JUCE_APPLICATION_VERSION_STRING`/`ProjectVersion`）。
- `scanInProcess` 已无调用但仍编译进二进制，可移除。
- `fetch_cmake.py`/`fetch_juce.py` 下载 `latest` 且无 SHA256 校验；固定版本+校验。
- 多 profile 并发（不同目录）仍允许；同目录已单实例。
- EQ 拖动发布节流目前依赖 60 Hz，若加高频 UI 需复核三页轮转上界。

---

## 13. 常见坑（踩过）

| 现象 | 原因/处理 |
|---|---|
| 随机 `0xC0000005` | 头文件改动但旧 obj 未重建；`build.ps1` 会按 `.stamp` 自动 clean，必要时 `-Clean` |
| `LNK1104` 打不开 exe | 有 EarLab 在运行；先 `taskkill /F /IM "EarLab Engine.exe"` |
| `build.ps1` ParserError | 脚本 UTF-8 BOM 丢失；用带 BOM 的编辑器保存 |
| PowerShell 中文乱码 | 控制台代码页；脚本内容仍按 UTF-8 BOM 保存 |
| 启动弹「未知发布者」/SmartScreen | 自签名证书；用 `cmd.exe` 快捷方式是历史绕过手法，安装器不使用 |
| 安装 setup 被拦截 | 本机火绒 HIPS；换机器或加白名单，**不要关闭安全软件** |
| `python` 是 Store 占位符 | 用 `py -3` 或完整路径 |
| 插件训练没有声音/参数 0 | 先确认预检通过、插件已加载；Pro-Q 4 是本机标准测试插件 |
| 测评/训练状态串页 | 已由 `SessionRouting.h` 守卫；改状态机时同步更新测试 |
| 数据文件坏 | 不要删 `.bak`；程序会从 `.bak` 恢复且不再覆盖它 |

---

## 14. 接手守则（Codex 请遵守）

1. **先读再改**：`docs/HANDOVER-CODEX.md` → `app/BUILD.md` → 要改的模块 + 对应 `app/tests` 断言。
2. **红-绿**：先写能失败的断言，再修；提交前 `build.ps1` + `ctest` 全绿。
3. **不回退已验收项**：§9 的数值、§11 的 P0/P1/P2 视觉行为、§12 的 P1 修复，改动前先跑对应验收报告里的检查。
4. **不改安装器许可/签名策略**：fail-closed、随包 VC 运行库、AGPL 告知；SmartScreen 不能靠改代码绕过。
5. **中文文案**用 `fromUTF8`；PS1 保持 BOM；不要引入新依赖。
6. **git**：单分支 `main`；提交信息用中文，写清“改了什么/为什么/证据”；tag 只在发布时打。
7. **错误处理**：不要静默失败；面向用户的错误必须可见（AlertWindow/状态栏/反馈行，且该控件必须可见）。
8. **数据安全**：任何写档案的改动都要考虑坏档/并发；新增文件走 `AtomicFile`。
9. **性能**：音频线程禁止锁/分配/日志；消息线程避免长阻塞。
10. **验收边界**：UI 改动至少截 1000×880 与 1600×1400 两档；安装改动跑 `test-installer.ps1`。

---

## 15. 路线图建议

1. 完成 setup 清洁机验收（阻塞项）。
2. 异步插件加载 + 进度（体验最大痛点）。
3. 历史表滚动/分页 + 更多记录展示。
4. 全局 type ramp 迁移（分批、每批回归）。
5. 图标/版本元数据统一 + About 页。
6. 固定依赖版本与校验，补 CI（GitHub Actions Windows runner）与自动截图回归。
7. 如需对外：CA 证书 + JUCE 商业授权 + 安装包 SmartScreen 提交。

---

## 16. 联系与许可

- 项目许可见 `installer/LICENSE`、`installer/THIRD-PARTY-NOTICES.txt`、`installer/SOURCE-OFFER.txt`。
- JUCE 8.0.15 按 AGPLv3 使用（或购买商业授权）；第三方声明随安装包分发。
- 本机开发签名证书：`CN=EarLab Dev, O=EarLab`（thumbprint `6945F54C860D6E0F197768F2C833CD02404A033B`），由 `scripts/setup-dev-signing.ps1` 创建并只加入当前用户信任；不要安装到用户机器。

---

## 附录 A：关键提交

| 提交 | 内容 |
|---|---|
| `9d737ac` | 测评页 P0 视觉修复 |
| `a34d326` | 测评页 P1 视觉改造 |
| `54a8e81` | 导航焦点环（D2） |
| `3028083` | 测评页 P2 打磨 |
| `6da5187` | 发布前整体审查 P0/P1 修复 |
| `22838b2` | A7 打包（安装器/便携包/脚本） |
| `c5ac653` | 签名 RFC3161 时间戳 |

tag：`v0.6.0-a6c`、`v0.6.1`、`pre-fix-2026-10-23`。

## 附录 B：交接包内容（与本文件同目录）

- `HANDOVER-CODEX.md`（本文件）
- `PROJECT-INVENTORY.md`（逐文件说明）
- `START-HERE-CODEX.md`（5 分钟速览）
- `restore.ps1`（从 git bundle 还原仓库 + 拉依赖 + 构建）
- `source/earlab-0.6.1-src.zip`（tag `v0.6.1` 源码快照）
- `source/earlab-0.6.1.bundle`（完整 git 历史）
- `dist/`（安装包/便携包/哈希/BUILD-INFO）
- `evidence/`（5 份审查报告、P0/P1/P2 验收报告、A7 QA 报告）
