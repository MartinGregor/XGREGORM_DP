---
name: ask-before-commit
description: Always ask the user before any git commit/push; log changes into CLAUDE.md
metadata:
  type: feedback
---
Before every `git commit` (and push) ask the user for permission first, even for small changes.
Record changes made to the project in the repo's CLAUDE.md (Slovak, same style as the rest of the file).

**Why:** user said so explicitly on 2026-10-07 after the initial push to [[thesis-git-repo]].
**How to apply:** prepare the change, show what would be committed, ask, commit only after "yes". Update CLAUDE.md as part of the change.
