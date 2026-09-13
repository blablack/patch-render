# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

Requires C++17, CMake 3.22+, Ninja, `python3.14-dev`, and JUCE system dependencies (ALSA, X11, FreeType on Linux). `just` and `uv` drive the dev workflow — run `just` (no args) to list all recipes.

```bash
# First time (JUCE + CLAP submodules must be present)
just submodules

# Configure + build (configures automatically if build/ doesn't exist yet)
just build

# Extension module lands at:
build/src/patch_render_artefacts/Release/patch_render.cpython-314-x86_64-linux-gnu.so
```

Equivalent raw commands: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` then `ninja -C build`.

Alternatively, build a wheel with scikit-build-core (same path cibuildwheel uses):

```bash
just wheel                # → /tmp/wheels
just wheel dist/          # or any other output dir
```

pybind11 is fetched automatically via FetchContent if not already installed; when building via scikit-build-core it is provided through `build-system.requires`.

`patch_render` itself is a compiled CMake/Ninja artifact — `uv`/`just sync` only manage a small dev-only venv (currently just `pyyaml`, for `scripts/render_yaml.py`; see `[tool.uv] package = false` in `pyproject.toml`), never the extension module itself. Run `just sync` once to create `.venv`, `just render <config.yaml>` to exercise `scripts/render_yaml.py` against the locally built extension (wires up `PYTHONPATH` for you), and `just lock`/`just upgrade [pkg]` to manage `uv.lock`.

There is no automated test suite.

## Running

Two independent formats are exposed, each with its own rendering pipeline (see Architecture): VST3 via `render()`, CLAP via `list_clap_plugins()` + `render_clap()`.

```python
import patch_render

patch_render.render({
    "plugin":      "/path/to/Synth.vst3",
    "raw_state":   "",
    "bpm":         120.0,
    "sample_rate": 44100,
    "duration":    3.0,
    "output":      "/tmp/render.wav",
    "midi": [
        {"type": "note_on",  "pitch": 60, "vel": 100, "time": 0.0},
        {"type": "note_off", "pitch": 60, "vel":   0, "time": 2.0},
    ],
    "fx_chain": [
        {"plugin": "/path/to/TapeEmulator.vst3", "raw_state": "..."}
    ],
})

# CLAP: a .clap bundle can contain multiple plugins, so pick one by ID first.
plugins = patch_render.list_clap_plugins("/path/to/Plugin.clap")
patch_render.render_clap({
    "plugin":    "/path/to/Plugin.clap",
    "plugin_id": plugins[0]["id"],
    "bpm":         120.0,
    "sample_rate": 48000.0,
    "duration":    3.0,
    "output":      "/tmp/render.wav",
    "midi": [...],  # same schema as render()
})
```

Raises `RuntimeError` on plugin load or render failure. Raises `ValueError`/`std::invalid_argument` on bad config (missing required fields). JUCE is initialised once at `import patch_render` time (torn down via an `atexit` hook rather than the global's own destructor — see Architecture); `render()` and `render_clap()` calls are serialised against each other internally via one shared mutex.

`fx_chain` is VST3-only — `render_clap()` has no FX chain support.

## Architecture

**Purpose:** A pybind11 Python extension module wrapping headless VST3 and CLAP offline renderers. Both populate the plugin's tempo/transport context (JUCE `AudioPlayHead` for VST3, `CLAP_EVENT_TRANSPORT` for CLAP) with BPM, beat position, and a "playing" flag on every block, so tempo-synced delays and LFOs inside plugins sync correctly. This is the key difference from Python's `pedalboard` library, which never sets `kTempoValid` in the VST3 `ProcessContext`.

**Two parallel pipelines, deliberately not unified.** VST3 goes through JUCE's `AudioPluginFormatManager` (full plugin hosting: parameter management, bus layouts, etc.). CLAP is hosted directly — `dlopen()` the bundle, pull the `clap_entry` symbol, walk `clap_plugin_factory_t` — with no JUCE plugin-hosting layer involved at all; `ClapHost` is a minimal hand-rolled `clap_host_t` whose callbacks are stubs (fine for one-shot offline rendering, not for a real-time host). Do not try to route CLAP through `PluginLoader`/`Renderer` or vice versa; the two code paths share only `MidiEvent` and the WAV-writing pattern.

**VST3 data flow:**

```
patch_render.render(dict)
  → Bindings.cpp: configFromDict()        — py::dict → RenderConfig struct
      → RenderPlayHead constructed first, passed into every load() call below
      → PluginLoader: load instrument VST3 + restoreState() from base64 raw_state
      → PluginLoader: load each FX plugin + restoreState() (stereo in/out, same playhead)
      → Renderer: block loop (512 samples/block)
            instrument->processBlock(buffer, midiBlock)
            for each fx: fx->processBlock(buffer, emptyMidi)
            playHead.advance(samplesThisBlock)
            → AudioFormatWriter → WAV file
