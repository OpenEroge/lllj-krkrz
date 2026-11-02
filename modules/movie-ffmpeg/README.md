# movie-ffmpeg

VideoOverlay backend for mp4/wmv/asf/mov/avi on FFmpeg, registered through the
engine's `TVPExternalMoviePlayerFactory`; WebM and MPEG-1 stay with the engine.
Own decode thread, audio sink sample counter as master clock, seek/loop/pause.
YUV frames are converted on the GPU when the presenter supports it.

- FFmpeg: prebuilt LGPL shared libraries from `tools/fetch_ffmpeg.sh`, loaded
  from next to the executable (`$ORIGIN` RPATH).
- **Subtitles**: ASS scripts rendered with libass onto decoded frames (also in
  overlay mode). Sources come from providers registered via
  `include/compat/subtitles.h`; the xpck-patch module is one.
- `CompatNative.movieStats()`: frames, drops, lateness, subtitle statistics.
- Known media quirks (WMA Pro timestamps after seek, 5 s WMV3 keyframe
  interval) are handled in `src/FFmpegMoviePlayer.cpp`.
