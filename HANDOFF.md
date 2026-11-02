# HANDOFF — ライムライト・レモネードジャム krkrz_dev 原生 Linux 移植

> 给接手的人（或下一个 Claude 会话）看的。先读这份，再读 `PLAN.md` 选一个批次开工。
> 构建、目录、工具的说明在 `README.md`。最后更新：2026-11-02。

## 1. 一句话现状

批次 1 已实现：去码包、增量资源索引、Hxv4 回退、隔离场景回归、实际 Swap 计时、游戏专用存档目录及 Windows 读档兼容。
完整 smoke 和 Windows 继续存档恢复/推进均有截图和断言。基线、验收命令与证据入口见 `PLAN.md` 批次 1。
批次 2 已实现：E-mote（Logo、标题 9 种背景、eyecatch、全部 SD）由 `plugins/lllj_compat/emote/`（krkr-ns emoteplayer + 本地修正 + 粒子）播放，
硬件 GL 下网格在 GPU 光栅化。验收数据见 `PLAN.md` 批次 2。
批次 3 已实现：OP/ED 视频由 `plugins/lllj_compat/src/FFmpegMoviePlayer.cpp`（预构建 FFmpeg，`tools/fetch_ffmpeg.sh`）播放，验收数据见 `PLAN.md` 批次 3。
批次 4 已实现：语音特效（带通/延迟/混响，`src/wave_filters.cpp`）与多轨 BGM `.mchx`（`src/multitrack.cpp`），验收数据见 `PLAN.md` 批次 4。
**后续**：其余插件、汉化和完整 60Hz 优化仍按各自批次处理。

## 2. 关键路径

| 东西 | 位置 |
|---|---|
| 工作根目录 | 项目仓库目录（与 `limelight/` 同级） |
| 游戏原始目录 | 项目同级的 `../limelight/` |
| 解包数据（只读，别改） | `<游戏目录>/result/`；`game/` 里全是指向它的 symlink |
| 引擎 | `krkrz_dev/`（master + 递归子模块），构建产物 `krkrz_dev/build/x64-linux/core/Release/krkrz` |
| 我们的代码 | `plugins/lllj_compat/`、`tools/` 和两份引擎补丁 |
| 本地改动 | `patches/krkrz_dev-umbrella.patch`（`KRKRZ_LLLJ` 选项/宏）、`patches/krkrz-core.patch`（文件查找、索引、SDLOGL 计时/重绘） |
| 脚本查阅 | `decomp/data|patch/**/*.dis`（字节码反汇编）、`decomp/text/`（解密后的 .ks/.tjs/.toml 文本） |
| 参考实现 | `ref/krkr-ns`（MIT，最重要：emoteplayer/视频/layerexdraw 都在 `krkrsdl2/src/plugins` 和 `core/visual/sdl2`）、`ref/msgtool_tjs_ns0.rs`（GPL，只当格式说明） |
| 上次 krkrsdl2 尝试的计划 | `~/.claude/plans/https-github-com-krkrsdl2-krkrsdl2-linu-functional-flamingo.md`（插件矩阵、汉化包分析，可参考） |
| 存档 | `${XDG_DATA_HOME:-$HOME/.local/share}/limelight_lj/`；测试各自用输出目录内的 `saves/` |
| 预构建 FFmpeg | `third_party/ffmpeg/`（不进 git；`tools/fetch_ffmpeg.sh` 下载，`tools/build.sh` 自动调用） |
| 验收产物 | `artifacts/`（不进 git）；每个运行目录有 `report.json`、`game.log`、截图和私有 REPL |

## 3. 日常工作流

```bash
cd /path/to/lllj-krkrz
tools/build.sh                     # 搬迁后也用此入口，默认开启 DRAW_STATS
tools/iterate.sh 30                 # 增量编译 + 无头跑 30 秒 + 打印报错（日志在 last.log）
SKIP_BUILD=1 tools/smoke.sh artifacts/run-01 6  # 新输出目录，完整场景回归
python3 -I tools/verify_windows_saves.py artifacts/win-01
./run.sh                            # 在真桌面跑（有声音）
```

- 无头环境自动分配 Xvfb display（2560x1600），使用 dummy 音频。指定 `HEADLESS_DISPLAY` 时连接调用方已有的 display。
- `REPLCH/display` 和 `REPLCH/pid` 标识输入目标；`REPLCH=... tools/click.sh X Y` 使用该窗口的客户区坐标。
- REPL：`tools/repl.sh .replch 'kag.conductor.curStorage'`。多语句用 `(function(){ ...; return x; })()`。
  截图：`tools/cap.sh /abs/path.png`（`System.captureScreen`；`Agent.*` 不可用，因为关了 Elements）。
