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

## Moje zmeny v kóde autorov (treba popísať v práci)

| Súbor | Zmena | Záloha |
|---|---|---|
| `vdpTraci.cc` | vetva `if (m_isStatic)` pre RSU (bod 6) | – (pred vznikom zálohy) |
| `areaSpeedAdvisorServer80211p.cc`, `areaSpeedAdvisorServerLTE.cc`, `trafficManagerServer80211p.cc` | `distA` 90->50, 300->500 | – (pred vznikom zálohy) |
| `trafficManagerServer80211p.cc/.h` | atribút `TlsRadius` + metóda `tlsDistance()`: RSU riadi len semafory do daného polomeru (svoju križovatku); 0 = pôvodné správanie (všetky semafory) | `xgregorm_kod_zaloha/` |
| `v2i-trafficManager-80211p.cc` | CLI `--tls-radius` -> atribút `TlsRadius`; **od 2026-10-11 predvolene 50** (každá RSU len svoj semafor, aj bez skriptu); 0 = pôvodné | `xgregorm_kod_zaloha/` |
| `trafficManagerServer80211p.cc/.h` | **koordinácia po kábli**: atribúty `Coordination`, `BackhaulBroadcast`; logika počítania áut z `receiveCAM` vyčlenená do `applyVehiclePosition()` (bez zmeny správania); `sendCoord()`/`receiveCoord()` (UDP port 7000); výpis `XGREGORM-COORD,<rsu>,TX:,RX:` | `xgregorm_kod_zaloha/` |
| `trafficManagerServer80211p.cc` | **oprava chyby autorov**: pri prázdnej križovatke 0/0 = NaN -> `phaseTimeout` naplánoval prepnutie o 0 s -> nekonečná slučka (simulácia „zamrzne“, SUMO aj ns-3 na 100 % CPU). Pomer sa pri nulovej záťaži neprepočíta. | `xgregorm_kod_zaloha/` |
| `v2i-trafficManager-80211p.cc` | CLI `--tls-coord` (kábel CSMA 100 Mb/s, 1 ms, 10.10.0.0/24 medzi RSU; pri ňom sa `PacketSocketHelper` na RSU neinštaluje, `InternetStackHelper` ho pridá sám); CLI `--sumo-seed` (pôvodne natvrdo 10) | `xgregorm_kod_zaloha/` |
| `trafficManagerServer80211p.cc/.h`, `v2i-trafficManager-80211p.cc` | atribút `CoordLookahead` / CLI `--coord-lookahead`: auto ohlásené susedom na mojom príjazde sa započíta až X s pred odhadovaným príchodom (dĺžka pruhu / limit); `applyCoordDelayed()` ho zahodí, ak medzitým prišla novšia informácia | `xgregorm_kod_zaloha/` |
| `src/automotive/examples/CMakeLists.txt` | `v2i-trafficManager-80211p` linkuje `${libcsma}` | `xgregorm_kod_zaloha/examples_CMakeLists.txt` |
| `areaSpeedAdvisorClient80211p.cc`, `areaSpeedAdvisorServer80211p.cc`, `trafficManagerClient80211p.cc`, `trafficManagerServer80211p.cc`, `emergencyVehicleWarningClient80211p.cc`, `emergencyVehicleWarningServer80211p.cc`, `areaSpeedAdvisorClientLTE.cc`, `trafficManagerClientLTE.cc` | **oprava A – desynchronizácia CAM**: `std::srand(Now)` + `rand()` nahradené `UniformRandomVariable` (rovnako ako autori v `emergencyVehicleAlert.cc`). Neprepínateľné – stará referencia „s chybou“ je len v Denníku (alebo zo záloh). `simpleCAMSender-gps-tc`, `simpleVAMSender-gps-tc`, `v2xEmulator` nechané (nepoužívame). | `xgregorm_kod_zaloha/` |
| `caBasicService.cc/.h` (jadro CAM, používajú ho všetky príklady) | **háčiky pre moje algoritmy CAM**: `setCamGate()` (pred CAM spustenou v `checkCamConditions` sa opýta, či ju poslať; `dynamic` = trigger poloha/smer/rýchlosť, inak časový), `addCamTxCallback()` (po každej odoslanej CAM), `forceCam()` (pošli CAM hneď; zachová T_GenCam a reštartuje 100 ms kontrolu, aby ďalšia CAM prišla najneskôr po T_GenCam). Bez nastavenia = pôvodné ETSI správanie, overené bit po bite | `xgregorm_kod_zaloha/` |
| `trafficManagerClient80211p.cc/.h`, `trafficManagerServer80211p.cc/.h`, `v2i-trafficManager-80211p.cc` | **algoritmy CAM 1-3** (atribúty `XgSpat`, `XgAck`, `XgRelax`, `XgBeaconMs`, `XgHoldMax`, `XgRelevance`; CLI `--xg-spat/--xg-ack/--xg-relax/--xg-beacon-ms/--xg-hold-max/--xg-relevance`), beacon RSU (vlastný 802.11p rámec, ethertype 0x88B5), merania `XGREGORM-AW/-V2V/-VEH/-GAP/-PRR(V)`; dodržiava ETSI EN 302 637-2 (odstup CAM 100-1000 ms); `forceCam` zachová T_GenCam; viď Denník 2026-10-12 | `xgregorm_kod_zaloha/` |
| `xgregorm-stats.h` (nový, `model/Applications/`), `src/automotive/CMakeLists.txt` | merania (len čítanie zo SUMO): presnosť informácie RSU, uplink do RSU, skutočné PRR auto-auto; hlavička zaradená do zoznamu hlavičiek modulu | `xgregorm_kod_zaloha/automotive_CMakeLists.txt` |

