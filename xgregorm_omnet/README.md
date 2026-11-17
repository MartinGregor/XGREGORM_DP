# Ten istý scenár v ns-3 aj v OMNeT++

Tento priečinok spája moju simuláciu vo VaN3Twin (ns-3 + SUMO) s druhým bežným simulátorom V2X, OMNeT++ s modulom Veins.
Oba simulátory používajú ten istý SUMO a ten istý scenár.

**Spustenie jedným príkazom:** zadáš parametre a skript spustí ns-3 aj OMNeT++ na rovnakej mape, s rovnakými autami
a rovnakým seedom SUMO. Výsledky vypíše vedľa seba v rovnakom formáte.

```bash
cd ~/VaN3Twin/ns-3-dev
python3 xgregorm_omnet/xgregorm_compare.py --name CMP --vehicles 140 --sim-time 60 --warmup 20 --seed 10 --tx-power 30
python3 xgregorm_omnet/xgregorm_compare.py --name URB --vehicles 140 --sim-time 60 --warmup 20 --seed 10 --tx-power 23 --channel urban
```

## Čo znamená „dá sa spustiť v oboch“

Simulácia V2X má päť vrstiev. Aby rovnaký pokus dal v oboch simulátoroch porovnateľné čísla, musí byť každá vrstva
rovnaká alebo aspoň rovnako nastavená:

| vrstva | ns-3 (VaN3Twin) | OMNeT++ | stav |
|---|---|---|---|
| 1. scenár (mapa, autá, RSU) | `xgregorm_gen_scenario.py` → súbory SUMO | tie isté súbory (`export_scenario.py` ich skopíruje) | **hotové, rovnaké** |
| 2. pohyb áut | SUMO cez TraCI, krok 0,1 s | SUMO cez TraCI (`veins_launchd`), krok 0,1 s, rovnaký seed | **hotové, rovnaké** (počet očakávaných príjmov sa líši o < 0,3 %) |
| 3. rádio 802.11p | Yans, LogDistance (exponent 3), pri `--channel urban` 3GPP TR 37.885 s budovami a Nakagami | Veins PhyLayer80211p, LogDistance cez BreakpointPathloss, pri `--channel urban` môj `XgV2vUrban` (to isté ako ns-3) | **hotové pre oba kanály** (`authors`, `urban`) |
| 4. sieťový zásobník ETSI | VaN3Twin: GeoNetworking, BTP, CAM/CPM/DENM, podpis, DCC | Veins: len 802.11p MAC/PHY a jednoduché správy. Plný ETSI zásobník má **Artery** (Vanetza) nad Veins | CAM podľa pravidiel ETSI je v OMNeT++ moja aplikácia `XgCamApp`; CPM, SPATEM, DCC zatiaľ nie |
| 5. ďalšie siete (LTE, NR-V2X) a výber siete | LTE (ns-3), NR-V2X sidelink (5G-LENA), môj prepínač a pravidlo kvality | **Simu5G** (LTE, 5G NR vrátane D2D) cez INET a `veins_inet`; Artery-C pre LTE-V2X mode 4 | **nie je** – v OMNeT++ je zatiaľ len 802.11p |
| meranie | `XGREGORM-V2V`, `XGREGORM-RANGE`, ... | tie isté riadky z `XgCamApp` | **hotové, rovnaký formát a definícia** |

Výsledok sa preto dá porovnať v prípade, v ktorom majú oba simulátory všetko rovnaké: **len 802.11p, CAM podľa ETSI,
podpis ako model veľkosti, 6 Mbit/s, semafory s pevným programom SUMO**. Kanál môže byť jednoduchý (LogDistance, bez
budov) alebo mestský s budovami podľa 3GPP, ako v práci.

Semafory s pevným programom sú nutné. V ns-3 by cestná jednotka riadila semafor podľa prijatých CAM, takže by rádio
zmenilo premávku a autá by sa v oboch simulátoroch pohybovali inak. Preto `--tls-radius -1`.

Na úplné prenesenie môjho výberu siete do OMNeT++ treba:

1. Doplniť Simu5G + INET + `veins_inet`. Simu5G má LTE aj 5G NR a podporuje priamu komunikáciu medzi autami (D2D),
   NR-V2X mode 2 (výber zdrojov bez stanice) však v rovnakej podobe ako 5G-LENA nemá.
2. Prepínač pod GeoNet, ktorý každú správu pošle jednou alebo viacerými sieťami, ako `XgHybridSocket` v ns-3.
3. Senzorové správy (CPM), správy zo semafora a pravidlá proti preplneniu (DCC), ako v ns-3.

Už hotové:
- mestský kanál 3GPP pre 802.11p vo Veins (`XgV2vUrban`),
- jadro pravidla kvality oddelené od simulátora (`xgregorm-qos-core.h`), volá ho ns-3 aj OMNeT++.

Bod 1 je inštalácia, body 2 a 3 sú programovanie. Odhad: niekoľko týždňov práce, preto to nie je súčasť tejto práce.

