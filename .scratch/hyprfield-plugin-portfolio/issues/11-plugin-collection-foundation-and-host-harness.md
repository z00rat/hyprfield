# 11 — Plugin Collection Foundation and Host Harness

**What to build:** Establish the shared collection foundation so independent plugin targets can be built, loaded, unloaded, and invoked through stable native Lua APIs, with a controlled host-backed test seam and exact Hyprland version-pin validation.

**Blocked by:** None — can start immediately

**Status:** resolved

- [x] Independent plugin targets build and package separately while the existing example plugin continues to work.
- [x] A controlled host-backed harness observes public Lua API behavior, lifecycle, and notifications without contacting a user's running compositor.
- [x] Exact Hyprland version pins are validated and pin changes require an explicit compatibility check.

## Comments

- Implemented the shared CMake plugin-target helper and migrated the `hello` example to it.
- Added an offline CTest host harness that loads the built plugin, invokes its native Lua API, records notifications, and exercises unload cleanup.
- CMake validates the exact Hyprland version from `hyprpm.toml`; `just compatibility-check` provides the explicit verification command.
