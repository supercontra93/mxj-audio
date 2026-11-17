# Architecture decision records

Short notes on decisions that shape the code. One file per decision, numbered, never rewritten:
a changed decision gets a new record that supersedes the old one.

| No. | Decision | Status |
|---|---|---|
| [0001](0001-juce-9.md) | Use JUCE 9.0.5 (not 8) under AGPLv3 | Accepted |
| [0002](0002-fetchcontent.md) | Fetch dependencies with CMake FetchContent instead of CPM | Accepted |
| [0003](0003-juce-builtins.md) | Use JUCE's own JSON, logger and translations in phase 0 | Accepted |
| [0004](0004-command-system.md) | String command ids, bridged to JUCE's command manager | Accepted |
