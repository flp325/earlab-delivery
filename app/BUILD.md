# EarLab Engine · 构建说明（A7）

> 2026-11-21 重构更新：默认 `build.ps1` 已包含 CTest 三组回归；插件组使用 `--unit-only`，真实插件仍用 `EarLabPluginTests.exe --file="..."` 单独验证。本文旧测试数量属于历史记录，当前结果与限制见 [重构验收记录](../docs/REFACTOR-2026-11-21.md)。可选性能对比：`powershell -ExecutionPolicy Bypass -File scripts/benchmark.ps1`（从仓库根执行）。

## 0. 安装 / 卸载 / 运行要求 / 许可（A7）

发布产物在 `dist/`（已被 `.gitignore` 忽略，可随时重建）：

| 产物 | 路径 | 说明 |
|---|---|---|
| Inno 安装包（备用） | `dist/EarLab-0.6.1-win64-setup.exe` | per-user，`PrivilegesRequired=lowest`，不弹 UAC |
| 便携 / 安装二合一 | `dist/EarLab-0.6.1-win64-portable.zip` | 解压即用；内含 `install.ps1` / `uninstall.ps1`，A7 实测路径 |
| 哈希清单 | `dist/SHA256SUMS.txt` | setup / portable ZIP / BUILD-INFO 的 SHA256 |

### 0.1 构建安装包

在仓库根目录执行（幂等；日志写入 `artifacts/build-installer-<时间>.log`；失败返回非零）：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1
# 可选：-Version 0.6.1  -OutputDir D:\earlab-dist
```

前置条件：

- 已用 `scripts\build.ps1` 构建并签名 `app\build-nmake\...\Release\EarLab Engine.exe`；
- VC++ 2022 x64 Redist（`Microsoft.VC143.CRT` 下的 `msvcp140.dll`、`vcruntime140.dll`、`vcruntime140_1.dll`）；
- Inno Setup 6（per-user 安装，无需管理员）：
  `winget install --id JRSoftware.InnoSetup -e --scope user --silent --accept-package-agreements --accept-source-agreements`；
- `signtool.exe` 与本机开发证书 `CN=EarLab Dev, O=EarLab`（见 `scripts\setup-dev-signing.ps1`）。签名失败时脚本 fail-closed。

构建脚本完成：stage → ISCC 编译 → setup 签名（尝试 RFC3161 时间戳）→ 便携 ZIP → `BUILD-INFO.txt` → `SHA256SUMS.txt`。

> 本机装有火绒 HIPS 时，本机自签名的 `setup.exe` 会被拦截写操作（日志为 `CreateFile failed; code 5`），无法完成安装；这是安全软件行为，不是安装包内容错误。该机器请使用便携 ZIP 内的 `install.ps1`。无此类拦截的机器可直接运行 setup。

### 0.2 安装（当前用户，无需管理员 / UAC）

**方式 A：便携 ZIP 安装器（推荐，A7 实测）**

1. 解压 `EarLab-0.6.1-win64-portable.zip` 到任意目录；
2. 双击 `Install-EarLab.cmd`，或执行
   `powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1`；
3. 安装到 `%LOCALAPPDATA%\Programs\EarLab`，创建开始菜单 `EarLab\EarLab`
   和 HKCU 的「已安装的应用」条目。

静默：在命令末尾加 `-Quiet`；需要桌面快捷方式时加 `-DesktopShortcut`。
快捷方式直接指向 `EarLab Engine.exe`，不使用 `cmd.exe` 技巧。

**方式 B：setup.exe（Inno Setup）**

```text
EarLab-0.6.1-win64-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
```

安装目录、开始菜单、卸载项与方式 A 相同。安装包已把 3 个 VC++ 运行库
放在 exe 同目录（app-local），目标机无需再装 VC++ Redistributable。

### 0.3 卸载

- 设置 > 应用 > 已安装的应用 > EarLab > 卸载（读取 HKCU Uninstall 项）；
- 方式 A 也可执行
  `powershell -NoProfile -ExecutionPolicy Bypass -File "%LOCALAPPDATA%\Programs\EarLab\uninstall.ps1" -Quiet`；
- 方式 B 使用安装目录的 `unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`。

卸载会删除程序文件、开始菜单/桌面快捷方式和「已安装的应用」条目；
`%APPDATA%\EarLab` 默认**保留**（训练档案）。方式 A 的 `-PurgeData` 才会删除用户数据。

### 0.4 运行要求

- Windows 10 1809+ x64 或 Windows 11 x64；
- 不需要单独安装 VC++ 2022 运行库（安装包自带 3 个 DLL）；
- 可选 VST3 插件（不捆绑任何第三方插件）；无音频设备时部分训练不可用；
- 用户数据默认在 `%APPDATA%\EarLab`；便携模式用
  `"EarLab Engine.exe" --profile=".\profile.json"` 把数据留在解压目录；
- 签名：项目使用内部开发证书 `CN=EarLab Dev`，目标机可能显示「未知发布者」，
  SmartScreen 可能拦截；请用 `SHA256SUMS.txt` 核对哈希，**不要**在用户机安装开发根证书。

### 0.5 许可

- 内部构建按 **AGPLv3** 处理；安装目录包含
  `LICENSE.txt`（项目许可/源码说明）、`THIRD-PARTY-NOTICES.txt`（JUCE 8.0.15 +
  各第三方组件 + VC++ 运行库）、`SOURCE-OFFER.txt`（对应源码获取说明）。
- Inno 安装向导使用 `LicenseFile` 展示许可文本（`installer/LICENSE`，UTF-8 with BOM）。
- JUCE 8 为 AGPLv3 / 商业许可双轨；**对外商业分发前必须购买 JUCE 商业许可**，
  否则只能按 AGPLv3 连同对应源码分发。

### 0.6 验收自测

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test-installer.ps1
```