**Pravidlo riadenia semaforov:** 802.11p = každá RSU riadi len semafor svojej križovatky (ako
v realite: radič + RSU na každej križovatke), predvolené v kóde aj v skripte. LTE = jeden server
riadi všetky semafory (pôvodný kód autorov, nemenené). Semafor bez RSU do 50 m nikto neriadi
(ide podľa pevného programu SUMO) – nastane len pri `--rsu` menšom než počet semaforov.

Všetky moje miesta v C++ sú označené komentárom `xgregorm`. Skript posiela `--tls-radius 50`
(lokálne riadenie); `--tls-radius 0` = referencia autorov; `--tls-coord 1` = koordinácia po kábli.
`--seed N` v skripte: ns-3 `--RngRun` pre všetky programy, seed SUMO zatiaľ len pre
`v2i-traffic-80211p` (ostatné príklady autorov majú `SumoSeed` natvrdo 10, seed v `.sumo.cfg` sa
ignoruje, lebo TraciClient posiela `--seed` na príkazovom riadku). Bez `--seed` = referenčné výsledky.

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
  **Opravené 2026-10-11 (oprava A):** V2I 802.11p PRR je teraz ~0.996 aj pri 30 autách. Výsledky
  V2I spred tohto dátumu sú „autori s chybou“ a s novými sa priamo porovnávať nedajú.
- **PRR z MetricSupervisora je skreslené (zistené 2026-10-12):** polohu odosielateľa berie z GeoNet
  EPV, ktorá sa obnovuje len raz za 1 s (`m_GnMinUpdateFrequencyEPV = 1000`). Do „baseline“ tak
  započíta autá okolo polohy starej až 1 s (pri 90 km/h 25 m vedľa) a chýbajúce príjmy sú z veľkej časti
  artefakt. Skreslenie závisí od frekvencie CAM: pri algoritme 3 kleslo z 0.995 na 0.988, hoci skutočné
  doručenie ostalo 100 %. Pri porovnávaní algoritmov, ktoré menia počet CAM, používať **skutočné PRR**
  `XGREGORM-V2V` (poloha zo SUMO v okamihu vyslania, rovnaký 100 m baseline).
- **`--datarate` v príkladoch 802.11p nefunguje pre CAM:** nastavuje `DataMode` v `ConstantRateWifiManager`,
  ale broadcast ide v ns-3 rýchlosťou `NonUnicastMode` (predvolená). 3/6/12 Mbit/s dali bit po bite
  rovnaké výsledky (chyba autorov, neopravená).