## Porovnanie (overené 2026-11-17)

**Pokus 1:** city-center 3×3, 140 áut, 60 s, štatistiky od 20 s, seed 10, 30 dBm, citlivosť −88 dBm.

| vzdialenosť | ns-3 auto-auto | OMNeT++ auto-auto | ns-3 auto-RSU | OMNeT++ auto-RSU |
|---|---|---|---|---|
| 0–50 m | 0,998 | 0,996 | 0,999 | 0,999 |
| 50–100 m | 0,993 | 0,990 | 0,991 | 0,993 |
| 100–150 m | 0,987 | 0,976 | 0,992 | 0,981 |
| 150–200 m | 0,956 | 0,928 | 0,961 | 0,917 |
| 200–250 m | 0,343 | 0,669 | 0,295 | 0,606 |
| 250 m a viac | 0 | 0 | 0 | 0 |

| | ns-3 | OMNeT++ |
|---|---|---|
| PRR auto-auto do 100 m | 0,9957 | 0,9936 |
| priemerné oneskorenie | 0,48 ms | 0,43 ms |
| očakávané príjmy do 100 m | 181 751 | 181 621 |
| CAM áut | asi 25 280 | 25 339 |
| čas behu | 167 s | 7 s |

**Pokus 2** (`xgregorm_compare.py`): 80 áut, 40 s, od 10 s, seed 3, 23 dBm. Do 50 m vyšlo 1,000 v ns-3 a 0,992
v OMNeT++, pri 50–100 m 0,991 a 0,988, pri 100–150 m 0,517 a 0,735. Celkovo do 100 m 0,9969 a 0,9909.

**Čo z toho vyplýva:**

- Pohyb áut je v oboch simulátoroch rovnaký: počty očakávaných príjmov sa líšia o menej ako 0,3 %.
- Do 100 m sa doručenie zhoduje na desatiny percenta.
- Na okraji dosahu sa simulátory líšia. Je to známy rozdiel v modeli príjmu slabého rámca. ns-3 navyše vyžaduje na
  detekciu preambuly SNR aspoň 4 dB (`ThresholdPreambleDetectionModel`) a používa iný chybový model, Veins prijme každý
  rámec nad citlivosťou, ktorý prejde chybovým modelom NIST.
- Zblízka stráca Veins o niečo viac (0,8 % do 50 m pri 23 dBm). Pravdepodobne pre iné správanie pri zrážke dvoch rámcov
  (ns-3 vie zachytiť silnejší rámec, Veins nie). Neoverené.

**Pokus 2b: mestský kanál s budovami, ako v práci** (`--channel urban`). Vo Veins je to môj model `XgV2vUrban`
(`src/XgV2vUrban.*`), ktorý počíta to isté ako ns-3:
- 3GPP TR 37.885 V2V Urban: budova medzi stanicami znamená NLOS, inak LOS alebo NLOSv s pravdepodobnosťou podľa
  vzdialenosti, stav dvojice sa obnovuje každých 100 ms,
- útlm LOS, NLOS a NLOSv vrátane blokovania autami podľa výšky antén,
- korelované tienenie 3 a 4 dB,
- Nakagami-m podľa vzdialenosti.

Budovy sú tie isté kvádre (`xgregorm_buildings.txt`), prepočítané do súradníc OMNeT++. Nastavenie: 140 áut, 60 s,
od 20 s, seed 10, 23 dBm.

| vzdialenosť | ns-3 auto-auto | OMNeT++ auto-auto | ns-3 auto-RSU | OMNeT++ auto-RSU |
|---|---|---|---|---|
| 0–50 m | 0,990 | 0,986 | 0,986 | 0,981 |
| 50–100 m | 0,863 | 0,853 | 0,981 | 0,962 |
| 100–150 m | 0,569 | 0,562 | 0,907 | 0,885 |
| 150–200 m | 0,308 | 0,301 | 0,562 | 0,554 |
| 200–250 m | 0,196 | 0,192 | 0,419 | 0,404 |
| 300–350 m | 0,080 | 0,078 | 0,104 | 0,103 |
| 450–500 m | 0,014 | 0,014 | 0,018 | 0,019 |

PRR auto-auto do 100 m vyšlo 0,936 v ns-3 a 0,929 v OMNeT++. Druhý beh (80 áut, 50 s, od 15 s, seed 4) dal 0,940
a 0,930. Pri 0–50 m vyšlo 0,994 a 0,990, pri 50–100 m 0,855 a 0,838, pri 100–150 m 0,569 a 0,564. Veins je teda
sústavne o 0,5 až 2 body nižšie, najviac pri 50–100 m. Je to pravdepodobne rozdiel v modeli príjmu pri nízkom SNR.
S realistickým kanálom sa teda simulátory zhodujú do 1 až 2 bodov pri autách aj pri RSU. Náhodné čísla sú v každom simulátore iné (stav kanála, tienenie,
úniky), takže zhoda je štatistická, nie rámec po rámci.

