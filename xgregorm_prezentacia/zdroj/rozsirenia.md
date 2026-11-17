# Pripomienky z obhajoby a rozšírenia

Táto časť odpovedá na pripomienky k obhajobe a opisuje, čo som po nej doplnil: simuláciu s 1000 autami a spustenie toho istého pokusu v druhom simulátore, OMNeT++.

## 1. Ako auto vie, že správa prišla do 100 ms

**Pripomienka:** pri meraní kvality sa počíta len správa, ktorá prišla do 100 ms. Na to musia mať odosielateľ aj príjemca rovnaký čas. Kde ho v realite vezmú?

**Ako to funguje:**

1. **Každá správa o polohe nesie čas svojho vzniku.** Norma ETSI pre CAM (EN 302 637-2) ho predpisuje v milisekundách (pole `generationDeltaTime`). Je to medzinárodný atómový čas, ktorý stanica berie zo satelitnej navigácie. Rovnaký čas je aj v hlavičke GeoNetworking a v podpise správy.
2. **Príjemca odčíta čas vzniku od svojich hodín.** Výsledok je oneskorenie. Ak je do 100 ms, správa sa ráta ako doručená načas.
3. **Hodiny majú obe autá z GPS (GNSS).** Auto potrebuje satelitnú navigáciu aj tak, lebo bez polohy nemá čo poslať v správe o polohe. Prijímač GNSS dáva čas presný na zlomok mikrosekundy, teda asi 100 000-krát presnejšie, než treba na hranicu 100 ms. Priame 5G (NR-V2X) sa podľa času z GNSS dokonca synchronizuje, inak by sa autá netrafili do svojich časových políčok.
4. **Bez signálu GNSS** (tunel, garáž) hodiny chvíľu bežia samé. Bežný kryštál sa odchýli o milióntiny, teda asi o 1 ms za 15 minút. Neskôr sa dajú dorovnať aj cez mobilnú sieť, presnosť je potom asi 10 ms.

**V simulácii** majú všetky autá presne rovnaký čas. Preto som pridal voľbu `--clock-err`: každé auto dostane hodiny posunuté o náhodnú odchýlku. Behy so 140 autami, 3 náhody, porovnané s presnými hodinami:

| hodiny áut | doručené správy o polohe | rozdiel oproti presným hodinám (95 % interval) | presuny na auto za minútu |
|---|---|---|---|
| presné (ako GNSS) | 90,77 % | – | 1,72 |
| bez limitu 100 ms (ráta sa každá doručená správa) | 90,70 % | −0,07 bodu (−0,68 až +0,54) | 1,05 |
| chyba každého auta ±10 ms | 90,91 % | +0,14 bodu (−1,06 až +1,35) | 1,36 |
| chyba každého auta ±50 ms | 90,67 % | −0,10 bodu (−0,93 až +0,73) | 1,19 |

**Čo z toho vyplýva:**

- **Výsledok sa nezmenil.** Rozdiely sú do 0,14 bodu a všetky intervaly obsahujú nulu. Senzorové správy vyšli pri nepresných hodinách dokonca o 0,4 až 0,8 bodu lepšie a autá prepínali menej často.
- **Prečo to tak je:** s presnými hodinami príde po 100 ms len 0,09 % prijatých správ cez 802.11p a 3,9 % cez 5G, ostatné prídu skôr alebo vôbec. Limit 100 ms teda rozhoduje len o malom zlomku správ.
- **Pri chybe 50 ms** autá omylom označia za oneskorených asi 16 % správ cez 802.11p a 15 % cez 5G. Odhad kvality klesne z 87 na 73 % a z 88 na 77 %. Klesne však u oboch sietí podobne a auto porovnáva siete medzi sebou, takže vyberie rovnako.
- **Pre prax:** výber podľa kvality potrebuje čas najviac na desiatky milisekúnd presne a ten auto má z GNSS s obrovskou rezervou. Keby hodiny nemalo, dá sa limit 100 ms vypustiť a merať len straty podľa poradových čísel, výsledok bude rovnaký.

Obmedzenia: 3 náhody, mesto bez zón. Chyba hodín je stála pre každé auto; postupne narastajúcu odchýlku hodín som nesimuloval.

## 2. Prečo píšem 802.11p, a nie Wi-Fi

802.11p sa niekedy volá „Wi-Fi pre autá“, lebo patrí do rovnakej rodiny noriem IEEE 802.11. Je to však samostatná technológia:

- **nemá prístupový bod (router) ani prihlasovanie:** auto vysiela priamo všetkým okolo, bez nadväzovania spojenia (režim „mimo siete“, OCB),
- **beží vo vyhradenom pásme pre dopravu:** 5,9 GHz, s polovičnou šírkou kanála (10 MHz namiesto 20 MHz), aby lepšie znášala odrazy pri rýchlej jazde,
- **nad ňou je iný zásobník:** v Európe ITS-G5 (GeoNetworking, správy CAM, CPM, DENM, podpisy), nie internetový zásobník Wi-Fi.

Preto v prezentácii aj vo Worde odteraz píšem 802.11p (ITS-G5).

## 3. Simulácia s 1000 autami

**Čo bránilo 1000 autám:**

- Mapa: centrum s 9 križovatkami unesie asi 600 áut, pri 1000 sa premávka zasekne už v samotnom SUMO.
- Mobilná sieť: jedna bunka LTE v ns-3 zvládne najviac asi 320 zariadení.
- Pamäť: model rádiového kanála priameho 5G si pre každú dvojicu zariadení pamätá parametre kanála. Pri 1000 autách je to asi pol milióna dvojíc a beh si vypýtal viac ako 12 GB.
- Čas: každé vysielanie sa počíta pre každého príjemcu a pri tisícke áut je to asi tisíc krát tisíc dvojíc pri každom vysielaní.

**Čo som zmenil (všetko sú nové voľby, bez nich simulácia dáva bit po bite to isté ako doteraz):**

| čo | voľba | účinok |
|---|---|---|
| väčšie centrum: 5 × 5 križovatiek namiesto 3 × 3 | `--cc-grid 5` | 1000 áut jazdí priemerne 23 km/h, len 2 zaseknutia za 7 minút |
| 9 buniek LTE namiesto 1 | `--enb 9 --enb-isd 400` | asi 110 áut na bunku, 191 odovzdaní spojenia medzi bunkami, všetky úspešné |
| rýchlejšie hľadanie budov medzi dvoma autami (mriežka) | vždy zapnuté | rovnaký výsledok, menej výpočtov |
| signál slabší než 120 dB útlmu sa pri priamom 5G nepočíta | `--nr-max-loss 120` | taký signál je 5 dB pod šumom a nedá sa prijať |
| zabúdanie kanálov dvojíc, ktoré sa 1 s nepočuli | `--nr-cache-ttl 1` | pamäť drží len dvojice v dosahu |
| priame 5G bez rýchlych únikov | `--nr-fading 0` | asi 3× menej pamäte, asi o tretinu rýchlejšie; je to zjednodušenie modelu |

**Koľko to trvá (1000 áut, 9 buniek LTE, všetky správy ako v práci):**

| nastavenie | 1 sekunda simulácie | pamäť |
|---|---|---|
| s rýchlymi únikmi 5G | asi 10–15 minút | asi 11 GB |
| bez rýchlych únikov 5G | asi 7–8 minút | asi 3 GB |

Pri hľadaní, prečo pamäť stále rastie, som našiel chybu v kóde autorov VaN3Twin. Časť GeoNetworking si pri každom prijatom pakete ukladala hodnotu obsadenosti od odosielateľa, ale mazala ju len vtedy, keď je zapnutý modul DCC autorov. Ten nepoužívam, takže zoznamy rástli donekonečna. Pri 1000 autách to bolo asi 0,5 GB za sekundu simulácie, pri 140 autách asi 3 GB za beh. Opravil som to tak, že sa hodnoty ukladajú len s modulom DCC autorov. Výsledky sú bit po bite rovnaké, len simulácia zaberie menej pamäte.

Pre porovnanie: 140 áut potrebuje asi pol minúty na sekundu simulácie. Beh s 1000 autami a 50 sekundami teda trvá asi 6 hodín, preto sa dá spúšťať len cez noc a niekoľko naraz.

**Neskreslia zjednodušenia výsledok?** Overil som to na pôvodnom meste so 140 autami: výber podľa kvality aj náhodné rozdelenie 50/50, každé s pôvodným modelom aj so zjednodušeniami (`--nr-max-loss 120 --nr-cache-ttl 1 --nr-fading 0`), 3 náhody.

| | pôvodný model | so zjednodušeniami |
|---|---|---|
| výber podľa kvality | 90,77 % | 91,92 % |
| náhodne 50/50 | 89,10 % | 89,91 % |
| **výhoda výberu podľa kvality** | **+1,67 bodu** (+0,69 až +2,65) | **+2,00 bodu** (+0,56 až +3,45) |
| čas behu (výber podľa kvality) | 70 minút | 44 minút |

