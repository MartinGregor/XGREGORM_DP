# Plán: dôkaz prínosu prediktívneho výberu RAT (xgregorm)

Zapísané 2026-11-02. Doplňuje `xgregorm_STAV_PRACE.md` a Denník v `CLAUDE.md`.

## 1. Tvrdenie, ktoré má práca obhájiť

> V scenári X prediktívny výber RAT oproti výberu podľa aktuálneho merania zlepší parametre Y o Δ (95 % interval
> spoľahlivosti) za cenu Z, a v scenári W prínos nemá.

Výsledok nie je vopred daný. Ak predikcia nepomôže, záver znie „nepomáha, lebo…“, a scenár sa nesmie ladiť, kým nevyhrá.

## 2. Hypotézy a pravidlá vyhodnotenia (stanovené pred behmi)

**H1 (presnosť):** predikcia spoľahlivosti na horizonte 3 s má menšiu chybu než „zostane ako teraz“ (MAE, párovo po seedoch).
**H2 (rozhodnutie):** P mení rozhodnutia v správnom čase: prepne pred zhoršením, nie až po ňom (čas prepnutia voči zmene kvality).
**H3 (prínos, hlavná):** v scenároch s meniacou sa kvalitou má P vyššie skutočné PRR auto-auto do 100 m než E.
**H4 (kontrola):** v statickom scenári je rozdiel P − E približne nulový. Ak nie, hľadať chybu.
**H5 (cena):** réžia mapy kvality (LTE) a počet prepnutí sú prijateľné, teda nezhoršia LTE PRB o viac než prínos ospravedlňuje.

**Hlavná metrika:** skutočné PRR auto-auto do 100 m. **Vedľajšie:** uplink do RSU, oneskorenie p95, PIR p95, podiel sekúnd s PRR < 0.9,
PRR v okne ±5 s okolo zmeny kvality. **Cena:** prepnutia na auto za minútu, LTE PRB, bajty réžie mapy, záťaž NR a 802.11p.

**Štatistika:** rovnaké seedy pre všetky politiky (párové porovnanie). Rozdiel P − E s 95 % intervalom (t alebo bootstrap), Holmova korekcia
pre vedľajšie metriky, minimálne 5 seedov, cieľ 10. **Ladiace seedy (101-103) sa nepoužijú vo finálnom vyhodnotení (1-10).**

## 3. Porovnávané politiky

| Kód | Politika | Účel |
|---|---|---|
| W | len 802.11p | referencia |
| N | len NR-V2X | referencia |
| A | adaptívne bez merania kvality | doterajší základ |
| E | podľa aktuálne nameranej kvality | reaktívny výber, hlavný súper |
| P | predikcia z mapy kvality online, s réžiou a oneskorením | môj návrh |
| M | model naučený offline na iných seedoch | „predikčný model“ v plnom zmysle |
| O | orákulum (mapa z toho istého behu, najlepšia možná znalosť) | horná hranica prínosu predikcie |

## 4. Scenáre

| Kód | Scenár | Čo sa mení v čase a priestore | Očakávanie |
|---|---|---|---|
| S0 | ustálený | nič | P ≈ E (kontrola H4) |
| S1 | prepad premávky | vlna áut cez centrum | prínos možný |
| S2 | hotspot chodcov | dav s telefónmi (námestie, koniec podujatia) zaťaží bunku a PC5 lokálne | prínos možný, mapa sa to môže naučiť |
| S3 | lokálny výpadok | oblasť s výpadkom 802.11p alebo NR (rušenie, stavba) na známom mieste | najväčší očakávaný prínos |
| S4 | zmes výbavy | W / N / WN / bez V2X | kontrola nekompatibility |

## 5. Zistené problémy doterajších výsledkov

