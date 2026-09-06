# patch-render

Python extension module (pybind11) that renders VST3 and CLAP instrument plugins to WAV files, offline and faster than realtime.

**Why it exists:** Python's [pedalboard](https://github.com/spotify/pedalboard) library never sets `kTempoValid` in the VST3 `ProcessContext`, so tempo-synced delays and LFOs inside plugins run at their internal defaults regardless of your intended BPM. patch-render fixes this with a proper `AudioPlayHead` implementation.

## Installation

Download the wheel for your platform from the [Releases](../../releases) page and install it:

```bash
pip install patch_render
```

## Usage

```python
import patch_render

patch_render.render({
    "plugin":      "/path/to/Synth.vst3",
    "raw_state":   "TVNTbgAA...",
    "bpm":         120.0,
    "sample_rate": 44100,
    "duration":    4.0,
    "output":      "/tmp/render.wav",
    "midi": [
        {"type": "note_on",  "pitch": 60, "vel": 100, "time": 0.0},
        {"type": "note_off", "pitch": 60, "vel":   0, "time": 2.0},
    ],
    "fx_chain": [
        {"plugin": "/path/to/TapeEmulator.vst3", "raw_state": "..."}
    ],
})
```

Raises `RuntimeError` on plugin load or render failure, `ValueError` on a bad config dict.

### CLAP

```python
import patch_render

# Discover plugin IDs inside a .clap bundle
plugins = patch_render.list_clap_plugins("/path/to/Plugin.clap")
# [{"id": "org.example.mysynth", "name": "My Synth", "version": "1.0.0"}, ...]

patch_render.render_clap({
    "plugin":      "/path/to/Plugin.clap",
    "plugin_id":   "org.example.mysynth",
    "raw_state":   "...",
    "bpm":         120.0,
    "sample_rate": 48000.0,
    "duration":    4.0,
    "output":      "/tmp/render.wav",
    "midi": [
        {"type": "note_on",  "pitch": 60, "vel": 100, "time": 0.0},
        {"type": "note_off", "pitch": 60, "vel":   0, "time": 2.0},
    ],
})
```

### Config fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `plugin` | string | required | Absolute path to the instrument `.vst3` |
| `output` | string | required | Absolute path for the output WAV file |
| `duration` | float | required | Total render duration in seconds |
| `raw_state` | string | `""` | Base64 `getStateInformation()` blob — restores a specific preset |
| `bpm` | float | `120.0` | Passed to all plugins via `AudioPlayHead` |
| `sample_rate` | float | `44100.0` | |
| `midi` | array | `[]` | MIDI events: `type` (`note_on`/`note_off`), `pitch`, `vel`, `time` (seconds) |
| `fx_chain` | array | `[]` | Serial FX plugins applied after the instrument; each has `plugin` and optional `raw_state` |

`raw_state` is produced by [patch-probe](https://github.com/blablack/patch-probe). `fx_chain` is optional — omit it for instrument-only renders.

### CLAP config fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `plugin` | string | required | Absolute path to the `.clap` bundle |
| `plugin_id` | string | required | Plugin ID from `list_clap_plugins()` |
| `output` | string | required | Absolute path for the output WAV file |
| `duration` | float | required | Total render duration in seconds |
| `raw_state` | string | `""` | Base64 state blob |
| `bpm` | float | `120.0` | Passed to the plugin via CLAP transport events |
| `sample_rate` | float | `48000.0` | |
| `midi` | array | `[]` | Same schema as VST3 (`type`, `pitch`, `vel`, `time`) |

## Building from source

Requires a C++17 compiler, CMake 3.22+, Ninja, and `python3.14-dev`. On Linux, install the JUCE system dependencies first:

```bash
sudo apt-get install -y \
    python3.14-dev \
    libasound2-dev libfreetype6-dev libx11-dev libxcomposite-dev \
    libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev \
    libxrender-dev libglu1-mesa-dev
```

```bash
git clone --recurse-submodules https://github.com/blablack/patch-render
cd patch-render
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
# extension module: build/src/patch_render_artefacts/Release/patch_render.cpython-314-x86_64-linux-gnu.so
```

Or build a wheel directly:

```bash
uv pip install scikit-build-core pybind11
uv build --wheel --no-build-isolation -o dist
```

## Ecosystem

patch-render is part of a three-tool pipeline:

1. **[patch-probe](https://github.com/blablack/patch-probe)** — extracts VST3 preset state to YAML (`raw_state` blob + parameter values)
2. **patch-render** — renders a preset + MIDI sequence to WAV with correct BPM sync
3. **patch-press** — Python orchestrator that imports patch-render directly, analyses the audio, and exports Deluge-ready WAV + XML sample sets
