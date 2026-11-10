# Diplomová práca jednoducho: výber technológie pre komunikáciu áut

Vysvetlenie celej práce od začiatku po koniec bez odborných skratiek. Každé odborné slovo je vysvetlené pri prvom použití, ďalej sa používajú obyčajné pomenovania. Všetky čísla sú prevzaté z Denníka v CLAUDE.md a z plánu xgregorm_PLAN_PREDIKCIA.md.

## 1. O čom je celá práca

Moderné autá sa medzi sebou rozprávajú rádiom. Každé auto niekoľkokrát za sekundu vykričí do okolia krátku správu: *„Som tu, idem takouto rýchlosťou týmto smerom.“* Volá sa **správa o polohe** (odborne CAM). Susedné autá si z nej poskladajú obraz okolia, takže vedia, že spoza rohu ide auto, aj keď ho vodič nevidí. Rovnakú správu chytí aj **skrinka pri križovatke** (odborne RSU, cestná jednotka). Je pripojená k semaforu a podľa áut, ktoré sa blížia, prepína zelenú.

Okrem nej autá a križovatky posielajú ďalšie dve správy:

- **správu zo senzorov** (CPM): „moja kamera vidí chodca a cyklistu tu a tu“,
- **správu zo semafora**: „o 5 sekúnd bude červená“, k tomu mapa križovatky.

**Problém:** auto má na posielanie tri rôzne „rádiá“ a treba rozhodnúť, ktorým správu pošle.

| | čo to je | výhoda | nevýhoda |
|---|---|---|---|
| **Wi-Fi pre autá** (802.11p) | starší systém, auto vysiela priamo autám okolo | veľmi rýchle (zlomok milisekundy) | krátky dosah, budovy ho tlmia, pri veľa autách sa „prekrikujú“ |
| **Priame 5G** (NR-V2X) | novší systém, tiež priamo auto-auto, bez vysielača | ďalší dosah, odolnejšie | pomalšie (asi 10–20 ms), keď vysiela veľa áut naraz, správy sa zrážajú |
| **Mobilná sieť** (LTE) | ako mobil: auto, vysielač na streche, internet, križovatka | dosiahne všade | k susedným autám nejde, len na server; pri plnej bunke je pomalé |

Toto rozhodovanie sa odborne volá **výber rádiovej technológie** a je to oficiálna téma práce. Cieľ: nájsť spôsob, ako auto vyberie technológiu tak, aby správy doručilo čo najspoľahlivejšie.

## 2. Na čom sa to skúša

Skutočných 280 áut v meste nemáme, preto všetko beží v **simulácii**, v dvoch programoch naraz:

- **SUMO** simuluje premávku: autá jazdia po mapách, stoja na semaforoch, odbočujú.
- **ns-3** simuluje rádiá: ako sa signál šíri, ako ho tlmia budovy, ako sa správy zrážajú, koľko meškajú.

Program **VaN3Twin** od iných autorov ich spája. Do neho sa dopisovali vlastné veci. Mapa je vymyslené centrum mesta s 9 križovatkami so semaformi a pri každej stojí skrinka.

Hlavné číslo, ktoré meriame, je **spoľahlivosť doručenia**: zo 100 správ o polohe, ktoré mali prísť susedom do 100 metrov, koľko naozaj prišlo. Odborne sa volá PRR. Keď ďalej píšem „79 zo 100“, myslím tým toto.

## 3. Začiatky: najprv sa opravovali chyby (7.–11. októbra)

Skôr než sa dalo čokoľvek merať, ukázalo sa, že program autorov má chyby:

1. **Autá kričali všetky naraz.** Autá, ktoré vznikli v rovnakom okamihu, dostali rovnakú „náhodu“, preto posielali správy v presne tých istých chvíľach a navzájom sa prekrikovali. Doručovalo sa len asi 35 zo 100 správ. Po oprave 99,7 zo 100.
2. **Skrinky sa bili o semafory.** Každá skrinka ovládala všetky semafory v meste, nielen svoj, takže ich prepínali jedna druhej. Autá stáli aj 104 sekúnd. Po oprave riadi každá skrinka len svoju križovatku a najdlhšie státie kleslo na asi 18 sekúnd.
3. **Simulácia občas zamrzla**, keď bola križovatka prázdna (delilo sa nulou). Opravené.
4. **Niektoré semafory mali obrátené smery**: zelenú dávali tomu smeru, kde nikto nebol. Opravené.
5. **Priame 5G padalo pri vyše 255 autách** (tri chyby v knižnici iných autorov). Opravené.

Toto ešte nie je hlavný výsledok práce, ale do práce to patrí: bez týchto opráv by žiadne meranie nemalo zmysel.

## 4. Prepínač: auto vie poslať správu hociktorou cestou (12.–13. októbra)

Vznikol **prepínač**. Auto najprv rozhodne, *kedy* správu pošle (podľa normy), a prepínač potom rozhodne, *ktorou cestou*: Wi-Fi pre autá, priamym 5G, mobilnou sieťou alebo viacerými naraz.

Vyskúšali sa aj **výpadky**: napríklad Wi-Fi na 30 sekúnd „vypadne“. Auto do sekundy prejde na inú technológiu a nič sa nestratí. To bol prvý dôkaz, že kombinovať technológie má zmysel.

## 5. Prvý nápad: rozhodovať podľa hustoty áut (13. októbra)

**Myšlienka:** keď auto nepočuje okolo seba žiadne iné auto, je „osamotené“. Pridá preto priame 5G, ktoré má väčší dosah, aby ho počuli aj ďalej.

**Čo vyšlo:**

- Pri 10 autách na mape sa správy k autám vzdialeným 450–600 m doručovali v 68 zo 100 namiesto 17 zo 100. Pomohlo to.
- Pri 30 a 100 autách už takmer nič, lebo pri hustejšej premávke nie je skoro nikto osamotený.
- K blízkym autám (do 100 m), na ktorých záleží najviac, sa nezlepšilo nič.
- Prísnejšia verzia síce dosiahla ďaleko, ale priame 5G zapínala takmer stále, takže už nezáležalo na tom, koľko áut je okolo.

**Prečo to nebola dobrá cesta:** hustota len *nepriamo naznačuje*, či bude problém. Pri samotných správach o polohe nebolo rádio nikdy preplnené, autá boli skôr riedko. Hustota riešila dosah, a to nie je hlavný problém. Preto bola z kódu odstránená, ale výsledky sú zapísané.

## 6. Realistickejší svet (20. októbra – 3. novembra)

Simulácia bola dovtedy príliš ideálna, preto sa postupne pridali:

- **budovy**: za rohom signál slabne (odborne „bez priamej viditeľnosti“),
- **podpis správ**: v realite je každá správa podpísaná proti podvrhom, takže je väčšia,
- **ďalšie správy**: zo semaforov a zo senzorov všetkých áut, aby bolo rádio naozaj vyťažené,
- **rušenie**: Wi-Fi pre autá a priame 5G sa v pásme navzájom rušia,
- **brzdy proti preplneniu** (odborne DCC): keď je rádio plné, norma nariaďuje autu vysielať menej často,
- **mobily v autách a iných používateľov mobilnej siete**, aby nebola prázdna,
- **náhodné krátke výkyvy signálu**.

### Druhý nápad: rozhodovať podľa vyťaženia

**Myšlienka:** keď je Wi-Fi preplnená, časť áut presunieme na priame 5G.

Mechanicky to fungovalo: asi 15 % áut sa presunulo a Wi-Fi bola menej preplnená. **Spoľahlivosť sa však nezlepšila.** Ukázalo sa prečo: správy sa nestrácali kvôli preplneniu, ale kvôli **budovám**. Odľahčenie rádia nepomôže, keď je problém v tom, že auto je za rohom.

