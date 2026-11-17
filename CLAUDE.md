# VaN3Twin - diplomová práca (xgregorm)

Projekt: VaN3Twin (ns-3 + SUMO) na Ubuntu 24.04 vo WSL2. Pracovný priečinok je `~/VaN3Twin/ns-3-dev`.
Cieľ práce: vlastný algoritmus, ktorý zlepší merateľné parametre VANET siete (PRR, latencia,
prepnutia medzi RSU) oproti pôvodným aplikáciám autorov.

**Stav práce, kde sa zastavili behy, časy behov, prenos na iný počítač a prehľad všetkého od prvého spustenia:
`xgregorm_STAV_PRACE.md` (vedľa tohto súboru). Pri pokračovaní v práci čítať najprv ten. Logy a skript série: `xgregorm_logs/`.**

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
| `xgregorm-hybrid.cc`, `xgregorm_gen_scenario.py` (moje) | **realistický mestský kanál `--channel urban`** (2026-10-20): 802.11p a NR = 3GPP TR 37.885 V2V Urban (budovy rozhodnú LOS/NLOS, inak pravdepodobnostne LOS/NLOSv, tienenie, `UpdatePeriod` 100 ms – u autorov 0 = stav kanála sa nikdy neobnoví), LTE = 3GPP TR 38.901 UMa + `BuildingsChannelConditionModel`; výšky eNB 25 m, RSU 5 m, autá 1.5 m; výkon 802.11p 23 dBm (ako NR), LTE UE 23 dBm. **Budovy** generuje skript z blokov medzi ulicami (`xgregorm_buildings.txt` pre ns-3 + `.poly.xml` do SUMO GUI), 15-30 m, 357 kvádrov na city-center | – (môj kód) |
| `xgregorm-hybrid.cc` (môj) | **pozadie `xgbg`**: RSU SPATEM 10 Hz (190 B) + MAPEM 1 Hz (30 + 4·pruhy·77 B) cez 802.11p aj NR; autá so senzormi (`--cpm-share`) CPM podľa ETSI TS 103 324 (kontrola 100 ms, objekty do `--cpm-range` 150 m s priamou viditeľnosťou cez budovy, zaradenie: nový / posun > 4 m / 1 s, 50 + 35·n B, info o senzore 1x za s), cez hlavnú priamu technológiu; každá správa + 44 B BTP/GN + podpis; výpisy `XGREGORM-BG`, `XGREGORM-BGCPM`, `XGREGORM-WIFIFRAMES` | – (môj kód) |
| `xgregorm-hybrid-socket.h` (môj), `trafficManagerClient80211p.cc/.h`, `trafficManagerServer80211p.cc/.h` | **bezpečnosť ETSI TS 103 097** `--security`: 1 = model veľkosti (prepínač pridá k paketu +93 B digest / +242 B certifikát 1x za s; hodnoty zmerané z kryptografie autorov), 2 = skutočná kryptografia GeoNet autorov (atribút klienta aj servera `XgSecurity` -> `setSecurity(true)`; 3x pomalšie a falošné „invalid signature“ pri digeste od neznámeho certifikátu, preto predvolene 1) | `xgregorm_kod_zaloha/` |
| `trafficManagerClient80211p.cc/.h`, `xgregorm-hybrid.cc`, `xgregorm_gen_scenario.py` | **výber technológie podľa záťaže** (2026-10-20, atribút klienta `XgLoadRat` + `XgCbrHigh` 0.3, `XgCbrLow` 0.2, `XgLteHigh` 0.6, `XgLoadPeriod` 1 s; CLI `--load-rat`, `--cbr-high`, `--cbr-low`, `--lte-high`): auto si vedie kĺzavý priemer (~1 s) vlastného CBR 802.11p, odhadu SL-CBR NR a vyťaženia PRB svojej bunky LTE (to oznamuje sieť – 3GPP Rel-16 predikcia QoS); raz za 1 s pri CBR 802.11p ≥ 0.3 a menej zaťaženom NR prejde hlavnou technológiou na NR s pravdepodobnosťou (CBR − 0.25)/CBR (presunie sa len časť áut), späť pod 0.2 alebo pri zahltení NR; keď RSU nepotvrdzuje žiadnu priamu technológiu, druhú priamu pridá len ak nie je zahltená a LTE len ak bunka < 0.6 PRB. Výpis `LOADSW` v `XGREGORM-HYB`. Bez `--load-rat` bit po bite zhodné | `xgregorm_kod_zaloha/` |
| `trafficManagerClient80211p.cc/.h`, `xgregorm-hybrid-socket.h`, `xgregorm-hybrid.cc`, `xgregorm_gen_scenario.py` | **výber technológie podľa nameranej kvality** (2026-10-20, atribúty klienta `XgQosRat`, `XgQosMargin` 0.05, `XgLatBudget` 0.1 s; CLI `--qos-rat`, `--qos-margin`, `--lat-budget`; má prednosť pred pravidlom záťaže): auto meria spoľahlivosť každej priamej technológie = podiel CAM susedov do 100 m doručených načas (≤ 100 ms, definícia spoľahlivosti 3GPP TS 22.186), kĺzavo ~2 s; straty určí z **poradového čísla** – prepínač zapíše do poľa Reserved základnej hlavičky GeoNet (EN 302 636-4-1, autori ho píšu 0 a ignorujú) 1 bajt: bit 7/6 = CAM ide cez 802.11p / NR, bity 0-5 = poradové číslo mod 64 (veľkosť správy bez zmeny; rozšírenie normy). Príjem 802.11p a NR ide cez klienta (`xgRxGn`, prečíta značku, potom `GeoNet::receiveGN` cez `XgRxProxySocket`). Raz za 1 s prejde na druhú technológiu, ak je spoľahlivejšia o ≥ 0.05 a nie je zahltená (CBR < 0.3), s pravdepodobnosťou zisk/0.4; pravidlo záťaže nepresunie auto na nameraně horšiu technológiu. Predpoklad: kanál je symetrický. Pravidlo záťaže navyše: prah PRB pre zálohu LTE má každé auto vlastný (XgLteHigh ± 0.2, rovnomerne), aby zálohu nevyplo naraz celé mesto. Výpis `QOSSW`, `QW`/`QN` (odhad) v `XGREGORM-HYB`. Bez `--qos-rat` bit po bite zhodné | `xgregorm_kod_zaloha/` |
| `xgregorm-hybrid.cc`, `xgregorm_gen_scenario.py` (moje) | **ďalší šum pri `--channel urban`** (2026-10-23): rýchle úniky 802.11p Nakagami-m (`--fading`, m = 3 do 50 m, 1.5 do 150 m, 1 ďalej; reťazené za TR 37.885, aj pre meranie CBR); šumové číslo NR UE 9 dB podľa 3GPP TR 37.885 (`--nr-noise-figure`, 5G-LENA/autori 5 dB); rušenie zo susedných buniek LTE ako +3 dB k šumovému číslu eNB (`--lte-iot`, bežná rezerva v plánovaní LTE). Pri inom kanáli bez zmeny | – (môj kód) |
| `xgregorm-stats.h`, `xgregorm-hybrid.cc`, `xgregorm_gen_scenario.py` (moje), `trafficManagerClient80211p.cc`, `trafficManagerServer80211p.cc` | **`--warmup X`** (2026-10-23, len hybrid): všetky štatistiky (sieť aj doprava zo SUMO) sa počítajú až od X s; CAM poslané skôr sa nezapočítajú, ani keď prídu neskôr. Časová os, LIVE a kontrola ETSI ostávajú za celý beh. Správanie bez zmeny (overené: výpisy CAM, LIVE, GAP pri 0 a 10 bit po bite zhodné). **Straty pri prepnutí technológie** `XGREGORM-SWLOSS`: pre každú CAM × suseda do 100 m podľa priamych ciest CAM (len 802.11p / len NR / obe), kto ju dostal (len 802.11p / len NR / oboma / nikto) a pri strate CAM jednou technológiou, či sused od toho auta za poslednú 1 s / 3 s prijal druhou (odhad strát, ktoré by druhá technológia zachránila) | `xgregorm_kod_zaloha/` (klient, server) |
| `xgregorm-hybrid.cc`, `xgregorm_gen_scenario.py` (moje) | **vzájomné rušenie 802.11p a NR-V2X v pásme 5.9 GHz** `--coex 1` (2026-11-01): v ns-3 sú to dva oddelené kanály, pri každom vysielaní sa prijímačom druhej technológie pridá cudzí signál – výkon v prekrytí pásiem (802.11p rovnomerne v 10 MHz, NR podľa PSD po RB) + únik do susedného pásma potlačený o ACLR (`--coex-aclr` 30 dB, 3GPP TS 38.101-1); útlm samostatným modelom rovnakého typu ako kanál 802.11p (bez rýchlych únikov, dvojica raz za 100 ms). 802.11p: cudzí signál v `InterferenceHelper` (SINR) + CCA podľa energie od −65 dBm (ETSI EN 302 571; 802.11p odkladá vysielanie), započíta sa do CBR; NR: do interferencie sidelinku (SINR), NR (mode 2 bez sensingu) neodkladá, do odhadu SL-CBR nad −94 dBm. 802.11p na kanáli `--coex-wifi-ch` 180 (5900 MHz, ETSI CCH) = celé v pásme NR 5870-5910 MHz (súbežný kanál). Navyše `--nr-freq`, `--nr-bw` (napr. plán pásma EÚ: NR 20 MHz pri 5915 MHz = susedný kanál). Bez zásahu do jadra ns-3 (autori: upravené `yans-wifi-channel.cc` / `multi-model-spectrum-channel.cc` v `TxTracker/channel_files`), cez trace `PhyTxPsduBegin` a `TxSigParams`. Výpis `XGREGORM-COEX`. Bez `--coex` bit po bite zhodné | – (môj kód) |
| `trafficManagerClient80211p.cc/.h`, `xgregorm-stats.h`, `xgregorm-hybrid.cc`, `xgregorm_gen_scenario.py` | **prediktívny výber technológie** (2026-11-01, atribúty klienta `XgPredRat`, `XgPredHorizon` 3 s; CLI `--pred-rat`, `--pred-horizon`, `--rem-cell` 20 m, `--rem-tau` 30 s; zapína aj pravidlo kvality): zdieľaná **mapa kvality** (`xgstats::rem`, v realite server, kam autá posielajú merania) – bunky 20 × 20 m, pre 802.11p a NR kĺzavé súčty očakávaných / načas doručených CAM susedov do 100 m nameraných autom v bunke a priemerné CBR, zabúdanie s časovou konštantou 30 s; mapa obsahuje len minulé merania. Auto raz za 1 s určí body, kde bude o 1, 2, 3 s (súčasná rýchlosť pozdĺž trasy zo SUMO = navigácia), spoľahlivosť a záťaž technológie = priemer vlastného súčasného merania a hodnôt mapy v týchto bodoch; ďalej rovnaké pravidlo ako `--qos-rat` (rozdiel ≥ 0.05, cieľ nezahltený, pravdepodobnosť zisk/0.4). Presnosť predikcie: po horizonte porovná predikciu aj „zostane ako teraz“ s tým, čo auto naozaj namerajo (`XGREGORM-PRED`), výpis `PREDSW` v `XGREGORM-HYB`. Bez `--pred-rat` bit po bite zhodné (overené 30 áut, 10 s) | `xgregorm_kod_zaloha/` |
| `xgregorm-hybrid-socket.h`, `xgregorm-hybrid.cc`, `xgregorm_gen_scenario.py` (moje), `trafficManagerClient80211p.cc` | **riadenie zahltenia** (2026-11-02): `--dcc 1` = reaktívne DCC 802.11p podľa ETSI TS 102 687 V1.2.1 (Annex A, hodnoty ako modul DCC autorov): stav podľa CBR stanice (priemer dvoch 100 ms okien; Relaxed < 0.30 / Active1 < 0.40 / Active2 < 0.50 / Active3 < 0.65 / Restrictive), vyhodnotenie raz za 100 ms, vždy len do susedného stavu; brána po každom rámci zatvorená Toff (Ton ≤ 0.5 ms: 50/100/200/250/1000 ms, ≤ 1 ms: 100/200/400/500/1000 ms, dlhší rámec úmerne, max 1 s – norma pre Ton > 1 ms tabuľku nemá, zachováva sa podiel času vysielania); správy čakajú vo fronte podľa priority (SPATEM/MAPEM, CAM, CPM a beacon GeoNet), novšia rovnakého druhu nahradí čakajúcu, po 1 s zahodená; T_GenCam_DCC a T_GenCpm ≥ Toff stavu. Zníženie výkonu z tabuľky autorov sa nepoužíva. `--nr-cr 1` = limit CR pre NR podľa ETSI TS 103 574 tab. 1 (PPPP3-5: SL-CBR ≤ 0.3 bez limitu, ≤ 0.65 0.03, ≤ 0.8 0.006, inak 0.003; CR z vlastných PSSCH za 1 s), kópia nad limitom sa zahodí. Výpis `XGREGORM-DCC` | – (môj kód; v klientovi `xgregorm_kod_zaloha/`) |
| `xgregorm-hybrid.cc`, `xgregorm-stats.h`, `xgregorm_gen_scenario.py` (moje) | **zmes výbavy áut** `--equip W:a,N:b,WN:c,NONE:d` (2026-11-02): auto len 802.11p / len NR / obe / bez V2X, LTE (telematika, eCall) má každé; bez 802.11p rádio 802.11p v spánku, bez NR bez NR zariadenia, bez V2X bez klienta (CAM neposiela, CPM ostatných ho vidia). Štatistiky: auto bez V2X nie je príjemca CAM ani ho RSU nemôže poznať; príjemca bez technológie, ktorou CAM odišla, ju nedostane (strata – nekompatibilita); výkon po technológiách len príjemcovia s ňou; SPATEM/MAPEM/CPM očakávaní príjemcovia len s danou technológiou. **Telefóny v autách** `--phone-share`, `--phone-kbps`: uplink ON/OFF cez LTE zariadenie auta, len kým je auto v mape; CAM cez LTE vtedy vlastným bearerom QCI 79 (NGBR_V2X). **`--lte-bw`** šírka pásma bunky [PRB]. Výpisy `XGREGORM-EQUIP`, `XGREGORM-PHONE`. Bez volieb bit po bite zhodné | – (môj kód) |
| `xgregorm-hybrid.cc` (môj) | **iná prevádzka v bunke LTE** `--lte-bg-ue N --lte-bg-kbps R`: N statických UE (náhodne v oblasti RSU ± 100 m), každé = skupina telefónov s uplinkom ON/OFF (exp. 0.5 s / 0.5 s, počas ON 2R, pakety 1000 B na server); ich PRB sa rátajú do vyťaženia bunky; výpis `XGREGORM-LTEBG`. Limit bunky ~320 UE platí aj pre ne | – (môj kód) |
| `trafficManagerClient80211p.cc/.h`, `xgregorm-hybrid.cc`, `xgregorm-hybrid-socket.h`, `xgregorm-stats.h`, `xgregorm_gen_scenario.py` | **voľby výberu RAT z 2026-11-02 a 11-03** (podrobne plán, časť 5a a 8; bez volieb bit po bite zhodné): `--wifi-bcast-rate`, `--wifi-rx-min` (rýchlosť a prah 802.11p podľa EN 302 663), `--clean --init-main --probe-hz --cell-dist` (čistá politika: jedna priama technológia na CAM), `--rem-period` (hlásenia mapy kvality cez LTE), `--feat-log`, `--pred-model` (dátová sada, naučený model), `--ped --ped-kbps --ped-vam-hz --ped-hotspot` (chodci, len LTE), nápady 1-6 `--dcc-offload --nb-equip --rsu-equip --rsu-route --dup-near --geo-rat --cpm-rat`, `--zones` (lokálne zhoršenie príjmu, kontrolovaný experiment), `--oracle` (nerealizovateľná referencia) | `xgregorm_kod_zaloha/` |
| `trafficManagerClient80211p.cc/.h`, `xgregorm-stats.h`, `xgregorm-hybrid.cc`, `xgregorm_gen_scenario.py` | **2026-11-03: vstupy z prijatých správ** v `--feat-log` (SPATEM po technológiách vrátane podielu z 10 Hz, vek MAPEM, počet CPM a ich objektov, čas do zmeny a farba, ak prišla SPATEM, susedia z CAM do 30 m, stojaci, približujúci sa, 100-200 m, vlastná zmena rýchlosti a státie; zjednodušenie: čas do zmeny a farba sa čítajú zo SUMO, keď auto má čerstvú SPATEM). **Učenie za jazdy `--bandit 1`** (atribút `XgBandit`): 8 situácií (pred semaforom, stojí, husto), zabúdajúci priemer vlastného doručenia a bonus za málo skúseností, skúšky len keď druhá technológia nebola nedávno zmeraná. Výpis `XGREGORM-LATP` (oneskorenie p50/p95/p99 po technológiách). Bez volieb bit po bite zhodné (referencia `EXP:2129,RX:2129`, `N:2966,BYTES:323086`) | `xgregorm_kod_zaloha/pred_2026-11-03/` |
| `trafficManagerClient80211p.cc/.h`, `xgregorm-stats.h`, `xgregorm-gbm.h` (nový), `xgregorm-hybrid.cc`, `src/automotive/CMakeLists.txt`, `xgregorm_gen_scenario.py` | **2026-11-03: naučené stromy v aute `--pred-gbm`** (predikcia nahradí aktuálne meranie v pravidle kvality, vstupy len z prijatých správ a vlastných meraní) a `--bandit-probe` (bandit s plnými skúškami). Bez volieb bit po bite zhodné | `xgregorm_kod_zaloha/pred_gbm_2026-11-03/` |
| `xgregorm-stats.h`, `trafficManagerClient80211p.cc`, `xgregorm-hybrid.cc`, `xgregorm_gen_scenario.py` | **2026-11-17: `--clock-err S`** – chyba hodín každej stanice N(0, S) (stála na stanicu, z ID a seedu vlastným generátorom), pripočíta sa k oneskoreniu meranému v pravidle kvality (čas príjmu podľa mojich hodín − čas vzniku podľa hodín odosielateľa). Bez voľby bit po bite zhodné | `xgregorm_kod_zaloha/` |
| `src/buildings/model/buildings-channel-condition-model.cc` (ns-3; `src/buildings/` je v `.gitignore`) | **2026-11-17: zrýchlenie hľadania budov** – mriežka 40 m: test LOS prechádza len budovy v bunkách, cez ktoré úsečka prechádza (konzervatívne), namiesto všetkých budov. Výsledok rovnaký (overené bit po bite, `--channel urban`) | `xgregorm_kod_zaloha/buildings-channel-condition-model.cc/.h` |
| `src/spectrum/model/three-gpp-channel-model.cc/.h`, `three-gpp-spectrum-propagation-loss-model.cc/.h` (ns-3; `src/spectrum/` je v `.gitignore`) | **2026-11-17: `--nr-cache-ttl X`** (globálna `ns3::g_xgCacheTtl`) – zabudne parametre, maticu kanála a dlhodobú zložku dvojíc nepoužitých X s (pri návrate nová realizácia); 0 = pôvodne | `xgregorm_kod_zaloha/three-gpp-*.cc/.h` |
| `xgregorm-hybrid.cc`, `xgregorm_gen_scenario.py` (moje) | **2026-11-17: veľké scenáre** `--nr-max-loss X` (`MaxLossDb` kanála NR sidelinku: príjemca s útlmom > X dB signál nedostane ani ako rušenie), `--nr-fading 0` (NR bez rýchlych únikov 3GPP, `InitializeOperationBand` bez `INIT_FADING`), `--cc-grid N` (city-center N × N križovatiek, pri 3 bit po bite rovnaká mapa). **Odstránené** (nepoužité v žiadnej sérii): `--ped` (chodci), `--nr-sensing` (nepoužiteľne pomalé, sensing ostáva vypnutý ako u autorov). Bez volieb bit po bite zhodné | – (môj kód, staré verzie v gite) |
| `trafficManagerServer80211p.cc` | **2026-11-17: `TlsRadius < 0`** (`--tls-radius -1`) = RSU neriadi žiadny semafor (pevný program SUMO), aby rádio neovplyvnilo pohyb áut pri porovnaní s OMNeT++ | `xgregorm_kod_zaloha/` |
| `xgregorm-qos-core.h` (nový, `model/Applications/`), `trafficManagerClient80211p.cc/.h`, `src/automotive/CMakeLists.txt` | **2026-11-17: jadro pravidla kvality nezávislé od simulátora** (C++17 bez ns-3): záznam CAM a uzavretie po 200 ms, chýbajúce poradové čísla, očakávané / načas, zabúdanie, rozhodnutie (`qosDecide`). Klient ns-3 ho volá (bit po bite zhodné, overené `--qos-rat`), používa ho aj `xgregorm_omnet/src/XgCamApp.cc` | `xgregorm_kod_zaloha/pred_qos_core_2026-11-17_*` |
| `src/automotive/model/GeoNet/geonet.cc` (kód autorov) | **2026-11-17: oprava úniku pamäte autorov** – pri každom prijatom SHB pakete sa do záznamu odosielateľa v tabuľke polôh pridávali hodnoty CBR (`cbr_extension.CBR_R0_Hop/R1_Hop`), mazala ich však len kontrola globálneho CBR modulu DCC autorov (`attachGlobalCBRCheck`, len pri `m_dcc != nullptr`). Bez neho zoznamy rástli s každým príjmom: 140 áut ~3 GB za 120 s, 1000 áut ~0.5 GB na sekundu simulácie. Teraz sa ukladajú len pri `m_dcc != nullptr`; výsledky bit po bite rovnaké (nikto iný ich nečíta). Treba nahlásiť autorom | `xgregorm_kod_zaloha/geonet.cc` |
| `trafficManagerClient80211p.cc/.h`, `xgregorm-hybrid.cc`, `xgregorm_gen_scenario.py` | **2026-11-17: porovnávacia metóda z literatúry `--kh 1`** (atribúty `XgKhalid`, `XgKhWin` 5 s, `XgKhD1` 125 m, `XgKhD2` 250 m, `XgKhLthr` 0.1 s, `XgKhSthr` 100 km/h; Khalid a kol., Future Internet 16(4):107, 2024): rozhodovací strom nad prijatými CAM za 5 s (priemerné oneskorenie po technológiách, 95. percentil vzdialenosti odosielateľov cez PC5, rýchlosť); prvých 5 s a bez prijatých CAM obe priame technológie. Odchýlky od článku: vetva „len Uu“ = NR (CAM cez LTE u nás dostanú len RSU), frekvencia CAM sa nemení. Výpis `KH:` v `XGREGORM-HYB`. Bez voľby bit po bite zhodné (`EXP:2129,RX:2129`, `N:2966,BYTES:323086`) | `xgregorm_kod_zaloha/pred_khalid_2026-11-17_*` |
| `xgregorm_import_scenario.py` (nový), `xgregorm_gen_scenario.py` | **2026-11-17: publikované scenáre SUMO** (`--import-dir`): import prehrá scenár do `--t0` a autá v sieti začnú v čase 0 na svojom mieste (pruh, poloha, zvyšok trasy a zastávok autobusov; najprv predné auto na úseku, `departSpeed="max"`), ID `veh<n>`, geo-referencia, RSU na križovatkách so semaforom, budovy mimo vozovky a 4 m chodníka; semafory podľa programov scenára (`--tls-radius -1`). Bologna Acosta (iTETRIS): `xgregorm_scenare/acosta/` | – (môj kód) |
| `xgregorm_omnet/` (nový projekt OMNeT++ 6.0.3 + Veins 5.2) | **2026-11-17: ten istý scenár v OMNeT++** – `XgCamApp` (CAM podľa ETSI, meranie v rovnakom formáte ako ns-3, jadro kvality), `export_scenario.py`, `xgregorm_compare.py` (oba simulátory jedným príkazom). Popis a výsledky v `xgregorm_omnet/README.md` | – |

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
python3 xgregorm_gen_scenario.py --map city-center --vehicles 280 --sim-time 20 --run hybrid --channel urban   # budovy + 3GPP mestský kanál (max 1 bunka LTE = 280 áut)
python3 xgregorm_gen_scenario.py --map city-center --vehicles 280 --run hybrid --channel urban --security 1 --spat 1 --cpm-share 1.0   # + podpis, SPaT/MAPEM, CPM všetkých áut (realistická záťaž)
python3 xgregorm_gen_scenario.py --map city-center --vehicles 280 --run hybrid --channel urban --security 1 --spat 1 --cpm-share 1.0 --load-rat 1 --lte-bg-ue 5   # výber technológie podľa záťaže + iní používatelia LTE
python3 xgregorm_gen_scenario.py --map city-center --cc-grid 5 --vehicles 1000 --enb 9 --enb-isd 400 --nr-max-loss 120 --nr-cache-ttl 1 --nr-fading 0 --run hybrid ...   # 1000 áut: ~7-8 min na 1 s simulácie, ~3 GB (s --nr-fading 1 ~10-15 min, ~11 GB)
python3 xgregorm_omnet/xgregorm_compare.py --vehicles 140 --sim-time 60 --warmup 20   # ten istý scenár v ns-3 aj OMNeT++/Veins (len 802.11p), výsledky vedľa seba
python3 xgregorm_import_scenario.py --src /usr/local/share/sumo/tools/sumolib/scenario/scenarios/RealWorld/acosta --net acosta_buslanes.net.xml --routes acosta.rou.xml --add acosta_busses.add.xml --other acosta_vtypes.add.xml,acosta_bus_stops.add.xml,acosta_tls.add.xml --t0 1800 --window 50 --out xgregorm_scenare/acosta   # publikovaný scenár Bologna Acosta
python3 xgregorm_gen_scenario.py --import-dir xgregorm_scenare/acosta --name acosta --sim-time 60 --warmup 20 --run hybrid --channel urban --enb 9 --enb-isd 500 ...   # hybrid na ňom (pozri xgregorm_logs/acosta.json)
python3 xgregorm_gen_scenario.py ... --clean 1 --init-main 2 --kh 1                   # metóda z literatúry Khalid a kol. 2024
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
- Pred commitom/pushom sa opýtaj; **výnimka od používateľa z 2026-11-03: počas tejto fronty sa po etapách commituje a pushuje do `dp/main` bez pýtania**, rozbehnuté série a dočasné priečinky sa nepridávajú. Zmeny v projekte zapisuj do tohto súboru (Denník nižšie).
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
- **2026-10-20** – **Realizmus: budovy + 3GPP mestský kanál, podpis správ, ďalšia prevádzka (SPaT/MAPEM, CPM)**
  (**necommitnuté – na želanie len zapísané**; viď posledné 3 riadky v Moje zmeny). Cieľ: dostať sa do stavu, keď má zmysel
  prepnúť z 802.11p, aj keď je dostupné (zahltenie / strata), nie len pri výpadku.
  1. **`--channel urban`**: budovy z blokov mapy (city-center 357 kvádrov 15-30 m, min. 1.6 m od osi pruhu), 802.11p a NR
     TR 37.885 V2V Urban, LTE TR 38.901 UMa. Zistené: kanál autorov má `UpdatePeriod` 0 = stav LOS/NLOS dvojice sa
     za celý beh nezmení (u nás 100 ms). Dosah (30 áut, 30 s, `--policy all`, doručenie po 50 m):

     | spoj | 0-50 | 50-100 | 100-150 | 150-200 | 200-250 | 250-300 | 300-350 | 350-400 | 400-450 |
     |---|---|---|---|---|---|---|---|---|---|
     | auto-auto 802.11p, autori | 1.00 | 1.00 | 0.75 | 0 | | | | | |
     | auto-auto 802.11p, urban | 0.99 | 0.75 | 0.43 | 0.11 | 0.11 | 0.08 | 0.03 | 0 | |
     | auto-auto NR, urban (autori ~0.9 do 450 m) | 0.93 | 0.93 | 0.86 | 0.60 | 0.48 | 0.28 | 0.17 | 0.04 | |
     | auto-RSU 802.11p, autori | 1.00 | 1.00 | 0.77 | 0 | | | | | |
     | auto-RSU 802.11p, urban (RSU 5 m) | 1.00 | 0.99 | 0.97 | 0.48 | 0.36 | 0.23 | 0.20 | 0.12 | 0.05 |
     | auto-RSU NR, urban | 0.97 | 0.92 | 0.93 | 0.80 | 0.62 | 0.42 | 0.27 | 0.20 | 0.09 |

     Za rohom (NLOS) 802.11p padá už pod 100 m – zodpovedá meraniam v meste (literatúra: stovky m len pri LOS).
  2. **Podpis (ETSI TS 103 097):** kryptografia autorov (`--security 2`) zväčší CAM priemerne 118 -> 250 B (+93 B digest,
     +242 B certifikát 1x/s), beh 3x pomalší a 0-12 falošných „invalid signature“ na uzol -> `--security 1` = len veľkosť
     (241 B, rýchlosť ako bez podpisu, PRR 1.0 pri 30 autách).
  3. **Pozadie:** 30 áut, 10 s, urban + podpis + SPaT + CPM 50 %: CBR 802.11p ~1 % -> 5.7 %, SPATEM 802.11p 0.995 / NR 0.83, CPM 0.92.
  4. **Limit 1 bunky LTE = 280 áut** (300 áut: `too many UEs (321) for SRS periodicity 320` – realistický kanál = opakované
     RRC pripojenia). Pamäť ~1.2 GB na beh -> najviac 3 paralelne (4 behy vyčerpali 7.8 GB). Rozhodnutie: ďalej 1 bunka, 280 áut.
  5. **280 áut, city-center, 1 bunka, 20 s, adaptívne, urban** (1 beh; c1 s kanálom autorov zastavený pre pamäť):

     | | urban | + podpis, SPaT/MAPEM, CPM 30 % áut | + CPM 100 % áut |
     |---|---|---|---|
     | CBR 802.11p priemer / p95 / max | 0.031 / 0.05 / 0.09 | 0.129 / 0.22 / 0.38 | **0.250 / 0.42 / 0.84** |
     | vzorky CBR ≥ 0.3 (ETSI DCC active) | 0 % | 0.3 % | **37 %** |
     | NR SL-CBR (odhad) priemer / max | 0.014 / 0.025 | 0.062 / 0.11 | 0.105 / 0.18 |
     | LTE PRB UL priemer / max (vzorky ≥ 0.3) | 0.15 / 0.65 (17 %) | 0.30 / 0.79 (45 %) | **0.48 / 0.79 (92 %)** |
     | CAM len 802.11p / so zálohou LTE | 75 / 22 % | 58 / 37 % | 41 / **54 %** |
     | CAM cez LTE za s | 192 | 321 | 466 |
     | PRR kópie 802.11p / NR (auto-auto < 100 m) | 0.83 / 0.996 | 0.80 / 0.987 | **0.77 / 0.978** |
     | skutočné PRR auto-auto (prvá kópia) | 0.851 | 0.867 | 0.892 |
     | uplink do RSU < 100 m | 0.993 | 0.979 | 0.965 |
     | vek informácie RSU (< 100 m) | 0.26 s | 0.27 s | 0.28 s |
     | oneskorenie 802.11p / NR auto-auto / LTE -> RSU | 0.27 / 8.3 / 70 ms | 0.49 / 15.6 / 67 ms | 0.54 / 18.6 / 73 ms |
     | SPATEM 802.11p / NR | – | 0.89 / 0.91 | 0.90 / 0.91 |
     | MAPEM 802.11p / NR | – | 0.94 / 0.91 | 0.88 / 0.91 |
     | CPM 802.11p / NR (počet, objekty na CPM) | – | 0.77 / 0.99 (14357, 4.6) | **0.70 / 0.97** (47184, 4.6, priem. 372 B) |
     | čas behu | 24 min | 35 min | 44 min |

     Hlavná technológia (LIVE) vo všetkých troch 94-97 % áut 802.11p. Doprava rovnaká (25.5 km/h, max státie 18.8 s).
     **Závery:**
     - Mestský kanál sám zníži PRR auto-auto do 100 m na ~0.85 (NLOS za rohom), 802.11p 0.83 vs. NR 0.996 – prečo je NR
       zblízka v NLOS o toľko lepšie, neoverené (citlivosť / kódovanie NR, HARQ).
     - S CPM všetkých áut je 802.11p v pásme ETSI DCC (priemer 0.25, 37 % času ≥ 0.3) a PRR klesá, NR kanál má rezervu
       (≤ 0.18, odhad = dolná hranica). Teraz má zmysel odľahčiť 802.11p, hoci je dostupné.
     - Súčasné adaptívne pravidlo reaguje len cez potvrdenia RSU -> pridáva zálohu cez **LTE** (22 -> 54 % CAM), čím zahltí
       jedinú bunku (PRB 0.48, 92 % času ≥ 0.3, oneskorenie 70 ms, p99 > 1 s) – teda presúva záťaž na najdrahší zdroj,
       kým NR má rezervu a lepšie PRR. Základ pre pravidlo podľa záťaže (ďalší krok): pri CBR 802.11p ≥ 0.3 presunúť časť áut na NR.
     - PRR 802.11p podľa koša CBR v čase vyslania nie je monotónne (vyšší CBR = hustejšie miesta pri križovatkách, bližší
       prijímatelia) – hodnoty po košoch nebrať ako príčinný vzťah.
