# Writing MXJ plugins (Lua API version 1)

An MXJ plugin is a small Lua 5.4 program that MXJ AUDIO runs in a sandbox. It can be an **effect**
(changes the selection), a **generator** (makes new audio), an **analysis** (reads audio and writes a report)
or a **tool** (runs program commands). The examples in `resources/lua-plugins/` are complete plugins; copy one
to start.

## Files

```
my-plugin/
  plugin.json    what the plugin is, its parameters
  main.lua       the code
```

To share a plugin, zip the files (not the folder) and rename the zip to `name.mxjplugin`.
Users install it with **Plugins > Install Plugin from File...** or from the catalogue.

While you write it, use **Plugins > Plugins... > Developer > Load from Folder...**: the plugin is loaded from
your folder (not copied), reloaded every time you save a file, and its errors and `print()` lines appear in the
console on that tab.

## plugin.json

```json
{
  "id": "com.example.tremolo",
  "name": "Tremolo",
  "version": "1.0.0",
  "author": "Jane Doe",
  "license": "MIT",
  "type": "effect",
  "category": "Modulation",
  "description": "Moves the volume up and down.",
  "homepage": "https://example.com",
  "minMxjVersion": "0.6.0",
  "apiVersion": 1,
  "permissions": [],
  "main": "main.lua",
  "parameters": [
    { "id": "rate",  "name": "Rate",  "type": "number", "min": 0.5, "max": 20, "default": 5, "unit": "Hz", "decimals": 1 },
    { "id": "shape", "name": "Shape", "type": "choice", "choices": ["Smooth", "Hard"], "default": 0 },
    { "id": "sync",  "name": "Sync",  "type": "toggle", "default": false }
  ]
}
```

| Field | Required | Meaning |
|---|---|---|
| `id` | yes | 3 to 64 characters: lower case letters, digits, `.`, `-`, `_`. Never change it; updates are matched by id. |
| `name`, `version`, `author`, `license` | yes | Shown on the plugin page. `version` like `1.2.0`. |
| `type` | yes | `effect`, `generator`, `analysis` or `tool`. |
| `category`, `description`, `homepage` | no | Shown on the plugin page and in the catalogue. |
| `minMxjVersion` | no | Oldest MXJ AUDIO that can run the plugin (default `0.6.0`). |
| `apiVersion` | no | The API version the plugin was written for (default 1). Newer than the program's: refused. |
| `permissions` | no | `["commands"]` lets the plugin run program commands. The user sees it before installing. |
| `main` | no | The Lua file to run (default `main.lua`). |
| `parameters` | no | Controls MXJ AUDIO draws for the plugin. |

Parameter types: `number` (`min`, `max`, `default`, `unit`, `decimals` 0 to 4, optional `mid` = the value in
the middle of the slider for log-like scales), `choice` (`choices`, `default` = index from 0) and `toggle`
(`default` true or false). Generators always have a `duration` parameter in seconds; one is added if missing.

## Functions MXJ AUDIO calls

| Type | Functions |
|---|---|
| effect | `prepare(sampleRate, channels, params)` (optional), `process(block, params)` |
| generator | `prepare(...)` (optional), `generate(block, params)` |
| analysis | `prepare(...)` (optional), `analyse(block, params)`, `finish(params)` returns the report text |
| tool | `run(params)` may return a text that is shown to the user |

`params` holds the parameter values by id: numbers in their unit, a choice as its index (0 = first),
a toggle as 1 or 0.

Effects, generators and analyses get the audio in blocks, one after the other, from the start of the
selection (or the file) to its end. Keep state between blocks in local variables and reset it in `prepare`.

## The block

| | |
|---|---|
| `block:get(channel, frame)` | sample value, channels and frames counted from 1 |
| `block:set(channel, frame, value)` | write a sample (effects and generators) |
| `block.channels`, `block.frames` | size of this block |
| `block.sampleRate` | Hz |
| `block.position` | where this block starts, in frames from the start of the selection (0 for the first) |
| `block.length` | length of the whole selection in frames |

A block is valid only during the call that received it.

## The `mxj` table

| | |
|---|---|
| `mxj.apiVersion` | 1 |
| `mxj.version` | MXJ AUDIO version, for example `"0.6.0"` |
| `mxj.log(text)` | a line in the developer console (`print` does the same) |
| `mxj.message(text)` | tools: a message shown to the user when the tool ends |
| `mxj.command(id)` | runs a program command such as `"edit.undo"` or `"view.zoomFull"`; needs the `commands` permission; returns true if it ran |
| `mxj.dbToGain(db)`, `mxj.gainToDb(gain)` | level conversion |

Command ids are the ones of the menus (`file.save`, `edit.selectAll`, `transport.toStart`, ...). Plugins
cannot run `plugins.*` commands or quit the program.

## The sandbox

Available: the Lua base functions (without `load`, `loadfile`, `dofile`, `require` and `collectgarbage`),
`table`, `string` (without `string.dump`), `math`, `utf8`, `coroutine`, and `os.clock`, `os.time`, `os.date`,
`os.difftime`. Not available: files (`io`), other modules (`package`), `debug`, starting programs.

Limits: 512 MB of memory; one call to `process`, `generate` or `analyse` may take 30 seconds, a tool 10 seconds.
A plugin that goes over a limit or raises an error is stopped, the audio is left unchanged, and the error
appears in the console. Lua effects are applied offline (they cannot preview while playing and are not offered in
the real-time effect rack).

## Versions

The API version only grows when something is added in a way old plugins do not notice, or when a change could
break them. A plugin states the version it needs in `apiVersion`; MXJ AUDIO runs every plugin whose
`apiVersion` is not newer than its own.
