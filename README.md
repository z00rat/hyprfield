# hyprfield

A collection of Hyprland plugins built with CMake and C++23.

## Build

Install `cmake`, `just`, `ccache`, `clang-format`, and `clang-tidy`. Hyprland must also be installed with its development files so that `pkg-config` can find `hyprland`.

```sh
just build
```

If `ccache` is installed, CMake uses it automatically. The active cache can be inspected with `ccache --show-stats`.

Each plugin has its own directory and CMake target. Add new plugins by creating a directory with a `CMakeLists.txt`, adding it with `add_subdirectory(...)` in the root `CMakeLists.txt`, and documenting it here.

Run `just` to list all available recipes:

```sh
just
```

## Code quality

The repository uses the `.clang-format` and `.clang-tidy` configurations based on Kribu's setup.

```sh
just format        # Format tracked C++ files
just format-check  # Check formatting without changing files
just tidy          # Run Clang-Tidy using the CMake compile database
```

`just tidy` configures the project first so the compile database is current. It does not start Hyprland or load any plugin.

## Test the example

Load the built plugin in a running Hyprland session:

```sh
just load-hello
```

The `hello` plugin displays a notification when it loads. Unload it with:

```sh
just unload-hello
```

The plugin exposes `hl.plugin.hello.say(...)` for native Lua configuration. Add this keybind:

```lua
hl.bind("SUPER + H", function()
    hl.plugin.hello.say("Zurat")
end, {
    description = "Say hello to Zurat",
})
```

Pressing the key shows `Hello, Zurat!`. The function uses `world` when no name is supplied.

From the command line, use native Lua dispatch syntax:

```sh
hyprctl dispatch 'function() hl.plugin.hello.say("Zurat") end'
```

The same plugin can be built through `hyprpm` using the included `hyprpm.toml`.
