# Whiteboard Geometry Capabilities

## Scope and Source Snapshot

This note answers ticket 01 using the local Hyprland source and protocol files
at commit `efb50993780079460b0cbed1363e2166a2de1d9f` (`VERSION` 0.56.2), plus
the current Hyprfield example plugin. No compositor was contacted.

The example plugin only registers `hl.plugin.hello.say` and a notification; it
does not establish a rendering, scene, or input extension point
(`/home/hz/codebase/hyprfield/hello/main.cpp:22-39`).

## Findings

### What Hyprland already models

- A workspace has one monitor association, an active/visible state, a layout
  space, and a persistent flag (`/home/hz/codebase/Hyprland/src/desktop/Workspace.hpp:21-40,72-91`). Each monitor also has an active regular and special workspace
  (`/home/hz/codebase/Hyprland/src/output/Monitor.hpp:67-78`). This supports
  one compositor workspace assigned to a monitor, not independent copies of a
  virtual whiteboard with its own camera. Workspace rules can create and keep
  named workspaces assigned to a monitor, but a matching workspace is moved if
  it is on another monitor (`/home/hz/codebase/Hyprland/src/state/WorkspacePlacementController.cpp:35-107`).
- Real windows have compositor geometry, workspace/monitor ownership, floating
  state, mapped/hidden state, and popup heads
  (`/home/hz/codebase/Hyprland/src/desktop/view/Window.hpp:116-203`). The
  layout target interface can set geometry, but the layout manager's generic
  geometry setter and mover return immediately for non-floating targets
  (`/home/hz/codebase/Hyprland/src/layout/LayoutManager.cpp:62-109`). Native
  Lua exposes window coordinates, size, workspace, monitor, floating state, and
  input acceptance (`/home/hz/codebase/Hyprland/src/config/lua/objects/LuaWindow.cpp:59-118`).
- Native Lua can issue absolute or relative window moves, but those actions
  delegate to the same floating-target-only layout path
  (`/home/hz/codebase/Hyprland/src/config/lua/bindings/LuaBindingsDispatchers.cpp:635-643,824-921`; `/home/hz/codebase/Hyprland/src/config/shared/actions/ConfigActions.cpp:618-630`). It can also focus a selected existing window
  (`/home/hz/codebase/Hyprland/src/config/lua/bindings/LuaBindingsDispatchers.cpp:1115-1119`).

### Rendering and camera boundary

- The public plugin API has Lua functions, dispatchers, config values,
  notifications, window decorations, custom events, and function hooks, but no
  generic scene-node, layer-surface, framebuffer, or input-surface creation
  API (`/home/hz/codebase/Hyprland/src/plugins/PluginAPI.hpp:194-378`). The
  public decoration seam is specifically window-owned: it supplies a
  positioning reply, `draw`, damage, and optional decoration input
  (`/home/hz/codebase/Hyprland/src/render/decorations/IHyprWindowDecoration.hpp:17-59`).
  It is not a monitor-wide canvas.
- Hyprland does contain internal render-pass and texture operations, and
  internal render-stage events, but those are compositor internals rather than
  the stable plugin surface (`/home/hz/codebase/Hyprland/src/render/Renderer.hpp:135-205,255-270`; `/home/hz/codebase/Hyprland/src/event/EventBus.hpp:106-138`). The plugin API explicitly warns that C++ object ABI compatibility is not guaranteed and that function hooks are not API-stable (`/home/hz/codebase/Hyprland/src/plugins/PluginAPI.hpp:7-18,233-248`). A plugin that reaches these internals can be version-coupled, but it is not reliable support for this ticket.
- The built-in cursor zoom is output rendering, not a whiteboard camera. The
  renderer sets a zoom factor from the global cursor configuration and applies
  `CMonitorZoomController::applyZoomTransform` to the monitor box at the end
  of rendering (`/home/hz/codebase/Hyprland/src/render/Renderer.cpp:2120-2137`; `/home/hz/codebase/Hyprland/src/render/OpenGL.cpp:789-797`). The controller can keep a detached camera, but its state is private to
  the monitor zoom implementation (`/home/hz/codebase/Hyprland/src/output/MonitorZoomController.hpp:11-29`; `/home/hz/codebase/Hyprland/src/output/MonitorZoomController.cpp:28-118`). There is no plugin-facing independent whiteboard camera, and the ordinary input hit tester continues to resolve compositor-global window/layer geometry (`/home/hz/codebase/Hyprland/src/desktop/state/ViewHitTester.cpp:29-64,74-123,174-240`).

