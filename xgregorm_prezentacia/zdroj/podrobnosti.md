# Podrobné vysvetlenia: ako to funguje vo vnútri

Odpovede na otázky, ktoré môžu prísť pri obhajobe. Vzorce útlmu sú z normy 3GPP TR 37.885 (tak, ako sú v kóde ns-3), obsah správy o polohe z kódu VaN3Twin a normy ETSI EN 302 637-2. Počty áut a správ sú priemer 5 behov série idea1 (mesto bez zón, náhody 201–205, meranie od 30. do 120. sekundy).

## 1. Druhy správ: nie všetko je správa o polohe

Správa o polohe (CAM) je len jeden z druhov správ. V simulácii chodia tri druhy priamych správ:

| druh správy | kto ju posiela | čo v nej je | ako často | veľkosť |
|---|---|---|---|---|
| **správa o polohe** (CAM) | každé auto | „som tu, idem takto rýchlo týmto smerom“ | 1–10× za sekundu, v meste priemerne 3,4× | asi 241 B s podpisom |
| **správa zo senzorov** (CPM) | auto so senzormi (v simulácii všetky) | zoznam objektov, ktoré vidí kamera a radar: iné autá, aj tie bez rádia, s ich polohou | kontrola každých 100 ms; pošle sa, keď uvidí nový objekt, objekt sa posunie o viac než 4 m alebo prejde 1 s; v meste asi 8× za sekundu na auto | 50 B + 35 B za každý objekt (+ hlavičky a podpis), v meste asi 370 B |
| **správy zo semafora** (SPATEM a MAPEM) | cestná jednotka pri križovatke | SPATEM: aká farba svieti a kedy sa zmení; MAPEM: mapa križovatky (pruhy, kam vedú) | SPATEM 10× za sekundu, MAPEM 1× za sekundu | SPATEM asi 190 B, MAPEM asi 1 kB (+ hlavičky a podpis) |

Rozdiel pre výber siete:

- **Správu o polohe a správu zo senzorov** posiela auto, takže idú jeho **hlavnou sieťou**, ktorú vyberá prepínač.
- **Správy zo semafora** posiela cestná jednotka, a to **vždy oboma priamymi sieťami** (802.11p aj 5G), pri každom spôsobe výberu rovnako. O nich auto nerozhoduje.
- Kvalitu siete auto meria **len zo správ o polohe**, lebo len tie nesú poradové číslo.

Správ zo senzorov je viac ako dvakrát toľko ako správ o polohe a sú väčšie. Preto práve ony najviac plnia kanál.

## 2. Ako sa líšia tri siete

**802.11p (v Európe ITS-G5)**

- **Nie je to bežná Wi-Fi.** Je to samostatná norma IEEE 802.11p pre dopravu, ktorá z rodiny 802.11 prevzala len rádiovú časť (modulácia OFDM a prístup ku kanálu). Na rozdiel od Wi-Fi nemá prístupový bod (router), prihlasovanie ani šifrovanie spojenia, beží v pásme vyhradenom pre dopravu (5,9 GHz) s polovičnou šírkou kanála (10 MHz namiesto 20 MHz) a nad ňou je európsky komunikačný systém ITS-G5 (GeoNetworking, CAM, CPM). Preto ho v práci nevolám Wi-Fi.
- Auto vysiela priamo do vzduchu a počuje ho každý v dosahu.
- Platí pravidlo „počúvaj, potom hovor“. Pred vysielaním rádio počúva. Ak niekto iný vysiela, počká náhodnú chvíľu a skúsi znova.
- Správa pre všetkých nemá potvrdenie ani opakovanie. Keď sa stratí, stratí sa.
- Pásmo je 10 MHz pri 5,9 GHz, rýchlosť 6 Mbit/s. Správa o polohe trvá vo vzduchu asi 0,4 ms. Keď kanál nie je vyťažený, príde za menej než 1 ms.
- Podľa normy musí rádio po každom vysielaní chvíľu počkať (pravidlá proti preplneniu). Správy preto čakajú vo fronte, a tak je v plnom meste priemerné oneskorenie asi 25–29 ms (5G 19–26 ms).