- PRR sa počíta v 3-sekundovom okne, takže `--sim-time` pod 60 s dáva nepoužiteľnú vzorku.
- Nízke PRR pri v2v 802.11p = zahltenie kanála (veľa vozidiel a RSU na malej ploche), nie chyba konfigurácie.
- **Počty CAM/DENM/CPM v súhrne sú z riadkov `INFO-vehX`, ktoré tlačia aplikácie** – každá iné:
  trafficManager klient len `CAM-SENT`, `DENM-RECEIVED`; areaSpeedAdvisor klient `CAM-SENT`,
  `DENM-RECEIVED`; v2v `CAM-SENT/RECEIVED`, `CPM-SENT/RECEIVED` (DENM nie). Čo aplikácia nehlási,
  skript od 2026-10-11 ukazuje ako `n/a` (predtým chybne 0). Skutočné doručenie = PRR.
  DENM 0 pri trafficManager je správne – server DENM neposiela; CPM posiela len v2v aplikácia.
- **Čo má ktorý scenár v súhrne ukazovať (overené 2026-10-11, kód aplikácií):**

  | hodnota | speed-80211p | speed-lte | traffic-80211p | traffic-lte | v2v-80211p |
  |---|---|---|---|---|---|
  | PRR / strata | áno | n/a (unicast) | áno | n/a | áno |
  | latencia | áno | áno | áno | áno | áno |
  | CAM sent | áno | n/a* | áno | áno | áno |
  | CAM received (auto) | n/a – klient nepočíta | n/a | n/a – klient nepočíta | n/a | áno |
  | DENM received | áno (z RSU zón) | áno (z CSV, len prechody zóny) | 0 – server DENM neposiela | 0 – rovnako | n/a – app DENM neposiela ani nepočíta |
  | CPM sent / received | n/a | n/a | n/a | n/a | áno |
  | RSU switches | áno (z CSV DENM) | n/a – 1 server | n/a – autá z RSU nič nedostávajú | n/a | n/a – bez RSU |
  | doprava (SUMO netstate) | áno | áno | áno | áno | áno |

  \* `print_summary` je v `v2i-areaSpeedAdvisor-lte.cc` natvrdo `false` (bez CLI) -> žiadne INFO riadky.
  Klienti trafficManager (802.11p aj LTE) tlačia `DENM-RECEIVED`, ale počítadlo nikdy nezvyšujú – 0 je
  pravdivé len preto, že server DENM neposiela. Servery počítajú prijaté CAM (`m_cam_received`), ale
  nevypisujú ich (areaSpeedAdvisor 802.11p má `--server-aggregate-output`, trafficManager nič).
  Opravy v skripte 2026-10-11: chýbajúce počítadlo = `n/a` (nie 0); `CPM sent` pridané; RSU switches
  pre LTE a pre programy bez CSV = `n/a` aj v riadku `CSV:` (predtým 0 – staršie CSV riadky v Denníku
  majú pri traffic/v2v/LTE v stĺpci rsu_switches 0, správne je n/a).
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
- **trafficManager 802.11p (pôvodne, `--tls-radius 0`)**: server beží na **každej** RSU a **každý riadi všetky semafory**
  (`getIDList()`), pričom vidí len CAM áut vo svojom dosahu. Opravené lokálnym riadením (viď Denník). Viac RSU sa tak pretláča o tie isté
  semafory, mestské autá potom stoja aj 90-110 s (čistý SUMO max ~30 s). Rovnaký návrh je aj
  v mape autorov (5 RSU, 2 semafory). Pri LTE je server jeden a premávka ide plynulo (~50 km/h).
- **v2v emergencyVehicleAlert**: `penetrationRate = 0.7`, takže ~30 % áut nemá V2X jednotku.
  Pri pevnom seede sú to v `xgregorm_mesto` vždy veh7 a veh8 (TX 0 / RX 0, chýba INFO riadok).

## Tok komunikácie v trafficManager 802.11p (čo ide cez ns-3 a čo nie)