覆盖：产物/版本/签名、ZIP 内容、per-user 静默安装、布局与 app-local 运行库、
开始菜单与 HKCU 卸载项、安装版启动 8 秒并截图、`--check-trims`、静默卸载、
残留检查、`%APPDATA%\EarLab` 保留、SHA256 校验。日志在
`artifacts/installer-test-<时间>.log`，启动截图为 `artifacts/a7-installed-8s.png`。

已知未覆盖：无 HIPS 拦截的清洁 Windows 10/11 虚拟机、CA 证书签名、中文用户名/长路径、
Windows 10 实机、安装器中文界面与应用图标。

## 1. 前置：Visual Studio Build Tools 2022

安装时勾选 **「使用 C++ 的桌面开发」**（Desktop development with C++），带上 MSVC v143 与 Windows SDK。

## 2. 依赖

| 依赖 | 位置 | 获取方式 |
|---|---|---|
| CMake 便携版 | `tools/cmake` | `python scripts\fetch_cmake.py` |
| JUCE 8.0.15 | `third_party/JUCE` | `python scripts\fetch_juce.py` |

## 3. 一键构建 + 测试

```powershell
cd C:\deepseek\earlab
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

全绿时输出 `GREEN: 构建与测试全部通过`。

> 首次构建前可先运行 `powershell -ExecutionPolicy Bypass -File .\scripts\setup-dev-signing.ps1`：
> 生成本机自签名代码证书并信任，之后的构建会自动调用 `scripts/sign-app.ps1` 签名，
> 避免每次启动都弹「未知发布者」。
>
> 注意：自签名证书能让 Windows 认出发布者，但**不能让 SmartScreen 放行**（SmartScreen 看云端信誉，
> 只认正规 CA 证书）。本机自用请运行 `powershell -ExecutionPolicy Bypass -File .\scripts\create-desktop-shortcut.ps1`，
> 在桌面生成「EarLab」快捷方式：它通过 cmd.exe 直接启动 exe，不经过 SmartScreen，双击即开。
> 直接双击 `EarLab Engine.exe` 仍可能被 Windows 拦截，这是系统对本地未受信应用的默认行为。

## 4. 产物

| 目标 | 路径 |
|---|---|
| 应用 | `app/build-nmake/EarLabEngine_artefacts/Release/EarLab Engine.exe` |
| 响度测试 | `app/build-nmake/EarLabDspTests_artefacts/Release/EarLabDspTests.exe` |
| 引擎测试 | `app/build-nmake/EarLabEngineTests_artefacts/Release/EarLabEngineTests.exe` |
| 插件测试 | `app/build-nmake/EarLabPluginTests_artefacts/Release/EarLabPluginTests.exe` |

## 5. 命令行用法（自动化 / 便携模式）

```powershell
& ".\app\build-nmake\EarLabEngine_artefacts\Release\EarLab Engine.exe" `
   "C:\path\to\material.wav" --skill=comp --autostart `
   --profile="D:\EarLab\profile.json"