**Priame 5G (NR-V2X, „sidelink“)**

- Tiež ide priamo auto–auto, bez vysielača na streche.
- Funguje inak ako 802.11p: kanál je rozdelený na malé políčka podľa času (sloty po 0,25 ms) a frekvencie (v našom nastavení len 2 časti, tzv. subkanály, pri 20 MHz).
- Každé auto si samo vyberie políčko a vysiela v ňom. Nepočúva pred vysielaním. Keď si dve autá vyberú to isté políčko, správy sa zrazia.
- V simulácii je vypnuté „predbežné sledovanie“, ktorým by sa auto mohlo obsadeným políčkam vyhnúť, rovnako ako u autorov. S ním simulácia nedobehla. Kolízií je preto viac než v realite a treba to uviesť ako obmedzenie.
- Oneskorenie je asi 20 ms, lebo auto čaká na svoje políčko.
- Pásmo je oddelené od 802.11p (5,885 GHz, 20 MHz).

**Mobilná sieť (LTE)**

- Ide cez vysielač na streche (v meste je jedna bunka).
- Auto si najprv vypýta povolenie, vysielač mu pridelí miesto, takže sa správy nezrážajú.
- Cesta je auto → vysielač → jadro siete → server → kábel → cestná jednotka. Susedné autá správu nedostanú, server ju ďalej autám neposiela.
- Oneskorenie je asi 70–90 ms, lebo bunku zdieľajú aj telefóny v autách.
- Bunka v simulácii zvládne najviac asi 280 zariadení.

## 3. Je auto niekedy len na mobilnej sieti?

Vo finálnych behoch **nie**. Hlavná sieť je vždy 802.11p alebo 5G, lebo len tie doručia správu susedným autám. Mobilnú sieť má každé auto (telefón, telematika) a prepínač ju používa len **navyše**: keď je auto na príjazde ku križovatke ďalej než 150 m, pošle správu aj mobilnou sieťou do cestnej jednotky. Toto pravidlo je pri všetkých spôsoboch rovnaké, preto je počet správ cez mobilnú sieť pri všetkých asi 10 tisíc za beh (okrem „všetkého naraz“, kde je 42 tisíc).

Len na mobilnej sieti by auto ostalo iba pri výpadku oboch priamych sietí. To som testoval v októbri: auto vtedy hneď prejde na mobilnú sieť, cestná jednotka o ňom ďalej vie, ale susedné autá ho nepočujú (doručenie autám 0 %).

## 4. Ako ide správa cez mobilnú sieť a čo v nej je

**Nie je to satelit.** Mobilná sieť (LTE) je pozemná: vysielače na strechách (bunky) sú prepojené káblami s centrálou operátora. Starlink ani iný satelit sa v práci nepoužíva.

**Postup jednej správy:**

1. Auto má modem mobilnej siete, rovnako ako telefón. Keď chce poslať správu, najprv **požiada vysielač o povolenie**.
2. Vysielač (v simulácii jeden, v strede mesta, 25 m vysoko, pásmo 10 MHz, čo je 50 blokov) **každú milisekundu rozdelí bloky** medzi zariadenia, ktoré chcú vysielať. Auto dostane svoje bloky a v nich pošle správu. Tie isté bloky v tom čase nepoužíva nikto iný, preto sa správy **nezrážajú**. Za to treba čakať na pridelenie.
3. Vysielač pošle správu po kábli do jadra mobilnej siete (centrála operátora) a odtiaľ na server. V simulácii to trvá 5 ms.
4. Server ju pošle po kábli (100 Mbit/s, 1 ms) do cestných jednotiek pri križovatkách.
5. **Späť k autám nejde nič.** Autá cez mobilnú sieť nedostávajú správy o iných autách.

**Čo posiela:** tú istú správu o polohe ako cez 802.11p a 5G (poloha, rýchlosť, smer, rozmery…), zabalenú ako internetový paket. Senzorové správy cez mobilnú sieť nejdú.

