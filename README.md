# hyprfield

A collection of independent plugins for Hyprland.

## Plugins

### hello

`hello` shows a notification when loaded and exposes a native Lua function for greeting someone.

```lua
hl.bind("SUPER + H", function()
    hl.plugin.hello.say("Zurat")
end, {
    description = "Say hello to Zurat",
})
```

The name is optional. Without one, the plugin greets `world`:

```lua
hl.plugin.hello.say()
```

## Install With Hyprpm

Add the repository and enable the plugin:

```sh
hyprpm add https://github.com/z00rat/hyprfield
hyprpm enable hello
hyprpm enable hyprdimension
```

After enabling it, reload your Hyprland configuration so the Lua API is available.

### HyprDimension

HyprDimension exposes monitor-scoped workspace and canvas actions through native Lua:

```lua
hl.plugin.hyprdimension.assign("DP-1 9")
hl.plugin.hyprdimension.configure("DP-1 2 4 16 32")
hl.plugin.hyprdimension.open("DP-1 0x123456")
hl.plugin.hyprdimension.zoom("DP-1 0.8")
hl.plugin.hyprdimension.camera("DP-1 100 -40")
```

## Manual Testing

For a locally built plugin, load it with:

```sh
hyprctl plugin load "$PWD/build/hello/hello.so"
```

List or unload plugins with:

```sh
hyprctl plugin list
hyprctl plugin unload "$PWD/build/hello/hello.so"
```

Native Lua dispatch can also invoke the function directly:

```sh
hyprctl dispatch 'function() hl.plugin.hello.say("Zurat") end'
```

Run this from the repository directory. It uses a temporary fish function so a missing monitor/window cannot close the terminal:

```fish
function run_hyprdimension_test; just build; set PLUGIN "$PWD/build/hyprdimension/hyprdimension.so"; hyprctl plugin unload "$PLUGIN" 2>/dev/null; or true; set STATE_HOME "$XDG_STATE_HOME"; test -n "$STATE_HOME"; or set STATE_HOME "$HOME/.local/state"; rm -f "$STATE_HOME/hyprfield/hyprdimension.state"; set MONITOR DP-1; set WINDOW_ADDRESS (hyprctl clients -j | jq -r --arg monitor "$MONITOR" 'map(select(.monitor == $monitor))[0].address // empty'); set WORKSPACE (hyprctl clients -j | jq -r --arg address "$WINDOW_ADDRESS" '.[] | select(.address == $address) | .workspace.name'); test -n "$WINDOW_ADDRESS"; and test -n "$WORKSPACE"; or begin; echo "No window found on $MONITOR"; return 1; end; hyprctl dispatch focusmonitor "$MONITOR"; hyprctl dispatch workspace "$WORKSPACE"; hyprctl plugin load "$PLUGIN"; sleep 1; hyprctl dispatch "function() hl.plugin.hyprdimension.assign(\"$MONITOR $WORKSPACE\") end"; sleep 1; hyprctl dispatch 'function() hl.plugin.hyprdimension.configure("DP-1 2 4 16 32") end'; sleep 1; hyprctl dispatch "function() hl.plugin.hyprdimension.open(\"$MONITOR $WINDOW_ADDRESS\") end"; sleep 1; hyprctl dispatch "function() hl.plugin.hyprdimension.place(\"$MONITOR $WINDOW_ADDRESS 0 0 1 1\") end"; sleep 1; hyprctl dispatch 'function() hl.plugin.hyprdimension.zoom("DP-1 0.8") end'; sleep 2; hyprctl dispatch 'function() hl.plugin.hyprdimension.zoom("DP-1 0.65") end'; sleep 1; hyprctl dispatch 'function() hl.plugin.hyprdimension.camera("DP-1 400 200") end'; sleep 2; hyprctl dispatch 'function() hl.plugin.hyprdimension.camera("DP-1 -300 -150") end'; sleep 2; hyprctl dispatch 'function() hl.plugin.hyprdimension.camera("DP-1 -300 500") end'; sleep 2; hyprctl dispatch "function() hl.plugin.hyprdimension.floating(\"$MONITOR $WINDOW_ADDRESS\") end"; sleep 1; hyprctl dispatch "function() hl.plugin.hyprdimension.floating(\"$MONITOR $WINDOW_ADDRESS\") end"; sleep 1; hyprctl dispatch 'function() hl.plugin.hyprdimension.zoom("DP-1 1.0") end'; sleep 2; hyprctl dispatch "function() hl.plugin.hyprdimension.focus(\"$MONITOR $WINDOW_ADDRESS\") end"; sleep 2; hyprctl plugin list; sleep 1; hyprctl plugin unload "$PLUGIN"; end; run_hyprdimension_test; functions --erase run_hyprdimension_test
```

The command deliberately removes HyprDimension's saved state first; otherwise windows from an earlier run can fill the 2x4 grid. It selects a window on `DP-1` and switches to that window's current workspace before assigning and running the test. Assignment moves the workspace to the selected monitor; opening, placement, floating, and focus also dispatch to the real client. Zoom and camera currently persist state and report mode changes, but do not yet render a transformed canvas.

The offline scenario in `tests/hyprdimension_test.cpp` follows the same lifecycle without contacting Hyprland. It covers two-monitor assignment, duplicate workspace rejection, invalid grid configuration, placeholder-ID rejection, placement, occupied-slot swapping, floating-layer toggling, management zoom, focus, camera movement, popup attachment, unload, and persistence restoration.

For a single-command live walkthrough, run `fish scripts/hyprdimension-demo.fish`. It discovers the first real client, pauses between each action, pans and zooms the canvas, toggles floating mode, restores `1.0x`, focuses the client, and unloads the plugin.

See [plugin development](docs/plugin-development.md) for building this repository or adding plugins.