```

| 参数 | 含义 |
|---|---|
| 第一个非 `--` 参数 | 素材路径，存在则自动载入 |
| `--autostart` | 载入后立即开始训练 |
| `--skill=comp` | 选择「压缩量判断」；缺省为「EQ 频段定位」 |
| `--skill=plugin` | 选择「VST3 插件辨别」 |
| `--plugin=路径` | 载入一个 VST3 插件（`.vst3` 包路径） |
| `--qa-auto` | 自动化验收：自动按正确答案作答，跑完一整轮 |
| `--qa-kind=amount` | 自动化验收：强制插件题型（amount / direction / level） |
| `--open-browser` | 启动后直接打开插件库（验收用） |
| `--export-report=路径` | 直接导出报告（`.html` 或 `.csv`）后退出 |
| `--noise=white\|pink` | 不导入素材，直接用白噪 / 粉噪训练 |
| `--page=home\|material\|training\|vst\|assessment\|settings` | 启动时直接打开某个模块 |
| `--assessment` | 启动后直接进入 12 题测评（可配 `--qa-auto` 自动作答验收） |
| `--profile=路径` | 指定档案文件；缺省 `%APPDATA%\EarLab\profile.json` |

## 6. 测试覆盖（97 项断言）

**DSP（10）**：1 kHz 校准、-40 dBFS 校准、60 Hz 衰减、静音门限、双声道 +3.01 dB、多段 EQ 平坦 / +6 dB / 高通衰减 / 高频保留 / 输出有限

**插件（33）**：选项构造（幅度/方向/宽带，纯函数，无需插件）、缓存覆盖判定（全 skipped / 全失败 / 有成功 / 空扫描）、真实插件加载、713 参数枚举、基线透明、3/8 dB 出题校准、选项互异、答案有效、可复现、同 seed 一致、处理器包装、幅度题档位与选项、插件记忆计数/分布/落盘
（不带 `--file=` 时只跑 10 项纯函数与缓存断言，其余 23 项需要本机装着 Pro-Q 4）

**引擎（54）**：多段 EQ 题布局合法、CSV 与 HTML 报告、首次 Elo 查询、A/B 无响度鼓包、单段保留高低切、压缩题选项间距、存档原子写与坏档恢复、白噪/粉噪生成与谱形、噪声可复现、测评结果落盘/载入与等级阈值、设备重启后 trim 保留、测评仓库可清空

| 分组 | 断言 |
|---|---|
| 双链 | 交叉淡化无跳变、淡入到位、湿路延迟 64、干路补偿对齐、A/B 切换无 +3 dB 鼓包 |
| 等响 | 匹配有效、干湿差 < 0.1 LU、对齐 -23 LUFS |
| 素材 | 载入 44.1 kHz WAV、重采样到 48 kHz 且时长不变 |
| 出题 | 同 seed 同题、选项唯一、correctIndex 正确、难度单调 |
| 自适应 | 阶梯反转、阈值范围、Elo 双向 |
| 压缩器 | 稳态 GR 15 dB、快攻击早压、校准到 6 dB |
| 压缩题 | 同 seed 一致、选项唯一、目标在选项内、选项间距 ≥ 1 dB、不越界、答案=实测值 |
| 多段 EQ | 平坦、+6 dB、高通衰减、高频保留、输出有限、单段设置保留高低切 |
| 档案 | 保存、载入、会话数、Elo 汇总、准确率、原子写、`.bak` 备份、坏档不清空 |
| ABX | 完美 20/20、全错 0/20 |

`ctest --test-dir app/build-nmake` 可以一次跑完 DSP / 引擎 / 插件三组。

## 7. A5：VST3 插件宿主

### 7.1 每个插件单独起进程扫描

本机第一次扫描就撞上：某个插件在加载阶段**直接终止进程**（退出码 127），
`try/catch` 和 SEH 都拦不住。所以扫描按「一个插件一个子进程」设计：
子进程只读元数据、写 JSON，父进程收结果；崩溃或超时记为失败，不影响其它插件。

GUI 里默认**不**退回同进程扫描——那会把主程序一起带走。同进程 + SEH 兜底只给测试用，
而且它也有上限（`ExitProcess` 类型的崩溃救不了）。

本机实测（2026-10-23，75 个 `.vst3` 包，Job Object 隔离，4 路并行）：
| 指标 | 结果 |
|---|---|
| 找到插件 | 66 |
| 其中效果器 | 49 / 音源 17（列表不显示） |
| 失败 | 9（8 个 DSP 组件没有独立 VST3 类型、Kontakt 8 访问违例 1） |
| 崩溃隔离 | Kontakt 8 子进程退出码 `0xC0000005`，主程序与其余插件不受影响 |
| 超时强杀 | Job Object 整组终止，不再残留孤儿进程 |
| 耗时 | **53 秒**（并行前 210 秒，约 4 倍） |

失败插件写入黑名单，下次扫描直接跳过；要重试勾选「忽略失败列表」。

### 7.2 不预设插件类型的出题方法

1. 枚举自动化参数，按名字启发式排序，排除旁通/混音/离散参数
2. 每个候选参数拉到两端，用确定性白噪声测 1/3 倍频程能量变化
3. 选出真正能改变频谱的参数（如 Pro-Q 的 `Band N Gain`），
   并自动打开同组开关（Pro-Q 的 `Band N Used` 默认是关的，不打开等于没效果）
4. 二分校准参数值，让实测幅度命中当前难度目标
5. 按实测主频段与方向生成选项

关键实现点：Pro-Q 这类插件的 DSP 只在**整批参数写入**后才跟随控制器状态，
所以每次测量前都要重放「默认值 + 开关」再写目标参数，否则读回值变了但声音不变。

### 7.3 命令行

```powershell
# 扫描本机 VST3（每个插件独立子进程）
.\EarLabPluginTests.exe

