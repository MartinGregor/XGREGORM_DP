# Text k prezentácii: výber siete podľa nameranej kvality

Čo povedať pri každej snímke prezentácie xgregorm_priebeh_prace.pptx (16 snímok). Ten istý text je aj v poznámkach pre prednášajúceho v PowerPointe. Čísla sú prevzaté z Denníka v CLAUDE.md. Porovnanie s jednou sieťou a so „všetkým naraz“, podiel áut na sieťach a počty správ sú prepočítané z logov série idea1 (mesto bez zón, náhody 201–205, priemer 5 behov).

## Snímka 1: Výber siete pre komunikáciu áut podľa nameranej kvality

Moja práca rieši, ktorou sieťou má auto poslať správu, keď má na výber tri. Navrhol som spôsob, ktorý sa rozhoduje podľa toho, ako dobre jednotlivé siete práve doručujú. V simulácii mesta som ukázal, že je lepší než pevné nastavenia.

## Snímka 2: Ako som sa dostal k výsledku

Prácu som robil v piatich krokoch. Najprv som musel opraviť chyby v simulátore, inak by žiadne meranie neplatilo. Potom som skúsil rozhodovať podľa hustoty áut a podľa vyťaženia siete, ale nepomohlo to. Tretí pokus, rozhodovanie podľa nameranej kvality, pomohol. Skúsil som ešte predpovedať kvalitu dopredu, no ukázalo sa, že to je zlý smer. Preto ostávam pri výbere podľa kvality a ten idem ďalej zlepšovať.

## Snímka 3: Tri siete, z ktorých auto vyberá

Auto niekoľkokrát za sekundu vysiela správu o polohe: kde je, ako rýchlo ide a kam. Okrem nej posiela správy zo senzorov a križovatky posielajú stav semafora. Na to má tri siete. **Wi-Fi pre autá** (802.11p) má veľmi rýchly samotný prenos, ale krátky dosah, budovy ju tlmia a v plnom meste správy čakajú vo fronte, takže priemerné oneskorenie je asi 25 ms. **Priame 5G** (NR-V2X) dosiahne ďalej, je však pomalšie a pri veľa autách sa správy zrážajú. **Mobilná sieť** (LTE) dosiahne všade, ale ide cez vysielač na streche a server len do cestnej jednotky, takže susedné autá správu nedostanú. Polovica správ príde do 22 ms, ale keď je bunka plná, 5 % mešká viac ako sekundu. Žiadna sieť nie je najlepšia všade, preto treba vyberať.

## Snímka 4: Aké správy sa posielajú

Nie všetko, čo sa posiela, je správa o polohe. Sú štyri druhy správ. **Správu o polohe** posiela každé auto asi trikrát za sekundu: kde je, ako rýchlo a kam ide, aké je veľké a ako zrýchľuje. **Správu zo senzorov** posiela auto s kamerou a radarom, v simulácii každé. Hovorí, čo auto vidí okolo seba, aj autá, ktoré samy rádio nemajú. Chodí asi osemkrát za sekundu a je väčšia, takže najviac plní kanál. Cestná jednotka pri križovatke posiela **stav semafora**, teda aká farba svieti a kedy sa zmení, desaťkrát za sekundu, a raz za sekundu **mapu križovatky** s jazdnými pruhmi. Auto vyberá sieť len pre svoje správy, teda polohu a senzory. Správy zo semafora posiela cestná jednotka vždy oboma priamymi sieťami. Kvalitu siete auto meria zo správ o polohe, lebo len tie nesú poradové číslo.

## Snímka 5: Simulácia mesta a scenáre

Všetko beží v simulácii. Program SUMO simuluje premávku, program ns-3 rádiá a spája ich projekt VaN3Twin od iných autorov, do ktorého som dopisoval vlastné časti. Mapa je centrum mesta s 9 križovatkami so semaformi a pri každej stojí **cestná jednotka** (na obrázku RSU), ktorá riadi semafor. Jazdí 140 áut, beh trvá 120 sekúnd a meria sa od 30. sekundy. Sú tam budovy, mestský model šírenia signálu, podpis správ, senzorové správy všetkých áut, rušenie medzi Wi-Fi a 5G a pravidlá proti preplneniu kanála podľa normy. Aby bolo vidno, kedy výber pomáha, pridal som **zóny**: kruhy s polomerom 150 metrov, kde jedna sieť stratí 70 % správ (na obrázku tyrkysová je zlá Wi-Fi, oranžová zlé 5G). V scenári A sú zóny stále, v C sa v 60. sekunde vymenia, D má rozloženie, ktoré žiadna metóda nevidela, a T zóny nemá. Hlavné číslo je **spoľahlivosť doručenia**: zo 100 správ o polohe, ktoré mali prísť autám do 100 metrov, koľko prišlo. Porovnávam vždy v pároch s rovnakou náhodou, s viacerými náhodami a s 95 % intervalom istoty.

## Snímka 6: Krok 1, opravil som chyby v simulátore