- 只想测原生 API、不想走游戏启动：`./run.sh -nostartup` + REPL（例如 `Storages.addAutoPath(System.exePath+'evimage/')` 后测 `PSBFile`）。
- 回归工具管理自身进程、存档、TMPDIR 和实例锁；手工调试也按自己的 PID 结束进程，保留其他会话。
- 改 `prelude.tjs` 后必须重新编译（它在 configure 时被嵌进 `lllj_prelude.inc`；cmake 已设 CONFIGURE_DEPENDS，`cmake --build` 会自动重新 configure）。
  新增 `.cpp` 放进 `plugins/lllj_compat/src/` 即可（GLOB + CONFIGURE_DEPENDS）。

## 4. 兼容层架构（`plugins/lllj_compat/`）

- **不是** krkrz_plugin 目标：`CMakeLists.txt` 通过 `PARENT_SCOPE` 把源码塞进引擎可执行文件（`KRKRZ_SRC_ADD`），所以能用引擎内部头文件
  （`tjsNative.h`、`LayerIntf.h`、`StorageIntf.h` …）。
- **注册入口**：`compat_main.cpp` 里的 `tTVPAtInstallClass`（核心类装完后调用）。`LLLJNative` 对象在这里建好，然后执行嵌入的 `prelude.tjs`。
  `PSBFile`、`TextRenderBase` 也在这里注册。
- **加原生函数**：任意 `.cpp` 里写 `LLLJ_NATIVE(name) { ... }`（见 `lllj_compat.h`），在 TJS 里就是 `LLLJNative.name(...)`。
- **加原生类成员到 Window/Layer 等**：在 prelude 里写一个 TJS 类，再 `LLLJNative.attachMembers(那个类, Window)`（ncbind 的 NCB_ATTACH_CLASS 同理）。
- **DLL → 实现** 的映射全在 `prelude.tjs`：`LLLJ.realName`（→ 静态链接的 krkrz_dev 插件）和 `lllj_shim(...)`（→ TJS 替身）。
  游戏的 `CanLoadPlugin()` 要求 DLL 文件存在，所以 `run.sh` 在 `game/plugin{,64}/` 放了空占位文件；**新增要被游戏主动加载的 DLL 时，记得把名字加进 `run.sh` 的占位列表**。

- **E-mote**（`emote/`）：krkr-ns emoteplayer 原样 vendor，差异集中在 `emote/shim/` 和带 `LLLJ` 注释的少数改动，清单在 `emote/UPSTREAM.txt`。
  它注册为静态插件 `lllj_emote`（`lllj_emote_module.cpp`），绑定表仍用 ncbind：`shim/lllj_ncbind.h` 让 krkrz_dev 的 ncbind 用引擎内部头文件，
  并把 ncbind 类名私有化（否则会和用 tp_stub 编译的静态插件在链接时合并 vtable）。所有 emote 源文件强制包含 `shim/lllj_emote_prefix.h`（替代 krkr-ns 的 PCH）。
  后端：硬件 GL → 私有 SDL GL 上下文光栅化（创建上下文会重置交换间隔，已恢复）；软件 GL/自检失败 → CPU。`-lllj_emote=cpu|gl` 强制。
  `LLLJNative.emoteStats()` 给出 progress/draw/读回/转换的耗时；`tools/benchmark.py --emote` 测帧率。
  与原版对照：`tools/emote_compare.py` 在 Xvfb 里用 Wine 跑原版（`-nod3dm=yes -used2d=no`，首启的"在线检查补丁"询问自动答"否"），
  通过 CialloHook 的 `unencrypted/` 覆盖 `custom.tjs` 注入 `tools/emote_compare.tjs`；镜像目录全是软链，不改游戏目录。

- **视频**（`src/FFmpegMoviePlayer.cpp`）：经引擎补丁的 `TVPExternalMoviePlayerFactory` 接管 mp4/wmv/asf/mov/avi，其余仍走 krkrz 的 WebM/MPEG-1。
  FFmpeg 是 BtbN 的 LGPL shared 预构建，链接时用 `third_party/ffmpeg/lib`，运行时靠 `$ORIGIN` RPATH 找 `game/` 里 `run.sh` 复制的副本
  （系统里有同 soname 但缺 wmv3/wmapro 的 openSUSE 版本，别让它被加载）。`LLLJNative.movieStats()` 给出帧/丢帧/落后/欠载。
  prelude 让 `System.getOSVersion` 报 Windows 10，原版 `SelectMovieFile` 因此选 `op.mp4`。

