# ライムライト・レモネードジャム：开发笔记

给维护这个 profile 的人看。架构见 [docs/architecture.md](../../docs/architecture.md)，
分批开发记录见 [docs/history/batches.md](../../docs/history/batches.md)。

## 这款游戏需要什么

| 游戏用到的 | 由谁提供 |
|---|---|
| PackinOne、tjsDataPack 存档（Windows 存档为 TJS/4s0 加密 + JPEG APP11 分段） | modules/packinone |
| E-mote（Logo、标题 9 种背景、eyecatch、31 个 SD） | modules/emote |
| 流程图、颜色选择器（GDI+） | modules/layerexdraw |
| 正文逐字显示 | modules/textrender |
| 图集字体（设置页数值、流程图标题） | modules/msdfrender |
| 系统条纹转场 | modules/layerstwcopy |
| 语音特效（带通/延迟/混响）、多轨 BGM `.mchx` | modules/audio |
| OP/ED/MV（mp4/wmv）及汉化字幕 | modules/movie-ffmpeg + xpck-patch |
| 汉化包 `lllj_zh.pck`（重定向表、WebP 背景） | modules/xpck-patch（`zh-patch.toml`）+ webp |
| 16 个只剩 Hxv4 哈希名的文件 | `game.cpp`（`HxFilename(leaf, "xp3hnp")` 回退） |
| 汉化存档里的 `option_cn__bg0.tlg` | `game.cpp`（回退到 `option__bg0.tlg`） |
| 汉化存档 ID、Alt+Enter、Alt+L 语言面板、`speechPlugin`、流程图 `piledCopy` | `prelude/game.tjs` |
| 手柄 | `prelude/game.tjs` 默认 `-joypad=dinput`，用引擎自带的 SDL 手柄 |

## 踩过的坑（都已解决，别再踩）

1. **`kirikiriz_generic`**：krkrz 的 SysInitScript 定义了全局常量 `kirikiriz_generic=1`，游戏会当成主机版（忽略 `Plugins.link`、开 Switch UI、xp3 当文件夹）。
   compat-core 的 prelude 第一件事就是 `delete global.kirikiriz_generic`。
2. **TJS 语义**：
   - 挂在 Dictionary 上的函数，函数体里未加前缀的名字**不会**回退到 global（字典对缺键返回 void，而不是“成员不存在”）。prelude 里一律写全局函数。
   - `function(){} incontextof %[...]` 在这里不可靠，要生成闭包就用 `Scripts.eval("(function(){...})")`。
   - `%[ a => 1 ]` 的 `a` 会被当表达式求值，字面键要写 `%[ a: 1 ]` 或 `"a" => 1`。
   - `real`、`int`、`string` 是关键字，不能当变量名。
   - `typeof` 一个“只能在实例上取”的原生属性会抛异常，要 try。
3. **ODR 冲突**：krkrz 核心自己有 `tTVPSharedMemoryStream`（StorageCache.cpp），从 krkr-ns 拷来的同名类会和它混用虚表 → 垃圾数据/崩溃。
   已改名 `tCompatSharedMemoryStream`。**移植任何外部代码前，先 grep 核心有没有同名的全局类/函数**，最好包进 namespace。
4. **存储介质接口**：krkrz 用 `iTVPStorageMedia2`（多了 `FileSize/LastModifiedFileTime/Remove/Move`），实现自定义 `xxx://` 介质必须继承它。
5. **TextRender 字宽**：游戏的字体名是预渲染字体（`スキップ` 等，.tft），引擎光栅化器量不出来。原版做法是回调脚本的
   `onFontChange(%[face,bold,italic])` / `onGetTextWidth(ch, size)`，已照做。