| Spoj | Cez čo | Simulované v ns-3? |
|---|---|---|
| auto -> RSU: CAM (poloha, rýchlosť) | 802.11p, BTP/GeoNet, `YansWifiChannel` | **áno** – útlm, kolízie, strata, latencia (meria MetricSupervisor) |
| RSU -> autá: CAM RSU | 802.11p | áno (len „tlkot srdca“, autá ho nevyužívajú) |
| RSU -> autá: DENM | – | **neposiela sa** (`receiveDENM` je prázdny, DENM rx = 0) |
| RSU -> semafor: `getPhase`/`setPhase` | TraCI priamo do SUMO | **nie** – okamžité, bez straty (ideálny kábel k radiču v skrinke) |
| poloha z CAM -> cesta (`convertLonLattoRoadmap`), dĺžka pruhu, pozícia križovatky | TraCI | nie – „digitálna mapa“ RSU, statické dáta |
| semafor -> auto | SUMO (auto „vidí“ farbu) | nie – žiadne V2X (SPaT/MAPEM sa neposiela) |
| RSU <-> RSU, pôvodne | – | **neexistuje**; „boj“ RSU o semafory išiel len cez TraCI |
| RSU <-> RSU, `--tls-coord 1` (moje) | CSMA 100 Mb/s, 1 ms, IPv4/UDP broadcast 10.10.0.255:7000 | **áno** – reálne pakety v ns-3; MetricSupervisor ich nemeria (len GeoNet), počty vypisuje `XGREGORM-COORD` |

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
- Simulácia trafficManager „zamrzne“ (ns-3 aj SUMO 100 % CPU, čas nebeží) → NaN pri prázdnej
  križovatke, opravené (viď Moje zmeny). Experimenty spúšťať s `timeout`.
- `Multiple aggregation of objects of type ns3::PacketSocketFactory` → na uzol sa inštaluje
  `PacketSocketHelper` aj `InternetStackHelper`; ten druhý ho už obsahuje.
- Autá v trafficManager stoja 60+ s → nízky `--threshold` (napr. 40) spôsobuje neustále okamžité
  prepínanie; držať 70. Pri `--tls-radius 0` je príčinou boj viacerých RSU o semafory.

## Ako sa pracuje

```bash
cd ~/VaN3Twin/ns-3-dev
python3 xgregorm_gen_scenario.py --diag                     # scenár + kontrola plynulosti premávky
python3 xgregorm_gen_scenario.py --run v2i-speed-80211p     # scenár + simulácia + súhrn
python3 xgregorm_gen_scenario.py --run all                  # všetkých 5 simulácií
./ns3 build                                                 # len po zmene C++ kódu
```

- **Grafika:** `--gui` otvorí `sumo-gui` (WSLg, `DISPLAY=:0` funguje). Simulácia sa spustí sama
  (TraciClient posiela `--start --quit-on-end`) a beží najrýchlejšie, ako vie – na sledovanie
  nastav v okne **Delay** (napr. 100 ms). Po konci sa okno samo zavrie. RSU = oranžové body (POI).
  Mapa bez ns-3: `sumo-gui -c src/automotive/examples/xgregorm_mesto/xgregorm_map.sumo.cfg`.
