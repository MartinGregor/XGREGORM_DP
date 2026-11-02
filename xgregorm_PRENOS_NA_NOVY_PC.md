# Prenos na nový počítač: úloha pre Claude na NOVOM počítači

Aktualizované 2026-11-02 zo starého počítača. Starý počítač svoju časť dokončil: archívy sú v súkromnom GitHub Release
`prenos-2026-11-02` repozitára `MartinGregor/XGREGORM_DP`, kontrolné súčty sedia aj po spätnom stiahnutí.

**Cieľ:** rozbehať na novom počítači rovnaké prostredie ako na starom, overiť ho a pokračovať v sérii behov.

Komunikácia s používateľom je po slovensky. Pravidlá projektu sú v `CLAUDE.md`, najmä: pred každým commitom a pushom sa opýtať, zmeny
zapisovať do Denníka a `build/` po prvej kompilácii nikdy nemazať.

## Pozor na tri veci

- **Remote:** v rozbalenom archíve je `dp` = `MartinGregor/XGREGORM_DP` a `origin` = repozitár autorov VaN3Twin. **Na `origin` nikdy nič neposielaj.**
  V súčasnom klone na novom počítači môže byť `origin` niečo iné, takže pred pushom vždy over `git remote -v`.
- **`sudo` pýta heslo:** Claude ho nezadá. Požiadaj používateľa, aby príkaz spustil sám v samostatnom termináli Ubuntu.
- **Pamäť:** 280 áut zaberie ~6 GB na beh, viď krok 7. Nedostatok pamäte beh zabije.

## Kroky

### 1. Stiahnuť a overiť archívy

```bash
mkdir -p ~/xgregorm_prenos && cd ~/xgregorm_prenos
gh release download prenos-2026-11-02 --repo MartinGregor/XGREGORM_DP
sha256sum -c xgregorm_prenos.sha256      # oba súbory musia byť OK, inak skončiť a povedať používateľovi
```

### 2. Rozbaliť a nahradiť klon

V klone chýbajú časti ns-3, preto sa pracuje z archívu. Klon sa nemaže, len odloží, kým sa neoverí krok 6.

```bash
cd ~/VaN3Twin/ns-3-dev 2>/dev/null && git status --short && git log --oneline dp/main..HEAD 2>/dev/null
# ak má klon necommitnuté zmeny alebo vlastné commity, ukáž ich používateľovi a opýtaj sa, čo s nimi
cd ~ ; [ -e ~/VaN3Twin/ns-3-dev ] && mv ~/VaN3Twin/ns-3-dev ~/VaN3Twin/ns-3-dev.klon
mkdir -p ~/VaN3Twin && tar -xzf ~/xgregorm_prenos/xgregorm_prenos.tar.gz -C ~/VaN3Twin
cd ~/VaN3Twin/ns-3-dev
git remote -v
gh auth setup-git
git pull --ff-only dp main               # archív je na 1d507286, pull donesie tento návod a novšie zápisy
```

`git status` ukáže nesledovaný `xgregorm_logs/prostredie_stary_pc.md`. Je to v poriadku, súbor popisuje prostredie starého počítača.

Pamäť Claude: obsah `xgregorm_logs/claude_pamat/` skopíruj do `~/.claude/projects/<cesta-projektu>/memory/`, kde `<cesta-projektu>`
je absolútna cesta s lomkami nahradenými pomlčkami. Prepis konverzácie od začiatku je v `xgregorm_logs/claude_prepis_konverzacie.jsonl`, je to 45 MB, takže ho
prehľadávaj, nečítaj celý. Potom si prečítaj `xgregorm_STAV_PRACE.md`.

### 3. Závislosti

Over `lsb_release -ds`. Stará inštalácia: Ubuntu 24.04.5, gcc 13.3.0, CMake 3.28.3, Python 3.12.3.

**Ubuntu 24.04 (odporúčané):** archív `/usr/local` obsahuje SUMO 1.18.0 v Release aj gRPC 1.60.0. gRPC je povinné, lebo ho vyžaduje `src/carla/CMakeLists.txt`,
hoci CARLA nepoužívame. Používateľ spustí v termináli:

```bash
sudo apt update
grep -vxE 'ubuntu-wsl|ubuntu-minimal|init|base-files|login' ~/VaN3Twin/ns-3-dev/xgregorm_logs/apt_balicky.txt | xargs sudo apt install -y
sudo tar -xzf ~/xgregorm_prenos/xgregorm_usr_local.tar.gz -C /
sudo ldconfig
```

**Iná verzia Ubuntu:** archív `/usr/local` nepoužívať. Treba `apt` balíky zo zoznamu vyššie (názvy sa môžu líšiť), gRPC `v1.60.0` zo zdrojákov
a SUMO tag `v1_18_0` zo zdrojákov s `-DCMAKE_BUILD_TYPE=Release`. SUMO z repozitára Ubuntu **nepoužívať**, má zapnuté asserty.
`sandbox_builder.sh install-dependencies` robí aj gRPC, ale navyše kompiluje OpenCV, takže trvá dlho.

Na oboch cestách:

```bash
grep -q SUMO_HOME ~/.bashrc || echo 'export SUMO_HOME=/usr/local/share/sumo' >> ~/.bashrc
export SUMO_HOME=/usr/local/share/sumo
sumo --version | head -1                 # Version 1.18.0
ldd /usr/local/bin/sumo | grep 'not found'   # nesmie nič vypísať
```

