# VaN3Twin - diplomová práca (xgregorm)

Projekt: VaN3Twin (ns-3 + SUMO) na Ubuntu 24.04 vo WSL2. Pracovný priečinok je `~/VaN3Twin/ns-3-dev`.
Cieľ práce: vlastný algoritmus, ktorý zlepší merateľné parametre VANET siete (PRR, latencia,
prepnutia medzi RSU) oproti pôvodným aplikáciám autorov.

## Čo je čie

- `src/` (moduly `automotive`, `traci`, `carla`, `nr`, `cv2x`) a hotové mapy = kód autorov VaN3Twin.
- `xgregorm_gen_scenario.py` v koreni `ns-3-dev` = môj generátor scenárov, spúšťač a vyhodnotenie.
- `src/automotive/examples/xgregorm_<meno>/` = vygenerované scenáre (mapa, autá, RSU, cfg).
- Zálohy upravených súborov autorov sú v `xgregorm_kod_zaloha/`, ak existuje.
- Všetko, čo vytvorím ja, má prefix `xgregorm_`.

## Pravidlá, ktoré VaN3Twin vyžaduje (zistené z kódu, nikde nie sú zdokumentované)

1. **ID vozidiel musia byť `veh<číslo>`** - aplikácie robia `std::stol(m_id.substr(3))`.
2. **Autá v trase musia byť `<vehicle>`**, nie `<flow>` ani `<trip>`; podľa nich sa počítajú uzly ns-3.
3. **RSU sa čítajú z `stations.xml`** (názov je zadrátovaný v kóde). Parser číta súbor textovo po
   riadkoch: komentár `<!-- V2X:STATION -->` musí byť na tom istom riadku ako `<poi>`, ID musí byť
   `poi_<číslo>`. Netedit pri uložení komentáre zmaže, takže súbor upravovať len textovo.
4. **Sieť musí byť geo-referencovaná** (`projParameter` != `"!"`), inak nefungujú CAM, DENM a GeoArea.
   Siete z `netgenerate` aj z ručných nod/edg ju nemajú, generátor ju dopĺňa.
5. **`trafficManager` zvláda len 4-ramenné križovatky** - indexuje vjazdy [0..3], na 3-ramennej
   číta mimo rozsahu. Semafory preto dávame len križovatkám so 4 ramenami.
6. **Opravená chyba vo `vdpTraci.cc`**: výpočet jazdného pruhu pre CAM nerozlišoval vozidlo a RSU,
   takže V2I príklady padali na `Vehicle 'poi_X' is not known`. Riešené vetvou `if (m_isStatic)`.

## Natvrdo zadané hodnoty v aplikáciách autorov

| Čo | Súbor | Hodnota |
|---|---|---|
| polomer zóny RSU (802.11p) | `areaSpeedAdvisorServer80211p.cc` | `geoArea.distA = 50;` (autori: 90) |
| polomer DENM oblasti (TM 802.11p) | `trafficManagerServer80211p.cc` | `geoArea.distA = 50;` (autori: 90) |
| polomer DENM oblasti (LTE) | `areaSpeedAdvisorServerLTE.cc` | `geoArea.distA = 500;` (autori: 300) |
| pomalá zóna (LTE) | `areaSpeedAdvisorServerLTE.cc` | stred mapy, 2/3 rozmeru siete (nie okolie RSU) |
| podiel áut s V2X (v2v) | `v2v-emergencyVehicleAlert-80211p.cc` | `penetrationRate = 0.7` (CLI `--penetrationRate`) |
| limit v zóne | `areaSpeedAdvisorServer80211p.cc` (2x) | `roadworks.speedLimit.setData (25);` |
| rýchlosť mimo zóny | `areaSpeedAdvisorClient80211p.cc` | `75/3.6` |
| timeout bez DENM | `areaSpeedAdvisorClient80211p.cc` | `Seconds(1.5)` |
| station ID RSU | servery | `1000000 + číslo z poi_N` |
| `print_summary` v LTE | `v2i-areaSpeedAdvisor-lte.cc` | natvrdo `false`, nemá CLI prepínač |

Zmena ktorejkoľvek z nich je zásah do kódu autorov: najprv záloha do `xgregorm_kod_zaloha/`,
potom `./ns3 build`, a v práci to treba popísať. Hodnoty označené „autori: X“ sú už zmenené oproti
VaN3Twin `0e70bb36` (zmena je v prvom commite repozitára, priečinok zálohy zatiaľ neexistuje).

## Interpretácia metrík

- **PRR a packet loss majú zmysel len pre 802.11p.** Metric Supervisor počíta, koľko staníc do
  baseline vzdialenosti správu prijalo. LTE doručuje adresne cez eNodeB, takže PRR tam vyjde ~0
  bez ohľadu na kvalitu siete. Pre LTE porovnávaj latenciu a počty doručených DENM/CAM.