- **音频**（`src/wave_filters.cpp`、`src/multitrack.cpp`）：游戏的 `voiceeffect.tjs` 把 `.ini` 里的每行表达式用 `Scripts.eval(..., VoiceEffectFactory)` 求值，
  得到 `WaveSoundBuffer.WaveDSPFilter/DelayEffect/StkFreeVerb` 实例后放进 `buf.filters`。这几个类在安装阶段原生挂到 `WaveSoundBuffer` 上，
  `wfbasiceffect/wftypicaldsp/wumultitrack.dll` 只需 link 成功（prelude 空壳）。三个类共用一个原生实例类型 `LLLJWaveFilter`，
  差异只在 `Effect` 子类（`prepare/process/reset/tailMs`）。剧本实际只用 `BandPass_Butterworth`、`Delay`、`Verb`；
  EQ/Vocoder/RingModulation/Osc*/GainLimit/OscLoop（`wvdecoder`）无引用，未实现（工厂调用时走脚本自带的 errorSound）。
  `.mchx` 由注册的 wave decoder 处理，打开时回调 `WaveSoundBuffer.multiTrackOpenCallbacks`（脚本据此拿到 `mtrack_id`），
  `setMultiTrackMask(id, name|void, vol, ms)` 改各轨增益。`setDelayMultiTrackMask` 未实现，脚本用 typeof 检测后退化为立即切换。

## 5. 踩过的坑（都已解决，别再踩）

1. **`kirikiriz_generic`**：krkrz 的 SysInitScript 定义了全局常量 `kirikiriz_generic=1`，游戏会当成主机版（忽略 `Plugins.link`、开 Switch UI、xp3 当文件夹）。
   prelude 第一件事就是 `delete global.kirikiriz_generic`。
2. **TJS 语义**：
   - 挂在 Dictionary 上的函数，函数体里未加前缀的名字**不会**回退到 global（字典对缺键返回 void，而不是“成员不存在”）。prelude 里一律写全局函数。
   - `function(){} incontextof %[...]` 在这里不可靠，要生成闭包就用 `Scripts.eval("(function(){...})")`。
   - `%[ a => 1 ]` 的 `a` 会被当表达式求值，字面键要写 `%[ a: 1 ]` 或 `"a" => 1`。
   - `real`、`int`、`string` 是关键字，不能当变量名。
   - `typeof` 一个“只能在实例上取”的原生属性会抛异常，要 try。
3. **ODR 冲突**：krkrz 核心自己有 `tTVPSharedMemoryStream`（StorageCache.cpp），从 krkr-ns 拷来的同名类会和它混用虚表 → 垃圾数据/崩溃。
   已改名 `tLLLJSharedMemoryStream`。**移植任何外部代码前，先 grep 核心有没有同名的全局类/函数**，最好包进 namespace。
4. **存储介质接口**：krkrz 用 `iTVPStorageMedia2`（多了 `FileSize/LastModifiedFileTime/Remove/Move`），实现自定义 `xxx://` 介质必须继承它。
5. **TextRender 字宽**：游戏的字体名是预渲染字体（`スキップ` 等，.tft），引擎光栅化器量不出来。原版做法是回调脚本的
   `onFontChange(%[face,bold,italic])` / `onGetTextWidth(ch, size)`，已照做。
6. **大小写**：脚本请求 `system/Initialize.tjs`，盘上是 `initialize.tjs`（文件名来自哈希表，全小写）。`patches/krkrz-core.patch` 在 SDL3 文件层加了按分量的大小写回退。
7. **存档格式**：本移植写 `<缩略图><TJS/ns0>`；Windows 的 JPEG 把 TJS/4s0 分散在 APP11 段内，每段最多 16KiB 解码数据，`FF 00` 要还原为 `FF`。
   先用 `tools/pbd_decode.py SAVE.jpg` 独立解码，再排查密钥。系统 `datasc/datasu` 也可能是 FEFE 压缩文本。
   汉化版与日文版存档 ID 不同，prelude 仅接受本作已确认的汉化 ID；保存里的 `option_cn__bg0.tlg` 缺失时回退到中性背景。
8. **插件目录**：游戏的 CanLoadPlugin 按位数检查 `plugin64/`，SDL 引擎实际搜索 `plugin/`，启动脚本两处都放占位文件。
9. 启动参数 `-used2d=no -debugwin=no` 是必须的（否则会去链接 D2D、并模拟 krkr2 调试控制台 → 一堆 Win32 对话框）。
10. 原始 `Debug.message` 被游戏在发行模式下置空，调试输出用 `LLLJNative.log()`（prelude 里 `lllj_log()`）。
11. **启动自动化**：先走完 caution/Logo，再用 `kag.process` 跳场景。提前跳出 caution 会留下透明 `SysCoverLayer`，标题看得见却点不到。
    游戏中的 backlog/设置/存档页使用 `SystemAction` 打开，保留 extra conductor；Windows 读档用完整 `loadFunction` 流程恢复 UI 状态。
