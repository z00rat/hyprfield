# Whiteboard Zoom And Overview

Label: wayfinder:grilling
Type: grilling
Status: resolved
Parent: ../map.md
Blocked by: 01, 02

## Question

What should the whiteboard's zoomed-out experience be, given the no-fork constraint: actual client resizing, live render transformations, snapshots, thumbnails/cards, or a staged blend? Decide the interaction available at each zoom level, the acceptable visual latency, and how camera movement can remain smooth without fragile coordinate or floating-point behavior.

## Answer

Use real compositor-rendered application windows as the whiteboard objects. A
version-locked plugin may use private Hyprland renderer and input hooks to apply
the camera transform to those windows; no QuickShell thumbnail canvas is part of
the primary design. Application Wayland geometry remains authoritative, so
resizing a window still uses normal Hyprland/client configure behavior.

At 1.0x, the whiteboard is in normal mode: the focused application receives
ordinary interaction. Camera panning at 1.0x requires an explicit whiteboard
gesture or keybind so normal application input is not ambiguous. Crossing below
1.0x snaps through a 0.9x threshold into management mode. Management mode may
zoom and pan smoothly at bounded levels, but owns whiteboard interaction rather
than direct miniature application input. Returning to 1.0x, or double-clicking a
window, returns to normal mode.

Each monitor has one whiteboard workspace and multiple windows. The canvas has
two layers: an exclusive lower grid layer and an upper floating layer. The
initial grid is 2 rows by 4 columns, with rows, columns, gaps, and monitor
margins configurable per monitor. A grid slot holds at most one window; a
window may span adjacent rectangular slots if all are free. Moving onto an
occupied slot swaps the grid windows. A span resize is rejected if it would
claim occupied slots. Windows become floating only through an explicit layer
toggle, and floating windows may overlap grid or other floating windows.

Opening placement is configurable. Grid placement, grid span, floating-layer
membership, and floating coordinates persist per window where a stable identity
is available. Application popups remain attached to their parent for Wayland
correctness, render above other content like floating surfaces, and are not
independent persistent whiteboard items.

The plugin must fail closed when its private hook compatibility range is not
supported, leaving the workspace usable as an ordinary Hyprland workspace.
QuickShell/helper thumbnails remain a possible degraded or future design, not
the whiteboard's intended interaction model.
