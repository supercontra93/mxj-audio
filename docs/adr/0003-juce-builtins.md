# 0003 - JUCE's own JSON, logger and translations in phase 0

Date: 2026-11-17. Status: accepted.

## Context
The master plan lists nlohmann/json for project and settings files. Phase 0 needs settings,
a log file and translations.

## Decision
Use `juce::JSON` for settings, `juce::FileLogger` behind `mxj::Log`, and `juce::LocalisedStrings`
for translations. No new dependency. nlohmann/json stays an option for the project file
(phase 5) and gets its own record if it is added.

## Consequences
Fewer dependencies and licences to track. Code outside `services/` uses our wrappers
(`Settings`, `Log`, `Localisation`), so the backend can change without touching callers.