- Každé spustenie skriptu nanovo vygeneruje scenár; `net.xml` sa potom líši len časovou pečiatkou
  (`git checkout -- src/automotive/examples/xgregorm_mesto/` ho vráti).
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
- **2026-10-07** – **Lokálne riadenie križovatiek v trafficManager 802.11p** (zásah do kódu autorov,
  zálohy v `xgregorm_kod_zaloha/`, `./ns3 build` hotový). Každá z 5 RSU riadi len semafor do 50 m od
  seba (RSU stoja priamo na križovatkách 200 m od seba, takže každá má presne jednu). Pozícia
  semaforu = `junction.getPosition(tlsid)`, záloha: koniec prvého riadeného pruhu.
  Zároveň **predvolený `--threshold` v skripte 40 -> 70** (= predvolená hodnota autorov): pri 40 server
  pri každej CAM okamžite prepínal, keď opačný smer mal > 40 % áut, a menej vyťažený smer zelenú
  takmer nedostal (30 áut: max státie 61 s, pri 70: 20 s, pri 100: 33 s). Pôvodný režim
  (`--tls-radius 0`) dáva pri 40 aj 70 identické výsledky, referencia ostáva platná.

  | 120 s, 5 RSU | max státie | čas v státí | rýchlosť | PRR |
  |---|---|---|---|---|
  | 10 áut, pôvodne (`--tls-radius 0`) | 107.0 s | 33.9 % | 43.1 km/h | 0.585 |
  | 10 áut, lokálne (`--tls-radius 50`) | **20.0 s** | **2.8 %** | **57.0 km/h** | 0.593 |
  | 30 áut, pôvodne | 107.0 s | 37.6 % | 39.3 km/h | 0.428 |
  | 30 áut, lokálne | **20.0 s** | **7.7 %** | **51.5 km/h** | 0.313 |
  | čistý SUMO, pevné semafory 30 s (`--diag`, 30 áut) | 34.4 s | – | 45 km/h | – |

  Lokálne riadenie je lepšie ako pevné semafory. PRR sa nezmenilo (desynchronizácia CAM je
  samostatný problém). Ostatné 4 simulácie po prekompilovaní identické s predchádzajúcimi.
  Nová referencia: `v2i-trafficManager-80211p,10,5,120,0.592778,40.72,0.265,0,15.83,2.8`
- **Plán (implementované 2026-10-11, viď nižšie):** koordinácia susedných križovatiek – RSU si posielajú info
  o autách, ktoré smerujú k susednej križovatke (čas príchodu), aby sa semafor pripravil vopred.
  Rozhodnutie: **RSU medzi sebou komunikujú káblom** (pevná sieť, v ns-3 napr. CSMA/P2P), nie cez
  802.11p. Zlepšuje parametre dopravy (státie, rýchlosť), nie sieťové. Sieťový algoritmus sa zatiaľ
  nerieši, aktuálny cieľ = mať otestovateľné scenáre.
- **2026-10-11** – **Koordinácia križovatiek po kábli: implementácia a test.**
  Návrh: RSU pri každej CAM zistí cestu auta (`convertLonLattoRoadmap`); keď auto prejde na inú
  (neinternú) cestu, pošle UDP broadcast `stationId,edge,čas` všetkým RSU po kábli (CSMA 100 Mb/s,
  1 ms). Prijímateľ si podľa toho aktualizuje počty áut na svojich príjazdoch (rovnaká funkcia ako
  pri vlastnej CAM, staršie správy ignoruje). Réžia: ~0.4 msg/s pri 10 autách, ~1 msg/s pri 30,
  ~1.5 msg/s pri 50 (celá sieť), každá správa < 30 B.
  Pri teste odhalená a opravená chyba autorov (NaN -> nekonečná slučka, viď Moje zmeny) a natvrdo
  zadaný seed SUMO (pridané `--sumo-seed`, v skripte `--seed`).
  Výsledky, 5 seedov (`--seed 1..5`), 120 s, 5 RSU, priemer ± smerodajná odchýlka:

  | autá | režim | max státie [s] | priem. najdlhšie státie [s] | čas v státí [%] | rýchlosť [km/h] |
  |---|---|---|---|---|---|
  | 10 | pôvodne (`--tls-radius 0`) | 104.1 ± 2.3 | 40.1 ± 1.7 | 34.1 ± 1.5 | 41.5 |
  | 10 | lokálne | **17.1 ± 2.3** | **3.1 ± 0.4** | **3.3 ± 0.4** | **55.0** |
  | 10 | lokálne + koordinácia | 18.1 ± 3.4 | 3.9 ± 1.4 | 4.0 ± 1.4 | 54.6 |
  | 30 | pôvodne | 105.8 ± 0.5 | 44.3 ± 0.7 | 37.5 ± 0.7 | 38.9 |
  | 30 | lokálne | **18.8 ± 0.5** | **5.7 ± 0.3** | **7.0 ± 0.4** | **50.5** |
  | 30 | lokálne + koordinácia | 22.4 ± 3.5 | 7.4 ± 1.1 | 9.3 ± 1.6 | 49.7 |
  | 30 | lokálne + koord., predstih 5 s | 23.6 ± 4.4 | 6.8 ± 1.0 | 7.9 ± 1.0 | 50.5 |
  | 30 | lokálne + koord., predstih 10 s | 22.7 ± 3.3 | 7.1 ± 0.9 | 8.4 ± 1.0 | 50.2 |
  | 50 | lokálne | **27.7 ± 7.6** | **7.6 ± 1.1** | **9.5 ± 1.3** | **47.9** |
  | 50 | lokálne + koordinácia | 28.0 ± 6.2 | 9.5 ± 1.5 | 12.1 ± 1.9 | 46.4 |

  **Záver: koordinácia v tejto podobe nepomáha, mierne škodí.** Hlavný prínos ostáva lokálne
  riadenie (max státie ~104 s -> ~18 s). Prečo (hypotézy, nie všetky overené):
  1. RSU sa o aute dozvie aj bez suseda: auto vysiela niekoľko CAM za sekundu, takže aj pri 70 %
     strate ho vlastná RSU zachytí do ~1 s. Pri predstihu 5 s sa pri 10 autách správa od suseda
     neuplatnila ani raz (výsledok identický s lokálnym režimom) -> nová informácia je malá.
  2. Algoritmus autorov počíta všetky autá na celom príjazde (200 m) rovnako a pri každej
     aktualizácii môže okamžite prepnúť (`threshold`). Viac aktualizácií = viac prepnutí, každé
     stojí 3-4 s žltej. Neoverené: počet prepnutí sa zatiaľ nemeria.
  Čo by mohlo pomôcť (ak sa k tomu vrátime): rozhodovať podľa áut čakajúcich pri stopčiare / ETA,
  nie podľa počtu na celom príjazde; minimálna dĺžka zelenej; koordinácia až po oprave
  desynchronizácie CAM. `--tls-coord` ostáva v skripte predvolene 0.