### 4. Kompilácia

`build/` a `cmake-cache/` sa neprenášali.

```bash
cd ~/VaN3Twin/ns-3-dev
CXXFLAGS="-include cstdint" ./ns3 configure --build-profile=optimized --enable-examples --disable-python --disable-werror
./ns3 build
```

Overené zo starého archívu bol len `configure`. Celá kompilácia z archívu ešte neprebehla, takže prípadné chyby rieš a zapíš.

### 5. Overovací beh

```bash
python3 xgregorm_gen_scenario.py --vehicles 30 --sim-time 20 --run hybrid --name OVER > /tmp/over.log 2>&1
grep -E '^XGREGORM-V2V,|^XGREGORM-WIFIFRAMES|^  CSV: xgregorm' /tmp/over.log
rm -rf src/automotive/examples/xgregorm_OVER xgregorm_tmp_OVER
```

Referencia zo starého počítača (kód `1d507286`, beh 56 s):

```
XGREGORM-V2V,EXP:2129,RX:2129
XGREGORM-WIFIFRAMES,N:2966,BYTES:323086
  CSV: xgregorm-hybrid,30,5,20,nan,nan,nan,n/a,17.67,4.0
```

Rovnaké čísla znamenajú rovnaké správanie. Beh musí dobehnúť bez pádu. Ak sa čísla líšia, neznamená to automaticky chybu (iná verzia kompilátora alebo knižníc),
ale **sériu nespúšťaj a povedz to používateľovi**: výsledky by sa nedali priamo porovnať s behmi L a RA zo starého počítača.

### 6. Upratanie klonu

Až po úspešnom kroku 5 sa opýtaj používateľa, či môžeš zmazať `~/VaN3Twin/ns-3-dev.klon` a `~/xgregorm_prenos/`.

### 7. Spustenie série

Over dostupnú pamäť cez `free -g` a počet jadier cez `nproc`.

| Pamäť vo WSL | `JOBS` |
|---|---|
| ~19 GB (podľa skoršieho zápisu z nového počítača) | **2** |
| 28 GB | 4 |

Pri 19 GB by 3 behy po ~6 GB nechali menej než 1 GB rezervy, a RE ani RP zatiaľ nedobehli, takže ich vrchol pamäte nie je známy. Pri 32 GB RAM v počítači sa oplatí navrhnúť
používateľovi `memory=28GB` v `%UserProfile%\.wslconfig` a `wsl --shutdown` z Windows, a to **pred** spustením série, lebo reštart WSL zabije bežiace behy.
Treba aj vypnúť uspávanie počítača: na starom sa počas behu RE stratili takmer 2 hodiny.

```bash
cd ~/VaN3Twin/ns-3-dev
setsid nohup env JOBS=2 ./xgregorm_logs/ser.sh > /dev/null 2>&1 &
```

Skript preskočí hotové LA, LE, LP, RA a spustí **RE, RP, MA, ME, MP**. Čiastočný `RE_prerusene_pri_32s.txt` je nepoužiteľný. Po prerušení stačí spustiť skript znova.
Hotovo je, keď existuje `xgregorm_logs/serDone`. Pamäť sa zapisuje do `xgregorm_logs/mem.log`.

**Kým séria beží, nemeň C++ ani `ser.sh`:** `./ns3 run` by prekompiloval a ďalšie behy by išli s novým kódom.

### 8. Priebežné výsledky

Po každom dokončenom scenári zapíš výsledky do Denníka v `CLAUDE.md` vo formáte existujúcej tabuľky scenára L (používateľ výslovne chce priebežné zápisy).
Do záznamu napíš, že behy bežali na novom počítači. Pri časoch uvádzaj, koľko behov išlo naraz.

### 9. Po sérii: porovnávací test DCC

Používateľ ho výslovne žiadal.

1. Pridaj prepínač škálovania Toff, napríklad `--dcc-scale 0/1`, predvolene 1 (súčasné správanie). Funkcia je `xgdcc::toff` v
   `src/automotive/model/Applications/xgregorm-hybrid-socket.h`, ostatné voľby ako `--dcc` idú cez `xgregorm_gen_scenario.py` do `xgregorm-hybrid.cc`.
   Pri 0 sa pre Ton > 1 ms použije hodnota z tabuľky pre 1 ms bez násobenia Ton / 1 ms.
2. Over, že pri predvolenej hodnote je výstup bit po bite rovnaký (krok 5).
3. Spusti L a R (A, E, P) s `--dcc-scale 0` pod novými názvami behov, logy do `xgregorm_logs/`.
4. Porovnaj SPATEM a CPM cez 802.11p, podiel fronty DCC, skutočné PRR, podiel NR a presuny po 30. s.
5. Kľúčová otázka: **je SPATEM cez 802.11p 0.156 (RA) a výsledok E > A dôsledok môjho predpokladu?** Norma nemá tabuľku Toff pre Ton > 1 ms.

Ďalšie kroky sú v `xgregorm_STAV_PRACE.md`, časť 4.

## Čo povedať používateľovi

Po každom väčšom kroku stručne stav. Po kroku 5 výsledok porovnania s referenciou a po spustení série počet behov naraz a odhad času. Na starom počítači trval
RA 3 h 07 min sám a behy E a P boli doteraz dlhšie než A. Ak niečo zlyhá, ukáž presnú chybovú hlášku a nevymýšľaj hodnoty.
