# 17 — Whiteboard Management Input

**What to build:** Add the management-mode input seam for camera navigation and client activation, while keeping 1x input owned by Hyprland and the focused application.

**Blocked by:** 12 — Whiteboard Compatibility Gate; 16 — Whiteboard Camera Rendering

**Status:** ready-for-agent

## Acceptance criteria

- [ ] Pointer pan and zoom gestures are recognized only for an active board below the management threshold and affect only that board's camera.
- [ ] Selecting or activating a placed client exits management mode, restores 1x, and focuses the corresponding real Hyprland client.
- [ ] Keyboard and pointer input at 1x is forwarded through ordinary Hyprland behavior; the plugin does not consume application input.
- [ ] Input coordinates are inverse-transformed consistently with the camera when selecting a managed client.
- [ ] Unsupported or failed input-hook setup leaves input with Hyprland and the board usable as an ordinary workspace.
- [ ] Tests cover management gestures, activation, 1x pass-through, hook failure, and teardown without relying on private helper implementation details.

## Comments

- Partial progress only: the `CInputManager::onMouseMoved` hook and 1x pass-through seam exist, but the hook currently delegates without implementing pan/zoom gestures, inverse hit testing, or client activation.
- Next implementation should add management-mode gesture ownership and activation on top of the existing camera API; do not replace the renderer-pass implementation in issue 16.