- **2026-10-11** – **Oprava A: desynchronizácia CAM** v 8 V2I aplikáciách (viď Moje zmeny),
  `./ns3 build` celý. Krátky test (bez `--seed`, 120 s, 5 RSU, lokálne riadenie TM):

  | simulácia | autá | PRR pred | PRR po | iné |
  |---|---|---|---|---|
  | areaSpeedAdvisor-80211p | 10 | 0.660 | **0.996** | rýchlosť/státie ~bez zmeny |
  | trafficManager-80211p | 10 | 0.593 | **0.995** | doprava identická (15.83 m/s, 2.8 %) |
  | areaSpeedAdvisor-80211p | 30 | 0.347 | **0.997** | 62 prepnutí RSU (pred 59) |
  | trafficManager-80211p | 30 | 0.313 | **0.997** | max státie 20.0 -> **25.4 s**, státie 7.7 -> **10.4 %** |
  | v2v-emergencyVehicleAlert | 10 | 0.997 | 0.997 | bez zmeny (oprava sa ho netýka) |
  | LTE areaSpeedAdvisor / trafficManager | 10 | – | – | latencia 45.8 -> 43.2 ms, 54.4 -> 53.2 ms |

  Všetkých 5 simulácií bez pádu, 0 teleportov. Pozorovanie: pri trafficManager 30 áut sa doprava
  s lepšími dátami (viac doručených CAM) mierne zhoršila – podporuje hypotézu, že algoritmus autorov
  pri každej aktualizácii zbytočne prepína (viď záver koordinácie). Overené len 1 behom.
  **Nové referencie (po oprave A, bez `--seed`):**
  `v2i-areaSpeedAdvisor-80211p,10,5,120,0.995733,0.43,0.183,24,13.13,8.0`
  `v2i-areaSpeedAdvisor-lte,10,5,120,n/a,n/a,43.175,0,13.51,8.5`
  `v2i-trafficManager-80211p,10,5,120,0.995281,0.47,0.265,0,15.83,2.8`
  `v2i-trafficManager-LTE,10,5,120,n/a,n/a,53.222,0,15.43,3.6`
  `v2v-emergencyVehicleAlert-80211p,10,5,120,0.996886,0.31,0.191,0,14.05,13.7`
  `v2i-areaSpeedAdvisor-80211p,30,5,120,0.997075,0.29,0.184,62,12.23,12.3`
  `v2i-trafficManager-80211p,30,5,120,0.997178,0.28,0.266,0,13.60,10.4`