- **2026-10-20** – **Výber technológie podľa záťaže (`--load-rat`) a iná prevádzka v bunke LTE (`--lte-bg-ue`)** (viď posledné
  2 riadky v Moje zmeny). Overenie: bez `--load-rat` a bez `--lte-bg-ue` bit po bite zhodné s behom pred zmenou (30 áut, 10 s).
  Test 280 áut, city-center, 1 bunka, 20 s, urban + podpis + SPaT/MAPEM + CPM 100 %, 1 beh na prípad (A = c4 vyššie);
  pozadie LTE 5 UE × 500 kbit/s (spolu 2.4 Mbit/s uplink):

  | | A: pôvodné | B: + podľa záťaže | C: + pozadie LTE | D: podľa záťaže + pozadie |
  |---|---|---|---|---|
  | CBR 802.11p priemer / vzorky ≥ 0.3 | 0.250 / 37 % | **0.230 / 24 %** | 0.250 / 37 % | 0.230 / 24 % |
  | NR SL-CBR (odhad) priemer / max | 0.105 / 0.18 | 0.149 / 0.23 | 0.114 / 0.20 | 0.147 / 0.21 |
  | LTE PRB priemer / vzorky ≥ 0.6 | 0.48 / 19 % | 0.47 / 17 % | **0.62 / 63 %** | 0.63 / 67 % |
  | autá s hlavnou technológiou NR (10-19 s) | 3-4 % | **15-18 %** | 6-9 % | 14-16 % |
  | autá so zálohou LTE (13-19 s) | 49-52 % | 46-48 % | 46-49 % | **0 %** |
  | CAM cez LTE za s | 466 | 445 | 458 | **202** |
  | presuny hlavnej technológie (LOADSW) | – | 115 | – | 126 |
  | PRR kópie 802.11p / NR (auto-auto < 100 m) | 0.771 / 0.978 | 0.766 / 0.967 | 0.762 / 0.977 | 0.764 / 0.969 |
  | skutočné PRR auto-auto | 0.892 | 0.892 | 0.890 | 0.890 |
  | uplink do RSU < 100 m | 0.965 | 0.969 | 0.971 | **0.956** |
  | oneskorenie LTE -> RSU | 72.6 ms | **58.4 ms** | 79.8 ms | 73.6 ms |
  | vek informácie RSU (< 100 m), RSU o aute nevie | 0.28 s, – | 0.26 s | 0.275 s | 0.27 s, 1.57 % |
  | SPATEM 802.11p / NR | 0.904 / 0.906 | 0.891 / 0.900 | 0.931 / 0.907 | 0.901 / 0.905 |
  | CPM 802.11p / NR (počet CPM cez NR) | 0.70 / 0.97 (3028) | 0.71 / 0.96 (6725) | 0.70 / 0.98 (4022) | 0.70 / 0.96 (6589) |
  | čas behu | 44 min | 51 min | 44 min | 45 min |

  Doprava rovnaká (25.2-25.8 km/h, max státie 17.8-18.8 s), 0 porušení ETSI, 0 zahodených NR paketov.
  **Závery:**
  - Pravidlo funguje mechanicky: pri CBR ≥ 0.3 presunie ~15 % áut na NR, čas 802.11p v pásme DCC active klesne 37 -> 24 %,
    NR má stále rezervu (≤ 0.23). Pri plnej bunke (D) auto LTE ako zálohu nepoužije (od ~12 s 0 % áut, CAM cez LTE −57 %).
  - **Koncové metriky sa nezlepšili:** PRR 802.11p ostáva ~0.77 (aj pri CBR < 0.2 len ~0.73-0.76) -> straty 802.11p sú
    hlavne z mestského kanála (NLOS), nie zo zahltenia; mierne zníženie CBR ich neodstráni. Rozdiely ±1 p. b. sú v rozptyle
    jedného behu (seedy nerobené).
  - V D je uplink o 1.5 p. b. horší než v C (bez zálohy LTE nedoručené CAM chýbajú). Brána LTE je binárna – všetky autá ju
    vypnú naraz (stádové správanie); PRB ostane ~0.65-0.74, lebo uvoľnené PRB zaberie pozadie (bunka je ním takmer plná).
  - Prechod NR -> LTE sa neuplatnil (NR nebolo zahltené). Na jeho overenie treba scenár, kde sa zahltí aj NR.