### Wayland client boundary

- A helper or QuickShell-like Wayland client can create one layer surface per
  output. Hyprland exposes layer-shell version 5 and resolves an explicitly
  requested output to a monitor (`/home/hz/codebase/Hyprland/src/managers/ProtocolManager.cpp:195-200`; `/home/hz/codebase/Hyprland/src/protocols/LayerShell.cpp:221-269`). The protocol provides size, edge/corner anchors, margins, four z-layers, and a surface input region, so a client can make a full-monitor canvas and place arbitrary cards inside its own buffer. It does not provide an infinite compositor workspace or arbitrary compositor-owned x/y placement for separate child windows (`/home/hz/codebase/Hyprland/protocols/wlr-layer-shell-unstable-v1.xml:28-37,122-204`).
- Layer surfaces can receive pointer, touch, and tablet input normally, and can
  request no, on-demand, or exclusive keyboard interaction. The protocol also
  allows an xdg popup to be assigned to the layer surface
  (`/home/hz/codebase/Hyprland/protocols/wlr-layer-shell-unstable-v1.xml:206-298`). Hyprland maps interactive layers and can focus them, while its hit tester checks layer popups, overlay/top layers, windows, and input regions in compositor z-order (`/home/hz/codebase/Hyprland/src/desktop/view/LayerSurface.cpp:162-211`; `/home/hz/codebase/Hyprland/src/managers/input/InputManager.cpp:453-489,580-631`; `/home/hz/codebase/Hyprland/src/desktop/state/ViewHitTester.cpp:257-366`). The helper therefore gets reliable surface-local events and must do the whiteboard's internal hit testing itself.
- Hyprland's layer and window popup trees are real compositor objects. Window
  rendering in `RENDER_PASS_ALL` traverses and draws popup surfaces, and popup
  hit testing uses the effective input region (`/home/hz/codebase/Hyprland/src/render/Renderer.cpp:754-820`; `/home/hz/codebase/Hyprland/src/desktop/state/ViewHitTester.cpp:257-278`; `/home/hz/codebase/Hyprland/src/desktop/view/Popup.cpp:637-685`). A helper can therefore use normal xdg popups for its own menus. It cannot make an arbitrary plugin-drawn rectangle become a compositor popup without becoming a Wayland client or relying on private internals.
- Keyboard focus is seat/surface state. Hyprland tracks focused window, surface,
  and monitor and changes keyboard focus through the seat
  (`/home/hz/codebase/Hyprland/src/desktop/state/FocusState.hpp:32-62`; `/home/hz/codebase/Hyprland/src/desktop/state/FocusState.cpp:223-271`; `/home/hz/codebase/Hyprland/src/managers/SeatManager.hpp:45-99`). A layer-surface client can focus its own canvas using layer-shell interactivity. For an actual application thumbnail, foreign-toplevel management exposes title/app/output/state data and an activation request; Hyprland implements activation by calling the window's activation path, but the protocol itself says activation is not guaranteed (`/home/hz/codebase/Hyprland/protocols/wlr-foreign-toplevel-management-unstable-v1.xml:71-109,139-145`; `/home/hz/codebase/Hyprland/src/protocols/ForeignToplevelWlr.cpp:23-33,192-216`).

### Live content, snapshots, and thumbnails

- The wlr screencopy protocol supports a one-shot full-output capture and a
  logical-coordinate output-region capture, followed by copying into a client
  buffer. `copy_with_damage` supports continuing live updates
  (`/home/hz/codebase/Hyprland/protocols/wlr-screencopy-unstable-v1.xml:41-73,83-126,184-209`). Hyprland enables that protocol and implements output/region sessions (`/home/hz/codebase/Hyprland/src/managers/ProtocolManager.cpp:228-232`; `/home/hz/codebase/Hyprland/src/protocols/Screencopy.cpp:27-40,98-160`).
- The current Hyprland source also exposes staging image-copy capture sources
  for monitors and foreign toplevel windows, and image-copy sessions render a
  window with `RENDER_PASS_ALL`; this is a direct path to current window
  thumbnails, including its popup pass, subject to screencopy permission and
  the source window being mapped (`/home/hz/codebase/Hyprland/CMakeLists.txt:602-609`; `/home/hz/codebase/Hyprland/src/protocols/ImageCaptureSource.cpp:47-53,97-121`; `/home/hz/codebase/Hyprland/src/protocols/ImageCopyCapture.cpp:15-50,341-424`; `/home/hz/codebase/Hyprland/src/managers/screenshare/ScreenshareFrame.cpp:330-333,359-390`).
