# HANDOFF — ライムライト・レモネードジャム krkrz_dev 原生 Linux 移植

> 给接手的人（或下一个 Claude 会话）看的。先读这份，再读 `PLAN.md` 选一个批次开工。
> 构建、目录、工具的说明在 `README.md`。最后更新：2026-11-02。

## 1. 一句话现状

原型已跑通：启动 → ATTENTION → 标题菜单 → はじめから → 序章正文。背景、事件 CG（pimg 差分）、立绘、头像窗、对话框文字、
名字栏、rule 转场、BGM（opus）、语音（ogg）、快存/快读（含缩略图）都已在无头环境截图确认。
**缺**：E-mote（标题背景/Logo/SD 动画全黑）、视频、语音特效、若干插件、汉化、帧率未测。

## 2. 关键路径

| 东西 | 位置 |
|---|---|
| 工作根目录 | `/var/home/aoba/.homes/dev/lllj-krkrz`（`~/lllj-krkrz`，无空格路径，别挪到游戏目录） |
| 游戏原始目录 | `/run/host/var/home/aoba/Downloads/limelight_lj_wm (Copy 2)/` |
| 解包数据（只读，别改） | `<游戏目录>/result/`；`game/` 里全是指向它的 symlink |
| 引擎 | `krkrz_dev/`（master + 递归子模块），构建产物 `krkrz_dev/build/x64-linux/core/Release/krkrz` |
| 我们的代码 | `plugins/lllj_compat/`（全部改动集中在这里 + 两个小 patch） |
| 本地改动 | `patches/krkrz_dev-umbrella.patch`（`KRKRZ_LLLJ` 选项）、`patches/krkrz-core.patch`（大小写不敏感文件查找） |
| 脚本查阅 | `decomp/data|patch/**/*.dis`（字节码反汇编）、`decomp/text/`（解密后的 .ks/.tjs/.toml 文本） |
| 参考实现 | `ref/krkr-ns`（MIT，最重要：emoteplayer/视频/layerexdraw 都在 `krkrsdl2/src/plugins` 和 `core/visual/sdl2`）、`ref/msgtool_tjs_ns0.rs`（GPL，只当格式说明） |
| 上次 krkrsdl2 尝试的计划 | `~/.claude/plans/https-github-com-krkrsdl2-krkrsdl2-linu-functional-flamingo.md`（插件矩阵、汉化包分析，可参考） |
| 存档 | `~/.local/share/wamsoft/krkrz/`（里面全是测试数据，可删） |

## 3. 日常工作流

```bash
cd ~/lllj-krkrz && source env.sh
tools/iterate.sh 30                 # 增量编译 + 无头跑 30 秒 + 打印报错（日志在 last.log）
tools/smoke.sh  /tmp/x 6            # 启动→ATTENTION→标题→はじめから→推进6句，截图 01..04
tools/savetest.sh /tmp/y            # 在 smoke 基础上测快存/快读
./run.sh                            # 在真桌面跑（有声音）
```

- 无头环境：私有 Xvfb `:91`（2560x1600），`SDL_AUDIO_DRIVER=dummy`。窗口默认 1440x810，`tools/click.sh` 会先把窗口挪到 (0,0)。
- 游戏内坐标（1440x810 窗口）：ATTENTION 任意处点击；标题「はじめから」(212,771)；系统栏 Q.SAVE (626,790)、Q.LOAD (698,790)；
  确认框「はい」(590,473)。
- REPL：`tools/repl.sh .replch 'kag.conductor.curStorage'`。多语句用 `(function(){ ...; return x; })()`。
  截图：`tools/cap.sh /abs/path.png`（`System.captureScreen`；`Agent.*` 不可用，因为关了 Elements）。
- 只想测原生 API、不想走游戏启动：`./run.sh -nostartup` + REPL（例如 `Storages.addAutoPath(System.exePath+'evimage/')` 后测 `PSBFile`）。
- **杀进程只用 `pkill -x krkrz`**。`pkill -f` 会把自己的 shell 命令行也匹配上杀掉。
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
7. **存档格式**：`<缩略图 jpg/png 字节><TJS/ns0 数据包>`。读的时候要在文件里搜数据包头，不能假定从 0 开始。缩略图由 `lllj_makeDataPackThumb` 用 `saveLayerImage` 生成。
8. **插件目录**：64 位引擎下游戏用 `plugin64/`，不是 `plugin/`。
9. 启动参数 `-used2d=no -debugwin=no` 是必须的（否则会去链接 D2D、并模拟 krkr2 调试控制台 → 一堆 Win32 对话框）。
10. 原始 `Debug.message` 被游戏在发行模式下置空，调试输出用 `LLLJNative.log()`（prelude 里 `lllj_log()`）。

## 6. 当前已知问题（详见 PLAN.md 对应批次）

| 现象 | 归属批次 |
|---|---|
| 标题背景黑、`title_bg.mtn` 加载异常（被捕获）；Logo/SD/eyecatch 缺 | 批次 2（E-mote） |
| 视频不能播（movie-player 只支持 WebM + MPEG-1） | 批次 3 |
| `voiceeffect.tjs DSP` 异常（电话/对讲机音效缺） | 批次 4 |
| 运行中反复 `Rebuilding Auto Path Table`（约 0.3 秒/次，造成卡顿） | 批次 1 |
| 被捕获的异常：`uisupport.tjs SetupFullHDMovie`、`mainwindow.tjs onResize` | 批次 1 |
| 16 个无名文件（voice 8、voice2 4、uipsd 4）读不到 | 批次 1 |
| 去码 CG（`result/unencrypted`）未挂载 | 批次 1 |
| 存档目录名是 `wamsoft/krkrz` | 批次 1 |
| 帧率从没测过 | 批次 1 测量，批次 7 优化 |

## 7. 交接给新会话时可以直接贴的开场白

```
项目在 ~/lllj-krkrz：krkrz_dev 原生 Linux 移植 ライムライト・レモネードジャム。
先读 ~/lllj-krkrz/HANDOFF.md 和 PLAN.md，然后做 PLAN.md 的「批次 N」。
做完后：跑 tools/smoke.sh 回归、更新 PLAN.md 的勾选和 HANDOFF.md 的现状/已知问题、重新导出 patches/。
```
