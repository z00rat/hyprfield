# 18 — Portfolio Independence and Release Verification

**What to build:** Verify the complete plugin collection as a release-ready set, proving that each plugin loads and unloads independently, remains useful without optional companions, enforces its compatibility policy, and passes repository quality checks.

**Blocked by:** 12 — Whiteboard Workspace and Normal Mode; 13 — Whiteboard Management Mode; 14 — Procedural Wallpaper; 15 — Layered Submap; 16 — Monitor Brightness Control; 17 — Per-Window Decoration

**Status:** ready-for-agent

- [ ] Each plugin can be loaded, exercised through its public Lua API, and unloaded without mandatory companion-plugin initialization.
- [ ] Wallpaper operates without Whiteboard, and optional Whiteboard-to-Wallpaper state cannot become a load-time dependency.
- [ ] Exact version-pin validation covers every plugin, including Whiteboard private-hook compatibility and fail-closed behavior.
- [ ] Portfolio integration tests and documented formatting, tidy, build, and quality checks pass without contacting a user's running compositor.