6. **大小写**：脚本请求 `system/Initialize.tjs`，盘上是 `initialize.tjs`（文件名来自哈希表，全小写）。引擎分叉在 SDL3 文件层加了按分量的大小写回退。
7. **存档格式**：本移植写 `<缩略图><TJS/ns0>`；Windows 的 JPEG 把 TJS/4s0 分散在 APP11 段内，每段最多 16KiB 解码数据，`FF 00` 要还原为 `FF`。
   先用 `tools/formats/pbd_decode.py SAVE.jpg` 独立解码，再排查密钥。系统 `datasc/datasu` 也可能是 FEFE 压缩文本。
   汉化版与日文版存档 ID 不同，prelude 仅接受本作已确认的汉化 ID；保存里的 `option_cn__bg0.tlg` 缺失时回退到中性背景。
8. **插件目录**：游戏的 CanLoadPlugin 按位数检查 `plugin64/`，SDL 引擎实际搜索 `plugin/`，启动脚本两处都放占位文件。
9. 启动参数 `-used2d=no -debugwin=no` 是必须的（否则会去链接 D2D、并模拟 krkr2 调试控制台 → 一堆 Win32 对话框）。
10. 原始 `Debug.message` 被游戏在发行模式下置空，调试输出用 `CompatNative.log()`（prelude 里 `lllj_log()` / `compat_log()`）。
11. **启动自动化**：先走完 caution/Logo，再用 `kag.process` 跳场景。提前跳出 caution 会留下透明 `SysCoverLayer`，标题看得见却点不到。
    游戏中的 backlog/设置/存档页使用 `SystemAction` 打开，保留 extra conductor；Windows 读档用完整 `loadFunction` 流程恢复 UI 状态。
12. **资源索引**：重复 add/remove 不再失效；新路径增量加入；compact 和资源外存档写入保留索引。外部增删资源后用 `Storages.clearStorageCaches()` 显式刷新。
    `-storagetrace=yes` 在 Release 中独立启用 TJS 栈追踪，查询调用栈和扫描事件，`-autopath_incremental=no` 是性能对照开关。原始资源保持只读。
13. **测量**：`-fpsstats=yes` 计默认 SDLOGL 完成的 Swap（引擎 present 回调）；字段口径见 README。切换绘制设备前先确认其呈现钩子已覆盖。
    本次基线的 Xvfb 用 llvmpipe，真实桌面用 AMD Radeon 880M；两种数据分开记录，原始报告保留渲染器和采样时长。
14. **视频素材特性**：WMA Pro 在 seek 后输出不带时间戳的帧（不处理会把音频起点当成 0，视频永远等时钟）；WMV3 关键帧间隔 5 秒，
    seek 后要先压住音频直到首帧就绪。ED 开头常有几秒纯黑，判断"画面在动"要先 seek 到片中。左键不跳过视频是游戏设计（`tflags.sysmovie_skip`），右键/Esc 才跳。
    xdotool 在 Wayland 桌面找不到窗口，`--desktop` 测量里不要用 `session.key/click`。
15. **滤镜链**：`buf.filters` 只是普通数组，引擎只在 `open()` 时按各元素的 `interface` 属性重建链（`WaveIntf.cpp` `RebuildFilterChain`），
    之后增删要重新 open 才生效；`iTVPBasicWaveFilter::Update()` 从不被调用，参数只能在 `Decode` 里读。音源结束（上游返回不足）后，
    宿主类继续喂静音直到 `extend` 用完，并入队 `tTVPWaveSegment(lastPos, 0, n)`，所以尾音期间 `position` 不可信。
16. **REPL 里的日文**：`json.dumps` 默认把非 ASCII 转成 `\uXXXX`，TJS 字符串不认，文件名会变成别的东西；传给 REPL 时用 `ensure_ascii=False`。

17. **非 D3D 路径**：本移植（和 Wine 下的原版一样）`kag.isD3D == 0`，`mainwindow.tjs` 因此把 `kag.uibase` 设为无图像的 binder 层。
    原版脚本在流程图退出时往它上面 `piledCopy`，会抛被捕获的 `Not drawable layer type`（Wine 下原版日志同样有）；`prelude/game.tjs` 给 `PlayBackViewUtil.pile` 加了父层有图像的判断。
