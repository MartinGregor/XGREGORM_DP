# Stav práce a prenos na iný počítač (xgregorm)

Zapísané 2026-11-02 po zastavení behov. Tento súbor hovorí, **kde sa práca zastavila, ako v nej pokračovať
a čo všetko sa spravilo**. Podrobné tabuľky a závery sú v Denníku v `CLAUDE.md`, pravidlá projektu tiež tam.

## 1. Kde sa to zastavilo

Séria s plným realizmom bola zámerne zastavená **2026-11-02 o 15:39** kvôli prenosu na výkonnejší počítač.

| Beh | Autá | Výber technológie | Stav |
|---|---|---|---|
| LA, LE, LP | 140 | žiadny, kvalita, predikcia | hotové, výsledky v Denníku |
| RA | 280 | žiadny | hotové, výsledky v Denníku |
| **RE** | 280 | kvalita | **zastavené v 32. s zo 60**, treba pustiť znova od začiatku |
| RP | 280 | predikcia | nespustené |
| MA, ME, MP | 280, zmes výbavy | žiadny, kvalita, predikcia | nespustené |
| Porovnávací test DCC | 140 a 280 | žiadny, kvalita, predikcia | nespustený, prepínač ešte nie je v kóde |

ns-3 nevie pokračovať z rozbehnutého behu, takže čiastočné RE sa nedá použiť. Jeho výstup ostal len na kontrolu
v `xgregorm_logs/RE_prerusene_pri_32s.txt` a meranie sa začína až od 30. s.

## 2. Trvanie behov a koľko ich išlo naraz

Pôvodný počítač: WSL2, 8 logických jadier, 7.8 GB RAM. Čas = reálny čas z logu (`Wall clock`). Pamäť = najviac použitej
RAM v celom systéme podľa `xgregorm_logs/mem.log` (nameraná raz za minútu; bez simulácie ~0.8 GB).

| Beh | Naraz | Čas | Pamäť systému |
|---|---|---|---|
| LA | 3 | 1 h 08 min (4062 s) | 6.2 GB spolu za 3 behy, ~1.8 GB na beh |
| LE | 3 | 1 h 21 min (4840 s) | to isté |
| LP | 3 | 1 h 20 min (4827 s) | to isté |
| RA | 1 | 3 h 07 min (11 198 s) | 6.8 GB, ~6 GB na beh |
| RE, zastavené v 32. s | 1 | 2 h 26 min skutočného výpočtu | 5.7 GB zatiaľ |

- **RE:** log ukazuje 15 941 s, ale v tom je **pauza 1 h 59 min** (12:32 až 14:31, medzera v `mem.log`, procesy v tom čase
  nebežali; najskôr uspaný počítač). Skoršia informácia „~4 h 20 min“ túto pauzu zahŕňala.
- **Limit 7.8 GB:** 140 áut najviac 3 naraz, 280 áut len 1. Staršie pokusy: 2 behy s 280 autami a 4 behy (2026-10-20) vyčerpali pamäť.
- **Bez `--coex`, DCC a telefónov** trval beh s 280 autami 1 h 58 min (A) a 2 h 56 min (E), s `--coex` 2.4-3.1 h.

### Odhad pre počítač s 32 GB (neoverený)

| Prostredie | Pamäť pre simuláciu | 280 áut naraz |
|---|---|---|
| Linux priamo | ~30 GB | **4** (5 by nechalo príliš malú rezervu) |
| WSL2 s `memory=28GB` v `.wslconfig` | 28 GB | **4** |
| WSL2 bez nastavenia (zvyčajne polovica RAM) | ~16 GB | **2** |

Jeden beh nevyužije viac než približne jedno jadro, takže viac jadier nezrýchli beh, iba dovolí viac behov naraz.
Počet jadier druhého počítača nie je známy, `JOBS` nemá byť väčší než ich počet. Pri prvom spustení sledovať `mem.log`.

## 3. Prenos na druhý počítač

**Samotný `git clone` nestačí.** `.gitignore` vynecháva veľkú časť ns-3 (napr. `src/lte`, `src/core`, `src/wifi`,
`src/internet`, `build-support`) a moja oprava `src/lte/model/nr-sl-ue-rrc.cc` je v gite len ako diff. V čase zápisu navyše
6 commitov nie je na `dp` a zmeny od 2026-10-23 nie sú commitnuté. Preto prenos cez **archív `~/VaN3Twin/xgregorm_prenos.tar.gz`**,
ktorý obsahuje všetko okrem `build/`, `cmake-cache/` a starých `.pcap`.

