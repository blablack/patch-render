# Changelog

## [v0.1.0] - 2026-05-08
### Added
- Initial release: headless VST3 offline renderer
- JSON config via stdin (`plugin`, `raw_state`, `bpm`, `sample_rate`, `duration`, `output`, `midi`)
- Custom `AudioPlayHead` that sets BPM, PPQ position, and `kTempoValid` — fixes tempo-synced delays and LFOs that misbehave under pedalboard's bare-bones host context
- Offline block render loop (512 samples/block) with per-block `MidiBuffer` scheduling
- 24-bit stereo WAV output via `juce::WavAudioFormat` + `AudioFormatWriterOptions`
- State restore from base64 `getStateInformation` blob (patch-probe YAML compatible)
- Serial FX chain support (`fx_chain` array): instrument audio passes through each FX plugin in order; all plugins share the same `AudioPlayHead` for BPM-sync throughout the chain