**Toto je kľúčová lekcia celej práce:** keď sa rozhoduješ podľa niečoho, čo len *nepriamo naznačuje* problém (hustota, vyťaženie), netrafíš skutočnú príčinu strát.

## 7. Tretí nápad, ten hlavný: rozhodovať podľa nameranej kvality (od 20. októbra)

**Myšlienka:** nehádaj, prečo sa správy strácajú, ale **zmeraj priamo, koľko ich prichádza**.

**Ako to auto meria:**

- Každá správa nesie malé **poradové číslo**: 1, 2, 3, 4… Je vložené do nevyužitého bajtu v hlavičke, takže správa nie je väčšia.
- Keď auto od suseda dostane správy 1, 2, 4, 5, vie, že **správa 3 sa stratila**.
- Takto si auto pre každú technológiu zvlášť ráta: „cez Wi-Fi mi od susedov prichádza 80 zo 100 správ, cez priame 5G 95 zo 100“. Počíta sa len to, čo prišlo včas (do 100 ms).
- Predpokladá sa, že ak ja od susedov dobre počujem cez 5G, aj oni dobre počujú mňa (rádio funguje oboma smermi podobne).

**Pravidlo:** raz za sekundu sa auto pozrie, či je druhá technológia lepšia aspoň o 5 zo 100 a nie je preplnená. Ak áno, prejde na ňu. Nerobia to všetky autá naraz, ale len časť, aby celé mesto neskočilo na jednu technológiu a nepreplnilo ju.

Aby auto vedelo, ako funguje technológia, ktorú práve nepoužíva, občas cez ňu pošle **skúšobnú správu** (asi raz za dve sekundy).

V skratke sa tento spôsob volá **„výber podľa kvality“**. V súboroch je označený písmenom Q.

## 8. Ako sa dokazuje, že niečo naozaj pomohlo

Simulácia má v sebe náhodu: kde autá štartujú, kedy vysielajú, ako kolíše signál. Jeden beh preto nič nedokáže, lebo rozdiel môže byť náhoda. Preto:

1. **Každé porovnanie sa opakuje s rôznymi náhodami.** Každá náhoda má číslo (odborne „seed“), napríklad 1 až 10.
2. **Porovnáva sa v pároch:** dva spôsoby rozhodovania bežia s *rovnakou* náhodou, teda s rovnakými autami na rovnakých miestach. Líši sa len pravidlo.
3. **Počíta sa interval istoty:** napríklad „rozdiel je +13 zo 100 a s 95 % istotou je medzi +12,5 a +14,3“. Ak celý interval leží nad nulou, zlepšenie je **preukázané**. Ak zasahuje cez nulu, je to možno náhoda.
4. **Kritérium úspechu bolo stanovené vopred**, pred behmi, aby sa nedalo prispôsobiť podľa výsledkov.
5. **Ladilo sa na iných náhodách než na finálnom meraní.** Ladenie bolo na číslach 101–103, finálne meranie na 1–10 a nápady na 201–205. Výsledok teda nie je „nastavený na mieru“.
6. Keď sa porovnáva veľa vecí naraz, výsledky sa ešte prísnejšie prepočítajú (odborne Holmova korekcia), aby nejaký nevyšiel dobre len náhodou.

### Čo s čím porovnávame

Hlavné porovnanie nie je „kvalita oproti niečomu zložitému“, ale **„kvalita oproti stavu, keď auto nerozhoduje vôbec“**:

- **pevné rozdelenie**: polovica áut má natrvalo Wi-Fi, polovica priame 5G (v súboroch MIX),
- **len Wi-Fi** pre všetkých (FW),
- **len priame 5G** pre všetkých (FN).

## 9. Scenáre: čo sú tie „zóny“

V realite sú v meste miesta, kde jedna technológia funguje zle: za budovou, pri zdroji rušenia, v diere pokrytia, pri skrinke, ktorá má len jednu technológiu. V simulácii ich ťažko presne vyrobiť a opakovať. Preto vznikli **zóny**: kruhy okolo niektorých križovatiek, kde sa *umelo* stratí 70 % správ jednej technológie. V dvoch zónach je zlá Wi-Fi, v dvoch priame 5G.

