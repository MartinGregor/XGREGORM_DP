---
name: long-run-results
description: Where long simulation series keep logs and interim results, and how to resume them
metadata:
  type: project
---

Dlhé série behov: logy a skript v `xgregorm_logs/` v koreni `ns-3-dev` (nie v `/tmp`, ten reštart WSL zmaže). `JOBS=N ./xgregorm_logs/ser.sh` po prerušení preskočí dokončené behy. Priebežné výsledky hneď po každom scenári do Denníka v `CLAUDE.md`.

**Why:** 2026-11-02 reštart počítača zmazal logy prvej série; používateľ chce priebežné výsledky mať zapísané.
**How to apply:** pri dlhých behoch logy mimo `/tmp`, po každom dokončenom scenári zápis do Denníka. Počas behu nemeniť C++ ani skript (`./ns3 run` by prekompiloval, ďalšie behy by použili nový kód). Súvisí s [[ask-before-commit]] a [[machine-handover]].
