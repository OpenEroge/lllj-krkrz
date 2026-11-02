# lllj-krkrz：吉里吉里Z 原生 Linux 移植

用 [krkrz](https://github.com/wamsoft/krkrz)（吉里吉里Z，SDL3 + GLES）的硬分叉原生运行 Windows 吉里吉里游戏，不经 Wine。
目前移植的是柚子社《ライムライト・レモネードジャム》（含汉化版：日/简/繁游戏内切换、OP/ED 字幕）。

代码分三层，越往下越通用：

| 层 | 目录 | 内容 |
|---|---|---|
| 游戏 | `games/lllj/` | 只属于这款游戏的东西：选哪些模块、游戏脚本修正、汉化包配置、启动参数、测试 |
| 兼容模块 | `modules/` | 与具体游戏无关：替代 Windows DLL（E-mote、GDI+、TextRender、PackinOne、FFmpeg 视频、音效…） |
| 引擎 | `engine/`（子模块）、`vendor/krkr-plugins/` | krkrz 硬分叉 [first-storm/krkrz](https://github.com/first-storm/krkrz) 和它的标准插件；不认识任何游戏，只提供扩展点 |

```
                          ┌──────────────────────── games/lllj ─────────────────────────┐
  游戏脚本 (xp3/pck)  ──▶ │ CMakeLists.txt: 选模块   game.cpp   prelude/game.tjs          │
                          │ zh-patch.toml   game.conf (启动)   tests/ (回归)             │
                          └───────────────┬──────────────────────────────────────────────┘
                                          │ compat_use(...)   compat::RegisterInit / Prelude
  ┌──────────────────────────── modules/ ─▼──────────────────────────────────────────────┐
  │ compat-core: 注册表 · CompatNative · Plugins.link 映射 · stub · ncbind 适配          │
  │ win32  packinone  toml  emote  layerexdraw  textrender  msdfrender  layerstwcopy     │
  │ audio  movie-ffmpeg  webp  xpck-patch  diagnostics                                   │
  └───────────────┬───────────────────────────────────────────────┬──────────────────────┘
                  │ EngineHooks.h（存储/图像/帧回调）               │ TJS 类、静态插件
  ┌───────────────▼──────────── engine/ (krkrz 硬分叉) ───────────▼──────────────────────┐
  │ TJS2 VM · KAG · Layer · Storage · Sound · VideoOverlay · SDL3/GLES · vendor 插件     │
  └──────────────────────────────────────────────────────────────────────────────────────┘
```

详细说明见 [docs/architecture.md](docs/architecture.md)。

## 快速开始

依赖（Fedora/openSUSE 包名大同小异）：CMake ≥ 3.20、Ninja、GCC、[vcpkg](https://github.com/microsoft/vcpkg)（`~/vcpkg` 或 `VCPKG_ROOT`）、
GLES 开发包、`yasm`、`systemd-devel`、`libdecor-devel`，以及 pkg-config 能找到的 `libzstd`、`libwebp`、`libass`。
测试另需 `Xvfb`、`xdotool`、Python 3.12+。

```bash
git clone --recursive <本仓库>          # 或 clone 后 git submodule update --init --recursive
games/lllj/setup.sh [../limelight/result]  # 用解包后的游戏数据生成运行目录 game/
./build.sh                                # 首次 configure 会编译 vcpkg 依赖（约 10 分钟），并下载预构建 FFmpeg
./run.sh                                  # 在当前桌面运行
```

`./build.sh` 的参数原样传给 CMake configure（如 `-DGAME=lllj`），`BUILD_JOBS` 控制并行数。
`./run.sh` 的参数原样传给引擎；`HEADLESS=1` 用私有 Xvfb + 无声运行，`REPLCH=目录` 打开 REPL 文件通道（`tools/dev/repl.sh`）。
游戏相关的用法（语言切换、存档、回归测试）见 [games/lllj/README.md](games/lllj/README.md)。

## 目录

| 路径 | 内容 |
|---|---|
| `engine/` | krkrz 硬分叉（git 子模块）；相对上游的改动见 [docs/engine-fork.md](docs/engine-fork.md) |
| `vendor/krkr-plugins/` | 用到的 wamsoft 插件，原样拷入（来源见 `UPSTREAM.md`） |
| `modules/` | 兼容模块，每个目录一个，带 README；索引见 [docs/modules.md](docs/modules.md) |
| `games/lllj/` | 本作 profile、启动配置、测试、踩坑记录（`NOTES.md`） |
| `tools/formats/` | 格式工具：XP3 打包、XPCK 解包、PSB 查看、存档解码、TJS 字节码反汇编、FEFE 文本 |
| `tools/dev/` | 开发辅助：REPL、截图、向测试窗口发输入、抓卡死线程栈 |
| `tools/fetch_ffmpeg.sh` | 下载固定版本的 LGPL FFmpeg 预构建包到 `third_party/ffmpeg/` |
| `CMakeLists.txt`、`cmake/` | 顶层构建（取代 krkrz_dev 伞形工程） |
| `docs/` | 架构、引擎分叉、模块、新游戏移植指南；`docs/history/` 是开发记录 |

不进 git：`build/`、`game/`（运行目录）、`third_party/`、`artifacts/`（测试输出）、`decomp/`（反汇编脚本）、`ref/`（参考实现）。

## 移植别的游戏

见 [docs/adding-a-game.md](docs/adding-a-game.md)：新建 `games/<名字>/`，列出需要的模块，写游戏专属的 prelude/原生代码和 `game.conf`。

## 许可

仓库自身的代码尚未单独声明许可。vendor 代码各自保留许可：krkr-ns（MIT）、krkrsdl3 派生的 textrender
（krkrsdl3 许可：分发商业游戏移植时须公开修改）、PlutoVG（MIT）、DSPFilters（MIT）、toml++（MIT）、FFmpeg（LGPL，动态链接）。
游戏数据不在仓库中。
