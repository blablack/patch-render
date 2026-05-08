# patch-render

Headless CLI tool that renders a VST3 instrument plugin to a WAV file, offline and faster than realtime. Accepts a JSON config on stdin, writes a WAV file to disk.

**Why it exists:** Python's [pedalboard](https://github.com/spotify/pedalboard) library never sets `kTempoValid` in the VST3 `ProcessContext`, so tempo-synced delays and LFOs inside plugins run at their internal defaults regardless of your intended BPM. patch-render fixes this with a proper `AudioPlayHead` implementation, making it a drop-in subprocess replacement for pedalboard's render step.

## Usage

```bash
echo '{
  "plugin":      "/path/to/Synth.vst3",
  "raw_state":   "TVNTbgAA...",
  "bpm":         120.0,
  "sample_rate": 44100,
  "duration":    4.0,
  "output":      "/tmp/render.wav",
  "midi": [
    {"type": "note_on",  "pitch": 60, "vel": 100, "time": 0.0},
    {"type": "note_off", "pitch": 60, "vel":   0, "time": 2.0}
  ],
  "fx_chain": [
    {"plugin": "/path/to/TapeEmulator.vst3", "raw_state": "..."}
  ]
}' | patch-render
```

### JSON fields

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

### Exit codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| `1` | Bad JSON or missing required field |
| `3` | Plugin load or render failure |

All diagnostic messages go to stderr; stdout is silent.

### Python integration

```python
import subprocess, json, wave

config = {
    "plugin":      "/path/to/Synth.vst3",
    "raw_state":   preset_yaml["source"]["raw_state"],
    "bpm":         120.0,
    "sample_rate": 44100,
    "duration":    4.0,
    "output":      "/tmp/render.wav",
    "midi": [
        {"type": "note_on",  "pitch": 60, "vel": 100, "time": 0.0},
        {"type": "note_off", "pitch": 60, "vel":   0, "time": 2.0},
    ],
}

result = subprocess.run(["patch-render"], input=json.dumps(config), capture_output=True, text=True)
if result.returncode != 0:
    raise RuntimeError(result.stderr)
```

## Building from source

Requires a C++17 compiler, CMake 3.22+, and Ninja. On Linux, install the JUCE system dependencies first:

```bash
sudo apt-get install -y \
    libasound2-dev libfreetype6-dev libx11-dev libxcomposite-dev \
    libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev \
    libxrender-dev libwebkit2gtk-4.1-dev libglu1-mesa-dev
```

```bash
git clone --recurse-submodules https://github.com/blablack/patch-render
cd patch-render
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
# binary: build/src/patch-render_artefacts/Release/patch-render
```

## Ecosystem

patch-render is part of a three-tool pipeline:

1. **[patch-probe](https://github.com/blablack/patch-probe)** — extracts VST3 preset state to YAML (`raw_state` blob + parameter values)
2. **patch-render** — renders a preset + MIDI sequence to WAV with correct BPM sync
3. **patch-press** — Python orchestrator that calls patch-render, analyses the audio, and exports Deluge-ready WAV + XML sample sets

## Download

Pre-built binaries for Linux, macOS, and Windows are available on the [Releases](../../releases) page.
