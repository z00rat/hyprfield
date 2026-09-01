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

Load and exercise HyprDimension manually with:

```sh
# Build the plugin and its offline tests.
just build

# Find a real window address and print all candidates.
WINDOW_ADDRESS=$(hyprctl clients -j | jq -r '.[0].address')
hyprctl clients -j | jq -r '.[] | [.address, .monitor, .class, .title] | @tsv'

# Load HyprDimension into the running compositor.
hyprctl plugin load "$PWD/build/hyprdimension/hyprdimension.so"

# Assign workspace 9 to monitor DP-1.
hyprctl dispatch 'function() hl.plugin.hyprdimension.assign("DP-1 9") end'

# Configure the default 2-by-4 grid, with 16px gaps and 32px margins.
hyprctl dispatch 'function() hl.plugin.hyprdimension.configure("DP-1 2 4 16 32") end'

# Add the real window to the canvas using its Hyprland address.
hyprctl dispatch "function() hl.plugin.hyprdimension.open(\"DP-1 $WINDOW_ADDRESS\") end"

# Enter management mode through the 0.9x threshold.
hyprctl dispatch 'function() hl.plugin.hyprdimension.zoom("DP-1 0.8") end'

# Move the monitor-scoped camera.
hyprctl dispatch 'function() hl.plugin.hyprdimension.camera("DP-1 100 -40") end'

# Confirm the plugin is loaded, then unload it cleanly.
hyprctl plugin list
hyprctl plugin unload "$PWD/build/hyprdimension/hyprdimension.so"
```

`WINDOW_ADDRESS` contains the first window returned by `hyprctl clients -j`. Choose another address from the printed table if needed. The current plugin validates and stores that identity; live transformed rendering and compositor focus integration are still separate implementation work.

The offline scenario in `tests/hyprdimension_test.cpp` follows the same lifecycle without contacting Hyprland. It covers two-monitor assignment, duplicate workspace rejection, invalid grid configuration, placeholder-ID rejection, placement, occupied-slot swapping, floating-layer toggling, management zoom, focus, camera movement, popup attachment, unload, and persistence restoration.

See [plugin development](docs/plugin-development.md) for building this repository or adding plugins.
