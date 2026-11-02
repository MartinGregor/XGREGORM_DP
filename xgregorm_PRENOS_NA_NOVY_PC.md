# Prenos na nový počítač: úloha pre Claude na STAROM počítači

Zapísané 2026-11-02 z nového počítača. Ak čítaš toto na **starom počítači** (tam, kde je funkčný
`~/VaN3Twin/ns-3-dev`), vykonaj kroky nižšie. Používateľ o to požiadal.

## Prečo je to potrebné

Nový počítač má len klon repozitára. V gite chýbajú časti ns-3 (`.gitignore`: `src/core`, `src/wifi`, `src/lte`,
`build-support`, `ns3`, `CMakeLists.txt` a ďalšie), oprava `src/lte/model/nr-sl-ue-rrc.cc` je len ako diff a prepis
konverzácie je tiež len lokálne. Viac v `xgregorm_STAV_PRACE.md`, časť 3.

**Nerob `git add -f` ani nemeň `.gitignore`.** Archív ide do **GitHub Release** súkromného repozitára, nie do histórie
gitu (limit 100 MB na súbor v gite, 2 GB na súbor v Release).

## Pozor na remote

Na starom počítači `dp` = `MartinGregor/XGREGORM_DP`, `origin` = repozitár autorov VaN3Twin. **Na `origin` nikdy nič
neposielaj.** Vo všetkých príkazoch nižšie over repozitár cez `git remote -v`.

## Kroky

```bash
cd ~/VaN3Twin/ns-3-dev

# 1. Nič nesmie bežať, inak by archív zachytil rozrobený stav.
pgrep -af 'hybrid-optimized|ser.sh'     # ak niečo beží, zastav sa a opýtaj sa používateľa

# 2. Stav kódu. Ak git status ukáže necommitnuté zmeny, ukáž ich používateľovi a opýtaj sa, či ich commitnúť.
git fetch dp
git status --short
git pull --ff-only dp main              # stiahne aj tento súbor

# 3. Vždy nový archív; existujúci xgregorm_prenos.tar.gz môže byť starší než posledné commity.
cd ~/VaN3Twin
tar --exclude='ns-3-dev/build' --exclude='ns-3-dev/cmake-cache' --exclude='ns-3-dev/xgregorm_tmp_*' \
    --exclude='*.pcap' -czf xgregorm_prenos.tar.gz ns-3-dev
ls -lh xgregorm_prenos.tar.gz
sha256sum xgregorm_prenos.tar.gz | tee xgregorm_prenos.sha256

# 4. Kontrola tajných údajov: archív obsahuje aj prepis konverzácie.
#    Ak niečo nájde, neposielaj a povedz to používateľovi (súbor a typ údaja, nie samotnú hodnotu).
tar -xzOf xgregorm_prenos.tar.gz --wildcards '*claude_prepis_konverzacie.jsonl' 2>/dev/null \
  | grep -cE 'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_|BEGIN [A-Z ]*PRIVATE KEY|sudo -S'

# 5. Ak chýba gh: sudo apt install -y gh, potom gh auth login --web --git-protocol https
gh auth status

# 6. Nahratie, repozitár je súkromný. Ak je archív nad 1.9 GB: split -b 1900M -d, nahraj všetky časti.
gh release create prenos-2026-11-02 xgregorm_prenos.tar.gz xgregorm_prenos.sha256 \
  --repo MartinGregor/XGREGORM_DP --prerelease \
  --title "Prenos prostredia na nový počítač" \
  --notes "Archív ns-3-dev bez build/, pozri xgregorm_PRENOS_NA_NOVY_PC.md"

# 7. Overenie
gh release view prenos-2026-11-02 --repo MartinGregor/XGREGORM_DP
```

Na záver povedz používateľovi: „Archív je nahratý, na novom počítači napíš: pokračuj v prenose.“ Sériu na starom počítači
nespúšťaj a nič nemaž.

Do repozitára ani do archívu nedávaj heslá. Ak je v kroku 4 nález, čaká sa na rozhodnutie používateľa.

## Čo spraví Claude na novom počítači potom

1. `gh release download prenos-2026-11-02 --repo MartinGregor/XGREGORM_DP`, overiť `sha256sum -c`.
2. Rozbaliť do `~/VaN3Twin`, ďalej podľa `xgregorm_STAV_PRACE.md`, časť 3 (závislosti, SUMO 1.18.0, kompilácia, overovací beh).
3. Pustiť sériu. Nový počítač má vo WSL 12 jadier a 19 GB RAM, teda `JOBS=3` (280 áut ~6 GB na beh).
