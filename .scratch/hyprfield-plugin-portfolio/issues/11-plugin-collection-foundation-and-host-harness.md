# 11 — Plugin Collection Foundation and Host Harness

**What to build:** Establish the shared collection foundation so independent plugin targets can be built, loaded, unloaded, and invoked through stable native Lua APIs, with a controlled host-backed test seam and exact Hyprland version-pin validation.

**Blocked by:** None — can start immediately

**Status:** ready-for-agent

- [ ] Independent plugin targets build and package separately while the existing example plugin continues to work.
- [ ] A controlled host-backed harness observes public Lua API behavior, lifecycle, and notifications without contacting a user's running compositor.
- [ ] Exact Hyprland version pins are validated and pin changes require an explicit compatibility check.
