# Podrobné vysvetlenia: ako to funguje vo vnútri

Odpovede na otázky, ktoré môžu prísť pri obhajobe. Vzorce útlmu sú z normy 3GPP TR 37.885 (tak, ako sú v kóde ns-3), obsah správy o polohe z kódu VaN3Twin a normy ETSI EN 302 637-2. Počty áut a správ sú priemer 5 behov série idea1 (mesto bez zón, náhody 201–205, meranie od 30. do 120. sekundy).

## 1. Ako budovy tlmia signál

Signál z antény slabne so vzdialenosťou. Koľko slabne, závisí od toho, či medzi vysielačom a prijímačom stojí budova.

- **Priama viditeľnosť:** medzi autami nie je žiadna budova. Signál ide rovno a slabne pomaly.
- **Za budovou:** budova je v ceste, signál sa k prijímaču dostane len odrazmi a ohybom okolo rohov a slabne oveľa rýchlejšie.
- **Za iným autom:** budova v ceste nie je, ale signál môže zakryť nákladiak alebo autobus. Je to mierne zhoršenie.

Ako to robí simulácia:

1. Mapa má 357 budov (kvádre 15–30 m vysoké v blokoch medzi ulicami).
2. Pre každú dvojicu áut sa nakreslí priamka. Ak pretína budovu, je to stav „za budovou“. Ak nie, simulácia podľa pravdepodobnosti z normy rozhodne, či je výhľad voľný, alebo ho zakrýva iné auto.
3. Stav sa prepočítava každých 100 ms, lebo autá sa hýbu. Pôvodne u autorov sa neprepočítaval vôbec.
4. Útlm sa počíta vzorcom z normy 3GPP pre mesto. K nemu sa pridá náhodné kolísanie (asi ±3 dB pri priamej viditeľnosti, ±4 dB za budovou) a pri Wi-Fi aj rýchle výkyvy signálu.

**Príklad (výpočet zo vzorca pre 5,9 GHz):**

| vzdialenosť | útlm, priama viditeľnosť | útlm, za budovou | prijatý signál pri 23 dBm, priama / za budovou |
|---|---|---|---|
| 50 m | 81 dB | 102 dB | −58 dBm / −79 dBm |
| 100 m | 86 dB | 111 dB | −63 dBm / −88 dBm |

Wi-Fi pre autá potrebuje na príjem aspoň −88 dBm (norma ETSI EN 302 663 pri 6 Mbit/s). **Za budovou je teda auto vzdialené 100 m presne na hranici príjmu, s priamou viditeľnosťou má 25 dB rezervu.** Preto sa v meste správy strácajú hlavne za rohmi.

V skoršom teste s 280 autami v mestskom kanáli sa kópia správy cez Wi-Fi do 100 m doručila asi v 75 % prípadov, cez priame 5G asi v 95 %. Prečo je 5G za rohom o toľko lepšie, som neoveroval. Pravdepodobne pomáha silnejšie kódovanie dát a opakovanie vysielania v 5G.

## 2. Čo presne je v správe o polohe

Správa o polohe (CAM, norma ETSI EN 302 637-2) má tieto časti:

- **Hlavička:** kto posiela (číslo stanice) a čas vytvorenia.
- **Základná časť:** typ stanice (osobné auto, sanitka…), poloha (zemepisná šírka, dĺžka, výška) a jej presnosť.
- **Rýchlo sa meniaca časť:** smer jazdy, rýchlosť, smer dopredu alebo dozadu, dĺžka a šírka auta, zrýchlenie, zakrivenie dráhy, otáčanie (yaw rate), jazdný pruh, natočenie volantu, bočné zrýchlenie.
- **Pomaly sa meniaca časť** (len občas): svetlá a rola vozidla. V aplikácii, ktorú používame, je vypnutá (tak to nastavili autori).

**Veľkosť:** samotná správa má v simulácii asi 118 bajtov. K tomu sa pridá podpis proti podvrhom, raz za sekundu s celým certifikátom. V priemere tak má správa asi 241 bajtov.

**Kedy sa posiela:** auto kontroluje každých 100 ms, či sa od poslednej správy zmenil smer o viac než 4°, poloha o viac než 4 m alebo rýchlosť o viac než 0,5 m/s. Ak áno, pošle správu. Najneskôr po 1 sekunde pošle správu vždy. Preto chodí 1 až 10 správ za sekundu, v našom meste v priemere 3,4 za sekundu na auto.

