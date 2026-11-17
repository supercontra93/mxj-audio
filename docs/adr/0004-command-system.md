# 0004 - String command ids, bridged to JUCE's command manager

Date: 2026-11-17. Status: accepted.

## Context
Master plan: every user action is one command that menus, shortcuts, the command palette,
macros and Lua plugins can all call. JUCE's `ApplicationCommandManager` uses integer ids and
lives in the UI module.

## Decision
`mxj::CommandRegistry` (core layer, no UI) stores commands under stable string ids such as
`edit.undo`, with name, category, description, default shortcut, enabled check, tick check and
action. The app layer maps them to JUCE integer ids (registration order + 1) for menus and
keyboard shortcuts.

## Consequences
Macros and Lua plugins (later phases) can refer to commands by readable, stable names. The
registry is tested without a window. Command ids must never be renamed once released.