18. **截图对照**：`kag.captureSnapshot` 只含场景层；系统界面和转场中途要用 `layer.piledCopy(0,0,kag.sysbase,...)`（游戏截图功能的做法）。原版引擎没有 `System.captureScreen`。
19. **音频死锁（上游）**：游玩数分钟后只有游戏卡死，栈为主线程 `GetVisBuffer`（口型）、`SoundDecodeThread` 在 `PushPlaySample → ReschedulePendingLabelEvent`、
    `SoundEventThread` 在 `Update()`，都卡在 `__lll_lock_wait`。原因是标签调度字段借用了全局缓冲区列表锁，解码线程持缓冲区锁去拿它，事件线程反向拿。
    已改为调度字段专用的末端锁 `ScheduleMutex`（锁顺序：列表锁 → 缓冲区锁 → `ScheduleMutex`，见 `SoundEventThread.h`）。
    回归：`python3 -I games/lllj/tests/audio_lock_stress.py OUT 60`（修复前约 15 秒内会卡；需要更快复现时，可临时在事件线程持列表锁处加 sleep 放大时序）。
    再卡住时不要关游戏，运行 `tools/dev/grab_hang.sh OUT` 抓全部线程栈。
20. **快进 CPU（上游）**：120Hz 桌面快进时游戏占约 1.8 核。两处原因：
    - 绘制线程池按逻辑核数（20）建，超过阈值的混合一律切成 20 条（`KRKRZ_THREAD_PIXEL_SCALE` 实际默认 100，不是注释写的 500）。
      整屏混合被切碎后每条吞吐只剩约 1/3，合成几乎不变快，CPU 却翻了几倍。
    - TJS 每次函数调用都无条件计算调试器 ScopeKey（三次字符串构造 + map 查找），约占主线程的 15%。
    已改为：
    - 池大小取允许运行的物理核数（Linux sysfs，读不到时退回逻辑核数）。
    - 每次分发按面积分配份数，每份至少 `factor × -drawthreadchunk`（默认 10000，0 = 旧行为）。
    - ScopeKey 按 start_ip 和名字指针缓存；挂调试器时照旧每次计算。
    桌面快进 CPU 从约 171–183% 降到 91–93%，快进速度 23.4→26.4 句/s；不同分片设置下渲染输出逐字节一致。
    测量脚本见 `tests/perf/README.md`（perf 在本环境不可用，改用 schedstat + gdb 抽样）。
