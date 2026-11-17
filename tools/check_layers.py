#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Checks the layer rule of MXJ AUDIO (master plan, section 6).

Code in src/<layer>/ may only include headers from its own layer or from layers
below it. Run from anywhere: python3 tools/check_layers.py
Exit code 0 = rule holds, 1 = violations found (printed one per line).
"""
import pathlib
import re
import sys

# Bottom to top. Keep in step with the top-level CMakeLists.txt and CLAUDE.md.
LAYERS = ["core", "dsp", "formats", "engine", "plugins", "services", "app"]
RANK = {name: i for i, name in enumerate(LAYERS)}
INCLUDE = re.compile(r'^\s*#\s*include\s*"([a-z_]+)/', re.MULTILINE)
# User interface modules may only be used by the app layer, so that every other
# layer can be tested without a window.
GUI_INCLUDE = re.compile(r'^\s*#\s*include\s*<(juce_gui_basics|juce_gui_extra|juce_graphics|juce_opengl)/', re.MULTILINE)

root = pathlib.Path(__file__).resolve().parent.parent / "src"
violations = []

for layer in LAYERS:
    for path in sorted((root / layer).rglob("*")):
        if path.suffix not in {".h", ".hpp", ".cpp", ".in"}:
            continue
        text = path.read_text(encoding="utf-8")
        for match in INCLUDE.finditer(text):
            target = match.group(1)
            if target in RANK and RANK[target] > RANK[layer]:
                line = text.count("\n", 0, match.start()) + 1
                violations.append(
                    f"{path.relative_to(root.parent)}:{line}: layer '{layer}' must not include '{target}/' (a higher layer)")
        if layer != "app":
            for match in GUI_INCLUDE.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                violations.append(
                    f"{path.relative_to(root.parent)}:{line}: layer '{layer}' must not use the UI module '{match.group(1)}'")

if violations:
    print("\n".join(violations))
    print(f"\n{len(violations)} layer rule violation(s). See CLAUDE.md, 'Layers'.")
    sys.exit(1)

print(f"Layer rule OK ({', '.join(LAYERS)})")
