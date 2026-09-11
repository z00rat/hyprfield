# 14 — Whiteboard Workspace and Normal Mode

**What to build:** Implement the Whiteboard's authoritative workspace and window model. A board belongs to one monitor and one Hyprland workspace; at 1x it leaves Hyprland's real window geometry, focus, keyboard input, pointer input, and popup behavior authoritative.

**Blocked by:** 13 — Whiteboard Host Activation

**Status:** ready-for-agent

## Acceptance criteria

- [ ] A valid native Lua API creates or assigns exactly one board for a monitor/workspace pair and rejects a workspace already owned by another monitor without changing the existing assignment.
- [ ] Ordinary workspaces and unassigned monitors continue to use ordinary Hyprland behavior.
- [ ] A real client is represented by one board record keyed by its stable Hyprland identity; missing, stale, or duplicate identities are rejected and reported.
- [ ] At 1x, the client remains a real compositor-rendered window and ordinary focus, keyboard, pointer, resize, and popup behavior is unchanged.
- [ ] Board metadata has an explicit lifecycle: creation, monitor/workspace loss, client close, plugin unload, and reload leave no stale records that block later use.
- [ ] Camera and placement metadata are persisted separately from Hyprland workspace state and restored only when monitor and client identities are valid.
- [ ] Host-backed tests invoke the public Lua API and observe assignments, rejection notifications, client identity behavior, persistence, and unload cleanup.

## Scope boundary

Grid placement is implemented in issue 15. Camera transforms are implemented in issue 16. Management input is implemented in issue 17. This issue must not claim those behaviors as complete.

## Current baseline

The existing HyprDimension implementation is a partial command/state scaffold. Its dispatches, notifications, rectangle rendering, and offline tests do not satisfy these criteria; treat this issue as unimplemented until the observable behaviors above pass.