Zjednodušenia zvýšia doručenie asi o 1 bod, lebo priame 5G bez rýchlych únikov vychádza o niečo lepšie. **Rozdiel medzi spôsobmi výberu však ostane rovnaký**: rozdiel rozdielov je +0,33 bodu a je v rámci náhody. Pri porovnávaní spôsobov sa teda dajú použiť, absolútne čísla treba brať s rezervou asi 1 bodu.

@@BIG@@

## 4. Ten istý pokus aj v OMNeT++

**Čo to znamená.** Simulácia V2X má viac vrstiev a každá musí byť v oboch simulátoroch rovnaká, aby sa dali porovnať čísla:

1. **Mapa, autá a cestné jednotky:** spoločné súbory SUMO z môjho generátora.
2. **Pohyb áut:** oba simulátory pýtajú polohy od toho istého programu SUMO, s rovnakým krokom (0,1 s) aj seedom.
3. **Rádio:** rovnaký výkon, citlivosť, šum, rýchlosť a model útlmu.
4. **Správy a ich pravidlá:** CAM podľa normy ETSI, rovnaká veľkosť s podpisom.
5. **Meranie:** rovnaká definícia doručenia a rovnaký formát výstupu.
6. **Algoritmus výberu siete:** ten istý kód.

**Čo som spravil.** Nainštaloval som OMNeT++ 6.0.3 a Veins 5.2 (modul OMNeT++ pre 802.11p a SUMO) a napísal:

- aplikáciu CAM podľa normy ETSI s meraním v rovnakom formáte ako ns-3,
- skript, ktorý zo scenára pre ns-3 vyrobí ten istý scenár pre OMNeT++,
- spúšťač, ktorý jedným príkazom pustí oba simulátory a vypíše výsledky vedľa seba.

Pravidlo výberu podľa kvality som vyčlenil do samostatného súboru bez závislosti od simulátora (`xgregorm-qos-core.h`). Používa ho ns-3, kde dáva bit po bite to isté ako predtým, aj OMNeT++.

**Výsledok porovnania, mestský kanál s budovami ako v práci** (140 áut, len 802.11p, 23 dBm, rovnaký scenár). Do OMNeT++ som doprogramoval ten istý model rádiového kanála 3GPP s budovami, aký používa ns-3:

| vzdialenosť | ns-3 | OMNeT++ |
|---|---|---|
| 0–50 m | 99,0 % | 98,6 % |
| 50–100 m | 86,3 % | 85,3 % |
| 100–150 m | 56,9 % | 56,2 % |
| 150–200 m | 30,8 % | 30,1 % |
| 200–250 m | 19,6 % | 19,2 % |
| 300–350 m | 8,0 % | 7,8 % |

S realistickým kanálom sa oba simulátory zhodujú do 1 bodu. Do 100 m vyšlo 93,6 % v ns-3 a 92,9 % v OMNeT++.

**Výsledok porovnania, jednoduchý kanál bez budov** (140 áut, len 802.11p, 30 dBm, rovnaký scenár):

| vzdialenosť | ns-3 | OMNeT++ |
|---|---|---|
| 0–50 m | 99,8 % | 99,6 % |
| 50–100 m | 99,3 % | 99,0 % |
| 100–150 m | 98,7 % | 97,6 % |
| 150–200 m | 95,6 % | 92,8 % |
| 200–250 m | 34 % | 67 % |
| ďalej | 0 | 0 |

- Pohyb áut je v oboch rovnaký: počet očakávaných doručení sa líši o menej ako 0,3 %.
- Do 100 m sa doručenie líši len o desatiny percenta.
- Rozdiel je len na okraji dosahu. Každý simulátor tam inak modeluje, kedy rádio ešte zachytí slabý rámec. ns-3 navyše vyžaduje, aby bol signál aspoň 4 dB nad šumom.
- OMNeT++ bol 20× rýchlejší (7 s oproti 167 s), lebo nemá celý zásobník ETSI, LTE ani 5G.
- Odhad kvality 802.11p, ktorý autá počítajú spoločným jadrom algoritmu, vyšiel v ns-3 99,41 % pri skutočnosti 99,39 % a v OMNeT++ 99,48 % pri skutočnosti 99,41 %. Ten istý kód teda v oboch simulátoroch meria rovnako.

**Čo v OMNeT++ ešte chýba:** priame 5G a mobilná sieť (dali by sa doplniť modulom Simu5G), senzorové správy a pravidlá proti preplneniu (DCC). Preto sa v OMNeT++ zatiaľ dá overiť len časť s 802.11p a samotný výber medzi sieťami ešte nie. Celý postup inštalácie a spustenia je v `xgregorm_omnet/README.md`.
