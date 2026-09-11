# Hyprfield Plugin Portfolio

Label: ready-for-agent
Status: open

## Problem Statement

Hyprfield currently contains only an `hello` example plugin, so it does not yet provide the independent compositor extensions needed for the proposed workflow. The desired capabilities span whiteboard workspace navigation, procedural wallpaper, layered submaps, per-window decoration, and monitor brightness control. The portfolio needs a decision-complete boundary that distinguishes Hyprland plugin responsibilities from QuickShell/helper responsibilities, preserves independent loading, and avoids an unsupported Hyprland fork.

Without this boundary, implementations could promise compositor-native behavior that the public plugin API cannot reliably provide, couple plugins that should remain independently useful, or silently depend on unstable compositor internals. The portfolio also needs a clear compatibility, persistence, failure, and testing policy before implementation begins.

## Solution

Build five separately loadable Hyprland plugins, each with a native Lua-facing API and an independently useful purpose:

- Whiteboard: monitor-assigned 2D workspaces containing real Hyprland application windows, with a normal 1x interaction mode and a bounded management mode using version-locked private renderer and input hooks.
- Procedural Wallpaper: deterministic, chunked simple-tiled WFC wallpaper presented through a per-monitor background surface, with bounded generation and optional whiteboard camera/hover inputs.
- Layered Submap: a declarative nested layer tree that compiles to existing Hyprland submap and bind primitives without owning raw input.
- Per-Window Decoration: deterministic, style-only decoration for tiled and floating windows, with configurable title-bar and shadow presentation.
- Monitor Brightness Control: an independent native Lua-facing per-monitor scalar brightness control with exclusive output ownership and explicit unsupported-state reporting.

Use QuickShell or helper processes only where they are the reliable surface for a capability: whiteboard support surfaces may own model, camera, persistence, hit testing, popups, and degraded thumbnails; monitor color research may inform a future helper but is not part of the first brightness deliverable. Cross-plugin integration is optional and must never be a load-time dependency. Implement Whiteboard first, Procedural Wallpaper second, then Layered Submap, Monitor Brightness Control, and Per-Window Decoration in any order.

## User Stories