- **2026-10-20** – **Výber technológie podľa nameranej kvality (`--qos-rat`)** (viď Moje zmeny). Opravené pri teste: presmerovaním
  príjmu cez klienta zanikla jediná referencia na GeoNet (držal ju callback socketu) -> na konci behu zničený s bežiacimi
  časovačmi LocT (`Event is still running while destroying`); callback teraz drží `Ptr<GeoNet>` ako predtým. Odhad kvality
  v autách vs. skutočnosť (30 áut, 20 s, urban + podpis + SPaT + CPM 50 %): NR 0.987 vs. 0.989, 802.11p 0.930 vs. 0.878
  (optimistický – sused, ktorého auto nepočuje žiadnou technológiou, v odhade chýba); NR sa meria až od vzniku bearera (2 s).
  Bez `--qos-rat` bit po bite zhodné. 280 áut (rovnaký scenár ako A-D, 1 beh, 54 min, pamäť 3.3 GB):

  | | A: pôvodné | B: podľa záťaže | E: podľa kvality |
  |---|---|---|---|
  | CBR 802.11p priemer / vzorky ≥ 0.3 | 0.250 / 37 % | 0.230 / 24 % | 0.233 / 30 % |
  | NR SL-CBR (odhad) priemer / max | 0.105 / 0.18 | 0.149 / 0.23 | 0.163 / 0.25 |
  | LTE PRB priemer / vzorky ≥ 0.6 | 0.48 / 19 % | 0.47 / 17 % | 0.56 / 43 % |
  | CAM len cez NR | 4.1 % | 6.9 % | **15.2 %** |
  | presuny hlavnej technológie | – | 115 (záťaž) | 270 (kvalita) |
  | odhad kvality v autách 802.11p / NR (načas) | – | – | 0.789 / 0.938 |
  | PRR kópie 802.11p / NR | 0.771 / 0.978 | 0.766 / 0.967 | 0.759 / 0.969 |
  | **skutočné PRR auto-auto** | 0.892 | 0.892 | **0.899** |
  | uplink do RSU < 100 m | 0.965 | 0.969 | 0.971 |
  | oneskorenie LTE / NR auto-auto | 72.6 / 18.6 ms | 58.4 / 18.9 ms | 68.6 / 18.2 ms |
  | CAM cez LTE za s | 466 | 445 | 473 |
  | CPM 802.11p / NR | 0.70 / 0.97 | 0.71 / 0.96 | 0.71 / 0.97 |

  Záver: pravidlo kvality presunie na NR najviac áut a skutočné PRR auto-auto je najvyššie (+0.7 p. b.), ale jeden beh –
  rozdiel je na hranici rozptylu. LTE pravidlo kvality nerieši (zálohu pridáva adaptívne pravidlo podľa potvrdení RSU), PRB
  vzrástlo. Beh F (kvalita + záťaž + 5 UE pozadia) systém zastavil pre nedostatok pamäte v 18. s (2 behy × 3.3 GB) – do
  18 s: hlavná NR 21-24 % áut, záloha LTE od 13 s 0 %, PRB bunky 0.96 (pozadie zaberie uvoľnené PRB). **Pri 280 autách
  s pravidlom kvality spúšťať len 1 beh naraz.**
- **2026-10-23** – **Kontrola dopravy s grafikou: obrátené fázy semaforov (opravené v generátore).** trafficManager
  (802.11p aj LTE, kód autorov) má natvrdo fázu 0 = zelená pre 1. a 3. príjazd v poradí riadených pruhov („sever-juh“),
  2 = pre 2. a 4. netconvert však dáva fázu 0 hlavnej ceste, takže na časti križovatiek bola opačne (city-center 5 z 9:
  C01, C02, C10, C11, C20; `xgregorm_mesto` 2 z 5: C01, C21). Radič potom dával smeru zelenú úmernú záťaži **druhého**
  smeru a pri zahltení (> 70 %) ju hneď ukončil -> rady na príjazdoch sa nikdy nevyprázdnili. V GUI: na C11 desiatky áut
  na červenej, zelená prázdnemu smeru. Oprava v `xgregorm_gen_scenario.py`: fázy takej križovatky sa v `net.xml` posunú
  o 2 (pevný program SUMO ostáva, len začne druhým smerom); kód autorov nezmenený. Test (city-center, 280 áut, urban,
  len 802.11p, 150 s, 1 beh; čisté SUMO s pevnými semaformi pre porovnanie):

  | | 0-30 s | 30-60 s | 60-90 s | 90-120 s | 120-150 s | stojace autá na príjazde C11 N/E/S/W |
  |---|---|---|---|---|---|---|
  | čisté SUMO, pevné 30 s | 22 km/h, stojí 33 % | 27 / 22 % | 28 / 22 % | 30 / 22 % | 28 / 25 % | 4.4 / 0.5 / 2.7 / 0.8 |
  | ns-3 pred opravou | 25 / 24 % | 16 / 56 % | 14 / 64 % | 12 / 71 % | **9.5 / 77 %** | **40 / 0 / 18 / 0** |
  | ns-3 po oprave | 27 / 19 % | 30 / 18 % | 32 / 15 % | 32 / 15 % | **32 / 17 %** | 0.7 / 1.3 / 0.3 / 1.6 |

  Skutočné PRR auto-auto 0.898 -> **0.851**, uplink 0.984 -> 0.977 (zápchy = autá natlačené blízko seba, umelo lepšie
  doručenie). **Dôsledky:** behy A-E (20 s) boli zasiahnuté len mierne (zápcha vzniká po ~30 s; 0-30 s 25 vs. 27 km/h), ale
  treba ich zopakovať; všetky staršie výsledky trafficManager na `xgregorm_mesto` (lokálne riadenie, koordinácia,
  algoritmus 3, hybrid) mali 2 z 5 križovatiek obrátené. Ďalej zistené: autá štartujú všetky v čase 0 stojace na
  náhodnom mieste, prvých ~30 s je rozbeh (aj v čistom SUMO 33 % stojí) – 20 s behy merajú len tento rozbeh.
- **2026-10-23** – **Ďalší šum v mestskom kanáli** (viď Moje zmeny): 802.11p nemalo rýchle úniky (NR ich má v spektrálnom
  modeli 3GPP), NR UE malo šumové číslo 5 dB namiesto 9 dB (TR 37.885), LTE bunka nemala rušenie od susedov. Overené
  rušenie zo susedov 5 dB: LTE už pri 30 autách 38 -> 104 ms (p95 434 ms) -> zvolené 3 dB (56 ms). Test 30 áut, 30 s,
  urban, `--policy all`, `--range-stats 1`, 1 beh (bez / s novým šumom; auto-auto = prvá kópia ktorejkoľvek technológie):

  | auto-auto | 0-50 m | 50-100 | 100-150 | 150-200 | 200-250 | 250-300 | 300-350 |
  |---|---|---|---|---|---|---|---|
  | bez | 1.00 | 0.98 | 0.90 | 0.59 | 0.47 | 0.26 | 0.14 |
  | s novým šumom | 1.00 | 0.97 | 0.78 | 0.37 | 0.25 | 0.09 | 0.05 |

  Do 100 m: 802.11p 0.896 -> 0.881, NR 0.990 -> 0.986, skutočné PRR 0.991 -> 0.987; LTE -> RSU 38 -> 56 ms (rušenie 3 dB).
  Dosah NR klesol hlavne zo šumového čísla (+4 dB). Stará konfigurácia: `--fading 0 --nr-noise-figure 5 --lte-iot 0`.
