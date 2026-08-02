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


def resolve(presets, name, seen=None):
    """Merge a preset with everything it inherits from, nearest wins."""
    if seen is None:
        seen = set()
    if name in seen:
        sys.stderr.write("error: circular inherits at preset '%s'\n" % name)
        raise SystemExit(1)
    seen.add(name)

    for preset in presets.get("configurePresets", []):
        if preset.get("name") == name:
            break
    else:
        available = [p.get("name") for p in presets.get("configurePresets", [])]
        sys.stderr.write(
            "error: no preset named '%s'\navailable: %s\n"
            % (name, ", ".join(a for a in available if a))
        )
        raise SystemExit(1)

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


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: mr_preset_flags.py <directory> <preset>\n")
        return 1

    variables = resolve(load_presets(argv[1]), argv[2])

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
