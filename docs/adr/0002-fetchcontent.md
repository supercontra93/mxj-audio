# 0002 - CMake FetchContent instead of CPM

Date: 2026-11-17. Status: accepted.

## Context
The master plan names CPM.cmake for downloading dependencies. CPM is a thin layer over CMake's
built-in FetchContent.

## Decision
Use FetchContent directly, with every dependency pinned to a release tag.

## Consequences
One less third-party file in the repository, nothing to update, same behaviour on Windows and
macOS. If dependency caching becomes important, CPM can still be added later with a new record.
