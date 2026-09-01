# Monitor Color Control Facts

Label: wayfinder:research
Type: research
Status: resolved
Parent: ../map.md
Blocked by: none

## Question

What do the current Hyprland and Wayland output-control protocols reliably provide for per-monitor gamma ramps, CTM/color temperature, brightness-like dimming, RGB adjustment, and inversion? Establish ownership/conflict behavior with ICC and other gamma clients, direct scanout implications, and the smallest supported external helper interface.

## Answer

Use a privileged external Wayland helper around Hyprland's CTM protocol for first-version per-monitor temperature, scalar dimming/gain, RGB gains, and identity. It must own the single CTM manager, send a complete matrix for every output on each commit, handle `blocked` and hotplug, and refuse to compose with ICC/VCGT or another color owner. Gamma control is an experimental, exclusive per-output fallback for custom ramps and possible inversion; inversion is not a CTM capability and should be unsupported initially. Native SDR brightness/saturation are SDR-to-HDR conversion settings, not general desktop filters; standard color management describes surfaces/output profiles rather than controlling output effects. Optional QuickShell UI should use a narrow helper IPC. Full-desktop shader effects cannot be promised across direct scanout. Detailed citations and source links: [`research/monitor-color-control-facts.md`](../research/monitor-color-control-facts.md).