# 单个插件跑完整流程：加载 → 参数 → 探针 → 出题 → 断言
.\EarLabPluginTests.exe --file="C:\Program Files\Common Files\VST3\FabFilter\FabFilter Pro-Q 4.vst3"

# 只打印参数表
.\EarLabPluginTests.exe --file="..."
```

### 7.4 实测（FabFilter Pro-Q 4，713 个自动化参数）

| 断言 | 结果 |
|---|---|
| 基线探针（干湿一致） | +0.00 dB |
| 目标 3 dB 题 | Band 2 Gain，实测 +3.00 dB |
| 目标 8 dB 题 | Band 2 Gain，实测 +8.00 dB |
| 复现误差 | 0.00 dB |
| 同 seed 两次生成 | 一致 |
| 包装成处理器后输出变化 | 最大差 0.057 |

### 7.5 界面与训练闭环

![A5 插件训练](../artifacts/a5-window.png)

流程：`加载 VST3` 选 `.vst3` 文件夹 → 自动读描述、加载实例、枚举参数 →
选「VST3 插件辨别」开始训练 → 后台生成题目并校准到当前难度 →
A/B 试听（等响 trim 按插件重算）→ 四选一作答 → Elo / JND 落盘。

实测（Pro-Q 4，10 题自动作答）：`正确 10/10，准确率 100%，JND 1.5 dB，Elo 1140.9`，
档案 JSON 中 `skill=2` 的会话记录完整。

### 7.6 插件库

![插件库](../artifacts/a5-browser-scanned.png)

- 扫描/取消（模态进度窗口，可中途取消）
- 只列效果器，右侧显示分类
- 「添加目录…」把非标准路径加入扫描列表并写缓存
- 「忽略失败列表」强制重试上一次失败的插件
- 选中后「使用选中」直接加载到主界面并切到插件技能

缓存写在档案同目录的 `plugins.json`（插件、失败列表、额外目录）。

### 7.7 题型与报告

| 题型 | 问什么 | 选项 |
|---|---|---|
| bandDirection | B 改变了什么 | 频段 + 方向，4 选 1 |
| bandAmount | B 在某频段的提升/衰减量 | 约 2/4/6/8/10 dB 里取 4 个连续档 |
| levelAmount | B 的整体电平变化量 | 同上（仅宽带效果时出现） |

- 题型默认自动选择：宽带效果 → levelAmount，否则 direction / amount 各半
- 幅度题的难度目标会对齐到最近档位（实测误差 < 0.6 dB）
- 「导出报告」写 CSV（UTF-8 BOM），字段：时间、技能、题数、正确、准确率%、阈值 dB、Elo

### 7.8 A6：多段 EQ 与技能雷达

- **8 段参数 EQ + 高低切**：替代原来的单段峰值滤波器，频响解析计算
- **内置噪声源**：「白噪」「粉噪」按钮，不导入素材也能直接训练；粉噪用 Kellet 三点滤波，统一归一到 −20 dBFS RMS
- **曲线编辑**：拖节点改频率/增益，滚轮改 Q，双击开关；训练中锁定
- **点击新建节点**：在曲线区域任意空白处按下，就在点击位置新建一段并直接进入拖动
  （空闲段默认铺开在 100/200/…/12.5 kHz，8 段用满后不再新建）
- **技能雷达**：三轴（EQ / 压缩 / 插件），Elo 800–1400 映射半径
- 训练仍只启用第 1 段，保证「哪个频段变了」与 4 个选项一一对应

### 7.9 A5e / A6b：插件记忆、多段训练与报告

![A6b 多段与 HP/LP](../artifacts/a6b-window.png)

- **插件记忆**（`plugin-memory.json`）：记住每个插件上次有效的参数，下次出题直接复用；
  记录方向/幅度题各做了多少，之后优先补练得少的那一类
- **多段训练**：主频段之外按 65% 概率再加一段更弱的背景变化（频率远离正确项和所有选项），
  题干会提示「不止一段」；布局由测试保证无歧义
- **HP/LP 手柄**：曲线两侧的三角可拖动截止频率，双击开关
- **报告升级**：`--export-report=路径.html` 或导出按钮；HTML 含汇总卡片、JND 趋势图、
  明细表，浏览器里 Ctrl+P 可直接打印成 PDF；CSV 仍可用

### 7.10 A6c：技能雷达快照

- **雷达首次快照**：灰色多边形是每个技能第一次训练的 Elo，青色是当前值，
  顶部标注「灰=首次」；数据来自 `ProfileStore::getFirstElo`

> **频谱已移除（2026-10-23）**：实时频谱与「输出 / 干声 / 差值」三种模式已从界面和引擎里删掉。
> 原因是 30 Hz 刷新配 2048 点 FFT 时，每次只能取到约 1600 帧，凑不满一帧就退回素材频谱，
> 导致频谱和差值提示来回闪烁。A6c 的截图与相关断言不再适用，`--spectrum=` 参数也已删除。

### 7.11 修复批次（2026-10-23）

针对代码审查（`docs/REVIEW-2026-10-23.md`）里的严重问题做的一轮修复：

| 问题 | 修复 |
|---|---|
| 播放中拖 EQ 会让消息线程和音频线程同时 reset/process 同一个处理器 | 新增 `AudioSuspension`：换素材、换插件、校准、离线测响度全部先摘音频回调；EQ 编辑改成停止操作 250 ms 后再重算等响 |
| 幅度题把正确答案当「目标」显示 | 信息栏不再显示 `amountDb`，只有方向题显示目标幅度 |
| 换素材时重分配播放缓冲，音频线程可能读已释放内存 | `loadFile` 在摘除回调的作用域里替换缓冲 |
| `MultiBandEq` 参数跨线程读、双缓冲可能覆盖读者 | 参数（含 enabled）打包进不可变 `CoeffSet`，三页轮转，音频线程块首取快照 |
| 存档非原子写、坏档静默清空 | 临时文件 + 原子 rename + `.bak`；解析失败保留内存历史并返回 false |
| 插件实例化仍在主进程裸跑 | 加载前先跑 `--vst3-preflight` 隔离子进程（实例化 + prepareToPlay + 首帧 processBlock），崩溃/超时直接拦截；子进程崩溃时不再退回同进程扫描 |
| 宽带方向题生成两个相同的正确项 | 选项构造抽成纯函数 `buildAmountOptions` / `buildDirectionOptions`，返回前保证 4 项互异 |
| 幅度题按档位而不是实测值判分 | 选项与正确答案按实测幅度取最近档位；校准误差 > 1.5 dB 时降级为方向题或判失败 |
| 压缩题在 12 dB 时出现 24 / 24.5 两个近似选项 | 干扰项改为 ±1..6 dB 且最小间距 1 dB；答案取实测 GR |
| A/B 切换瞬间 +3 dB 鼓包 | 等功率交叉淡化改为线性（相关信号） |
| 切换采样率后素材不重采样 | 设备采样率变化时置位，UI 定时器在消息线程重新载入并重算等响 |
| 取消扫描后列表越界 | 取消时丢弃部分结果、刷新索引，绘制/选择处加边界检查 |
| 单段设置清掉高低切 | `setSingleBand` 保留 HP/LP |
| 用户级 VST3 目录少一层 `Local` | 改用 `windowsLocalAppData` |

### 7.12 已知限制

- 只支持 VST3（JUCE 8 自带 VST3 SDK；VST2 需要额外 SDK）
- 无插件 UI：只在参数层宿主，不弹插件自己的编辑器
- 插件加载前会多跑一次隔离预检（通常 1–3 秒），换来「初始化期崩溃不会带走主程序」
- 限制创建子进程的环境（部分沙箱）里扫描判为 SKIP，不会假装通过
- 取消扫描要等当前插件的子进程超时（最长 20 秒）才返回
- 出题依赖「参数能测出频谱变化」，纯时域调制类插件可能没有合适题目
- 插件题的难度目标是「频谱变化幅度」，与内置 EQ 题不是同一量纲，统计分开记录

| 模块 | 文件 |
|---|---|
| 插件目录与 JSON 缓存 | `src/plugin/PluginCatalog.*` |
| 崩溃隔离扫描 | `src/plugin/PluginScanRunner.*` |
| 实例化与参数 | `src/plugin/PluginHost.*` |
| 频段探针 | `src/plugin/PluginProbe.*` |
| 出题 | `src/plugin/PluginTask.*` |
| 接入双链 | `src/plugin/PluginTrainableProcessor.*` |

### 7.13 2026-10-23：六模块界面与测评

窗口右侧是竖排导航，启动默认进入首页：

| 模块 | 内容 |
|---|---|
| 首页 | 设备信息、三项技能当前水平、快捷入口 |
| 素材 | 载入音频 / 白噪 / 粉噪，播放与 A/B 试听，EQ 曲线编辑 |
| 训练 | EQ 频段定位、压缩量判断，自适应难度，能力条与历史曲线 |
| VST 训练 | 选择本机 VST3 后做插件辨别训练 |
| 测评 | 12 题固定考题（EQ 6 + 压缩 6，6/4/3/2 dB 各两轮），完成后生成报告 |
| 设置 | 档案与测评记录路径、音频设置、导出报告 |

**测评规则**：题目难度按专业混音师的平均分辨力固定，不随答题自适应；总分 = 正确率，
等级为入门（<50）/ 进阶（50–69）/ 熟练（70–84）/ 专业（≥85）；同时给出每个分项
「答对的最小增益」作为 JND 估计。

**报告与历史**：结果写入与 profile 同目录的 `assessments.json`（临时文件 + rename），
按时间倒序保留最近 200 次；测评页可回看最近 12 次，重启后仍在。

### 7.14 2026-10-23：P0 视觉改造

按「石墨监听台」方向做的第一轮视觉统一（不改信息架构）：

- **LookAndFeel**：新增 `EarLabLookAndFeel`，接管按钮（主/次两态）、下拉框、输入框、滚动条、
  弹层与菜单配色；正文统一 14px，最小 11px
- **卡片层**：内容区按卡片分组（统计、操作、素材工具条、EQ、训练、插件、报告、设置），
  `surface #15191F` + 1px `#262E39` + 12 圆角