**Na čo ju cestná jednotka potrebuje:** cestná jednotka je zároveň radič semafora.

1. Z polohy a smeru auta zistí, na ktorom príjazde ku križovatke auto je.
2. Spočíta autá na každom príjazde.
3. Podľa toho predĺži alebo skráti zelenú. Dĺžka zelenej je úmerná počtu áut, pri veľkej nerovnováhe prepne skôr. Je to algoritmus autorov VaN3Twin.

Keď o aute nevie, semafor riadi podľa neúplného obrazu.

**Kedy auto posiela aj mobilnou sieťou:** len keď je na príjazde ku križovatke ďalej než 150 m od nej, lebo tam ho priame siete cestnej jednotky nemusia dosiahnuť. Navyše tak ide asi štvrtina správ o polohe, približne 10 tisíc za beh. Pri všetkých spôsoboch výberu je to rovnako, okrem „všetkého naraz“ (42 tisíc).

**Môže sa mobilná sieť zahltiť? Áno.** Bunku zdieľajú:

- kópie správ o polohe,
- telefóny v polovici áut (69 áut × 20 kbit/s, asi 22 tisíc paketov po 1000 B za beh),
- pri predikcii aj hlásenia do mapy kvality.

| | výber podľa kvality | všetko naraz |
|---|---|---|
| vyťaženie bunky (podiel pridelených blokov) | 44 % | 49 % |
| podiel času nad 30 % | 93 % | 99,9 % |
| oneskorenie: polovica správ do | 22 ms | 26 ms |
| oneskorenie: najpomalších 5 % správ viac ako | 1,1 s | 1,0 s |

V skoršom teste s 250 autami, ktoré posielali všetko len mobilnou sieťou, bolo oneskorenie 289 ms a stále rástlo. Bunka v simulácii zvládne najviac asi 280–320 zariadení.

**Pozor na číslo vyťaženia:** plánovač v ns-3 rozdelí celé pásmo medzi tých, čo práve vysielajú, aj keď im stačí menej. Vyťaženie blokov preto nadhodnocuje skutočnú potrebu. Pre mobilnú sieť je spoľahlivejšie sledovať oneskorenie a počet správ.

## 5. Čo je obsadenosť kanála a prečo také čísla

**Obsadenosť** hovorí, aký podiel času je kanál zaneprázdnený, teda niekto v okolí práve vysiela. Je to ako vysielačka: kým hovorí jeden, ostatní čakajú alebo sa prekrikujú. Aj 802.11p teda môže byť obsadená, keď v okolí vysiela veľa áut.

- **802.11p** meria každé auto podľa normy ETSI: podiel času, keď počuje signál silnejší než −85 dBm.
- **5G** sa v simulácii dá len odhadnúť: podiel políčok (slot × subkanál), v ktorých auto zachytilo vysielanie.
- Hranica 30 % je z noriem pre obe siete. Nad ňou sa kanál považuje za preplnený.

**Ako presne sa počíta obsadenosť 802.11p** (meranie podľa normy ETSI TS 102 687):

1. Pri každom vyslaní akéhokoľvek rámca 802.11p simulácia vypočíta pre každé auto, aký silný signál k nemu dorazí. Použije ten istý model útlmu ako rádio.
2. Ak je signál silnejší než −85 dBm, celé trvanie rámca sa autu započíta ako „obsadené“. Vlastné vysielanie sa započíta tiež.
3. Každých 100 ms sa obsadený čas vydelí 100 ms. Výsledok je obsadenosť pre toto auto.
4. Čísla v tabuľkách sú priemer cez všetky autá a všetky 100 ms úseky.

Príklad: správa o polohe trvá asi 0,4 ms. Keď auto za 100 ms počuje 25 takých správ, je kanál obsadený 10 ms, teda 10 %. Pri rušení sa pripočítajú aj signály 5G zo susedného pásma. Zjednodušenie: viac slabých signálov pod −85 dBm sa nesčíta.

**Ako sa odhaduje obsadenosť 5G:**