| # | Problém | Dopad | Stav |
|---|---|---|---|
| 1 | **Broadcast 802.11p išiel 3 Mbit/s, norma má na G5-CCH predvolených 6 Mbit/s** (EN 302 663 V1.2.1 tab. 2). `--datarate` sa na broadcast nepoužije. | Dvojnásobný čas vysielania, CBR, a DCC so Toff nad tabuľkou; pravdepodobná príčina SPATEM 0.156. Doterajšie výsledky skreslené v neprospech 802.11p. | `--wifi-bcast-rate`, kalibrácia beží (`kal.sh`) |
| 2 | NR má 40 MHz, 802.11p 10 MHz | Zvýhodňuje NR, autá skončia na NR a nie je o čom rozhodovať | kalibrácia: NR 20 MHz na 5885 MHz vedľa 802.11p (návrh 5GAA) |
| 3 | Mapa kvality je zdieľaná pamäť: bez réžie, okamžite | Prínos predikcie je nadhodnotený, chýba „cena“ | treba doplniť |
| 4 | NR mode 2 bez sensingu (autori), s ním beh nedobehne | Rozpor s 3GPP TS 38.214, viac kolízií NR | preveriť na novom PC, inak uviesť ako obmedzenie |
| 5 | MAC 802.11p bez EDCA (`NqosWaveMacHelper`) | Bez priorít AC; EN 302 663 EDCA predpokladá | preveriť `QosWaveMacHelper` |
| 6 | Všetky správy vrátane CPM na jednom kanáli | CPM 100 % áut je záťažový extrém | uviesť ako predpoklad, príp. CPM < 100 % |
| 7 | Scenár je ustálený, po 30. s 0 presunov, meranie len 30 s, mapa má τ = 30 s | P nemá o čom rozhodovať, P = E je očakávané | scenáre S1-S3, dlhšie behy |
| 8 | Každý výsledok z 1 behu | Rozdiely nie sú preukázateľné | série so seedmi |
| 9 | Chýba chyba GNSS | Poloha v CAM je presná | doplniť |
| 10 | Chodci nie sú | Požiadavka používateľa | etapa 3 |

**Oprava pôvodného podozrenia:** škálovanie Toff úmerne Ton nie je svojvoľné. TS 102 687 Annex B (adaptívny prístup) má odstup úmerný Ton (Ton/δ, 25 ms až 1 s)
a pozičný dokument C2C-CC TR 2049 hovorí, že dlhšie správy sa smú posielať s rýchlosťou znížené úmerne R·Tavg/T (Tavg = 0.5 ms = 375 B pri 6 Mbit/s). Tabuľky Annexu A sú informatívne.

## 5a. Audit mechaník (2026-11-02, na novom počítači)

