# Hyprfield

Hyprfield is a collection of independent plugins for the Hyprland compositor. Each plugin extends a running Hyprland host and can be loaded or managed separately.

## Language

**Hyprfield**:
The repository context containing multiple independent Hyprland plugins.
_Avoid_: Monolithic plugin, Hyprland itself

**Plugin**:
A separately loadable extension that adds behavior to a running Hyprland compositor through the Hyprland plugin API.
_Avoid_: Module, feature, patch

**Plugin collection**:
The set of independent plugins maintained in Hyprfield; plugins share repository tooling but are loaded and used individually.
_Avoid_: Super plugin, plugin bundle

**Per-window decoration**:
A visual decoration attached to an individual application window, including its title bar and shadow geometry, without global-panel behavior.
_Avoid_: Global panel, window manager chrome

**Host**:
The running Hyprland compositor process that loads, initializes, and unloads plugins.
_Avoid_: Runtime, platform, operating system

**Plugin lifecycle**:
The stages of a plugin being loaded, initialized, used by the host, and unloaded.
_Avoid_: Build lifecycle, application lifecycle

**Native Lua configuration**:
Hyprland's Lua-based configuration interface, which is the supported configuration interface for Hyprfield plugins.
_Avoid_: Lua mode, Lua config file, legacy configuration

**Lua-facing API**:
The plugin functionality exposed for use from native Lua configuration, such as a plugin function invoked by a keybind.
_Avoid_: Lua binding, command alias, dispatcher

**Layered submap**:
A named set of key bindings active as a group, where bindings may explicitly transition to another named layer.
_Avoid_: Key state machine, raw input layer

**Layered submap declaration**:
The native Lua configuration describing layered submaps, their bindings, transitions, and user-defined reset behavior.
_Avoid_: Submap implementation, key-processing engine

**Layer**:
A named state in a layered submap declaration whose bindings are installed using Hyprland's existing submap and dispatch primitives.
_Avoid_: Mode, context

**Root layer**:
The top-level layer entered by the layered submap's entry binding, such as `super`.
_Avoid_: Master layer

**Child layer**:
A layer nested under another layer and entered through its declared layer key.
_Avoid_: Sub-layer

**Layer key**:
The existing Hyprland key and modifier binding used to enter a child layer and, while that child is active, return to its parent.
_Avoid_: Layer transition key

**Example plugin**:
A deliberately minimal plugin used to verify that the repository's plugin loading and Lua integration work.
_Avoid_: Production plugin, reference implementation

**Whiteboard workspace**:
A monitor-assigned workspace whose windows are arranged and navigated on a 2D canvas.
_Avoid_: Virtual desktop, overview screen

**Whiteboard canvas**:
The 2D space containing the windows and camera position of a whiteboard workspace.
_Avoid_: Wallpaper, desktop surface

**Normal mode**:
The whiteboard state at 1x where the focused application receives ordinary interaction.
_Avoid_: Focus mode, application mode

**Management mode**:
The whiteboard state where the canvas camera and window arrangement are manipulated.
_Avoid_: Overview mode, thumbnail mode

**Whiteboard window**:
A real Hyprland application window represented as an item on a whiteboard canvas.
_Avoid_: Window card, embedded surface

**Grid-placed window**:
A whiteboard window positioned according to the canvas grid and its available monitor space.
_Avoid_: Tiled window, layout-managed window

**Whiteboard floating window**:
A whiteboard window positioned freely above grid-placed windows.
_Avoid_: Floating layout, popup

**Management threshold**:
The zoom boundary below 1x at which a whiteboard enters management mode; the initial threshold is 0.9x.
_Avoid_: Overview threshold, thumbnail threshold

**Grid slot**:
An exclusive region in the lower whiteboard layer that can contain at most one grid-placed window.
_Avoid_: Tile, pane

**Floating layer**:
The upper whiteboard layer containing free-form windows that may overlap grid slots and one another.
_Avoid_: Overlay, popup layer

**Procedural wallpaper**:
A monitor-scoped background generated from a deterministic ruleset and optionally influenced by whiteboard camera or hover state.
_Avoid_: Desktop shader, wallpaper overlay

**Monitor brightness control**:
A per-monitor scalar adjustment exposed by a Hyprland plugin's native Lua-facing API.
_Avoid_: Full-desktop shader, general color-management system

**Wallpaper chunk**:
A coordinate-addressed finite region of procedural wallpaper that can be generated, cached, and presented independently.
_Avoid_: Wallpaper tile