- **2026-10-11** – **Lokálne riadenie je predvolené priamo v kóde** (`TlsRadius` atribút aj
  `--tls-radius` v príklade = 50, predtým 0). Dôvod: v realite má každá križovatka vlastný radič
  s RSU; LTE ostáva s jedným serverom pre všetky semafory. Test (10 áut, bez `--seed`):
  príklad spustený priamo bez `--tls-radius` -> PRR 0.995281 = presne lokálna referencia;
  skript `v2i-traffic-80211p` identický s referenciou; `v2i-traffic-lte` identický (53.222 ms).
  Nová referencia pôvodného režimu po oprave A (`--tls-radius 0`):
  `v2i-trafficManager-80211p,10,5,120,0.993968,0.60,0.265,0,11.97,33.9` (max státie stále 107 s).
- **2026-10-11** – **Overenie s 5 seedmi po oprave A: pôvodný trafficManager vs. lokálne riadenie**
  (802.11p, 120 s, 5 RSU, `--seed 1..5`, priemer ± sm. odch.):

  | autá | režim | max státie [s] | priem. najdlhšie státie [s] | čas v státí [%] | rýchlosť [km/h] | PRR |
  |---|---|---|---|---|---|---|
  | 10 | pôvodne (`--tls-radius 0`) | 104.1 ± 2.3 | 40.1 ± 1.7 | 34.1 ± 1.5 | 41.5 ± 0.8 | 0.994 |
  | 10 | lokálne (každá RSU svoj semafor) | **17.1 ± 2.3** | **2.9 ± 0.4** | **3.2 ± 0.5** | **55.2 ± 1.5** | 0.995 |
  | 30 | pôvodne | 105.8 ± 0.5 | 44.3 ± 0.7 | 37.5 ± 0.7 | 38.9 ± 0.5 | 0.996 |
  | 30 | lokálne | **23.8 ± 3.0** | **7.4 ± 1.0** | **8.7 ± 1.2** | **49.8 ± 1.1** | 0.997 |

  Pôvodný režim má po oprave A rovnakú dopravu ako pred ňou (boj RSU o semafory prevláda nad
  kvalitou dát). Lokálny režim pri 30 autách po oprave A mierne horší než pred ňou
  (18.8 ± 0.5 s / 7.0 % -> 23.8 ± 3.0 s / 8.7 %) – potvrdené 5 seedmi: viac doručených CAM =
  viac okamžitých prepnutí v algoritme autorov.
- **2026-10-12** – **Algoritmy CAM 1-3** (viď Moje zmeny). Princíp: 1 = SPaT – auto stojace pri červenej
  (farbu oznámi RSU beaconom) potláča dynamické CAM; 2 = RSU v beacone (200 ms) posiela zoznam áut, ktorých
  CAM počula, nepotvrdené auto CAM raz zopakuje; 3 = relevancia – mimo oblasti, ktorú RSU oznámi (250 m
  pred jej semaforom na trase auta), len 1 CAM/s. Meranie: skutočné PRR auto-auto (`XGREGORM-V2V`,
  viď Interpretácia metrík), uplink do RSU, presnosť polohy, ktorú RSU pozná. Bez algoritmov bit po bite
  zhodné s referenciou. Hypotéza „zhluk CAM po zelenej“ vyvrátená (uplink do 3 s po zelenej 1.0000 vs. 0.9996).
  Prvá verzia (porušovala ETSI, viď nižšie), 30 áut priemer 3 seedov: alg. 3 CAM −55 %, rámce −51 %,
  PRR a doprava bez zmeny; alg. 1 −2.5 % CAM; alg. 2 +21 % rámcov bez prínosu. 100 áut (seed 1, beh so
  150 autami a seedy 2-3 sa prerušili): strata auto-auto 0.12 % -> ACK 0.07 %, 1+2+3 0.05 % pri −55 % CAM;
  alg. 1 a 3 samostatne stratu zvýšili (0.33 %, 0.20 %) – neoverené viac seedmi.