| Mechanika | Zistenie | Rozhodnutie |
|---|---|---|
| Záloha CAM cez všetky technológie, keď RSU nepotvrdí (`A`, a preto aj `E`, `P`) | V behu LE ide **60 % CAM všetkými tromi technológiami**, 67 % aj cez LTE. Toto je zahlcovanie siete vlastným mechanizmom, nie prepínanie. | Nová **čistá politika `--clean 1`**: jedna priama technológia na CAM. Doterajšie `A` nie je férový základ „bez prepínania“. |
| Beacon RSU so zoznamom počutých CAM | Neštandardná správa (44.7/s). V čistej politike netreba. | Vypnutý pri `--clean 1`. LTE sa pridá podľa mapy: príjazd ďalej než `--cell-dist` 150 m od RSU. |
| CPM pri zálohe | `mask = WIFI`, keď je 802.11p v ceste: auto s hlavnou NR a kópiou cez 802.11p posiela CPM cez 802.11p. Chyba v `A/E/P`. | Pri `--clean` nenastane. Staré režimy ponechané kvôli porovnateľnosti, v práci uviesť. |
| LTE PRB ako ukazovateľ záťaže | Plánovač autorov (`pf-ff-mac-scheduler.cc`) delí celé pásmo medzi aktívnych, `rbPerFlow = šírka / počet`. Malý paket dostane veľa PRB, takže PRB ≠ potreba. Vysvetľuje PRB ~0.5 pri ~2 Mbit/s (bod STAV 4.3). | PRB neuvádzať ako kapacitu; pravidlo záťaže pre LTE je preto nedôveryhodné. Cena LTE sa uvádza aj v bajtoch. |
| `--load-rat`, `--xg-relax`, `--tls-coord`, `--enb > 1`, `--security 2`, `--channel 3gpp` | Merania ukázali nulový prínos alebo artefakty (Denník). Vo finálnej konfigurácii sa nepoužijú. | Ostávajú vypnuté, neodstraňujem ich, aby staré záznamy ostali overiteľné. |
| Poradové číslo v poli Reserved GeoNet | Rozšírenie normy, ale veľkosť správy sa nemení a je to jediný spôsob, ako z príjmu určiť straty. | Ponechať, uviesť ako odchýlku. |
| Skúšky druhej technológie | Bez kópií by auto druhú technológiu nevedelo zmerať, preto `--probe-hz`. Pri 1 Hz je to takmer polovica CAM stojaceho auta. | Cena výberu, musí sa merať a zvoliť čo najnižšia. |
| Mapa kvality | Bola okamžitá zdieľaná pamäť. | `--rem-period`: hlásenia sú skutočné pakety cez LTE (113+ B), do mapy sa dostanú s oneskorením. Downlink mapy sa neposiela, jeho bajty treba odhadnúť zvlášť. |

**Chodci (`--ped`, zatiaľ len LTE):** telefón = LTE UE s uplinkom a VAM. Chodia po chodníkoch v ns-3, v SUMO nie sú a autá na nich nereagujú, aby zostali zachované párové porovnania premávky.
**Možné vylepšenie do budúcna:** osoby v SUMO s prechodmi (netreba meniť semafory a pozor na posun indexov pruhov), VAM aj cez PC5 / 802.11p pre časť telefónov. Pretože sú teraz len na LTE, výber medzi 802.11p a NR ovplyvňujú len nepriamo.

## 6. Etapy

1. **Kalibrácia realizmu** (beží): rýchlosť broadcastu a plán pásma. Výstup: rozhodnutie o základnej konfigurácii. Doterajšie série R a M sa nedopúšťajú v starej konfigurácii.
2. **Audit noriem:** tabuľka každého modelu: norma, hodnota, odchýlka (body 4-6, 9 z časti 5).
3. **Chodci:** chodníky a prechody v `city-center`, osoby v SUMO, telefóny ako LTE UE (telefónna prevádzka + VAM), voliteľne PC5. Overiť, že trafficManager zvládne semafory s prechodmi a budovy nestoja na chodníkoch. Limit bunky ~320 UE platí aj pre chodcov.
4. **Poctivá predikcia:** réžia a oneskorenie mapy (reálne správy cez LTE), export dátovej sady z behov, offline model (lineárny, stromy) overený na iných seedoch, politiky M a O.
5. **Scenáre S1-S3:** meniaca sa kvalita, dlhšie merané úseky.
6. **Ladenie na seedoch 101-103:** zmrazenie parametrov.
7. **Finálna séria seedy 1-10**, skript na štatistiku a grafy, kapitola výsledkov.

## 7. Výpočtová náročnosť (odhad)

140 áut × 60 s ≈ 1 h na beh. 7 politík × 4 scenáre × 10 seedov = 280 behov. Pri dlhších behoch (~2 h) a 8 behoch naraz to je ~70 h, preto M a O len na vybraných scenároch a 5 seedov tam, kde je rozptyl malý. Spresní sa po kalibrácii.

## 8. Fronta ďalších spôsobov výberu RAT (požiadavka používateľa 2026-11-02)

Všetky budú porovnané s rovnakými základmi (čisté 802.11p, čisté NR, 50/50, posielanie všade), rovnakými seedmi a metrikami. Stav sa dopĺňa.

