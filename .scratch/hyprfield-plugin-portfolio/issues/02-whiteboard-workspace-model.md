# Whiteboard Workspace Model

Label: wayfinder:grilling
Type: grilling
Status: resolved
Parent: ../map.md
Blocked by: 01

## Question

What is the canonical ownership and navigation model for the whiteboard: whether each whiteboard workspace owns a 2D canvas displayed by one monitor, how ordinary workspaces coexist with it, whether windows can move between canvases/monitors, how positions survive reloads and window lifecycle changes, and what “infinite in theory” means for the window canvas?

## Answer

Each whiteboard workspace owns one 2D canvas and is displayed by the monitor to which that workspace is assigned. Every monitor has independent camera state; ordinary Hyprland workspaces remain available and are not converted into whiteboards. A real application window continues to belong to exactly one Hyprland workspace and monitor; the design does not duplicate a live window across monitors.

Normal mode is the 1x application mode: keyboard and pointer input follow the focused real window. Entering management mode starts at a bounded zoom-out level, such as 0.5x. Management mode owns navigation and window/system actions, permits further zoom-out and panning, and returns to normal mode when zoom is restored to 1x. Selecting another window focuses the real window, which is then interacted with at 1x.

The canvas is logically unbounded but uses integer cell/world coordinates and bounded practical zoom levels. Camera state and window placement metadata are plugin-owned persistent state, separate from Hyprland's persistent workspace mechanism. Cross-monitor movement is an explicit management action, not simultaneous shared viewing.

The no-fork constraint is firm. The whiteboard may use private Hyprland renderer and input hooks, with an explicit running-version/header compatibility check and rebuild per Hyprland release. This is an accepted ABI trade-off for keeping real windows renderable at 1x in normal mode and render-scaled only during management mode. The zoom transform, inverse pointer mapping, popup/decorations behavior, damage handling, and direct-scanout behavior remain unresolved in `Whiteboard Zoom And Overview`.

See the source-backed capability analysis in [`Whiteboard Geometry Capabilities`](../issues/01-whiteboard-geometry-capabilities.md) and the current zoom decision in [`Whiteboard Zoom And Overview`](03-whiteboard-zoom-and-overview.md).
