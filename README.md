# ライムライト・レモネードジャム → krkrz_dev (SDL3) 原生 Linux 移植（原型）

用 [wamsoft/krkrz_dev](https://github.com/wamsoft/krkrz_dev)（吉里吉里Z 多平台版，SDL3 + GLES）原生运行游戏，
不经 Wine。资源直接用游戏目录下 `result/`（14 个 xp3 的解包结果）。

- **接手先看 [`HANDOFF.md`](HANDOFF.md)**（现状、工作流、踩过的坑）
- **后续分批计划见 [`PLAN.md`](PLAN.md)**（目标：汉化 + 60Hz + 全功能）

## 目录

| 路径 | 内容 |
|---|---|
| `krkrz_dev/` | 上游 clone（master，递归子模块）。本地改动见 `patches/` |
| `plugins/lllj_compat/` | 兼容层，直接编进引擎（可用引擎内部头文件） |
| `game/` | 运行目录：相对 symlink + 明文 `patch.xp3` + 去码 `patch2.xp3` |
| `run.sh` | 启动脚本 |
| `tools/` | 调试工具（见下） |
| `decomp/` | 游戏脚本反汇编 (`*.dis`) 和解密后的文本脚本 (`text/`)，只供查阅 |
| `ref/` | 参考实现（krkr-ns 等），只读 |
| `patches/` | 对 krkrz_dev / krkrz core 的本地改动 |
| `artifacts/` | 隔离回归的截图、存档、日志及 JSON 报告（不进 git） |

## 从 git 仓库重建

仓库只跟踪我们自己的代码。`krkrz_dev/`、`ref/`、`game/`、`decomp/` 都在 `.gitignore` 里：
- 引擎源码：按 `patches/UPSTREAM.txt` clone 到固定 commit 并打补丁；
- 运行目录：`tools/setup_game.sh`（默认使用同级的 `../limelight/result/`；要求其中有 `unencrypted/`，生成 `patch.xp3` 和含 53 个 pimg 的 `patch2.xp3`；来源大小/时间变化时重打包）；
- 脚本反汇编：`tools/tjsdis.py --tree` / `tools/fefe.py --tree`。

## 构建

```bash
tools/setup_game.sh
tools/build.sh                         # Release + KRKRZ_DRAW_STATS=ON
```

`tools/build.sh` 会处理搬迁后的 CMake 缓存，并复用已下载的 FetchContent 源码。可设 `BUILD_JOBS=8` 控制并行数；参数会传给 CMake。

首次 configure 会通过 vcpkg 编译依赖（约 10 分钟）。系统包：`Mesa-libGLESv2-devel Mesa-libGLESv3-devel yasm systemd-devel libdecor-devel`。

## 运行

```bash
./run.sh                      # 在当前桌面运行（有声音）
HEADLESS=1 ./run.sh           # 自动分配私有 Xvfb + 无声；结束时清理本次进程
REPLCH=/some/dir ./run.sh     # 打开 REPL 文件通道（tools/repl.sh 用）
```

`run.sh` 会把可执行文件复制进 `game/`（游戏用 `System.exePath` 找 `patch.xp3` 和 `plugin64/` 占位 DLL），
并带上 `-used2d=no -debugwin=no`。存档默认在 `${XDG_DATA_HOME:-$HOME/.local/share}/limelight_lj/`，
可用 `./run.sh -datapath=/absolute/path/` 覆盖。

首次导入使用显式命令，保留来源且跳过目标中已有的文件：

```bash
python3 -I tools/import_saves.py                         # 默认导入 ../limelight/savedata_zh/
python3 -I tools/import_saves.py /path/to/old/saves       # 也可导入原生旧目录
```

Alt+Enter 切换全屏；缩放和右键返回已回归。系统操作使用游戏内系统栏和设置页。

## 兼容层做了什么

- **前置脚本** `src/prelude.tjs`（VM 初始化后、startup.tjs 前执行）：
  - 删除 krkrz 定义的 `kirikiriz_generic` 常量，让游戏走 PC 分支（否则它会当成主机版：忽略 `Plugins.link`、开 Switch UI）。
  - 接管 `Plugins.link`：DLL 名 → 静态链接的 krkrz_dev 插件（KAGParserEx、extrans、extNagano、psdfile、saveStruct、scriptsEx、layerEx* …）或 TJS 替身。
  - 替身：PackinOne（数据包 API）、windowEx（Window/System 成员）、menu（`MenuItem`）、win32dialog（对话框一律“取消”）、getLangName、toml、gamepad（`GamepadPort` 空实现）等。
- **原生代码**：
  - `datapack.cpp`：数据包和存档；支持 Windows 加密 TJS/4s0、JPEG APP11 分段/字节转义，以及本移植的缩略图 + TJS/ns0。
  - `toml.cpp`：`Scripts.tomlDecodeFromStorage`（toml++）。
  - `PsbFilePlugin.cpp` / `PSBData.cpp`：`PSBFile` + `psb://` 存储（移植自 krkr-ns；补了 krkrz 的 `iTVPStorageMedia2`，内存流类改名避开与 krkrz 核心同名类的 ODR 冲突）。
  - `TextRenderBase.cpp`：消息排版（移植自 krkr-ns；字宽改为回调脚本的 `onFontChange/onGetTextWidth`，这样预渲染字体也能量）。
  - `compat_main.cpp`：`LLLJNative`（`log`、`attachMembers`：把 TJS 类成员挂到原生类上）。
  - `frame_stats.cpp` / `storage_stats.cpp`：呈现计数、计时快照、资源索引诊断。
- **引擎补丁**：大小写回退、auto-path 增量索引、正常查找失败后的 Hxv4 文件名回退、SDLOGL 计时和 `recreate()` 兼容入口。游戏专用改动由 `KRKRZ_LLLJ` 控制。

## 工具

| 工具 | 用途 |
|---|---|
| `tools/tjsdis.py` | TJS2100 字节码反汇编 |
| `tools/fefe.py` | krkr 简单加密文本（FE FE 0/1/2）解码 |
| `tools/pbd_decode.py` | `.pbd` / Windows JPEG 存档解码，独立 Python 参照实现 |
| `tools/psbdump.py` | PSB（pimg/mtn/scn）结构和资源查看 |
| `tools/xp3plain.py` | 目录打包为明文 XP3 |
| `tools/iterate.sh` | 编译 + 无头运行 N 秒 + 抓报错 |
| `tools/smoke.sh` / `tools/savetest.sh` | 完整场景回归，含普通/快速存读档断言 |
| `tools/benchmark.py` | 1080p 标题、对话各 30 秒及 10 轮转场；`--emote` 测 SD、标题开场、eyecatch |
| `tools/verify_batch1.py` | 索引、哈希、分段存档、导入回归 |
| `tools/verify_resources.py` | 53 个去码资源的挂载优先级/字节及 16 个无名文件完整性 |
| `tools/verify_windows_saves.py` | 复制 Windows 存档后实测恢复、缩略图和推进 |
| `tools/repl.sh` `tools/cap.sh` `tools/click.sh` | REPL 求值 / 截图 / 向指定 REPL 通道的 X11 窗口输入 |

## 回归与测量

输出目录使用新路径；脚本会隔离存档、临时目录（含实例锁）、REPL 和 Xvfb，仅清理自身进程。无头工具需要 `Xvfb`、`xdotool` 和 Python 3。
完整 smoke 从同级 `limelight/补丁/全CG存档/全CG存档.zip` 复制解锁数据到测试目录；源存档保持只读。

```bash
tools/smoke.sh artifacts/smoke-01 6
SKIP_BUILD=1 tools/smoke.sh artifacts/settings-01 --only settings
python3 -I tools/verify_batch1.py artifacts/storage-01
python3 -I tools/verify_resources.py artifacts/resources-01
python3 -I tools/verify_windows_saves.py artifacts/windows-01
python3 -I tools/benchmark.py artifacts/before-01 --legacy-index
python3 -I tools/benchmark.py artifacts/after-01
python3 -I tools/benchmark.py artifacts/desktop-01 --desktop
python3 -I tools/scenarios.py artifacts/emote-01 --only emote   # 全部 43 个 .mtn
python3 -I tools/benchmark.py artifacts/emote-desktop-01 --emote --desktop
```

Smoke 覆盖正常启动/Logo、标题、新游戏推进、backlog、存读档、CG/SD/音乐/立绘鉴赏入口、10 个设置分页、全屏/缩放、右键返回。
`--only emote` 覆盖两个 Logo、31 个 SD、9 种标题背景和 eyecatch（每个都要求画面在动），日志出现 `source unsupported` 即失败。
E-mote 后端可用 `-lllj_emote=cpu|gl` 强制；`LLLJNative.emoteStats()` 返回各阶段耗时。
SD 和 Logo 的 E-mote 画面仍属批次 2。脚本只接受已确认的 `.mtn` 加载和 DSP 调用点例外；其余异常、REPL 失败和超时返回非零。

`./run.sh -lllj_fps=yes` 每秒输出 `(lllj) fps: {...}`，不需要显示统计叠层。TJS 可用
`LLLJNative.resetFrameStats("scene")`、`LLLJNative.frameStats()`、`LLLJNative.storageStats()`。
当前测量钩子覆盖默认 SDLOGL 设备：`frames/fps` 计完成的 Swap，`max_interval_ms` 为相邻 Swap 的最大间隔，
`max_show_ms` 为呈现函数最长 CPU 墙钟时间。`compose_ms` 是 Layer 完成阶段总时间，包含其中的 `upload_ms`；
`wait_ms` 是 Swap 调用耗时，含驱动等待/提交。各阶段是 CPU 墙钟计时，嵌套项不能相加当作独立占比。
报告会记录渲染器；Xvfb 的呈现次数不能当成显示器刷新率。

`-lllj_paths=yes` 输出路径操作、调用栈及扫描事件；`-lllj_index=no` 仅用于以同一构建复现原有全量重建成本。
运行期资源列表变化后用 `Storages.clearStorageCaches()` 显式刷新。基线及验收结果见 `PLAN.md` 批次 1。

## 现状

能用：启动 → ATTENTION → 标题菜单 → はじめから → 序章；背景、事件 CG（pimg 差分）、立绘、头像窗、对话框文字、名字栏、
rule 转场、BGM（opus）、语音（ogg）、快存/快读（含缩略图）。

未做（按计划后续处理）：
- **E-mote**（`motionplayer`）：标题背景、Logo、SD 动画是黑的/缺失，`title_bg.mtn` 加载报错（被捕获）。
- **视频**：krkrz 内置 movie-player 只支持 WebM，游戏是 WMV/MP4。
- 语音特效（wf* DSP 滤镜，电话/对讲机声）——报错被捕获，声音无特效。
- layerExDraw（GDI+ 绘图）、msdfrender、汉化补丁、glibc 2.31 发行构建。
- 颜色选择器和附加系统转场需要批次 5 的 layerExDraw/layerStwCopy；目前保留色样和普通规则转场，记录明确的 stub。

批次 1 已修复 `SetupFullHDMovie` / `onResize`，挂载去码包并补齐存档、索引和测试基础设施。
16 个无名文件保留哈希名，通过 Hxv4 回退查找；原始名字仍未恢复。
