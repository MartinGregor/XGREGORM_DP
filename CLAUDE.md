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
| `trafficManagerClient80211p.cc/.h`, `trafficManagerServer80211p.cc/.h`, `v2i-trafficManager-80211p.cc` | **algoritmus CAM podľa relevancie** (atribúty klienta `XgRelax`, `XgRelevance`; CLI `--xg-relax`, `--xg-relevance`): ďalej než 250 m pred ďalším semaforom na trase (z mapy) najviac 1 CAM/s, pri vstupe/odchode z oblasti CAM hneď; dodržiava ETSI EN 302 637-2 (odstup 100-1000 ms). Algoritmy 1 (SPaT) a 2 (potvrdenie + opakovanie) **odstránené 2026-10-12** (výsledky ostávajú v Denníku). Merania `XGREGORM-AW/-V2V/-VEH/-GAP/-PRR(V)`; viď Denník 2026-10-12 | `xgregorm_kod_zaloha/` |
| `trafficManagerClient80211p.cc/.h`, `trafficManagerServer80211p.cc/.h` | **hybrid 802.11p + LTE + NR-V2X**: klient atribúty `XgHybrid` (0 = vypnuté/pôvodné, 1 = každá CAM všetkými technológiami, 2 = adaptívne), `XgPaths` (maska 1 = 802.11p, 2 = LTE na `ServerAddr` UDP 9, 4 = NR-V2X multicast 225.0.0.0:19), `XgHybRange`, `XgHybQmin`, `XgHybDwell`; server `XgHybrid` (1 = prijíma CAM z kábla UDP 9, 2 = aj beacon s potvrdeniami cez 802.11p aj NR), `XgNr` (NR sidelink na RSU), zahadzuje staršie kópie (generationDeltaTime, od 2026-10-13 len do 10 s od posledného príjmu – oprava pretečenia), vlastné CAM RSU idú cez prepínač (pri výpadku 802.11p nevysiela); od 2026-10-13 bez priamej technológie posiela cez LTE len auto, ktoré radič potrebuje (+1 s po opustení príjazdu); výpisy `XGREGORM-HYB` (P0..P7 = CAM podľa kombinácie ciest), rozšírené `XGREGORM-AW`; od 2026-10-19 atribút `XgColor` (farba auta v SUMO podľa technológie, len vizualizácia). Pri 0 bez zmeny (overené bit po bite pred pridaním NR) | `xgregorm_kod_zaloha/` |
| `xgregorm-hybrid-socket.h` (nový), `xgregorm-hybrid.cc` (nový príklad), `src/automotive/CMakeLists.txt`, `src/automotive/examples/CMakeLists.txt` | prepínač ciest CAM pod GeoNet (Socket, ktorý posiela cez 802.11p / LTE / NR-V2X), výpadky technológií počas behu (`xghyb::up`, aj viac intervalov) a hybridný scenár (viď Denník 2026-10-12); od 2026-10-13 `--channel authors/3gpp`, `--enb`/`--enb-isd` (viac LTE buniek s odovzdávaním X2), `--lte-ue-power`, `--range-stats`, výpis `XGREGORM-LTEHO`; meranie hustoty a pravidlá P1-P1d (2026-10-13) **odstránené**, kód v `xgregorm_kod_zaloha/xgregorm_hustota_P1-P1d_diff.txt` | `xgregorm_kod_zaloha/` |
| `src/nr/model/nr-net-device.cc/.h` (5G-LENA) | **oprava pádu pri veľkej záťaži NR sidelinku**: `NrNetDevice::Receive` pri pakete bez platnej IP hlavičky ukončil celú simuláciu (`NS_ABORT_MSG "Unknown IP type"`, 500 áut na city-center v 3. s); teraz paket zahodí a započíta (`NrNetDevice::m_xgUnknownIpDrops`, výpis `XGREGORM-NRDROP`). Príčina (pravdepodobne skladanie segmentov RLC pri záťaži) nevyriešená; v overovacom behu (500 áut, 6 s) 2 zahodené pakety | `xgregorm_kod_zaloha/nr-net-device.cc/.h` |
| `src/nr/model/nr-ue-mac.cc` (5G-LENA) | **> 255 NR zariadení**: SCI stage 2 nesie zo zdrojového L2 ID len 8 bitov (3GPP; zvyšných 16 je v realite v MAC hlavičke SL-SCH, ktorú model nemá) -> ID sa opakovali (IMSI 256 -> 0 -> `NS_ABORT` „Layer 2 source ... shouldn't be 0“, IMSI 1 a 257 zdieľali prijímaciu RLC -> zle poskladané pakety „Unknown IP type“). Plné 24-bitové ID ide ako značka paketu `XgSlSrcL2IdTag` pri SCI 2 (veľkosť vysielania sa nemení); pri < 256 zariadeniach bit po bite bez zmeny | `xgregorm_kod_zaloha/nr-ue-mac.cc` |
| `src/lte/model/nr-sl-ue-rrc.cc` (5G-LENA; `src/lte/` je v `.gitignore`, zmena je v repozitári len ako `xgregorm_kod_zaloha/nr-sl-ue-rrc_xgregorm_diff.txt`) | **chyba autorov 5G-LENA**: `DoAddNrSlRxDataRadioBearer` hľadal prijímací bearer v `m_slRxDrbMap`, ale vkladal do `m_slDrbMap` (vysielacie bearery podľa cieľa); pri zdroji s L2 ID = cieľu vlastného vysielania (255) sa vloženie neurobilo, RLC/PDCP zaniklo a MAC držal neplatný ukazovateľ -> SIGSEGV v `NrSlBwpManagerUe::DoReceiveNrSlRlcPdu` (500 áut pri výpadku 802.11p). Opravené vkladanie do `m_slRxDrbMap`; pri < 255 zariadeniach bez zmeny | `xgregorm_kod_zaloha/nr-sl-ue-rrc.cc` |
| `xgregorm-stats.h` (nový, `model/Applications/`), `src/automotive/CMakeLists.txt` | merania (len čítanie zo SUMO): presnosť informácie RSU, uplink do RSU, skutočné PRR auto-auto; hlavička zaradená do zoznamu hlavičiek modulu; od 2026-10-14 záťaž technológií a výkonnosť podľa záťaže (`trafficManagerClient80211p.cc` posiela do `onCamTx` cesty CAM a ID uzla) | `xgregorm_kod_zaloha/automotive_CMakeLists.txt` |

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
- **Dosah technológií v simulácii (namerané 2026-10-13, `--range-stats`, doručenie ≥ 90 %):**

  | technológia / kanál | dosah | realita (literatúra) |
  |---|---|---|
  | 802.11p, kanál autorov (LogDistance exp. 3, 30 dBm) | **~130 m, od ~145 m nič** (ostrý zlom: prah detekcie preambuly ns-3 -82 dBm, bez únikov) | 90 % do ~500 m mesto, ~700-1200 m priama viditeľnosť/diaľnica |
  | 802.11p, kanál autorov, 23 dBm | ~75 m | – |
  | 802.11p, `--channel 3gpp` (TR 37.885 V2V highway ako NR), 23 dBm | ~200 m (50 % pri ~500 m) | – |
  | 802.11p, `--channel 3gpp`, 30 dBm | ~420 m | – |
  | NR-V2X (autori: 3GPP V2V highway, 23 dBm, MCS 14) | ~400-450 m (80 % pri 800 m); aj zblízka len ~97 % (kolízie mode 2 bez sensingu) | C-V2X ~1.3-2x dosah 802.11p (5GAA/GSMA, nezávislé testy menší rozdiel) |
  | LTE (1 bunka, Friis) | celá mapa 100 % | ideálne pokrytie – optimistické |

  Kanál autorov je pre 802.11p výrazne pesimistický a 802.11p a NR majú **rôzne modely** (rozdiel dosahu
  je hlavne z modelu, nie z technológie). `--channel 3gpp --tx-power 23` dá obom rovnaký model aj výkon
  (pomer dosahu ~2x ako v literatúre). Predvolené ostáva `authors` (na rozhodnutie).
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
python3 xgregorm_gen_scenario.py --run all                  # všetkých 5 simulácií autorov
python3 xgregorm_gen_scenario.py --run hybrid                # môj hybrid 802.11p + LTE + NR-V2X, adaptívne
python3 xgregorm_gen_scenario.py --run hybrid --tech 80211p,lte --policy all   # vybrané technológie, každá CAM všetkými
python3 xgregorm_gen_scenario.py --run hybrid --fail-80211p 60:90              # výpadok 802.11p od 60 do 90 s
python3 xgregorm_gen_scenario.py --run hybrid --fail-nr 30:31,40:42             # viac krátkych výpadkov (blikanie)
python3 xgregorm_gen_scenario.py --run hybrid --channel 3gpp --tx-power 23     # 802.11p a NR na rovnakom kanáli 3GPP
python3 xgregorm_gen_scenario.py --run hybrid --tech nr --policy all --range-stats 1   # doručenie podľa vzdialenosti
python3 xgregorm_gen_scenario.py --name velke --grid 5 --step 250 --rsu 13 --vehicles 250 --run hybrid   # veľké mesto
python3 xgregorm_gen_scenario.py --map city-center --diag          # druhá mapa: centrum, 9 semaforov, 3 pruhy, 600 áut
python3 xgregorm_gen_scenario.py --vehicles 30 --run hybrid --fail-80211p 40:70 --gui --colorful   # autá farebne podľa technológie
python3 xgregorm_gen_scenario.py --vehicles 30 --sim-time 60 --run hybrid --fail-80211p 20:40 --live 1   # každú 1 s: % áut na technológiách + ich záťaž
./ns3 build                                                 # len po zmene C++ kódu
```

- **Grafika:** `--gui` otvorí `sumo-gui` (WSLg, `DISPLAY=:0` funguje). Simulácia sa spustí sama
  (TraciClient posiela `--start --quit-on-end`) a beží najrýchlejšie, ako vie – na sledovanie
  nastav v okne **Delay** (napr. 100 ms). Po konci sa okno samo zavrie. RSU = oranžové body (POI).
  Mapa bez ns-3: `sumo-gui -c src/automotive/examples/xgregorm_mesto/xgregorm_map.sumo.cfg`.
- Paralelné behy: každý s iným `--name` (vlastný priečinok scenára aj `xgregorm_tmp_<name>`), štart posunúť
  o pár sekúnd (voľný port SUMO). Priečinky `xgregorm_<name>` po teste zmazať.
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
  | `--tx-power` | 30 dBm | 30 dBm (bez zmeny) | v norme ETSI (max 33 dBm); s kanálom autorov (LogDistance, exp. 3) dosah ~200 m (**oprava 2026-10-13: nameraných ~130 m**, viď Interpretácia metrík) |

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
- **2026-10-12** – **Hybridný scenár 802.11p + LTE** (`xgregorm-hybrid.cc`, v skripte `--run hybrid --hybrid 0..3`,
  `--run all` ho nespúšťa). Každé auto má 802.11p aj LTE UE, jedna makro bunka eNB v strede mesta (30 m),
  EPC -> server v sieti (p2p, `--core-delay` 5 ms ako autori) -> kábel CSMA (100 Mb/s, 1 ms) ku všetkým RSU;
  RSU = radič semaforu svojej križovatky (ako v 802.11p). Infraštruktúra je rovnaká vo všetkých režimoch,
  mení sa len cesta CAM: 0 = len 802.11p, 1 = len LTE, 2 = vždy oboje, 3 = adaptívne.
  Princíp: CA služba (ETSI) rozhodne, **kedy** CAM poslať, prepínač pod GeoNet (`XgHybridSocket`) **ktorou cestou**.
  Adaptívne pravidlo (každých 100 ms, auto): 802.11p je základ (CAM je hlavne pre susedné autá, LTE ich
  nedosiahne). LTE sa pridá (DUAL), len ak (a) auto je do `XgHybRange` (250 m) pred semaforom, teda ho radič
  potrebuje, a (b) nemá dôkaz, že ho RSU cez 802.11p počuje: beacon jej RSU za poslednú 1 s a kĺzavý priemer
  potvrdených CAM (zoznam v beacone, α = 0.2) ≥ `XgHybQmin` 0.9. Späť na samotné 802.11p až po `XgHybDwell` 1 s
  dobrého spojenia (hysterézia). Samotné LTE adaptívny režim nepoužíva nikdy (auto by zmizlo susedom).
  RSU spracuje len CAM novšiu než poslednú od auta (DUAL kópia cez LTE príde ~18 ms po 802.11p).
  **MetricSupervisor je v hybride vypnutý** – CAM poslanú oboma cestami počíta ako dva príjmy (PRR > 1 ->
  pád) a LTE CAM ako nepočutý broadcast. Merania: skutočné PRR auto-auto, uplink, vek a presnosť informácie
  RSU do 100 m aj na celom príjazde, oneskorenie po každej ceste, podiel ciest, prepnutia (`XGCSV-HYB`).
  Opravené pri teste: auto bez alg. 2 spadlo na beacone s potvrdeniami (chýbal generátor jittra).
  Overenie len chodu (10 áut, 60 s, nie porovnanie): adaptívny 71.5 % CAM 802.11p / 28.5 % oboje, 30 prepnutí,
  oneskorenie 802.11p 0.27 ms, LTE + jadro + kábel 18.5 ms; len LTE: susedia 0 CAM, uplink 0.9988.
  Referencia `v2i-trafficManager-80211p` po zmenách bit po bite rovnaká. Porovnanie režimov zatiaľ nerobené.
- **2026-10-12** – **Hybrid rozšírený o NR-V2X a vypínanie technológií** (nahrádza `--hybrid 0..3` vyššie).
  Technológie: `--use-80211p/--use-lte/--use-nr` (v skripte `--tech 80211p,lte,nr`, ľubovoľná podmnožina).
  Vypnuté LTE/NR sa vôbec neinštaluje; 802.11p zariadenie ostáva vždy (GeoNet berie adresu zo zariadenia 0),
  ale nič nevysiela (ani CAM samotných RSU). Politika `--policy all` = každá CAM všetkými zapnutými (jedna
  technológia = čistá referencia), `adaptive` = pravidlo nižšie. Výpadok počas behu `--fail-80211p/-lte/-nr od:do`
  (`xghyb::up`): prepínač na vypadnutú cestu nič nepošle, RSU na nej neposiela beacony, auto o výpadku vie (modem).
  NR-V2X: nastavenie ako `v2v-emergencyVehicleAlert-nrv2x` autorov (5.89 GHz, 40 MHz, 23 dBm, MCS 14, mode 2
  bez gNB, bearer od 2 s), sidelink majú autá aj RSU (RSU ako UE, ako C-V2X RSU v realite), IP 8.0.0.0/8,
  multicast 225.0.0.0 port 19 (CAM) a 20 (beacony). 802.11p a NR sú na oddelených kanáloch (vzájomné rušenie
  sa nemodeluje, na to je príklad autorov `v2v-coexistence-80211p-nrv2x`).
  Adaptívne pravidlo (auto, každých 100 ms, len z toho, čo samo vie):
  1. priama technológia pre susedov: 802.11p, ak je dostupné, inak NR-V2X (CAM je hlavne pre susedov);
  2. ak radič najbližšieho semafora auto potrebuje (do 250 m) a RSU mu cez primárnu technológiu nepotvrdzuje
     CAM (beacon za 1 s a ≥ 90 % potvrdených), ale cez druhú priamu áno, prepne sa na ňu;
  3. ak potvrdenie nemá ani tak, pridá zálohu: LTE (nezávislé od dosahu), bez LTE druhú priamu technológiu;
  4. zálohu vypne až po 1 s dobrého spojenia; bez priamej technológie posiela len cez LTE.
  Merania navyše: CAM podľa kombinácie ciest (P0..P7), oneskorenie auto -> RSU pre 802.11p/LTE/NR, auto -> auto
  pre 802.11p/NR (`XGREGORM-V2VLAT`), skutočné PRR auto-auto počíta každú CAM raz (prvá kópia, párovanie podľa
  generationDeltaTime). Časová os `XGREGORM-HYBT` (CAM za 1 s podľa kombinácie ciest, skript ju vypíše po 5 s).
- **2026-10-12** – **Prvý test hybridu 802.11p + LTE + NR-V2X** (10 áut, 60 s, 1 beh, adaptívne).
  Opravy pri teste: (1) LTE a NR majú každé vlastný EPC helper s natvrdo rovnakými internými adresami
  (PGW 7.0.0.1) -> `Address Collision`; NR sidelink LTE EPC nepríjme (`DoActivateEpsBearerForUe` padne na
  LTE UE), preto dve oddelené siete a vypnutá len globálna kontrola kolízií (`Ipv4AddressGenerator::TestMode`,
  len keď sú zapnuté obe); (2) helpery LTE/NR boli lokálne v `if` bloku -> zanikli pred štartom -> pád;
  (3) adaptívne pravidlo záviselo od histórie: NR sa pri príjazde použilo, len ak ho niekto „náhodou“ skúsil
  (RSU posiela beacon cez technológiu, len keď ňou niečo počula) -> prepísané (body 1-3 v Princípe nižšie)
  a auto na NR skúša raz za sekundu 802.11p. Výsledné pravidlo v oblasti radiča: RSU potvrdzuje 802.11p ->
  802.11p; len NR -> NR + skúška 802.11p; nič -> všetky priame + LTE (LTE ešte 1 s po potvrdení).
  Výpadok 802.11p 25-45 s (časová os, % CAM 802.11p / LTE / NR):
  pred 83-100 / 0-20 / 2-20; počas 0 / 3-13 / 99-100 (1.4 % CAM v prvých ≤ 100 ms výpadku sa stratilo);
  po 68-77 / 15-28 / 35-56. NR po výpadku = autá 150-250 m pred RSU, kam 802.11p beacon nedosiahne (rovnaké
  pravidlo ako pred výpadkom, overené výpisom stavov; viac áut v tej vzdialenosti). Skutočné PRR auto-auto
  1.000, uplink do RSU 1.000, odstupy CAM podľa ETSI bez porušenia. Oneskorenie auto -> RSU: 802.11p 0.27 ms,
  NR 11.5 ms, LTE + jadro + kábel 18.4 ms; auto -> auto: 802.11p 0.27 ms, NR 8.2 ms.
  Vypnutie technológie prepínačom: `--tech 80211p,lte --fail-lte 20:40` OK (LTE 0 % počas výpadku, 802.11p
  100 %); `--tech nr --policy all` OK, ale samotné NR: PRR auto-auto 0.944, uplink 0.946 (do 3 s po zelenej
  0.892) - sidelink mode 2 bez sensingu (`EnableSensing=false` ako u autorov) má kolízie. Beh s NR trvá ~1 min
  pri 10 autách / 60 s (bez NR ~30 s).
- **2026-10-12** – **Upratanie: ostáva len algoritmus 3 (relevancia).** Algoritmy 1 (SPaT, držanie CAM pri
  červenej) a 2 (potvrdenie v beacone + opakovanie CAM) odstránené z kódu aj zo skriptu (`--xg-spat`, `--xg-ack`,
  `--xg-hold-max`, `--xg-beacon-ms` už neexistujú): v rámci ETSI prínos ~0 % / pri malej záťaži škodili (viď
  vyššie). Algoritmus 3 už **nepotrebuje beacon RSU**: relevanciu určuje auto samo z mapy (ďalší semafor na
  trase bližšie než `--xg-relevance`, predvolene 250 m; predtým navyše musel byť počuť beacon jeho RSU). Výsledky
  alg. 3 v tabuľkách vyššie sú s beaconom, nová verzia môže dať mierne iné čísla (netestované).
  Beacon RSU ostáva **len v adaptívnom hybride** ako jeho spätná väzba, zúžený na `XG|semafor|začiatok_okna_ms|id;id;...`
  (bez stavu semafora a relevancie), perióda `--beacon-ms` 200 ms (hybrid). Bez `--policy adaptive` RSU nevysiela
  žiadny beacon. Stĺpec `retx` z `XGCSV` vypadol. Skompilované, netestované (na želanie).
- **2026-10-12** – **Test prepínania technológií v hybride** (adaptívne, 15 áut, 90 s, 1 beh na prípad).
  Nové merania: časová os doručenia po 1 s (`XGREGORM-LOSST`; skript: % CAM po technológiách | uplink do RSU
  < 100 m, auto-auto < 100 m | RSU o aute nevie %, vek informácie) a zoznam sekúnd s uplink alebo auto-auto
  < 90 % alebo vekom > 1.5 s. Opravené:
  1. Meranie auto-auto počítalo očakávané príjmy len pre CAM poslané priamo -> pri samotnom LTE ukazovalo „-“
     namiesto straty. Teraz má každú CAM dostať každý sused do 100 m (LTE-only = 0 %, poctivé).
  2. Pri výpadku posielalo auto do 100 ms (do ďalšieho rozhodnutia) na vypadnutú technológiu (1.4-1.8 % CAM sa
     stratilo, uplink v tej sekunde 96 %). Teraz `xgEffectivePath`: vypadnutá cesta sa hneď nahradí (802.11p, NR,
     LTE) -> 0 stratených CAM pri prepnutí.
  3. Väčšia mapa (`--step 800`, príjazdy ~800 m): autá na začiatku príjazdu (780-790 m od RSU, ďalej než
     `--hyb-range`) nedosiahli RSU ani 802.11p, ani NR a LTE nepridali -> RSU mala o nich informáciu starú až 9-58 s
     (vek na príjazde 1.09 s, chyba 13.9 m). Teraz auto radič „potrebuje“ na **celom príjazde** k ďalšiemu semaforu
     (cesty, ktorých pruhy semafor riadi - MAPEM) alebo do `--hyb-range` -> vek 0.21 s, chyba 3.5 m, LTE menej
     (15 -> 10 CAM/s). Na veľkej mape NR nedosiahne ~780 m, LTE tam zaskočí.
  Výsledky (pôvodná mapa, % CAM 802.11p / LTE / NR počas výpadku; doručenie):
  bez výpadku: 74 % CAM len 802.11p, auto-auto 0.999, uplink 0.998, vek 0.21 s;
  výpadok 802.11p 30-60 s: 0 / 4-32 / 100, uplink 96-100 %, auto-auto 92-100 % (NR kolízie, viď nižšie);
  výpadok NR 30-60 s: 100 / 21-56 / 0 (LTE nahrádza NR na vzdialenejších príjazdoch), všetko 100 %;
  výpadok LTE 30-60 s: 78-90 / 0 / 21-54, všetko ~100 %;
  výpadok 802.11p 30-60 s + NR 40-50 s: 40-50 s len LTE: uplink 100 %, auto-auto 0 % (fyzikálne nevyhnutné).
  Vek informácie RSU vo všetkých prípadoch 0.14-0.42 s, odstupy CAM podľa ETSI bez porušenia.
  **NR samotné stráca ~5 %** (auto-auto 0.947, uplink 0.946, do 3 s po zelenej 0.89): mode 2 bez sensingu
  (autori). `--nr-sensing 1` (nový prepínač) je nepoužiteľne pomalé: 15 áut / 60 s nedobehlo za 30 min (bez neho
  ~1 min) -> ostáva vypnuté.
- **2026-10-13** – **Rozsiahly test prepínania technológií + kontrola realizmu** (~50 behov, 30-500 áut).
  Výsledky (adaptívne, 30 áut, 120 s, ak nie je uvedené inak): všetky behy bez pádu, 0 porušení ETSI, žiadna CAM
  na vypadnutej technológii, návrat technológie po výpadku do 1 s. Bez výpadku uplink 0.999, auto-auto 0.999, vek
  informácie RSU 0.21 s. Výpadok 802.11p: 100 % CAM na NR (+ LTE pre vzdialené príjazdy), doručenie ~99.7 %.
  Výpadok NR / LTE / oboch priamych / všetkých troch (50-60 s: CAM sa stratia, po obnovení hneď OK), blikanie
  802.11p (0.1-5 s výpadky) aj NR, výpadok od štartu aj do konca, postupné výpadky, podmnožiny technológií,
  veľká mapa (`--step 800`), 3 seedy (rozptyl malý: uplink 0.997-0.999), 100 a 200 áut (200 áut, mesto 5x5,
  13 RSU, výpadok 802.11p: 100 % NR, uplink 0.997, auto-auto 0.996) – všetko podľa očakávaní. Fyzikálne straty:
  bez priamej technológie auto-auto 0 %; len 802.11p (dosah ~130 m) RSU nevidí začiatok príjazdov.
  Opravené: (1) **RSU ignorovala auto, ktoré nepočula 33-65 s** (porovnanie generationDeltaTime modulo 65.536 s
  vyhodnotilo novú CAM ako starú, až ~32 s) – porovnanie len do 10 s od posledného príjmu; (2) vrátená priama
  technológia sa použije hneď (predtým do 100 ms len LTE -> auto-auto 89.5 % v tej sekunde); (3) CAM samotných
  RSU rešpektujú výpadok 802.11p; (4) skúška 802.11p sa nepočíta ako prepnutie; (5) generátor: trasy cez
  stred pri `--grid` ≠ 3 (pre 3 bez zmeny); (6) skript: paralelné behy s rôznym `--name` si mazali dočasný priečinok.
  Nové: `--fail-*` viac intervalov, `--range-stats`, `--channel 3gpp`, `--enb/--enb-isd`, `--lte-ue-power`, meranie
  „fantómov“ (radič počíta auto na príjazde, ktoré tam už nie je).
  **Kapacita LTE:** 1 bunka (ns-3: 5 MHz, max ~320 UE). 250 áut len cez LTE (~870 CAM/s): všetko doručené, ale
  oneskorenie 289 ms a rastie (bunka preťažená). 250 áut, výpadok 802.11p: uplink 73-88 %, LTE 168 ms.
  **Zmena pravidla (posledná, netestovaná po doplnení dobehu):** bez priamej technológie posiela cez LTE len auto,
  ktoré radič potrebuje (CAM cez LTE dostanú len RSU, nie susedia), po opustení príjazdu ešte `XgHybDwell` 1 s
  (aby radič videl odchod). Prvá verzia (bez dobehu) pri 250 autách: LTE 168 -> 19 ms, vek 0.33 -> 0.22 s.
  **Viac LTE buniek (`--enb 4/9`) je v ns-3 nespoľahlivé:** odovzdanie trvá 4 ms, ale potom uplink auta občas
  stojí ~2 s; pri 9 bunkách výpadky rádiového spoja (100 áut: 99 RLF / 30 s, 2.5 % strata) – Friis (bez útlmu
  prekážok) + rovnaká frekvencia = extrémne rušenie susedných buniek. 500 áut (9 buniek) pri výpadku 802.11p
  LTE skolabovalo (uplink 0 %, vek až 15 s) – kombinácia artefaktu a preťaženia, nie výsledok pre prácu.
  NR pri 500 autách nerealizovateľné (odhad ~8 h behu).
  Realizmus dosahov: viď tabuľka v Interpretácii metrík (802.11p u autorov ~130 m vs. realita stovky metrov).
  Referencia `v2i-trafficManager-80211p` bit po bite bez zmeny. Algoritmus 3 bez beaconu (30 áut): CAM −39 %
  (s beaconom −56 %, auto je teraz relevantné celých 250 m pred semaforom), 0 porušení ETSI, doručenie 100 %.
- **2026-10-13** – **Cieľ práce upresnený: výber RAT podľa hustoty** (oficiálne zadanie = mechanizmy výberu
  rádiového prístupového systému; vlastný návrh preformulovaný z „routingu“ na výber RAT). Bod 3 = hustota:
  1. **Meranie hustoty** (každé auto každých 100 ms): susedia počutí za 1 s cez 802.11p / NR, CBR 802.11p (podiel
     času RX + TX + CCA busy za 100 ms, ETSI TS 102 687). Adaptívne, 60 s:

     | autá | kanál | susedia 802.11p / NR | bez suseda na 802.11p | CBR priemer / max | auto-auto <100 m |
     |---|---|---|---|---|---|
     | 10 | autorov | 0.3 / 2.0 | 72 % | 0.001 / 0.008 | 1.000 |
     | 30 | autorov | 2.2 / 6.9 | 19 % | 0.003 / 0.018 | 0.998 |
     | 100 | autorov | 8.7 / 19.1 | 2 % | 0.006 / 0.034 | 0.998 |
     | 200 (5x5, 13 RSU) | autorov | 6.3 / 50.2 | 1.3 % | 0.005 / 0.022 | 0.999 |
     | 30 | 3gpp 23 dBm | 20.3 / 2.5 | 0 % | 0.020 / 0.046 | 0.991 |
     | 100 | 3gpp 23 dBm | 68.4 / 12.3 | 0 % | 0.050 / 0.101 | 0.980 |

     Záver: pri samotných CAM sa hustota prejaví ako **riedkosť**, nie preťaženie (CBR ďaleko pod prahmi ETSI
     DCC 0.3/0.6; jedna CAM ~0.2 ms vysielania, ~4 CAM/s). P2 (preťaženie) potrebuje záťažový scenár (CPM, 3gpp kanál).
  2. **P1 (riedko -> dosah):** auto bez suseda na 802.11p za posledné 2 s (= 2 x T_GenCamMax) posiela CAM aj cez
     NR-V2X; prah = definícia izolácie (bez ladenia). `--density 1`. Test (kanál autorov, 120 s, 5 seedov, 100 áut 60 s
     3 seedy; doručenie auto-auto podľa vzdialenosti, priemer):

     | autá | režim | 0-100 m | 100-200 | 200-300 | 300-450 | 450-600 | NR CAM/s | LTE CAM/s |
     |---|---|---|---|---|---|---|---|---|
     | 10 | adaptívne | 0.999 | 0.51 | 0.29 | 0.24 | 0.17 | 10 | 3 |
     | 10 | **+ P1** | 1.000 | **0.69** | **0.66** | **0.70** | **0.68** | 30 | 3 |
     | 10 | všetky RAT | 1.000 | 0.99 | 0.97 | 0.97 | 0.93 | 42 | 42 |
     | 30 | adaptívne | 0.999 | 0.52 | 0.29 | 0.22 | 0.16 | 28 | 12 |
     | 30 | **+ P1** | 0.999 | 0.55 | 0.36 | 0.36 | 0.32 | 43 | 11 |
     | 30 | všetky RAT | 1.000 | 0.98 | 0.97 | 0.94 | 0.91 | 120 | 120 |
     | 100 | adaptívne / + P1 | 0.998 | 0.48 | 0.24-0.25 | 0.21 | 0.15-0.16 | 73 / 76 | 32 |

     P1 je aktívne v 48 % rozhodnutí pri 10 autách, 12 % pri 30, 0.7 % pri 100 (prispôsobuje sa hustote). Uplink
     (0.998-0.999), vek informácie RSU (0.18-0.24 s), auto-auto do 100 m bez zmeny, 0 porušení ETSI, 0 % „fantómov“.
     Veľká mapa (`--step 800`, 30 áut): 200-600 m 0.32-0.45 -> ~0.70, vek 0.27 -> 0.21 s. Kanál 3gpp: nikto nie je
     izolovaný -> P1 sa nespustí (výsledok zhodný). Okrajové prípady (výpadok 802.11p, NR, blikanie NR, bez NR, bez
     LTE): žiadna sekunda pod 90 %, P1 sa pri výpadku NR nespúšťa. Obmedzenie: P1 pomáha len izolovaným autám –
     auto s aspoň jedným susedom na 802.11p vzdialeným autám neposiela (pri 30 autách preto len 0.16 -> 0.32; „všetky
     RAT“ ~0.9, ale 4x viac NR a LTE prevádzky).
     **Varianty P1b / P1c** (`--density 2/3`; NR navyše, aj keď auto počuje aspoň jedno auto len cez NR / viac áut
     len cez NR než cez 802.11p), rovnaké seedy:

     | autá | režim | 100-200 | 200-300 | 300-450 | 450-600 | NR CAM/s | LTE CAM/s | NR pridané |
     |---|---|---|---|---|---|---|---|---|
     | 10 | P1 / P1b / P1c | 0.69 / 0.99 / 0.98 | 0.66 / 0.97 / 0.95 | 0.70 / 0.97 / 0.96 | 0.68 / 0.93 / 0.93 | 30 / 42 / 42 | 4 | 48 / 82 / 82 % |
     | 30 | P1 / P1b / P1c | 0.55 / 0.98 / 0.98 | 0.36 / 0.96 / 0.96 | 0.36 / 0.94 / 0.94 | 0.32 / 0.91 / 0.91 | 43 / 119 / 119 | 11-13 | 12 / 84 / 83 % |
     | 100 | P1 / P1b / P1c | 0.48 / 0.97 / 0.97 | 0.25 / 0.95 / 0.95 | 0.21 / 0.92 / 0.92 | 0.16 / 0.89 / 0.89 | 76 / 344 / 347 | 33-43 | 0.7 / 87 / 87 % |
     | 30, `--step 800` | d0 / P1 / P1b / P1c | 0.61 / 0.77 / 0.99 / 0.99 | 0.32 / 0.70 / 0.95 / 0.95 | 0.38 / 0.71 / 0.96 / 0.96 | 0.45 / 0.70 / 0.91 / 0.91 | 42 / 81 / 121 / 121 | 24-27 | 0 / 40 / 82 / 83 % |

     P1b a P1c dajú dosah ako „všetky RAT“ (0.9-0.99) bez LTE navyše, ale NR posiela takmer každý takmer stále
     (82-87 % rozhodnutí) -> **nezávisia od hustoty** (NR má ~3.5x dosah = ~12x plochu, auto „len cez NR“ sa nájde
     vždy, aj pri 100 autách; P1c sa preto správa ako P1b). Uplink, vek RSU, auto-auto do 100 m, ETSI bez zmeny.
     Zatiaľ predvolené P1. Na obmedzenie P1b by bola potrebná záťaž NR kanála: NR model v ns-3 SL-CBR nepočíta.
     **Kompromis P1d** (`--density 4`): ako P1b, ale len kým je obsadenosť NR kanála < 0.3 (hranica ETSI DCC
     relaxed/active). Obsadenosť NR = odhad SL-CBR podľa 3GPP: podiel obsadených subkanálov x slotov za 100 ms z každého
     prijatého PSCCH (aj kolízneho), kapacita z konfigurácie (40 MHz: 5 subkanálov x 4000 slotov/s; ns-3 SL-CBR nepočíta).
     Nové prepínače `--nr-bw` (MHz, autori 40) a `--nr-numerology` (pri 1 = 30 kHz padá: výberové okno T2 81 slotov
     > rezervačná perióda 20 ms, preto len 2). Doručenie auto-auto 450-600 m a obsadenosť NR (priemer / max):

     | scenár | P1 | P1b | P1d | NR-CBR pri P1b | NR CAM/s P1b / P1d |
     |---|---|---|---|---|---|
     | 10 / 30 / 100 áut, NR 40 MHz | 0.69 / 0.32 / 0.16 | 0.93 / 0.91 / 0.89 | = P1b | 0.006 / 0.014 / 0.037 (max 0.06) | rovnaké |
     | 100 áut, NR 10 MHz (3 seedy) | 0.15 | 0.75 | 0.75 | 0.18 (max 0.31) | 346 / 346 |
     | 200 áut, malá mapa, NR 10 MHz (1 seed) | – | 0.63 | **0.49** | **0.31** (max 0.47) | 295 / 225 |

     Záver: v realistických hustotách sa NR kanál nepreťaží, P1d = P1b. Pri skutočnom preťažení (200 áut, 10 MHz)
     P1d obmedzí NR (CBR 0.31 -> 0.24), ale dosah klesne (100-200 m 0.88 -> 0.76) a nič iné sa nezlepší (uplink 0.999,
     vek RSU 0.29 s, auto-auto do 100 m 0.998 v oboch) – v našich metrikách obmedzenie nič neprinesie. Zmysel má len ako
     dodržanie štandardu: reálny NR UE musí pri vysokom CBR obmedziť vysielanie (3GPP sidelink congestion control,
     CR limit), model ns-3 v konfigurácii autorov ho nemá -> výhoda P1b pri preťažení je čiastočne artefakt modelu.
     **P1 je v hybride predvolene zapnuté** (`--density 0` = vypnuté na porovnanie; atribút klienta `XgDensity`
     ostáva predvolene false, takže iné príklady sa nemenia).
- **2026-10-13** – **Hustota odstránená, ostáva čisté prepínanie technológií** (na želanie). Z kódu vyňaté: meranie
  hustoty (susedia, CBR 802.11p, odhad SL-CBR NR), pravidlá P1/P1b/P1c/P1d (`--density`), `--nr-bw`, `--nr-numerology`.
  Ostáva: adaptívny výber RAT (potreba radiča, potvrdenia RSU, záloha LTE len keď treba, okamžitá náhrada pri výpadku)
  so všetkými opravami z testu prepínania, `--range-stats`, `--channel`, `--enb`, `--lte-ue-power`, viac výpadkov, fantómy.
  Celý odstránený kód: `xgregorm_kod_zaloha/xgregorm_hustota_P1-P1d_diff.txt` (`patch -p1 < ...` ho vráti; súbor má príponu .txt, lebo *.diff a *.patch sú v .gitignore). Postup
  a výsledky hustoty ostávajú v záznamoch vyššie.
- **2026-10-13** – **Porovnanie čistého prepínania s jednotlivými technológiami** (`xgregorm_mesto`, 30 áut, 120 s,
  seedy 1-3, priemer; jedna technológia = `--tech X --policy all`):

  | konfigurácia | auto-auto <100 m | 100-200 m | 200-450 m | oneskorenie auto-auto | uplink RSU | vek info RSU (príjazd) | RSU o aute nevie | CAM/s 802.11p / LTE / NR |
  |---|---|---|---|---|---|---|---|---|
  | **adaptívne (všetky 3)** | 0.9987 | 0.52 | 0.25 | 2.2 ms | 0.998 | **0.21 s** | 0.24 % | 99 / 11 / 28 |
  | len 802.11p | 0.9996 | 0.25 | 0.00 | 0.27 ms | 0.9998 | 7.85 s | 17.6 % | 120 / 0 / 0 |
  | len LTE | **0.0000** | 0 | 0 | – | 0.9996 | 0.24 s | 0.23 % | 0 / 118 / 0 |
  | len NR-V2X | 0.986 | 0.98 | 0.95 | 10.8 ms | 0.986 | 0.23 s | 1.05 % | 0 / 0 / 118 |
  | všetky naraz | 0.9999 | 0.98 | 0.95 | 0.29 ms | 1.0000 | 0.21 s | 0.24 % | 118 / 118 / 118 |
  | adaptívne, výpadok 802.11p 40-80 s | 0.997 | 0.71 | 0.48 | 6.1 ms | 0.998 | 0.22 s | 0.24 % | 68 / 14 / 56 |
  | len 802.11p, výpadok 40-80 s | **0.545** | 0.14 | 0 | 0.27 ms | **0.596** | 14.3 s | 42 % | 80 / 0 / 0 |

  Oneskorenie auto -> RSU: 802.11p 0.27 ms, LTE + jadro + kábel 18.4 ms, NR 11-13 ms. 0 porušení ETSI. Doprava
  prakticky rovnaká (max státie 23-25 s, státie 9-11 %, 48-50 km/h). Adaptívne = spoľahlivosť „všetko naraz“ pri
  blízkych autách a RSU s ~1/3 prevádzky (LTE 11 namiesto 118 CAM/s, NR 28 namiesto 118); horšie len auto-auto
  nad 100 m (NR samotné/všetko naraz 0.95-0.98, adaptívne 0.25-0.52 – tu pomáhali pravidlá hustoty, teraz odstránené).
- **2026-10-13** – **Druhá mapa „city center“** (`--map city-center`, priečinok `xgregorm_city_center/`; pôvodná mapa
  = `--map default`, ostáva `xgregorm_mesto/`, generuje sa bit po bite rovnako – líši sa len časová pečiatka).
  9 križovatiek so semaforom (všetky 4-ramenné), 3 pruhy v každom smere, nie šachovnica: bloky 171-286 m, ulice
  mierne posunuté/nerovnobežné. Krajné ulice pokračujú 200 m von na jednosmerný okruh (3 pruhy, 50 km/h, zvyšok mesta).
  Pruhy: pravý vpravo + rovno, stredný rovno, ľavý len vľavo; ľavé odbočenie nechránené (`--tls.left-green.time 0`,
  inak netconvert pridá chránené fázy -> 8 fáz a trafficManager počíta so 4). Okruhy áut: vjazd e -> centrom do
  výjazdu x -> 1 úsek okruhu na vjazd x-1 -> centrom do výjazdu e+1 -> vjazd e (120 okruhov, všetky smery).
  Autá štartujú na náhodnom voľnom mieste (`departPos="random_free"`), 9 RSU = 9 semaforov. `--diag` vypisuje
  nové aj rady pred semaformi (stojace autá na príjazd, všetky pruhy, po 100 s rozbehu).
  Ladenie (čisté SUMO, pevné semafory 30 s, 420 s):

  | verzia | autá | teleporty | max státie | rýchlosť | poznámka |
  |---|---|---|---|---|---|
  | obojsmerný okruh, prednosť sprava | 400 / 1000 | 10 / 66 | 180 / 268 s | 21 / 8 km/h | zablokované napojenia na okruh |
  | jednosmerný okruh, autá po ňom ďalej | 400 | 38 | 278 s | 21 km/h | výjazd (vedľajšia) nedostane medzeru; zipper SUMO pri 3 pruhoch nepodporuje |
  | okruh len 1 úsek | 400 / 700 / 1000 | 0 / 29 / 41 | 62 / 204 / 230 s | 24 / 16 / 11 km/h | ľavo odbočujúci blokuje aj rovno |
  | + samostatný ľavý pruh | 400 / 500 / **600** / 700 / 1000 | 0 / 0 / **0** / 2 / 27 | 56 / 97 / **83** / 120 / 159 s | 25 / 22 / **20** / 17 / 12 km/h | **predvolené 600** |

  600 áut: priemerne 10-23 áut v rade na vyťažených príjazdoch (95. percentil 30-40), krajné stredné križovatky
  (C01, C21) len ~5. 1000 áut sieť nezvládne už v SUMO. ns-3 zatiaľ nespustené (na želanie).
- **2026-10-14** – **Meranie záťaže technológií a výkonnosti podľa záťaže** (hybrid, `--load-stats 1` predvolene, len
  meranie – overené: výpisy behu s meraním a bez neho bit po bite zhodné). Každých 100 ms:
  - **802.11p – MERANIE podľa ETSI TS 102 687:** CBR = podiel času, keď je prijatý výkon > −85 dBm. Pri každom vyslaní
    (`PhyTxPsduBegin`, dĺžka rámca z `CalculateTxDuration`) sa pre každú stanicu vypočíta prijatý výkon tým istým modelom
    šírenia ako kanál (autori: LogDistance, deterministický; 3gpp: ten istý objekt, rovnaké poradie výpočtov -> rovnaké
    losovanie stavu kanála). Vlastné vysielanie sa počíta ako obsadené. Zjednodušenie: súčet viacerých slabých signálov
    pod −85 dBm sa neráta. ns-3 samo hlási obsadenie až od −82 dBm (detekcia preambuly), preto vlastný výpočet.
  - **LTE – MERANIE podľa 3GPP TS 36.314 (PRB usage):** využitie PRB uplinku bunky = PRB pridelené v UL DCI všetkým UE
    (`LteUePhy::ReportUlPhyResourceBlocks`) / (PRB bunky × 100 TTI). Bunka 5 MHz = 25 PRB. Cesta CAM cez LTE je len uplink.
  - **NR-V2X – ODHAD (nie meranie podľa normy):** SL-CBR podľa 3GPP TS 38.215 je podiel subkanálov so S-RSSI nad prahom;
    ns-3 (5G-LENA) S-RSSI po subkanáloch nevypisuje. Odhad = subkanály z prijatých (dekódovaných) PSCCH / (subkanály ×
    sloty) za 100 ms (40 MHz: 5 subkanálov × 4000 slotov/s) -> dolný odhad (nedekódované/kolízne prenosy chýbajú).
    **Rozhodnutie: presné meranie by vyžadovalo zásah do NR modulu (trace v `NrSpectrumPhy::StartRx`, vetva sidelinku,
    kde je prijatý výkon po RB), zámerne nerobené – NR ostáva odhad.**
  - **Výkonnosť podľa záťaže odosielateľa v čase vyslania** (koše < 0.1 / 0.1-0.2 / 0.2-0.3 / 0.3-0.6 / ≥ 0.6 podľa ETSI DCC):
    802.11p a NR = PRR auto-auto do 100 m, každá technológia zvlášť (aj druhá kópia); LTE = auto -> RSU (doručená = aspoň
    jedna RSU); oneskorenie priemer, p50/p95/p99, max (logaritmický histogram, krok ~6 %). NR CAM pred vznikom bearera (2 s,
    nastavenie autorov) sa do NR nepočítajú (nemohli sa doručiť – artefakt, 44 z 66 strát v teste). Navyše **PIR** (3GPP TR
    37.885): čas medzi dvoma prijatými CAM od toho istého auta, auto-auto do 100 m, medzery > 5 s vynechané.
  Výpisy `XGREGORM-LOAD`, `XGREGORM-PERF`, `XGREGORM-PIR`. Test (pôvodná mapa, 15 áut, 40 s, adaptívne): CBR 802.11p priemer
  0.002 / max 0.011, PRB LTE 0.004 / 0.100, NR (odhad) 0.002 / 0.009 – všetko v koši < 0.1; PRR 802.11p 1.0000 (646),
  NR 1.0000 (80), LTE 1.0000 (169); oneskorenie 0.27 / 6.3 / 18.4 ms; PIR p50 205 ms, p95 410 ms, max 1000 ms (= interval CAM).
- **2026-10-19** – **Farba áut podľa technológie** (v skripte `--colorful`, v príklade `--color-tech`; predvolene vypnuté; atribút
  klienta `XgColor`, predvolene false). Každých 100 ms klient nastaví autu v SUMO farbu podľa **hlavnej** technológie, ktorou by
  práve odišla CAM (`xgEffectivePath`, bez skúšky 802.11p z NR, aby auto neblikalo): modrá 802.11p, zelená NR, červená
  LTE (len keď nie je priama technológia), sivá žiadna. Záložná kópia tej istej CAM inou technológiou sa nezobrazuje
  (prvá verzia s 8 farbami pre kombinácie bola neprehľadná). Len vizualizácia: výpisy
  behu s farbením a bez neho bit po bite zhodné (10 áut, 20 s, výpadok 802.11p 8-14 s). Prepíše aj červenú farbu sanitiek.
  Test zahltenia (2026-10-19, samostatná kópia v `scratch/`, 15 s na technológiu) bol na želanie odstránený, nezapísaný.
- **2026-10-19** – **Farba zjednodušená na hlavnú technológiu** (4 farby: modrá 802.11p, zelená NR, červená LTE len bez
  priamej technológie, sivá žiadna; záložná kópia sa nezobrazuje) a **priebežný výpis hybridu** `XGREGORM-LIVE`
  (`--live X`, predvolene každú 1 s; 0 = vypnuté): podiel áut podľa hlavnej technológie (802.11p > NR > LTE > žiadna),
  podiel áut so záložnou kópiou (+LTE/NR) a priemerná/max záťaž každej technológie za periódu (CBR 802.11p, odhad SL-CBR
  NR, PRB LTE; vzorka sa berie, len kým technológia nemá výpadok). Klient registruje `xgstats::pathNow()` (nová metóda
  `xgPathNow()` = cesty CAM bez skúšky 802.11p). Test (pôvodná mapa, 30 áut, 60 s, výpadok 802.11p 20-40 s, beh 209 s):
  mimo výpadku 67-100 % áut na 802.11p, zvyšok na NR (autá 150-250 m pred semaforom, kam 802.11p RSU nedosiahne -
  pravidlo 2 adaptívneho prepínača), počas výpadku 100 % NR; záťaž 802.11p 0.2-0.6 %, NR 0.3-0.8 % (výpadok 1.3-1.8 %),
  LTE 0.1-5 %; PRR auto-auto 0.995, uplink 0.998.
  Doplnené: aj podiel áut so **záložnou kópiou** po technológiách (NR, LTE) a **používaných vôbec** (hlavná + záloha;
  súčet môže byť > 100 %). Test výpadok 802.11p 15-35 s + NR 22-28 s: 22 s 100 % LTE (okamžitá náhrada), od 23 s LTE len
  47-50 % áut (tie, ktoré radič potrebuje), zvyšok žiadna; po návrate NR 1-2 s LTE ako záloha 60 % (dobeh 1 s).
- **2026-10-19** – **Hybrid na mape city-center, 500 áut.** Bez NR (`--tech 80211p,lte`, `--enb 4 --enb-isd 300`, 60 s,
  výpadok 802.11p 35-45 s, beh 65 min): prístup k RSU cez 802.11p 80-86 % áut, CBR 802.11p priemer 3-5 % (max 12 %),
  PRR 802.11p 0.993 (CBR < 0.1) / 0.984 (0.1-0.2); pri výpadku 83-84 % áut na LTE, PRB LTE 45-49 % (max 78 %), LTE so
  záťažou: PRR 0.94 -> 0.76, p50 oneskorenia 19 -> 205 ms (časť je artefakt viacerých buniek: 13 RLF, po výpadku PRB
  ostane ~38 %). S NR model 5G-LENA padal v 3. s (`Unknown IP type`) -> oprava v Moje zmeny; overenie 500 áut / 6 s:
  2 zahodené pakety, PRR auto-auto 0.990, ~3 min behu na 1 s simulácie.
- **2026-10-20** – **NR pri > 255 zariadeniach: dve chyby 5G-LENA opravené** (viď Moje zmeny: `nr-ue-mac.cc` – 8-bitové
  zdrojové ID v SCI 2; `nr-sl-ue-rrc.cc` – prijímací bearer vkladaný do zlej mapy). Nájdené cez gdb (`LteUeRrc::AddNrSlDrb`
  NS_ABORT, `NrSlBwpManagerUe::DoReceiveNrSlRlcPdu` SIGSEGV). Overenie: 30 áut bit po bite zhodné s behom pred opravami,
  0 zahodených NR paketov; 500 áut s výpadkom 802.11p (100 % áut na NR) už nepadá, ale je **výpočtovo nepoužiteľné**
  (> 1 h na 1 s simulácie). Bežná prevádzka 500 áut (~15 % na NR) ~3-4 min na 1 s.
- **2026-10-20** – **Test 3 technológií na city-center (po opravách NR).** A: 500 áut, 15 s, `--enb 4`, výpadok NR 6-9 s
  a LTE 10-12 s (29 min): bez pádu, 0 zahodených NR paketov; hlavná technológia 85-88 % 802.11p / 12-16 % NR, prístup k RSU
  cez 802.11p 80-86 %; záťaž 802.11p 3-5 % (max 11 %), NR 3-5 %, LTE 0-27 %; PRR auto-auto 0.993, uplink 0.996, vek RSU
  0.23 s; PRR podľa technológie 802.11p 0.992, NR 0.997, LTE 0.87-0.98 (LTE oneskorenie priemer 242 ms – štart a viac buniek).
  Výpadok NR: 100 % áut na 802.11p, LTE záloha 13 -> 26 %; výpadok LTE: zálohy 0 %, nič sa nestratilo.
  B: 200 áut, 30 s, 1 bunka, výpadok 802.11p 10-20 s + NR 15-17 s (18 min): pri výpadku 802.11p 100 % na NR (záťaž NR
  2 -> 7 %, LTE záloha 32-44 %), pri dvojitom výpadku 94-100 % na LTE (PRB 22-39 %), po návrate do 1 s späť; PRR 802.11p
  0.997, NR 0.992, LTE 0.955-1.0 (18-31 ms); auto-auto 0.925 (vrátane 2 s bez priamej technológie), uplink 0.994.
- Pozn.: v sekcii „Tok komunikácie“ je prehľad, čo ide cez ns-3 (CAM, kábel RSU-RSU) a čo cez
  TraCI (RSU -> semafor, mapa).

## Čo robiť, keď si na pochybách

- Pred zmenou v kóde autorov sa spýtaj; je to vec, ktorú treba v práci popísať a nahlásiť do Issues.
- Nevymýšľaj si hodnoty metrík. Ak beh spadol, povedz to a ukáž chybovú hlášku.
- Dlhé behy (LTE, NR-V2X) spúšťaj najprv s menším `--vehicles` a `--sim-time`, kým sa overí, že bežia.
- LTE beh trvá rádovo 4 minúty pri 30 vozidlách a 120 s simulácie; nerušiť ho predčasne.
