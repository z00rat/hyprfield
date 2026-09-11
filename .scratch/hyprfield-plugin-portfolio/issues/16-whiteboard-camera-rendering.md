# 16 — Whiteboard Camera Rendering

**What to build:** Apply a bounded camera transform to the active board's compositor-rendered clients only while below the management threshold, and restore the exact ordinary 1x presentation when management ends.

**Blocked by:** 12 — Whiteboard Compatibility Gate; 15 — Whiteboard Window Placement

**Status:** ready-for-agent

## Acceptance criteria

- [ ] Zoom values are validated and clamped to bounded practical limits; crossing below 0.9x enters management at a bounded entry zoom, and 1x is normal mode.
- [ ] In management mode, the canvas and placed clients visibly scale and pan together using monitor-local camera coordinates.
- [ ] Camera movement is bounded, stable, and persisted during the session without changing client ownership or authoritative Wayland geometry.
- [ ] Returning to 1x removes the transform and restores ordinary client geometry, focus, keyboard, pointer, and popup behavior.
- [ ] Render damage is requested only for affected assigned monitors; ordinary workspaces and other monitors receive no transform or stale damage.
- [ ] Unsupported or failed renderer integration leaves the board in ordinary Hyprland presentation and reports failure.
- [ ] Offline seam tests and an explicit live walkthrough prove both transformed and restored states.