- **2026-10-12** – **Algoritmy upravené podľa ETSI EN 302 637-2** (odstup CAM 100-1000 ms, prahy 4°/4 m/0.5 m/s;
  kód autorov ich dodržiava). Prvá verzia porušovala: alg. 1 držal CAM až 5 s; opakovanie (alg. 2) a vynútená
  CAM pri zmene oblasti (alg. 3) mohli ísť < 100 ms po predošlej; `forceCam` skrátil T_GenCam (nasledujúce
  CAM potom chodili častejšie) a časový trigger po nej prišiel až o ~1.1 s. Oprava: brána (`camGate`) nikdy
  nepustí CAM < 100 ms po predošlej; alg. 1 a 3 len predlžujú minimálny odstup dynamických CAM najviac na
  1 s (ako DCC, T_GenCam_DCC ≤ T_GenCamMax), časová CAM po 1 s ide vždy; `XgHoldMax` predvolene 1, povolené
  0.1-1; opakovanie najskôr 100 ms po nepotvrdenej CAM a len ak medzitým neodišla nová; `forceCam` zachová
  T_GenCam a reštartuje kontrolu podmienok. Nová kontrola `XGREGORM-GAP` (min/max odstup CAM, porušenia).
  Odchýlka, ktorú treba v práci uviesť: štandard dovoľuje obmedziť dynamické CAM len kvôli DCC (zaťaženie
  kanála), my podľa relevancie/SPaT. `./ns3 build` celý. Test 30 áut, 120 s, bez `--seed`, 1 beh:

  | konfigurácia | CAM | rámce/s | strata auto-auto | uplink | min/max odstup | porušenia ETSI |
  |---|---|---|---|---|---|---|
  | bez algoritmov | 14510 | 120.9 | 1/37318 | 1.0000 | 100/1000 ms | 0 |
  | 1 (SPaT) | 14390 (−0.8 %) | 126.2 (+4 %) | 1/36779 | 1.0000 | 100/1000 | 0 |
  | 2 (ACK) | 14517 (31 opakovaní) | 144.9 (+20 %) | 19/37427 | 1.0000 | 100/1000 | 0 |
  | 3 (relevancia) | **6399 (−56 %)** | **58.1 (−52 %)** | **0/23477** | 1.0000 | 100/1000 | 0 |
  | 1+2+3 | 6345 (−56 %) | 78.2 (−35 %) | 14/23091 | 1.0000 | 100/1000 | 0 |

  Doprava vo všetkých rovnaká (49.0 km/h, 10.3-10.4 % státia, max 25.4 s). Referencia bez algoritmov je
  bit po bite zhodná s predošlou (`none,...,0.997178,...,14510`). Záver: v rámci ETSI má zmysel len alg. 3;
  alg. 1 pri limite 1 s nemá čo ušetriť (stojace auto aj tak posiela 1 CAM/s); alg. 2 pri 30 autách
  (kanál takmer prázdny) len pridáva beacony a stratu mierne zvyšuje.
- Pozn.: v sekcii „Tok komunikácie“ je prehľad, čo ide cez ns-3 (CAM, kábel RSU-RSU) a čo cez
  TraCI (RSU -> semafor, mapa).

## Čo robiť, keď si na pochybách

- Pred zmenou v kóde autorov sa spýtaj; je to vec, ktorú treba v práci popísať a nahlásiť do Issues.
- Nevymýšľaj si hodnoty metrík. Ak beh spadol, povedz to a ukáž chybovú hlášku.
- Dlhé behy (LTE, NR-V2X) spúšťaj najprv s menším `--vehicles` a `--sim-time`, kým sa overí, že bežia.
- LTE beh trvá rádovo 4 minúty pri 30 vozidlách a 120 s simulácie; nerušiť ho predčasne.
