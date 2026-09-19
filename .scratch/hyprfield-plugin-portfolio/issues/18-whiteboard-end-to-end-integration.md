# 18 — Whiteboard End-to-End Integration

**What to build:** Integrate the completed Whiteboard slices into one usable workflow and verify the compositor-visible contract across monitors, clients, popups, persistence, and failure paths.

**Blocked by:** 14 — Whiteboard Workspace and Normal Mode; 15 — Whiteboard Window Placement; 16 — Whiteboard Camera Rendering; 17 — Whiteboard Management Input

**Status:** ready-for-agent

## Acceptance criteria

- [ ] A fresh board can be assigned, populated, navigated in management mode, and returned to 1x normal interaction using only the supported public API and documented live walkthrough.
- [ ] Tiled and floating clients, multiple monitors, client close/reopen, monitor loss/reconnect, and application popups preserve the one-client/one-workspace model.
- [ ] Persisted placement and camera state restores only for stable identities; invalid persisted records are ignored and reported without preventing startup.
- [ ] Rendering damage and input ownership are limited to the assigned monitor and board; ordinary workspaces remain unaffected.
- [ ] Unsupported renderer, input, direct-scanout, or host-version conditions fail closed to an ordinary usable workspace and do not leave hooks, transforms, or stale state after unload.
- [ ] Integration tests cover the observable compositor outcomes rather than private helper functions or exact hook call sequences.

## Comments

- Partial progress: the manual walkthrough now exercises exactly ten deterministic windows, zoom levels, 1x panning in four directions, native geometry snapshots, and restoration.
- The live renderer/canvas path is confirmed, including surrounding windows and complete window-pass compositing. Full integration remains pending for management input, popup behavior, multiple monitors, and failure-path coverage.
- Next implementation should extend the existing public-API harness and manual workflow rather than introduce a second rendering or placement implementation.

## Scope boundary

This is an integration and acceptance ticket, not a place to add a second implementation of placement, rendering, or input. Any failing slice is fixed in its owning issue first.
