# EarLab 项目文件清单（逐文件说明）

2026-11-21 重构新增文件：

| 文件 | 说明 |
|---|---|
| `app/src/util/LatestSnapshot.h` | 无锁单写者／单读者最新快照邮箱 |
| `app/src/plugin/PluginLoadJob.h` | 可取消的扫描／隔离预检后台任务 |
| `app/src/ui/MainComponentLayout.cpp` | 六页布局、显示状态与页面导航 |
| `app/src/ui/MainComponentPlugin.cpp` | 插件载入、进度、取消与出题流程 |
| `app/src/ui/MainComponentSupport.h` | 主组件共用的文本、颜色与提示框辅助函数 |
| `app/src/ui/HistoryPagination.h` | 历史表分页与缩放边界纯逻辑 |
| `app/tests/Performance.cpp` | 同进程比较旧版与当前实现的可选微基准 |
| `scripts/benchmark.ps1` | 提取版本标签源码，构建和运行微基准 |
| `docs/REFACTOR-2026-11-21.md` | 本轮修改、验证与剩余限制 |

- 基线：`v0.6.1` / `c5ac653` + 交接文档提交
- 路径均相对仓库根 `C:\deepseek\earlab`
- 「类型」：源文件 / 测试 / 脚本 / 文档 / 安装器 / 配置
- 构建产物（`app/build-nmake/`、`third_party/JUCE/`、`tools/cmake/`、`dist/`、`artifacts/`）被 gitignore，不在 git 里，见 §8

---

## 1. 根目录

| 文件 | 类型 | 说明 |
|---|---|---|
| `.gitattributes` | 配置 | 换行/文本属性（LF 进库，CRLF 检出） |
| `.gitignore` | 配置 | 忽略构建产物、依赖、`dist/`、`artifacts/`、日志 |
| `PRD-v0.3.md` | 文档 | 产品需求（历史版本，功能与交互的原始来源） |
| `validate_prd.py` | 脚本 | PRD 一致性校验（历史工具，可忽略） |

## 2. app/（构建与说明）

| 文件 | 类型 | 说明 |
|---|---|---|
| `app/CMakeLists.txt` | 配置 | 四个目标：EarLabEngine（GUI）、EarLabDspTests、EarLabEngineTests、EarLabPluginTests；JUCE 8；C++20；`/utf-8`；VST3 宿主宏；`project VERSION 0.6.1` |
| `app/BUILD.md` | 文档 | 构建/测试/CLI/插件/VST/安装/卸载/许可/历史修复记录（最全的工程说明） |

## 3. app/src（应用源码）

### 3.1 入口

| 文件 | 类型 | 说明 |
|---|---|---|
| `app/src/Main.cpp` | 源文件 | JUCEApplication、主窗口、CLI 解析、子进程模式（`--vst3-scan-one`/`--vst3-preflight`）、`--check-trims`、单实例锁、导出退出码、DWM 深色标题栏 |

### 3.2 DSP（2）

| 文件 | 说明 |
|---|---|
| `app/src/dsp/LoudnessMeter.cpp/.h` | ITU-R BS.1770 近似积分响度测量（干/湿等响、压缩校准用） |

### 3.3 engine（24）

| 文件 | 说明 |
|---|---|
| `AudioEngine.cpp/.h` | 音频设备管理、播放状态、A/B、等响 trim、插件挂载、压缩校准、suspend/resume 计数 |
| `DualChain.cpp/.h` | 干/湿双链线性交叉淡化 + trim 增益 |
| `MaterialPlayer.cpp/.h` | 素材载入/循环播放/重采样准备 |
| `LoudnessMatcher.cpp/.h` | 10 s 窗口干/湿 LUFS 测量 → ±30 dB trim（`computeLoudnessMatch`） |
| `MultiBandEq.cpp/.h` | 8 段 EQ + HP/LP；多页 `CoeffSet` 无锁快照；`setSingleBand` 保留 HP/LP（压缩题需显式关闭） |
| `CompressorProcessor.cpp/.h` | 压缩器（阈值/比率/attack/release/GR 测量） |
| `CompressorCalibration.cpp/.h` | 二分校准阈值到目标 GR |
| `BuiltinChain.h` | 内置湿链：EQ → 压缩 |
| `TrainableProcessor.h` | 可训练处理器接口（内置链/插件统一） |
| `TrainingTask.cpp/.h` | EQ/压缩出题、选项生成（频率升序）、`formatHzLabel/formatDbLabel`、`Staircase`、`EloRating` |
| `Assessment.cpp/.h` | 测评评分/等级/`AssessmentStore`（派生文件名、坏档恢复、原子写） |
| `AbxSession.cpp/.h` | ABX 测试会话（20 题） |
| `ProfileStore.cpp/.h` | 档案：会话记录、技能统计、Elo、CSV/HTML 报告、原子写/备份恢复 |

