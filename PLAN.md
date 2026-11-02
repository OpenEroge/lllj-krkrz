# PLAN — 分批完成到「汉化 + 60Hz + 全功能」

最终目标（全部满足才算完成）：
1. **汉化**：日 / 简 / 繁可在游戏内切换，效果等同 Windows 下的 `lllj_zh.exe`（中文标题动画、正文、UI、OP/ED 字幕），去码 CG 生效。
2. **60Hz**：1080p 下标题动画、SD 动画、转场、对话推进、视频播放时稳定 60fps（以实测为准，见批次 1 的测量工具）。
3. **全功能**：E-mote、视频、语音特效、所有转场/UI 特效、鉴赏模式、存读档、设置项都与 Wine 下原版一致，日志里没有未处理异常。

每个批次都可以单独做、单独验收。依赖关系：

```
批次1 基础设施/测量 ──┬─> 批次2 E-mote ─────┐
                      ├─> 批次3 视频 ────────┼─> 批次6 汉化 ─> 批次8 全流程验收/打包
                      ├─> 批次4 音频特效      │
                      └─> 批次5 其余插件/特效 ┘
                                批次7 性能 60Hz（批次1之后随时可做，批次2/3完成后必须再做一遍）
```

每批完成的通用要求：`tools/smoke.sh` 回归通过；更新本文件的勾选、`HANDOFF.md` 的「已知问题」；有引擎改动就重新导出 `patches/`。

---

## 批次 1：基础设施、测量和小修

目标：后面每一批都依赖的东西先补齐；把“看得见的小毛病”清掉。

- [x] **帧率测量**：加 `LLLJNative` 或启动参数（如 `-lllj_fps=yes`），每秒输出实际呈现帧数、最长帧耗时、Layer 合成/上传/等待各占多少。
      先看 krkrz 自带的 `KRKRZ_DRAW_STATS` 和 `-drawthread`，能复用就复用。记录当前基线（标题、对话、转场三个场景）。
- [x] **自动化测试脚本**：把 `smoke.sh` 扩成场景脚本（TJS，经 REPL 或 `-replfile` 注入）：
      Logo → 标题 → 新游戏推进 N 句 → backlog → 存/读档 → 鉴赏（CG/SD/音乐/立绘）→ 设置页每一页 → 回标题。每步截图 + 帧率记录。
      用 `kag.process(storage, label)` 直接跳到指定场景，比点击稳定。
- [x] **`Rebuilding Auto Path Table` 反复触发**（每次约 0.3 秒）：查是谁在运行时调用 `Storages.addAutoPath/removeAutoPath`
      （在 prelude 里包一层打印调用栈 `Scripts.getTraceString()`），对症处理（缓存/改成只建一次/在引擎侧做增量重建）。
- [x] **被捕获异常**清零：`uisupport.tjs SetupFullHDMovie`、`mainwindow.tjs onResize`、标题的 `affinesourceimage loadImages`（后者属批次 2，先确认原因）。
- [x] **去码 CG**：`result/unencrypted/`（53 个 pimg）以最高优先级挂上。`initialize.tjs` 会依次挂 `patch2.xp3, patch3.xp3 …`（遇到第一个不存在的就停），
      所以用 `tools/xp3plain.py` 打成 `game/patch2.xp3` 最省事；先验证“后挂的优先”。
- [x] **16 个无名文件**：在存储查找失败时按 Hxv4 规则把文件名算成 BLAKE2s-256 的 64 字符十六进制名再查（算法在 `<游戏目录>/xp3tool/` 的 Rust 代码里），
      或者干脆离线反查出名字后在 `game/` 里补 symlink。
- [x] **存档目录**改成 `~/.local/share/limelight_lj/`（看 krkrz 的 `-datapath` / appconfig 机制），通过显式命令可选导入 `<游戏目录>/savedata_zh/`。
      Windows 存档是 TJS/4s0 加密（iv = `System.title`），`loadDataPack` 已能解，需实测读档。
- [x] 系统菜单/快捷键：全屏切换（Alt+Enter）、窗口缩放、右键菜单的替代品是否可用。

验收：基线帧率数据写进本文件；smoke 日志里没有 `exception occured`（除批次 2/3/4 负责的）；去码 CG 在鉴赏模式可见。

