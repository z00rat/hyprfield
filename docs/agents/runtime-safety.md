# Runtime Safety

The repository can build and inspect plugin artifacts without contacting the running compositor. During automated work, prefer offline checks such as CMake configuration, compilation, formatting, Tidy, symbol inspection, and diff checks.

Compositor IPC commands such as `hyprctl reload`, `hyprctl dispatch`, and plugin load/unload commands require explicit user execution when testing behavior in a live Hyprland session.