- Typické hodnoty: v2v 802.11p pri 10 vozidlách PRR ~0.997 a latencia ~0.2 ms; LTE latencia 50-65 ms.
- **V2I 802.11p aplikácie (areaSpeedAdvisor, trafficManager) majú PRR nízke už u autorov**: na ich
  vlastnej mape pri 120 s vyšlo 0.34 a 0.60, na `xgregorm_mesto` 0.66 a 0.59 (pôvodná malá mapa 0.38 a 0.46).
  **Príčina nájdená (2026-10-07):** synchronizované CAM, viď Denník. Nie je to zahltenie ani chyba scenára.
- PRR sa počíta v 3-sekundovom okne, takže `--sim-time` pod 60 s dáva nepoužiteľnú vzorku.
- Nízke PRR pri v2v 802.11p = zahltenie kanála (veľa vozidiel a RSU na malej ploche), nie chyba konfigurácie.
- `stop tot[s]` je súčet všetkých státí vozidla, `stop max[s]` najdlhšie jedno neprerušené státie.
- Typy správ: CAM je pravidelný tlkot srdca (1-10 Hz), DENM je udalostná správa do geografickej
  oblasti, CPM hlási objekty zo senzorov. PRR ovplyvňujú hlavne CAM, funkčnosť aplikácie hlavne DENM.

## Správanie aplikácií autorov (dôležité pre porovnávanie)

- **areaSpeedAdvisor 802.11p**: každá RSU má vlastnú zónu (kruh `distA` okolo seba) a posiela DENM
  periodicky. Autá na obchvate (veh1, veh2) žiadnu zónu nemajú, DENM 0 je správne.
- **areaSpeedAdvisor LTE**: úplne iná logika. Jeden server, jedna zóna = stred mapy (2/3 rozmeru),
  DENM sa pošle len pri prechode hranice zóny. V `xgregorm_mesto` je mesto celé vnútri a obchvat
  celý mimo, takže DENM dostávajú len autá jazdiace mesto <-> obchvat (veh7-10), ~2 za 120 s.
  RSU zo `stations.xml` sa v LTE nepoužívajú, preto „RSU switches“ = 0.
- **trafficManager 802.11p**: server beží na **každej** RSU a **každý riadi všetky semafory**
  (`getIDList()`), pričom vidí len CAM áut vo svojom dosahu. Viac RSU sa tak pretláča o tie isté
  semafory, mestské autá potom stoja aj 90-110 s (čistý SUMO max ~30 s). Rovnaký návrh je aj
  v mape autorov (5 RSU, 2 semafory). Pri LTE je server jeden a premávka ide plynulo (~50 km/h).
- **v2v emergencyVehicleAlert**: `penetrationRate = 0.7`, takže ~30 % áut nemá V2X jednotku.
  Pri pevnom seede sú to v `xgregorm_mesto` vždy veh7 a veh8 (TX 0 / RX 0, chýba INFO riadok).

## Časté pády a ich príčiny

- `Assertion vNext >= vMinEmergency failed` → SUMO z Ubuntu repozitára má zapnuté asserty.
  Musí byť SUMO 1.18.0 skompilované zo zdrojákov v Release (`/usr/local/bin/sumo`).
- `too many UEs (41) for current SRS periodicity 40` → limit LTE bunky. Rieši parameter `--srs 320`,
  ktorý pridá `--ns3::LteEnbRrc::SrsPeriodicity=320`.
- `Vehicle 'poi_X' is not known` → chýba patch vo `vdpTraci.cc` (bod 6 vyššie).
- Autá sa zasekávajú na napojení mesta na obchvat → uzly brán musia byť `right_before_left`,
  pri `priority` čakajú na zaradenie aj 38 s.
- Semafory "preblikávajú" v trafficManager → jeho interval je `phase-time * podiel zaťaženia`,
  pri prázdnej križovatke vyjde len dĺžka žltej. Pomáha vyšší `--phase-time`.

## Ako sa pracuje

```bash
cd ~/VaN3Twin/ns-3-dev
python3 xgregorm_gen_scenario.py --diag                     # scenár + kontrola plynulosti premávky
python3 xgregorm_gen_scenario.py --run v2i-speed-80211p     # scenár + simulácia + súhrn
python3 xgregorm_gen_scenario.py --run all                  # všetkých 5 simulácií
./ns3 build                                                 # len po zmene C++ kódu
```

- Zmena Python skriptu alebo SUMO súborov **nevyžaduje** `./ns3 build`.
- Priečinok `build/` nikdy nemazať, znamenalo by to hodinovú rekompiláciu.
- Dočasné súbory (`xgregorm_tmp/`) si skript maže sám, žiadne CSV nenechávať na disku.
- Pri porovnávaní vlastného riešenia s referenciou nechaj scenár úplne nezmenený (rovnaký počet
  vozidiel, RSU aj seed) a meň len algoritmus. Minimálne 5 behov na konfiguráciu.
