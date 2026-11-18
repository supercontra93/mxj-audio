# mxj_plugins

Plugins (phase 6, ADR 0010):

- `host/` VST3 and CLAP plugins as effects: `PluginHost` (formats, list of plugins, block list, folders,
  effect types), `HostedPlugin` (one live plugin shared by the rack and the mixer), `PluginScanner`
  (scans each file in the separate `mxj-plugin-scanner` program).
- `clap/` our own small CLAP host (JUCE hosts VST3 only).
- `lua/` MXJ plugins written in Lua 5.4: `plugin.json` (`LuaManifest`), the sandbox (`LuaScript`),
  installed and developer plugins (`LuaPluginManager`). The API is in `docs/plugins/lua-api.md`.
- `catalogue/` the community catalogue (a local test catalogue until the release).
- `scanner/` the scanner program.

This layer uses JUCE's windowless plugin module only. Plugin windows are made by the app layer.
