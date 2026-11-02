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

首次 configure 会通过 vcpkg 编译依赖（约 10 分钟）。视频后端用的 FFmpeg 不编译：`tools/build.sh` 先调用 `tools/fetch_ffmpeg.sh`，
下载 [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds) 的 8.1 LGPL shared 预构建包到 `third_party/ffmpeg/`（固定 tag + sha256，tarball 保留在目录里；
BtbN 会清理旧 autobuild，失效时换成新的 n8.1 lgpl-shared 资源并更新脚本里的三个常量）。`run.sh` 把 `libav*`/`libswresample` 复制进 `game/`，
可执行文件的 `$ORIGIN` RPATH 保证用的是这份而不是系统同 soname 的库。系统包：`Mesa-libGLESv2-devel Mesa-libGLESv3-devel yasm systemd-devel libdecor-devel`。

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
  - 替身：PackinOne（数据包 API）、windowEx（Window/System 成员）、menu（`MenuItem`）、win32dialog（对话框一律“取消”）、getLangName、toml 等。gamepad.dll 不模拟：prelude 默认 `-joypad=dinput`，游戏改用引擎自带的 SDL 手柄（VK_PAD* 键）。
- **原生代码**：
  - `datapack.cpp`：数据包和存档；支持 Windows 加密 TJS/4s0、JPEG APP11 分段/字节转义，以及本移植的缩略图 + TJS/ns0。
  - `toml.cpp`：`Scripts.tomlDecodeFromStorage`（toml++）。
  - `PsbFilePlugin.cpp` / `PSBData.cpp`：`PSBFile` + `psb://` 存储（移植自 krkr-ns；补了 krkrz 的 `iTVPStorageMedia2`，内存流类改名避开与 krkrz 核心同名类的 ODR 冲突）。
  - `layerexdraw/` + `third_party/plutovg/`：layerExDraw（GDI+ 模拟，移植自 krkr-ns，PlutoVG 光栅化；PathGradient 按 GDI+ 三角扇语义重写），供流程图和颜色选择器使用。
  - `textrender/`：`TextRenderBase`（textrender.dll：排版、逐字延迟、按键等待、链接、注音；移植自 krkrsdl3，按 Windows 原版实测修正，见 `textrender/UPSTREAM.txt`；`tools/textrender_compare.py` 在 Wine 原版与本移植上跑同一探针对比）。
  - `layer_extras.cpp`：`Layer.stitchWrappedCopy`（layerStwCopy，按原 DLL 反汇编实现，系统条纹转场）、`Layer.fetchImageSize`、`Storages.saveOctet`、`System.getKnownFolderPath`。
  - `msdfrender.cpp`：MSDF/SDF 图集字体（`makeGlyphMSDF/SDF`、`operateGlyphToProvince`、`makeBitmapFromProvince`、`getAscentHeight`，按原 DLL 反汇编实现）。
  - `compat_main.cpp`：`LLLJNative`（`log`、`attachMembers`：把 TJS 类成员挂到原生类上）。
  - `frame_stats.cpp` / `storage_stats.cpp`：呈现计数、计时快照、资源索引诊断。
  - `FFmpegMoviePlayer.cpp`：VideoOverlay 的 mp4/wmv/asf 后端（libavformat/libavcodec，结构仿 krkrz 的 `Mpeg1MoviePlayer`：
    自有解码线程、以音频 sink 的已播放采样数为主时钟、seek 后先压住音频直到首帧就绪），经引擎补丁的 `TVPExternalMoviePlayerFactory` 注册。
    `LLLJNative.movieStats()` 返回最近一个视频的出帧/丢帧/最大落后/解码耗时/欠载。
  - `wave_filters.cpp`：`voiceeffect.tjs` 用的 WaveSoundBuffer 滤镜（`WaveDSPFilter`、`DelayEffect`、`StkFreeVerb`），和引擎的 PhaseVocoder 一样放进 `buf.filters`。
    一个宿主类负责格式转换、segment 透传和音源结束后的尾音（`extend` 毫秒），各效果只处理交错 float。
    `WaveDSPFilter` 用 vendor 的 DSPFilters（MIT，`third_party/DSPFilters/`），类型/族名与参数顺序与原插件一致。
    `LLLJNative.waveFilterGain(filter, hz)` 离线测幅频响应（测试用）。
  - `multitrack.cpp`：`.mchx` 多轨 BGM 解码器（清单里的多声道 opus 按 `track` 声道对混成立体声，各轨增益可渐变），
    `WaveSoundBuffer.multiTrackOpenCallbacks` / `setMultiTrackMask`（`MultiTrackMusicSupport` 用）。
