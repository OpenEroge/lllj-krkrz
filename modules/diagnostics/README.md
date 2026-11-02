# diagnostics

Measurement used by the test scenarios.

- `-fpsstats=yes`: counts presented frames (engine present hook, after the
  buffer swap) and logs one JSON line per second, `(compat) fps: {...}`
  (fps, longest interval, swap/show time, composition and upload time), plus the
  GL renderer once. `CompatNative.frameStats()` / `resetFrameStats(scene)`.
- Storage counters: `CompatNative.storageStats()` (scans, rebuilds, writes,
  clears, listing hits). `-storagetrace=yes` logs each event and every
  `Storages.addAutoPath/removeAutoPath` call with its TJS stack.