- **2026-10-23** – **Zopakované A a E s opravenými fázami a novým šumom** (280 áut, city-center, 1 bunka, 20 s, urban +
  podpis + SPaT/MAPEM + CPM 100 %, 1 beh na prípad; A = adaptívne, E = + `--qos-rat 1`; staré = záznamy 2026-10-20):

  | | A starý | A nový | E starý | E nový |
  |---|---|---|---|---|
  | CBR 802.11p priemer / vzorky ≥ 0.3 | 0.250 / 37 % | 0.246 / 34 % | 0.233 / 30 % | **0.217 / 23 %** |
  | NR SL-CBR (odhad) priemer / max | 0.105 / 0.18 | 0.116 / 0.20 | 0.163 / 0.25 | 0.176 / 0.26 |
  | LTE PRB priemer / vzorky ≥ 0.6 | 0.48 / 19 % | 0.52 / 20 % | 0.56 / 43 % | 0.51 / 18 % |
  | CAM len cez NR | 4.1 % | 4.1 % | 15.2 % | **16.4 %** |
  | autá s hlavnou technológiou NR (8-19 s) | 3-4 % | 3-8 % | – | 17-24 % |
  | presuny podľa kvality | – | – | 270 | 274 |
  | odhad kvality v autách 802.11p / NR (načas) | – | – | 0.789 / 0.938 | 0.759 / 0.914 |
  | PRR kópie 802.11p / NR (súčet košov záťaže) | 0.771 / 0.978 | 0.745 / 0.957 | 0.759 / 0.969 | 0.738 / 0.951 |
  | **skutočné PRR auto-auto** | 0.892 | 0.886 | 0.899 | **0.889** |
  | uplink do RSU < 100 m | 0.965 | 0.969 | 0.971 | 0.973 |
  | oneskorenie LTE -> RSU / NR auto-auto | 72.6 / 18.6 ms | 104.3 / 19.6 ms | 68.6 / 18.2 ms | 91.4 / 20.5 ms |
  | CAM cez LTE za s | 466 | 534 | 473 | 510 |
  | SPATEM 802.11p / NR | 0.904 / 0.906 | 0.913 / 0.907 | – | 0.929 / 0.899 |
  | MAPEM 802.11p / NR | 0.88 / 0.91 | 0.883 / 0.901 | – | 0.880 / 0.891 |
  | rýchlosť, státie, max státie | 25.5 km/h | 26.7 km/h, 12.2 %, 16.3 s | – | 26.7 km/h, 12.1 %, 16.3 s |
  | čas behu | 44 min | 39 min | 54 min | 50 min |

  0 porušení ETSI, 0 zahodených NR paketov, 0 fantómov. **Závery:** nový šum zhoršil NR (kópie 0.978 -> 0.957) viac než
  802.11p (0.771 -> 0.745) a LTE (+3 dB rušenie) je pri plnej bunke pomalšie (73 -> 104 ms). Pravidlo kvality presúva
  na NR rovnako ako predtým (~17-24 % áut, 274 presunov) a odľahčí 802.11p (CBR ≥ 0.3: 34 -> 23 % času, LTE PRB 0.52 -> 0.51,
  LTE 104 -> 91 ms), ale skutočné PRR auto-auto sa zlepší len o +0.3 p. b. (0.886 -> 0.889; predtým +0.7) – v rozptyle
  jedného behu. Rozdiel NR a 802.11p sa zmenšil (NR 0.95 vs. 802.11p 0.74 na kópiu, ale skutočné PRR = prvá kópia
  ktorejkoľvek technológie, a autá na NR prestanú posielať 802.11p tým, ktorí NR nepočujú). 20 s = rozbeh, treba dlhšie
  behy a viac seedov.
- **2026-10-23** – **`--warmup` a meranie strát pri prepnutí (`XGREGORM-SWLOSS`)** (viď Moje zmeny). Overenie: 30 áut,
  20 s, `--warmup 0` vs. `10` – výpisy CAM, LIVE a GAP bit po bite zhodné (správanie bez zmeny). Ostrý test: 280 áut,
  city-center, 1 bunka, **60 s, štatistiky od 30 s** (po rozbehu), urban + podpis + SPaT/MAPEM + CPM 100 %, 1 beh na prípad
  (A = adaptívne, E = + `--qos-rat 1`; beh A 1 h 58 min, E 2 h 56 min, pamäť ~3.2 GB):

  | 30-60 s | A | E |
  |---|---|---|
  | **skutočné PRR auto-auto** | 0.903 | **0.918** |
  | uplink do RSU < 100 m | 0.962 | **0.973** |
  | CBR 802.11p priemer / vzorky ≥ 0.3 | 0.273 / 44 % | **0.229 / 32 %** |
  | NR SL-CBR (odhad) priemer / max | 0.098 / 0.13 | 0.226 / 0.29 |
  | LTE PRB priemer / vzorky ≥ 0.6 | 0.48 / 11 % | 0.44 / 7 % |
  | CAM len cez NR / len 802.11p | 2.3 / 46.8 % | **34.5** / 17.7 % |
  | CAM cez LTE za s | 222 | 210 |
  | presuny podľa kvality (od 30 s) | – | 16 (autá sa presunú počas rozbehu a ostanú) |
  | PRR kópie 802.11p / NR (súčet košov) | 0.75 / 0.96 | 0.75 / 0.95 |
  | oneskorenie LTE -> RSU / NR auto-auto | 69.0 / 19.9 ms | 70.0 / 20.8 ms |
  | SPATEM 802.11p / NR | 0.896 / 0.985 | 0.912 / 0.972 |
  | MAPEM 802.11p / NR | 0.838 / 0.970 | 0.859 / 0.968 |
  | CPM 802.11p / NR | 0.678 / 0.962 | 0.679 / 0.954 |
  | vek informácie RSU (< 100 m) | 0.31 s | 0.32 s |
  | rýchlosť, státie, max státie (30-60 s) | 29.8 km/h, 15.4 %, 22.6 s | 29.8 km/h, 15.5 %, 22.5 s |

  Straty podľa priamej cesty CAM (CAM × sused do 100 m; dostal len 802.11p / len NR / oboma / nikto):

  | | A | E |
  |---|---|---|
  | len 802.11p | 80.7 / – / – / **19.3 %** (227 106) | 80.1 / – / – / **19.9 %** (154 282) |
  | len NR | – / 97.2 / – / **2.8 %** (18 067) | – / 95.8 / – / **4.2 %** (115 247) |
  | 802.11p + NR | 1.2 / 27.6 / 68.8 / 2.4 % | 1.8 / 24.9 / 70.3 / 3.0 % |
  | strata len 802.11p: sused prijal toto auto cez NR za 1 s / 3 s | 37 / 49 % | 58 / 74 % |
  | strata len NR: sused prijal toto auto cez 802.11p za 1 s / 3 s | 58 / 82 % (514) | 15 / 23 % (4 837) |

  0 porušení ETSI, 2-6 zahodených NR paketov, 0 fantómov. **Závery:**
  - Po rozbehu je prínos pravidla kvality väčší ako v 20 s behoch: skutočné PRR +1.5 p. b. (0.903 -> 0.918), uplink
    +1.1 p. b., 802.11p v pásme DCC active 44 -> 32 % času; doprava rovnaká. Stále 1 beh, seedy nerobené.
  - **Hypotéza „prechodom na NR sa stratia susedia, ktorí počujú len 802.11p“ sa nepotvrdila:** CAM len cez NR stratí
    4.2 % (802.11p samo 19.9 %) a z nich len 15 % susedov počulo auto cez 802.11p. Pri CAM poslanej oboma by NR samo
    stratilo 1.2-1.8 %, 802.11p samo 25-28 % príjmov.
  - Rezerva: CAM poslané len cez 802.11p stále strácajú ~20 % a v E 58 % z týchto susedov auto počuje cez NR -> pravidlo
    presúva málo (prah zisku 0.05, pravdepodobnosť zisk/0.4). NR je pri 34.5 % CAM na 0.23 (max 0.29), blízko prahu 0.3.
- **2026-11-02** – **Vzájomné rušenie 802.11p a NR (`--coex`) a prediktívny výber (`--pred-rat`)** (viď Moje zmeny). Overenie:
  bez volieb bit po bite zhodné (30 áut, 10 s); 30 áut, 20 s, `--policy all`: koexistencia CBR 802.11p 0.073 -> 0.153, SL-CBR
  0.062 -> 0.108, skutočné PRR 0.974 -> 0.960, čas behu +4 %. Ostrý test (rovnaký scenár ako 2026-10-23: 280 áut, city-center,
  60 s, štatistiky od 30 s, urban + podpis + SPaT/MAPEM + CPM 100 %; 1 beh na prípad, 2.4-3.1 h na beh; bez koexistencie A a E
  z 2026-10-23):

  | 30-60 s | A | E (kvalita) | P (predikcia) | A + coex | E + coex | P + coex |
  |---|---|---|---|---|---|---|
  | **skutočné PRR auto-auto** | 0.903 | **0.918** | 0.917 | 0.890 | 0.909 | **0.913** |
  | uplink do RSU < 100 m | 0.962 | 0.973 | **0.976** | 0.955 | 0.969 | **0.971** |
  | CBR 802.11p priemer / ≥ 0.3 | 0.273 / 44 % | 0.229 / 32 % | 0.230 / 32 % | 0.328 / 57 % | 0.331 / 58 % | 0.331 / 59 % |
  | NR SL-CBR (odhad) priemer / ≥ 0.3 | 0.098 / 0 % | 0.226 / 0 % | 0.227 / 0 % | 0.332 / 59 % | 0.394 / 76 % | 0.408 / 82 % |
  | LTE PRB priemer | 0.48 | 0.44 | 0.44 | 0.50 | 0.45 | 0.48 |
  | CAM len cez NR / len 802.11p | 2.3 / 46.8 % | 34.5 / 17.7 % | 34.3 / 18.7 % | 1.9 / 40.6 % | 23.7 / 17.4 % | 27.9 / 13.4 % |
  | presuny (od 30 s) | – | 16 | 14 | – | 48 | 21 |
  | PRR CAM len 802.11p / len NR | 0.807 / 0.972 | 0.801 / 0.958 | 0.805 / 0.958 | 0.762 / 0.956 | 0.768 / 0.951 | 0.754 / 0.949 |
  | oneskorenie LTE -> RSU / NR auto-auto | 69 / 20 ms | 70 / 21 ms | 82 / 20 ms | 81 / 22 ms | 69 / 23 ms | 88 / 22 ms |
  | SPATEM 802.11p / NR | 0.896 / 0.985 | 0.912 / 0.972 | 0.905 / 0.982 | 0.851 / 0.985 | 0.857 / 0.981 | 0.867 / 0.977 |
  | CPM 802.11p / NR | 0.678 / 0.962 | 0.679 / 0.954 | 0.680 / 0.953 | 0.621 / 0.943 | 0.603 / 0.942 | 0.589 / 0.941 |
  | doprava (rýchlosť, státie) | 29.8 km/h, 15.4 % | 29.8, 15.5 % | 29.9, 15.2 % | 29.7, 15.4 % | 29.8, 15.5 % | 29.8, 15.6 % |

  Koexistencia (802.11p 5900 MHz celé v pásme NR 5870-5910 MHz): 802.11p cudzie signály od NR v priemere −95 dBm, CCA obsadené
  (802.11p odloží vysielanie) 6.5-9.9 mil. krát za beh; obe merania záťaže teraz vidia aj druhú technológiu (spoločné pásmo).
  Presnosť predikcie spoľahlivosti na 3 s (stredná absolútna chyba, predikcia vs. „zostane ako teraz“): bez koexistencie 802.11p
  0.070 vs. 0.081, NR 0.039 vs. 0.047; s koexistenciou 0.076 vs. 0.087, 0.046 vs. 0.056 (o 13-18 % presnejšia; mapa 600 buniek,
  99.8 % predikcií s dátami mapy). 0 porušení ETSI, 0 fantómov. **Závery:**
  - Súbežný kanál zhorší všetko: PRR A 0.903 -> 0.890, CPM 802.11p 0.68 -> 0.62, SPATEM 802.11p 0.90 -> 0.85.
  - S koexistenciou je poradie P > E > A (0.913 / 0.909 / 0.890; uplink 0.971 / 0.969 / 0.955), bez nej P ≈ E (0.917 / 0.918).
    Predikcia teda pomáha hlavne, keď sa kvalita mení viac (rušenie). Rozdiel P - E je 0.4 p. b. – 1 beh, v rozptyle.
  - Presun na NR pri koexistencii odľahčí menej: obe technológie zdieľajú pásmo (NR SL-CBR 0.33-0.41, 59-82 % času ≥ 0.3).
- **2026-11-02** – **Riadenie zahltenia, zmes výbavy, telefóny v autách, šírka pásma LTE** (viď Moje zmeny). Bez volieb bit po bite
  zhodné. Krátke testy (40 áut, 20 s, urban + podpis + SPaT + CPM 100 % + coex + predikcia):
  zmes W 0.3 / N 0.2 / WN 0.3 / NONE 0.2: skutočné PRR 0.62 (všetky WN 0.96) – nekompatibilita technológií; telefóny 50 % áut
  × 50 kbit/s: PRB LTE (10 MHz) 0.25; DCC v stave Relaxed: 64 % správ 802.11p čaká na bránu (priemerne 51 ms), auto s CAM
  + CPM 10 Hz je pri limite 20 správ/s; s DCC + pravidlom kvality PRR 0.924 -> 0.965 (802.11p správy meškajú, autá prejdú na NR),
  ale SPATEM cez 802.11p 0.95 -> 0.57 (RSU: SPATEM 10 Hz ~1 ms + MAPEM ~3.4 ms vyčerpajú 1 % času vysielania stavu Relaxed).