1. Kanál je mriežka políčok: 2 subkanály × 400 slotov za 100 ms, teda 800 políčok.
2. Pred každou správou ide krátka riadiaca informácia, ktoré políčka správa zaberá. Auto, ktoré ju zachytí, si tie políčka označí ako obsadené.
3. Obsadenosť je počet označených políčok delený 800.

Je to dolný odhad, lebo vysielania, ktoré auto nezachytilo (kolízie, veľká vzdialenosť), chýbajú. Norma meria inak, silu signálu v každom políčku. To by si vyžadovalo zásah do modulu 5G v ns-3, ktorý som zámerne nerobil.

**Mobilná sieť:** koľko z 50 blokov vysielač pridelil v každej milisekunde (viac v časti 4).

**Čísla 802.11p a 5G sa nedajú porovnávať 1 : 1.** Merajú sa inak a 5G má v našom nastavení len 2 subkanály, takže každá správa zaberie celé políčko. Porovnávať treba vždy tú istú sieť medzi rôznymi spôsobmi výberu.

**Prečo je 5G obsadené 5 %, aj keď sú všetky autá na 802.11p?** Lebo cestné jednotky posielajú správy zo semafora vždy aj cez 5G (9 jednotiek × 10 SPATEM a 1 MAPEM za sekundu). To je tých 5 %. Rovnako je 802.11p obsadené 3 %, keď sú všetky autá na 5G. Malú časť môže pridávať aj slabý únik signálu z druhého pásma.

**Prečo je 5G obsadené 57 % pri „len 5G“ aj pri „všetkom naraz“?** V oboch prípadoch ide cez 5G úplne všetko: všetky správy o polohe, všetky správy zo senzorov aj správy zo semafora. Pre kanál 5G je to tá istá prevádzka. Že pri „všetkom naraz“ idú tie isté správy aj cez 802.11p, kanál 5G nezaťaží, lebo je to iné pásmo. Preto má aj 802.11p pri „všetkom naraz“ (12,7 %) podobnú obsadenosť ako pri „len 802.11p“ (11,3 %).

**Prečo je pri výbere podľa kvality 5G len na 29 %?** Lebo cez 5G posiela len asi 40 % áut a zvyšok ide cez 802.11p.

| spôsob | kto posiela cez 802.11p | kto posiela cez 5G | obsadenosť 802.11p | obsadenosť 5G |
|---|---|---|---|---|
| len 802.11p | všetky autá + semafory | len semafory | 11,3 % | 5,4 % |
| len 5G | len semafory | všetky autá + semafory | 3,3 % | 56,7 % |
| náhodne 50/50 | polovica áut + semafory | polovica áut + semafory | 7,4 % | 35,7 % |
| podľa kvality | asi 60 % áut + semafory | asi 40 % áut + semafory | 8,8 % | 29,4 % |
| všetko naraz | všetky autá + semafory | všetky autá + semafory | 12,7 % | 56,9 % |

## 6. Ako budovy tlmia signál

Signál z antény slabne so vzdialenosťou. Koľko slabne, závisí od toho, či medzi vysielačom a prijímačom stojí budova.

- **Priama viditeľnosť:** medzi autami nie je žiadna budova. Signál ide rovno a slabne pomaly.
- **Za budovou:** budova je v ceste, signál sa k prijímaču dostane len odrazmi a ohybom okolo rohov a slabne oveľa rýchlejšie.
- **Za iným autom:** budova v ceste nie je, ale signál môže zakryť nákladiak alebo autobus. Je to mierne zhoršenie.

Ako to robí simulácia:

1. Mapa má 357 budov (kvádre 15–30 m vysoké v blokoch medzi ulicami).
2. Pre každú dvojicu áut sa nakreslí priamka. Ak pretína budovu, je to stav „za budovou“. Ak nie, simulácia podľa pravdepodobnosti z normy rozhodne, či je výhľad voľný, alebo ho zakrýva iné auto.
3. Stav sa prepočítava každých 100 ms, lebo autá sa hýbu. Pôvodne u autorov sa neprepočítaval vôbec.
4. Útlm sa počíta vzorcom z normy 3GPP pre mesto. K nemu sa pridá náhodné kolísanie (asi ±3 dB pri priamej viditeľnosti, ±4 dB za budovou) a pri 802.11p aj rýchle výkyvy signálu.

