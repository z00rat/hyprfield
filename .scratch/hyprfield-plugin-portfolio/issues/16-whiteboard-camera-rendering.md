# 16 — Whiteboard Camera Rendering

**What to build:** Apply a bounded camera transform to the active board's compositor-rendered clients only while below the management threshold, and restore the exact ordinary 1x presentation when management ends.

**Blocked by:** 12 — Whiteboard Compatibility Gate; 15 — Whiteboard Window Placement

**Status:** ready-for-agent

## Acceptance criteria

- [partial] Zoom values are validated and clamped; camera rendering works at 1x and below 1x, but threshold/mode orchestration is still pending.
- [x] The canvas and placed clients visibly scale and pan together using monitor-local camera coordinates.
- [x] Camera movement is bounded, stable, and persisted without changing client ownership or authoritative Wayland geometry.
- [partial] Returning to 1x removes the render transform and restores native geometry; input/focus/popup behavior still belongs to issues 17/18.
- [x] Render damage is requested for the assigned monitor only.
- [partial] Renderer compatibility is pinned and failures fall back to ordinary rendering; failure reporting for optional off-monitor capture remains to be tightened.
- [partial] Offline lifecycle tests and the manual transformed/restored walkthrough exist; complete integration coverage remains pending.

## Comments

- The renderer slice is implemented and live-confirmed: complete workspace passes are captured into a signed, padded offscreen canvas so borders, shadows, blur, decorations, surfaces, and subsurfaces transform together; 1x panning and zoomed views preserve native client geometry.
- The capture path includes off-monitor workspace windows and uses monitor-relative pixel damage coordinates, including fractional-scale outputs, to avoid clipping.
- This issue remains open because management-mode threshold/orchestration and pointer activation/focus behavior are not implemented; those remain in issues 17 and 18.
- Current handoff: do not replace the signed offscreen-pass architecture. Continue from the existing `renderWorkspaceWindows` and `visibleOnMonitor` seams, and preserve the monitor-relative pixel damage conversion and canvas padding fixes.
