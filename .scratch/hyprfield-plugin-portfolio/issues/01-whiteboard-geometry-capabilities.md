# Whiteboard Geometry Capabilities

Label: wayfinder:research
Type: research
Status: resolved
Parent: ../map.md
Blocked by: none

## Question

What can the current Hyprland/plugin model reliably support for an independent per-monitor whiteboard workspace: 2D window placement, viewport/camera movement, zoomed-out live content, snapshots or thumbnails, floating windows, popups, focus, input hit testing, and persistence? Identify the strongest plugin-only boundary, the cases that require QuickShell or a helper, and the cases that would require a Hyprland fork.

## Answer

Use the plugin for native Lua actions, configuration, and orchestration of
existing Hyprland windows, but use one QuickShell/helper layer surface per
monitor for the actual whiteboard model, 2D placement, camera/zoom, client-side
hit testing, popups, persistence, and screencopy/image-copy thumbnails. This is
reliable without a fork when application windows are represented as live or
snapshot cards and focus is requested for the real window. A Hyprland fork is
only required for compositor-native infinite/zoomable canvases containing real
app surfaces with transformed rendering, popup constraints, focus, and pointer
hit testing. See `research/whiteboard-geometry-capabilities.md`.