Zdrojové prostredie: Ubuntu 24.04.5, gcc 13.3.0, CMake 3.28.3, Python 3.12.3, SUMO 1.18.0, profil `optimized`.

```bash
mkdir -p ~/VaN3Twin && tar -xzf xgregorm_prenos.tar.gz -C ~/VaN3Twin
cd ~/VaN3Twin/ns-3-dev

# 1. závislosti (xgregorm_logs/apt_balicky.txt = ručne nainštalované balíky pôvodného systému)
./sandbox_builder.sh install-dependencies

# 2. SUMO 1.18.0 skompilované zo zdrojákov v Release, nie z repozitára Ubuntu (má zapnuté asserty, viď CLAUDE.md)
#    pôvodne: /usr/local/bin/sumo, SUMO_HOME=/usr/local/share/sumo
sumo --version

# 3. prvá kompilácia, build/ sa neprenáša (mimo tohto kroku build/ nikdy nemazať)
CXXFLAGS="-include cstdint" ./ns3 configure --build-profile=optimized --enable-examples --disable-python --disable-werror
./ns3 build

# 4. overenie krátkym behom
python3 xgregorm_gen_scenario.py --vehicles 30 --sim-time 20 --run hybrid

# 5. pokračovanie série: hotové preskočí, dobehne RE, RP, MA, ME, MP
setsid nohup env JOBS=4 ./xgregorm_logs/ser.sh > /dev/null 2>&1 &
```

Pri pokračovaní v Claude Code: spustiť ho v `~/VaN3Twin/ns-3-dev`, `CLAUDE.md` sa načíta sám a odkazuje sem. Pamäť Claude je
skopírovaná v `xgregorm_logs/claude_pamat/`, celý prepis konverzácie v `xgregorm_logs/claude_prepis_konverzacie.jsonl`
(len v archíve, git ho ignoruje).

## 4. Čo robiť ďalej

1. Dobehnúť sériu (krok 5) a výsledky R a M zapísať do Denníka. Hotovo je, keď existuje `xgregorm_logs/serDone`.
2. **Porovnávací test DCC** (požiadavka používateľa): pridať prepínač škálovania v `xgdcc::toff`
   (`src/automotive/model/Applications/xgregorm-hybrid-socket.h`), kde predvolené správanie ostane bit po bite rovnaké.
   Spustiť s prepínačom vypnutým scenáre L a R (A, E, P) a porovnať SPATEM a CPM cez 802.11p, podiel fronty, PRR,
   podiel NR a presuny po rozbehu. **Treba zistiť, či za SPATEM 0.156 a výsledok E > A nemôže môj predpoklad**: norma nemá tabuľku Toff
   pre Ton > 1 ms a ja škálujem Toff úmerne Ton, najviac na 1 s.
3. Overiť, prečo je LTE PRB také vysoké (~0.5-0.6 pri ~2-3 Mbit/s v 10 MHz).
4. Spraviť scenár, kde sa kvalita počas merania mení (vlna áut, výpadok). V scenári L po 30. s nenastal žiadny presun,
   takže predikcia nemala o čom rozhodovať.
5. Zopakovať kľúčové porovnania s viacerými seedmi (pravidlo projektu: min. 5 behov). Všetky výsledky hybridu s 280 autami sú zatiaľ z 1 behu.
6. Commit a push: **vždy sa najprv opýtať** (`dp`/`main`, nikdy `origin`).

## 5. Čo všetko sa spravilo od prvého spustenia

Prvý záznam je z 2026-10-07, keď sa overovali scenáre na už nainštalovanom VaN3Twin. Inštalácia pred týmto dátumom zaznamenaná nie je.