12. **资源索引**：重复 add/remove 不再失效；新路径增量加入；compact 和资源外存档写入保留索引。外部增删资源后用 `Storages.clearStorageCaches()` 显式刷新。
    `-lllj_paths=yes` 在 Release 中独立启用 TJS 栈追踪，查询调用栈和扫描事件，`-lllj_index=no` 是性能对照开关。原始资源保持只读。
13. **测量**：`-lllj_fps=yes` 计默认 SDLOGL 完成的 Swap；采样命令和各字段口径见 README。切换绘制设备前先确认其呈现钩子已覆盖。
    本次基线的 Xvfb 用 llvmpipe，真实桌面用 AMD Radeon 880M；两种数据分开记录，原始报告保留渲染器和采样时长。
14. **视频素材特性**：WMA Pro 在 seek 后输出不带时间戳的帧（不处理会把音频起点当成 0，视频永远等时钟）；WMV3 关键帧间隔 5 秒，
    seek 后要先压住音频直到首帧就绪。ED 开头常有几秒纯黑，判断"画面在动"要先 seek 到片中。左键不跳过视频是游戏设计（`tflags.sysmovie_skip`），右键/Esc 才跳。
    xdotool 在 Wayland 桌面找不到窗口，`--desktop` 测量里不要用 `session.key/click`。
15. **滤镜链**：`buf.filters` 只是普通数组，引擎只在 `open()` 时按各元素的 `interface` 属性重建链（`WaveIntf.cpp` `RebuildFilterChain`），
    之后增删要重新 open 才生效；`iTVPBasicWaveFilter::Update()` 从不被调用，参数只能在 `Decode` 里读。音源结束（上游返回不足）后，
    宿主类继续喂静音直到 `extend` 用完，并入队 `tTVPWaveSegment(lastPos, 0, n)`，所以尾音期间 `position` 不可信。
16. **REPL 里的日文**：`json.dumps` 默认把非 ASCII 转成 `\uXXXX`，TJS 字符串不认，文件名会变成别的东西；传给 REPL 时用 `ensure_ascii=False`。

## 6. 当前已知问题（详见 PLAN.md 对应批次）

| 现象 | 归属批次 |
|---|---|
| eyecatch 在桌面平均约 70fps，但最长帧约 38ms（1080p 读回+转换、首次贴图上传）；无 GPU 时约 20fps | 批次 7 |
| 视频期间桌面最长呈现间隔 26–28ms（>25ms）；真实声卡下的音画同步尚待人工实听 | 批次 7 |
| 视频字幕（汉化版 `.ass`）未接；接入点是 `FFmpegMoviePlayer.cpp` 的 `Deliver` | 批次 6 |
| 延迟的 dry/wet 语义（按 STK 习惯交叉淡化）和混响电平尚未与 Wine 原版实听对比；多轨按各轨直接相加，部分素材（`rgb_live`）相加后超过 0dBFS | 批次 4 人工复核 |
| 鉴赏音乐页频谱（`fftgraph.dll` 的 `drawFFTGraph`）缺失，提示「fftgraph.dllがありません」；引擎有 `getSoundSpectrum` 可用 | 批次 5 |
| 颜色选择器依赖 layerExDraw；当前显示色样，禁用缺失的轮盘实现 | 批次 5 |
| layerStwCopy 附加系统转场未实现；当前使用原有规则转场 | 批次 5 |
| 汉化 UI、字幕及语言切换尚未挂载；读汉化存档不等于完成汉化 | 批次 6 |
| 转场仍有布局/软件合成耗时；完整 60Hz 需后续场景实测 | 批次 7 |

批次 1 的 `SetupFullHDMovie`、`onResize` 已清除；前者因空 ProxyStorageMap 宣称了未实现的 `proxy://`，后者因 SDL 设备缺少 `recreate()`。
53 个去码文件的挂载来源与 MD5 均已核验。16 个无名文件保留哈希名：文件字节可读，回退算法通过独立向量及合成逻辑名请求验证；真实原名仍未反查。

## 7. 交接给新会话时可以直接贴的开场白

```
项目在 lllj-krkrz 仓库中：krkrz_dev 原生 Linux 移植 ライムライト・レモネードジャム。
先读 HANDOFF.md 和 PLAN.md，然后做 PLAN.md 的「批次 N」。
做完后：跑 tools/smoke.sh 回归、更新 PLAN.md 的勾选和 HANDOFF.md 的现状/已知问题、重新导出 patches/。
```
