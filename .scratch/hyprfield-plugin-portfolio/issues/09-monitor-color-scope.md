# Monitor Color Control Scope

Label: wayfinder:grilling
Type: grilling
Status: resolved
Parent: ../map.md
Blocked by: 08

## Question

Which monitor color-control destination is worth pursuing: gamma/CTM controls through a helper, QuickShell controls over that helper, a plugin-rendered wallpaper-only effect, or a general per-monitor shader over the full desktop? Decide the first-version controls, inversion expectations, conflict policy, and whether a Hyprland core change is explicitly excluded.

## Answer

Make this an independent Hyprland plugin with a native Lua-facing API for
simple per-monitor scalar brightness/dimming. QuickShell is not part of the
deliverable; it may consume the plugin's Lua-facing controls later.

The plugin does not promise temperature, RGB gains, inversion, custom gamma
ramps, wallpaper-only effects, or arbitrary full-desktop shaders. It uses the
existing supported output-control capability, reports unsupported operations
explicitly, and preserves the last valid state when a monitor cannot apply a
request.

Output ownership is exclusive: the plugin refuses to take control when an
incompatible CTM, gamma, or ICC/VCGT owner is active rather than replacing or
silently composing with it. Brightness state is persisted per monitor using a
stable monitor identifier. Hyprland core changes, forks, and private hooks are
out of scope.
