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
| polomer zóny RSU (802.11p) | `areaSpeedAdvisorServer80211p.cc` | `geoArea.distA = 90;` |
| polomer zóny RSU (LTE) | `areaSpeedAdvisorServerLTE.cc` | `geoArea.distA = 300;` |
| limit v zóne | `areaSpeedAdvisorServer80211p.cc` (2x) | `roadworks.speedLimit.setData (25);` |
| rýchlosť mimo zóny | `areaSpeedAdvisorClient80211p.cc` | `75/3.6` |
| timeout bez DENM | `areaSpeedAdvisorClient80211p.cc` | `Seconds(1.5)` |
| station ID RSU | servery | `1000000 + číslo z poi_N` |
| `print_summary` v LTE | `v2i-areaSpeedAdvisor-lte.cc` | natvrdo `false`, nemá CLI prepínač |

Zmena ktorejkoľvek z nich je zásah do kódu autorov: najprv záloha do `xgregorm_kod_zaloha/`,
potom `./ns3 build`, a v práci to treba popísať.

## Interpretácia metrík

- **PRR a packet loss majú zmysel len pre 802.11p.** Metric Supervisor počíta, koľko staníc do
  baseline vzdialenosti správu prijalo. LTE doručuje adresne cez eNodeB, takže PRR tam vyjde ~0
  bez ohľadu na kvalitu siete. Pre LTE porovnávaj latenciu a počty doručených DENM/CAM.
- Typické hodnoty: 802.11p pri 10 vozidlách PRR ~0.997 a latencia ~0.2 ms; LTE latencia ~65 ms.
- PRR sa počíta v 3-sekundovom okne, takže `--sim-time` pod 60 s dáva nepoužiteľnú vzorku.
- Nízke PRR pri 802.11p = zahltenie kanála (veľa vozidiel a RSU na malej ploche), nie chyba konfigurácie.
- `stop tot[s]` je súčet všetkých státí vozidla, `stop max[s]` najdlhšie jedno neprerušené státie.
- Typy správ: CAM je pravidelný tlkot srdca (1-10 Hz), DENM je udalostná správa do geografickej
  oblasti, CPM hlási objekty zo senzorov. PRR ovplyvňujú hlavne CAM, funkčnosť aplikácie hlavne DENM.

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

## Čo robiť, keď si na pochybách

- Pred zmenou v kóde autorov sa spýtaj; je to vec, ktorú treba v práci popísať a nahlásiť do Issues.
- Nevymýšľaj si hodnoty metrík. Ak beh spadol, povedz to a ukáž chybovú hlášku.
- Dlhé behy (LTE, NR-V2X) spúšťaj najprv s menším `--vehicles` a `--sim-time`, kým sa overí, že bežia.
- LTE beh trvá rádovo 4 minúty pri 30 vozidlách a 120 s simulácie; nerušiť ho predčasne.