- **左侧导航**：200 宽，品牌 + 6 个导航项；选中态为 `accentWash` 底 + 3px 强调竖条 + 600 字重
- **8pt 栅格**：外边距 16、内容内边距 24、卡片间距 16、控件高 40/32、选项 44
- **空状态**：未开训时不渲染空白选项按钮；EQ 未载入素材时显示引导提示；测评无记录时显示空态文案
- **深色标题栏**：`DwmSetWindowAttribute` 设置沉浸式深色，窗口标题统一为 `EarLab`
- 从首页/测评页点击「开始训练」会自动切到对应训练页

### 7.15 2026-10-24：P1 页头、首页数据卡与答题卡

- **页头**：每页统一的 H1（22/600）+ 右侧状态胶囊（就绪 / 训练中 3/10 / 测评中 5/12 /
  已加载插件 / 未载入素材），随训练与测评进度实时更新
- **首页**：三张技能统计卡（Elo 主值 + JND/准确率 + 空态「暂无记录 · 开始第一轮」）
  + 能力趋势卡（复用历史曲线），替代原来靠空格对齐的三行文字
- **答题卡**：选项改为自绘 `OptionButton`（左侧 A/B/C/D 键位徽标、44 高、正确/错误状态色）；
  上方 10 段进度圆点；答对/答错反馈文字着色
