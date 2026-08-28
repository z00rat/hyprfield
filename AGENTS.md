## Agent skills

### Issue tracker

Issues and specs live as local markdown files under `.scratch/<feature>/`. See `docs/agents/issue-tracker.md`.

### Triage labels

The canonical triage labels are `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, and `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

This is a single-context repo using root `CONTEXT.md` and `docs/adr/`. See `docs/agents/domain.md`.

### Runtime safety

When verifying plugins, read `docs/agents/runtime-safety.md` before using `hyprctl` or otherwise contacting the running Hyprland compositor.

### Code verification

After editing source files, run `just format` followed by `just tidy`. Run both before reporting the work complete.