**Príklad (výpočet zo vzorca pre 5,9 GHz):**

| vzdialenosť | útlm, priama viditeľnosť | útlm, za budovou | prijatý signál pri 23 dBm, priama / za budovou |
|---|---|---|---|
| 50 m | 81 dB | 102 dB | −58 dBm / −79 dBm |
| 100 m | 86 dB | 111 dB | −63 dBm / −88 dBm |

802.11p potrebuje na príjem aspoň −88 dBm (norma ETSI EN 302 663 pri 6 Mbit/s). **Za budovou je teda auto vzdialené 100 m presne na hranici príjmu, s priamou viditeľnosťou má 25 dB rezervu.** Preto sa v meste správy strácajú hlavne za rohmi.

V skoršom teste s 280 autami v mestskom kanáli sa kópia správy cez 802.11p do 100 m doručila asi v 75 % prípadov, cez priame 5G asi v 95 %. Prečo je 5G za rohom o toľko lepšie, som neoveroval. Pravdepodobne pomáha silnejšie kódovanie dát a opakovanie vysielania v 5G.

## 7. Čo presne je v správe o polohe

Správa o polohe (CAM, norma ETSI EN 302 637-2) má tieto časti:

- **Hlavička:** kto posiela (číslo stanice) a čas vytvorenia.
- **Základná časť:** typ stanice (osobné auto, sanitka…), poloha (zemepisná šírka, dĺžka, výška) a jej presnosť.
- **Rýchlo sa meniaca časť:** smer jazdy, rýchlosť, smer dopredu alebo dozadu, dĺžka a šírka auta, zrýchlenie, zakrivenie dráhy, otáčanie (yaw rate), jazdný pruh, natočenie volantu, bočné zrýchlenie.
- **Pomaly sa meniaca časť** (len občas): svetlá a rola vozidla. V aplikácii, ktorú používame, je vypnutá (tak to nastavili autori).

**Veľkosť:** samotná správa má v simulácii asi 118 bajtov. K tomu sa pridá podpis proti podvrhom, raz za sekundu s celým certifikátom. V priemere tak má správa asi 241 bajtov.

**Kedy sa posiela:** auto kontroluje každých 100 ms, či sa od poslednej správy zmenil smer o viac než 4°, poloha o viac než 4 m alebo rýchlosť o viac než 0,5 m/s. Ak áno, pošle správu. Najneskôr po 1 sekunde pošle správu vždy. Preto chodí 1 až 10 správ za sekundu, v našom meste v priemere 3,4 za sekundu na auto.

**Moje doplnenie:** prepínač zapíše do jedného nevyužitého bajtu hlavičky sieťovej vrstvy, ktorou sieťou správa odišla (1 bit pre 802.11p, 1 bit pre 5G) a poradové číslo od 0 do 63. Veľkosť správy sa tým nemení. Je to malé rozšírenie normy, ktoré treba v práci uviesť.

## 8. Ako presne funguje prepínač (cesta jednej správy)

**Odoslanie:**

1. Služba správ o polohe zistí, že treba poslať správu (pravidlá vyššie), a správu zostaví.
2. Sieťová vrstva normy (GeoNet a BTP) pridá svoje hlavičky.
3. Správa nejde rovno do rádia, ale do môjho prepínača. Ten zapíše značku siete a poradové číslo a rozhodne, kam ju pošle:
   - **hlavnou priamou sieťou**, teda 802.11p, alebo priamym 5G, ktoré vysiela všetkým autám okolo naraz;
   - **mobilnou sieťou navyše**, ak je auto na príjazde ku križovatke ďalej než 150 m od nej. Tam ho priame siete cestnej jednotky nemusia dosiahnuť. Správa ide do vysielača, cez jadro siete na server a odtiaľ po kábli do cestnej jednotky;
   - približne raz za 2 sekundy pošle jednu správu aj druhou priamou sieťou ako **skúšobnú**.