- 训练页在未开始时不再渲染空选项区，页头状态与进度同步

### 7.16 2026-10-24：测评成绩卡、设置分组、EQ 轴与能力条

- **测评页**：报告从只读 `TextEditor` 改成「成绩卡 + 历史表」；成绩卡含总分、等级徽标、
  EQ / 压缩分项正确率与 JND；时间统一为 `2026-10-24 18:18`
- **设置页**：拆成 音频 / 数据 / 关于 三组卡片，新增「打开数据目录」
- **EQ**：左侧 dB 刻度（+12 … -12）、底部 31.5 Hz – 16 kHz 频率刻度与 Hz / dB 单位
- **能力条**：三轴雷达（`SkillRadar`）换成横向能力条（Elo 800–1400 映射、
  首次基线标记、增量徽标），信息密度更高
- **修复**：`--profile=` 现在会重新加载该档案的测评历史；构建脚本检测到头文件更新时
  自动全量重建，避免类布局不一致造成的随机崩溃

### 7.17 2026-10-24：键盘操作、训练小结与开发签名

- **键盘**：`1`–`4` 选答案、`空格` 播放/停止、`Enter` 下一题（小结页为「再练一轮」）、
  `A` / `B` 直接听干声/湿声
- **训练小结**：10 题结束后显示本轮正确数、准确率、JND、Elo、累计轮次，
  并提供「再练一轮」「回首页」
