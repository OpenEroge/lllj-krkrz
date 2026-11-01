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
| `game/` | 运行目录：`data` 的内容和 `bgimage/ voice/ …` 都是指向 `result/*` 的 symlink；`patch.xp3` 是明文重打包 |
| `run.sh` | 启动脚本 |
| `tools/` | 调试工具（见下） |
| `decomp/` | 游戏脚本反汇编 (`*.dis`) 和解密后的文本脚本 (`text/`)，只供查阅 |
| `ref/` | 参考实现（krkr-ns 等），只读 |
| `patches/` | 对 krkrz_dev / krkrz core 的本地改动 |

## 从 git 仓库重建

仓库只跟踪我们自己的代码。`krkrz_dev/`、`ref/`、`game/`、`decomp/` 都在 `.gitignore` 里：
- 引擎源码：按 `patches/UPSTREAM.txt` clone 到固定 commit 并打补丁；
- 运行目录：`tools/setup_game.sh`（默认使用同级的 `../limelight/result/`；也可传入其他 result 路径，创建相对 symlink 并生成明文 `patch.xp3`）；
- 脚本反汇编：`tools/tjsdis.py --tree` / `tools/fefe.py --tree`。

## 构建

```bash
source env.sh                          # PATH 去掉 linuxbrew，设 VCPKG_ROOT 等
cd krkrz_dev
cmake --preset x64-linux -DKRKRZ_LLLJ=ON -DLLLJ_EXTRA_PLUGINS=lllj_compat \
      -DKRKRZ_BUILD_TOOLS=OFF -DKRKRZ_USE_ELEMENTS=OFF -DKRKRZ_BUILD_TESTS=OFF
cmake --build build/x64-linux --config Release -j$(nproc)
```

移动项目后，旧的 CMake 构建目录包含绝对路径，需要重新配置和构建。

首次 configure 会通过 vcpkg 编译依赖（约 10 分钟）。系统包：`Mesa-libGLESv2-devel Mesa-libGLESv3-devel yasm systemd-devel libdecor-devel`。

## 运行

```bash
./run.sh                      # 在当前桌面运行（有声音）
HEADLESS=1 ./run.sh           # 私有 Xvfb :91 + 无声，自动化测试用
REPLCH=/some/dir ./run.sh     # 打开 REPL 文件通道（tools/repl.sh 用）
```

`run.sh` 会把可执行文件复制进 `game/`（游戏用 `System.exePath` 找 `patch.xp3` 和 `plugin64/` 占位 DLL），
并带上 `-used2d=no -debugwin=no`。存档在 `~/.local/share/wamsoft/krkrz/`。

## 兼容层做了什么

- **前置脚本** `src/prelude.tjs`（VM 初始化后、startup.tjs 前执行）：
  - 删除 krkrz 定义的 `kirikiriz_generic` 常量，让游戏走 PC 分支（否则它会当成主机版：忽略 `Plugins.link`、开 Switch UI）。
  - 接管 `Plugins.link`：DLL 名 → 静态链接的 krkrz_dev 插件（KAGParserEx、extrans、extNagano、psdfile、saveStruct、scriptsEx、layerEx* …）或 TJS 替身。
  - 替身：PackinOne（数据包 API）、windowEx（Window/System 成员）、menu（`MenuItem`）、win32dialog（对话框一律“取消”）、getLangName、toml、gamepad（`GamepadPort` 空实现）等。
- **原生代码**：
  - `datapack.cpp`：`Scripts.loadDataPack/saveDataPack/makeDataPackDigest`，`.pbd`（TJS/4s0：ChaCha + BLAKE2s + LZ4）和存档（缩略图 JPG + TJS/ns0 数据包）。
  - `toml.cpp`：`Scripts.tomlDecodeFromStorage`（toml++）。
  - `PsbFilePlugin.cpp` / `PSBData.cpp`：`PSBFile` + `psb://` 存储（移植自 krkr-ns；补了 krkrz 的 `iTVPStorageMedia2`，内存流类改名避开与 krkrz 核心同名类的 ODR 冲突）。
  - `TextRenderBase.cpp`：消息排版（移植自 krkr-ns；字宽改为回调脚本的 `onFontChange/onGetTextWidth`，这样预渲染字体也能量）。
  - `compat_main.cpp`：`LLLJNative`（`log`、`attachMembers`：把 TJS 类成员挂到原生类上）。
- **引擎补丁** `patches/krkrz-core.patch`：SDL3 本地文件层在精确路径不存在时按路径分量做大小写不敏感匹配（脚本里写 `system/Initialize.tjs`，盘上是 `initialize.tjs`）。

## 工具

| 工具 | 用途 |
|---|---|
| `tools/tjsdis.py` | TJS2100 字节码反汇编 |
| `tools/fefe.py` | krkr 简单加密文本（FE FE 0/1/2）解码 |
| `tools/pbd_decode.py` | `.pbd`（TJS/4s0）解码，C++ 版的参照实现 |
| `tools/psbdump.py` | PSB（pimg/mtn/scn）结构和资源查看 |
| `tools/xp3plain.py` | 目录打包为明文 XP3 |
| `tools/iterate.sh` | 编译 + 无头运行 N 秒 + 抓报错 |
| `tools/smoke.sh` / `tools/savetest.sh` | 冒烟测试（启动→标题→序章）/ 快存快读往返 |
| `tools/repl.sh` `tools/cap.sh` `tools/click.sh` | REPL 求值 / 截图 / 在 Xvfb 窗口里点击 |

## 现状

能用：启动 → ATTENTION → 标题菜单 → はじめから → 序章；背景、事件 CG（pimg 差分）、立绘、头像窗、对话框文字、名字栏、
rule 转场、BGM（opus）、语音（ogg）、快存/快读（含缩略图）。

未做（按计划后续处理）：
- **E-mote**（`motionplayer`）：标题背景、Logo、SD 动画是黑的/缺失，`title_bg.mtn` 加载报错（被捕获）。
- **视频**：krkrz 内置 movie-player 只支持 WebM，游戏是 WMV/MP4。
- 语音特效（wf* DSP 滤镜，电话/对讲机声）——报错被捕获，声音无特效。
- layerExDraw（GDI+ 绘图）、msdfrender、汉化补丁、glibc 2.31 发行构建。
- 运行中多次出现 `Rebuilding Auto Path Table`（每次约 0.3 秒），会造成卡顿，需排查是谁在反复 addAutoPath。
- 已知被捕获的异常：`SetupFullHDMovie`、`MainWindow.onResize`。