- These protocols deliver client buffers. Scaling, arranging, caching, and
  camera-transforming those buffers into zoomed-out cards is client work. A
  client can retain the last copied buffer as a snapshot or request repeated
  frames for live thumbnails. A plugin-only implementation has no corresponding
  stable public capture-buffer or canvas API; the public renderer methods are
  compositor-side internals (`/home/hz/codebase/Hyprland/src/plugins/PluginAPI.hpp:127-378`; `/home/hz/codebase/Hyprland/src/render/Renderer.hpp:135-205`).

### Persistence

- Hyprland's persistent workspace mechanism preserves the existence and
  monitor assignment of config-declared workspaces. `setPersistent` keeps the
  workspace object alive; it is not a serialization mechanism for arbitrary
  window cards, camera position, thumbnail buffers, or whiteboard metadata
  (`/home/hz/codebase/Hyprland/src/desktop/Workspace.cpp:571-584`; `/home/hz/codebase/Hyprland/src/state/WorkspacePlacementController.cpp:44-106`).
- Plugin config values can be registered under `plugin:` and plugin Lua
  functions are removed when the plugin unloads (`/home/hz/codebase/Hyprland/src/plugins/PluginAPI.hpp:341-364`; `/home/hz/codebase/Hyprland/src/config/lua/ConfigManager.cpp:1147-1163,1326-1346`). The current plugin API has no write-state or restore-state facility. A whiteboard model therefore needs a helper/plugin-owned file or database, with startup restoration and monitor identity handling outside the compositor workspace persistence path. Native Lua config can provide static declarative defaults, but it is not a runtime save format.

## Capability Matrix

| Requirement | Reliable plugin-only result | QuickShell/helper result | Core-fork boundary |
| --- | --- | --- | --- |
| Independent per-monitor whiteboards | Orchestrate existing monitor/workspace state; no stable canvas surface | One layer surface and model per output | Native compositor workspaces that each own an independent virtual canvas/camera |
| 2D placement | Read geometry and move existing floating windows; tiled geometry remains layout-owned | Arbitrary card placement inside the client surface | Arbitrary placement of real compositor windows in a new world coordinate system |
| Camera movement and zoom | Not via the stable plugin API; built-in cursor zoom is the wrong output-wide mechanism | Client-side transform of its own scene and captured textures | Transforming compositor window scenes while keeping compositor geometry/input consistent |
| Zoomed-out live content | No stable plugin capture/render path | Screencopy or image-copy capture, then client-side scaling | Compositor-native transformed live sub-scenes without a client buffer boundary |
| Snapshots/thumbnails | Can store metadata, not stable capture buffers | One-shot/repeated capture and retained client buffers | Native thumbnail objects with compositor-managed lifecycle/input |
| Floating windows | Existing Hyprland floating windows and focus/move actions | Floating cards in the client surface; actual app windows remain compositor windows | App windows as first-class canvas items with world transforms |
| Popups | Existing window/deco behavior only | Normal xdg popups attached to the layer surface | Popups belonging to transformed embedded app surfaces with transformed constraints |
| Focus and hit testing | Invoke existing focus/actions; no public custom hit-test target | Layer-shell receives events; client maps them to cards and may request toplevel activation | Pointer/keyboard routing into scaled embedded windows and popup trees |
| Persistence | Config-declared persistent workspaces only | Helper/plugin-owned serialized model and restore | Compositor-owned persistence for a new canvas/workspace object |

## Resolution

The strongest reliable design is a plugin plus a QuickShell/helper client, not a
Hyprland fork: let the plugin expose native Lua actions/config and coordinate
existing windows, while one client-owned layer surface per monitor owns the
whiteboard model, 2D layout, camera, input hit testing, popups, persistence,
and captured live/snapshot thumbnails. Treat real application windows as
external compositor windows represented by thumbnails; clicking a card can
request focus and optionally move/float the real window, but the card is not an
embedded interactive copy of that window.

A fork would only be justified for the stronger product definition in which
real app surfaces themselves are compositor-native items in an infinite or
zoomable per-monitor world, with compositor-managed rendering, popup placement,
focus, and pointer hit testing after transforms. Private plugin hooks can
prototype parts of that, but they do not meet the ticket's reliability bar.