Program autorov mal chyby, ktoré skresľovali výsledky. Najväčšia: autá, ktoré vznikli v rovnakom okamihu, vysielali presne naraz a prekrikovali sa. Pri 30 autách sa doručila len asi tretina správ, po oprave 99,7 %. Druhá: každá cestná jednotka riadila všetky semafory v meste, takže si ich navzájom prepínali a autá stáli vyše 100 sekúnd. Po oprave riadi každá len svoj semafor a najdlhšie státie kleslo na 17 až 24 sekúnd. Ďalej som opravil zamrznutie simulácie pri prázdnej križovatke, obrátené smery zelenej na piatich z deviatich križovatiek a tri chyby v module 5G, pre ktoré simulácia padala pri viac ako 255 autách.

## Snímka 7: Krok 2, hustota a vyťaženie nepomohli

Prvý nápad: auto, ktoré okolo seba nepočuje nikoho, pridá priame 5G, aby ho počuli ďalej. Pri 10 autách to pomohlo vzdialeným autám, pri 30 menej a pri 100 vôbec. K blízkym autám to nepomohlo nikdy. Druhý nápad: keď je Wi-Fi preplnená, presunúť časť áut na 5G. Preplnenie kleslo, ale spoľahlivosť ostala rovnaká. Dôvod je, že správy sa v meste strácajú hlavne kvôli budovám, nie kvôli preplneniu. Hustota aj vyťaženie problém len nepriamo naznačujú. **Poučenie: treba merať priamo výsledok, teda koľko správ naozaj prišlo.**

## Snímka 8: Krok 3, prepínač medzi sieťami

Aby auto mohlo vyberať, postavil som prepínač. Služba, ktorá vytvára správy o polohe, rozhodne podľa normy, **kedy** správu poslať. Prepínač potom rozhodne, **ktorou sieťou** odíde. Pre susedné autá ide každá správa práve jednou priamou sieťou: buď Wi-Fi, alebo priamym 5G. Tú volám **hlavná sieť**. Druhou sieťou ide len občasná skúšobná správa, o ktorej hovorím o chvíľu. Mobilná sieť sa pridá len tam, kde to potrebuje križovatka, teda keď je auto na príjazde ďalej než 150 metrov od nej. Toto pravidlo je rovnaké pri všetkých porovnávaných spôsoboch, takže mobilnú sieť nezaťažujem viac. Keď niektorá sieť vypadne, prepínač ju nahradí okamžite, bez straty správy. Senzorové správy idú tiež hlavnou sieťou.

## Snímka 9: Ako auto meria kvalitu

Auto si meria kvalitu každej siete samo, z toho, čo prijíma. Každá správa nesie malé **poradové číslo** a značku, ktorou sieťou odišla. Je to jeden bajt v nevyužitom poli hlavičky, takže správa sa nezväčší. Keď od suseda prídu správy 1, 2, 4, 5, auto vie, že trojka sa stratila. Počítajú sa len susedia do 100 metrov a len správy, ktoré prišli do 100 milisekúnd, lebo neskoro doručená správa o polohe je nepoužiteľná. Výsledkom je podiel doručených správ zvlášť pre Wi-Fi a pre 5G, s pamäťou asi 2 sekundy. Aby auto poznalo aj sieť, ktorú práve nepoužíva, pošle cez ňu približne raz za dve sekundy **skúšobnú správu**. Predpokladám, že ak ja dobre počujem susedov cez nejakú sieť, aj oni počujú mňa.

## Snímka 10: Kedy auto prepne na druhú sieť

Rozhoduje sa raz za sekundu. Na začiatku má polovica áut ako hlavnú sieť Wi-Fi a polovica 5G, presne ako pri náhodnom rozdelení. Auto prepne na druhú sieť, len ak platia tri podmienky:

1. kvalitu oboch sietí pozná, teda má aspoň 10 očakávaných správ od susedov,
2. druhá sieť doručuje lepšie aspoň o 5 zo 100 správ,
3. druhá sieť nie je preplnená: kanál je obsadený menej než 30 % času, čo je hranica z normy.

Ak to platí, auto prepne len s určitou pravdepodobnosťou: **zisk delený 0,4**. Keď napríklad 5G doručuje o 20 bodov lepšie, za sekundu prepne polovica áut. Je to preto, aby celé mesto neskočilo naraz na jednu sieť a nepreplnilo ju.

## Snímka 11: Koľko áut je na ktorej sieti

Tento graf ukazuje, akú hlavnú sieť mali autá. Pri pevných voľbách je to jednoduché: všetky autá na Wi-Fi, všetky na 5G, alebo náhodne polovica a polovica (v behoch presne 47,7 % Wi-Fi a 52,3 % 5G). Pri „všetkom naraz“ posiela každé auto oboma sieťami. Výber podľa kvality začne ako polovica a polovica, no za prvých päť sekúnd sa veľa áut presunie na Wi-Fi. Potom sa to ustáli okolo 60 % Wi-Fi a 40 % 5G (priemer od 30. sekundy 59,6 % a 40,4 %). Prečo práve tak? Kanál 5G sa pri tejto záťaži plní rýchlejšie a pri polovici áut je obsadený v priemere 36 % času, teda nad hranicou 30 %. Správy sa na ňom zrážajú, autá to namerajú a prejdú na Wi-Fi. Keď obsadenosť 5G klesne k 30 %, presuny sa zastavia, lebo pravidlo nepustí autá na preplnenú sieť. Pri výbere podľa kvality je 5G obsadené v priemere 29 % času, pri samotnom 5G a pri všetkom naraz 57 %. Výber podľa kvality teda sám nájde rozdelenie, pri ktorom ani jedna sieť nie je zahltená.