- Riadok `CSV:` na konci súhrnu je určený na kopírovanie do tabuľky s výsledkami.
- Výpisy skriptu sú po anglicky, komentáre v kóde môžu byť po slovensky.

## Git

- Repozitár práce: `https://github.com/MartinGregor/XGREGORM_DP` (remote `dp`, vetva `main`).
  História začína čistým snapshotom VaN3Twin `0e70bb36` bez histórie autorov; README autorov je
  v `README_VaN3Twin.md`. Lokálna vetva `master` + remote `origin` = pôvodný VaN3Twin, do `dp` nepushovať.
- Novinky od autorov preberať len ako zmeny súborov, nikdy merge z `origin` (natiahol by ich históriu).
- **Pred každým commitom/pushom sa opýtaj.** Zmeny v projekte zapisuj do tohto súboru (Denník nižšie).
- `*:Zone.Identifier` (metadáta Windows) sú v `.gitignore`.

## Denník zmien a overení

- **2026-10-07** – Prvý push do `XGREGORM_DP`. Overenie scenára `xgregorm_mesto` (10 áut, 5 RSU,
  120 s, `--run all --diag`): všetkých 5 simulácií dobehlo bez pádu, 0 teleportov, generátor je
  deterministický (líši sa len časová pečiatka v `net.xml`). Výsledky (CSV riadky):
  `v2i-areaSpeedAdvisor-80211p,10,5,120,0.380626,61.94,0.197,34,10.12,19.5`
  `v2i-areaSpeedAdvisor-lte,10,5,120,n/a,n/a,53.000,0,10.19,26.2`
  `v2i-trafficManager-80211p,10,5,120,0.459649,54.04,0.265,0,9.28,46.6`
  `v2i-trafficManager-LTE,10,5,120,n/a,n/a,65.405,0,13.88,6.1`
  `v2v-emergencyVehicleAlert-80211p,10,5,120,0.996706,0.33,0.185,0,11.42,26.5`
  Referencia na mapách autorov (120 s): areaSpeedAdvisor-80211p PRR 0.339, trafficManager-80211p
  PRR 0.597, areaSpeedAdvisor-lte latencia 38.6 ms. Vysvetlenie odchýlok v časti „Správanie aplikácií“.
- **2026-10-07** – Realistické rozmery mapy `xgregorm_mesto` (zmenené len predvolené hodnoty generátora,
  mapy a kód autorov nedotknuté):

  | parameter | predtým | teraz | prečo |
  |---|---|---|---|
  | `--step` (vzdialenosť križovatiek) | 100 m | 200 m | typický mestský blok medzi semaformi 150-250 m; mesto 400x400 m |
  | `--odstup` (obchvat od okraja mesta) | 150 m | 250 m | polomer obchvatu 450 m, obvod ~2,8 km |
  | `--lanes-mesto` | 2 | 1 | mestská ulica 50 km/h má 1 pruh v smere |
  | `--lanes-obchvat` | 3 | 2 | 3 pruhy v smere sú diaľnica, nie obchvat 90 km/h |
  | `--v-privod` (nový, cesty mesto-obchvat) | 90 km/h | 70 km/h | prípojné cesty nie sú rýchlostné |
  | `--tx-power` | 30 dBm | 30 dBm (bez zmeny) | v norme ETSI (max 33 dBm); s kanálom autorov (LogDistance, exp. 3) dosah ~200 m |

  RSU sú na 5 semaforizovaných križovatkách 200 m od seba, takže pri dosahu ~200 m sa susedné
  tesne prekrývajú (bežné rozmiestnenie RSU). Obchvat (r = 450 m) leží celý mimo pomalej zóny LTE
  aj vnútri jej DENM oblasti (500 m), prechody sú len na prípojných cestách.
  `--diag`: 10 áut 51 km/h, 30 áut 45 km/h, 0 teleportov, max státie 34 s. Výsledky `--run all`:
  `v2i-areaSpeedAdvisor-80211p,10,5,120,0.660271,33.97,0.187,24,13.15,8.2`
  `v2i-areaSpeedAdvisor-lte,10,5,120,n/a,n/a,45.838,0,13.51,8.5`
  `v2i-trafficManager-80211p,10,5,120,0.585244,41.48,0.265,0,11.97,33.9`
  `v2i-trafficManager-LTE,10,5,120,n/a,n/a,54.425,0,15.43,3.6`
  `v2v-emergencyVehicleAlert-80211p,10,5,120,0.996886,0.31,0.191,0,14.05,13.7`
  trafficManager-80211p stále brzdí (veh3 stojí 107 s), čo je vlastnosť aplikácie, nie mapy.
