# 性能测量脚本

为快进 CPU 问题写的（见 `games/lllj/NOTES.md`「快进 CPU」），基于 `games/lllj/tests/scenarios.py` 的 `Session`。在仓库根目录运行，输出目录要用新的。

| 脚本 | 用途 |
|---|---|
| `ab.py OUT 秒数` | 先测 5 秒 idle，再开快进（`kag.allskip=1; SystemAction.skip()`）测 N 秒；输出每线程 CPU、fps、句/s、合成耗时、DrawThread 亲和性 |
| `matrix.sh 标签 [VAR=val ...]` | 在桌面跑 `ab.py` 并打印一行摘要，例如 `games/lllj/tests/perf/matrix.sh dt2 EXTRA=-drawthread=2` |
| `ctx.py OUT` | Xvfb 下快进 10 秒，统计每线程 utime/stime、上下文切换、`schedstat` 精确运行时间 |
| `resample_bench.py OUT` | `Layer.stretchCopy`（stLinear/stCubic，1080p→1440p）耗时 |
| `pixel_check.py OUT` | 跑一组固定的图层操作并保存 BMP，打印哈希；不同 `-drawthread`/`-drawthreadchunk` 下哈希应该一致 |
| `sample.py OUT N` / `wsample.py OUT N` | 快进期间用 gdb 抽栈（主线程 / 全部线程） |
| `menu.py OUT [N]` | 游戏中开关设置画面 N 次（默认 20），输出每次开关的主线程 CPU 毫秒、最长呈现间隔 |
| `bgprefetch.py OUT [秒数] [--no-prefetch]` | 快进 N 秒，统计每次背景加载时图像是否已由预读解码好（cached / inflight / miss），并测一次单张预读 |
| `png_bench.py OUT [N]` | 标题画面下用 `Layer.loadImages` 读 N 张背景 PNG（每张前清图像缓存，取三次最快），输出中位数/均值毫秒 |

共用环境变量：
- `EXTRA="..."`：额外启动参数
- `DESK=1`：在当前桌面跑，否则用私有 Xvfb
- `FULL=1`：全屏（仅 `ab.py`）
- `KRKRZ_BIN_DIR=目录`：`run.sh` 改用该目录里的 `krkrz`/`libSDL3`（A/B 对比旧二进制、PGO 训练构建）

注意：
- 本机 perf 打不开事件（`perf_event_paranoid`），所以用 `schedstat` 和 gdb 抽样代替。
- 桌面测量时窗口必须在前台、不能被遮挡：Wayland 下被遮挡会降频，XWayland 会掉到 1Hz，游戏的"非激活时停止"也会暂停（`-stopdeactive=no` 可以关掉）。
- Xvfb 用 llvmpipe 软件渲染，统计时只看 `krkrz` 和 `DrawThread` 两类线程。