### 3.4 plugin（14）

| 文件 | 说明 |
|---|---|
| `PluginCatalog.cpp/.h` | 目录缓存、扫描编排、`scanDetached` 快照（worker 安全）、`plugins.json` 读写 |
| `PluginScanRunner.cpp/.h` | 子进程扫描/预检、Job Object、超时/取消、`scanInChild` |
| `PluginHost.cpp/.h` | 插件实例创建、`prepareAndWarmUp`（声道安全）、加载类型/块大小规则 |
| `PluginProbe.cpp/.h` | 插件参数探针（固定 16k 块）、可用性/幅度判断 |
| `PluginTask.cpp/.h` | 插件题生成（bandAmount/levelAmount/bandDirection）、实测/二分校准 |
| `PluginTrainableProcessor.cpp/.h` | 插件湿链适配：按插件声道 prepare、按 maxBlock 切块 process |
| `PluginMemory.cpp/.h` | 每插件题型记忆（`plugin-memory.json`） |

### 3.5 ui（28）

| 文件 | 说明 |
|---|---|
| `MainComponent.cpp/.h` | 主界面：页面布局/可见性、训练/测评/插件流程、键盘、状态机、60 Hz 定时器、AlertWindow、插件扫描 UI |
| `Theme.h` | 设计 token：颜色、字号 ramp、间距、圆角、内容宽 |
| `EarLabLookAndFeel.cpp/.h` | 按钮/下拉/菜单/滚动条/编辑器绘制、focus ring、禁用态、hover 过渡 |
| `SessionRouting.h` | 会话与页面归属纯逻辑（训练/测评/插件做题键、Enter 小结） |
| `LabButton.h` | `FadeBase<TextButton>`：120 ms hover/按下过渡 |
| `FadeBase.h` | 通用过渡基类（`FadeValue`/`FadeProvider`/Timer） |
| `NavButton.h` | 侧栏导航项（选中条、hover、焦点环） |
| `StatusChip.h` | 页头状态胶囊（surface+border+状态点） |
| `OptionButton.h` | 答题选项（1–4 徽标、正确/错误态、focus、过渡） |
| `AssessmentCard.cpp/.h` | 成绩卡：分数/等级/分项/JND（176 高布局） |
| `AssessmentHistory.cpp/.h` | 历史表：行 40、全宽分隔线、页脚、空态 |
| `GradeStyle.h` | 等级色唯一来源 |
| `TimeFormat.h` | `formatDateTime` / `formatShortDateTime` |
| `EqCurvePanel.cpp/.h` | EQ 曲线/节点拖拽/滚轮、A/B 视图、坐标轴 |
| `PluginBrowser.cpp/.h` | 插件库列表、扫描线程/进度、加载入口 |
| `HistoryChart.cpp/.h` | Elo 趋势曲线（空态提示） |
| `AbilityBars.cpp/.h` | 三项能力条（Elo/JND/准确率） |
| `StatCard.h` | 首页统计卡（自绘） |

### 3.6 util（2）

| 文件 | 说明 |
|---|---|
| `AtomicFile.h` | `.tmp` → 原子替换 + `.bak`；`createBackup=false` 用于坏档恢复 |
| `SingleInstanceLock.h` | 按档案目录命名的 `InterProcessLock`（单实例） |

## 4. app/tests（3）

| 文件 | 说明 |
|---|---|
| `LoudnessTest.cpp` | DSP 10 断言（响度计、多段 EQ） |
| `EngineTest.cpp` | 业务 114 断言：出题/选项、阶梯、Elo、测评评分/等级/JND、存档与坏档恢复、profile 隔离、单实例锁、导出失败、SessionRouting、插件预热/切块规则 |
| `PluginTest.cpp` | 插件 18/42 断言：目录缓存、选项、参数索引、探针、预检类型规则、等响块、Pro-Q 4 全流程（带 `--file`） |

## 5. scripts（18）