### 批次 1 验收记录（2026-11-02）

- 完整回归：`artifacts/batch1-complete/report.json`（启动/Logo、推进 6 句、backlog、普通及快速存读档、鉴赏入口、10 个设置页、Alt+Enter、1280×720 缩放后真实点击、右键返回）。
- Windows 存档：`artifacts/batch1-windows-complete/report.json`，按独立 Python 解码结果核对场景及 checkpoint 628，实际恢复 `savedata_zh/data_continue.jpg` 并推进，缩略图可读，来源 SHA-256 保持不变。
- 资源：`artifacts/batch1-resources/report.json`，53 个 pimg 均解析到 `patch2.xp3` 且与来源 MD5 一致；16 个无名文件可读。真实原名未反查，运行时回退通过独立哈希向量和合成逻辑名测试。
- 存储/容器/导入：`artifacts/batch1-storage-trace/report.json`，含 Release 调用栈、优先级、重复挂载、删除回退、负缓存、写入、compact、显式刷新、分段 JPEG 和截断拒绝。
- 异常验收只接受已确认的 `.mtn` 加载与 `WaveSoundBuffer.StkFreeVerb` 调用点。`SetupFullHDMovie`、`onResize` 已消失。颜色轮盘和附加系统转场记录为批次 5 的 stub。
- 存档格式补充：Windows JPEG 使用 APP11 分段（每段最多 16KiB 解码字节）及 `FF 00` 转义；直接从 `TJS/` 截到文件尾会误报 LZ4 损坏。已补容器解析和本作汉化存档 ID 兼容。

### 1080p 基线

同一 Release 构建，`KRKRZ_DRAW_STATS=ON`、drawthread=auto、dummy 音频；标题/对话各 30 秒，设置打开/返回共 10 轮。
“旧索引”通过 `-lllj_index=no` 复现原有失效/全量重建行为，其余兼容修复相同；按顺序运行三组采样。
Xvfb 为 llvmpipe（LLVM 23.1.3），真实桌面为 AMD Radeon 880M；两者均为 GLES 3.2 / Mesa 26.2.5。
原始报告：`artifacts/batch1-measure-before/`、`artifacts/batch1-measure-after/`、`artifacts/batch1-measure-desktop/`。

| 环境/索引 | 场景 | 采样(s) | Swap/s | 最长间隔(ms) | Layer(ms/s) | 上传(ms/s) | Swap耗时(ms/s) |
|---|---|---:|---:|---:|---:|---:|---:|
| Xvfb/旧索引 | 标题（缺 E-mote） | 30.05 | 419.8 | 4.8 | 11.5 | 0.06 | 946.4 |
| Xvfb/旧索引 | 对话 | 30.05 | 419.9 | 4.5 | 13.7 | 0.42 | 945.1 |
| Xvfb/旧索引 | 10 轮转场 | 18.65 | 108.6 | 652.9 | 278.7 | 54.55 | 269.9 |
| Xvfb/增量索引 | 标题（缺 E-mote） | 30.01 | 421.5 | 5.7 | 10.7 | 0.06 | 950.0 |
| Xvfb/增量索引 | 对话 | 30.04 | 424.8 | 5.0 | 13.0 | 0.37 | 947.1 |
| Xvfb/增量索引 | 10 轮转场 | 12.78 | 161.2 | 163.6 | 401.7 | 79.16 | 397.4 |
| 桌面/增量索引 | 标题（缺 E-mote） | 30.01 | 119.9 | 16.8 | 2.5 | 0.01 | 995.1 |
| 桌面/增量索引 | 对话 | 30.04 | 119.6 | 28.5 | 6.4 | 0.38 | 989.7 |
| 桌面/增量索引 | 10 轮转场 | 13.34 | 97.1 | 216.4 | 404.2 | 57.32 | 384.0 |

Layer 列是包含上传子项的完成阶段墙钟时间；Swap 耗时包含驱动等待/提交，各列不能相加当作独立 CPU 占比。
计数代表完成的 Swap，不代表显示器 scanout 刷新率。标题/Logo/SD 的 E-mote 仍缺失，这些数值不构成全功能 60Hz 验收。

