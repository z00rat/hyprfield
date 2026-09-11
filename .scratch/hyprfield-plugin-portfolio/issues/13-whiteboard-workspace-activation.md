# 13 — Whiteboard Host Activation

**What to build:** Connect the Whiteboard workspace model to a live Hyprland host through a checked activation path. This is the host-facing slice of issue 12; it establishes assignment and ordinary-workspace preservation before client placement is attempted.

**Blocked by:** 12 — Whiteboard Compatibility Gate

**Status:** ready-for-agent

## Acceptance criteria

- [ ] Assigning a valid workspace to a monitor performs and verifies the required Hyprland workspace action, then exposes the board as active only on that monitor.
- [ ] A workspace already assigned elsewhere is rejected with the previous assignment unchanged.
- [ ] Unassigned monitors and ordinary workspaces retain their native behavior.
- [ ] Missing monitors, invalid workspaces, and failed compositor commands produce failure notifications and no partial board activation.
- [ ] The host harness observes command results and lifecycle state; the live walkthrough exits nonzero or reports failure when a compositor command fails.

## Scope boundary

Client placement, camera transforms, and management input belong to issues 15–17. This issue does not claim a visible canvas implementation beyond the compatibility proof in issue 12.
