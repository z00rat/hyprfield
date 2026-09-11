# 15 — Whiteboard Window Placement

**What to build:** Place real clients on an active board using the lower exclusive grid layer and the upper explicit floating layer, while retaining Hyprland's real client ownership and 1x interaction.

**Blocked by:** 14 — Whiteboard Workspace and Normal Mode

**Status:** ready-for-agent

## Acceptance criteria

- [ ] Opening a live client on an active board records it once, moves it to the board workspace, and rejects invalid, stale, duplicate, or inactive-board identities.
- [ ] The default configurable grid is 2-by-4; row, column, gap, margin, and opening-layer settings validate before changing state.
- [ ] Grid placement has deterministic monitor-relative geometry, rejects out-of-bounds and occupied rectangular claims, and never displaces a different client implicitly.
- [ ] Single-slot moves onto an occupied single-slot claim swap the two grid records atomically; spans require every claimed slot to be free.
- [ ] An explicit layer action moves a client between grid and floating records; floating clients may overlap grid and other floating clients.
- [ ] Closing a client, unloading the plugin, or losing the board removes its record so the same later live identity can be opened again.
- [ ] Host-backed tests observe placement state, dispatch results, notifications, focusability, and cleanup.
