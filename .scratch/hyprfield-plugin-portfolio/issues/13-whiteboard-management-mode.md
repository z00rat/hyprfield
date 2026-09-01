# 13 — Whiteboard Management Mode

**What to build:** Let users manage a Whiteboard canvas below the management threshold, including bounded camera movement, window arrangement, layer changes, and activation of real application windows, while preserving popup behavior and safely degrading on unsupported hosts.

**Blocked by:** 12 — Whiteboard Workspace and Normal Mode

**Status:** ready-for-agent

- [ ] Crossing below 0.9x enters management mode at a bounded zoom level, with bounded smooth panning and zooming.
- [ ] Grid spans reject occupied claims, occupied-slot moves swap grid-placed windows, and explicit toggles move windows to or from the floating layer.
- [ ] Selecting or activating a window returns to normal mode and focuses the corresponding real window; popups remain attached to their parent.
- [ ] Private-hook compatibility, rendering/input integration, damage, and unsupported-host behavior are tested at observable compositor seams, failing closed to an ordinary workspace.
