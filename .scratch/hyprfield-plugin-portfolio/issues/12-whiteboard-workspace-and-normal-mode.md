# 12 — Whiteboard Workspace and Normal Mode

**What to build:** Let users assign a Whiteboard workspace to a monitor and use a monitor-scoped canvas containing real application windows, while ordinary workspaces remain unchanged and 1x interaction stays normal. Persist camera and placement state across reloads when stable identities are available.

**Blocked by:** 11 — Plugin Collection Foundation and Host Harness

**Status:** in-progress

- [ ] A Whiteboard workspace can be assigned to one monitor without converting ordinary Hyprland workspaces.
- [ ] Real application windows appear once on the canvas and receive ordinary keyboard and pointer interaction at 1x.
- [ ] Configurable grid and floating placement, including initial 2-by-4 defaults, are restored with camera state after reload.
- [ ] Native Lua actions and observable lifecycle tests cover creation, assignment, focus, persistence, and cleanup.
