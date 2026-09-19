# 16 — Whiteboard Camera Rendering

**What to build:** Apply a bounded camera transform to the active board's compositor-rendered clients only while below the management threshold, and restore the exact ordinary 1x presentation when management ends.

**Blocked by:** 12 — Whiteboard Compatibility Gate; 15 — Whiteboard Window Placement

**Status:** resolved

## Acceptance criteria

- [x] Zoom values are validated and clamped; below 0.9x is reported as management mode and 1x is normal mode.
- [x] The canvas and placed clients visibly scale and pan together using monitor-local camera coordinates.
- [x] Camera movement is bounded, stable, and persisted without changing client ownership or authoritative Wayland geometry.
- [x] Returning to 1x removes the render transform and restores native geometry; ordinary input remains delegated and popups stay in the workspace pass.
- [x] Render damage is requested for the assigned monitor only.
- [x] Renderer compatibility is pinned and missing renderer/visibility seams fail closed with a compatibility notification.
- [x] Offline host-harness tests and the manual transformed/restored walkthrough cover the camera contract.

## Comments

- The renderer slice is implemented and live-confirmed: complete workspace passes are captured into a signed, padded offscreen canvas so borders, shadows, blur, decorations, surfaces, and subsurfaces transform together; 1x panning and zoomed views preserve native client geometry.
- The capture path includes off-monitor workspace windows and uses monitor-relative pixel damage coordinates, including fractional-scale outputs, to avoid clipping.
- Management gesture ownership and pointer activation remain separate concerns tracked by issues 17 and 18.
- Current handoff: do not replace the signed offscreen-pass architecture. Continue from the existing `renderWorkspaceWindows` and `visibleOnMonitor` seams, and preserve the monitor-relative pixel damage conversion and canvas padding fixes.

## Answer

Issue 16 is complete: bounded camera rendering, 0.9x management-mode reporting, signed offscreen composition, complete window-pass transforms, native-geometry preservation, monitor-relative damage, and fail-closed renderer compatibility are implemented and verified by the offline harness plus the live ten-window walkthrough.
