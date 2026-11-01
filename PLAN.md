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

- [ ] **帧率测量**：加 `LLLJNative` 或启动参数（如 `-lllj_fps=yes`），每秒输出实际呈现帧数、最长帧耗时、Layer 合成/上传/等待各占多少。
      先看 krkrz 自带的 `KRKRZ_DRAW_STATS` 和 `-drawthread`，能复用就复用。记录当前基线（标题、对话、转场三个场景）。
- [ ] **自动化测试脚本**：把 `smoke.sh` 扩成场景脚本（TJS，经 REPL 或 `-replfile` 注入）：
      Logo → 标题 → 新游戏推进 N 句 → backlog → 存/读档 → 鉴赏（CG/SD/音乐/立绘）→ 设置页每一页 → 回标题。每步截图 + 帧率记录。
      用 `kag.process(storage, label)` 直接跳到指定场景，比点击稳定。
- [ ] **`Rebuilding Auto Path Table` 反复触发**（每次约 0.3 秒）：查是谁在运行时调用 `Storages.addAutoPath/removeAutoPath`
      （在 prelude 里包一层打印调用栈 `Scripts.getTraceString()`），对症处理（缓存/改成只建一次/在引擎侧做增量重建）。
- [ ] **被捕获异常**清零：`uisupport.tjs SetupFullHDMovie`、`mainwindow.tjs onResize`、标题的 `affinesourceimage loadImages`（后者属批次 2，先确认原因）。
- [ ] **去码 CG**：`result/unencrypted/`（53 个 pimg）以最高优先级挂上。`initialize.tjs` 会依次挂 `patch2.xp3, patch3.xp3 …`（遇到第一个不存在的就停），
      所以用 `tools/xp3plain.py` 打成 `game/patch2.xp3` 最省事；先验证“后挂的优先”。
- [ ] **16 个无名文件**：在存储查找失败时按 Hxv4 规则把文件名算成 64 位十六进制名再查（算法在 `<游戏目录>/xp3tool/` 的 Rust 代码里），
      或者干脆离线反查出名字后在 `game/` 里补 symlink。
- [ ] **存档目录**改成 `~/.local/share/limelight_lj/`（看 krkrz 的 `-datapath` / appconfig 机制），首次启动可选导入 `<游戏目录>/savedata_zh/`。
      Windows 存档是 TJS/4s0 加密（iv = `System.title`），`loadDataPack` 已能解，需实测读档。
- [ ] 系统菜单/快捷键：全屏切换（Alt+Enter）、窗口缩放、右键菜单的替代品是否可用。

验收：基线帧率数据写进本文件；smoke 日志里没有 `exception occured`（除批次 2/3/4 负责的）；去码 CG 在鉴赏模式可见。

---

## 批次 2：E-mote（motionplayer）

目标：标题背景动画、yuzulogo/m2logo、eyecatch、SD 小剧场（`sdmotion/` 31 个 + patch 2 个，共 43 个 .mtn，PSB v4）全部正常且满帧。

- [ ] 移植 `ref/krkr-ns/krkrsdl2/src/plugins/emoteplayer/`（≈11k 行，MIT，基于 krkrsdl3）。它提供
      `Motion.ResourceManager / Player / SeparateLayerAdaptor / D3DAdaptor`、`EmotePlayer`。
      - 渲染后端：有 `EmoteSWRenderBackend`（CPU，含 NEON/simde）和 `EmoteGLRenderBackend`。krkrz 的默认绘制设备是 `SDLOGLDrawDevice`（GLES 3.2），
        **优先接 GL 后端**（性能），SW 后端作为回退和对照。
      - 先删 `__SWITCH__`、`KrkrNSLog/Prof` 相关；`Platform.h` 已有替身；注意 ODR（见 HANDOFF 坑 3）。
- [ ] 让游戏走 `motionplayer_nod3d.dll` 路径（`-nod3dm=yes` 或在 prelude 里决定），避免 `D3DEmotePlayer`；
      之后视需要再支持 d3d 路径（`system/d3d.tjs`）。在 `run.sh` 加 motionplayer 占位 DLL。