Je to **kontrolovaný pokus**, nie model skutočnej fyziky. Výhoda: presne vieme, kde a kedy je zle, takže môžeme porovnať, ako rýchlo to auto zistí.

Scenáre:

- **A**: zóny sú stále na tom istom mieste,
- **C**: v polovici behu si zóny vymenia technológie (kde bola zlá Wi-Fi, je zrazu zlé 5G),
- **D**: iné rozloženie zón, ktoré žiadna metóda nikdy predtým „nevidela“,
- **T**: **žiadne zóny**, len realistické mesto s budovami a rušením.

## 10. Skúsili sa aj múdrejšie spôsoby: nepomohli

Otázka znela: „výber podľa kvality“ reaguje až *potom*, čo sa niečo zhorší. Nedalo by sa to **predpovedať dopredu**?

1. **Mapa kvality.** Autá posielajú na server, aká bola kvalita na ktorom mieste, a ostatné autá sa pozrú, aká bude o 1–3 sekundy tam, kam idú. Pri stálych zónach je rovnaká ako výber podľa kvality. **Keď sa podmienky zmenia (scenár C), je horšia**, lebo mapa si pamätá staré hodnoty. V jednom úseku doručovala len 61 zo 100 namiesto 73.
2. **Strojové učenie** (rozhodovacie stromy naučené na záznamoch z jázd). Na záznamoch predpovedalo kvalitu presnejšie. **V živej simulácii však bolo horšie** o 0,5 zo 100. Lepšia predpoveď sa do lepšieho doručenia nepreniesla.
3. **„Bandit“**: metóda zo štatistiky, ako hráč pri hracích automatoch, ktorý skúša, ktorý automat viac platí. Horší vo všetkých scenároch.
4. **Veštec** (odborne orákulum): **nerealizovateľný** podvod. Auto presne vie, kde sú zóny, a vidí aj 3 sekundy do budúcnosti. Slúži len na zmeranie, **koľko by sa dalo získať, keby predpoveď bola dokonalá**. Veštec je lepší než výber podľa kvality len o **2 až 3 zo 100**. Samotné „videnie do budúcnosti“ z toho tvorí len asi 1 zo 100.

**Záver:** ani dokonalá predpoveď by veľa nepridala, lebo jednoduchý výber podľa kvality už dosahuje takmer maximum. Aj toto je platný a užitočný výsledok.

## 11. Výsledky veľkej záverečnej série (10 náhod, 200 behov, asi 2 dni)

Doručené správy o polohe zo 100:

| scenár | auto nerozhoduje (pevne 50/50) | **výber podľa kvality** | mapa | veštec (strop) |
|---|---|---|---|---|
| A (stále zóny) | 65 | **79** | 79 | 81 |
| C (zóny sa vymenia) | 66 | **79** | 76 | 81 |
| D (nevidené zóny) | 73 | **82** | 80 | 84 |
| T (bez zón, len mesto) | 88 | **91** | 90 | – |

Pri výbere podľa kvality sa navyše lepšie doručovali aj správy zo senzorov (o 6 až 12 zo 100) a správy do skrinky pri križovatke (o 7 až 9 zo 100). Skrinka mala o autách čerstvejšie informácie, asi o 0,3 až 0,4 sekundy.

**Všetky tieto zlepšenia sú preukázané.**

## 12. Je to v praxi použiteľné?

Áno. Zóny sú len testovacie podmienky, nie súčasť mechanizmu. Auto o žiadnych zónach nevie. Jednoducho meria, čo mu prichádza, a podľa toho sa rozhodne. V realite sa kvalita líši za budovami, pri rušení a pri výpadkoch.

Poctivo však treba povedať:

- **Čím viac sa technológie líšia, tým viac výber pomôže.** Pri zónach o 8 až 14 zo 100, v obyčajnom simulovanom meste len asi o 2 zo 100 oproti pevnému rozdeleniu a o 5 zo 100 oproti samotnej Wi-Fi. Koľko takých rozdielov je v skutočnom meste, simulácia nepovie.
- Poradové číslo v hlavičke je **malé rozšírenie normy**.
- Skúšobné správy niečo stoja (asi pol správy za sekundu na auto).
- Je to simulácia jednej mapy so 140 autami.

## 13. Ďalšie nápady, ktoré boli vo fronte (náhody 201–205, bez zón)

| nápad | jednoducho | výsledok |
|---|---|---|
| **1. Obchádzka čakania** | keď by správa na Wi-Fi čakala kvôli brzdám proti preplneniu dlhšie než 20 ms, pošle sa cez priame 5G | rovnako dobrý ako výber podľa kvality, ale **bez merania a bez skúšobných správ**. Pri dlhšom čakaní (100 ms) už nepomohol |
| **2. Podľa výbavy susedov** | keď niektoré autá majú len Wi-Fi a iné len 5G, auto vysiela tým, čo majú susedia | **+10 až +14 zo 100** oproti pevnému výberu |
| **3. Podľa skrinky na trase** | keď skrinka pri križovatke má len jednu technológiu, auto pred ňou použije práve tú | skrinka dostane o **16 až 19 zo 100 viac** správ, informácia o aute stará 0,4 s namiesto 5–7 s |
| **4. Blízkym aj cez 5G** | autám veľmi blízko pošle správu oboma technológiami | najlepšia varianta o 1 zo 100 nad výberom podľa kvality, správy zo senzorov však horšie |
| **5. Podľa geometrie** | rozhodovanie podľa vzdialenosti a polohy | horšie než výber podľa kvality |
| **6. Rozdelenie podľa druhu správy** | správy o polohe vždy cez priame 5G, správy zo senzorov vždy cez Wi-Fi | **najväčší zisk: 96 zo 100**, o 5 zo 100 lepšie než výber podľa kvality. Správy zo senzorov trochu horšie. **V zónach sa však nevie prispôsobiť**: správy zo senzorov tam boli o 12 zo 100 horšie |

### Ktorým smerom by sa dalo ísť

1. **Spojiť nápad 6 s výberom podľa kvality:** bežne pevné rozdelenie podľa druhu správy, a keď sa niekde zhorší, reagovať podľa nameranej kvality. Mohlo by to byť najlepšie z oboch. **Zatiaľ to nikto nevyskúšal.**
2. **Nápad 1 vyskúšať v zónach:** je najjednoduchší a najbližší norme. Netreba v ňom merať ani rozširovať hlavičku. Nevieme však, či by stačil aj tam, kde je problém v budovách, a nie v preplnení.
3. **Nápady 2 a 3** sú prakticky veľmi dôležité, lebo ešte roky budú jazdiť vedľa seba autá s rôznou výbavou.

## 14. Celá práca v piatich vetách

1. Auto má tri rádiá a musí si vybrať, ktorým poslať správu. To je téma práce.
2. Skúšalo sa rozhodovať podľa hustoty áut a podľa vyťaženia rádia. **Nefungovalo to**, lebo správy sa v meste strácajú hlavne kvôli budovám, nie kvôli preplneniu.
3. Fungovalo **priamo merať, koľko správ prichádza**, a vybrať technológiu, ktorá doručuje lepšie. Je to preukázateľne lepšie než nerozhodovať. Pri zónach o 8 až 14 zo 100 správ, v obyčajnom meste o 2 až 5 zo 100.
4. Zložitejšie metódy (mapa, strojové učenie, bandit) to neprekonali a ani dokonalá predpoveď budúcnosti by nepridala viac než 2 až 3 zo 100.
5. Popri tom sa našli a opravili chyby v cudzom programe, bez ktorých by žiadne meranie neplatilo. A sú tu dva sľubné smery do budúcnosti: spojiť pevné rozdelenie podľa druhu správy s výberom podľa kvality a rozhodovať podľa výbavy susedov a skriniek.
