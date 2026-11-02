# 移植一款新游戏

以 `games/lllj/` 为样板。假设新游戏叫 `foo`。

## 1. 准备数据

把游戏的 xp3 解包成目录（加密游戏需要对应的解包工具），写 `games/foo/setup.sh` 生成运行目录：
指向解包数据的符号链接，加上游戏启动时自己会挂的 `patch*.xp3`（可用 `tools/formats/xp3plain.py` 打包）。
脚本反汇编用 `tools/formats/tjsdis.py`，加密文本用 `tools/formats/fefe.py`。

## 2. profile 骨架

```
games/foo/
├── CMakeLists.txt     compat_use(...) + 游戏自己的源码/prelude + compat_export()
├── game.conf          启动配置（run.sh 读取）
├── game.cpp           可选：游戏专属原生代码（存储回退、补丁配置…）
├── prelude/game.tjs   可选：游戏脚本修正
└── tests/             回归测试
```

`CMakeLists.txt`：

```cmake
include(${CMAKE_SOURCE_DIR}/modules/compat.cmake)
compat_use(compat-core win32 packinone)   # 先最少，按日志逐步加
compat_module(foo PRELUDE prelude/game.tjs ORDER 1000)
compat_export()
```

`game.conf`（变量和 `game_prepare` 函数，见 `games/lllj/game.conf` 的注释）：

```bash
RUN_DIR=run/foo
PLACEHOLDER_DIRS=(plugin plugin64)
PLACEHOLDER_DLLS=(PackinOne)
ENGINE_ARGS=(-loglevel=INFO)
game_prepare() { ENGINE_ARGS+=("-datapath=${XDG_DATA_HOME:-$HOME/.local/share}/foo/"); }
```

构建和运行：

```bash
./build.sh -DGAME=foo
GAME=foo ./run.sh
```

## 3. 迭代

启动后看日志（`./run.sh` 的终端输出）：

- `Plugins.link` 失败 → 这个 DLL 还没人替代。已有模块覆盖的就 `compat_use` 它；
  游戏用 `CanLoadPlugin()` 检查 DLL 的，把名字加进 `PLACEHOLDER_DLLS`。
- `(compat) stub: X` → 游戏调用了一个 Windows 专属成员，stub 返回“不存在”的值。逐个确认可接受，
  不可接受的就实现（通用的进模块，游戏专属的进 profile）。
- `exception occured` → 看调用栈；游戏脚本对 Windows 行为的假设，在 `prelude/game.tjs` 里修正。

常见的坑见 `games/lllj/NOTES.md`（`kirikiriz_generic`、TJS 语义陷阱、大小写、存档格式等）。

## 4. 放哪儿

问自己“另一款游戏会不会用到”：

- 会 → `modules/`（新模块或扩展已有模块），不能出现游戏名、游戏文件名、存档 ID；
- 不会 → `games/foo/`；
- 对所有游戏都是错的引擎行为 → 改 `engine/`（见 [engine-fork.md](engine-fork.md)）；
  只有某些游戏需要的 → 在引擎加扩展点，由模块注册。
