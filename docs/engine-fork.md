# 引擎硬分叉

`engine/` 是 git 子模块，指向 [first-storm/krkrz](https://github.com/first-storm/krkrz)，
从 [wamsoft/krkrz](https://github.com/wamsoft/krkrz) `7d91ba5`（tag `upstream-7d91ba5`）分出。
krkrz_dev 伞形工程不再使用：本仓库的顶层 `CMakeLists.txt` 只保留了它实际需要的部分，
用到的插件拷在 `vendor/krkr-plugins/`。

分叉原则：引擎不认识任何游戏或模块。游戏需要的特殊行为通过扩展点（`common/base/EngineHooks.h`）
由上层注册；对所有游戏都正确的修复直接改在引擎里。

## 相对上游的改动

`git -C engine log --oneline upstream-7d91ba5..` 列出全部提交，每个提交说明原因。

| 提交 | 改动 | 原因 |
|---|---|---|
| Release the pool lock before firing memory pressure callbacks | `PooledAllocator.cpp` | 压力回调清存储缓存会再进 `free()`，同线程自锁死（大文件进缓存时必现） |
| Give sound label scheduling its own leaf lock | `SoundEventThread.*` | 解码线程与事件线程反序加锁，游玩数分钟后卡死；新增末端锁 `ScheduleMutex` |
| Cache the debugger scope key per function context | `tjsInterCodeGen/Exec` | 每次函数调用都算调试器 ScopeKey，占主线程约 15% |
| Split draw work by area and size the pool by physical cores | `ThreadIntf`、`LayerBitmapIntf`、`SysInitImpl` | 整屏混合被切成逻辑核数份，CPU 翻倍而合成不变快；新选项 `-drawthreadchunk=N` |
| Fall back to case-insensitive path lookup | `sdl3/base/FileImpl.cpp` | Windows 脚本的文件名大小写与磁盘不一致 |
| VideoOverlay period events / segment loops; pluggable movie decoders | `VideoOvlImpl`、`movie.cpp`、`MoviePlayer.h` | 补上 generic 版缺的 `setPeriodEvent/setSegmentLoop`；`TVPExternalMoviePlayerFactory` 允许外部解码器 |
| Add SDLOGLDrawDevice.recreate() | `SDLOGLDrawDevice` | 游戏 `onResize` 调用 `drawDevice.recreate()`；交换缓冲计入 render stats |
| Add engine extension hooks (EngineHooks.h) | 新文件 + `StorageIntf`、`LoadPNG`、`Application`、`SDLOGLDrawDevice` | 存储解析/图像嗅探/帧呈现/主循环回调，见 [architecture.md](architecture.md) |
| Index auto paths incrementally | `StorageIntf.cpp` | 运行时 `addAutoPath` 每次全量重扫（约 0.3 秒）；`-autopath_incremental=no` 恢复上游行为 |
| Make bitmap and graphic cache refcounts atomic | `LayerBitmapImpl.h`、`GraphicsLoaderIntf.cpp` | 异步预读在工作线程推入图像缓存后释放临时位图，主线程同时命中缓存共享同一位图，引用计数是普通 int |
| Take back queued image prefetches instead of waiting for them | `GraphicsLoadThread.*`、`GraphicsLoaderIntf.cpp` | 预读是单线程 FIFO，同步加载会等完排在前面的所有解码；现在未开始的预读被取回、改为同步解码。`TVPSetGraphicCacheLimit`/`TVPTouchImages` 补上缓存锁，后者改用与加载相同的缓存键 |

新增引擎选项：`-drawthreadchunk=N`（默认 10000，0 = 上游行为）、`-autopath_incremental=no`。

## 日常维护

```bash
cd engine
git switch master                     # 在 fork 的 master 上工作
# ……修改、提交（一个主题一个提交，提交说明写清原因）……
git push origin master
cd .. && git add engine && git commit  # 本仓库记录新的子模块提交
```

## 从上游取修复

不做整体合并；需要上游某个修复时单独 cherry-pick：

```bash
cd engine
git remote add upstream https://github.com/wamsoft/krkrz.git   # 只需一次
git fetch upstream
git cherry-pick <commit>
```

上游若改动了 `StorageIntf.cpp`/`SDLOGLDrawDevice.cpp` 等文件，冲突集中在上表列出的位置。
引擎的嵌套子模块（movie-player、glyphware、appserve；elements 未启用）仍指向 wamsoft 上游的固定提交。
