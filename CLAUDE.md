# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

吉里吉里Z（krkrz）硬分叉 + 兼容模块，原生 Linux 运行 Windows 吉里吉里游戏（当前：柚子社《ライムライト・レモネードジャム》，含汉化包）。
仓库文档是中文；提交说明是英文。详细资料：`README.md`、`docs/architecture.md`、`docs/engine-fork.md`、`docs/modules.md`、
`docs/adding-a-game.md`、`games/lllj/README.md`、`games/lllj/NOTES.md`（踩坑记录，动手前必读相关条目）。

## 命令

```bash
./build.sh                      # configure（必要时 --fresh）+ 增量 Release 构建；参数原样传给 cmake configure
BUILD_JOBS=16 ./build.sh -DLLLJ_LTO=OFF   # 迭代时可关 LTO 缩短链接
tools/pgo.sh                    # 发布构建（约 14 分钟，先告知用户）
./run.sh [引擎参数]              # 桌面运行；HEADLESS=1 私有 Xvfb + 无声；REPLCH=dir 开 REPL 文件通道
games/lllj/tests/iterate.sh 20  # 增量构建 + 无头跑 20 秒 + 打印值得注意的日志（完整日志 last.log）

games/lllj/tests/run.py --changed          # 日常测试：只跑改动涉及的用例（相对 HEAD + 未跟踪文件）
games/lllj/tests/run.py emote_sd_a zh      # 指定用例/套件；--list 列出；-o OUT；--engine-arg=-lllj_zh=no
python3 -I games/lllj/tests/cases.py OUT 用例名 [引擎参数]   # 单个用例前台跑
games/lllj/tests/regress.sh artifacts/run-01                 # 全量（= run.py all）
```

交互调试（无头实例 + REPL）：

```bash
REPLCH=$PWD/.replch HEADLESS=1 ./run.sh -lllj_logo=no &
tools/dev/repl.sh .replch 'kag.allskip = 1; SystemAction.skip()'   # 求值一个 TJS 表达式，输出 JSON
REPLCH=$PWD/.replch tools/dev/cap.sh shot.png                      # 截这个实例
REPLCH=$PWD/.replch tools/dev/click.sh ...                         # 只向这个实例发鼠标/按键
tools/dev/grab_hang.sh OUT                                         # 卡死时（不要先关游戏）抓全部线程栈
```

前提：`game/` 运行目录由 `games/lllj/setup.sh` 生成（原始数据只读）；首次构建会编 vcpkg 依赖（约 10 分钟）。
Python 脚本一律 `python3 -I`。所有测试输出放 `artifacts/`（不进 git），每次用新目录。

## 架构要点

三层，依赖只能向下：`games/<game>/` → `modules/` → `engine/`（子模块 first-storm/krkrz）+ `vendor/krkr-plugins/`。

- 全部编进**一个可执行文件**。顶层 `CMakeLists.txt` 把 `games/${GAME}` 当作引擎的“静态插件”加入；
  游戏的 `CMakeLists.txt` 用 `compat_use(...)` 选模块，`modules/compat.cmake` 的 `compat_module()` 收集源文件、
  把 `.tjs` prelude 生成为 C++ 注册代码，`compat_export()` 交回引擎。所以模块可直接用引擎内部头文件。
- 启动：安装 `CompatNative` 类时按 `(order, name)` 跑所有 C++ init（`compat::RegisterInit`），再跑所有 prelude
  （compat-core → 各模块 → 游戏，ORDER 1000）。游戏 `Plugins.link("x.dll")` 经 `compat_mapDll`（静态插件）/
  `compat_shim`（TJS 安装成员或 stub）解析；`run.sh` 按 `game.conf` 放空占位 DLL 应付 `CanLoadPlugin()`。
- 引擎不认识任何游戏/模块：特殊行为走 `engine/common/base/EngineHooks.h` 扩展点（存储重定向/leaf/回退/事件、
  图像嗅探、present、dispatch）和 `TVPExternalMoviePlayerFactory`。模块之间只通过 `include/compat/*.h` 联系。
- 放哪儿：另一款游戏也会用到 → `modules/`（不得出现游戏名、文件名、存档 ID）；只这款 → `games/lllj/`；
  对所有游戏都错的引擎行为 → `engine/`；只有部分游戏需要 → 引擎加扩展点、模块注册。
- 命名约定（`docs/architecture.md`）：`CompatNative.名字`、prelude 全局函数 `compat_*` / `lllj_*`、日志前缀 `(compat) ` / `(lllj) `、
  vendor 本地修改注释 `PORT` 并记入该目录 `UPSTREAM.txt`、游戏专属命令行选项带 `-lllj_` 前缀。