10 轮转场中，目录扫描 **966 → 0**、索引重建 **21 → 0**；存档/设置写入仍是 20 次。
Xvfb 总耗时 **18.65s → 12.78s**，最长呈现间隔 **652.9ms → 163.6ms**。
真实桌面转场最长 **216.4ms**，仍需批次 7 处理布局、合成及资源加载卡顿。

---

## 批次 2：E-mote（motionplayer）

目标：标题背景动画、yuzulogo/m2logo、eyecatch、SD 小剧场（`sdmotion/` 31 个 + patch 2 个，共 43 个 .mtn，PSB v4）全部正常且满帧。

- [x] 移植 krkr-ns emoteplayer（MIT）到 `plugins/lllj_compat/emote/`，与上游的差异见其中 `UPSTREAM.txt`。
      ncbind 绑定表原样保留（shim 让它用引擎内部头文件，类名私有化避免与静态插件合并）。
      GL 后端（私有 SDL GL 上下文光栅化网格，读回 Layer）用于硬件 GL；软件 GL 或自检失败时用 CPU 光栅化；`-lllj_emote=cpu|gl` 可强制。
- [x] 只放 `motionplayer_nod3d.dll` 占位，prelude 映射到静态插件 `lllj_emote`；不需要 `-nod3dm`。游戏实际走 `D3DAdaptor`（krkr-ns 的 CPU 读回实现）。
- [x] 方法核对：`play/playTimeline/getMainTimelineLabelList/setVariable/setDrawAffineTranslateMatrix/serialize/progress/skipToSync` 均已实现。
      `initPhysics`、`setTimelineBlendRatio`、wind 等只在 E-mote 角色文件（`_storageType=="emote"`，带 metadata 的 PSB）分支调用，本作没有这类资源，krkr-ns 中仍是空实现。
