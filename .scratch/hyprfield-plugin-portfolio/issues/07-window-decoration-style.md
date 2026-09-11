# Per-Window Decoration Style

Label: wayfinder:grilling
Type: grilling
Status: resolved
Parent: ../map.md
Blocked by: none

## Question

What should the per-window neobrutalist decoration provide: selectable bar side, configurable shadow sides and dimensions, title/buttons, tiled and floating coverage, window-rule overrides, animation/rounding behavior, and input interactions? Decide the minimum style-only contract and whether any global-panel behavior belongs elsewhere.

## Answer

The MVP is a style-only per-window decoration. It applies to tiled and floating
windows by default, while each window can disable the decoration or override its
style through configuration or window rules. The visual contract is a selectable
title-bar side (`top`, `right`, `bottom`, or `left`), title text, optional
compositor-safe buttons, and independently configurable shadow sides, thickness,
offset, and color.

The plugin delegates dragging, resizing, and other input behavior to existing
Hyprland behavior rather than owning raw input. It adds no global panel,
launcher, task list, or workspace-overview behavior and no new rule language.
Animation and rounding are deferred unless the public decoration seam later
supports them without fragile private hooks. Defaults and geometry must remain
deterministic as a window moves or changes between tiled and floating states.