**Pokus 2c: 1000 áut v OMNeT++** (mapa `--cc-grid 5`, mestský kanál, 23 dBm, len 802.11p a CAM, 40 s, od 20 s):
beh trval 764 s a zabral 0,77 GB, PRR auto-auto do 100 m vyšlo 0,846. ns-3 s plnou konfiguráciou práce (LTE, NR,
CPM, DCC...) potrebuje pri 1000 autách asi 7 minút na sekundu simulácie (Denník 2026-11-17). Rozdiel je hlavne v tom,
že tu je len jedna sieť a jeden druh správ.

**Pokus 3: spoločné jadro pravidla kvality** (`src/automotive/model/Applications/xgregorm-qos-core.h`). Rovnaký scenár
ako pokus 1, seed 10. ns-3 beží s `--policy adaptive --clean 1 --init-main 0 --qos-rat 1 --tech 80211p`, teda jedna
priama sieť a meranie kvality zapnuté. Počet rámcov je takmer rovnaký ako pri `--policy all`: 25 859 oproti 25 845.
OMNeT++ volá to isté jadro z `XgCamApp`. Odhad kvality 802.11p v autách je podiel CAM susedov do 100 m doručených do
100 ms. Je to to isté číslo, podľa ktorého auto vyberá sieť.

| | odhad v autách (jadro) | skutočné PRR do 100 m |
|---|---|---|
| ns-3 | 0,9941 (178 441 / 179 502) | 0,9939 |
| OMNeT++ | 0,9948 (178 313 / 179 240) | 0,9941 |

Jadro teda v oboch simulátoroch odhadne kvalitu rovnako blízko skutočnosti. Samotné rozhodovanie medzi dvoma sieťami
sa v OMNeT++ zatiaľ nedá vyskúšať, lebo tam chýba druhá priama sieť (NR-V2X).

Obmedzenia: 1 až 2 behy na kanál. Ide o overenie, že sa simulátory zhodujú, nie o výsledok práce.

## Inštalácia (Ubuntu 24.04, overené)

```bash
sudo apt install bison flex python3-pandas python3-matplotlib python3-scipy libdw-dev
mkdir -p ~/omnet && cd ~/omnet
curl -LO https://github.com/omnetpp/omnetpp/releases/download/omnetpp-6.0.3/omnetpp-6.0.3-core.tgz
curl -L -o veins-5.2.tar.gz https://github.com/sommer/veins/archive/refs/tags/veins-5.2.tar.gz
tar xzf omnetpp-6.0.3-core.tgz && tar xzf veins-5.2.tar.gz
cd omnetpp-6.0.3
sed -i 's/^WITH_QTENV=yes/WITH_QTENV=no/; s/^WITH_OSG=yes/WITH_OSG=no/; s/^WITH_OSGEARTH=yes/WITH_OSGEARTH=no/' configure.user
source setenv -f && ./configure && make -j6 MODE=release        # ~15 min
cd ../veins-veins-5.2 && ./configure && make -j4 MODE=release  # ~3 min
cd ~/VaN3Twin/ns-3-dev/xgregorm_omnet && make                   # môj projekt (VEINS_HOME=~/omnet/veins-veins-5.2)
```

Bez grafického rozhrania (Qtenv), beží len v príkazovom riadku (Cmdenv). SUMO 1.18.0 je to isté ako pre ns-3.

## Súbory

- `src/XgCamApp.*`: aplikácia CAM pre autá aj RSU:
  - pravidlá ETSI EN 302 637-2: kontrola každých 100 ms; zmena polohy o viac ako 4 m, smeru o viac ako 4° alebo
    rýchlosti o viac ako 0,5 m/s; T_GenCam na 3 CAM; RSU 1 CAM za sekundu,
  - veľkosť podpisu ako `--security 1`,
  - meranie doručenia po 50 m, v rovnakej definícii aj formáte ako ns-3.
- `src/XgCam.msg`: správa (poradové číslo a čas vzniku slúžia len na meranie).
- `src/XgScenario.ned`: sieť Veins Scenario a RSU.
- `src/XgV2vUrban.*`, `src/XgPhyLayer80211p.*`, `src/XgNic80211p.ned`: mestský kanál 3GPP TR 37.885 pre Veins
  (`--channel urban`).
- `omnetpp_base.ini`, `config.xml`, `antenna.xml`: rádio nastavené ako v ns-3 (výkon, citlivosť, šum −97 dBm,
  6 Mbit/s, LogDistance s exponentom 3 a útlmom 46,68 dB v 1 m, všesmerová anténa).
- `export_scenario.py`: zo scenára ns-3 vyrobí beh OMNeT++:
  - skopíruje súbory SUMO,
  - vytvorí `launchd.xml` s rovnakým seedom,
  - prepočíta RSU zo `stations.xml` a budovy (`--channel urban`) do súradníc OMNeT++ (x − x0 + okraj, y1 − y + okraj).
- `xgregorm_compare.py`: spustí obidva simulátory a vypíše porovnanie.
- `vysledky/`: logy všetkých overovacích pokusov (ns-3 aj OMNeT++).