| Dátum | Čo sa spravilo | Výsledok |
|---|---|---|
| 10-07 | Repozitár `XGREGORM_DP`, generátor, mapa `xgregorm_mesto`, overenie 5 simulácií autorov | Všetky dobehli, výsledky deterministické |
| 10-07 | Realistické rozmery mapy (bloky 200 m, 1 pruh, obchvat) | 10 áut 51 km/h, 0 teleportov |
| 10-07 | Príčiny nízkeho V2I PRR a brzdenia trafficManagera | Synchronizované CAM; viac RSU sa pretláčalo o rovnaké semafory |
| 10-07 | Lokálne riadenie: každá RSU len svoj semafor | Max státie 107 → 20 s (10 áut) |
| 10-11 | Koordinácia RSU po kábli, oprava NaN slučky, `--seed` | Koordinácia nepomáha, ostáva vypnutá |
| 10-11 | Oprava A: desynchronizácia CAM | V2I PRR 0.660 → 0.996 (10 áut) |
| 10-12 | Algoritmy CAM 1-3 podľa ETSI EN 302 637-2 | Ostala len relevancia (CAM −56 %, bez beaconu −39 %) |
| 10-12 | Hybrid 802.11p + LTE + NR-V2X, výpadky technológií | Prepínanie funguje, pri výpadku 802.11p 100 % CAM na NR |
| 10-13 | ~50 behov prepínania, realizmus dosahu, `--channel 3gpp`, viac buniek | Opravy; viac buniek LTE je v ns-3 nespoľahlivých |
| 10-13 | Pravidlá hustoty P1-P1d | Odstránené, kód v `xgregorm_kod_zaloha/` |
| 10-13 | Mapa city-center (9 križovatiek, 3 pruhy) | Predvolených 600 áut v SUMO, 0 teleportov |
| 10-14 | Meranie záťaže (CBR, PRB, odhad SL-CBR), výkon podľa záťaže, PIR | Len meranie, správanie bez zmeny |
| 10-19 | Farby áut podľa technológie, výpis LIVE, test 500 áut | LTE pri výpadku 802.11p spomalí (p50 19 → 205 ms) |
| 10-20 | Opravy 5G-LENA pri > 255 NR zariadeniach | 500 áut nepadá, pri 100 % áut na NR výpočtovo nepoužiteľné |
| 10-20 | **Prvý šum:** budovy, mestský kanál | Do 100 m kópia 802.11p 0.83, NR 0.996 |
| 10-20 | Podpis správ, SPaT/MAPEM, CPM | CAM 118 → 241 B; CBR 0.03 → 0.25 |
| 10-20 | Výber podľa záťaže, prevádzka v bunke LTE | CBR ≥ 0.3: 37 → 24 % času, PRR bez zmeny |
| 10-20 | Výber podľa nameranej kvality | PRR 0.892 → 0.899 |
| 10-23 | Oprava obrátených fáz semaforov | Na konci behu 9.5 → 32 km/h |
| 10-23 | Rýchle úniky, šumové číslo NR 9 dB, rušenie LTE +3 dB | Doručenie 150-200 m: 0.59 → 0.37 |
| 10-23 | `--warmup`, meranie strát pri prepnutí | 280 áut, 30-60 s: PRR 0.903 → 0.918 s výberom podľa kvality |
| 11-02 | Vzájomné rušenie 802.11p a NR | PRR bez výberu 0.903 → 0.890 |
| 11-02 | Prediktívny výber | S rušením 0.913, kvalita 0.909, bez výberu 0.890 (1 beh, rozdiel v rozptyle) |
| 11-02 | DCC, limit CR, zmes výbavy, telefóny, šírka LTE | Krátke testy, potom séria nižšie |
| 11-02 | Séria s plným realizmom: L hotové, RA hotové | L: 0.946 / 0.967 / 0.967; RA: 0.901, SPATEM cez 802.11p 0.156 |

**Stav kódu pri zastavení:** posledný commit `18b4d5ae` (2026-10-23), necommitnuté `--coex`, `--pred-rat`, DCC, limit CR, výbava,
telefóny, `--lte-bw`, `CLAUDE.md`, `.gitignore`, tento súbor a `xgregorm_logs/`.

## 6. Čo je v `xgregorm_logs/`

| Súbor | Obsah |
|---|---|
| `ser.sh` | Skript série, pokračuje po prerušení, `JOBS` = behov naraz |
| `LA.log`, `LE.log`, `LP.log`, `RA.log` | Úplné výstupy hotových behov |
| `RE_prerusene_pri_32s.txt` | Výstup zastaveného behu, nepoužiteľný ako výsledok |
| `mem.log` | Pamäť každú minútu počas série |
| `apt_balicky.txt` | Ručne nainštalované balíky pôvodného systému |
| `claude_pamat/` | Kópia pamäte Claude |
| `claude_prepis_konverzacie.jsonl` | Celý prepis konverzácie od 2026-10-07 (len v archíve) |