1. As a Hyprland user, I want each portfolio capability to be loaded independently, so that I can adopt only the plugins I need.
2. As a Hyprland user, I want each plugin to remain useful when the other plugins are absent, so that optional integrations do not create hidden dependencies.
3. As a native Lua configuration user, I want every user-facing action exposed under a stable plugin-owned Lua namespace, so that I can invoke it from Lua keybinds and dispatch expressions.
4. As a plugin maintainer, I want each plugin to target exactly one pinned Hyprland version, so that ABI-sensitive behavior is explicit and testable.
5. As a plugin maintainer, I want compatibility validation before changing a version pin, so that a release does not silently break private-hook behavior.
6. As a whiteboard user, I want a whiteboard workspace assigned to one monitor, so that each monitor has an independent canvas and camera.
7. As a whiteboard user, I want ordinary Hyprland workspaces to remain available, so that enabling whiteboards does not convert or remove existing workspace behavior.
8. As a whiteboard user, I want each real application window to belong to exactly one Hyprland workspace and monitor, so that the canvas does not create duplicate live windows.
9. As a whiteboard user, I want application windows represented as real compositor-rendered objects, so that normal mode preserves ordinary application interaction.
10. As a whiteboard user, I want normal mode at 1x, so that the focused application receives ordinary keyboard and pointer input.
11. As a whiteboard user, I want an explicit gesture or keybind for panning at 1x, so that canvas navigation does not steal ambiguous application input.
12. As a whiteboard user, I want crossing below 1x to enter management mode through the 0.9x threshold, so that the mode change is predictable.
13. As a whiteboard user, I want management mode to begin at a bounded zoom-out level such as 0.5x, so that entering overview is immediately useful without creating unbounded rendering cost.
14. As a whiteboard user, I want to pan and zoom smoothly within bounded practical limits, so that I can manage a logically unbounded canvas without numerical instability.
15. As a whiteboard user, I want management mode to own canvas and window actions, so that miniature application content is not treated as direct application input.
16. As a whiteboard user, I want selecting a window to focus the corresponding real application window, so that returning to 1x gives me normal interaction with that application.
17. As a whiteboard user, I want restoring 1x or double-clicking a window to return to normal mode, so that entering an application is direct.
18. As a whiteboard user, I want an initial configurable 2-by-4 grid, so that windows have a useful default arrangement.
19. As a whiteboard user, I want configurable grid rows, columns, gaps, and monitor margins, so that the layout fits different outputs.
20. As a whiteboard user, I want each grid slot to contain at most one grid-placed window, so that placement is unambiguous.
21. As a whiteboard user, I want a window to span adjacent rectangular free slots, so that larger applications can occupy more canvas space.
22. As a whiteboard user, I want an attempted span resize to be rejected when any claimed slot is occupied, so that existing placements are not silently displaced.
23. As a whiteboard user, I want moving onto an occupied grid slot to swap grid windows, so that rearranging a populated board is efficient.
24. As a whiteboard user, I want to explicitly toggle a window into the floating layer, so that free-form placement is intentional.
25. As a whiteboard user, I want floating windows to overlap grid slots and other floating windows, so that the upper layer supports free-form composition.
26. As a whiteboard user, I want opening placement to be configurable, so that new windows can follow my preferred grid or floating behavior.
27. As a whiteboard user, I want grid placement, spans, layer membership, and floating coordinates to persist when identity is stable, so that my canvas survives reloads and window lifecycle changes.
28. As a whiteboard user, I want camera state and placement metadata stored separately from Hyprland workspace persistence, so that whiteboard state is not confused with native workspace state.
29. As a whiteboard user, I want cross-monitor movement to be an explicit management action, so that a window is never presented as simultaneously belonging to multiple canvases.
30. As a whiteboard user, I want application popups to remain attached to their parent, so that Wayland popup correctness is preserved.
31. As a whiteboard user, I want popups rendered above other whiteboard content where appropriate, so that application UI remains usable.
32. As a whiteboard user, I want unsupported private-hook versions to fail closed, so that the affected workspace remains an ordinary usable Hyprland workspace.
33. As a wallpaper user, I want procedural wallpaper to work without Whiteboard, so that it is independently useful.
34. As a wallpaper user, I want wallpaper structure generated from explicit-edge simple-tiled WFC rules, so that authored tile compatibility produces coherent results.
35. As a wallpaper user, I want each finite wallpaper chunk addressed by integer coordinates, so that visible regions can be generated and cached independently.
36. As a wallpaper user, I want chunk boundaries to use shared profiles or compatible patches, so that adjacent chunks do not show seams.
37. As a wallpaper user, I want chunk identity derived from the master seed, generator and ruleset versions, and coordinates, so that regeneration is deterministic.
38. As a wallpaper user, I want generation work bounded by observations, retries, worker queues, resident chunks, and texture memory, so that wallpaper cannot monopolize system resources.
39. As a wallpaper user, I want deterministic fallback content when generation fails or is delayed, so that missing chunks never block interaction.
40. As a wallpaper user, I want generation driven by the visible region plus a small prefetch margin, so that the plugin avoids generating an unbounded canvas.
41. As a wallpaper user, I want structural WFC output independent of time, cursor, hover, and camera, so that state changes do not invalidate deterministic generation.
42. As a wallpaper user, I want animation handled in a bounded presentation layer, so that effects remain responsive without regenerating structure.
43. As a wallpaper user, I want cursor-local, stable hovered-surface, and monitor-scoped camera inputs available to presentation effects, so that optional interaction effects can respond without owning input.
44. As a wallpaper user, I want wallpaper presented strictly behind applications, fullscreen surfaces, lock screens, and other authoritative surfaces, so that it cannot obscure security or application content.
45. As a wallpaper user, I want covered wallpaper animation to pause or degrade and resume from cached state when visible, so that hidden rendering does not waste resources.
46. As a wallpaper user, I want visited-region continuity to survive restarts through persisted deterministic metadata, so that changing sessions does not unexpectedly change explored regions.
47. As a wallpaper maintainer, I want changed seeds, rulesets, and generator versions to use separate cache namespaces, so that incompatible chunks are never mixed.
48. As a layered-submap user, I want to declare one root layer and nested child layers in native Lua, so that related bindings can be described once as a hierarchy.
49. As a layered-submap user, I want each layer to own its bindings, layer key, and optional enter/exit hooks, so that navigation and behavior are colocated.
50. As a layered-submap user, I want the root entry binding to enter the root layer, so that the layered submap has a predictable starting point.
51. As a layered-submap user, I want a child layer's layer key to enter it from its parent and return to its parent while active, so that navigation is reversible without repeated declarations.
52. As a layered-submap user, I want sibling transitions inferred where the layer keys permit them, so that common navigation requires less configuration.
53. As a layered-submap user, I want explicit transition overrides for unusual navigation, so that inference does not limit valid layouts.
54. As a layered-submap user, I want Escape to exit all layers and clear the submap, so that recovery is always available without a user-defined reset layer.
55. As a layered-submap user, I want existing bind semantics and action values passed through unchanged, so that descriptions, repeating, locked, keyboard, and mouse behavior remain available.
56. As a layered-submap user, I want validation to reject duplicate names, duplicate binding identities, missing targets, and invalid declarations, so that configuration errors are visible before navigation begins.
57. As a layered-submap user, I want cycles to be allowed, so that navigation may intentionally return to an earlier layer.
58. As a layered-submap user, I want enter/exit hooks to run after transitions, so that hooks observe an already-applied state.
59. As a layered-submap user, I want hook failures to leave navigation applied and report visibly, so that a callback cannot corrupt the active layer.
60. As a layered-submap user, I want hooks unable to consume input, redirect navigation, or mutate the declaration, so that Hyprland remains responsible for raw key processing.
61. As a decoration user, I want a style-only decoration attached to each application window, so that individual windows have a consistent visual identity without a global panel.
62. As a decoration user, I want the decoration enabled for tiled and floating windows by default, so that style does not depend on layout mode.
63. As a decoration user, I want to disable decoration per window, so that exceptional applications can opt out.
64. As a decoration user, I want per-window style overrides through configuration or window rules, so that individual windows can differ without a new rule language.
65. As a decoration user, I want to choose a top, right, bottom, or left title-bar side, so that the decoration fits my workflow.
66. As a decoration user, I want title text displayed, so that the decorated window remains identifiable.
67. As a decoration user, I want optional compositor-safe buttons, so that supported controls can be presented without fragile raw input behavior.
68. As a decoration user, I want shadow sides, thickness, offset, and color configured independently, so that the visual style is expressive and deterministic.
69. As a decoration user, I want dragging, resizing, and other input to retain existing Hyprland behavior, so that the decoration does not become a competing input system.
70. As a decoration user, I want defaults and geometry to remain deterministic across tiled/floating transitions and movement, so that visual state does not jump unpredictably.
71. As a brightness user, I want to set a scalar brightness value per monitor through native Lua, so that I can adjust outputs independently.
72. As a brightness user, I want brightness state persisted by stable monitor identifier, so that settings survive compositor reloads and monitor reconnection where identity is stable.
73. As a brightness user, I want unsupported operations reported explicitly, so that the API does not imply support for broader color management.
74. As a brightness user, I want the last valid state preserved when a monitor cannot apply a request, so that a failed update does not corrupt a working setting.
75. As a brightness user, I want the plugin to refuse incompatible CTM, gamma, or ICC/VCGT ownership, so that it does not silently replace or compose with another color owner.
76. As a brightness maintainer, I want monitor hotplug and output-control failure handled explicitly, so that stale state is not applied to the wrong output.
77. As a portfolio maintainer, I want broader temperature, RGB gain, inversion, custom gamma ramps, wallpaper-only color effects, and arbitrary full-desktop shaders excluded from the first release, so that the brightness contract remains supportable.
78. As a portfolio maintainer, I want Hyprland forks and core changes excluded, so that the collection remains an independently loadable plugin collection.