- [ ] 游戏侧用到的方法（来自脚本分析）：`play`、`playTimeline`、`setTimelineBlendRatio`、`getMainTimelineLabelList`、`setVariable`、
      `initPhysics`、`setDrawAffineTranslateMatrix`、`serialize`、`progress`、`skipToSync`。逐个核对。
- [ ] 资源：`.mtn` 是 PSB，可能有 `lzfs://` / MDF 压缩 / `setEmotePSBDecryptSeed` 加密；`tools/psbdump.py` 可用来看结构。
- [ ] 视觉对照：在 Wine 下截同一帧（HANDOFF 里有 Wine/Lutris 环境说明，见旧记忆 `lllj-xp3-roundtrip`），逐个比遮罩、混合模式、时间轴混合、物理（头发/胸部摆动）、眨眼。

验收：标题 9 种背景（`title_bg0..7`、`title_bg_trial`，需全通存档解锁）、两个 Logo、eyecatch、全部 SD 场景播放正确；E-mote 场景 60fps。

---

## 批次 3：视频

目标：OP（`op.mp4` / `op.wmv`）、7 个 ED（`ed1_ena … edg_gra.wmv`，均 1080p）、鉴赏「ムービー」、以及剧情里的图层视频（`gfx_movie.tjs`）全部正常。

- [ ] krkrz 的 SDL `VideoOverlay` 已有完整框架（`generic/visual/VideoOvlImpl.cpp` + `generic/app/movie.cpp`），后端只支持 WebM（libvpx）和 MPEG-1。
      **推荐**：在 `TVPCreateMoviePlayer()` 里按扩展名再加一个 **FFmpeg 后端**（实现 `iTVPMoviePlayer`，输出 I420 走现有 GPU YUV 路径，
      音频走 `tTVPMovieAudioSinkAdapter`），支持 wmv3/vc1/wmav2/h264/aac。参考 `ref/krkr-ns/.../core/visual/sdl2/SwitchMovieOverlay.cpp`、`MovieAudioQueue.h`。
      FFmpeg：系统包、vcpkg（加到 `plugins/lllj_compat/vcpkg.json`，umbrella 会自动合并）或静态最小构建三选一。
      备选方案：离线转码成 WebM（VP9 + Opus），零代码但要 ~1.2GB 额外空间，且字幕得烧进画面（每种语言一份）。
- [ ] `movie.tjs` 依次尝试 `vomMFEVR → vomMixer → vomOverlay`；`-vomstyle=` 可强制。确认 mixer/overlay/layer 三种模式和
      `onStatusChanged`/`onPeriod`/播放结束事件都正常，等待播放结束的脚本不会卡死。`CUSTOM_USE_MP4` + MFEVR 判定决定选 mp4 还是 wmv。
- [ ] 图层视频：`gfx_movie.tjs` 用 PackinOne 的 `Layer.copyBottomBlueToTopAlpha / copyRightBlueToLeftAlpha`（蓝通道当 alpha），需在兼容层实现；
      还有字幕 CSV、MovieSE。AlphaMovie（`.amv`）本作没有文件，只需类存在（已静态链接）。
- [ ] 音画同步、跳过（点击/Esc）、循环、seek。

验收：8 个视频完整播放、音画同步、结束后正确返回；视频期间 60fps；剧情中的图层视频正常。

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
- [ ] **视频字幕**：在批次 3 的 FFmpeg 后端里接 libass，按语言叠加 `.ass`（原 DLL 就是这么做的）；或退而求其次用离线烧录版本。
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
- [ ] E-mote 走 GPU、视频 YUV 在 GPU 转换（批次 2/3 完成后再测一遍）。
- [ ] 发布构建：Release + LTO；考虑 steamdev `deckbuild` 容器（glibc 2.31）构建以便在别的机器上跑。

验收：把各场景的 fps/最长帧表格写进本文件，全部达标。

---

## 批次 8：全流程验收与打包

- [ ] 用全 CG 存档（`<游戏目录>/补丁/` 里有）验证鉴赏模式全部内容；各角色线用 skip（已读跳过）各跑一遍到 ED。
- [ ] Wine 版与本移植逐项截图对比（标题、UI 各页、典型剧情画面、视频、SD）。
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
