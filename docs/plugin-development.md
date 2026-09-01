# Plugin Development

## Prerequisites

- Hyprland development files available through `pkg-config` as `hyprland`
- CMake
- C++23 compiler
- `just`
- `ccache` (optional, detected automatically)
- `clang-format` and `clang-tidy`
- `trash-cli` for `just clean`

## Build

```sh
just build    # Debug build
just release  # Optimized build for the current machine
```

Both configurations use `build/`. Release builds enable `-O3`, `-march=native`, and `-mtune=native`. Disable machine-specific tuning for portable artifacts:

```sh
cmake -S . -B build -DHYPRFIELD_NATIVE_OPTIMIZATIONS=OFF
```

CMake uses all available logical CPUs through uncapped `cmake --build --parallel`. If installed, `ccache` is used automatically.

## Code Quality

```sh
just format        # Format all non-ignored C++ files
just format-check  # Check C++ formatting
just tidy          # Run Clang-Tidy using the build compile database
just clean         # Move build/ to the desktop trash
```

## Compatibility

The collection targets the exact Hyprland version in `hyprpm.toml`. CMake rejects
any other installed version. Run `just compatibility-check` after changing the
pin; do not publish a plugin until that check passes.

The host harness is an offline CTest seam for lifecycle, Lua-facing API, and
notification assertions. Run it with `ctest --test-dir build --output-on-failure`.
It never contacts a running compositor.

## Add A Plugin

Use lowercase kebab-case for the plugin name and keep the same name for its directory, CMake target, and shared library:

```text
my-plugin/
├── CMakeLists.txt
└── main.cpp
```

1. Create `my-plugin/CMakeLists.txt` by copying `hello/CMakeLists.txt`.
2. Replace every `hello` target and output name with `my-plugin`.
3. Add `add_subdirectory(my-plugin)` to the root `CMakeLists.txt`.
4. Keep the output at `build/my-plugin/my-plugin.so`; this makes `just load my-plugin`, `just unload my-plugin`, and `just reload my-plugin` work automatically.
5. Use a Lua-safe namespace such as `my_plugin` for APIs exposed under `hl.plugin.my_plugin.*`.
6. Add the plugin to `hyprpm.toml` if it should be installable through `hyprpm`.
7. Add user-facing instructions to `README.md` when the plugin is ready for users.

The minimal CMake target shape is:

```cmake
project(my-plugin VERSION 0.1.0 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)

add_library(my-plugin SHARED main.cpp)
target_compile_features(my-plugin PRIVATE cxx_std_23)
target_link_libraries(my-plugin PRIVATE PkgConfig::HYPRLAND)

set_target_properties(my-plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "my-plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/my-plugin"
)
```

After adding source files, run:

```sh
just format
just tidy
just build
```