- **2026-11-02** – **Séria s plným realizmom (priebežne, dopĺňa sa).** Všetky behy: city-center, 60 s, štatistiky od 30 s, urban +
  podpis + SPaT/MAPEM + CPM 100 % + `--coex 1 --dcc 1 --nr-cr 1 --phone-share 0.5 --phone-kbps 20 --lte-bw 50`, 1 beh na prípad.
  Skript a úplné logy: `xgregorm_logs/` (`ser.sh` po prerušení preskočí dokončené behy, `mem.log` = pamäť). Prvý pokus
  sa prerušil reštartom počítača/WSL (behy stáli od 03:35, logy v `/tmp` sa stratili; príčina neznáma), spustené znova.
  **Scenár L: 140 áut, všetky s oboma technológiami** (3 behy naraz, 68-81 min, spolu najviac 6.2 GB):

  | 30-60 s | A | E (kvalita) | P (predikcia) |
  |---|---|---|---|
  | **skutočné PRR auto-auto** | 0.946 | **0.967** | **0.967** |
  | uplink do RSU < 100 m | 0.981 | 0.987 | 0.986 |
  | CBR 802.11p priemer / ≥ 0.3 | 0.163 / 4.6 % | 0.175 / 7.0 % | 0.176 / 7.2 % |
  | NR SL-CBR (odhad) priemer / ≥ 0.3 | 0.159 / 0 % | 0.198 / 0 % | 0.199 / 0 % |
  | LTE PRB (10 MHz) priemer / ≥ 0.6 | 0.51 / 15 % | 0.49 / 14 % | 0.51 / 7 % |
  | CAM len 802.11p / len NR / všetky tri | 29 / 4 / 61 % | 0.7 / 35 / 60 % | 0.8 / 35 / 59 % |
  | oneskorenie auto-auto 802.11p / NR | 38.6 / 16.8 ms | 24.4 / 17.6 ms | 24.7 / 17.3 ms |
  | oneskorenie LTE -> RSU | 88.8 ms | 69.1 ms | 72.9 ms |
  | SPATEM 802.11p / NR | 0.454 / 0.981 | 0.393 / 0.978 | 0.393 / 0.978 |
  | MAPEM 802.11p / NR | 0.837 / 0.970 | 0.787 / 0.968 | 0.786 / 0.971 |
  | CPM 802.11p / NR | 0.457 / 0.953 | 0.446 / 0.953 | 0.442 / 0.953 |
  | DCC: 802.11p hneď / vo fronte / nahradené; stav Relaxed | 9 / 67 / 25 %; 97 % | 7 / 67 / 26 %; 95 % | 8 / 67 / 26 %; 96 % |
  | rýchlosť, státie | 34.2 km/h, 12.7 % | 34.1, 12.8 % | 33.9, 13.3 % |

  Limit CR nezasiahol (0 zahodených, CR 0.003-0.004, SL-CBR takmer stále < 0.3); telefóny 74 áut, 1.6 Mbit/s; 0 porušení ETSI.
  Presnosť predikcie na 3 s: 802.11p 0.081 vs. 0.092, NR 0.036 vs. 0.042 („zostane ako teraz“). **Závery (zatiaľ):**
  - Výber podľa kvality zlepší PRR o 2.1 p. b., predikcia nad to nič nepridá (P = E). Prakticky všetky autá skončia na NR ako hlavnej
    technológii a po 30. s nie je **ani jeden presun** (E aj P: 0), takže predikcia v meranom úseku nemá o čom rozhodovať.
  - **Pozor, veľkú časť tohto výsledku určuje DCC:** už v stave Relaxed 67 % správ 802.11p čaká a 25 % nahradí novšia, SPATEM a CPM
    cez 802.11p klesnú na ~0.4-0.46. Zodpovedá za to hlavne **môj predpoklad** Toff úmerné Ton pre rámce > 1 ms (norma ho nemá);
    SPATEM s podpisom má ~1.02 ms a Toff ~102 ms pri perióde 100 ms. Treba overiť citlivosť (napr. Toff bez škálovania).
  - PRB LTE ~0.5 pri ~2 Mbit/s v 10 MHz sa zdá vysoké, príčina neoverená (možno plánovač prideľuje viac PRB než treba).

  **Scenár R: 280 áut, všetky s oboma technológiami: zatiaľ len A** (3 h 07 min; E zastavené, P nespustené): skutočné PRR 0.901, uplink 0.951;
  CBR 802.11p 0.234 (≥ 0.3 30 %), NR SL-CBR 0.277 (≥ 0.3 36 %), LTE PRB (10 MHz) 0.619 (≥ 0.6 70 %); oneskorenie auto-auto 802.11p 68 ms / NR 19 ms,
  LTE -> RSU 133 ms; SPATEM 802.11p / NR **0.156** / 0.976, MAPEM 0.757 / 0.966, CPM 0.264 / 0.937; DCC: 802.11p hneď / vo fronte / nahradené
  5.5 / 57 / 37 %, stav Relaxed 72 %; limit CR 0 zahodených; 29.2 km/h; 0 porušení ETSI. Veľmi nízke SPATEM cez 802.11p môže byť dôsledok môjho
  predpokladu o Toff (overí porovnávací test).
- **2026-11-02** – **Séria zastavená na prenos na počítač s 32 GB RAM** (15:39, na želanie). Hotové LA, LE, LP, RA; RE zastavené v 32. s zo 60,
  treba ho pustiť znova; RP, MA, ME, MP a porovnávací test DCC nespustené. Časy, koľko behov išlo naraz a postup prenosu: `xgregorm_STAV_PRACE.md`.
  Logy presunuté z `~/VaN3Twin/xgregorm_logs/` do `xgregorm_logs/` v repozitári, `.gitignore` povoľuje ich `.log`. `ser.sh` má relatívne cesty a `JOBS`
  (behov naraz). Opravené: beh sa považoval za hotový podľa `Wall clock`, ten sa však vypíše aj po zabití simulácie; teraz sa vyžaduje aj `XGREGORM-V2V`.
  Zistené: `git clone` na prenos nestačí, `.gitignore` vynecháva veľkú časť ns-3 vrátane `src/lte`, preto archív.
- **2026-11-02** – **Prenos: archívy v GitHub Release `prenos-2026-11-02`** (súkromný repozitár): `ns-3-dev` bez `build/` a celý `/usr/local` (SUMO 1.18.0, gRPC 1.60.0, len
  Ubuntu 24.04). Zistené: gRPC je povinné pre `./ns3 configure` (`src/carla/CMakeLists.txt`), hoci CARLA nepoužívame. Postup pre nový počítač vrátane referenčného behu
  (30 áut, 20 s: `XGREGORM-V2V,EXP:2129,RX:2129`) a `JOBS`: `xgregorm_PRENOS_NA_NOVY_PC.md`.
- **2026-11-02** – **Plán dôkazu prínosu predikcie: `xgregorm_PLAN_PREDIKCIA.md`** (hypotézy, politiky, scenáre, štatistika, etapy). Zistené: **broadcast 802.11p išiel 3 Mbit/s**,
  EN 302 663 V1.2.1 tab. 2 má pre G5-CCH predvolených 6 Mbit/s; `--datarate` sa na broadcast nevzťahuje (`NonUnicastMode` nebol nastavený) a `xgdcc::airtime` počítal 24 bitov na symbol, hoci komentár uvádzal 6 Mbit/s.
  Nové `--wifi-bcast-rate 3/6/12` (0 = ako doteraz). Bez voľby bit po bite zhodné (30 áut, 20 s: `XGREGORM-V2V,EXP:2129,RX:2129`, `WIFIFRAMES,N:2966,BYTES:323086`). Škálovanie Toff úmerne Ton má oporu v TS 102 687 Annex B a C2C-CC TR 2049.
  Séria `ser.sh` (RE až MP) sa v starej konfigurácii **nedopúšťa**, kým nerozhodne kalibrácia `xgregorm_logs/kal.sh` (C6A/E/P = L s 6 Mbit/s, C6UA/E/P = navyše NR 20 MHz na 5885 MHz vedľa 802.11p; 140 áut, 60 s, 1 beh na prípad).
- **2026-11-02** – **Audit a nové voľby hybridu** (záznam v `xgregorm_PLAN_PREDIKCIA.md`, časť 5a; bez volieb bit po bite zhodné, overené znova): `--clean 1 --init-main 0/1/2 --probe-hz --cell-dist` (čistá politika),
  `--rem-period` (hlásenia mapy cez LTE), `--feat-log` a `--pred-model` s `xgregorm_pred_train.py` (naučený model, logistická regresia), `--ped --ped-kbps --ped-vam-hz --ped-hotspot` (chodci, len LTE), `xgregorm_eval.py` (tabuľky a párové porovnanie).
  Krátky test (30 áut, 25 s, od 10 s, 1 beh, len overenie chodu): skutočné PRR čisté 802.11p 0.806, čisté NR 0.941, kvalita 0.948, predikcia z mapy 0.935; 0 porušení ETSI. Chodci: 40, 15 s, beh bez pádu.
- **2026-11-02** – **Kalibrácia (140 áut, 60 s, od 30 s, 1 beh na prípad, `xgregorm_logs/C6*.log`):** A / kvalita / predikcia: 3 Mbit/s + NR 40 MHz súbežne (L) PRR 0.946 / 0.967 / 0.967; 6 Mbit/s súbežne (C6) 0.942 / 0.961 / 0.963; 6 Mbit/s + NR 20 MHz na 5885 MHz vedľa 802.11p (C6U) 0.942 / 0.955 / 0.953.
  SPATEM cez 802.11p (A): 0.454 / 0.553 / 0.734; CPM cez 802.11p (A): 0.457 / 0.624 / 0.702; správy 802.11p odoslané hneď 8.8 / 49.4 / 50.2 %. Pri C6U kvalita a predikcia presunú toľko áut, že NR má SL-CBR ≥ 0.3 v 60-68 % času, teda výber má reálnu protiváhu. **Zvolená základná konfigurácia: C6U** (norma pre rýchlosť, oddelené kanály).
  Nápady 1-6 (voľby `--dcc-offload --nb-equip --rsu-equip --rsu-route --dup-near --geo-rat --cpm-rat`, plán časť 8) implementované, ladiaca séria `xgregorm_logs/dev.sh` (seedy 101-103) beží; výsledky budú tu. Literatúra: presný ekvivalent v rešerši nenájdený (záznam v pláne).
- **2026-11-03** – **Ladiaca séria `dev.sh`** (140 áut, 60 s, od 30 s, C6U, seedy 101-103, priemer; PRR = skutočné PRR auto-auto, len CAM): len 802.11p 0.784, len NR **0.660** (SL-CBR 0.535, 78 % ≥ 0.3), 50/50 0.871, kvalita 0.874, starý režim so zálohami **0.941** (CAM cez NR +9098, LTE 9107 oproti 3220), nápad 1 0.847, 4 0.867, 5 0.856, 6 (CPM cez NR) 0.861; RSU W/N: bez trasy 0.805, uplink 0.643, nápad 3 0.830, uplink 0.795.
  **Dôležité:** PRR meria len CAM. Starý režim posiela CPM cez 802.11p (chyba `mask = WIFI`, viď plán 5a), takže NR ostane voľné pre CAM (SL-CBR 0.194) a veľké PRR je čiastočne tým; pri čistých režimoch ide CPM hlavnou technológiou a 20 MHz NR zahltí. Porovnanie musí zahŕňať aj CPM. Nová hypotéza: CAM cez NR + CPM cez 802.11p (`I6R`). Predikčný model na neviditeľnom seede: MAE 802.11p 0.068 (bez zmeny 0.084, mapa 0.072), NR 0.067 (0.080, 0.071). Padlo: `--equip` + `--coex`, keď auto č. 0 nemá 802.11p (null `p0`), opravené; EQF a I2 seedy 102-103 sa opakujú v `dev2.sh`.
- **2026-11-03** – **Druhá ladiaca séria `dev2.sh`** (rovnaké podmienky, seedy 101-103, priemer; PRR CAM / doručenie CPM na kópiu / CAM cez LTE): len 802.11p 0.784 / 0.681 / 3221, 50/50 0.871 / 0.823 / 3220, kvalita 0.874 / 0.795 / 3231, predikcia z mapy s hláseniami cez LTE 0.874 / 0.788 / 3228, naučený model 0.872 / 0.794 / 3225, podľa preťaženia 0.787 / 0.688 / 3222 (presun takmer nenastane, 802.11p ≥ 0.3 len 2 % času, 69 CAM cez NR), starý režim 0.941 / 0.698 / 9107, 1 + 4 0.900 / 0.775 / 3238, **CAM cez NR + CPM cez 802.11p (`I6R`) 0.952 / 0.760 / 3215**, zmes výbavy pevne 0.585, nápad 2 0.672.
  Skúšky pri kvalite a predikcii: ~2090 CAM (15 % CAM). **Predikcia ani v tejto sérii nepriniesla prínos** oproti 50/50, hoci chyba odhadu je menšia (seed 101: 802.11p 0.076 oproti 0.089). Dôvod treba overiť scenárom s meniacimi sa podmienkami.
  **Nález:** ns-3 má prah detekcie preambuly 802.11p −82 dBm, EN 302 663 tab. 1 pri 6 Mbit/s vyžaduje citlivosť aspoň −88 dBm. 802.11p bolo teda znevýhodnené o 6 dB, čo môže skresľovať aj výsledok `I6R`. Nová voľba `--wifi-rx-min`, bez nej bit po bite zhodné. Séria `dev3.sh` opakuje kľúčové režimy s −88 dBm.
- **2026-11-03** – **Tretia ladiaca séria `dev3.sh` s prahom 802.11p −88 dBm** (inak ako dev2, seedy 101-103; PRR CAM / CPM na kópiu): len 802.11p 0.837 / 0.721 (s −82 dBm 0.784), podľa preťaženia 0.841 / 0.730, 50/50 0.897 / 0.845, kvalita **0.905** / 0.821 (oproti 50/50 po seedoch +0.009, +0.007, +0.010), 1 + 4 0.923 / 0.814, CAM cez NR + CPM cez 802.11p 0.952 / 0.801, starý režim 0.958 / 0.737 (LTE 9022 oproti 3215). Model na neviditeľnom seede: 802.11p 0.054 (bez zmeny 0.066), NR 0.061 (0.075). **Základná konfigurácia odteraz: C6U + `--wifi-rx-min -88`.**
- **2026-11-03** – **Štvrtá ladiaca séria `dev4.sh`** (120 s, 4 zóny polomer 150 m, strata príjmu 70 %, dve 802.11p a dve NR; kontrolovaný experiment, nie fyzikálny model; seedy 101-103; PRR CAM / CPM na kópiu 802.11p a NR / CAM cez LTE): len 802.11p 0.608 / 0.525 / 10274, 50/50 0.661 / 0.546 a 0.687 / 10327, I6R 0.736 / 0.562 / 10368, starý režim 0.845 / 0.522 a 0.815 / 24696, kvalita Q 0.780 / 0.648 a 0.800 / 10299, predikcia z mapy PM 0.782 / 0.641 a 0.813 / 10348, model PL (učený bez zón) 0.754 / 0.602 a 0.792 / 10299. Párovo, 95 % interval, n = 3: Q − 50/50 +0.119 (+0.083, +0.155); PM − Q +0.002 (−0.003, +0.007); PL − Q −0.026 (−0.035, −0.017). **Výber podľa aktuálnej kvality v nerovnorodom prostredí jasne pomáha, predikcia k nemu nepridala nič a model učený bez zón škodí.** Mimo siete, model preučený na zónach (dva seedy učenie, tretí test, horizont 3 s): MAE 802.11p 0.066-0.068 (bez zmeny 0.083-0.084, mapa 0.067-0.069), NR 0.076-0.078 (bez zmeny 0.088-0.093, mapa 0.076-0.078): model teda mape nepridáva. Presnejšia predikcia sa do siete nepremieta, preto `--oracle 1` (`XgOracle`, nerealizovateľná referencia, auto pozná zóny presne, výhľad `--pred-horizon`, teraz od 0). Bez voľby bit po bite zhodné (`XGREGORM-V2V,EXP:2129,RX:2129`, `WIFIFRAMES,N:2966,BYTES:323086`). Súbežné behy potrebujú rozostup 20 s, inak `Address already in use` na porte SUMO. Spustená `dev5.sh`: O3 − O0 je horná hranica hodnoty pozerania dopredu, O0 − Q hodnota okamžitej znalosti; navyše model učený na zónach (PZ) a zóny s polomerom 50 m.
- **2026-11-03** – **Piata ladiaca séria `dev5.sh`, horná hranica predikcie** (27 behov, žiadny nezlyhal, seedy 101-103; porovnania s Q pri 150 m používajú Q z dev4, referencia bit po bite zhodná). PRR / CPM spolu: 150 m: Q 0.780 / 0.713, PM 0.782 / 0.714, PZ 0.785 / 0.715, O0 0.789 / 0.746, O3 0.800 / 0.758, O8 0.801 / 0.760. 50 m: 50/50 0.747 / 0.699, Q 0.838 / 0.767, PM 0.846 / 0.773, O0 0.845 / 0.803, O3 0.852 / 0.811. Párovo, PRR, 95 % interval, n = 3: 150 m O3 − O0 +0.0114 (+0.0035, +0.0192), O8 − O0 +0.0116 (+0.0043, +0.0190), O3 − Q +0.0203 (+0.0079, +0.0328), PM − Q +0.0021 (−0.0030, +0.0073), PZ − Q +0.0051 (−0.0134, +0.0236), Q − 50/50 +0.119 (+0.083, +0.155). 50 m O3 − O0 +0.0077 (+0.0035, +0.0118), O3 − Q +0.0140 (+0.0118, +0.0163), PM − Q +0.0081 (−0.0069, +0.0230), Q − 50/50 +0.091 (+0.075, +0.108). **Podľa pravidla z časti 8a plánu má výhľad pri 150 m malý preukázateľný prínos (nad prahom 0.01), pri 50 m je pod prahom.** Hypotéza, že kratší prejazd predikcii pomôže viac, sa nepotvrdila. Skutočná predikcia zatiaľ z hranice nezískala preukázateľne nič. Rozdiely O* − Q nie sú čistá hodnota znalosti: O* nemajú skúšky (CPM +0.034 až +0.037 pri O0 − Q, príčiny nie sú oddelené) a latenciu majú asi o 3 ms horšiu. Hranica platí len pre zóny, nie pre preťaženie z premávky. Pamäť: 8 behov po 120 s potrebuje spolu až asi 26-28 GB, `xgregorm_logs/xgregorm_guard.py` pri nedostatku pozastavuje behy (SIGSTOP), žiadny nespadol.
  Voľba `--zones rsu:r:t0:t1:W|N:strata+...` = lokálne zhoršenie príjmu (kontrolovaný experiment, nie fyzikálny model); bez nej bit po bite zhodné. Séria `dev4.sh`: 120 s, 4 zóny 150 m so stratou 70 % (2 pre 802.11p, 2 pre NR), rozhodujúci test predikcie (hypotéza H3). PL používa model naučený na dev bez zón.