## Implementation Decisions

- The deliverable is five independent plugins: Whiteboard, Procedural Wallpaper, Layered Submap, Per-Window Decoration, and Monitor Brightness Control. The existing example plugin remains the repository's minimal loading and Lua integration example.
- User-facing actions and configuration use plugin-owned namespaces under `hl.plugin.<namespace>.<name>`, following ADR-0001. Legacy `.conf` dispatcher configuration is not the supported interface.
- Each plugin uses a single exact Hyprland version pin. The pin is managed through the plugin collection's package metadata, and any pin change requires explicit compatibility validation before release.
- Whiteboard owns native Lua configuration, orchestration of real windows, workspace/canvas metadata, focus requests, and compatibility checks. A version-locked private renderer/input integration may transform compositor-rendered windows during management mode.
- Whiteboard does not resize application clients to create thumbnails. Wayland application geometry remains authoritative and normal Hyprland configure behavior remains responsible for resizing.
- Whiteboard has one monitor-scoped canvas per whiteboard workspace, a logically unbounded integer-coordinate world, bounded practical zoom, independent camera state, and separate lower grid and upper floating layers.
- Whiteboard begins management mode below 1x through an initial 0.9x threshold, uses an initial bounded entry level such as 0.5x, and returns to normal mode at 1x or through explicit window activation.
- Whiteboard's initial grid is configurable 2-by-4. Grid slots are exclusive, rectangular spans require all slots to be free, occupied-slot moves swap grid windows, and floating placement requires an explicit layer toggle.
- Whiteboard persistence stores camera and placement metadata in plugin-owned state, keyed by stable window and monitor identity where available. GPU textures are not treated as persistent state.
- Whiteboard application popups remain attached to their parent and are not independent persistent whiteboard items.
- Whiteboard private hooks must fail closed when the running host is outside the supported compatibility range. The fallback is an ordinary Hyprland workspace, not a partial unsafe transformation.
- QuickShell/helper support is permitted for whiteboard model ownership, per-monitor layer surfaces, camera presentation, client-side hit testing, popups, persistence, and screencopy/image-copy thumbnails as degraded or future support. It is not the primary interaction model for the whiteboard's real-window presentation.
- Procedural Wallpaper uses a bounded declarative native Lua WFC ruleset and owns deterministic chunk generation, scheduling, cache state, and presentation state.
- Wallpaper chunks use explicit-edge simple-tiled WFC with fixed shared boundary profiles or an equivalent compatible-patch contract. Seeds include master seed, generator version, ruleset version, and integer chunk coordinates.
- Wallpaper generation is viewport-driven and bounded by worker queue size, retries, observations, resident chunks, prefetch margin, and texture memory. Generation failure selects deterministic fallback content and fills asynchronously.
- Wallpaper structural generation is independent of time, cursor, hover, and camera. Animation and effects are applied only to bounded resident presentation textures.
- Wallpaper presentation is a per-monitor background surface that remains behind application, fullscreen, lock-screen, and other authoritative surfaces. The plugin has no input or foreground UI role.
- Whiteboard may optionally publish monitor-scoped camera and hover/effect state to Wallpaper. Wallpaper must operate without that state and must not require Whiteboard to load.
- Wallpaper persistence stores deterministic generation metadata and cache identity, not rendered GPU textures. Seed, ruleset, or generator-version changes create separate cache namespaces.
- Layered Submap exposes a focused `define`-style native Lua API around a nested layer-centric declaration. The API has one root layer; each layer declares bindings, an optional layer key, and optional enter/exit callbacks.
- Layered Submap derives parent/child navigation and available sibling transitions, accepts explicit transition overrides, generates Escape reset behavior, and installs behavior using existing Hyprland submap and bind primitives.
- Layered Submap passes through existing action values and bind options, adds no dispatcher vocabulary, and never processes raw key events.
- Layered Submap validates declaration structure before installation. Duplicate names, duplicate binding identities, missing targets, and invalid layer declarations are rejected; cycles are valid.
- Layered Submap callbacks run after an applied transition. They cannot consume input, redirect navigation, or mutate the declaration. Callback failure preserves the applied transition and reports through Hyprland's notification mechanism.
- Per-Window Decoration uses the public window-owned decoration seam. It provides only style: selectable title-bar side, title text, optional compositor-safe buttons, and independently configurable shadow sides, thickness, offset, and color.
- Per-Window Decoration applies to tiled and floating windows by default, with per-window disable and configuration/window-rule overrides. It does not introduce a new rule language.
- Per-Window Decoration delegates dragging, resizing, and other input to existing Hyprland behavior. Animation and rounding are deferred unless the supported decoration seam can provide them without fragile private hooks.
- Monitor Brightness Control exposes only a per-monitor scalar brightness/dimming value through native Lua. It uses existing supported output-control capability and preserves the last valid state when an output cannot apply a request.
- Monitor Brightness Control uses stable monitor identifiers for persistence and refuses to take control when incompatible CTM, gamma, or ICC/VCGT ownership is active. It must handle ownership conflicts and hotplug explicitly.
- Temperature, RGB gains, inversion, custom gamma ramps, arbitrary full-desktop shaders, wallpaper-only color effects, and a mandatory QuickShell color UI are not part of the brightness plugin contract.
- No plugin may require a Hyprland fork or core modification. Where private hooks are accepted for Whiteboard, they are version-coupled, compatibility-checked, and fail closed.
- Implementation order is Whiteboard, Procedural Wallpaper, then Layered Submap, Monitor Brightness Control, and Per-Window Decoration in any order. This order does not make later plugins dependent on earlier ones.