| # | Mechanizmus | Princíp | Stav |
|---|---|---|---|
| 1 | DCC po správach | Správa, ktorá by čakala na 802.11p dlhšie než rozpočet, ide cez NR | vo fronte |
| 2 | Výbava susedov | Technológia podľa toho, kam dosiahne viac susedov (zistené z prijatých CAM) | vo fronte |
| 3 | Výbava RSU na trase | Zo statickej mapy vie auto technológiu ďalšej RSU a prepne vopred | vo fronte |
| 4 | Selektívna redundancia | Kópia len pri slepej križovatke alebo konfliktnom aute | vo fronte |
| 5 | Geometria LOS/NLOS | Pred slepou križovatkou technológia odolnejšia za rohom | vo fronte |
| 6 | Typ správy | CAM, CPM a ďalšie každá technológiou, ktorá jej najviac vyhovuje | vo fronte |

Ďalšie nápady, zatiaľ nezaradené: koordinácia cez RSU, minimalizácia veku informácie, kontextový bandit, požadovaný dosah, cesta V2N.

### Porovnanie s literatúrou (rešerš 2026-11-02, webové vyhľadávanie, nie úplný systematický prehľad)

Presný ekvivalent nenájdený, ale príbuzné práce existujú a treba ich v práci citovať a vymedziť sa voči nim. Pred odovzdaním overiť so školiteľom a v IEEE Xplore / Scopus.