```

**CLAP data flow:**

```
patch_render.render_clap(dict)
  → Bindings.cpp: clapConfigFromDict()     — py::dict → ClapRenderConfig struct
      → ClapRenderer::render():
          getOrLoadEntry(pluginPath)        — dlopen + clap_entry lookup, PROCESS-LIFETIME CACHE (see below)
          factory->create_plugin(id)        — one fresh instance per render call
          plugin->init() / activate() / start_processing()
          stateExt->load() from base64 raw_state (CLAP_EXT_STATE)
          block loop (512 samples/block):
              build clap_event_transport_t with bpm/song_pos_beats/song_pos_seconds for this block
              collect due MIDI events into an InputEventList (CLAP_EVENT_NOTE_ON/OFF)
              plugin->process()             — output events from the plugin are silently discarded
              downmix to stereo if the plugin's output has >2 channels (CLAP_EXT_AUDIO_PORTS)
              → AudioFormatWriter → WAV file
          plugin->stop_processing() / deactivate() / destroy()   — instance only; library stays loaded
```

**Key source files:**

- `src/Bindings.cpp` — pybind11 module entry point. Initialises `ScopedJuceInitialiser_GUI` once at import time and registers a Python `atexit` hook to reset it explicitly *before* module unload — without this, the global's own destructor runs after `Py_Finalize()` and crashes in `shutdownJuce_GUI()`. Exposes `render()`, `render_clap()`, `list_clap_plugins()`. `configFromDict()`/`clapConfigFromDict()` map the Python dict directly to their respective config structs without a JSON round-trip.
- `src/PluginLoader.h/cpp` — Wraps `AudioPluginFormatManager` to scan and instantiate a `.vst3` file. `restoreState()` decodes a base64 blob and calls `setStateInformation()`.
- `src/RenderPlayHead.h` — `juce::AudioPlayHead` subclass for the VST3 path. `getPosition()` returns a `PositionInfo` with `setBpm()`, `setPpqPosition()`, `setIsPlaying(true)`, and `setTimeSignature({4,4})` set on every call. `advance(n)` increments the internal sample counter after each block.
- `src/Renderer.h/cpp` — Owns the VST3 render loop. Attaches the playhead via `plugin->setPlayHead()` (called before `prepareToPlay()` — see timing note below), builds per-block `MidiBuffer` from time-sorted events, calls `plugin->processBlock()`, writes samples with `AudioFormatWriter`.
- `src/ClapHost.h/cpp` — Minimal `clap_host_t` for offline batch rendering. Implements `log`, `thread-check` (reports every call as both main- and audio-thread — true here since rendering is single-threaded), `state`, and `params` extensions; everything else is a no-op stub.
- `src/ClapRenderer.h/cpp` — Owns the CLAP render loop, including the process-lifetime library cache described below.

**Why the CLAP library/entry is cached for the process lifetime, not per render:** `clap_entry.init()`/`deinit()` are meant to bracket the *library's* lifetime in the process, not each render. Some plugins (u-he Diva, confirmed via gdb) build/tear down global machinery (thread pools, internal mutexes) inside those calls; calling them once per render eventually leaks a mutex and deadlocks the whole process after ~1,270 renders (fixed in v0.3.1 — see CHANGELOG). `getOrLoadEntry()` in `ClapRenderer.cpp` therefore `dlopen`s and `init()`s each distinct plugin path exactly once per process and caches the `(lib, entry, factory)` triple in a static map; only the plugin *instance* is created/destroyed per render, which is what keeps renders' audio independent (a previous render's voices/delay tail die with its instance). If you touch this code, preserve that split — do not move `init()`/`deinit()` back inside `render()`.

**Thread safety:** both `render()` and `render_clap()` release the GIL before acquiring a single shared static mutex, so Python threads aren't blocked waiting, but only one render (of either format) runs at a time (JUCE's MessageManager is not re-entrant, and the CLAP path shares the same JUCE-initialised process).

**VST3 config dict schema:**

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

**CLAP config dict schema:** same as above minus `fx_chain`, plus a required `plugin_id` (from `list_clap_plugins()`) and `sample_rate` defaulting to 48000.0 instead of 44100.0.

**PlayHead/transport timing convention (both pipelines):** the position reported for a block is the position at its *start*. For VST3, `advance()` is called *after* `processBlock()` returns. For CLAP, `song_pos_beats`/`song_pos_seconds` are computed from `position` *before* `plugin->process()` is called for that block. In both cases the plugin sees the correct beat position for the samples it's about to process — and in the VST3 case specifically, the playhead is attached before `prepareToPlay()` (not after), matching DAW plugin-hosting order; getting this backwards previously caused an audible pitch-sweep artifact on BPM-synced delays (fixed in v0.2.2).

## Code style

Google C++ style as base, with these overrides (enforced by `.clang-format`):
- Indent: 4 spaces
- Column limit: 128
- Brace style: Allman (opening brace on its own line)

Run `just fmt` (or `clang-format -i src/*.cpp src/*.h` directly) to format.

## Ecosystem context

patch-render is one tool in a three-stage pipeline:

1. **patch-probe** (`../patch-probe`) — GUI/CLI that extracts VST3/CLAP preset state to YAML files containing `raw_state` (base64 binary blob) and `parameter_state` (normalized floats).
2. **patch-render** (this repo) — Python extension module that renders a VST3 or CLAP preset to WAV given a `raw_state` blob and MIDI events.
3. **patch-press** (`../patch-press`) — Python orchestrator that imports patch-render directly, analyses the resulting audio (loop points, envelope, sustain type), and exports Deluge-ready WAV + XML sample sets.

The `raw_state` field produced by patch-probe is the primary input to patch-render's `"raw_state"` config key.