- **2026-11-03** – **Kontrola zápisov po spustení na 20 GB a príprava `dev6`.** Logy dev až dev5 kompletné, výsledky dev4 a dev5 prepočítané z logov sú zhodné s Denníkom. Zistené: `.wslconfig` má 22 GB, ale WSL nebolo reštartované, takže simulácie vidia 20 GB (stačí). Nový spúšťač `xgregorm_logs/xgregorm_series.py` + `xgregorm_start.sh`: spec v JSON, po prerušení preskočí hotové behy, cgroup `MemoryHigh=17G` necháva ~3 GB pre systém a Claude (nahrádza `xgregorm_guard.py`). `xgregorm_eval.py` doplnený o chýbajúce metriky z plánu: oneskorenie p95 po technológiách, podiel sekúnd s PRR < 0.9, PRR ±5 s okolo zmeny (`window_prr`), vek informácie, doručenie SPATEM a CPM spolu, presuny na auto za minútu, skúšky na auto za sekundu, bajty mapy vrátane odhadu downlinku, čas prepnutia voči vstupu do zóny (`zone_timing`, H2), Holmova korekcia a p-hodnoty. Na dev4 výsledok `PM − Q` rovnaký ako predtým (+0.0021, −0.0030, +0.0073). H2 na dev4 Q: v zóne na postihnutej technológii 15-19 % času, medián prepnutia 4 s **po** vstupe, pred vstupom len 13-16 % prepnutí. Skript `xgregorm_algo_select.py` (GBM, MLP, GRU, Kalman, mapa, logit; hodnotí aj rozhodnutie). Prvý test dev4, učenie seedy 101-102, test 103: MAE zostane ako teraz 0.090, mapa 0.074, GBM 0.065, GRU 0.067; spoľahlivosť zvolenej technológie 0.791 / 0.797 / 0.798 / 0.797, najlepšia možná 0.812.
- **2026-11-03** – **Šiesta ladiaca séria `dev6`** (42 behov, žiadny nezlyhal, 13:57-22:46, 6 naraz, seedy 101-103, 120 s, od 30 s, 0 porušení ETSI; definícia v pláne, časť 9). AQ je po seedoch zhodné s Q z dev4 (0.77797, 0.76883, 0.79315), teda nové výpisy a spúšťač správanie nezmenili. Priemer, PRR / CPM spolu / uplink / presuny na auto za min / skúšky na auto za s:

  | | MIX | Q | PM | B | O0 | O3 |
  |---|---|---|---|---|---|---|
  | A | – | 0.780 / 0.713 / 0.900 / 0.98 / 0.50 | – | 0.748 / 0.702 / 0.877 / 1.07 / 0.19 | – | – |
  | B | – | 0.883 / 0.814 / 0.931 / 1.37 / 0.50 | – | 0.861 / 0.812 / 0.905 / 1.26 / 0.16 | – | – |
  | C | 0.659 / 0.609 / 0.808 | 0.789 / 0.711 / 0.898 / 1.22 / 0.50 | 0.763 / 0.679 / 0.879 / 1.16 / 0.50 | 0.753 / 0.697 / 0.870 / 1.38 / 0.19 | 0.803 / 0.746 / 0.920 / 0.81 / 0 | 0.812 / 0.753 / 0.929 / 0.88 / 0 |
  | T | 0.890 / 0.848 / 0.958 | 0.908 / 0.833 / 0.958 / 1.69 / 0.50 | 0.906 / 0.834 / 0.958 / 0.77 / 0.50 | 0.900 / 0.844 / 0.955 / 1.51 / 0.10 | – | – |

  Párovo, PRR, 95 % interval, n = 3: **C:** Q − MIX +0.129 (+0.088, +0.170), PM − Q **−0.026** (−0.038, −0.014), B − Q −0.035 (−0.050, −0.021), O0 − Q +0.014 (+0.007, +0.022), O3 − O0 +0.009 (+0.005, +0.013), O3 − Q +0.023 (+0.018, +0.029). **T:** Q − MIX +0.018 (+0.015, +0.022), PM − Q −0.002 (−0.005, +0.001; presuny 1.69 -> 0.77 na auto za min), B − Q −0.009 (−0.019, +0.002). **A:** B − Q −0.032 (−0.044, −0.019). **B:** B − Q −0.022 (−0.046, +0.002).
  PRR v úsekoch C, 30-60 / 60-75 / 75-90 / 90-105 / 105-120 s: MIX 0.620 / 0.591 / 0.668 / 0.775 / 0.767; Q 0.757 / 0.747 / 0.814 / 0.857 / 0.856; PM 0.767 / **0.630** / **0.743** / 0.867 / 0.858; B 0.724 / 0.690 / 0.795 / 0.825 / 0.829; O0 0.768 / 0.783 / 0.814 / 0.872 / 0.870; O3 0.780 / 0.797 / 0.823 / 0.875 / 0.866. Čas prepnutia voči vstupu do zóny (H2), podiel času v zóne na postihnutej technológii / prepnutia pred vstupom / medián po vstupe: AQ 17 % / 15 % / 4 s, BQ 15 % / 15 % / 4 s, CQ 18 % / 14 % / 4 s, CPM 26 % / 35 % / 4 s, CB 22 % / 17 % / 4-5 s.
  **Závery:** (1) výber podľa aktuálnej kvality ostáva najlepšou realizovateľnou politikou vo všetkých štyroch scenároch dev6; (2) **H3 pre mapu sa v scenári so zmenami v čase nepotvrdila, mapa škodí:** po výmene technológií v 60. s PM klesne na 0.630 oproti 0.747 pri Q, lebo mapa drží staré hodnoty (zabúdanie 30 s), mimo tohto úseku je PM voči Q o 0.002-0.010 lepšia (bez testu po úsekoch); (3) H2: PM prepína pred vstupom častejšie (35 oproti 14 %), ale v C trávi na zlej technológii viac času; (4) H4 platí: v T je PM − Q približne 0, PM však má menej ako polovicu presunov; (5) **bandit v tejto podobe neuspel:** vo všetkých scenároch má nižšie PRR než Q (A a C preukázane), skúšky znižuje o 61-80 %. Príčina nie je oddelená, môže to byť práve menej skúšok alebo samotné pravidlo; (6) výhľad: O3 − O0 pri C +0.009 je podľa časti 8a pod prahom 0.01, takže pozeranie dopredu má malú hornú hranicu aj pri zmenách v čase. O* − Q opäť zahŕňa aj chýbajúce skúšky, nie je to čistá hodnota znalosti. Pozn.: pri MIX je `--feat-log` prázdny, záznam funguje len s pravidlom kvality. Výsledky: `xgregorm_logs/dev6/`.
- **2026-11-03** – **Výber algoritmu na záznamoch dev6** (`xgregorm_algo_select.py`, výstupy `xgregorm_logs/dev6_algo/`; vždy test na seede mimo učenia, 3 rezy, priemer). Metrika: skutočná spoľahlivosť zvolenej technológie o 3 s. Vstupy sú merania z behov Q, mapa v nich neposiela hlásenia a nie je oneskorená, teda je pre mapu optimistická.

  | test | persist. = Q | mapa | Kalman | GBM (s mapou) | GBM + správy, s mapou | GBM + správy, bez mapy | najlepšia možná |
  |---|---|---|---|---|---|---|---|
  | A (učenie A, B, C, T) | 0.7873 | 0.7929 | 0.7872 | 0.7940 | 0.7941 | 0.7918 | 0.8069 |
  | B | 0.8682 | 0.8687 | 0.8678 | 0.8700 | 0.8699 | 0.8690 | 0.8944 |
  | C | 0.7779 | **0.7501** | 0.7802 | 0.7800 | 0.7940 | **0.7951** | 0.8167 |
  | T | 0.8814 | 0.8806 | 0.8818 | 0.8822 | 0.8817 | 0.8822 | 0.9099 |

  Bez učenia na rovnakom scenári (samostatný rez, GBM s mapou): A -> B oproti persistencii −0.0005, A + B -> C **−0.016** (so správami −0.007), teda **presun na nevidené podmienky je hlavná slabina** a modely musia vidieť aj zmeny. GRU a logit nepriniesli výhodu oproti GBM. MLP so správami je pri učení na všetkých scenároch v C mierne lepší (bez mapy 0.7967 oproti 0.7951), v A, B a T je však horší (0.7914, 0.8669, 0.8803 oproti 0.7918, 0.8690, 0.8822), preto ostáva GBM. Na neznáme rozloženie boli GRU aj MLP horšie než persistencia (GRU A -> B −0.0056). Do siete nejdú. Kalman je prakticky ako persistencia (C +0.0023). Najsilnejšie vstupy bez mapy: `r`, `spatr` (SPATEM po technológiách), `dist`, `cpmn`, `cpmobj`. V T nepredikuje nič: model získa +0.0008, presnosť rozhodnutia 0.61 oproti 0.59, hoci teoretický strop je +0.0285 (strop je optimistický, obsahuje aj náhodný šum vzoriek). **Horná hranica pre T zo záznamov teda existuje, ale z dostupných vstupov sa nedá využiť.** Stromy: 200 stromov, 6200 listov, 28 vstupov, na záznamoch C++ a Python bez rozdielu (65 tisíc riadkov), súbor 0.4 MB, jedna predikcia ~200 porovnaní × hĺbka stromu.
  Nové voľby `--pred-gbm` (stromy, `xgregorm-gbm.h`, `xgregorm_gbm_export.py`) a `--bandit-probe`. Referencia po prebudovaní bez zmeny (`EXP:2129,RX:2129`, `N:2966,BYTES:323086`). Spustená séria `dev7` (plán, časť 9).
- **2026-11-04** – **Siedma ladiaca séria `dev7`: naučené stromy a bandit v simulácii siete** (30 behov, žiadny nezlyhal, 23:17-05:52, 6 naraz, seedy 101-103, 120 s, od 30 s, 0 porušení ETSI; definícia pred behmi v pláne, časť 9). Q pri A, B, C, T je z dev6. Priemer, PRR / CPM spolu / uplink / presuny na auto za min:

  | | MIX | Q | GS | O3 | B2 |
  |---|---|---|---|---|---|
  | A | – | 0.780 / 0.713 / 0.900 / 0.98 | 0.769 / 0.695 / 0.896 / 0.35 | – | 0.771 / 0.699 / 0.895 / 0.94 |
  | B | – | 0.883 / 0.814 / 0.931 / 1.37 | 0.878 / 0.810 / 0.925 / 0.35 | – | – |
  | C | – | 0.789 / 0.711 / 0.898 / 1.22 | 0.779 / 0.697 / 0.889 / 0.66 | – | 0.780 / 0.704 / 0.891 / 1.32 |
  | T | – | 0.908 / 0.833 / 0.958 / 1.69 | 0.905 / 0.838 / 0.958 / 0.18 | – | – |
  | **D** (nevidený) | 0.734 / 0.704 / 0.841 | 0.821 / 0.755 / 0.906 / 1.14 | **0.808** / 0.743 / 0.897 / 0.54 | 0.841 / 0.814 / 0.938 / 0.66 | – |

  Párovo, PRR, 95 % interval, n = 3: **D (rozhodujúci): GS − Q −0.0133 (−0.0187, −0.0080)**, CPM −0.012 (−0.032, +0.007), uplink −0.009 (−0.017, −0.001); Q − MIX +0.0866 (+0.0744, +0.0988); O3 − Q +0.0199 (+0.0130, +0.0269), CPM +0.059. A: GS − Q −0.0113 (−0.0178, −0.0049). B: −0.0048 (−0.0315, +0.0219). C: −0.0093 (−0.0258, +0.0071). T: −0.0038 (−0.0058, −0.0018), CPM +0.004 (−0.005, +0.014), presuny 1.69 -> 0.18 na auto za min. Bandit s plnými skúškami: AB2 − AQ −0.0087 (−0.0168, −0.0006), AB2 − AB +0.0230 (+0.0106, +0.0355), CB2 − CQ −0.0091 (−0.0223, +0.0041), CB2 − CB +0.0261 (+0.0245, +0.0276).
  PRR v úsekoch D, 30-50 / 50-65 / 65-85 / 85-100 / 100-120 s: MIX 0.641 / 0.778 / 0.771 / 0.777 / 0.762; Q 0.752 / 0.809 / 0.863 / 0.863 / 0.867; GS 0.731 / **0.821** / 0.837 / 0.864 / 0.842; O3 0.790 / 0.844 / 0.859 / 0.882 / 0.871. C, 30-60 / 60-75 / 75-90 / 90-105 / 105-120 s: Q 0.757 / 0.747 / 0.814 / 0.857 / 0.856; GS 0.745 / **0.778** / 0.817 / 0.830 / 0.806.
  **Závery:** (1) **GS kritérium nesplnil, v D je preukázateľne horší než Q.** Výber podľa aktuálnej kvality ostáva najlepšou realizovateľnou politikou. (2) GS má menej ako polovicu presunov a tesne po zmene zón je lepší (C 60-75 s +0.031, D 50-65 s +0.012, jeden úsek a 3 seedy, bez testu po úsekoch), v ustálenom stave však stráca (C 105-120 s −0.050, D 100-120 s −0.025). (3) **Rozpor záznamy a sieť:** tie isté modely na záznamoch DQ, ktoré nevideli, volia lepšie než persistencia (po seedoch 0.8234 / 0.8137 / 0.8359 oproti 0.8194 / 0.8068 / 0.8311), MAE 0.076-0.079 oproti 0.101-0.105. Na záznamoch z vlastných behov DGS je to podobne (+0.003 až +0.006), no PRR v sieti klesne. Lepšia predikcia sa teda do siete neprenáša. Možné vysvetlenie, zatiaľ neoverené: spoľahlivosť v zázname je to, čo auto samo prijíma, a do PRR sa premieta nepriamo; predikcia zužuje rozdiel medzi technológiami (rozdiel aspoň 0.05 v 50-55 % oproti 72-74 % prípadov v D, v T 12-17 % oproti 47-51 %), takže auto reaguje pomalšie. (4) **Bandit neuspel ani s plnými skúškami:** väčšinu straty z dev6 vysvetľuje úbytok skúšok (+0.023 a +0.026), samotné pravidlo však ani potom Q neprekoná (A preukázane horšie). Úloha uzavretá. (5) Model sa po pohľade na D nemenil a menený nebude. Záznamy: `xgregorm_logs/dev7/`, CSV len na disku.
  Spustené: `final1` (120 behov), po nej reťazou `xgregorm_chain2.sh` `final2` a `idea1` až `idea5`, definície v pláne, časť 9. Bez zásahu do C++.