- 测试：每个用例（`games/lllj/tests/cases.py`）在自己的引擎/Xvfb/存档/输出目录中经 REPL 驱动（`scenarios.py` 的 `Session`），
  `run.py` 并行调度。每个 `Case` 声明它覆盖的源路径（`paths`），`--changed` 据此选用例；命中 `CORE_PATHS`（引擎、compat-core、prelude、
  测试框架、构建文件……）只加跑 `CORE` 五个用例。日志中任何 `exception occured`、REPL 失败、超时都算失败，例外只有
  `scenarios.py` 中逐条评估过的 `KNOWN_CAUGHT` / `KNOWN_DIALOGS` / `WIN32_STUBS`。

易错点（细节见 `games/lllj/NOTES.md`）：TJS 字典函数里的裸名不回退 global；`%[ a => 1 ]` 的 `a` 会被求值；
移植外部 C++ 代码前先 grep 引擎有无同名全局类（ODR/虚表混用），新代码放命名空间；全程 `-fno-strict-aliasing` 不能去；
调试输出用 `CompatNative.log()`（`Debug.message` 被游戏置空）；传日文给 REPL 时 `json.dumps(..., ensure_ascii=False)`。

## Agent 工作流

### 1. 定位
- 先判断任务归哪一层（见上“放哪儿”），读对应 `README.md`（每个模块都有）和 `NOTES.md` 相关条目，再动代码。
- 需要扫多个模块/引擎大范围找代码时，交给 Explore 子代理并只取结论；目标明确的查找自己 grep。
- `ref/`（参考实现）、`decomp/`（反汇编的游戏脚本，`tools/formats/tjsdis.py`/`fefe.py`）只读，用来查原版行为。

### 2. 复现 / 诊断
- 先拿到失败证据：相关用例 `run.py <用例>`，或 `iterate.sh N` 看日志，或无头实例 + REPL 现场求值。
- 按日志分类：`Plugins.link` 失败 → 缺模块或占位 DLL；`(compat) stub: X` → 评估是否需要实现；`exception occured` → 看调用栈，
  Windows 行为假设在 `prelude/game.tjs` 修正；卡死 → `grab_hang.sh`（看锁顺序，如 `SoundEventThread.h`）。
- 对照原版：`emote_compare.py` / `textrender_compare.py` 在 Wine 里跑原版截图对比。
- 性能：`perf` 在本环境不可用，用 gdb 抽栈（`tests/perf/sample.py`）+ `/proc/PID/task/*/stat`/schedstat；
  Xvfb 下只计 `krkrz` 和 `DrawThread` 线程（llvmpipe 线程代替 GPU）；桌面数据需窗口在前台（`-stopdeactive=no`），否则请用户在桌面测。

### 3. 实现
- 一个主题一次改动；引擎改动在子模块里单独提交（`cd engine && git switch master`，一个主题一个提交、说明写原因），再在本仓库提交新的子模块指针。
- 选定方案后，把只服务于被否决方案的代码和实验开关一并删掉，不留“以防万一”的分支；总结里说明删了什么。
- 新模块按 `docs/modules.md`「写一个新模块」：`CMakeLists.txt` + `README.md` + 需要时 `UPSTREAM.txt`，并在游戏 `compat_use` 中启用。
- 新增/改变覆盖范围的测试：在 `cases.py` 给用例写准 `paths`，保持“快”的约束（条件等待而非 sleep、一次 REPL 请求合并、`-maxfps`）。

### 4. 验证
- 构建：`./build.sh`（输出很长时重定向到文件，只看尾部）。
- 测试**只跑改动涉及的**：`run.py --changed` 或点名用例。日常改动**不跑全量** `regress.sh`；全量只在用户要求或一批改动提交前跑。
- 测量：每个配置一轮即可，已有数据能回答问题就不再加测；任何预计超过约 5 分钟的任务（PGO、长 A/B、全量）先告诉用户时长并给出短方案。
- 已知偶发：emote 套件 `sd011a_t01a`「看起来静止」贴着阈值，偶发失败重跑即过；其他失败不要当成偶发。
- 如实报告：失败就贴 `OUT/summary.txt` 和失败用例输出，跳过的步骤要说明。

### 5. 收尾
- 文档同步：行为/选项变化更新对应模块 `README.md`、`docs/modules.md` 或 `docs/engine-fork.md` 的改动表；新踩的坑或测量结论写进 `games/lllj/NOTES.md`；
  已知问题表随之增删。
- 提交只在用户要求时做。提交说明英文：祈使句标题（如 `Sample movie progress after playback resumes from the seek`），
  正文讲原因、证据（数字、反汇编、日志）和顺带的修复，参考 `git log`。

### 子代理分工（可并行时）
| 任务 | 做法 |
|---|---|
| 大范围找代码（引擎 + 多个模块） | Explore 子代理，只回传位置和结论 |
| 互不相关的两个问题 | 各开一个子代理；需要独立构建时用 `isolation: "worktree"`（各自 `build/`，首次构建慢，先评估） |
| 独立审查改动 | 只给代码和需求，不给自己的结论 |
| 同一构建目录 | 只允许一个代理在跑 `./build.sh`；`run.py` 实例可并行（各自 Xvfb），但会和构建争 CPU，测量时别同时编译 |