**Moje doplnenie:** prepínač zapíše do jedného nevyužitého bajtu hlavičky sieťovej vrstvy, ktorou sieťou správa odišla (1 bit pre Wi-Fi, 1 bit pre 5G) a poradové číslo od 0 do 63. Veľkosť správy sa tým nemení. Je to malé rozšírenie normy, ktoré treba v práci uviesť.

## 3. Ako presne funguje prepínač (cesta jednej správy)

**Odoslanie:**

1. Služba správ o polohe zistí, že treba poslať správu (pravidlá vyššie), a správu zostaví.
2. Sieťová vrstva normy (GeoNet a BTP) pridá svoje hlavičky.
3. Správa nejde rovno do rádia, ale do môjho prepínača. Ten zapíše značku siete a poradové číslo a rozhodne, kam ju pošle:
   - **hlavnou priamou sieťou**, teda Wi-Fi pre autá, alebo priamym 5G, ktoré vysiela všetkým autám okolo naraz;
   - **mobilnou sieťou navyše**, ak je auto na príjazde ku križovatke ďalej než 150 m od nej. Tam ho priame siete cestnej jednotky nemusia dosiahnuť. Správa ide do vysielača, cez jadro siete na server a odtiaľ po kábli do cestnej jednotky;
   - približne raz za 2 sekundy pošle jednu správu aj druhou priamou sieťou ako **skúšobnú**.
4. Ak niektorá sieť práve nefunguje (výpadok), prepínač ju hneď nahradí inou, takže správa neodíde do mŕtvej siete.
5. Pri Wi-Fi ešte platia pravidlá proti preplneniu (DCC): po každom vysielaní musí rádio chvíľu počkať. Správa preto môže čakať vo fronte a novšia rovnakého druhu staršiu nahradí.

**Príjem:**

1. Susedné auto prijme správu a prepínač prečíta značku a poradové číslo.
2. Započíta si, či prišla načas (do 100 ms) a či medzi číslami nechýba nejaké, teda či sa niečo stratilo. Toto je meranie kvality.
3. Potom správu pustí ďalej do normálneho spracovania.

**Rozhodovanie** (raz za sekundu, podmienky sú na snímke 9): auto porovná kvalitu Wi-Fi a 5G za posledné asi 2 sekundy a prípadne zmení hlavnú sieť.

Senzorové správy idú tou istou hlavnou sieťou. Správy zo semafora posiela cestná jednotka vždy oboma priamymi sieťami.

## 4. V čom presne bola hustota zlá

Pravidlo znelo: ak auto za posledné 2 sekundy nepočulo cez Wi-Fi žiadne iné auto, je osamotené, a tak pridá priame 5G s väčším dosahom.

1. **Riešilo nesprávny problém.** Pomáhalo autám ďaleko (450–600 m), no na bezpečnosť sú dôležité autá do 100 m. Tam sa nezlepšilo nič, doručenie bolo už predtým takmer 100 %.
2. **Fungovalo len v prázdnom meste.** Pri 10 autách bolo osamotené auto v 48 % rozhodnutí, pri 30 autách v 12 % a pri 100 autách len v 0,7 %. Pri bežnej premávke sa takmer nikdy nespustilo.
3. **Prísnejšie verzie stratili zmysel.** Verzie, ktoré pridávali 5G aj vtedy, keď auto niekoho počulo len cez 5G, zapínali 5G v 82 až 87 % rozhodnutí bez ohľadu na hustotu. 5G má väčší dosah, takže taký sused sa nájde takmer vždy.
4. **Kanál nebol preplnený.** Pri samotných správach o polohe bol kanál Wi-Fi obsadený menej než 5 % času. Hustota sa prejavila ako riedkosť, nie ako preplnenie.
5. **Testované bolo v ešte jednoduchom modeli**, bez budov. Keď pribudli budovy, ukázalo sa, že hlavnou príčinou strát sú práve ony.

Rovnako dopadol neskorší nápad podľa vyťaženia: preplnenie kleslo, ale doručenie nie. Spoločné poučenie: **hustota aj vyťaženie len nepriamo naznačujú problém, treba merať priamo, koľko správ prišlo.**

## 5. Prečo „všetko naraz“ dá 94,5 % správ o polohe, ale horšie senzory

Ide hlavne o to, **ako sa jednotlivé správy počítajú.**

| druh správy | ako sa počíta doručenie |
|---|---|
| správa o polohe | doručená, ak sused dostal **aspoň jednu kópiu** (Wi-Fi alebo 5G) |
| senzorová správa, správa zo semafora | **každá kópia zvlášť**: koľko kópií z tých, ktoré mali prísť, naozaj prišlo |

