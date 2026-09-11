# Hyprfield Plugin Portfolio

Label: wayfinder:map
Status: open

## Destination

Produce a decision-complete handoff package for the proposed Hyprfield plugin portfolio: MVP scope, plugin-versus-QuickShell boundaries, APIs and configuration, implementation order, risks, and explicit non-goals for each worthwhile idea.

The map may recommend building, deferring, dropping, or replacing an idea with a QuickShell/helper solution.

## Notes

Domain: Hyprland compositor plugins, native Lua configuration, QuickShell surfaces, Wayland output control, and compositor rendering.

Consult `CONTEXT.md`, `docs/adr/`, `docs/agents/issue-tracker.md`, and the current `/home/hz/codebase/Hyprland` source. Use `/grilling` and `/domain-modeling` for HITL decisions, `/research` for external or source-backed facts, and `/prototype` only when a concrete artifact is needed to judge interaction or API shape.

Standing preferences: preserve independent plugin loading, prefer native Lua-facing APIs, require multi-monitor support, avoid Hyprland core forks, and keep the five ideas independently useful. Do not resolve more than one HITL ticket in a session.

## Decisions so far

<!-- Open tickets are child issues in `.scratch/hyprfield-plugin-portfolio/issues/`. -->

- [Whiteboard Geometry Capabilities](issues/01-whiteboard-geometry-capabilities.md) — reliable whiteboard camera, cards, hit testing, popups, and persistence belong in a per-monitor QuickShell/helper surface; the plugin orchestrates real windows and Lua actions.
- [Procedural Wallpaper Generation](issues/04-procedural-wallpaper-generation.md) — use bounded coordinate-seeded simple-tiled WFC chunks with explicit seam constraints and a separate animated presentation layer.
- [Monitor Color Control Facts](issues/08-monitor-color-control-facts.md) — use an exclusive CTM helper for initial per-monitor temperature, dimming, and RGB gains; defer inversion and general shader guarantees.
- [Whiteboard Workspace Model](issues/02-whiteboard-workspace-model.md) — each whiteboard workspace has one monitor-scoped 2D canvas; normal mode is 1x application interaction, management mode is bounded zoom-out/pan, and private renderer/input hooks are accepted without a Hyprland fork.
- [Layered Submap API](issues/06-layered-submap-api.md) — use a nested native-Lua layer tree with plugin-derived parent/child navigation, inferred sibling transitions with overrides, generated Escape reset, and pass-through Hyprland primitives.
- [Per-Window Decoration Style](issues/07-window-decoration-style.md) — provide a style-only decoration for tiled and floating windows, with per-window toggles and overrides; defer global panels, raw input, and fragile animation/rounding features.
- [Whiteboard Zoom And Overview](issues/03-whiteboard-zoom-and-overview.md) — transform real compositor-rendered windows through version-locked private hooks; use 1.0x for normal app interaction and sub-1.0x for smooth, bounded management of a configurable 2x4 grid and floating layer.
- [Wallpaper Rendering Boundary](issues/05-wallpaper-rendering-boundary.md) — an independent plugin executes Lua-declared WFC rules and presents viewport-driven, persisted deterministic chunks through a bounded per-monitor background surface; whiteboard inputs remain optional.
- [Monitor Color Control Scope](issues/09-monitor-color-scope.md) — build an independent plugin with a native Lua-facing per-monitor scalar brightness API; leave QuickShell consumption to a later client and exclude broader color effects and core changes.
- [Portfolio Boundary And Sequence](issues/10-portfolio-boundary-and-sequence.md) — keep all five plugins in scope; implement Whiteboard first, Wallpaper second, then the three remaining plugins in any order, with optional integration and exact Hyprland pins managed through `hyprpm.toml`.

## Implementation map

- [Whiteboard Compatibility Gate](issues/12-whiteboard-compatibility-gate.md) — establish and verify the fail-closed renderer/input seam before product behavior.
- [Whiteboard Host Activation](issues/13-whiteboard-workspace-activation.md) — verify monitor/workspace assignment and ordinary-workspace preservation.
- [Whiteboard Workspace and Normal Mode](issues/14-whiteboard-workspace-and-normal-mode.md) — own board/client identity, lifecycle, persistence, and 1x behavior.
- [Whiteboard Window Placement](issues/15-whiteboard-window-placement.md) — implement exclusive grid and explicit floating placement.
- [Whiteboard Camera Rendering](issues/16-whiteboard-camera-rendering.md) — implement bounded transforms and restoration.
- [Whiteboard Management Input](issues/17-whiteboard-management-input.md) — implement managed gestures and activation.
- [Whiteboard End-to-End Integration](issues/18-whiteboard-end-to-end-integration.md) — verify the complete Whiteboard workflow and failure behavior.

## Out of scope

- Broader monitor color effects (temperature, RGB gains, inversion, custom gamma ramps, and arbitrary shaders) are excluded from this portfolio; the color-control plugin is intentionally limited to scalar brightness. See [Monitor Color Control Scope](issues/09-monitor-color-scope.md).
