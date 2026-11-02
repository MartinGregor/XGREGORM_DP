---
name: thesis-git-repo
description: Where the thesis work is pushed - remote dp, branch main, without VaN3Twin history
metadata:
  type: project
---
Work is pushed to https://github.com/MartinGregor/XGREGORM_DP.git (remote `dp`, local branch `main`, tracks dp/main).
History starts fresh from 2026-10-07 (snapshot of VaN3Twin commit 0e70bb36), the authors' history is deliberately excluded.
Local `master` still holds the original VaN3Twin history; `origin` = DriveX-devs/VaN3Twin (upstream).

**Why:** user wanted only their own history in the thesis repo.
**How to apply:** commit on `main`, push with `git push` (to dp). Never push `master` to dp. Upstream updates must be brought in as file changes, not merged (it would pull the authors' history into dp).