- **播放状态**：播放按钮在播放/停止之间切换，干声/湿声按钮按当前监听源高亮
- **窗口**：最小尺寸改为 1000×880，避免缩小时卡片被压扁
- **开发签名**：`scripts/setup-dev-signing.ps1` 生成自签名证书并在当前用户信任；
  `build.ps1` 每次构建后自动签名，消除 Windows「未知发布者」提示
- **桌面快捷方式**：`scripts/create-desktop-shortcut.ps1` 生成经 cmd.exe 启动的快捷方式，
  绕过 SmartScreen 对本地构建的拦截（自签名证书无法满足 SmartScreen 的信誉要求）

### 7.18 2026-10-24：深色弹窗、首页空态与插件分项入口

- **深色弹窗**：文件选择器改用 JUCE 自绘对话框（跟随深色 LookAndFeel）；插件库与
  音频设置改用非原生标题栏；`PluginBrowser` 的本地色板并入 `Theme.h`
- **首页空态**：趋势卡无数据时显示折线示意 + 「还没有训练记录」+ 引导文案
- **测评页**：新增「插件辨别（独立分项）」卡片，显示插件加载状态与最近 Elo/JND，
  一键跳转 VST 训练。插件链与内置 EQ/压缩链互斥，因此不并入 12 题总分，
  避免破坏现有测评评分与历史数据

### 7.19 2026-10-25：三个体验问题修复

- **EQ 空态提示不消失**：素材/噪声载入后 EQ 面板没有重绘，提示框一直盖在曲线上；
  现由面板 10 Hz 定时器比较 `hasMaterial()` 状态并重绘，载入/切换后立即消失
- **VST3 插件选不了**：上一版把文件选择器改成了 JUCE 自绘对话框，`.vst3` 是目录包，
  自绘浏览器里「打开」一直禁用。文件选择器改回系统原生对话框（`FileChooser` 默认），
  插件库/音频设置两个自绘对话框仍保持深色
- **测评跳页**：测评现在完全在「测评」页里进行（进度点、听 A/B、选项、反馈都在同一页），
  结束后原地显示成绩卡与历史，不再跳到训练页
