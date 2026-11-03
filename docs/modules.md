# 兼容模块

每个模块是 `modules/` 下的一个目录：`CMakeLists.txt`（调用 `compat_module()`）、`README.md`、
`src/`、可选的 `prelude/*.tjs` 和 `include/compat/*.h`（给别的模块用的公开头文件）。
游戏 profile 用 `compat_use(...)` 选择需要的模块。

## 索引

| 模块 | 替代 | TJS 接口 / 公开头文件 | 选项 |
|---|---|---|---|
| [compat-core](../modules/compat-core/README.md) | — | `CompatNative.log/attachMembers`，`compat_mapDll/compat_shim/compat_stub(s)`；`compat/compat.h`、`compat/layer.h`、`compat/ncbind.h` | — |
| [win32](../modules/win32/README.md) | windowEx、win32dialog、menu、getLangName 等 Windows 专属 DLL/API | `WIN32Dialog`、`MenuItem`、`Window` 成员 | — |
| [packinone](../modules/packinone/README.md) | PackinOne.dll | `Scripts.loadDataPack/saveDataPack/…`，`Layer.fetchImageSize`，`Storages.saveOctet`，`System.getKnownFolderPath`；`compat/packinone.h`（`HxFilename`） | — |
| [toml](../modules/toml/README.md) | toml.dll | `Scripts.tomlDecodeFromStorage` | — |
| [emote](../modules/emote/README.md) | motionplayer.dll、psbfile.dll | `Motion.*`、`PSBFile`，`CompatNative.emoteStats()` | `-emote=cpu\|gl`、`-emote_readback=sync` |
| [layerexdraw](../modules/layerexdraw/README.md) | layerExDraw.dll（GDI+） | `GdiPlus`、`Layer` 绘图成员 | — |
| [textrender](../modules/textrender/README.md) | textrender.dll | `TextRenderBase` | — |
| [msdfrender](../modules/msdfrender/README.md) | msdfrender.dll | `Layer.makeGlyphMSDF/SDF…` | — |
| [layerstwcopy](../modules/layerstwcopy/README.md) | layerStwCopy.dll | `Layer.stitchWrappedCopy` | — |
| [audio](../modules/audio/README.md) | wfTypicalDSP、wfBasicEffect、wumultitrack | `WaveSoundBuffer.WaveDSPFilter/DelayEffect/StkFreeVerb`，`.mchx`，`setMultiTrackMask` | — |
| [movie-ffmpeg](../modules/movie-ffmpeg/README.md) | Windows 的 mp4/wmv 视频播放 | `CompatNative.movieStats()`；`compat/subtitles.h` | — |
| [webp](../modules/webp/README.md) | （补丁 DLL 的 WebP 解码 hook） | — | — |
| [xpck-patch](../modules/xpck-patch/README.md) | XPCK 补丁包 + hook DLL 配置（如 lllj_zh.dll） | `CompatNative.xpckEnabled/xpckRedirect`；`compat/xpck_patch.h` | `-<配置里的 option>=no\|<路径>` |
| [diagnostics](../modules/diagnostics/README.md) | — | `CompatNative.frameStats/resetFrameStats/storageStats` | `-fpsstats=yes`、`-storagetrace=yes` |

共享的第三方库在 `modules/third_party/`（toml++）；只被一个模块用的放在那个模块的 `third_party/` 里
（PlutoVG → layerexdraw，DSPFilters → audio）。

### 选项改名（相对重构前）

| 旧 | 新 |
|---|---|
| `-lllj_fps=yes` | `-fpsstats=yes` |
| `-lllj_paths=yes` | `-storagetrace=yes` |
| `-lllj_index=no` | `-autopath_incremental=no`（引擎） |
| `-lllj_emote=cpu\|gl` | `-emote=cpu\|gl` |
| `-lllj_zh=…` | 不变（游戏专属，定义在 `games/lllj/zh-patch.toml`） |
| `LLLJNative.*` | `CompatNative.*`（`zhEnabled/zhRedirect` → `xpckEnabled/xpckRedirect`，`hashFilename(leaf, domain)` 多一个参数） |
| 日志 `(lllj) stub:/fps:/renderer:` | `(compat) stub:/fps:/renderer:` |

## 写一个新模块

1. 建目录 `modules/<名字>/`，写 `CMakeLists.txt`：

   ```cmake
   # 一句话：替代什么、怎么做的
   compat_module(<名字>
       PRELUDE prelude/<名字>.tjs          # 可选
       INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include   # 有公开头文件时
       LIBS PkgConfig::...)                # 有外部库时
   ```
   源文件默认是 `src/*.cpp` 和 `src/*.c`；vendor 代码需要强制包含前缀头等特殊编译选项时，
   参考 `modules/emote/CMakeLists.txt`。
2. 原生函数：

   ```cpp
   #include "compat/compat.h"
   COMPAT_NATIVE(myFunc) { ...; return TJS_S_OK; }      // TJS: CompatNative.myFunc(...)
   ```
   需要在命令行解析后做初始化（注册引擎回调等）：
   ```cpp
   static compat::RegisterInit init(compat::OrderModule, "<名字>", [] { TVPAddPresentHook(...); });
   ```
3. prelude 里把 DLL 名接到实现上：

   ```tjs
   compat_mapDll("foo.dll", "compat_foo");        // 静态插件（ncbind 模块）
   // 或
   class CompatFooMembers { function bar() { return CompatNative.fooBar(this); } }
   function compat_shim_foo() { CompatNative.attachMembers(CompatFooMembers, Layer); }
   compat_shim("foo.dll", compat_shim_foo);
   ```
4. 写 `README.md`：替代哪个 DLL、TJS 接口、选项、实现依据（反汇编地址、上游来源与提交）。
   vendor 代码放进模块目录，附 `UPSTREAM.txt`（来源、提交、本地修改清单），本地修改处注释 `PORT`。
5. 在需要它的游戏 profile 里 `compat_use(<名字>)`。

注意事项（移植外部代码时）：先 grep 引擎有没有同名的全局类/函数（曾因 `tTVPSharedMemoryStream` 重名导致虚表混用），
新代码尽量放进命名空间；ncbind 模块必须在前缀头里定义自己的 `TVP_PLUGIN_NAME`。
