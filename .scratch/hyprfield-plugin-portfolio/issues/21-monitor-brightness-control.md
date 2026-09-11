# 21 — Monitor Brightness Control

**What to build:** Let users set and persist an independent scalar brightness value for each monitor through native Lua, with explicit handling for invalid requests, monitor lifecycle changes, unsupported operations, and incompatible output-color ownership.

**Blocked by:** 11 — Plugin Collection Foundation and Host Harness

**Status:** ready-for-agent

- [ ] Valid scalar brightness requests apply to the selected monitor and persist by stable monitor identifier.
- [ ] Invalid or unsupported operations report explicitly and preserve the last valid state.
- [ ] Hotplug and output-control failures do not apply stale state to the wrong monitor.
- [ ] Active incompatible CTM, gamma, ICC, or VCGT ownership is detected and refused without replacement or silent composition.
