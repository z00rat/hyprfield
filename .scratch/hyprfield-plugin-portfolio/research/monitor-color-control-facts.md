# Monitor Color Control Facts

Research date: 2026-08-29

This note uses the checked-out Hyprland source at commit `efb50993780079460b0cbed1363e2166a2de1d9f`, the locally installed Wayland protocol definitions, and the first-party `hyprsunset` source. No compositor IPC or live compositor was used.

## Executive Findings

- The standard Wayland `wl_output` interface reports output geometry, transforms, and modes. It has no request for an output gamma ramp, CTM, brightness, RGB gain, or inversion. [`/usr/share/wayland/wayland.xml:2900-3038`](file:///usr/share/wayland/wayland.xml)
- `wp-color-management-v1` is about describing output color properties and describing surface content. Its output object lets a client read the current image description; its surface object lets a client set a surface image description. It is not a general output-effect control protocol. [`/usr/share/wayland-protocols/staging/color-management/color-management-v1.xml:31-58`](file:///usr/share/wayland-protocols/staging/color-management/color-management-v1.xml), [`...:634-701`](file:///usr/share/wayland-protocols/staging/color-management/color-management-v1.xml), [`...:704-790`](file:///usr/share/wayland-protocols/staging/color-management/color-management-v1.xml)
- Hyprland has two relevant privileged output-control protocols: `zwlr_gamma_control_manager_v1` for per-output gamma tables and `hyprland_ctm_control_manager_v1` for per-output 3x3 CTMs. Hyprland enables both in its protocol setup. [`Hyprland/CMakeLists.txt:541-560`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/CMakeLists.txt#L541-L560), [`Hyprland/src/managers/ProtocolManager.cpp:179-210`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/managers/ProtocolManager.cpp#L179-L210)
- The smallest useful external helper is therefore a Wayland client that owns the CTM manager, tracks `wl_output` globals, sends one matrix per output, and commits. `hyprsunset` is the first-party example of this shape. [`hyprsunset/src/Hyprsunset.cpp@d729fc124fc93d93f5ad795e59c4ff30377fc4d4`](https://github.com/hyprwm/hyprsunset/blob/d729fc124fc93d93f5ad795e59c4ff30377fc4d4/src/Hyprsunset.cpp#L66-L174)

## Capability Matrix

| Need | Supported mechanism | Reliable boundary |
| --- | --- | --- |
| Per-monitor gamma ramps | Privileged `zwlr_gamma_control_unstable_v1` | One exclusive gamma controller per output; hardware/output support is required; the protocol is experimental. |
| Temperature / white-point shift | Hyprland CTM protocol, normally a diagonal RGB matrix | One privileged CTM manager globally; matrix components must be finite and non-negative. |
| Brightness-like dimming | CTM scalar/diagonal gains, or gamma ramps; native `sdrbrightness` is narrower | Native `sdrbrightness` only participates in SDR-to-HDR conversion, not as a general desktop dimmer. |
| RGB adjustment | CTM channel gains/mixing, or independent gamma ramps | CTM is a linear 3x3 transform; there is no public arbitrary full-desktop shader control here. |
| Inversion | Descending gamma ramps can express it where the output accepts them | No dedicated protocol; CTM cannot express `1 - color` because it has no offset and rejects negative matrix values. |
| ICC display calibration | Hyprland monitor `icc` rule | Compositor-owned 3D LUT, plus optional KMS VCGT ramps; it is not an external output-control claim through standard Wayland. |

## Gamma Ramps

The `wlr-gamma-control-unstable-v1` definition is explicit about the ownership model:

- It is a privileged, experimental protocol for setting output gamma tables. [`Hyprland/protocols/wlr-gamma-control-unstable-v1.xml:29-41`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/protocols/wlr-gamma-control-unstable-v1.xml#L29-L41)
- A client requests a control for a particular `wl_output`, receives that output's `gamma_size`, and supplies three ramps in one file descriptor. The file must contain exactly three times the gamma size. [`.../wlr-gamma-control-unstable-v1.xml:43-64`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/protocols/wlr-gamma-control-unstable-v1.xml#L43-L64), [`...:66-104`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/protocols/wlr-gamma-control-unstable-v1.xml#L66-L104)
- There can be at most one control object per output. It has exclusive access, and destroying a valid control restores the original gamma tables. A client must handle `failed`, including unsupported gamma tables, another exclusive client, or compositor transfer. [`.../wlr-gamma-control-unstable-v1.xml:66-78`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/protocols/wlr-gamma-control-unstable-v1.xml#L66-L78), [`...:106-123`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/protocols/wlr-gamma-control-unstable-v1.xml#L106-L123)
- Hyprland rejects a `set_gamma` request if its monitor currently has ICC VCGT ramps in use. The rejection is sent as `failed`; malformed file sizes are a protocol error. [`Hyprland/src/protocols/GammaControl.cpp:49-99`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/protocols/GammaControl.cpp#L49-L99)
- Hyprland reads the fd as `uint16_t`, translates the protocol's planar `[red][green][blue]` data into Aquamarine's interleaved `[r,g,b]+` state, and applies it as the output gamma LUT. It does not provide a request to read the current ramp, so a helper must own or reconstruct its desired baseline. [`Hyprland/src/protocols/GammaControl.cpp:80-120`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/protocols/GammaControl.cpp#L80-L120), [`/usr/include/aquamarine/output/Output.hpp:70-108`](file:///usr/include/aquamarine/output/Output.hpp)
- Hyprland resets the LUT when the gamma control is destroyed, and reapplies a surviving control when a monitor connects. [`Hyprland/src/protocols/GammaControl.cpp:129-159`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/protocols/GammaControl.cpp#L129-L159), [`Hyprland/src/output/Monitor.cpp:378-389`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/output/Monitor.cpp#L378-L389)

The protocol does not define a monotonicity requirement for ramp values. Consequently, a descending ramp is a plausible way to implement per-channel inversion, but this remains conditional on the output/KMS gamma implementation accepting that table. It is not an inversion guarantee supplied by Wayland.

## CTM And Temperature

Hyprland's CTM protocol is the better fit for a small temperature helper:

- The protocol is explicitly privileged. It accepts a row-major 3x3 matrix, applies changes only on `commit`, permits values in `[0, infinity)`, and raises `invalid_matrix` for values outside that range. [`Hyprland/subprojects/hyprland-protocols/protocols/hyprland-ctm-control-v1.xml:33-73`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/subprojects/hyprland-protocols/protocols/hyprland-ctm-control-v1.xml#L33-L73)
- A CTM manager is global and exclusive. If another manager was already bound, later managers receive `blocked` and their `set_ctm_for_output` requests are silently ignored. [`.../hyprland-ctm-control-v1.xml:75-105`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/subprojects/hyprland-protocols/protocols/hyprland-ctm-control-v1.xml#L75-L105)
- Destroying the manager resets every output to identity. On each commit, every currently tracked monitor omitted from the client's pending map is also reset to identity. A helper must therefore send a complete per-output state on every commit, and must reapply state after hotplug. [`Hyprland/src/protocols/CTMControl.cpp:50-80`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/protocols/CTMControl.cpp#L50-L80), [`hyprsunset/src/Hyprsunset.cpp@d729fc124fc93d93f5ad795e59c4ff30377fc4d4`](https://github.com/hyprwm/hyprsunset/blob/d729fc124fc93d93f5ad795e59c4ff30377fc4d4/src/Hyprsunset.cpp#L100-L144)
- Hyprland validates every component for finiteness and non-negativity, stores the matrix by monitor name, and commits it to the monitor. [`Hyprland/src/protocols/CTMControl.cpp:20-64`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/protocols/CTMControl.cpp#L20-L64)
- The compositor stores the CTM, schedules a frame, and applies it either through the output state or through the rendering path. In fullscreen color-management cases it can combine the custom CTM with the color-conversion matrix when `render:non_shader_cm_interop` permits it. [`Hyprland/src/output/Monitor.cpp:1794-1798`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/output/Monitor.cpp#L1794-L1798), [`Hyprland/src/render/Renderer.cpp:2418-2468`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/render/Renderer.cpp#L2418-L2468), [`Hyprland/src/config/values/ConfigValues.cpp:565-581`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/config/values/ConfigValues.cpp#L565-L581)
- `hyprsunset` calculates a diagonal matrix from the requested Kelvin temperature, multiplies it by a scalar gamma matrix, sends that matrix for each output, and commits. This establishes a small first-party helper contract for temperature plus scalar dimming/gain. [`hyprsunset/src/Hyprsunset.cpp@d729fc124fc93d93f5ad795e59c4ff30377fc4d4`](https://github.com/hyprwm/hyprsunset/blob/d729fc124fc93d93f5ad795e59c4ff30377fc4d4/src/Hyprsunset.cpp#L40-L64), [`...:66-87`](https://github.com/hyprwm/hyprsunset/blob/d729fc124fc93d93f5ad795e59c4ff30377fc4d4/src/Hyprsunset.cpp#L66-L87)

A positive 3x3 CTM can express per-channel gains, positive cross-channel mixing, white-point matrices approximated by positive gains, and scalar dimming. It cannot express a general affine operation such as `1 - color`: there is no offset term, and negative coefficients are rejected.

## Native Hyprland Color Settings

Hyprland already has native monitor-rule values, but their scope is narrower than a general monitor filter:

- `sdrbrightness` and `sdrsaturation` are monitor-rule fields with default `1.0`; the native Lua monitor rule exposes both. [`Hyprland/src/config/shared/monitor/MonitorRule.hpp:51-56`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/config/shared/monitor/MonitorRule.hpp#L51-L56), [`Hyprland/src/config/lua/bindings/LuaBindingsConfigRules.cpp:118-142`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/config/lua/bindings/LuaBindingsConfigRules.cpp#L118-L142)
- The renderer only marks these as SDR modifications when the conversion is SDR-to-HDR. It passes them as saturation around target luminance and as a brightness multiplier. [`Hyprland/src/render/Renderer.cpp:1939-1975`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/render/Renderer.cpp#L1939-L1975), [`Hyprland/src/helpers/cm/ColorManagement.cpp:474-485`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/helpers/cm/ColorManagement.cpp#L474-L485)
- The shader implementation applies saturation by mixing RGB toward computed luminance and then multiplies RGB by the brightness value. The feature is part of the color-management conversion shader, not an always-on monitor post-process. [`Hyprland/src/render/shaders/glsl/cm_helpers.glsl:11-18`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/render/shaders/glsl/cm_helpers.glsl#L11-L18), [`...:251-260`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/render/shaders/glsl/cm_helpers.glsl#L251-L260)
- Values less than or equal to zero are treated as disabled and fall back to `1.0` in the renderer's settings construction. [`Hyprland/src/render/Renderer.cpp:1949-1975`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/render/Renderer.cpp#L1949-L1975)

Therefore these settings are useful for SDR-to-HDR presentation tuning, but are not a reliable API for a general per-monitor brightness, RGB, or inversion feature.

## ICC And Other-Client Conflicts

- A monitor rule with `icc` takes the ICC branch instead of applying explicit `cm`, SDR EOTF, luminance, brightness, and saturation settings. The comment in the compositor says explicit CM settings apply only when there is no ICC file. [`Hyprland/src/output/Monitor.cpp:652-697`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/output/Monitor.cpp#L652-L697)
- Hyprland parses the ICC into a compositor-side 3D LUT and can extract an optional VCGT table. VCGT-to-KMS is enabled by default through `render:icc_vcgt_enabled`; the ramp is resampled to the output gamma size and installed as the output gamma LUT. [`Hyprland/src/helpers/cm/ICC.cpp:179-231`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/helpers/cm/ICC.cpp#L179-L231), [`...:274-320`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/helpers/cm/ICC.cpp#L274-L320), [`Hyprland/src/output/Monitor.cpp:2605-2631`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/output/Monitor.cpp#L2605-L2631), [`Hyprland/src/config/values/ConfigValues.cpp:572-580`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/config/values/ConfigValues.cpp#L572-L580)
- Gamma control checks `gammaRampsInUse()`, which in this source means VCGT ramps are installed. Thus ICC with VCGT is an explicit gamma-client conflict. ICC without VCGT is not rejected by this particular check, but the compositor does not offer a general protocol for composing an external gamma client's state with an ICC policy. [`Hyprland/src/protocols/GammaControl.cpp:57-63`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/protocols/GammaControl.cpp#L57-L63), [`Hyprland/src/output/Monitor.cpp:2629-2631`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/output/Monitor.cpp#L2629-L2631)
- The standard color-management protocol does not let a client claim the output transform. Its output interface exposes an immutable image description for observation, while its ICC creator describes surface/display color characteristics; the compositor remains responsible for conversion. [`/usr/share/wayland-protocols/staging/color-management/color-management-v1.xml:440-470`](file:///usr/share/wayland-protocols/staging/color-management/color-management-v1.xml), [`...:634-701`](file:///usr/share/wayland-protocols/staging/color-management/color-management-v1.xml), [`...:730-771`](file:///usr/share/wayland-protocols/staging/color-management/color-management-v1.xml)
- Hyprland's current color-management implementation advertises ICC creation only in debug mode, otherwise returns `unsupported_feature`; even its debug path contains a `FIXME` for actually parsing the ICC file. This is not a dependable external monitor-control route. [`Hyprland/src/protocols/ColorManagement.cpp:27-30`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/protocols/ColorManagement.cpp#L27-L30), [`...:136-153`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/protocols/ColorManagement.cpp#L136-L153), [`...:489-518`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/protocols/ColorManagement.cpp#L489-L518)

Recommended conflict policy for a helper: claim one CTM manager, fail closed on `blocked`, do not overwrite or silently compose with an ICC/VCGT owner, and treat gamma-ramp mode as a separate exclusive mode. The Wayland protocols provide failure/reset semantics, not a shared color-state transaction across clients.

## Direct Scanout

- Hyprland attempts direct scanout before its normal compositor render. If it succeeds, the render function returns after `handleFullscreenSettings`; no desktop shader pass runs for that frame. [`Hyprland/src/render/Renderer.cpp:2071-2095`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/render/Renderer.cpp#L2071-L2095)
- Direct scanout requires a suitable solitary surface, matching size/transform, and a DMA-BUF. The scanout blocker explicitly considers color management: a surface that needs CM is blocked when an acceptable no-shader CM path is unavailable; HDR/scRGB conditions can also block it. [`Hyprland/src/output/Monitor.cpp:2010-2100`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/output/Monitor.cpp#L2010-L2100)
- Shader-only effects cannot be assumed to apply to a directly scanned surface. Output-state effects such as a committed KMS CTM or gamma LUT are the appropriate class of effect, but actual support still depends on the output backend and its test/commit result. Aquamarine models gamma LUT and CTM as output state, and Hyprland commits that state through the output. [`/usr/include/aquamarine/output/Output.hpp:70-108`](file:///usr/include/aquamarine/output/Output.hpp), [`Hyprland/src/render/Renderer.cpp:2473-2493`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/render/Renderer.cpp#L2473-L2493)
- Hyprland has an explicit `render:non_shader_cm_interop` setting for interaction between non-shader color management and the CTM protocol. Fullscreen handling may combine the custom CTM with the color conversion matrix, or temporarily set identity when interoperation is disabled. [`Hyprland/src/config/values/ConfigValues.cpp:580-581`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/config/values/ConfigValues.cpp#L580-L581), [`Hyprland/src/render/Renderer.cpp:2418-2468`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/render/Renderer.cpp#L2418-L2468)

The product implication is to avoid promising that a wallpaper shader, compositor plugin shader, or native SDR conversion setting affects every fullscreen/direct-scanout frame. A helper using CTM/gamma output state has the right layer, but must report unsupported output commits rather than pretending to guarantee coverage.

## Smallest Helper Interface

### Wayland side

1. Connect with `wl_display_connect`.
2. Bind `hyprland_ctm_control_manager_v1` at version 1 or 2, and bind each `wl_output` global.
3. Keep one desired matrix per output; on every update send all current outputs with `set_ctm_for_output`, then send `commit`.
4. Handle the version-2 `blocked` event, output removal, output addition, Wayland dispatch/flush, and manager destruction.

This is exactly the shape used by `hyprsunset`; its only control state is temperature, scalar gamma, and identity, and its Wayland operation is matrix-per-output plus commit. [`hyprsunset/src/Hyprsunset.cpp@d729fc124fc93d93f5ad795e59c4ff30377fc4d4`](https://github.com/hyprwm/hyprsunset/blob/d729fc124fc93d93f5ad795e59c4ff30377fc4d4/src/Hyprsunset.cpp#L66-L174), [`hyprsunset/src/main.cpp@d729fc124fc93d93f5ad795e59c4ff30377fc4d4`](https://github.com/hyprwm/hyprsunset/blob/d729fc124fc93d93f5ad795e59c4ff30377fc4d4/src/main.cpp#L1-L101)

### Optional UI side

The helper needs no plugin API, `hyprctl`, compositor fork, or arbitrary shader endpoint. An optional QuickShell surface can talk to a narrow private helper IPC with commands such as:

```text
set-output <output-id> temperature <kelvin>
set-output <output-id> gamma <percent>
set-output <output-id> identity
get-state
```

If gamma ramps or inversion are later required, add a separately selected gamma-control backend rather than silently mixing it with CTM/ICC ownership. The smallest first version should expose temperature, scalar dimming/gain, and identity through CTM, with inversion explicitly unsupported.

## Sources

- [Hyprland source at `efb50993780079460b0cbed1363e2166a2de1d9f`](https://github.com/hyprwm/Hyprland/tree/efb50993780079460b0cbed1363e2166a2de1d9f)
- [Hyprland `hyprland-ctm-control-v1.xml`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/subprojects/hyprland-protocols/protocols/hyprland-ctm-control-v1.xml)
- [Hyprland `wlr-gamma-control-unstable-v1.xml`](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/protocols/wlr-gamma-control-unstable-v1.xml)
- Local installed core protocol: `/usr/share/wayland/wayland.xml`
- Local installed official staging protocol: `/usr/share/wayland-protocols/staging/color-management/color-management-v1.xml`
- [First-party `hyprsunset` source at `d729fc124fc93d93f5ad795e59c4ff30377fc4d4`](https://github.com/hyprwm/hyprsunset/tree/d729fc124fc93d93f5ad795e59c4ff30377fc4d4)
- Local installed Aquamarine output abstraction: `/usr/include/aquamarine/output/Output.hpp`