21. **内存池自锁**：`TVPPooledAllocator::allocate` 原来持锁调用内存压力回调，回调清存储缓存又进 `free()` → 同线程死锁（汉化字体进缓存时必现，表现为启动卡在字体许可证日志之后）。已改为释放锁后回调。
22. **TJS 小坑**：`isvalid void` 为真（判断对象已释放要先看 `typeof`）；`Layer.getMainPixel` 不带 alpha。
23. **性能批次（2026-11）**：
    - 构建：引擎的内存统计（全局 new/delete 计数、内存浮层、周期 dump）默认不编入（`-DLLLJ_DEV_STATS=ON` 恢复）；Release 开 LTO；`tools/pgo.sh` 做 PGO。
      全程 `-fno-strict-aliasing`：Kirikiri 代码到处是 `(iTJSNativeInstance**)&layer` 这种强转，PGO+LTO 下 `Layer` 构造会忽略父层（子层全被当成主层，启动即失败）。
      LTO 还报出 json/csvParser/saveStruct 插件同名类的 ODR 冲突，已改为内部链接。
    - PNG：vcpkg 的 zlib 换成 zlib-ng（覆盖端口），背景解码 36→27ms；剩下主要是 libpng 逐行反滤波（约 14ms），再快只能换解码器。
      背景预读：`prelude/game.tjs` 在 `KAGEnvPlayer.play` 前看 30 行，把 stage/stage2/event 的 PNG（含 `_cn` 本地化版）交给引擎加载线程；
      快进 30 秒内 35 次背景加载有 34 次已解码好（不预读时 16 次，全靠缓存复用）。
    - E-mote：游戏链接的是 `motionplayer_nod3d.dll`，但 krkr-ns 的 `Player.useD3D` 让脚本走 D3D 路径（全屏共享 D3DAdaptor → `motionWorkLayer` → 每帧 `assignImages`），已去掉。
      现在每个 motion 画到自己的层：GL 目标存 BGRA（着色器换 R/B），只读回/比较变化区域，GLES3 下经 PBO+fence 异步读回，下一轮主循环送进图层（晚一帧；`-emote_readback=sync` 关闭）。
    - Xvfb 实测（改动前 → 仅 LTO → PGO+LTO）：设置界面每次开关主线程 CPU 790 → 733 → 724ms；背景 PNG 35.5 → 27 → 28ms；快进 20–22 句/s，差异在噪声内。
      E-mote 每次 draw 的读回 2.8–8.0 → 0.5–2.1ms（llvmpipe，绝对值不代表真 GPU）。桌面帧率需要在桌面上重测。
    - 测试：截图（PNG 压缩）变快后，movie 套件 seek 后立刻取样的"前进 1 秒"断言失效，已改为等播放恢复再取样；
      emote 套件 `sd011a_t01a`「看起来静止」贴着阈值（历来 5 张里 2–5 张不同），偶发失败，重跑即过。

## 已知问题

| 现象 | 归属批次 |
|---|---|
| eyecatch 在桌面平均约 70fps，最长帧约 38ms：读回已改为异步、只处理变化区域（见 23），首次贴图上传仍在主线程；桌面帧率待重测 | 待桌面实测 |
| 视频期间桌面最长呈现间隔 26–28ms（>25ms）；真实声卡下的音画同步尚待人工实听 | 未做（原批次 7 已取消） |
| 延迟的 dry/wet 语义（按 STK 习惯交叉淡化）和混响电平尚未与 Wine 原版实听对比；多轨按各轨直接相加，部分素材（`rgb_live`）相加后超过 0dBFS | 批次 4 人工复核 |
| 鉴赏音乐页频谱提示「fftgraph.dllがありません」：原版游戏目录里也没有 fftgraph.dll，行为与原版一致，未实现 | 不做 |
| 手柄只在无头环境验证了设置页和 `-joypad=dinput` 分支；实体手柄的按键/摇杆需在桌面 `./run.sh` 人工确认 | 批次 5 人工复核 |
| 汉化版画面未能与 Wine 下的 `lllj_zh.exe` 逐张对照（日文版对照已一致）：它在 Xvfb 的 Wine 里因 `Direct3DCreate9() failed` 停在对话框 | 未做 |
| 汉化版启动有两处由包脚本自己 try/catch 的异常日志（`KNOWN_CAUGHT`），原版日志里没有；推断是引擎记日志的差异 | 已评估 |
| 日简繁之外，汉化包的窗口菜单（双语显示、帮助等）没有入口；朗读（SAPI）不支持 | 不做 |
| 转场仍有布局/软件合成耗时；完整 60Hz 需后续场景实测 | 未做（原批次 7 已取消） |
| 上游 `tTVPSoundBuffers::ReschedulePendingLabelEvent` 无锁读 `EventThread`，而 `RemoveBuffer` 可能同时 delete 它（另一个竞态，未修） | 待定 |

批次 1 的 `SetupFullHDMovie`、`onResize` 已清除；前者因空 ProxyStorageMap 宣称了未实现的 `proxy://`，后者因 SDL 设备缺少 `recreate()`。
53 个去码文件的挂载来源与 MD5 均已核验。16 个无名文件保留哈希名：文件字节可读，回退算法通过独立向量及合成逻辑名请求验证；真实原名仍未反查。