| Nápad | Najbližšia práca | Rozdiel |
|---|---|---|
| Predikcia z mapy | [Reliable multi-RAT connectivity in urban V2X](https://www.sciencedirect.com/science/article/abs/pii/S1570870525002550): mriežka a výber pred vstupom do bunky (make-before-break), experiment | Blízke! Rozdiel: ns-3 + SUMO, spoločné pásmo a DCC, cena hlásení, párové porovnanie s reaktívnym výberom |
| 1 DCC po správach | [Hybrid ITS-G5 / PC5 / 5G](https://www.researchgate.net/publication/379280026_Optimizing_Hybrid_V2X_Communication_An_Intelligent_Technology_Selection_Algorithm_Using_5G_C-V2X_PC5_and_DSRC): PC5 pri zahltení ITS-G5; [QoS-aware RAT](https://arxiv.org/pdf/2501.12304); [DRL](https://arxiv.org/pdf/2407.00828) | Rozhodnutie po jednotlivých správach podľa brány DCC som v nájdených prácach nenašiel |
| 2 Výbava susedov | [Augmenting V2X RSU](https://arxiv.org/pdf/2305.14809), [patent dual mode](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/11706831) | Riešia RSU / preklad, nie výber v aute podľa susedov |
| 3 Výbava RSU na trase | [Radio-map výber](https://patents.google.com/patent/US11751077) (mapa QoS) | Priamo nenájdené |
| 4 Selektívna kópia | [Reliable multi-RAT](https://www.sciencedirect.com/science/article/abs/pii/S1570870525002550) rieši spojitosť | Podľa miesta na križovatke nenájdené |
| 5 LOS/NLOS | [LoS-map](https://arxiv.org/pdf/2111.07804): výber relé, nie RAT | RAT podľa mapy nenájdené |
| 6 Typ správy | [Patent: RAT podľa kategórie aplikácie](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/11671907) | Patentovaná myšlienka, prínos treba uviesť ako meranie, nie novinku |

## 8a. Horná hranica prínosu predikcie (stanovené pred behmi dev5, 2026-11-03)

Referencie `O0`, `O3`, `O8` poznajú zóny presne, s výhľadom 0, 3 a 8 s, a nepotrebujú skúšky. Nie sú to realizovateľné politiky.

- `O3 − O0` a `O8 − O0`: najviac, čo môže priniesť pozeranie dopredu. Ak interval obsahuje 0 alebo je rozdiel pod 0.01 PRR, predikcia v tomto scenári nemá čo získať a dôvod je preukázaný, nielen predpokladaný.
- `O0 − Q`: hodnota okamžitej znalosti stavu oproti meraniu, vrátane ceny skúšok.
- Polomer 50 m je citlivostná skúška s kratším prejazdom. Uvedie sa vždy spolu s polomerom 150 m, bez ohľadu na výsledok.

## 9. Fronta (poradie podľa používateľa, 2026-11-02)

1. Ladiaca séria `dev.sh`. 2. Vyhodnotenie. 3. Prepínanie podľa preťaženia a kombinácia 1 + 4. 4. Naučený model. 5. Chýbajúce metriky. 6. Scenár s meniacimi sa podmienkami. 7. Finálna séria, seedy 1-10. 8. Záverečné porovnanie.
Potom, **na konci a v tomto poradí, samostatné overenie nápadov: 1, 2, 3, 4, 5** (vlastné seedy a parametre). Nápad 6 ostáva len v ladiacej sérii.

**Iné štýly predikcie, na pokyn používateľa z 2026-11-03 presunuté pred finálnu sériu:** poradie je chýbajúce metriky, scenár s meniacimi sa podmienkami, tieto úlohy, až potom finálna séria, ktorá obsiahne aj prípadného víťaza. Bod 5 tak odpadá.

**Záväzné pravidlo pre všetky algoritmy:** algoritmus beží v aute a rozhoduje o RAT, cieľom je zlepšiť sieťový parameter (doručenie CAM a CPM, latencia, vek informácie, zaťaženie kanála a bunky). Vstupom môže byť len to, čo by auto malo aj v realite:

- obsah správ, ktoré naozaj prijalo cez 802.11p, NR alebo LTE: CAM, CPM, SPATEM, MAPEM, DENM. Stratená správa znamená, že informáciu nemá;
- vlastné merania: CBR, SL-CBR, doručenie podľa poradových čísel;
- vlastná poloha, rýchlosť, trasa z navigácie a statická mapa;
- čo oznámi sieť: vyťaženie bunky, hlásenia a mapa kvality, ktoré idú cez LTE a počítajú sa do záťaže.

Zakázané sú polohy ostatných áut priamo zo SUMO, poloha zón a čokoľvek iné, čo auto nemôže vedieť. Pravda zo simulátora slúži len na metriky a na výslovne označené referencie `O*`. Nové vstupy sa čítajú z prijatých správ. Dnešné vstupy tomu zodpovedajú s dvomi zjednodušeniami, ktoré sa v práci uvedú: poloha počutého suseda sa berie zo vzorky SUMO v čase príjmu namiesto dekódovania z CAM, a značka technológie a poradové číslo v CAM nie sú štandardné pole.

1. **Výber algoritmu na záznamoch V2X:** gradient boosting, malá neurónová sieť (MLP), rekurentná sieť (GRU) nad históriou auta, Kalmanov filter. Vyhodnotenie zo záznamov simulácie bez nového behu, tým sa odfiltrujú slabé kandidáty. Porovnanie s mapou a logistickou regresiou. Test oddelene podľa seedu aj polohy zón, súradnice nie sú vstupom.
2. **Vstupy z prijatých správ:** čas do zmeny signálu zo SPATEM, geometria z MAPEM, polohy, rýchlosti a smer susedov z CAM, objekty z CPM, vlastná trasa, odhad počtu áut v okolí o H sekúnd. Testuje sa aj na scenári bez zón, kde preťaženie vzniká zo samotnej premávky pri semafore.
3. **Učenie za jazdy:** kontextový bandit, auto sa učí z vlastného doručenia. Obchádza krok predikcia → rozhodnutie.
4. **Simulácia siete:** do ns-3 idú len algoritmy, ktoré na záznamoch prekonajú mapu. Úspech je zlepšenie PRR oproti výberu podľa kvality, 95 % interval bez nuly, pri nehoršom CPM a LTE; prah aspoň 0.01 PRR. Neúspešné varianty sa uvedú tiež.
5. Víťaz ide do finálnej série so seedmi 1-10. Výkon a zložitosť modelu v aute sa uvedú pri výsledku.

**Séria `dev6` (2026-11-03, stanovené pred behmi; seedy 101-103, 120 s, od 30 s):** A = zóny ako dev4 (RSU 0 W, 2 N, 5 W, 8 N), B = iné miesta (1 W, 3 N, 6 W, 7 N), C = meniace sa v čase (do 60 s ako A, 60-90 s tie isté miesta s vymenenými technológiami, 90-120 s ako B; merajú sa zmeny v 60. a 90. s), T = bez zón. Politiky: Q kvalita, B bandit, PM mapa s hláseniami, MIX 50/50, O0 a O3 orákulum. Dáta na výber algoritmu: testy naprieč seedmi aj naprieč rozložením zón.
**Odchýlka od časti 4:** S2 (hotspot chodcov) sa neposudzuje, chodci sú len na LTE a výber medzi 802.11p a NR priamo neovplyvňujú. S1 (vlna áut) zastupuje T, kde záťaž vzniká pri semaforoch; samostatná vlna áut sa zatiaľ nerobí.

Horná hranica z časti 8a platí len pre zóny; pre preťaženie z premávky ju treba určiť zvlášť.

**Séria `dev7` (2026-11-03, stanovené pred behmi; seedy 101-103, 120 s, od 30 s).** Výber na záznamoch (Denník 2026-11-03) dal jediného kandidáta: **GS = stromy (GBM) so vstupmi z prijatých správ, bez mapy kvality**, teda bez hlásení cez LTE. Kalman, MLP, GRU, logit a GBM bez správ do siete nejdú.
- Model pre test na seede s je učený len na zvyšných dvoch seedoch (`xgregorm_logs/dev7_models/gs_s<seed>.txt`), na záznamoch AQ, BQ, CQ, TQ z dev6. Poradie vstupov overuje C++ pri načítaní. Rozhodnutie: predikcia na 3 s nahradí aktuálne meranie, ďalej rovnaké pravidlo ako Q (rozdiel aspoň 0.05, pravdepodobnosť zisk/0.4, skúšky 0.5 Hz).
- Porovnania: AGS, BGS, CGS, TGS s Q z dev6 a **nový scenár D, ktorý model nevidel** (do 50 s RSU 0N 3W 6N 8W, 50-85 s 1N 2N 5W 7W, od 85 s 0W 3N 4W 7N; zmeny v 50. a 85. s), kde sú DGS, DQ, DMIX, DO3. Rozhodujúci je D: C bolo súčasťou učenia, hoci z iných seedov, takže výsledok CGS sám osebe nestačí.
- Úspech podľa časti 9: PRR voči Q aspoň +0.01, 95 % interval bez nuly, nehoršie CPM a LTE. Model sa po pohľade na D nesmie preučiť ani meniť, ak sa to stane, je to nový model a treba nový nevidený scenár.
- Známe obmedzenie: zóna znižuje príjem všetkých správ danej technológie v aute, takže aj SPATEM. Vstup `spatr` je preto v zónach veľmi priamy ukazovateľ. V T taký zásah nie je a tam výsledok ukáže, koľko model pomôže bez neho.
- Bandit: AB2 a CB2 = bandit s plnými skúškami, oddeľujú, či za horšie PRR z dev6 môže úbytok skúšok, alebo samotné pravidlo.

**Finálna séria, referencie `final1` (2026-11-03, stanovené pred dev7, nezávislé od jej výsledku; seedy 1-10, 120 s, od 30 s):** scenáre A, C, D, T × politiky Q, MIX, PM, spolu 120 behov, odhad 27 h. `xgregorm_logs/xgregorm_chain.sh` ich spustí sám po dobehnutí dev7, aby výpočet nestál, ak Claude práve nebeží. Po vyhodnotení dev7 sa doplní víťaz (`final2`, modely učené len na ladiacich seedoch) a referencia O3. Scenár B a čisté W, N sa do finálnej série nezaraďujú kvôli času; A a C pokrývajú zóny, W a N ostávajú z ladiacich sérií.

**Výsledok `dev7` (2026-11-04, Denník):** GS kritérium nesplnil, D: GS − Q −0.0133 PRR (−0.0187, −0.0080). Bandit neuspel ani s plnými skúškami. **Víťaz nad Q teda nie je.** Do finálnej série ide GS ako neúspešný kandidát, aby sa výsledok potvrdil na nových seedoch, nie ako víťaz.

**Finálna séria, `final2` (2026-11-04, stanovené pred behmi; seedy 1-10, rovnaké podmienky ako `final1`, 80 behov, odhad 18 h):**
- `DGS`, `TGS`: modely z dev7 **bez zmeny a bez preučenia**, seed s používa `gs_s(101 + (s−1) mod 3)` (`xgregorm_logs/dev7_models/final2/`), aby žiadny model nebol vybraný podľa výsledku na D. Porovnanie s `DQ`, `TQ` z `final1`.
- `AO0`, `AO3`, `CO0`, `CO3`, `DO0`, `DO3`: referencie podľa časti 8a. O3 − O0 je hranica pozerania dopredu, O3 − Q ostáva zmes hodnoty znalosti a chýbajúcich skúšok. T nemá zóny, orákulum tam nemá čo poznať, preto sa nespúšťa. Odchýlka oproti pôvodnému zámeru: pridané O0, bez neho sa hranica z časti 8a nedá určiť.
- Vyhodnotenie: párovo, n = 10, 95 % interval, Holm pre vedľajšie metriky, kritérium ako v časti 9. Rozhodne sa bez ohľadu na smer výsledku.

**Samostatné overenie nápadov 1-5, `idea1` až `idea5` (2026-11-04, stanovené pred behmi; vlastné seedy 201-205, nikdy nepoužité na ladenie, základ ako finálna séria bez zón, 120 s, od 30 s; 115 behov, odhad 26 h):**

| Séria | Referencie | Varianty | Poznámka |
|---|---|---|---|
| `idea1` | FW, FN, MIX, Q, ALL (`--policy all`) | I1a, I1b, I1c = `--dcc-offload` 20, 50, 100 ms | spoločné základy z časti 8 pre všetky nápady |
| `idea2` | EF1, EF2 = rovnaká zmes bez výberu | I2a, I2b = `--nb-equip 1`; zmes 1 W 0.3, N 0.2, WN 0.3, NONE 0.2; zmes 2 W 0.25, N 0.25, WN 0.5 | referencie FW a pod. tu neplatia, výbava je iná |
| `idea3` | RF1, RF2 = RSU bez výberu podľa trasy | I3a, I3b = `--rsu-route 1`; RSU W,N a W,N,WN | |
| `idea4` | FW z `idea1` | I4a, I4b, I4c = `--dup-near` 30, 60, 100 m | |
| `idea5` | FW z `idea1` | I5a, I5b, I5c = `--geo-rat` 30, 60, 100 m | |

Každá varianta vychádza z 802.11p (`--clean 1 --init-main 0`), ako v `dev`. Úspech voči vlastnej referencii: PRR aspoň +0.01, 95 % interval bez nuly (n = 5), nehoršie CPM a LTE, pri viacerých variantoch Holm. Porovnanie s Q sa uvedie tiež, nápady 2 a 3 majú inú výbavu, takže s Q sa porovnať priamo nedajú. Hodnoty 50 a 60 sú z `dev`, ostatné sú zvolené vopred, bez ladenia. Nápad 6 ostáva len v ladiacej sérii.