- **引擎补丁**：大小写回退、auto-path 增量索引、正常查找失败后的 Hxv4 文件名回退、SDLOGL 计时和 `recreate()` 兼容入口、
  外部视频播放器工厂钩子、VideoOverlay 的 `setPeriodEvent` / `setSegmentLoop`。游戏专用改动由 `KRKRZ_LLLJ` 控制。

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
| `tools/benchmark.py` | 1080p 标题、对话各 30 秒及 10 轮转场；`--emote` 测 SD、标题开场、eyecatch；`--movie` 测 OP/ED 播放 |
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
python3 -I tools/scenarios.py artifacts/movie-01 --only movie   # 鉴赏里的 OP + 7 个 ED
python3 -I tools/scenarios.py artifacts/movie-layer-01 --only movie --vomstyle layer   # 强制模式，只播 OP
python3 -I tools/benchmark.py artifacts/movie-desktop-01 --movie --desktop
python3 -I tools/emote_compare.py artifacts/compare-01      # Wine 原版 vs 移植，需要 wine/Xvfb/ImageMagick
python3 -I tools/scenarios.py artifacts/audio-01 --only audio   # 语音特效 + 多轨 BGM
python3 -I tools/scenarios.py artifacts/extras-01 --only extras # 批次 5：流程图/颜色轮盘/条纹转场/图集字体/转场插件
```

Smoke 覆盖正常启动/Logo、标题、新游戏推进、backlog、存读档、CG/SD/音乐/立绘鉴赏入口、10 个设置分页、全屏/缩放、右键返回。
`--only emote` 覆盖两个 Logo、31 个 SD、9 种标题背景和 eyecatch（每个都要求画面在动），日志出现 `source unsupported` 即失败。
E-mote 后端可用 `-lllj_emote=cpu|gl` 强制；`LLLJNative.emoteStats()` 返回各阶段耗时。
`--only movie` 从鉴赏入口逐个播放 8 个视频：核对打开的文件、跳到 30 秒后画面在变且位置前进，
再分别以播完（跳到结尾前 3 秒）、右键、Esc 返回鉴赏，并要求视频落后音频时钟不超过 60ms。
`--only audio`：带通幅频响应、剧本引用的 16 个特效 + `wf_reverb` 经游戏自己的 `loadFilter`/`VoiceEffectFactory` 构造后播放
（有输出、尾音长度符合 `extend`）、语音缓冲的 `语音|特效` 路径和口型电平、14 个 `.mchx` 的打开回调与静音/恢复。
`--only extras`：流程图连线、颜色轮盘色相、手柄设置页、backlog 条纹转场计时、`fetchImageSize`/`saveOctet`/`getKnownFolderPath`、
mosaic/turn 及 extNagano 六种转场（`tools/emote_test.ks` 标签）。
任何 `exception occured`、REPL 失败和超时都返回非零（不再有已知例外）；`(lllj) stub:` 只允许 `scenarios.py` 的 `WIN32_STUBS`
（`setDpiAwareness`、`readRegValue`、`setIconicPreview`，均为 Windows 专有、无对应功能），`msdfrender not linked` 也算失败。
`tools/emote_compare.py` 除 E-mote 外还对照流程图、设置页、mosaic/turn/条纹转场中途和名字栏（系统画面用 `kag.sysbase` 的 `piledCopy` 截取）。

`./run.sh -lllj_fps=yes` 每秒输出 `(lllj) fps: {...}`，不需要显示统计叠层。TJS 可用
`LLLJNative.resetFrameStats("scene")`、`LLLJNative.frameStats()`、`LLLJNative.storageStats()`。
当前测量钩子覆盖默认 SDLOGL 设备：`frames/fps` 计完成的 Swap，`max_interval_ms` 为相邻 Swap 的最大间隔，
`max_show_ms` 为呈现函数最长 CPU 墙钟时间。`compose_ms` 是 Layer 完成阶段总时间，包含其中的 `upload_ms`；
`wait_ms` 是 Swap 调用耗时，含驱动等待/提交。各阶段是 CPU 墙钟计时，嵌套项不能相加当作独立占比。
报告会记录渲染器；Xvfb 的呈现次数不能当成显示器刷新率。

`-lllj_paths=yes` 输出路径操作、调用栈及扫描事件；`-lllj_index=no` 仅用于以同一构建复现原有全量重建成本。
运行期资源列表变化后用 `Storages.clearStorageCaches()` 显式刷新。基线及验收结果见 `PLAN.md` 批次 1。

绘制线程：`-drawthread=N` 指定线程池大小（默认按允许运行的物理核数）；`-drawthreadchunk=N` 指定每份最少 `factor×N` 像素（默认 10000，0 = 引擎原行为，即超过阈值就切给所有线程）。
启动日志 `(lllj) renderer:` 一行会给出实际的 `drawpool` 和 `drawchunk`。

## 现状

能用：启动 → ATTENTION → 标题菜单 → はじめから → 序章；背景、事件 CG（pimg 差分）、立绘、头像窗、对话框文字、名字栏、
rule 转场、BGM（opus）、语音（ogg）、快存/快读（含缩略图）；E-mote（批次 2）；OP/ED 视频（批次 3）；
语音特效与多轨 BGM（批次 4）；流程图、颜色选择器、系统条纹转场、图集字体、手柄（批次 5）。

未做（按计划后续处理）：
- 汉化补丁（批次 6）、60Hz 优化（批次 7）、glibc 2.31 发行构建。

批次 1 已修复 `SetupFullHDMovie` / `onResize`，挂载去码包并补齐存档、索引和测试基础设施。
16 个无名文件保留哈希名，通过 Hxv4 回退查找；原始名字仍未恢复。
