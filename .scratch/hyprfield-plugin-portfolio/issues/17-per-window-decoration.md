# 17 — Per-Window Decoration

**What to build:** Let users apply deterministic style-only decorations to tiled and floating application windows, with configurable title-bar and shadow presentation, per-window disablement and overrides, while Hyprland retains responsibility for input behavior.

**Blocked by:** 11 — Plugin Collection Foundation and Host Harness

**Status:** ready-for-agent

- [ ] Decorations apply to tiled and floating windows by default and can be disabled or overridden per window through supported configuration and window rules.
- [ ] Title-bar side, title text, optional compositor-safe buttons, and independent shadow sides, thickness, offset, and color are supported.
- [ ] Geometry remains deterministic through movement and tiled/floating transitions.
- [ ] Dragging, resizing, and other input retain existing Hyprland behavior; deferred unsupported animation or rounding is not implied.
