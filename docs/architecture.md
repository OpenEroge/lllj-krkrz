# 架构

## 三层

```
games/<game>/      游戏 profile ── 只有它知道游戏的脚本、存档 ID、补丁、启动方式
      │  compat_use(模块...)，compat::RegisterInit / RegisterPrelude（order 1000）
      ▼
modules/           兼容模块 ──── 与游戏无关，一个模块替代一个（或一组）Windows DLL/功能
      │  EngineHooks.h、TVPExternalMoviePlayerFactory、静态插件、tTVPAtInstallClass
      ▼
engine/            krkrz 硬分叉 ─ 不认识任何游戏或模块，只暴露扩展点
vendor/krkr-plugins/  wamsoft 标准插件（KAGParserEx、extrans、psdfile…），原样
```

依赖只能向下：游戏 → 模块 → 引擎。模块之间只通过各自 `include/compat/*.h` 的公开头文件联系
（例如 xpck-patch 用 movie-ffmpeg 的 `compat/subtitles.h` 提供字幕），引擎里没有任何 `#ifdef 某游戏`。

全部代码编进**一个可执行文件**：模块不是动态插件，而是由顶层 CMake 交给引擎目标一起编译，
因此可以直接用引擎内部头文件（`LayerIntf.h`、`StorageIntf.h`…）。

## 构建如何组合

```
CMakeLists.txt（顶层）
 ├─ 合并 vcpkg 清单（引擎 + movie-player + 插件）
 ├─ TVP_PLUGINS = vendor 插件 + ${GAME}       ← 游戏 profile 被当成“静态插件”加入
 └─ add_subdirectory(engine)
       └─ 对每个插件 add_subdirectory()
             └─ games/lllj/CMakeLists.txt
                  ├─ compat_use(compat-core win32 emote ...)   → modules/<m>/CMakeLists.txt → compat_module()
                  ├─ compat_module(lllj SOURCES game.cpp PRELUDE prelude/game.tjs ORDER 1000)
                  └─ compat_export()  → KRKRZ_SRC_ADD / KRKRZ_INC / KRKRZ_LIB_ADD 交回引擎
```

`modules/compat.cmake` 里的 `compat_module()` 负责：收集源文件、把每个 `.tjs` prelude 生成为一段注册代码
（UTF-16 原始字符串）、设置模块私有的 include/宏/编译选项、登记公开头文件目录和要链接的库。

## 启动流程

1. 引擎解析命令行，安装核心类（`System`、`Layer`、`Window`…）。
2. 安装 `CompatNative` 类时（`tTVPAtInstallClass`，`modules/compat-core/src/core.cpp`）：
   1. 按 `(order, name)` 运行所有 **init**：webp 注册图像嗅探器、diagnostics 注册帧/存储回调、
      游戏 profile 挂载汉化包并注册存储回退……（此时命令行已可读）；
   2. 创建 `global.CompatNative`，挂上所有 `COMPAT_NATIVE` 函数；
   3. 按 `(order, name)` 运行所有 **prelude**：`compat-core/core.tjs`（Plugins.link 映射框架）→ 各模块 → 游戏。
3. 引擎执行游戏的 `startup.tjs`。游戏 `Plugins.link("xxx.dll")` 时：
   - 映射到静态插件（`compat_mapDll`）→ 链接编进 exe 的插件；
   - 映射到 shim（`compat_shim`）→ 运行 TJS 函数（安装原生成员或 stub）；
   - 都没有 → 交给引擎原来的 `Plugins.link`。

游戏的 `CanLoadPlugin()` 通常先检查 DLL 文件是否存在，所以启动器（`run.sh` + `games/<game>/game.conf`）
会在运行目录放空的占位 DLL。

## 引擎扩展点（`engine/common/base/EngineHooks.h`）

| 扩展点 | 调用时机 | 使用者 |
|---|---|---|
| `TVPAddStorageRedirectHook` | 每次 `TVPGetPlacedPath`，在缓存之前 | xpck-patch：按当前语言重定向、包内文件 |
| `TVPAddStorageLeafHook` | 在 auto path 表之前按文件名查找 | xpck-patch：补丁包优先于所有 xp3 |
| `TVPAddStorageFallbackHook` | 正常查找失败后，给出备选文件名 | 游戏：Hxv4 哈希名、存档里的缺失资源名 |
| `TVPAddStorageEventHook` | 扫描/重建/写入/清缓存 | diagnostics |
| `TVPAddGraphicSniffer` | PNG 加载器读数据前 | webp |
| `TVPAddPresentHook` | SDLOGL 交换缓冲之后（有回调时才计时） | diagnostics（`-fpsstats`） |
| `TVPAddDispatchHook` | 每次主循环事件分发开头 | diagnostics |
| `TVPExternalMoviePlayerFactory`（`MoviePlayer.h`） | 打开视频时最先询问 | movie-ffmpeg |

其他引擎侧改动（死锁修复、增量 auto path 索引、大小写回退等）对所有游戏都成立，见 [engine-fork.md](engine-fork.md)。

## 命名约定

| 地方 | 约定 |
|---|---|
| TJS 原生函数 | `CompatNative.名字`（`COMPAT_NATIVE(名字)`） |
| prelude 全局函数 | 模块用 `compat_*`，游戏用 `<游戏>_*`（如 `lllj_*`） |
| 挂到原生类上的 TJS 辅助类 | 以 `Members` 结尾（`attachMembers` 跳过它的构造函数） |
| 日志前缀 | 模块 `(compat) `，游戏 `(<游戏>) ` |
| 静态插件名 | `compat_<模块>`（ncbind 符号以此为前缀，互不冲突） |
| 对 vendor 代码的本地修改 | 注释标 `PORT`，并在该目录 `UPSTREAM.txt` 列出 |
| 命令行选项 | 模块用功能名（`-emote`、`-fpsstats`、`-storagetrace`），游戏专属的带游戏前缀（`-lllj_zh`） |

## 测试

每个游戏在 `games/<game>/tests/` 下有自己的场景回归：脚本启动隔离的 Xvfb 实例，经 REPL 文件通道（`tools/dev/repl.py`）
驱动游戏、截图、断言，并检查日志里没有未经评估的异常和 stub。测试拆成互相独立的用例，由 `games/lllj/tests/run.py` 并行运行：
日常改动用 `run.py --changed` 只跑相关用例，`games/lllj/tests/regress.sh` 是全量，只在一批改动完成或被要求时跑。