| 文件 | 说明 |
|---|---|
| `build.ps1` | 一键构建（vcvars64 + 便携 CMake + NMake + 头文件变更自动 clean + 测试 + trim 自检 + 签名） |
| `build-installer.ps1` | A7 打包：stage → Inno setup → 便携 ZIP → 签名 → SHA256SUMS；幂等、fail-closed |
| `test-installer.ps1` | 安装/卸载 60 项自测 |
| `sign-app.ps1` | 签名 exe（`CN=EarLab Dev`，RFC3161 时间戳，TSA 失败回退） |
| `setup-dev-signing.ps1` | 创建自签名代码签名证书并加入当前用户信任 |
| `create-desktop-shortcut.ps1` | 开发机桌面快捷方式（cmd.exe 绕过 SmartScreen 的历史手法） |
| `fetch_juce.py` | 下载 JUCE 到 `third_party/JUCE` |
| `fetch_cmake.py` | 下载便携 CMake 到 `tools/cmake` |
| `verify-toolchain.ps1` | 环境自检 |
| `capture_attached.ps1` | 按进程名抓窗口截图（PrintWindow） |
| `capture_window.ps1` | 按窗口标题抓图 |
| `make_test_audio.py` | 生成测试音频 |
| `check_cpp_balance.py` | 大括号/结构平衡检查（辅助） |
| `fix_ps_encoding.py` | 修复 PS1 BOM/编码 |
| `build_target.bat` / `build_plugin_test.bat` / `build_app_logged.bat` | 定向构建小工具（bat） |

## 6. installer（9）

| 文件 | 说明 |
|---|---|
| `earlab.iss` | Inno Setup 脚本（per-user、开始菜单、HKCU 卸载、LicenseFile、VC DLL） |
| `install.ps1` / `uninstall.ps1` | 便携安装器/卸载器（HKCU + 开始菜单 + 卸载项） |
| `Install-EarLab.cmd` / `portable-launcher.cmd` | 便携包入口 |
| `LICENSE` / `THIRD-PARTY-NOTICES.txt` / `SOURCE-OFFER.txt` / `README.txt` | 许可、第三方（JUCE AGPLv3）、源码提供说明、安装说明 |

## 7. docs（4 + 本交接 2）

| 文件 | 说明 |
|---|---|
| `REVIEW-2026-10-23.md` | 2026-10-23 代码审查与修复记录 |
| `DESIGN-REVIEW-2026-10-25-assessment.md` | 测评页视觉评审（P0/P1/P2 规格与 handoff） |
| `HANDOVER-CODEX.md` | 本交接文档 |
| `PROJECT-INVENTORY.md` | 本文件 |

## 8. 被 gitignore 的目录（交接包中另行提供或需重新生成）

| 路径 | 说明 | 获取方式 |
|---|---|---|
| `third_party/JUCE/` | JUCE 8.0.15（约 93 MB） | `python scripts/fetch_juce.py`（或交接包 `deps/`） |
| `tools/cmake/` | 便携 CMake（约 155 MB） | `python scripts/fetch_cmake.py`，或系统 CMake 3.22+ |
| `app/build-nmake/` | 构建产物（约 590 MB） | `scripts/build.ps1` 生成 |
| `dist/` | 安装包/便携包/哈希 | `scripts/build-installer.ps1` 生成；交接包 `dist/` 有现成产物 |
| `artifacts/` | 验收/审查/QA 截图与日志（约 44 MB） | 交接包 `evidence/` 有核心报告；截图可按报告命令重跑 |

## 9. 数据文件（运行时，不在仓库）

| 路径 | 说明 |
|---|---|
| `%APPDATA%\EarLab\profile.json` | 训练档案（会话/技能统计/设置） |
| `%APPDATA%\EarLab\assessments-<profile>.json` | 测评历史（旧 `assessments.json` 只读回退） |
| `%APPDATA%\EarLab\plugins.json` | VST3 扫描缓存 |
| `%APPDATA%\EarLab\plugin-memory.json` | 插件题型记忆 |
| `*.json.bak` | 原子写备份（坏档恢复源） |

## 10. 构建产物命名

| 目标 | 产物 |
|---|---|
| EarLabEngine | `app/build-nmake/EarLabEngine_artefacts/Release/EarLab Engine.exe` |
| EarLabDspTests | `app/build-nmake/EarLabDspTests_artefacts/Release/EarLabDspTests.exe` |
| EarLabEngineTests | `app/build-nmake/EarLabEngineTests_artefacts/Release/EarLabEngineTests.exe` |
| EarLabPluginTests | `app/build-nmake/EarLabPluginTests_artefacts/Release/EarLabPluginTests.exe` |
