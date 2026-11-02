# ライムライト・レモネードジャム（柚子社，2024）

本作的游戏 profile。模块选择见 `CMakeLists.txt`，各模块负责什么见 [NOTES.md](NOTES.md)。

| 文件 | 内容 |
|---|---|
| `CMakeLists.txt` | 用到的模块 + 本作源码 |
| `game.cpp` | 挂载汉化包、存储回退（Hxv4 哈希名、汉化存档里的资源名） |
| `prelude/game.tjs` | 游戏脚本修正、汉化存档 ID、快捷键、语言面板 |
| `zh-patch.toml` | 汉化 DLL（`lllj_zh.dll`）内嵌配置的翻译：补丁包、重定向表、字幕、字幕字体 |
| `game.conf` | 启动配置：运行目录、占位 DLL、引擎参数、存档目录 |
| `setup.sh` | 由解包数据生成运行目录 `game/` |
| `import_saves.py` | 导入 Windows 存档 |
| `tests/` | 场景回归、对照工具、性能脚本 |

## 运行目录

```bash
games/lllj/setup.sh [RESULT_DIR]   # 默认 ../limelight/result
```

`RESULT_DIR` 是 14 个 xp3 的解包结果（含 `data/`、`patch/` 和 53 个去码 CG 的 `unencrypted/`）。
生成的 `game/` 里是指向它的相对符号链接、明文 `patch.xp3` 与 `patch2.xp3`（去码 CG，最高优先级），
以及汉化包 `lllj_zh.pck` 的链接（原地读取，不解包）。原始数据只读。

## 语言

默认以汉化版运行（等同 `lllj_zh.exe`）。`./run.sh -lllj_zh=no` 运行纯日文版，`-lllj_zh=/path/lllj_zh.pck` 指定包。

- 标题右上角「文/あ」按钮（汉化包自带）：日文 ↔ 简体，界面和正文一起切换。
- **Alt+L**：任何画面打开汉化包的语言面板（日本語 / 简体中文 / 繁體中文），代替 Windows 菜单栏的「文本语言」，切换正文语言。
  标题画面选繁体时界面保持简体（汉化包只有简体界面图）。
- OP/ED/MV 字幕跟随正文语言（日文无字幕）。

Alt+Enter 切换全屏。

## 存档

- 汉化版：`${XDG_DATA_HOME:-~/.local/share}/limelight_lj/savedata_zh/`（对应原 DLL 的 `savedata_zh`）
- 日文版：`limelight_lj/`
- `./run.sh -datapath=/absolute/path/` 覆盖。

导入 Windows 存档（不覆盖已有文件、不改动来源）：

```bash
python3 -I games/lllj/import_saves.py                       # 默认 ../limelight/savedata_zh/ → limelight_lj/savedata_zh/
python3 -I games/lllj/import_saves.py /path/to/old/saves
```

## 回归测试

需要 `Xvfb`、`xdotool`、Python 3.12+。每次用新的输出目录；脚本隔离存档、临时目录、REPL 和 Xvfb，只清理自己的进程。
完整 smoke 会从 `../limelight/补丁/全CG存档/全CG存档.zip` 复制解锁数据到测试目录。

```bash
games/lllj/tests/regress.sh artifacts/run-01            # 全部套件，结果在 artifacts/run-01/summary.txt
games/lllj/tests/regress.sh artifacts/run-02 zh movie   # 只跑部分
```

| 套件 | 覆盖 |
|---|---|
| `smoke` | 启动/Logo、标题、新游戏推进、backlog、普通/快速存读档、鉴赏入口、10 个设置分页、全屏/缩放、右键返回 |
| `emote` | 两个 Logo、31 个 SD、9 种标题背景、eyecatch（每个都要求画面在动） |
| `movie` | 鉴赏里的 OP + 7 个 ED：打开的文件、画面在变、位置前进、播完/右键/Esc 返回、音画落后 ≤ 60ms |
| `audio` | 带通幅频响应、剧本引用的 16 个特效 + 混响、语音特效路径与口型电平、14 个 `.mchx` |
| `extras` | 流程图连线、颜色轮盘、手柄设置页、条纹转场计时、PackinOne 散件、mosaic/turn 与 extNagano 转场 |
| `zh` | 汉化包挂载、中文标题动画、标题按钮切换、Alt+L 选繁体、WebP、正文/backlog 语言、OP 简/繁/日字幕 |
| `winsave` | 复制 Windows 汉化存档，恢复到预期场景并推进 |
| `smoke-jp` | 纯日文版（`-lllj_zh=no`）的 smoke |

单个套件也可直接跑：`python3 -I games/lllj/tests/scenarios.py OUT --only zh`（`--engine-arg=...` 追加引擎参数，
`--vomstyle layer` 强制视频模式）。任何 `exception occured`、REPL 失败、超时都算失败；例外只有 `scenarios.py` 里逐条评估过的
`KNOWN_CAUGHT`（汉化包脚本自己 try/catch 的两处）、`KNOWN_DIALOGS`（汉化包的防倒卖提示）和 `WIN32_STUBS`（Windows 专有、无对应功能的 stub）。

其他工具（`tests/`）：

| 工具 | 用途 |
|---|---|
| `benchmark.py` | 1080p 标题/对话/转场帧率；`--emote`、`--movie`、`--desktop`；`--legacy-index` 对照引擎旧的全量索引 |
| `emote_compare.py`、`textrender_compare.py` | 在 Xvfb 的 Wine 里跑原版，与本移植截图/排版对照（需要 wine、ImageMagick） |
| `verify_batch1.py`、`verify_resources.py` | 存储索引/哈希回退/分段存档、53 个去码资源与 16 个无名文件 |
| `audio_lock_stress.py` | 音频标签调度死锁的压力回归 |
| `iterate.sh N` | 增量编译 + 无头运行 N 秒 + 打印值得注意的日志 |
| `perf/` | 快进 CPU 测量（见 `perf/README.md`） |

## 测量

`./run.sh -fpsstats=yes` 每秒输出 `(compat) fps: {...}`：`frames/fps` 计完成的缓冲交换，`max_interval_ms` 是相邻交换的最大间隔，
`max_show_ms` 是呈现函数最长耗时，`compose_ms` 是 Layer 合成（含 `upload_ms`），`wait_ms` 是交换调用本身。
TJS 里 `CompatNative.resetFrameStats("scene")` / `frameStats()` / `storageStats()`。Xvfb 用 llvmpipe，它的帧数不能当成显示器刷新率。

绘制线程：`-drawthread=N` 线程池大小（默认物理核数），`-drawthreadchunk=N` 每份最少 `factor×N` 像素（默认 10000，0 = 上游行为）。