4. Ak niektorá sieť práve nefunguje (výpadok), prepínač ju hneď nahradí inou, takže správa neodíde do mŕtvej siete.
5. Pri 802.11p ešte platia pravidlá proti preplneniu (DCC): po každom vysielaní musí rádio chvíľu počkať. Správa preto môže čakať vo fronte a novšia rovnakého druhu staršiu nahradí.

**Príjem:**

1. Susedné auto prijme správu a prepínač prečíta značku a poradové číslo.
2. Započíta si, či prišla načas (do 100 ms) a či medzi číslami nechýba nejaké, teda či sa niečo stratilo. Toto je meranie kvality.
3. Potom správu pustí ďalej do normálneho spracovania.

**Rozhodovanie** (raz za sekundu, podmienky sú na snímke 10): auto porovná kvalitu 802.11p a 5G za posledné asi 2 sekundy a prípadne zmení hlavnú sieť.

Senzorové správy idú tou istou hlavnou sieťou. Správy zo semafora posiela cestná jednotka vždy oboma priamymi sieťami.

## 9. V čom presne bola hustota zlá

Pravidlo znelo: ak auto za posledné 2 sekundy nepočulo cez 802.11p žiadne iné auto, je osamotené, a tak pridá priame 5G s väčším dosahom.

1. **Riešilo nesprávny problém.** Pomáhalo autám ďaleko (450–600 m), no na bezpečnosť sú dôležité autá do 100 m. Tam sa nezlepšilo nič, doručenie bolo už predtým takmer 100 %.
2. **Fungovalo len v prázdnom meste.** Pri 10 autách bolo osamotené auto v 48 % rozhodnutí, pri 30 autách v 12 % a pri 100 autách len v 0,7 %. Pri bežnej premávke sa takmer nikdy nespustilo.
3. **Prísnejšie verzie stratili zmysel.** Verzie, ktoré pridávali 5G aj vtedy, keď auto niekoho počulo len cez 5G, zapínali 5G v 82 až 87 % rozhodnutí bez ohľadu na hustotu. 5G má väčší dosah, takže taký sused sa nájde takmer vždy.
4. **Kanál nebol preplnený.** Pri samotných správach o polohe bol kanál 802.11p obsadený menej než 5 % času. Hustota sa prejavila ako riedkosť, nie ako preplnenie.
5. **Testované bolo v ešte jednoduchom modeli**, bez budov. Keď pribudli budovy, ukázalo sa, že hlavnou príčinou strát sú práve ony.

Rovnako dopadol neskorší nápad podľa vyťaženia: preplnenie kleslo, ale doručenie nie. Spoločné poučenie: **hustota aj vyťaženie len nepriamo naznačujú problém, treba merať priamo, koľko správ prišlo.**

## 10. Prečo „všetko naraz“ dá 94,5 % správ o polohe, ale horšie senzory

Ide hlavne o to, **ako sa jednotlivé správy počítajú.**

| druh správy | ako sa počíta doručenie |
|---|---|
| správa o polohe | doručená, ak sused dostal **aspoň jednu kópiu** (802.11p alebo 5G) |
| senzorová správa, správa zo semafora | **každá kópia zvlášť**: koľko kópií z tých, ktoré mali prísť, naozaj prišlo |

Pri „všetkom naraz“ ide každá správa o polohe cez 802.11p aj cez 5G (aj cez mobilnú sieť, ale tá autám nedoručuje). Má teda **dva pokusy**, a keď jedna kópia zlyhá, druhá ju často zachráni. Preto 94,5 %.

Senzorové správy idú pri „všetkom naraz“ tiež oboma sieťami, takže **na kanáloch je dvakrát toľko prevádzky**:

| | výber podľa kvality | všetko naraz |
|---|---|---|
| obsadenosť kanála 5G | 29 % času | 57 % času |
| obsadenosť kanála 802.11p | 9 % času | 13 % času |
| senzorová správa, kópia cez 802.11p | 80,0 % | 74,4 % |
| senzorová správa, kópia cez 5G | 92,1 % | 73,4 % |
| správa zo semafora, kópia cez 802.11p | 78,8 % | 68,6 % |
| správa zo semafora, kópia cez 5G | 96,4 % | 83,2 % |
| správy o polohe cez mobilnú sieť za beh | 10 154 | 42 498 (4,2×) |

Každá jednotlivá kópia sa teda doručí horšie, lebo kanály sú preplnené. **Pozor:** koľko senzorových správ prišlo aspoň jednou kópiou, simulácia nemerala. Preto netvrdím, že senzory celkovo dopadli horšie. Isté je, že „všetko naraz“ zahltí kanály, každá kópia trpí a mobilná sieť nesie 4,2-krát viac správ.

## 11. Koľko áut je na ktorej sieti

Hlavná sieť áut, priemer od 30. do 120. sekundy:

| spôsob | 802.11p | 5G | obsadenosť 802.11p | obsadenosť 5G |
|---|---|---|---|---|
| len 802.11p | 100 % | 0 % | 11,3 % | 5,4 % |
| len 5G | 0 % | 100 % | 3,3 % | 56,7 % |
| náhodne 50/50 | 47,7 % | 52,3 % | 7,4 % | 35,7 % |
| **podľa kvality** | **59,6 %** | **40,4 %** | 8,8 % | 29,4 % |
| všetko naraz | obe | obe | 12,7 % | 56,9 % |

Aj pri „len 802.11p“ je 5G trochu obsadené a pri „len 5G“ 802.11p, lebo cestné jednotky posielajú správy zo semafora vždy oboma sieťami.

**Čo sa deje pri výbere podľa kvality:** začne ako 50/50. Kanál 5G sa pri tejto záťaži plní rýchlejšie (v našom nastavení má len 2 časti, tzv. subkanály, o ktoré sa autá delia) a pri polovici áut je obsadený 36 % času, teda nad hranicou 30 %. Správy sa na ňom zrážajú, autá to namerajú a časť z nich prejde na 802.11p. V 5. sekunde je na 802.11p 71 % áut, potom sa časť vráti a od 10. sekundy sa podiel drží okolo 55–66 % 802.11p. Keď obsadenosť 5G klesne k 30 %, presuny ustanú, lebo pravidlo nepustí autá na preplnenú sieť. **Výber podľa kvality teda sám nájde rozdelenie, pri ktorom nie je zahltená ani jedna sieť.**

## 12. Koľko správ sa pošle a koľko sa stratí

Každé auto pošle v priemere **3,4 správy o polohe za sekundu** a každú má dostať v priemere **9 susedov do 100 m**. Za 90 sekúnd merania a 140 áut je to asi **385 tisíc doručení v jednom behu** (jedno doručenie = jedna správa jednému susedovi).

| spôsob | doručené | stratené | stratí sa zo 100 |
|---|---|---|---|
| len 5G | 287 tisíc | 95 tisíc | 24,9 |
| len 802.11p | 329 tisíc | 56 tisíc | 14,5 |
| náhodne 50/50 | 342 tisíc | 42 tisíc | 11,0 |
| **podľa kvality** | **350 tisíc** | **36 tisíc** | **9,2** |
| všetko naraz | 363 tisíc | 21 tisíc | 5,5 (dva pokusy na správu) |

**Ako to čítať:** pri samotnom 5G sa stratí štvrtina, lebo kanál je zahltený (obsadený 57 % času). Pri samotnom 802.11p sa stratí asi každá siedma, hlavne za budovami. Výber podľa kvality stratí najmenej z možností, ktoré posielajú každú správu jednou priamou sieťou: o 59 tisíc menej než len 5G a o 20 tisíc menej než len 802.11p.

Podobne pre senzorové správy: autá ich za beh pošlú asi 99 tisíc a každú má dostať v priemere asi 12 susedov.