- **2026-11-06** – **Finálna séria `final1` + `final2`, záverečné porovnanie** (200 behov, žiadny nezlyhal, 2026-11-04 05:52 až 2026-11-06 ~01:00, seedy 1-10, ktoré sa nikdy nepoužili na ladenie, 120 s, od 30 s, 0 porušení ETSI; definície v pláne, časť 9; skript `xgregorm_final_eval.py`). Priemer, PRR / CPM spolu / uplink / presuny na auto za min:

  | | MIX | Q | PM | GS | O0 | O3 |
  |---|---|---|---|---|---|---|
  | A | 0.653 / 0.607 / 0.808 | 0.787 / 0.723 / 0.902 / 1.00 | 0.788 / 0.723 / 0.905 / 0.71 | – | 0.796 / 0.756 / 0.911 | 0.807 / 0.768 / 0.925 |
  | C | 0.661 / 0.615 / 0.807 | 0.790 / 0.721 / 0.899 / 1.32 | 0.761 / 0.685 / 0.874 / 1.14 | – | 0.805 / 0.758 / 0.919 | 0.814 / 0.765 / 0.928 |
  | D | 0.734 / 0.700 / 0.836 | 0.815 / 0.756 / 0.905 / 1.23 | 0.800 / 0.734 / 0.892 / 0.93 | 0.810 / 0.750 / 0.900 / 0.62 | 0.836 / 0.809 / 0.931 | 0.842 / 0.814 / 0.939 |
  | T | 0.884 / 0.844 / 0.951 | 0.906 / 0.840 / 0.959 / 1.80 | 0.903 / 0.837 / 0.957 / 0.72 | 0.901 / 0.837 / 0.956 / 0.25 | – | – |

  Párovo, PRR, 95 % interval, n = 10 (Holm pre vedľajšie metriky):

  | rozdiel | A | C | D | T |
  |---|---|---|---|---|
  | **Q − MIX** | +0.134 (+0.125, +0.143) | +0.129 (+0.121, +0.137) | +0.081 (+0.074, +0.088) | +0.021 (+0.018, +0.024) |
  | PM − MIX | +0.136 (+0.125, +0.146) | +0.100 (+0.094, +0.107) | +0.066 (+0.059, +0.073) | +0.019 (+0.016, +0.022) |
  | GS − MIX | – | – | +0.076 (+0.067, +0.085) | +0.017 (+0.015, +0.018) |
  | PM − Q | +0.001 (−0.003, +0.006) | **−0.029** (−0.033, −0.025) | **−0.015** (−0.020, −0.010) | −0.003 (−0.006, +0.001) |
  | GS − Q | – | – | **−0.005** (−0.010, −0.000) | **−0.005** (−0.008, −0.001) |
  | O0 − Q | +0.010 (+0.006, +0.013) | +0.016 (+0.009, +0.022) | +0.021 (+0.017, +0.025) | – |
  | **O3 − O0** | +0.011 (+0.008, +0.014) | +0.009 (+0.005, +0.012) | +0.007 (+0.005, +0.008) | – |
  | O3 − Q | +0.020 (+0.018, +0.023) | +0.024 (+0.017, +0.031) | +0.028 (+0.025, +0.031) | – |

  Vedľajšie metriky: Q − MIX má v A, C, D aj CPM spolu +0.056 až +0.116 a uplink +0.069 až +0.094 (Holm p < 0.001), vek informácie RSU −0.29 až −0.40 s, doručenie SPATEM −0.005 až −0.007 (A a D Holm p < 0.05). V T je CPM −0.004 (Holm p = 0.15), uplink +0.008. PM − MIX a GS − MIX v T: CPM −0.007 (Holm p = 0.002). LTE (bajty CAM, PRB) sa nemení v žiadnom porovnaní. PM − Q v C a D má aj horšie CPM (−0.036, −0.022). GS − Q: CPM v D −0.006 (Holm p = 0.28), v T bez rozdielu, presuny −0.6 a −1.5 na auto za min. O* − Q obsahuje aj chýbajúce skúšky (CPM O0 − Q +0.033 až +0.053).
  PRR v úsekoch D (30-50 / 50-65 / 65-85 / 85-100 / 100-120 s): MIX 0.665 / 0.749 / 0.773 / 0.784 / 0.753; Q 0.745 / 0.801 / 0.861 / 0.858 / 0.867; PM 0.742 / 0.781 / 0.833 / 0.834 / 0.858; GS 0.731 / **0.815** / 0.854 / 0.861 / 0.850; O0 0.777 / 0.844 / 0.863 / 0.876 / 0.867; O3 0.794 / 0.845 / 0.865 / 0.875 / 0.872. C (30-60 / 60-75 / 75-90 / 90-105 / 105-120 s): MIX 0.607 / 0.604 / 0.697 / 0.779 / 0.769; Q 0.761 / 0.725 / 0.843 / 0.851 / 0.864; PM 0.764 / **0.609** / 0.766 / 0.855 / 0.865; O0 0.775 / 0.768 / 0.834 / 0.867 / 0.868; O3 0.790 / 0.772 / 0.840 / 0.869 / 0.870.
  **Závery (potvrdené na nezávislých seedoch):** (1) **Každý reaktívny výber je preukázateľne lepší než stav bez reaktívnej logiky** (50/50): Q +0.081 až +0.134 PRR so zónami, +0.021 bez nich, a v zónach výrazne lepšie aj CPM, uplink a vek informácie. Rovnako PM aj GS. (2) Z realizovateľných politík je najlepšie Q. Mapa je pri zmenách v čase preukázateľne horšia (C −0.029, D −0.015) pre staré hodnoty po zmene (C 60-75 s 0.609 oproti 0.725), pri stálych zónach rovnaká ako Q s o 29 % menej presunmi. GS je o 0.005 horšie v D aj T (dev7 −0.013 a −0.004, smer potvrdený), s polovičným až šestinovým počtom presunov a lepšie tesne po zmene (D 50-65 s +0.014, bez testu po úsekoch). (3) **Horná hranica predikcie:** pozeranie dopredu (O3 − O0) dá najviac +0.007 až +0.011 PRR. Podľa časti 8a je nad prahom 0.01 len v A (+0.0109, dolná hranica intervalu 0.008), v C a D pod ním. Okamžitá znalosť stavu bez skúšok (O0 − Q, +0.010 až +0.021) je väčšia než prínos výhľadu. Priestor medzi Q a dokonalou znalosťou je 0.020 až 0.028 PRR. (4) Bez zón (T) je zisk akéhokoľvek výberu malý, okolo +0.02 PRR, pri miernom zhoršení CPM. Záznamy: `xgregorm_logs/final1/`, `xgregorm_logs/final2/`.
- **2026-11-06** – **Overenie nápadov 1-6, `idea1` + `idea26`** (130 behov, žiadny nezlyhal, 2026-11-06 01:00 až 23:15, seedy 201-205, ktoré sa nikdy nepoužili na ladenie, základ ako finálna séria bez zón, 120 s, od 30 s, 0 porušení ETSI; definície pred behmi v pláne, časť 9; skript `xgregorm_idea_eval.py`). Priemer, PRR / CPM spolu / uplink (LTE: bajty CAM a PRB sa nemenia okrem ALL):

  | referencie | PRR / CPM / uplink | nápad | PRR / CPM / uplink |
  |---|---|---|---|
  | FW (len 802.11p) | 0.855 / 0.773 / 0.922 | I1a / I1b / I1c (DCC offload 20 / 50 / 100 ms) | 0.908 / 0.853 / 0.954; 0.883 / 0.839 / 0.937; 0.853 / 0.795 / 0.920 |
  | FN (len NR) | 0.752 / 0.735 / 0.860 | I4a / I4b / I4c (kópia blízko 30 / 60 / 100 m) | 0.887 / 0.771 / 0.945; 0.901 / 0.771 / 0.957; 0.918 / 0.770 / 0.974 |
  | MIX (50/50) | 0.890 / 0.853 / 0.955 | I5a / I5b / I5c (geometria 30 / 60 / 100 m) | 0.885 / 0.837 / 0.948; 0.889 / 0.847 / 0.951; 0.893 / 0.854 / 0.954 |
  | Q (kvalita) | 0.908 / 0.843 / 0.960 | I6R (CAM cez NR, CPM cez 802.11p) | **0.956** / 0.833 / 0.975 |
  | ALL (každá CAM všetkým) | 0.945 / 0.739 / 0.998, CAM cez LTE 4.2x | | |
  | EF1 / EF2 (zmes výbavy) | 0.622 / 0.836 / 0.951; 0.648 / 0.825 / 0.943 | I2a / I2b (výbava susedov) | 0.719 / 0.832 / 0.970; 0.790 / 0.822 / 0.971 |
  | RF1 / RF2 (RSU W,N / W,N,WN) | 0.861 / 0.793 / 0.603, vek 7.4 s; 0.860 / 0.788 / 0.684, vek 5.4 s | I3a / I3b (RSU na trase) | 0.879 / 0.822 / 0.794, vek 0.43 s; 0.877 / 0.804 / 0.840, vek 0.38 s |
  | DMIX / DQ (zóny D) | 0.727 / 0.693 / 0.831; 0.817 / 0.766 / 0.903 | DI6R | 0.836 / 0.644 / 0.860 |

  Párovo, PRR, 95 % interval, n = 5, Holm cez varianty nápadu (všetky uvedené úspechy majú upravené p < 0.01):

  | rozdiel voči vlastnej referencii | PRR | CPM spolu | kritérium | voči Q (PRR) |
  |---|---|---|---|---|
  | I1a − FW | +0.053 (+0.043, +0.063) | +0.080 | **splnené** | +0.000 (−0.008, +0.009) |
  | I1b − FW | +0.028 (+0.018, +0.038) | +0.066 | **splnené** | −0.025 |
  | I1c − FW | −0.002 (−0.006, +0.003) | +0.022 | nesplnené | −0.055 |
  | I2a − EF1 | +0.097 (+0.080, +0.114) | −0.004 (Holm p = 0.19) | **splnené** | – (iná výbava) |
  | I2b − EF2 | +0.142 (+0.120, +0.165) | −0.002 (Holm p = 0.14) | **splnené** | – |
  | I3a − RF1 | +0.018 (+0.012, +0.025) | +0.029, uplink +0.191, vek −6.9 s | **splnené** | – |
  | I3b − RF2 | +0.017 (+0.014, +0.020) | +0.015, uplink +0.156, vek −5.1 s | **splnené** | – |
  | I4a / I4b / I4c − FW | +0.032 / +0.046 / +0.063 | −0.002 až −0.003 (n. s.), SPATEM −0.006 až −0.008 | **splnené** | −0.021 / −0.007 / **+0.010** (+0.005, +0.015), CPM −0.073 |
  | I5a / I5b / I5c − FW | +0.030 / +0.034 / +0.038 | +0.064 / +0.074 / +0.081 | **splnené** | −0.023 / −0.019 / −0.015 |
  | I6R − FW | +0.101 (+0.095, +0.107) | +0.060 | **splnené** | **+0.048** (+0.045, +0.051), CPM −0.010 (Holm p = 0.21) |
  | I6R − MIX | +0.066 (+0.065, +0.068) | **−0.020** (Holm p = 0.014) | PRR áno, CPM horšie | |
  | DI6R − DMIX | +0.109 (+0.079, +0.139) | −0.049 (Holm p = 0.065) | PRR áno, CPM nejasné | DI6R − DQ +0.018 (−0.016, +0.053), CPM **−0.122**, uplink −0.043 |

  Referencie navzájom: Q − FW +0.053, Q − MIX +0.018 (CPM −0.010), MIX − FW +0.035, FN − FW −0.103, ALL − FW +0.090 a ALL − Q +0.037 pri CPM −0.104, SPATEM −0.117 a 4.2x viac CAM cez LTE.
  **Závery:** (1) Každý nápad okrem I1c splnil vopred stanovené kritérium voči svojej referencii bez reaktívnej logiky. (2) Najväčší zisk bez zón: I6R (+0.101 PRR voči FW, +0.048 voči Q), za cenu CPM o 0.010-0.020 horšieho než pri MIX a Q; v zónach (D) sa I6R nevie prispôsobiť: PRR je ako DQ, CPM o 0.122 horšie. Statické rozdelenie podľa typu správy je teda dobré pri stálych podmienkach, reaktívny výber pri meniacich sa. (3) I1a (správa ide cez NR, keď by na 802.11p čakala na DCC dlhšie než 20 ms) dosiahne PRR aj CPM ako Q bez merania kvality a bez skúšok (0 presunov, 0 skúšok). (4) I4c je jediná varianta s PRR nad Q (+0.010) pri nehoršom LTE, ale CPM zostáva ako pri FW (Q −0.073). (5) Nápady 2 a 3 riešia nekompatibilnú výbavu: výber podľa susedov +0.10 až +0.14 PRR, výber podľa RSU na trase uplink +0.16 až +0.19 a vek informácie RSU z 5-7 s na 0.4 s. (6) Pri I4 je SPATEM o 0.006-0.008 horší, pri I2 CPM bodovo mierne horšie, ale nepreukázane. Záznamy: `xgregorm_logs/idea1/`, `xgregorm_logs/idea26/`.
