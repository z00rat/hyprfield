# ADR-0001: Native Lua Plugin Integration

## Status

Accepted

## Context

Hyprland's native Lua configuration changes how `hyprctl dispatch` is interpreted. In Lua mode, a command such as `hyprctl dispatch hello:hello Zurat` is translated into a Lua expression and is not a legacy dispatcher invocation. Plugin dispatchers therefore cannot be treated as the Lua configuration interface.

## Decision

Hyprfield plugins expose user-facing actions through plugin-owned Lua functions under `hl.plugin.<namespace>.<name>`. The `hello` example uses `hl.plugin.hello.say("Zurat")` from Lua keybinds and Lua dispatch expressions.

Legacy `.conf` configuration support is intentionally not maintained by the example plugin.

## Consequence

Native Lua users call plugin functions directly, for example:

```lua
hl.bind("SUPER + H", function()
    hl.plugin.hello.say("Zurat")
end)
```

From a shell, pass a Lua function to `hyprctl dispatch`:

```sh
hyprctl dispatch 'function() hl.plugin.hello.say("Zurat") end'
```