## Testing Decisions

- Tests assert externally observable behavior at the highest available seam: the plugin's native Lua-facing API, resulting Hyprland actions/state, and visible lifecycle or failure behavior. Tests must not couple to helper functions, private data structures, renderer implementation details, or exact internal hook call sequences.
- The primary common seam is loading a plugin into a controlled Hyprland host, invoking its public Lua API, and observing the resulting compositor-visible state or notification. This follows the existing example plugin's public `hl.plugin` contract and its manual load/dispatch workflow.
- Whiteboard tests cover workspace assignment, normal/management mode transitions, zoom threshold and bounded entry, camera isolation per monitor, grid occupancy/spans/swaps, floating-layer toggles, focus requests, popup attachment, persistence restoration, lifecycle cleanup, and fail-closed behavior for incompatible host versions.
- Whiteboard integration tests cover transformed rendering and inverse pointer behavior only through observable window placement, focus, input routing, damage, and fallback outcomes. They must include tiled and floating windows, multiple monitors, popups, and direct-scanout or unsupported-render conditions where the host makes those observable.
- Procedural Wallpaper tests cover deterministic chunk identity, same-input regeneration, seam compatibility, bounded scheduling, retry and deterministic fallback behavior, viewport prefetch, cache namespace separation, restart continuity metadata, covered-surface animation degradation, and strict background ordering.
- Layered Submap tests cover declaration installation, generated root/child navigation, parent return, inferred and overridden sibling transitions, generated Escape reset, pass-through bind options/actions, validation failures, cycles, callback timing, and callback failure behavior.
- Per-Window Decoration tests cover default tiled/floating coverage, per-window disable and override selection, all title-bar sides, title presentation, button safety behavior, independent shadow values, deterministic geometry after layout changes, and delegation of input behavior to Hyprland.
- Monitor Brightness Control tests cover per-monitor scalar values, stable-identifier persistence, invalid request handling, preservation of the last valid state, hotplug behavior, exclusive ownership conflicts, and explicit unsupported-operation reporting.
- Portfolio tests cover independent loading and unloading, operation without optional companion plugins, exact version-pin validation, and the absence of mandatory cross-plugin initialization coupling.
- Existing prior art is the `hello` example's native Lua function registration and notification behavior, the repository's manual plugin load/unload/dispatch workflow, and the documented CMake/Clang quality checks. Because the current codebase has no automated plugin behavior suite, implementation should establish a host-backed integration harness at the public Lua API seam before adding plugin-specific test cases.
- Tests must avoid contacting a running user's compositor during ordinary verification. Any live compositor verification must follow the repository's runtime-safety guidance and be an explicit manual check rather than an implicit automated test dependency.