- **2026-11-08** – **Prezentácia priebehu práce** `xgregorm_prezentacia/xgregorm_priebeh_prace.pptx` (40 snímok, na želanie): skratky, začiatky, hybridná sieť, plný realizmus a metodika, alternatívy k Q, niekoľkodňová fronta a záver. Čísla len z tohto Denníka a z plánu. Zdroj a postup generovania v `xgregorm_prezentacia/zdroj/` a `README.md`. Overené validátorom Office XML a vlastným náhľadom (`preview.py`); LibreOffice na vykreslenie nebol k dispozícii.
- **2026-11-10** – **Jednoduché vysvetlenie práce vo Worde** `xgregorm_prezentacia/xgregorm_vysvetlenie_prace.docx` (na želanie, bez skratiek; zdroj `zdroj/vysvetlenie.md`, prevod `zdroj/build_docx.js` + `fix_docx.py`). Čísla len z Denníka. Overené validátorom Office XML.
- **2026-11-15** – **Prezentácia skrátená na 15 snímok, zameraná na výber podľa kvality** (na želanie; `xgregorm_prezentacia/`, pôvodná 40-snímková v commite `0ad05920`): opravy, hustota a vyťaženie, prepínač, meranie kvality a pravidlo prepnutia, výsledky, predikcia, záver; text k snímkam v poznámkach aj ako 1. časť Wordu (`zdroj/text_prezentacie.md`). Nové prepočítanie z logov `idea1` (náhody 201–205), párovo, 95 % interval: Q − FN PRR +0.156 (+0.142, +0.170), CPM +0.108; Q − FW +0.053 (+0.046, +0.060), CPM +0.070 (+0.059, +0.081); Q − MIX +0.018 (+0.014, +0.022), CPM −0.010 (−0.013, −0.007); Q − ALL −0.037 (−0.042, −0.033), CPM +0.104, SPATEM +0.117, uplink −0.038; CAM cez LTE Q 10 154 oproti ALL 42 498.
- **2026-11-15** – **Prezentácia: rozdelenie áut na sieťach, počty správ, oprava výkladu „všetko naraz“.** Z logov `idea1` (priemer 5 behov, od 30. s): hlavná sieť Q 59.6 % 802.11p / 40.4 % NR (začiatok 47.7 %, v 5. s 71.3 %, potom 55-66 %), MIX 47.7 / 52.3; obsadenosť 802.11p / NR (odhad SL-CBR): FW 0.113 / 0.054, FN 0.033 / 0.567, MIX 0.074 / 0.357, Q 0.088 / 0.294, ALL 0.127 / 0.569. CAM (správa × sused do 100 m) očakávané / doručené za beh: FW 384.8k / 329.0k, FN 382.1k / 287.1k, MIX 383.7k / 341.5k, Q 385.1k / 349.6k, ALL 383.8k / 362.7k; ~3.4 CAM na auto za s, ~9 susedov. **Pozor pri ALL:** `cpm_all` a `spat_all` sú doručenie na kópiu, PRR CAM je „aspoň jedna kópia“. ALL posiela CPM oboma sieťami (TX 99k + 99k), takže CPM ALL − Q −0.104 nie je strata CPM u príjemcu, ale horšie doručenie každej kópie pri preplnení (CPM 802.11p kópia Q 0.800 / ALL 0.744, NR 0.921 / 0.734; SPATEM 802.11p 0.788 / 0.686, NR 0.964 / 0.832). Doručenie CPM „aspoň jednou kópiou“ sa nemeria. Podrobnosti `xgregorm_prezentacia/zdroj/podrobnosti.md`.
- **2026-11-15** – **Word: základy (druhy správ, rozdiel 802.11p / NR / LTE, obsadenosť kanála).** Oprava: oneskorenie auto-auto 802.11p vo finálnej konfigurácii nie je ~0.3 ms (to je len prenos), ale s čakaním na DCC priemerne FW 28.8 / Q 24.7 ms, NR FN 26.5 / Q 18.9 ms (`XGREGORM-V2VLAT`, idea1). Obsadenosť NR 5 % pri FW = SPATEM/MAPEM cestných jednotiek (posielajú vždy oboma sieťami); FN a ALL majú rovnakú záťaž NR (všetky CAM a CPM idú cez NR).
- **2026-11-15** – **Prezentácia 16 snímok (pridané druhy správ) a Word: cesta cez mobilnú sieť, výpočet obsadenosti, časť „čo všetko som skúšal“** (`zdroj/skusane.md`). Z logov `idea1`: oneskorenie LTE (XGREGORM-LATP) p50 ~22 ms, p95 ~1.0-1.1 s pri všetkých spôsoboch; PRB LTE priemer 0.44 (ALL 0.49), nad 0.3 93-96 % času (ALL 99.9 %); telefóny 69 áut, 22 404 paketov po 1000 B za beh.
- **2026-11-17** – **Upratanie, 802.11p namiesto Wi-Fi, pripomienka o čase** (na želanie). Zmazané nepoužité súbory:
  - prepis konverzácie (45 MB, kópia ostala len v archíve Release `prenos-2026-11-02`),
  - prerušený beh `RE`,
  - reťazové skripty `xgregorm_chain*.sh` a `xgregorm_guard.py` (nahradil ich `xgregorm_start.sh`),
  - prázdne výstupy `*.out`.

  Z kódu odstránené voľby `--ped` a `--nr-sensing`, ktoré nepoužila žiadna séria. Všetko ostatné, aj horšie
  alternatívy (stromy, mapa, bandit, zóny, hustota v zálohe), ostáva. V prezentácii a vo Worde „Wi-Fi“ → 802.11p (ITS-G5)
  s vysvetlením, prečo to nie je Wi-Fi. Nová voľba `--clock-err` a séria `clk` (viď nižšie).
- **2026-11-17** – **1000 áut.** Prekážky:
  - mapa 3×3 unesie ~600 áut,
  - 1 bunka LTE ≤ 320 UE,
  - pamäť NR: matice a parametre kanála 3GPP pre každú dvojicu; prvý beh zabil OOM pri 12 GB v 2.3 s,
  - čas ∝ dvojice.

  Riešenia (všetky voliteľné, bez nich bit po bite zhodné; overené `OVER` 30 áut aj mestský `REFU` 40 áut s `--qos-rat`):
  - `--cc-grid 5`: SUMO `--diag` 1000 áut, 23 km/h, 2 teleporty za 420 s, max státie 120 s,
  - `--enb 9 --enb-isd 400`,
  - mriežka budov: bit po bite, 47 → 44 s pri 40 autách,
  - `--nr-max-loss 120` + `--nr-cache-ttl 1` + voliteľne `--nr-fading 0`.

  Profil (perf) pri 280 a 1000 autách: NR `MultiModelSpectrumChannel::StartTx` 33-48 %, z toho fast fading
  (`CalcBeamformingGain`, sincos) ~15-27 %, 3GPP pathloss pre všetky dvojice ~17-21 %, malloc ~15-20 %, LTE ~7 %, 802.11p ~7 %.
  Heaptrack 400 áut: špička 1.43 GB, z toho ~0.9 GB parametre kanála NR.

  **1000 áut, 9 buniek, plná konfigurácia (CPM 100 %, coex, DCC, podpis, telefóny):**
  - s fadingom: ~11 min na 1 s simulácie, 12 GB, beh 6 s za 51 min dobehol (`xgregorm_logs/K1000_fading_6s.log`):
    191/191 handoverov OK, 1 RLF, 0 NRDROP; CBR 802.11p priemer 0.28, NR SL-CBR 0.46 (GE60 31 %), LTE PRB 0.25;
    PRR 0.686 je len rozbeh 2-6 s, nepoužiteľné,
  - bez fadingu: ~7 min na 1 s, 3.1 GB (`xgregorm_logs/K1000_bez_fadingu_casy.txt`).
  - Spustená séria `big1000` (Q a MIX, seed 1, 50 s, od 30 s).
  - Spustené overenie zjednodušení na 140 autách `scal140` (QS, MIXS = Q, MIX + `--nr-max-loss 120 --nr-cache-ttl 1 --nr-fading 0`, seedy 201-203; porovná sa s idea1).
- **2026-11-17** – **Ten istý scenár v OMNeT++ 6.0.3 + Veins 5.2** (`xgregorm_omnet/`, popis a tabuľky v jeho README).
  Nainštalované do `~/omnet` (bez Qtenv), spoločný SUMO 1.18.
  - `export_scenario.py`: kopíruje súbory SUMO, nastaví seed v `launchd.xml` a prepočíta RSU aj budovy do súradníc Veins.
  - `xgregorm_compare.py`: spustí ns-3 (`--tech 80211p --policy all --tls-radius -1 --range-stats 1`) aj Veins.
  - Výsledky:
    - kanál `authors`, 140 áut, 30 dBm: PRR <100 m 0.9957 / 0.9936; po 50 m zhoda do 1-3 b. až po 200 m, 200-250 m 0.34 / 0.67
      (ns-3 prah SNR 4 dB pre preambulu); počty očakávaných príjmov sa líšia < 0.3 %, Veins 20× rýchlejší,
    - **mestský kanál** (`XgV2vUrban` = TR 37.885 + budovy + NLOSv podľa výšky antén + korelované tienenie + Nakagami),
      140 áut, 23 dBm: PRR <100 m 0.936 / 0.929, po vzdialenostiach do 1 b. (auto-auto) a 2 b. (auto-RSU); 80 áut, seed 4: 0.940 / 0.930,
    - jadro kvality: odhad v autách ns-3 0.9941 (skutočnosť 0.9939), OMNeT++ 0.9948 (0.9941).

  NR/LTE vo Veins nie sú (Simu5G nenainštalované).
- **2026-11-17** – **Únik pamäte v GeoNet autorov** (viď Moje zmeny, `geonet.cc`). Nájdený pri 1000 autách: RSS rástlo
  ~0.56 GB na sekundu simulácie (Q, bez fadingu, 4 s 3.1 GB, 9 s 6.0 GB). Príčina sú zoznamy CBR v tabuľke polôh
  GeoNet, ktoré bez modulu DCC autorov nikto nemaže. Po oprave bit po bite zhodné (REFU, OVER). Pamäť všetkých
  doterajších behov (napr. 140 áut ~3.3 GB, 280 áut ~3.2 GB) bola z veľkej časti tento únik. Behy `big1000`
  reštartované s opravou: 45 s, od 25 s, aby dobehli za deň; staré čiastočné logy zmazané.
  Po oprave, 1000 áut s fadingom NR: 2 s 1.2 GB, 3 s 10.2 GB, 4 s 10.7 GB, krok 3→4 s 14.7 min pri 6 súbežných
  simuláciách (`xgregorm_logs/K1000_fading_po_oprave_casy.txt`). Bez fadingu (`big1000`, Q) 6 s 3.0 GB, ~8 min na 1 s pri 5 súbežných.
- **2026-11-17** – **Overenie zjednodušení pre 1000 áut na 140 autách (`scal140`)**, seedy 201-203, porovnané s idea1;
  `xgregorm_scal_eval.py`, výstup `xgregorm_logs/scal140/scal_eval.txt`. Prvá dávka bežala ešte so starým binárom
  (s únikom pamäte GeoNet), výsledky sú rovnaké (únik správanie nemení).

  | | PRR | CPM spolu | čas behu |
  |---|---|---|---|
  | Q / QS | 0.9077 / 0.9192 | 0.846 / 0.864 | 4186 / 2654 s |
  | MIX / MIXS | 0.8910 / 0.8991 | 0.857 / 0.866 | 4972 / 2580 s |

  Párovo, n = 3:
  - QS − Q PRR +0.0115 (−0.003, +0.026), MIXS − MIX +0.0082 (+0.003, +0.014),
  - Q − MIX +0.0167 (+0.007, +0.027), QS − MIXS +0.0200 (+0.006, +0.035),
  - **(QS − MIXS) − (Q − MIX) +0.0033 (−0.015, +0.022), p = 0.52**,
  - CPM: Q − MIX −0.011 (preukázané), QS − MIXS −0.002 (nepreukázané).

  **Záver:** zjednodušenia zvýšia absolútne PRR asi o 1 b. (NR bez fadingu je lepšie), poradie a veľkosť rozdielu politík pri PRR
  zachovajú. Pri CPM sa malá nevýhoda Q oproti MIX stratí.
- **2026-11-17** – **Citlivosť výberu podľa kvality na presnosť hodín (`clk`)**, 140 áut, mesto bez zón, základ ako idea1,
  seedy 201-205 (Q z idea1), `xgregorm_clk_eval.py`, výstup `xgregorm_logs/clk/clk_eval.txt`. QNODL = bez limitu 100 ms
  (`--lat-budget` veľký), QC10 / QC50 = `--clock-err 0.01 / 0.05`.

  | | PRR | CPM spolu | presuny na auto za min | neskoro 802.11p / NR | odhad kvality 802.11p / NR |
  |---|---|---|---|---|---|
  | Q | 0.9079 | 0.8431 | 1.77 | 0.08 / 3.84 % | 0.875 / 0.879 |
  | QNODL | 0.9058 | 0.8453 | 0.99 | 0 / 0 % | 0.872 / 0.909 |
  | QC10 | 0.9085 | 0.8503 | 1.42 | 2.0 / 3.2 % | 0.859 / 0.883 |
  | QC50 | 0.9051 | 0.8465 | 1.12 | 17.0 / 13.0 % | 0.725 / 0.787 |

  Párovo, n = 5, PRR: QNODL − Q −0.0021 (−0.0054, +0.0012), QC10 − Q +0.0006 (−0.0040, +0.0051), QC50 − Q −0.0028
  (−0.0074, +0.0018); CPM QC10 − Q +0.0072 (+0.0055, +0.0090), ostatné nepreukázané; presuny vo všetkých preukázateľne menej.
  **Záver:** presný čas nie je podmienkou výberu podľa kvality; chyba hodín zníži odhad kvality oboch sietí podobne.
- Pozn.: v sekcii „Tok komunikácie“ je prehľad, čo ide cez ns-3 (CAM, kábel RSU-RSU) a čo cez
  TraCI (RSU -> semafor, mapa).

## Čo robiť, keď si na pochybách

- Pred zmenou v kóde autorov sa spýtaj; je to vec, ktorú treba v práci popísať a nahlásiť do Issues.
- Nevymýšľaj si hodnoty metrík. Ak beh spadol, povedz to a ukáž chybovú hlášku.
- Dlhé behy (LTE, NR-V2X) spúšťaj najprv s menším `--vehicles` a `--sim-time`, kým sa overí, že bežia.
- LTE beh trvá rádovo 4 minúty pri 30 vozidlách a 120 s simulácie; nerušiť ho predčasne.