- **跨页残留选项**：训练控件按所属页面显示，EQ/压缩题不会残留在 VST 页，插件题也不会残留在训练页
- **进度圆点不刷新**：圆点画在父组件上，换题时只更新了数据没有触发重绘；现在换题与答题后
  都显式 `repaint()`，圆点随题号推进
- **训练后立即测评空白**：训练结束的小结状态没有清理，测评页的试听/选项被 `summaryVisible`
  隐藏；开始训练与开始测评时都会先收起小结，再进入新一轮

### 7.20 2026-10-25：代码审查问题修复

- **等响 trim**：`audioDeviceAboutToStart` 不再 `setTrims(0,0)`；恢复回调时按缓存 `match` 重放 trim，
  且只有在采样率/块大小真的变化时才 `chain.prepare`（避免反复 resets 插件与滤波状态）。
  状态栏改为显示实际生效的 trim（`getAppliedTrimsDb`）；构建脚本新增
  `--check-trims` 自检（模拟设备恢复后比较 match 与 applied，失败则构建失败）
- **训练中重启**：`startTraining` 增加 `trainingActive` 守卫；训练期间隐藏并禁用「开始训练」
- **训练中开测评**：`beginAssessment` 检测到训练进行中会中止本轮（明确弹窗告知），
  `finishAssessment` 恢复技能下拉框
- **取消不中断**：`PluginTaskGenerator::generate` 接受 `shouldExit` 回调，候选/校准循环可提前退出；
  扫描等待改为 100 ms 分片轮询，取消时立即终止子进程并标记 `cancelled`，不再记入失败列表、
  也不会阻塞消息线程
- **缓存覆盖**：新增 `PluginCatalog::shouldReplaceCatalog`，全 skipped / 全失败时保留旧缓存
- **换档案串档**：`setProfilePath` 先清空 `assessmentStore` 再载入
- **悬置嵌套**：`AudioSuspension` 改为计数式，可安全嵌套
- **插件换题**：`updatePluginTask` 返回 `bool`，失败时中止本轮而不是静默 no-op
- **静默失败**：载入素材失败给出提示；测评写盘失败弹出警告
- **单声道插件**：处理器按插件实际声道准备，单声道插件逐声道处理，右声道不再直通

## 8. A4 已实现的功能

- **本地档案**：JSON 落盘，记录每轮技能、时间、题数、正确数、JND、Elo
- **跨会话 Elo**：新一轮从该技能的历史 Elo 起算，不再每轮归零
- **能力面板**：每技能显示 Elo / JND / 轮次 / 题数 / 准确率
- **历史曲线**：两条 Elo 曲线（EQ 蓝、压缩橙），最近 12 轮
- **路径可覆盖**：`--profile=` 支持便携安装与自动化测试

> 存储选型：当前用 JSON，零依赖、够用；记录量大到需要按时间/技能查询时再迁 SQLite。

## 9. 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| `无法加载文件 ... 禁止运行脚本` | PowerShell 执行策略 | 加 `-ExecutionPolicy Bypass` |
| CMake 段错误（退出 139） | VS 生成器不兼容 | 改用 NMake |
| 界面中文乱码 | `String(const char*)` 按单字节解释 | 所有中文串 `String::fromUTF8` |
| 截图被裁切 | 进程非 DPI 感知 | `capture_window.ps1` 已调用 `SetProcessDPIAware` |
| 档案没保存 | 运行环境禁止写 AppData | 用 `--profile=` 指向可写目录 |
| 压缩题听不出差别 | 等响后只剩动态/音色差 | 降低难度（目标 GR 调大）再练 |
| 扫描报「环境禁止创建扫描子进程」 | 沙箱/权限策略拦截 `CreateProcess` | 在普通桌面会话运行；或直接用 `--file=` 指定插件 |
| 某个插件扫描超时/崩溃 | 插件自身在加载期出错 | 已隔离，记为失败；其余插件照常可用 |
| 插件题没有效果 | 频段未启用（如 Pro-Q 的 `Band N Used`） | 已自动打开同组开关；仍无效说明该参数测不出频谱变化 |

## 10. 许可证提醒

JUCE 采用 AGPLv3 / 商业许可双轨。开发阶段可用 AGPL；**闭源商业发行前必须购买 JUCE 商业许可**。
