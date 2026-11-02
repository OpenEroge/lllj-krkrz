# audio

- **Voice effects** (`wfTypicalDSP.dll`, `wfBasicEffect.dll`):
  `WaveSoundBuffer.WaveDSPFilter`, `DelayEffect`, `StkFreeVerb` as engine wave
  filters (`iTVPBasicWaveFilter`); scripts put instances into `buf.filters`
  before `open()`. Implemented: band-pass (Butterworth, via
  [DSPFilters](third_party/DSPFilters/UPSTREAM.txt)), delay, reverb. Not
  implemented (no known user): EQ, vocoder, ring modulation, oscillators,
  gain limit, `OscLoop`.
- **Multi-track BGM** (`wumultitrack.dll`): `.mchx` manifests are a wave
  decoder; `WaveSoundBuffer.setMultiTrackMask(id, name|void, vol, ms)` sets
  per-track gain; open callbacks go to `WaveSoundBuffer.multiTrackOpenCallbacks`.
- `CompatNative.waveFilterGain(filter, hz)`: test helper.
