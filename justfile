# patch-render dev commands. Run `just` to list them.
# See CLAUDE.md "Build" for the full rationale behind each one.
# NOTE: patch_render itself is a compiled CMake/Ninja artifact, not something uv builds —
# `sync`/`lock`/`upgrade` only manage the dev-group venv (pyyaml, for scripts/render_yaml.py).

set shell := ["bash", "-uc"]

default:
    @just --list

# Install/refresh .venv from uv.lock (dev group only: pyyaml)
sync:
    uv sync

# Re-resolve uv.lock against current pyproject.toml constraints (no version bumps unless
# a constraint changed — uv keeps existing resolutions where still valid)
lock:
    uv lock

# Upgrade all deps to their latest allowed versions and update uv.lock
# e.g.: just upgrade            (everything)
#       just upgrade pyyaml     (just one package)
upgrade *pkg:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ -z "{{pkg}}" ]; then
        uv lock --upgrade
    else
        uv lock --upgrade-package {{pkg}}
    fi

# First-time setup: pull in JUCE + CLAP submodules
submodules:
    git submodule update --init --recursive

# Configure the CMake build (Ninja, Release)
configure:
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build the extension module (configures first if build/ doesn't exist yet)
# Lands at: build/src/patch_render_artefacts/Release/patch_render.cpython-314-x86_64-linux-gnu.so
build:
    #!/usr/bin/env bash
    set -euo pipefail
    [ -d build ] || just configure
    ninja -C build

# Build a wheel with scikit-build-core (same path cibuildwheel uses)
wheel out="/tmp/wheels":
    uv build --wheel --no-build-isolation -o {{out}}

# Render one note from a patch-press YAML config against the locally built extension
# e.g.: just render /path/to/config.yaml --note 60
render *args:
    PYTHONPATH=build/src/patch_render_artefacts/Release uv run --group dev python scripts/render_yaml.py {{args}}

# Format C++ sources (Google style base, 4-space indent, 128 col, Allman braces — see .clang-format)
fmt:
    clang-format -i src/*.cpp src/*.h
