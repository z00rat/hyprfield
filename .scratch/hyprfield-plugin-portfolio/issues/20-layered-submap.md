# 20 — Layered Submap

**What to build:** Let users declare a nested layered submap through native Lua and navigate it using generated root, child, parent, sibling, and Escape transitions while retaining Hyprland's existing binding semantics and visible configuration failures.

**Blocked by:** 11 — Plugin Collection Foundation and Host Harness

**Status:** ready-for-agent

- [ ] A valid declaration installs one root layer and nested layers with layer keys, bindings, optional hooks, and the root entry action.
- [ ] Parent/child navigation, permitted inferred sibling transitions, explicit overrides, and generated Escape reset work without raw input ownership.
- [ ] Existing action values and bind options pass through unchanged, including descriptions, repeating, locked, keyboard, and mouse behavior.
- [ ] Duplicate names or binding identities, missing targets, and invalid declarations are rejected before installation; cycles remain valid.
- [ ] Hooks run after applied transitions, cannot redirect or mutate navigation, and failures preserve state while reporting visibly.
