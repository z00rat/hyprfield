# Portfolio Boundary And Sequence

Label: wayfinder:grilling
Type: grilling
Status: resolved
Parent: ../map.md
Blocked by: 02, 03, 05, 06, 07, 09

## Question

After the individual decisions, what is the final portfolio boundary and implementation sequence? Decide which ideas are build-ready, which are QuickShell/helper projects rather than plugins, which should be deferred or dropped, how cross-plugin integration remains optional, and what Hyprland version/ABI policy the handoff must state.

## Answer

All five independent plugins remain in scope; there is no MVP subset or plugin
to defer. Whiteboard is the first implementation priority and Procedural
Wallpaper is second. Layered Submap, Monitor Brightness Control, and
Per-Window Decoration follow in any order.

The plugins remain independently loadable and useful. Cross-plugin integration
is optional: the whiteboard may publish monitor-scoped camera and hover state
for the wallpaper, but neither plugin depends on the other. QuickShell and
helper processes are supporting clients or surfaces, not mandatory plugin
coupling.

Every plugin targets exactly one Hyprland version, managed through its
`hyprpm.toml`. Changing that pin requires explicit compatibility validation
before release. The whiteboard's private hooks therefore use the same exact
version policy and must fail closed when incompatible. Hyprland forks and core
changes remain excluded, as do the previously recorded broader color effects
and other plugin-specific non-goals.

The handoff is decision-complete for portfolio boundary and ordering; it does
not authorize implementation in this planning session.

## Comments

- Resolution reached through the required grilling and domain-modeling session.
