# Wallpaper Rendering Boundary

Label: wayfinder:grilling
Type: grilling
Status: resolved
Parent: ../map.md
Blocked by: 04

## Question

Should the procedural wallpaper be a Hyprland plugin, a QuickShell per-monitor background surface, or a split design? Decide the public state/effect contract, whether it must remain useful without the whiteboard, how it behaves under windows/fullscreen/lock screens, and what performance and fallback guarantees are required.

## Answer

The procedural wallpaper is an independently useful plugin. Native Lua defines
a bounded declarative WFC ruleset; the plugin owns deterministic generation,
chunk scheduling, and presentation state. It presents generated content through
a per-monitor background surface rather than owning a fragile full-desktop
renderer, so the wallpaper remains strictly behind application, fullscreen,
lock-screen, and other authoritative surfaces.

The wallpaper is useful without the whiteboard. The whiteboard may optionally
provide monitor-scoped camera and hover/effect inputs, but neither plugin
depends on the other. The wallpaper has no input or foreground UI role.

Generation is viewport-driven: only the visible whiteboard region plus a small
prefetch margin is generated, with bounded worker queues, retries, resident
chunks, and texture memory. Missing chunks use deterministic fallback content
and fill asynchronously without blocking interaction. Generated chunk identity
includes the master seed, generator version, ruleset version, and integer
coordinates.

Visited-region continuity survives restarts through persisted deterministic
generation metadata and identity; rendered GPU textures are regenerated rather
than persisted. A bounded in-memory cache retains active chunks, while old
persisted regions may be garbage-collected. Changing the seed, ruleset, or
generator version creates a separate cache namespace and never mixes old chunks
into the new wallpaper. Animation pauses or degrades when the background is
fully covered and resumes from cached state when visible.

## Comments

- Resolution reached through the required grilling and domain-modeling session.
