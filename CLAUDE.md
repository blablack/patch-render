# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

Requires C++17, CMake 3.22+, Ninja, and JUCE system dependencies (ALSA, X11, FreeType, WebKit2GTK on Linux — see `.github/workflows/build-release.yml` for the full apt list).

```bash
# First time (JUCE submodule must be present)
git submodule update --init --recursive

# Configure
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
ninja -C build

# Binary lands at:
build/src/patch-render_artefacts/Release/patch-render
```

Incremental rebuilds only recompile changed `.cpp` files; JUCE modules are precompiled and cached.

## Running

The binary reads a JSON config from stdin and writes a WAV file:

```bash
echo '{
  "plugin": "/path/to/Synth.vst3",
  "raw_state": "",
  "bpm": 120.0,
  "sample_rate": 44100,
  "duration": 3.0,
  "output": "/tmp/render.wav",
  "midi": [
    {"type": "note_on",  "pitch": 60, "vel": 100, "time": 0.0},
    {"type": "note_off", "pitch": 60, "vel": 0,   "time": 2.0}
  ]
}' | ./build/src/patch-render_artefacts/Release/patch-render
```

Exit codes: `0` success · `1` bad JSON or missing field · `3` render failed. All diagnostics go to stderr; stdout is silent.

## Architecture

**Purpose:** A headless C++ JUCE CLI that renders VST3 plugins to WAV offline, with a properly populated `AudioPlayHead` so tempo-synced delays and LFOs inside plugins sync to the requested BPM. This is the key difference from Python's `pedalboard` library, which never sets `kTempoValid` in the VST3 `ProcessContext`.

**Data flow:**

```
stdin JSON
  → Main.cpp: parseConfig()
      → PluginLoader: load instrument VST3 + restoreState() from base64 raw_state
      → PluginLoader: load each FX plugin + restoreState() (stereo in/out, same playhead)
      → RenderPlayHead: attached to all plugins, returns BPM/beat position on every processBlock()
      → Renderer: block loop (512 samples/block)
            instrument->processBlock(buffer, midiBlock)
            for each fx: fx->processBlock(buffer, emptyMidi)
            → AudioFormatWriter → WAV file
```

**Key source files:**

- `src/Main.cpp` — Entry point. Reads stdin, parses JSON with `juce::JSON::parse()`, calls `Renderer::render()`.
- `src/PluginLoader.h/cpp` — Wraps `AudioPluginFormatManager` to scan and instantiate a `.vst3` file. `restoreState()` decodes a base64 blob and calls `setStateInformation()`.
- `src/RenderPlayHead.h` — `juce::AudioPlayHead` subclass. `getPosition()` returns a `PositionInfo` with `setBpm()`, `setPpqPosition()`, `setIsPlaying(true)`, and `setTimeSignature({4,4})` set on every call. `advance(n)` increments the internal sample counter after each block.
- `src/Renderer.h/cpp` — Owns the render loop. Attaches the playhead via `plugin->setPlayHead()`, builds per-block `MidiBuffer` from time-sorted events, calls `plugin->processBlock()`, writes samples with `AudioFormatWriter`.

**JSON input schema:**

| Field | Type | Default | Notes |
|-------|------|---------|-------|
| `plugin` | string | required | Absolute path to `.vst3` |
| `output` | string | required | Absolute path for output WAV |
| `duration` | float | required | Seconds to render |
| `raw_state` | string | `""` | Base64 `getStateInformation()` blob from patch-probe |
| `bpm` | float | 120.0 | Passed to plugin via `AudioPlayHead` |
| `sample_rate` | float | 44100.0 | |
| `midi` | array | `[]` | Objects with `type` (`note_on`/`note_off`), `pitch`, `vel`, `time` (seconds) |
| `fx_chain` | array | `[]` | Serial FX plugins: each has `plugin` (path) and `raw_state`; loaded with stereo in/out |

**PlayHead timing convention:** `getPosition()` reports the position at the *start* of the current block. `advance()` is called *after* `processBlock()` returns, so the plugin always sees the correct beat position for the samples it is processing.

## Code style

Google C++ style as base, with these overrides (enforced by `.clang-format`):
- Indent: 4 spaces
- Column limit: 128
- Brace style: Allman (opening brace on its own line)

Run `clang-format -i src/*.cpp src/*.h` to format.

## Ecosystem context

patch-render is one tool in a three-stage pipeline:

1. **patch-probe** (`../patch-probe`) — GUI/CLI that extracts VST3 preset state to YAML files containing `raw_state` (base64 binary blob) and `parameter_state` (normalized floats).
2. **patch-render** (this repo) — Renders a VST3 preset to WAV given a `raw_state` blob and MIDI events.
3. **patch-press** (`../patch-press`) — Python orchestrator that calls patch-render via `subprocess`, analyzes the resulting audio (loop points, envelope, sustain type), and exports Deluge-ready WAV + XML sample sets.

The `raw_state` field produced by patch-probe is the primary input to patch-render's `"raw_state"` JSON field.
