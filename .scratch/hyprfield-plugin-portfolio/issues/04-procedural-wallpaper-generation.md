# Procedural Wallpaper Generation

Label: wayfinder:research
Type: research
Status: resolved
Parent: ../map.md
Blocked by: none

## Question

Which seeded, chunked Wave Function Collapse or comparable procedural-generation design can provide an effectively infinite animated wallpaper with bounded CPU/GPU/memory cost? Establish tile-set constraints, chunk seams, determinism, regeneration/caching, animation strategy, and the inputs needed for cursor, hovered-surface, and camera-dependent effects.

## Answer

Use explicit-edge simple-tiled WFC for finite, coordinate-addressed chunks, with fixed shared boundary profiles or an offset compatible-patch scheme for seams. Derive each chunk seed from the master seed, generator/grammar versions, and integer chunk coordinates; cap observations, retries, worker queue, resident chunks, and textures; and use a deterministic compatible fallback on failure. Keep WFC structural output independent of time, cursor, hover, and camera. Animate a bounded resident texture set in the presentation layer, and pass cursor-local, stable hovered-surface, and monitor-scoped camera state as effect inputs. See [`research/procedural-wallpaper-generation.md`](../research/procedural-wallpaper-generation.md). The plugin-versus-QuickShell presentation boundary remains ticket 05.