- **2026-10-07** – Opakované overenie na novej mape s ns-3 (`--diag --run all`):
  - 10 áut: výsledky **zhodné do posledného miesta** s riadkami vyššie -> simulácie sú deterministické.
  - 30 áut (záťažový test): všetkých 5 behov bez pádu, 0 teleportov, LTE so `--srs 320` OK
    (~4,5 min na LTE beh, ~2 min na 802.11p). Výsledky:
    `v2i-areaSpeedAdvisor-80211p,30,5,120,0.347288,65.27,0.193,59,12.30,12.8`
    `v2i-areaSpeedAdvisor-lte,30,5,120,n/a,n/a,49.069,0,12.40,13.0`
    `v2i-trafficManager-80211p,30,5,120,0.428363,57.16,0.265,0,10.91,37.6`
    `v2i-trafficManager-LTE,30,5,120,n/a,n/a,40.833,0,14.55,6.8`
    `v2v-emergencyVehicleAlert-80211p,30,5,120,0.997835,0.22,0.205,0,13.18,14.6`
  - Pozorovanie: V2I 802.11p PRR s počtom áut klesá (areaSpeedAdvisor 0.66 -> 0.35), v2v nie
    (0.997 -> 0.998). V2I aplikácie autorov teda strácajú pakety so záťažou - kandidát na zlepšenie.
    Vo v2v je bez V2X 7 z 30 áut (penetrationRate 0.7).
- **2026-10-07** – Analýza príčin dvoch slabín (bez zmeny kódu autorov; diagnostická kópia príkladu
  `scratch/xgregorm_diag_asa80211p.cc` vypisuje PRR po typoch správ `XDIAG-*`, scratch je v `.gitignore`):
  1. **Nízke V2I PRR = synchronizované CAM.** V2I aplikácie (`areaSpeedAdvisor*`, `trafficManager*`,
     `emergencyVehicleWarning*`, klient aj server) robia `std::srand(Simulator::Now().GetNanoSeconds());
     desync = rand()/RAND_MAX`. Autá vzniknuté v rovnakom kroku dostanú rovnaký seed -> rovnaký posun
     -> vysielajú CAM v tých istých okamihoch a kolidujú. Autori to vo `emergencyVehicleAlert.cc`
     opravili (`UniformRandomVariable`, komentár „nodes not being desynchronized properly“), vo V2I nie.
     Test (30 dBm, baseline 100 m, rovnaký scenár, len odchody posunuté o 0.1 s na auto):

     | | PRR CAM | PRR DENM | PRR spolu |
     |---|---|---|---|
     | 10 áut, všetky t=0 | 0.619 | 0.997 | 0.660 |
     | 10 áut, posunuté | 0.978 | 1.000 | 0.981 |
     | 30 áut, všetky t=0 | 0.311 | 0.999 | 0.347 |
     | 30 áut, posunuté | 0.991 | 0.998 | 0.991 |
     | 10 áut, posunuté, bez RSU | 1.000 | - | 1.000 |
  2. **trafficManager 802.11p brzdí kvôli viacerým RSU, nie kvôli CAM.** (10 áut, rýchlosť / státie / max státie)
     5 RSU sync: 43.1 km/h / 33.9 % / 107 s; 5 RSU posunuté: rovnako (106.8 s);
     1 RSU sync: 55.5 km/h / 3.6 % / 20 s; 1 RSU posunuté: 55.8 / 3.5 % / 19.8 s (= ako LTE s 1 serverom).
     Mechanizmus (z kódu `trafficManagerServer80211p.cc`): každý server plánuje `phaseTimeout` pre
     všetky semafory -> prepínajú sa ~5x častejšie; vetva `else` nastaví `EW_YELLOW` z akejkoľvek
     fázy okrem NS_GREEN (aj zo žltej) -> smer sa preskočí/žltá sa reštartuje; server, ktorý autá
     danej križovatky nevidí, má záťaž (0,0) -> interval len 0*phase + 3 s žltá.
  Dôsledok pre porovnania: referenčné V2I PRR je zaťažené touto chybou; ak ju vlastný algoritmus
  opraví, treba to v práci uviesť oddelene od prínosu samotného algoritmu.

## Čo robiť, keď si na pochybách

- Pred zmenou v kóde autorov sa spýtaj; je to vec, ktorú treba v práci popísať a nahlásiť do Issues.
- Nevymýšľaj si hodnoty metrík. Ak beh spadol, povedz to a ukáž chybovú hlášku.
- Dlhé behy (LTE, NR-V2X) spúšťaj najprv s menším `--vehicles` a `--sim-time`, kým sa overí, že bežia.
- LTE beh trvá rádovo 4 minúty pri 30 vozidlách a 120 s simulácie; nerušiť ho predčasne.
