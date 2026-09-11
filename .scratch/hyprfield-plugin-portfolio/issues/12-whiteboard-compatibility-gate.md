# 12 — Whiteboard Compatibility Gate

**What to build:** Establish the smallest safe renderer/input compatibility seam required by Whiteboard before implementing product behavior. Prove the exact pinned Hyprland host version, hook registration, monitor scoping, damage, and unload cleanup with an unmistakable temporary render artifact and an offline seam test.

**Blocked by:** 11 — Plugin Collection Foundation and Host Harness

**Status:** resolved

## Acceptance criteria

- [x] The production plugin refuses an unsupported or unverified host/API version before installing renderer or input hooks and reports the reason.
- [x] On a supported live host, a temporary proof artifact is visibly monitor-scoped, is not presented as the Whiteboard product, and is removed cleanly on unload.
- [x] Hook registration and teardown are idempotent; unload leaves no listener, transform, damage, or input ownership behind.
- [x] The offline seam test exercises supported, unsupported, failed-registration, and teardown paths without contacting a running compositor.
- [x] The live verification command fails on plugin load, API dispatch, or compositor command failure instead of treating a notification as success.

## Scope boundary

This ticket proves the safety gate and seam only. It does not implement workspace assignment, window placement, camera transforms, or management gestures.

## Comments

- Implemented the exact `0.56.2` version and commit gate, renderer/input hook seam, monitor-labelled temporary proof API, idempotent cleanup, and offline host-harness coverage.
- Live verification passed on Hyprland `0.56.2`: plugin load, `hl.plugin.whiteboard.proof("current")`, and plugin unload each returned `ok`.
- The proof artifact is intentionally a temporary notification; product rendering and input behavior remain out of scope for this ticket.
