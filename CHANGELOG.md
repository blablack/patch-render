# Changelog

## [v0.3.2] - 2026-08-22
### Added
- Juce 9.0.1

## [v0.3.1] - 2026-07-19
### Fixed
- Deadlock when rendering many presets in one process: `ClapRenderer` ran `clap_entry.init()`/`deinit()` per render, churning the plugin's global state until Diva leaked an internal mutex (~1,270 renders in). Now the library is loaded and initialized once per process and cached; only the plugin instance is created/destroyed per render, so renders stay independent.

## [v0.3.0] - 2026-05-25
### Added
- CLAP plugin support via two new Python functions:
  - `patch_render.render_clap(payload)` — renders a CLAP instrument to WAV. Config fields mirror the VST3 `render()` schema, with `plugin_id` (required, string) added to select a specific plugin within a `.clap` bundle.
  - `patch_render.list_clap_plugins(plugin_path)` — enumerates all plugins in a `.clap` file; returns a list of dicts with `id`, `name`, `vendor`, `description`.
- `ClapHost`: minimal `clap_host_t` implementation with stub callbacks sufficient for offline rendering; exposes `log`, `thread-check`, `state`, and `params` extensions.
- `ClapRenderer`: block render loop (512 samples/block) with a `CLAP_EVENT_TRANSPORT` sent on every block carrying BPM, beat-position, second-position, and 4/4 time signature — equivalent to the VST3 `RenderPlayHead` fix for tempo-synced plugins. Output channel count is queried via `CLAP_EXT_AUDIO_PORTS` and downmixed to stereo when the plugin exposes more than two channels. State is restored from a base64 blob via `CLAP_EXT_STATE`.

### Fixed
- Segfault on process exit when Python finalizes the interpreter. The `ScopedJuceInitialiser_GUI` global was being destroyed during `.so` unload (after `Py_Finalize`), at which point JUCE's `shutdownJuce_GUI()` could no longer safely stop the message thread. Fixed by registering a Python `atexit` handler at module init time that explicitly resets the initializer before module teardown.

## [v0.2.2] - 2026-05-11
### Fixed
- BPM-synced delays (e.g. Odin2) produced a pitch-sweep artifact at the start of every render. The play head is now set before `prepareToPlay()`, matching DAW plugin hosting order. Previously the play head arrived after init, so delays saw no tempo during `prepareToPlay` and smoothly interpolated to the correct delay time on the first `processBlock`, causing the audible sweep.

## [v0.2.1] - 2026-05-10
### Changed
- patch-probe writes standard RFC 4648 base64, update decoding (same as Spotify PedalBoard)

## [v0.2.0] - 2026-05-10
### Changed
- Converted from a standalone CLI binary to a pybind11 Python extension module (`patch_render.cpython-*.so`)
- `patch_render.render(dict)` replaces stdin JSON — same field schema, callable directly from Python without subprocess overhead
- cibuildwheel CI: produces `manylinux_2_28` Linux wheels and macOS arm64/x86_64 wheels on tag push, attached to GitHub releases

## [v0.1.0] - 2026-05-08
### Added
- Initial release: headless VST3 offline renderer
- JSON config via stdin (`plugin`, `raw_state`, `bpm`, `sample_rate`, `duration`, `output`, `midi`)
- Custom `AudioPlayHead` that sets BPM, PPQ position, and `kTempoValid` — fixes tempo-synced delays and LFOs that misbehave under pedalboard's bare-bones host context
- Offline block render loop (512 samples/block) with per-block `MidiBuffer` scheduling
- 24-bit stereo WAV output via `juce::WavAudioFormat` + `AudioFormatWriterOptions`
- State restore from base64 `getStateInformation` blob (patch-probe YAML compatible)
- Serial FX chain support (`fx_chain` array): instrument audio passes through each FX plugin in order; all plugins share the same `AudioPlayHead` for BPM-sync throughout the chain