## Out of Scope

- A monolithic plugin, shared mandatory plugin bundle, or plugin-to-plugin load dependency.
- A Hyprland fork, Hyprland core modification, or an attempt to upstream a new compositor-native canvas in this portfolio.
- General-purpose raw input processing by any plugin.
- A compositor-native infinite canvas containing transformed live application surfaces with fully native popup, focus, and hit-testing semantics; that would require a core-level solution outside this spec.
- QuickShell thumbnails as the primary Whiteboard interaction model. They may provide degraded or future support surfaces.
- Mandatory QuickShell or helper delivery for Monitor Brightness Control.
- Temperature, RGB gains, inversion, custom gamma ramps, wallpaper-only color effects, and arbitrary full-desktop shaders in the brightness plugin.
- Global panels, launchers, task lists, workspace overviews, or global window-manager chrome in Per-Window Decoration.
- A new rule language, raw decoration input system, fragile animation, or rounding guarantees in Per-Window Decoration.
- User-defined reset layers, raw key event ownership, or dispatcher vocabulary in Layered Submap.
- Unbounded WFC generation, unbounded worker queues/cache/texture memory, nondeterministic chunk identity, or structural regeneration based on transient effect inputs.
- Persisting rendered GPU textures or treating old cache data as compatible after seed, ruleset, or generator-version changes.
- Direct-scanout guarantees for full-desktop shader effects.
- Replacing or silently composing with an existing CTM, gamma, ICC, or VCGT owner.
- Release implementation of the portfolio in this planning artifact; this spec defines the boundary and order for subsequent implementation work.

## Further Notes

- The current repository is a minimal plugin scaffold. The first implementation pass should preserve its existing native Lua conventions while expanding build and package metadata for independent plugin targets.
- The accepted native Lua decision in ADR-0001 is normative: plugin actions are Lua functions under `hl.plugin`, not legacy dispatcher commands.
- The strongest implementation risk is Whiteboard's private renderer/input compatibility surface. It is intentionally accepted as a version-locked trade-off, but every unsupported host state must degrade to an ordinary workspace rather than expose partially transformed interaction.
- The portfolio's single highest test seam is the public Lua-facing API exercised against a controlled host. Whiteboard rendering/input compatibility is the one unavoidable additional integration boundary because its value depends on compositor-visible transformed windows.
- Optional Whiteboard-to-Wallpaper state should be a one-way, monitor-scoped effect input contract. Neither plugin should require the other's persisted state or lifecycle.
- The portfolio remains five-plugin scope despite Whiteboard and Wallpaper requiring supporting client/surface processes for selected responsibilities.