Pri „všetkom naraz“ ide každá správa o polohe cez Wi-Fi aj cez 5G (aj cez mobilnú sieť, ale tá autám nedoručuje). Má teda **dva pokusy**, a keď jedna kópia zlyhá, druhá ju často zachráni. Preto 94,5 %.

Senzorové správy idú pri „všetkom naraz“ tiež oboma sieťami, takže **na kanáloch je dvakrát toľko prevádzky**:

| | výber podľa kvality | všetko naraz |
|---|---|---|
| obsadenosť kanála 5G | 29 % času | 57 % času |
| obsadenosť kanála Wi-Fi | 9 % času | 13 % času |
| senzorová správa, kópia cez Wi-Fi | 80,0 % | 74,4 % |
| senzorová správa, kópia cez 5G | 92,1 % | 73,4 % |
| správa zo semafora, kópia cez Wi-Fi | 78,8 % | 68,6 % |
| správa zo semafora, kópia cez 5G | 96,4 % | 83,2 % |
| správy o polohe cez mobilnú sieť za beh | 10 154 | 42 498 (4,2×) |

Každá jednotlivá kópia sa teda doručí horšie, lebo kanály sú preplnené. **Pozor:** koľko senzorových správ prišlo aspoň jednou kópiou, simulácia nemerala. Preto netvrdím, že senzory celkovo dopadli horšie. Isté je, že „všetko naraz“ zahltí kanály, každá kópia trpí a mobilná sieť nesie 4,2-krát viac správ.

## 6. Koľko áut je na ktorej sieti

Hlavná sieť áut, priemer od 30. do 120. sekundy:

| spôsob | Wi-Fi | 5G | obsadenosť Wi-Fi | obsadenosť 5G |
|---|---|---|---|---|
| len Wi-Fi | 100 % | 0 % | 11,3 % | 5,4 % |
| len 5G | 0 % | 100 % | 3,3 % | 56,7 % |
| náhodne 50/50 | 47,7 % | 52,3 % | 7,4 % | 35,7 % |
| **podľa kvality** | **59,6 %** | **40,4 %** | 8,8 % | 29,4 % |
| všetko naraz | obe | obe | 12,7 % | 56,9 % |

Aj pri „len Wi-Fi“ je 5G trochu obsadené a pri „len 5G“ Wi-Fi, lebo cestné jednotky posielajú správy zo semafora vždy oboma sieťami.

**Čo sa deje pri výbere podľa kvality:** začne ako 50/50. Kanál 5G sa pri tejto záťaži plní rýchlejšie (v našom nastavení má len 2 časti, tzv. subkanály, o ktoré sa autá delia) a pri polovici áut je obsadený 36 % času, teda nad hranicou 30 %. Správy sa na ňom zrážajú, autá to namerajú a časť z nich prejde na Wi-Fi. V 5. sekunde je na Wi-Fi 71 % áut, potom sa časť vráti a od 10. sekundy sa podiel drží okolo 55–66 % Wi-Fi. Keď obsadenosť 5G klesne k 30 %, presuny ustanú, lebo pravidlo nepustí autá na preplnenú sieť. **Výber podľa kvality teda sám nájde rozdelenie, pri ktorom nie je zahltená ani jedna sieť.**

## 7. Koľko správ sa pošle a koľko sa stratí

Každé auto pošle v priemere **3,4 správy o polohe za sekundu** a každú má dostať v priemere **9 susedov do 100 m**. Za 90 sekúnd merania a 140 áut je to asi **385 tisíc doručení v jednom behu** (jedno doručenie = jedna správa jednému susedovi).

| spôsob | doručené | stratené | stratí sa zo 100 |
|---|---|---|---|
| len 5G | 287 tisíc | 95 tisíc | 24,9 |
| len Wi-Fi | 329 tisíc | 56 tisíc | 14,5 |
| náhodne 50/50 | 342 tisíc | 42 tisíc | 11,0 |
| **podľa kvality** | **350 tisíc** | **36 tisíc** | **9,2** |
| všetko naraz | 363 tisíc | 21 tisíc | 5,5 (dva pokusy na správu) |

**Ako to čítať:** pri samotnom 5G sa stratí štvrtina, lebo kanál je zahltený (obsadený 57 % času). Pri samotnej Wi-Fi sa stratí asi každá siedma, hlavne za budovami. Výber podľa kvality stratí najmenej z možností, ktoré posielajú každú správu jednou priamou sieťou: o 59 tisíc menej než len 5G a o 20 tisíc menej než len Wi-Fi.

Podobne pre senzorové správy: autá ich za beh pošlú asi 99 tisíc a každú má dostať v priemere asi 12 susedov.
