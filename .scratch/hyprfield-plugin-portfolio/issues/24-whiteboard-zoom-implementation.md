# Whiteboard Zoom Implementation

Label: ready-for-human
Type: implementation-plan
Status: planned
Parent: ../map.md

## Goal

Add a safe zoomed-out whiteboard view that reveals more surrounding windows,
without deleting windows, changing workspaces, or corrupting the working 1x
placement and pan behavior.

## Working rules

- Implement one phase at a time.
- Run formatting, tidy, offline build, and tests after every phase.
- Report the phase and wait for review before starting the next phase.
- Never close or remove a client.
- Never move a client to another workspace.
- Preserve exact restoration on reset and plugin unload.

## Todo

- [x] Phase 1: separate persistent world rectangles from applied screen geometry.
- [x] Phase 2: add pure projection math for position, size, zoom, and pan.
- [x] Phase 3: test projection and minimum-size/viewport bounds offline.
- [x] Phase 4: apply projected geometry transactionally: resize, move, verify, rollback.
- [x] Phase 5: distribute all clients across deterministic 2D positions around the central grid.
- [x] Phase 6: add zoom API and reset behavior while preserving 1x compatibility.
- [x] Phase 7: add zoom/pan manual walkthrough and geometry snapshots.
- [ ] Phase 8: verify restoration and report live-test results.

## Proposed geometry model

Each client keeps a world rectangle independent of its current compositor
rectangle. The camera projects it as:

```text
screenCenter = monitorCenter
             + (worldCenter - monitorCenter) * zoom
             + pan
screenSize   = worldSize * zoom
```

At `zoom=1` and `pan=(0,0)`, projected geometry must equal the current working
2D placement exactly.

## Review gates

After each phase, report:

1. What changed.
2. What was verified.
3. What remains risky.
4. The exact next decision needed from the human.