- [x] 资源：43 个 `.mtn` 全是明文 PSB v4，脚本不调用 `setEmotePSBDecryptSeed`。
- [x] 修正 krkr-ns：`loopTime` 是循环起点（SD 先播开场再循环）；变量切换参数化节点时子动画从头播放、参数化主动画时钟不钳制（eyecatch）；
      补上粒子发射器（eyecatch 角色卡与泡泡、SD002/007/101/501），语义参照 [krkr2 motionplayer](https://github.com/2468785842/krkr2/tree/main/cpp/plugins/motionplayer) 的逆向实现。
- [x] 与 Wine 原版视觉对照：`tools/emote_compare.py`（见下方验收记录）。原版在 Wine 下需 `-nod3dm=yes -used2d=no`（D3D 动画播放器、D2D 绘制设备在 Wine 中失败），
      通过游戏自带的 CialloHook（镜像目录的 `unencrypted/` 覆盖 `custom.tjs`）注入同一份截图脚本 `tools/emote_compare.tjs`，两边用 KAG 的 `captureSnapshot` 在相同时间点截图。
      遮罩/混合（eyecatch 舞台遮罩、SD 叠加）、开场→循环段、粒子（eyecatch 角色卡与泡泡、SD002/007/101/501）均与原版一致。
      物理、时间轴混合：只在 E-mote 角色文件（`_storageType=="emote"`，带 metadata 的 PSB）分支调用，本作 43 个 `.mtn` 全部 `metadata=None`、没有角色 PSB，
      原版运行时这些接口同样不会被调用；眨眼等在 `.mtn` 里是普通关键帧，已包含在对照中。

验收：标题 9 种背景（`title_bg0..7`、`title_bg_trial`，需全通存档解锁）、两个 Logo、eyecatch、全部 SD 场景播放正确；E-mote 场景 60fps。

### 批次 2 验收记录（2026-11-02）

- 全覆盖场景：`tools/scenarios.py --only emote`（`artifacts/batch2-emote/`，GL 强制版 `artifacts/batch2-emote-gl/`）。
  两个 Logo、全部 31 个 SD 文件、9 种标题背景、eyecatch 都在播放（截图有变化），43 个 `.mtn` 全部加载，日志无异常，无 `source unsupported`。
- 完整 smoke：`artifacts/batch2-smoke/`，只剩批次 4 的 DSP 异常。
- 与原版对照：`python3 -I tools/emote_compare.py artifacts/batch2-compare`（`report.json` 为逐对 RMSE，`sheets/` 左原版右移植）。
  6 个 SD（含 4 个带粒子）、`title_bg0`、`title_bg_trial`、标题、eyecatch，共 43 对截图。SD 全部时间点、标题背景定格帧 RMSE ≤0.035（多数 ≤0.008）；
  较高值只出现在开场/转场进行中的帧（原版在 Wine 中首次加载更慢，开场整体晚约 300–500ms）和 eyecatch 定格帧（角色卡是 `particleMotionList` 里随机选的 4 个变体之一，两边抽到的不同）。
- 帧率：`tools/benchmark.py --emote [--desktop]`，1080p，dummy 音频，`artifacts/batch2-measure-{desktop,xvfb}/`。

| 环境/后端 | 场景 | Swap/s | 最长间隔(ms) |
|---|---|---:|---:|
| 桌面 AMD 880M（120Hz）/ GL | SD105（最重） | 119.9 | 17.8 |
| 桌面 / GL | SD001 | 119.9 | 16.0 |
| 桌面 / GL | 标题开场 | 119.8 | 14.5 |
| 桌面 / GL | eyecatch（叠在场景上） | 70.7 | 38.5 |
| Xvfb llvmpipe / CPU | SD105 | 98.9 | 26.2 |
| Xvfb llvmpipe / CPU | eyecatch | 20.2 | 109.9 |

在目标硬件（有 GPU，引擎本身就要求 GLES 3.2）上 E-mote 场景平均都达到 60fps。Xvfb 行是 llvmpipe 软件 GL：那里整个引擎都在 CPU 上渲染，
E-mote 自动改用 CPU 光栅化，不是 60Hz 的目标环境，只作回归用。批次 7 继续压帧时间：eyecatch 每帧读回 1080p 目标（约 5ms）再做 RGBA→BGRA，
首次出现时上传贴图，最长间隔 38ms（>25ms 的批次 7 标准）。

---

## 批次 3：视频

目标：OP（`op.mp4` / `op.wmv`）、7 个 ED（`ed1_ena … edg_gra.wmv`，均 1080p）、鉴赏「ムービー」、以及剧情里的图层视频（`gfx_movie.tjs`）全部正常。

实际素材：`op.mp4` 为 H.264 + AAC 24fps，`op.wmv` 与 7 个 ED 为 **WMV3 + WMA Pro**（不是 wmav2）24/30fps，全部 1080p。
资源里没有图层视频（无 webm/mpg/amv），所有视频都经 `[sysmovie]` / `[opmovie]`（`macro.ks`、`replay.ks`）走 overlay。

- [x] **FFmpeg 后端** `plugins/lllj_compat/src/FFmpegMoviePlayer.cpp`：直接实现 `iTVPMoviePlayer`，结构照搬 krkrz 的 `Mpeg1MoviePlayer`
      （自有解码线程、音频 sink 已播放采样数为主时钟、prepare 帧、seek/loop/pause），I420 走现有 GPU YUV 路径，layer 模式用 libyuv 转 BGRA。
      引擎补丁只加 `TVPExternalMoviePlayerFactory` 钩子（`movie.cpp` 两个重载开头）。FFmpeg 不编译：`tools/fetch_ffmpeg.sh` 下载 BtbN 8.1 LGPL shared 预构建，
      `$ORIGIN` RPATH（engine 已用 `--disable-new-dtags`，对 libavcodec 自己的 libswresample 也生效）+ `run.sh` 复制进 `game/`。
      处理了两个素材特性：WMA Pro 在 seek 后输出无时间戳的帧（按上一帧结束 / 最近音频包时间推算）；WMV3 关键帧间隔 5 秒，
      seek 后先压住音频直到首帧就绪，否则追帧期间音频先跑、视频丢帧。
- [x] 模式：SDL 版 `vomMFEVR`→`vomOverlay`，mixer/overlay 同一 presenter。原版的 `SelectMovieFile` 只在 `checkWindowsVersion.canMFEVR`
      且风格为 mfevr 时选 `.mp4`，Linux 上判定为非 Windows 而选了 wmv；prelude 让 `System.getOSVersion` 报 Windows 10（原版目标环境），
      现在与 Windows 一致选 `op.mp4`。该分支的 `SetupFullHDMovie` 会 unlink 从未 link 的 `krmovie.dll`，prelude 的 unlink 对替身/静态插件改为 no-op。
      `onStatusChanged` / 播放结束返回、`setPeriodEvent` / `setSegmentLoop`（引擎原为空函数，已在 `CheckUpdate` 按帧号实现）。
- [x] 图层视频：`layerExBTOA`（`copyBottomBlueToTopAlpha` 等）早已静态链接并映射；素材中无图层视频，`--vomstyle layer` 强制 layer 模式播 OP 验证 ARGB 路径。
      字幕 CSV / MovieSE 没有对应素材触发。
- [x] 音画同步、跳过、seek、播完返回。左键跳过只在 `tflags.sysmovie_skip` 时生效（游戏脚本设计），右键/Esc 走 `rightClickHook` / `keyDownHook`。

验收：8 个视频完整播放、音画同步、结束后正确返回；视频期间 60fps；剧情中的图层视频正常。

### 批次 3 验收记录（2026-11-02）

- 全覆盖：`python3 -I tools/scenarios.py artifacts/batch3-movie --only movie`。8 个视频都打开预期文件（OP 为 `op.mp4`），跳到 30 秒后画面变化、位置前进；
  OP 与 5 个 ED 跳到结尾前 3 秒后自然播完返回鉴赏（2.4–4.4 秒），ED2 右键、ED3 Esc 跳过（0.6–0.7 秒）。视频最大落后音频时钟 ≤32ms，无音频欠载，
  丢帧除 ED5 跳到结尾那段 27 帧外均为 0–1。
- 强制模式：`--vomstyle layer` / `--vomstyle mixer`（`artifacts/batch3-movie-{layer,mixer}/`，此时原版逻辑选 `op.wmv`）通过；layer 截图颜色正确。
- 回归：`artifacts/batch3-smoke/`（只剩批次 4 的 DSP 异常）、`artifacts/batch3-emote/` 通过。
- 帧率：`tools/benchmark.py --movie [--desktop] --seconds 15`，1080p，dummy 音频，`artifacts/batch3-measure-{desktop,xvfb}/`。

| 环境 | 视频 | Swap/s | 最长间隔(ms) | 出帧 | 丢帧 | 最大落后(ms) | 解码(ms/帧) |
|---|---|---:|---:|---:|---:|---:|---:|
| 桌面 AMD 880M（120Hz） | OP（H.264） | 119.9 | 26.5 | 419 | 0 | 21.3 | <1（帧线程，只计提交） |
| 桌面 | ED1（WMV3） | 119.7 | 27.5 | 514 | 0 | 21.3 | 5.9 |
| Xvfb llvmpipe | OP | 153.8 | 8.2 | 403 | 0 | 21.3 | — |
| Xvfb llvmpipe | ED1 | 154.1 | 7.9 | 502 | 0 | 21.3 | 6.3 |

桌面最长间隔 26–28ms 略超批次 7 的 25ms 标准，留给批次 7。音频实听（真实声卡下的口型/节拍同步）需在桌面 `./run.sh` 手动确认。
字幕（批次 6）：overlay 模式下视频在最上层，字幕需烧进帧；播放器的 `Deliver` 是接 libass 的位置。

---

## 批次 4：音频与语音特效

目标：`sysscn/voiceeffect.tjs`（剧本里调用约 1700 次：电话、对讲机、混响、回声…）和语音相关设置全部生效。

- [ ] `wfBasicEffect` / `wfTypicalDSP`：WaveSoundBuffer 滤镜（`BandPass/Butterworth`、`GraphicEqualizer`、`StkFreeVerb`、`DelayEffect`、`Vocoder`、`Osc*`…）。
      先确认 krkrz 的 `WaveSoundBuffer.filters` 接口（`common/sound/`）怎么挂自定义滤镜；行为规格可参考 Kirakira（AGPL，只看不抄）。
- [ ] `wvdecoder`（`WaveSoundData`）、`wumultitrack`（`setMultiTrackMask`、`.mchx`）。
- [ ] `getSample`（口型同步）已静态链接，确认立绘嘴型随语音动。
- [ ] 音量/声道设置、角色单独音量、BGV、「お気に入りボイス」。

验收：抽查 10 处带特效的语音和 Wine 版对比听感一致；`DSP` 异常消失。

---

## 批次 5：其余插件和特效

- [ ] **PackinOne 剩余部分**（按脚本实际调用补）：`gaussianBlur`、`colorize`、`copyRaster`、`generateWhiteNoise`、`SliceLayer`/`loadSlicedImage`、
      `Array.save2`、`Scripts.safeEvalStorage/getObjectContext/equalStruct`、`Storages.dirlistEx` 等。用
      `grep -rhoE "calld .*'(名字)'" decomp/` 列出实际调用，再对照 krkrz_dev 已有插件（scriptsEx、saveStruct、layerEx*）。
- [ ] **layerExDraw**（GDI+：`drawPathString`、`drawBezier`、`measureString`、`GdiPlus.addPen/addBrush`…；用于 `exroll.tjs`（片尾滚动）、
      `scnchart_ui.tjs`（流程图）、`affinesource*`）：移植 `ref/krkr-ns/.../plugins/layerexdraw`（有抽象 `DrawBackend.h`）。
- [ ] **msdfrender**（`prerenderfontex.tjs` 的 `sysfont/headfont/numfont…_atlas`）：确认现在的回退渲染与原版观感差异，必要时实现。
- [ ] **extNagano**（blurfade、flutter、honeyturn、multiripple、scanline、zoomfade、spin）和 **extrans**（mosaic 在 184 个 scn 里用到）逐个跑一遍。
- [ ] GlitchEffect、layerStwCopy（`stitchWrappedCopy`）、k2compat 需要的原生部分、`System.fetchImageSize` 等零散 API。
- [ ] 手柄：`GamepadPort` 现在是空壳，接 krkrz 自带的 SDL 手柄输入（`doc/Gamepad.md`）。
- [ ] 流程图（フローチャート）、backlog、语音回放、截图功能（snapcapture）、鉴赏模式各页。

验收：自动化场景脚本覆盖的每个画面与 Wine 截图一致；日志无异常、无 `(lllj) stub:` 以外的警告（stub 也要逐个评估是否可接受）。

---

## 批次 6：汉化（复刻 lllj_zh.exe / lllj_zh.dll）

目标：中文正文、UI、标题动画、字幕；日/简/繁游戏内热切换；可读 Windows 汉化版存档。

- [ ] **解包 `lllj_zh.pck`**：自定义 `XPCK` 容器（头 `XPCK 00000001`，索引/数据段为 zstd），约 508 个文件。写 `tools/xpck_unpack.py`
      （上次 krkrsdl2 会话已成功解过，格式：`File/info/segm` 风格索引 + zstd 段）。输出到 `<游戏目录>/result/zh/`（新目录，不碰原有数据）。
- [ ] 内容（上次分析）：改过的 `default.tjs`（`CUSTOM_MULTILANG=1`）、`multilang.tjs`、`uitext.tjs`、`AppConfig.tjs`、`*_cn.pbd`、`uitexts_cn.toml`、
      思源字体、`title_bg*_cn.mtn`、带日简繁三语的 138 个 `.scn`、OP/ED 的 `.ass` 字幕。
      其中一批伪装成 `.png` 的 WebP（上次统计 35/123，需重新确认）：离线转成真 PNG，或给引擎加 WebP 解码（libwebp）。
- [ ] **挂载**：打成 `game/patch3.xp3`（排在去码包之后）或在 prelude 里 `Storages.addAutoPath` 最后挂，保证中文文件优先。
      `yuzu_patchdetect.tjs` 的联网更新检查强制关闭。
- [ ] **语言重定向**（原 DLL 的 `Multilang.RedirectTable`）：按当前语言把 `title_bg*.mtn` → `*_cn.mtn`、`*.ass` → `_chs/_cht` 等。
      实现方式：prelude 里包 `Storages` 查找，或在 `lllj_compat` 里注册一个存储介质 / 查找钩子；语言值从 `multilang.tjs` 的切换回调拿。
      先在 Wine 下跑 `lllj_zh.exe` 观察它重定向了哪些文件（`-forcelog=yes` 的日志里能看到读取的文件名）。
- [ ] **视频字幕**：在批次 3 的 FFmpeg 后端（`FFmpegMoviePlayer.cpp` 的 `Deliver`，I420 平面）里接 libass，按语言叠加 `.ass`（原 DLL 就是这么做的）；
      注意 `SelectMovieFile` 已有 `SelectLangMovieFile` 钩子。或退而求其次用离线烧录版本。
- [ ] 汉化组鸣谢画面、中文字体回退（无字形时）、竖排/注音等文字细节（TextRender 的 ruby、换行禁则：上次发现单独的「。」会被折到新行）。
- [ ] 存档：导入 `<游戏目录>/savedata_zh/`，读档验证。

验收：中文标题→新游戏→正文/选项/backlog 全中文；设置里切换日/简/繁即时生效；OP/ED 有对应语言字幕；汉化版存档可读。

---

## 批次 7：性能 60Hz

目标：所有典型场景稳定 60fps，最长帧 < 25ms；无明显卡顿（读图、切场景时也一样）。

- [ ] 用批次 1 的测量工具找瓶颈（Layer 软件合成？纹理上传？图片解码？脚本？）。`perf record -g` 直接跑 `game/krkrz` 也行。
- [ ] 绘制：`-drawthread=auto`、SIMD（确认 AVX2 路径在 Linux 构建里真的启用，CPU 支持 AVX2）、只更新脏矩形、
      看 krkrz 的 GL 合成器（`doc/GLCompositor.md`、`-drawdevice=ogl`）能否把最终合成搬到 GPU。
- [ ] 帧节拍：VSync、`-contfreq`、`waitVSync`，避免双重限速导致的抖动；Wayland/X11 都测。
- [ ] 图片加载：异步加载/预读（krkrz 有 ImageCache、prefetch），解决进场景时的卡顿；去掉批次 1 发现的 auto path 重建。
- [ ] E-mote 已走 GPU 光栅化（批次 2）；剩读回/转换（可改 PBO 异步或直接合成到 Layer 纹理）和首次贴图上传卡顿。视频 YUV 已在 GPU 转换；批次 3 实测桌面视频期间最长间隔 26–28ms，需压到 25ms 以下。
- [ ] 发布构建：Release + LTO；考虑 steamdev `deckbuild` 容器（glibc 2.31）构建以便在别的机器上跑。

验收：把各场景的 fps/最长帧表格写进本文件，全部达标。

---

## 批次 8：全流程验收与打包

- [ ] 用全 CG 存档（`<游戏目录>/补丁/` 里有）验证鉴赏模式全部内容；各角色线用 skip（已读跳过）各跑一遍到 ED。
- [ ] Wine 版与本移植逐项截图对比（标题、UI 各页、典型剧情画面、视频、SD）。
      E-mote 已在批次 2 用 `tools/emote_compare.py` 对照；同一注入方式可扩展到其他画面。
- [ ] 交付：`<游戏目录>/linux/` 下放可执行文件 + 启动脚本 + `.desktop` + 图标（或保留 `~/lllj-krkrz/game/` 方式）；README 写清安装/构建。
- [ ] 清理：删除 `game/` 以外的测试残留、关闭调试日志级别、`run.sh` 去掉开发用参数。

---

## 风险与备选

| 风险 | 备选 |
|---|---|
| krkr-ns 的 E-mote 在本作 .mtn 上有渲染偏差 | 对照 Wine 截图逐项修；krkrsdl3 上游 emoteplayer 有更新的修复可以 cherry-pick |
| FFmpeg 后端工作量大 | 先离线转 WebM 让视频能播，字幕烧录；之后再做 FFmpeg 后端 |
| 1080p 软件合成达不到 60fps | 把合成搬到 GL（krkrz GL 合成器），或降低内部分辨率作为最后手段 |
| 汉化 DLL 的重定向规则不明 | Wine 下跑汉化版，用引擎日志/strace 记录实际读取的文件，反推规则 |
| 上游 krkrz_dev 更新与本地 patch 冲突 | 本地改动保持最少（目前只有两个 patch），更新前先 `git stash`，按 `patches/` 重打 |
| 许可 | krkr-ns 移植代码 MIT；krkrsdl3 派生代码有“分发移植商业游戏须开源修改”条款；msg-tool/Kirakira 只作格式/行为参考。仅自用无问题，分发前需复核 |