## Snímka 12: Kam sa správy strácajú

To isté v počte správ. Každé auto pošle v priemere 3,4 správy o polohe za sekundu a každú má dostať asi 9 susedov do 100 metrov. Za 90 sekúnd merania je to asi 385 tisíc doručení v jednom behu. Graf ukazuje, koľko z nich prišlo a koľko sa stratilo. Pri samotnom 5G sa stratí 95 tisíc, teda štvrtina, lebo kanál je zahltený a správy sa zrážajú. Pri samotnej Wi-Fi sa stratí 56 tisíc, hlavne kvôli budovám. Pri náhodnom rozdelení 42 tisíc a pri **výbere podľa kvality 36 tisíc, teda zo 100 doručení sa stratí 9**. Pri všetkom naraz sa stratí 21 tisíc, ale tam má každá správa dva pokusy, cez Wi-Fi aj cez 5G. Rozdiely výberu podľa kvality oproti pevným voľbám sú v tabuľke a všetky sú preukázané.

## Snímka 13: Prečo „všetko naraz“ dá viac správ o polohe, ale horšie ostatné

Ako je možné, že pri všetkom naraz príde 94,5 % správ o polohe, ale senzorové správy vychádzajú horšie? Je to tým, ako sa počítajú. **Správa o polohe sa počíta ako doručená, ak príde aspoň jedna z jej kópií.** Pri všetkom naraz ide každá cez Wi-Fi aj cez 5G, takže má dva pokusy, a keď jedna kópia zlyhá, druhá ju často zachráni. **Senzorové správy a správy zo semafora sa počítajú za každú kópiu zvlášť.** Pri všetkom naraz idú aj senzorové správy oboma sieťami, takže na kanáloch je dvakrát toľko prevádzky a 5G je obsadené 57 % času namiesto 29 %. Preto sa každá jednotlivá kópia doručí horšie: senzorová správa cez 5G v 73 % prípadov namiesto 92 %, cez Wi-Fi v 74 % namiesto 80 %. Rovnako dopadnú správy zo semafora. Koľko senzorových správ prišlo aspoň jednou kópiou, simulácia nemerala, preto netvrdím, že senzory celkovo dopadli horšie. Isté je, že všetko naraz zahltí kanály, každá kópia trpí a cez mobilnú sieť ide 4,2-krát viac správ.

## Snímka 14: Výsledok so zónami

Keď sú v meste miesta, kde jedna sieť zlyháva, je prínos výberu oveľa väčší (10 náhod). Oproti náhodnému rozdeleniu doručí výber podľa kvality o 13,4 bodu viac správ o polohe v scenári A, o 12,9 v C, o 8,1 v D a o 2,1 v meste bez zón. Zároveň sa lepšie doručujú senzorové správy (o 6 až 12 bodov) aj správy do cestnej jednotky (o 7 až 9 bodov). Na porovnanie: auto, ktoré by zóny vopred presne poznalo (čo v realite nejde), by bolo lepšie len o ďalšie 2 až 3 body. Jednoduchý výber podľa kvality je teda blízko maxima.

## Snímka 15: Krok 4, predikcia je zlý smer

Skúšal som, či by pomohlo predpovedať kvalitu dopredu. **Mapa kvality**, do ktorej autá hlásia, kde ako dobre doručovali, vyšla pri stálych zónach rovnako ako výber podľa kvality. Keď sa však zóny zmenili, držala staré hodnoty a hneď po zmene doručovala len 60,9 % správ namiesto 72,5 %. **Naučené rozhodovacie stromy** predpovedali kvalitu na záznamoch presnejšie, ale v živej simulácii boli o pol bodu horšie. A hlavne: aj dokonalé videnie 3 sekundy dopredu by pridalo len 0,7 až 1,1 bodu. Na predikciu teda nie je priestor, preto ostávam pri výbere podľa aktuálnej kvality.

## Snímka 16: Záver a čo ďalej

Opravil som simulátor a ukázal, že nepriame ukazovatele, ako je hustota a vyťaženie, nepomáhajú. Výber podľa priamo nameranej kvality je preukázateľne lepší než jedna sieť aj než náhodné rozdelenie. Sám nájde rozdelenie áut, pri ktorom nie je zahltená ani jedna sieť, a na rozdiel od posielania všetkými sieťami kanály nepreplní. Najviac pomáha tam, kde sa kvalita sietí v meste líši. Ďalej ho chcem zlepšovať:

1. kombinovať ho s pevným rozdelením podľa druhu správy,
2. znížiť počet skúšobných správ,
3. overiť ho pri autách, ktoré nemajú všetky siete.

Obmedzenia: je to simulácia jednej mapy a zóny sú umelý pokus, nie model skutočnej fyziky.
