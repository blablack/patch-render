"""Render a single note from a patch-press YAML config using patch_render."""

import argparse
from pathlib import Path

import yaml
import patch_render


_LEAD_IN_S = 5
_DEFAULT_NOTE = 60
_DEFAULT_VEL = 100
_DEFAULT_BPM = 120.0
_SAMPLE_RATE = 44100


def main():
    parser = argparse.ArgumentParser(description="Render a single note from a patch-press YAML config")
    parser.add_argument("config", type=Path, help="Path to patch-press YAML config file")
    parser.add_argument("--note", type=int, default=_DEFAULT_NOTE, help="MIDI note (default: 60)")
    parser.add_argument("--vel", type=int, default=_DEFAULT_VEL, help="MIDI velocity (default: 100)")
    parser.add_argument("--output", type=Path, default=None, help="Output WAV path (default: <config_stem>.wav)")
    args = parser.parse_args()

    with open(args.config) as f:
        cfg = yaml.safe_load(f)

    source = cfg["source"]
    capture = cfg.get("capture", {})

    plugin = source["plugin"]
    raw_state = source.get("raw_state", "")
    duration_s = capture.get("duration_s", 4.0)
    release_tail_s = capture.get("release_tail_s", 2.0)
    bpm = capture.get("tempo_bpm", _DEFAULT_BPM)

    total_s = _LEAD_IN_S + duration_s + release_tail_s

    output = args.output or args.config.with_suffix(".wav")

    patch_render.render({
        "plugin": plugin,
        "raw_state": raw_state,
        "bpm": float(bpm),
        "sample_rate": float(_SAMPLE_RATE),
        "duration": float(total_s),
        "output": str(output),
        "midi": [
            {"type": "note_on",  "pitch": args.note, "vel": args.vel,  "time": _LEAD_IN_S},
            {"type": "note_off", "pitch": args.note, "vel": 0,         "time": _LEAD_IN_S + duration_s},
        ],
    })

    print(f"Rendered to {output}")


if __name__ == "__main__":
    main()
