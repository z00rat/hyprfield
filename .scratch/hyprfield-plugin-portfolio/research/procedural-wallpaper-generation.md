# Procedural wallpaper generation

## Resolution

Use a bounded, coordinate-addressed **simple-tiled WFC** generator for the wallpaper's
structural layer. Generate a finite chunk only when it enters a small visible/prefetch
window, keep a bounded LRU cache, and render the resulting tile data with a single
animated presentation layer. Do not run WFC every frame and do not make cursor,
hover, or camera state part of the generation seed.

This is a recommendation for the generator contract, not an implementation decision
about whether the presentation belongs in a Hyprland plugin or QuickShell. That
boundary remains ticket 05's decision.

## What the sources establish

### WFC is finite constraint solving

The original WFC implementation creates a wave array whose size is the requested
output size, repeatedly observes one cell, and propagates constraints until the wave
is solved or reaches a contradiction. Its `Run` method accepts a seed and a limit,
and constructs a fresh seeded `Random` for the run. The same source also returns
failure when propagation produces an empty domain. [WFC `README.md`, Algorithm](https://github.com/mxgmn/WaveFunctionCollapse#algorithm),
[WFC `Model.cs`](https://github.com/mxgmn/WaveFunctionCollapse/blob/master/Model.cs#L48-L86)

The limit is not by itself a completion guarantee: the reference code returns `true`
after the loop limit even if the wave is not fully observed. A bounded generator must
therefore separately require a completed output, or use a deterministic fallback when
the budget expires. [WFC `Model.cs`](https://github.com/mxgmn/WaveFunctionCollapse/blob/master/Model.cs#L54-L86)

The reference algorithm can contradict, and its README explicitly notes that deciding
whether a valid completion exists is NP-hard. A wallpaper generator must treat
contradiction and budget exhaustion as normal outcomes, not as compositor-fatal
errors. [WFC `README.md`, Algorithm](https://github.com/mxgmn/WaveFunctionCollapse#algorithm)

### The tiled model is the appropriate runtime model

WFC's simple tiled model reduces the problem to tile adjacency data. The reference
implementation expands authored tile symmetries into concrete variants, assigns
weights, builds directional propagator tables, and reports tiles with no legal
neighbor in a direction. [WFC `README.md`, Tilemap generation](https://github.com/mxgmn/WaveFunctionCollapse#tilemap-generation),
[WFC `SimpleTiledModel.cs`](https://github.com/mxgmn/WaveFunctionCollapse/blob/master/SimpleTiledModel.cs#L34-L174)

For this wallpaper, tiles should be authored around a small semantic edge grammar,
not extracted from a large full-color image. Each concrete variant needs four
directional connectors whose rendered borders actually agree. Include neutral
fill, straight, corner, junction, and termination variants as needed by the visual
grammar. Validate that every enabled variant has at least one legal neighbor in every
required direction. The reference README warns that an unconstrained or "easy"
tileset quickly loses correlations and does not produce interesting global
arrangements. [WFC `README.md`, Tilemap generation](https://github.com/mxgmn/WaveFunctionCollapse#tilemap-generation)

The overlapping model is useful offline when the desired look is learned from a
sample: it extracts NxN patterns and only permits locally observed patterns. Its
correlation radius is `N`, which increases boundary context and runtime work. For a
wallpaper whose tiles can be designed explicitly, the simple tiled model gives a
smaller and more inspectable runtime state. [WFC `OverlappingModel.cs`](https://github.com/mxgmn/WaveFunctionCollapse/blob/master/OverlappingModel.cs#L25-L108)

## Chunk contract

### Addressing and budget

Represent a chunk by integer coordinates `(cx, cy)`, a fixed cell width and height,
the generator version, the tileset/grammar version, and the user seed. The visible
camera maps to a finite set of chunk coordinates plus a fixed one-chunk safety ring.
The world is effectively infinite because no global world array is retained; only
the integer coordinate domain and currently resident chunks exist.

Choose a fixed chunk size and an explicit maximum for each of these quantities:

- cells in the WFC region, including any boundary ring;
- tile variants and propagation entries per cell;
- observation steps per attempt;
- deterministic retry count;
- concurrently queued generation jobs;
- resident decoded chunks and resident GPU textures.

The temporary wave and propagation queue are then bounded by the chunk dimensions and
tile grammar, while the resident cache is bounded by its entry count. The exact
numbers are performance-policy choices and need benchmarking; they must be config
limits rather than emergent properties of camera travel.

### Seams

Independent chunks must not merely have independently generated random seeds. That
would allow two legal interiors to choose incompatible borders. Use one of these
equivalent seam contracts:

1. Derive each shared horizontal and vertical boundary profile from the shared edge
   coordinate. Pass those profiles as fixed boundary constraints to both adjacent
   chunks.
2. Generate a periodic base map and compatible replacement patches. Generate each
   runtime chunk with a fixed outer boundary and crop an offset interior, so the
   runtime seam replaces the repeated base seam.

The second approach is documented by Marian Kleineberg's chunk-based WFC work: a
periodic base tiling is prepared, compatible patches are prepared, and runtime WFC
is run at a half-chunk offset from four surrounding patches. The author explicitly
states that the offset lets chunks be generated independently and that a failed
runtime generation can fall back to the starting patches. [Kleineberg, “Generating
an infinite world with the Wave Function Collapse algorithm”, Chunk-based WFC](https://marian42.de/article/infinite-wfc/#chunk-based-wfc)

For a 2D wallpaper, the first contract is simpler if the tile art has exact matching
edge profiles. The second contract is preferable when a repeated seam would otherwise
be visible or when the grammar has useful patch-level structure. If the generator
uses an overlapping model instead, retain at least an `N - 1` cell ghost/context ring
around the chunk and crop the interior; the ring still needs a deterministic shared
boundary source. The reference overlapping model's `N`-cell pattern agreement is the
reason for that context requirement. [WFC `OverlappingModel.cs`](https://github.com/mxgmn/WaveFunctionCollapse/blob/master/OverlappingModel.cs#L70-L108)

Do not hide seams with a per-frame blur or camera shift. A seam contract is required
for correctness, and visual post-processing should only be an optional effect.

### Determinism

Define a stable key such as:

```text
chunkSeed = Hash(masterSeed, generatorVersion, grammarHash, cx, cy)
```

Use that seed for all choices in that chunk, including tie-breaking, weighted tile
selection, retry seeds, and any deterministic fallback. Never consume a process-wide
RNG or use generation order, wall-clock time, thread scheduling, pointer position,
or camera movement as hidden inputs. The reference WFC program demonstrates the
important part of this contract by taking a run seed and creating a seeded RNG for
the run. [WFC `Model.cs`](https://github.com/mxgmn/WaveFunctionCollapse/blob/master/Model.cs#L54-L86)

The cache key must include the same inputs that affect output:
`(masterSeed, generatorVersion, grammarHash, cx, cy, boundaryProfile)`. Increment
`generatorVersion` when the algorithm, tie-breaking, hash, or fallback changes. This
makes a visual change an explicit invalidation instead of a traversal-order bug.

Use a specified integer hash/PRNG implementation if reproducibility across builds or
architectures matters. A seeded result is only useful as a contract when the choice
ordering and arithmetic are also fixed.

### Failure and regeneration

Generation should be a pure data job. Run it away from the compositor's render path,
with a hard work budget and a small fixed retry sequence. Accept the result only when
all cells are resolved and all boundary constraints are satisfied. On failure,
publish the previous cached chunk; if there is none, use a pre-generated compatible
patch or a neutral valid tile field. Never block a render callback waiting for an
unbounded solve.

Regenerate only for a cache miss, a changed grammar/seed/configuration, a changed
boundary profile, or a chunk that must be replaced by an explicit user action. Camera
motion across already cached chunks, time progression, cursor motion, and hover
changes must not invalidate structural chunks.

## Caching and rendering

### CPU and memory

Keep a ring or LRU of chunks covering the monitor view plus a small prefetch margin.
Evict by entry count or byte budget, and bound the worker queue. Store compact tile
IDs and per-tile parameters in the CPU cache; release the WFC wave, compatibility
counts, and propagation stack after a job completes. Cache failed keys briefly to
avoid retry storms.

The cache should be per generator configuration and coordinate, not a single mutable
map that grows with the camera path. Kleineberg identifies order-dependent output,
unbounded memory retention, reliability failures, and lack of parallelism as the
problems of an unbounded dictionary-based WFC approach. The chunk-based method is
presented specifically to make chunks independent and parallelizable. [Kleineberg,
“Generating an infinite world with the Wave Function Collapse algorithm”, original
limitations and chunk-based WFC](https://marian42.de/article/infinite-wfc/#the-original-approach-and-its-limitations)

Generation workers must not call compositor/render APIs. The worker should return
plain tile data; texture creation, upload, cache swap, damage, and pass insertion
must occur through the presentation side's supported thread/context. This is also
consistent with the local plugin API warning that C++ objects cross the plugin ABI
and that internal function hooks have no API stability. [Hyprland `PluginAPI.hpp`](../../../Hyprland/src/plugins/PluginAPI.hpp#L8-L12),
[Hyprland `PluginAPI.hpp`](../../../Hyprland/src/plugins/PluginAPI.hpp#L232-L248)

### GPU and animation

Keep generated structure static over an animation interval. Animate in a shader or
other presentation stage using time, a low-frequency phase, color modulation, glow,
parallax, or per-tile deterministic phase. If topology must animate, precompute a
small number of variants from the same structural chunk and blend or step between
them; do not rerun WFC or upload a new full-resolution texture every frame.

The GPU cost is then bounded by the visible surface, resident texture set, and number
of presentation passes. A full-screen animated effect still causes full-screen work
when it is continuously damaged, so the performance contract should include a target
frame rate and a reduced-rate or paused mode when the desktop is idle, occluded, or
locked.

QuickShell's first-party `ShaderEffect` API supports custom vertex and fragment
shaders on a rectangle, shader inputs from QML properties, and image samplers. Its
documentation also notes that the software backend does not render the effect and
that Qt 6 shader assets are normally prepared as `.qsb` files. [Qt `ShaderEffect`](https://doc.qt.io/qt-6/qml-qtquick-shadereffect.html)

QuickShell's current Astroland example uses a `FrameAnimation` and `frameTime` for
per-frame motion, which is suitable for advancing presentation state but not for
structural generation. Qt's first-party documentation distinguishes this synchronized
frame callback from a timer-based custom animation. [Qt `FrameAnimation`](https://doc.qt.io/qt-6/qml-qtquick-frameanimation.html),
[Astroland `ScreensaverComponent.qml`](file:///home/hz/codebase/astroland_dots/.config/quickshell/ScreensaverComponent.qml#L58-L90)

### Local Hyprland and Astroland evidence

The current Hyprland source exposes render stages including `RENDER_POST_WALLPAPER`
and `RENDER_POST_WINDOWS`; the renderer emits the wallpaper stage after background
layer surfaces and before bottom-layer surfaces. A plugin can therefore insert a
wallpaper pass at a defined point, but that is a compositor-internal render contract,
not a portable wallpaper-surface API. [Hyprland `SharedDefs.hpp`](../../../Hyprland/src/SharedDefs.hpp#L20-L30),
[Hyprland `Renderer.cpp`](../../../Hyprland/src/render/Renderer.cpp#L1161-L1175)

Hyprland's pass system accepts texture elements with a box, damage, clipping, and
wrap modes, and the pass simplifies elements against damage and opaque regions. A
wallpaper renderer can use a bounded set of texture elements or a composed texture,
but it must damage old and new regions when content changes. [Hyprland
`TexPassElement.hpp`](../../../Hyprland/src/render/pass/TexPassElement.hpp#L32-L69),
[Hyprland `Pass.cpp`](../../../Hyprland/src/render/pass/Pass.cpp#L31-L87)

Astroland demonstrates the existing local pattern: it loads a texture into a Hyprland
texture, inserts repeated background texture pass elements at `RENDER_POST_WALLPAPER`,
and uses a fixed 16 ms event-loop timer for scene updates. It currently selects
`m_monitors[0]`, has a repeat-loop typo that uses `repeatsY` for the X bound, and
does not have a seam-constrained generator. These are useful integration evidence,
not a design to preserve. [Astroland `globals.hpp`](file:///home/hz/codebase/astroland_dots/plugin/src/globals.hpp#L28-L53),
[Astroland `main.cpp`](file:///home/hz/codebase/astroland_dots/plugin/src/main.cpp#L45-L109),
[Astroland `main.cpp`](file:///home/hz/codebase/astroland_dots/plugin/src/main.cpp#L270-L314)

Astroland also uploads a newly decoded texture on each animated GIF frame. That is
acceptable for a small sprite experiment, but it is the wrong scaling strategy for a
full-monitor wallpaper. [Astroland `AnimatedSprite.cpp`](file:///home/hz/codebase/astroland_dots/plugin/src/AnimatedSprite.cpp#L39-L81)

## Inputs and effect contract

### Cursor

Use the compositor's global pointer position and convert it once to monitor-local
logical coordinates, then normalize and clamp it for presentation uniforms. The
current Hyprland input manager exposes `getMouseCoordsInternal()` as the pointer
position, and the event bus exposes cancellable mouse-move events. [Hyprland
`InputManager.hpp`](../../../Hyprland/src/managers/input/InputManager.hpp#L86-L127),
[Hyprland `EventBus.hpp`](../../../Hyprland/src/event/EventBus.hpp#L101-L112)

The cursor position should affect only an effect uniform such as attraction,
highlight, parallax, or ripple center. It must not enter `chunkSeed`, because that
would cause the wallpaper to regenerate and potentially change its structural seams
under ordinary pointer motion.

### Hovered surface

The effect contract needs a stable hover record, not just a pointer coordinate:

```text
hover = none | { kind: window | layer, stable identity, monitor, local position }
```

Hyprland's input routing hit-tests layer popups, overlay and top layer surfaces, then
windows, and obtains both the found surface and surface coordinates. This gives the
presentation layer the information needed to distinguish a hovered window from a
hovered layer surface. [Hyprland `InputManager.cpp`](../../../Hyprland/src/managers/input/InputManager.cpp#L453-L489)

The wallpaper should normally be non-interactive and observe hover state rather than
consume it. The layer-shell protocol says that background and bottom surfaces are
ordered below normal shell surfaces, and that a client can make a layer surface
non-interactive by using an empty input region. [Wayland layer-shell protocol,
official XML](https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/raw/master/unstable/wlr-layer-shell-unstable-v1.xml)

### Camera and monitor

The camera input should be an explicit monitor-scoped value:

```text
camera = { monitor_id, origin, zoom, transform, revision }
```

Use integer chunk coordinates plus a local fractional offset when mapping camera
space to world tiles. This avoids using a growing floating-point world coordinate as
the cache identity. Camera movement changes which bounded chunks are resident and
changes presentation uniforms; it does not change a chunk's generated tile IDs.

The current Astroland code has a private camera position and explicit world-to-monitor
conversion, while Hyprland render data has render modifications and a mouse zoom
factor. Neither is a general cross-plugin camera contract. [Astroland `globals.hpp`](file:///home/hz/codebase/astroland_dots/plugin/src/globals.hpp#L98-L125),
[Astroland `main.cpp`](file:///home/hz/codebase/astroland_dots/plugin/src/main.cpp#L83-L106),
[Hyprland `render/types.hpp`](../../../Hyprland/src/render/types.hpp#L72-L107)

The future public state/effect contract should therefore carry:

- `master_seed`, `generator_version`, and `grammar_hash`;
- chunk dimensions, cache limits, and animation mode;
- monitor identity and monitor-local viewport size/scale;
- camera origin, zoom, transform, and revision;
- cursor position and whether the pointer is present on that monitor;
- hover kind, stable identity, monitor, and local coordinates;
- effect time or phase, separate from structural generation state.

## Verification requirements for a future implementation

- Generate the same chunk twice in fresh processes and compare tile IDs and boundary
  profiles byte-for-byte.
- Generate adjacent chunks in both orders and compare their shared edge data.
- Revisit chunks after evicting them and verify identical output.
- Change cursor, hover, and camera inputs without changing structural chunk hashes.
- Force contradiction and budget exhaustion and verify deterministic fallback without
  blocking the render path.
- Measure worst-case worker time, queue depth, CPU cache bytes, texture bytes, and
  full-screen animation cost on every supported monitor configuration.
- Verify that lock, fullscreen, occluded, and software-renderer behavior follows the
  eventual presentation boundary's fallback policy.

## Sources

- Maxim Gumin, [WaveFunctionCollapse](https://github.com/mxgmn/WaveFunctionCollapse),
  official repository and reference implementation.
- Marian Kleineberg, [Generating an infinite world with the Wave Function Collapse
  algorithm](https://marian42.de/article/infinite-wfc/), author description of
  deterministic chunk-based WFC.
- Hyprland source at local commit `efb50993780079460b0cbed1363e2166a2de1d9f`:
  [render stages](../../../Hyprland/src/SharedDefs.hpp), [renderer](../../../Hyprland/src/render/Renderer.cpp),
  [pass elements](../../../Hyprland/src/render/pass/Pass.cpp), and [input routing](../../../Hyprland/src/managers/input/InputManager.cpp).
- [wlr-layer-shell-unstable-v1.xml](https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/raw/master/unstable/wlr-layer-shell-unstable-v1.xml),
  official wlroots protocol XML.
- [Qt ShaderEffect](https://doc.qt.io/qt-6/qml-qtquick-shadereffect.html) and
  [Qt FrameAnimation](https://doc.qt.io/qt-6/qml-qtquick-frameanimation.html),
  first-party Qt documentation.
- Local [Astroland plugin](file:///home/hz/codebase/astroland_dots/plugin/src/main.cpp)
  and [Astroland QuickShell example](file:///home/hz/codebase/astroland_dots/.config/quickshell/ScreensaverComponent.qml).
