# 15 — Whiteboard Window Placement

**What to build:** Place real clients on an active board using the lower exclusive grid layer and the upper explicit floating layer, while retaining Hyprland's real client ownership and 1x interaction.

**Blocked by:** 14 — Whiteboard Workspace and Normal Mode

**Status:** resolved

## Acceptance criteria

- [x] Opening a live client on an active board records it once, moves it to the board workspace, and rejects invalid, stale, duplicate, or inactive-board identities.
- [x] The default configurable grid is 2-by-4; row, column, gap, margin, and opening-layer settings validate before changing state.
- [x] Grid placement has deterministic monitor-relative geometry, rejects out-of-bounds and occupied rectangular claims, and never displaces a different client implicitly.
- [x] Single-slot moves onto an occupied single-slot claim swap the two grid records atomically; spans require every claimed slot to be free.
- [x] An explicit layer action moves a client between grid and floating records; floating clients may overlap grid and other floating clients.
- [x] Closing a client, unloading the plugin, or losing the board removes its record so the same later live identity can be opened again.
- [x] Host-backed tests observe placement state, dispatch results, notifications, focusability, and cleanup.

## Comments

- Implemented validated grid and floating placement through the native Lua API, including deterministic monitor-relative geometry, rectangular spans, single-slot swaps, client workspace movement, focus, persistence, and geometry dispatch.
- Added pinned Hyprland event-bus cleanup for window close, monitor removal, and workspace removal, with offline host-harness event simulation.
- Placement and configuration failures restore plugin records and prior geometry where compositor dispatch can fail; swaps compensate for partial dispatch failure.
- Placement is intentionally runtime-only; do not reintroduce plugin-owned persistence for transient Hyprland client identities.
- Verified with `just format`, `just tidy`, a successful build, and the full CTest suite.
