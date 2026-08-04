#!/usr/bin/env python3
"""Emit the -D flags for a named CMake preset.

The Pico VS Code extension and every task in microrender/.vscode/tasks.json
are hardcoded to ${workspaceFolder}/build. Presets each use their own binary
directory, and CMake does not let you override binaryDir from the command line,
so "configure preset X into build/" is not expressible as a preset invocation.

Rather than duplicating the preset contents into a batch script, this reads
CMakePresets.json and prints the cache variables of the requested preset,
resolving `inherits` chains. The presets stay the single source of truth.

    python mr_preset_flags.py microrender stress-lace
    -DMR_APP=STRESS -DMR_STRESS_MODE=lace -DMR_STRESS_SPRITES=1024 ...
"""

import json
import os
import sys


def load_presets(directory):
    path = os.path.join(directory, "CMakePresets.json")
    if not os.path.isfile(path):
        sys.stderr.write("error: no CMakePresets.json in %s\n" % directory)
        raise SystemExit(1)
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def find_preset(presets, name):
    for preset in presets.get("configurePresets", []):
        if preset.get("name") == name:
            return preset
    available = [p.get("name") for p in presets.get("configurePresets", [])]
    sys.stderr.write(
        "error: no preset named '%s'\navailable: %s\n"
        % (name, ", ".join(a for a in available if a))
    )
    raise SystemExit(1)


def resolve(presets, name, seen=None):
    """Merge cache variables from a preset inheritance chain; nearest wins."""
    if seen is None:
        seen = set()
    if name in seen:
        sys.stderr.write("error: circular inherits at preset '%s'\n" % name)
        raise SystemExit(1)
    seen.add(name)

    preset = find_preset(presets, name)

    merged = {}
    parents = preset.get("inherits", [])
    if isinstance(parents, str):
        parents = [parents]
    # Later entries in `inherits` have lower priority than earlier ones, and
    # all of them lose to the preset's own values.
    for parent in reversed(parents):
        merged.update(resolve(presets, parent, set(seen)))
    merged.update(preset.get("cacheVariables", {}))
    return merged


def resolve_field(presets, name, field, seen=None):
    """Resolve one inherited preset field, preferring the nearest definition."""
    if seen is None:
        seen = set()
    if name in seen:
        sys.stderr.write("error: circular inherits at preset '%s'\n" % name)
        raise SystemExit(1)
    seen.add(name)

    preset = find_preset(presets, name)
    if field in preset:
        return preset[field]

    parents = preset.get("inherits", [])
    if isinstance(parents, str):
        parents = [parents]
    for parent in parents:
        value = resolve_field(presets, parent, field, set(seen))
        if value is not None:
            return value
    return None


def main(argv):
    if len(argv) not in (3, 4):
        sys.stderr.write(
            "usage: mr_preset_flags.py <directory> <preset> "
            "[--binary-dir|--generator]\n"
        )
        return 1

    directory = os.path.abspath(argv[1])
    preset_name = argv[2]
    presets = load_presets(directory)

    if len(argv) == 4:
        if argv[3] == "--binary-dir":
            value = resolve_field(presets, preset_name, "binaryDir")
            if not value:
                sys.stderr.write("error: preset has no binaryDir\n")
                return 1
            value = value.replace("${sourceDir}", directory)
            sys.stdout.write(os.path.normpath(value))
            return 0
        if argv[3] == "--generator":
            value = resolve_field(presets, preset_name, "generator")
            if not value:
                sys.stderr.write("error: preset has no generator\n")
                return 1
            sys.stdout.write(str(value))
            return 0
        sys.stderr.write("error: unknown option %s\n" % argv[3])
        return 1

    variables = resolve(presets, preset_name)

    flags = []
    for key in sorted(variables):
        value = variables[key]
        if isinstance(value, bool):
            value = "ON" if value else "OFF"
        elif isinstance(value, dict):
            # {"type": "STRING", "value": "..."} form
            value = value.get("value", "")
        flags.append("-D%s=%s" % (key, value))

    sys.stdout.write(" ".join(flags))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
