# Text k prezentácii: výber siete podľa nameranej kvality

Čo povedať pri každej snímke prezentácie xgregorm_priebeh_prace.pptx (15 snímok). Ten istý text je aj v poznámkach pre prednášajúceho v PowerPointe. Čísla sú prevzaté z Denníka v CLAUDE.md, porovnanie s jednou sieťou a so „všetkým naraz“ je prepočítané z logov série idea1 (náhody 201–205).

## Snímka 1: Výber siete pre komunikáciu áut podľa nameranej kvality

Moja práca rieši, ktorou sieťou má auto poslať správu, keď má na výber tri. Navrhol som spôsob, ktorý sa rozhoduje podľa toho, ako dobre jednotlivé siete práve doručujú. V simulácii mesta som ukázal, že je lepší než pevné nastavenia.

## Snímka 2: Ako som sa dostal k výsledku

Prácu som robil v piatich krokoch. Najprv som musel opraviť chyby v simulátore, inak by žiadne meranie neplatilo. Potom som skúsil rozhodovať podľa hustoty áut a podľa vyťaženia siete, ale nepomohlo to. Tretí pokus, rozhodovanie podľa nameranej kvality, pomohol. Skúsil som ešte predpovedať kvalitu dopredu, no ukázalo sa, že to je zlý smer. Preto ostávam pri výbere podľa kvality a ten idem ďalej zlepšovať.

## Snímka 3: Tri siete, z ktorých auto vyberá

Auto niekoľkokrát za sekundu vysiela správu o polohe: kde je, ako rýchlo ide a kam. Okrem nej posiela správy zo senzorov a križovatky posielajú stav semafora. Na to má tri siete. **Wi-Fi pre autá** (802.11p) je veľmi rýchla, ale má krátky dosah a budovy ju tlmia. **Priame 5G** (NR-V2X) dosiahne ďalej, je však pomalšie a pri veľa autách sa správy zrážajú. **Mobilná sieť** (LTE) dosiahne všade, ale ide cez vysielač a server, takže susedné autá správu priamo nedostanú. Žiadna sieť nie je najlepšia všade, preto treba vyberať.

## Snímka 4: Simulácia mesta

Všetko beží v simulácii. Program SUMO simuluje premávku, program ns-3 rádiá. Spája ich projekt VaN3Twin od iných autorov, do ktorého som dopisoval vlastné časti. Mapa je centrum mesta s 9 križovatkami so semaformi a pri každej stojí **cestná jednotka** (na obrázkoch RSU), ktorá riadi semafor. Jazdí 140 áut, beh trvá 120 sekúnd a meria sa od 30. sekundy, keď sa premávka rozbehne. Aby to bolo realistické, sú tam budovy, mestský model šírenia signálu, podpis správ, senzorové správy všetkých áut, vzájomné rušenie Wi-Fi a 5G a pravidlá proti preplneniu kanála podľa normy. Hlavné číslo je **spoľahlivosť doručenia**: zo 100 správ o polohe, ktoré mali prísť autám do 100 metrov, koľko naozaj prišlo.

## Snímka 5: Krok 1, opravil som chyby v simulátore

Program autorov mal chyby, ktoré skresľovali výsledky. Najväčšia: autá, ktoré vznikli v rovnakom okamihu, vysielali presne naraz a prekrikovali sa. Pri 30 autách sa doručila len asi tretina správ, po oprave 99,7 %. Druhá: každá cestná jednotka riadila všetky semafory v meste, takže si ich navzájom prepínali a autá stáli vyše 100 sekúnd. Po oprave riadi každá len svoj semafor a najdlhšie státie kleslo na 17 až 24 sekúnd. Ďalej som opravil zamrznutie simulácie pri prázdnej križovatke, obrátené smery zelenej na piatich z deviatich križovatiek a tri chyby v module 5G, pre ktoré simulácia padala pri viac ako 255 autách.

## Snímka 6: Krok 2, hustota a vyťaženie nepomohli

Prvý nápad: auto, ktoré okolo seba nepočuje nikoho, pridá priame 5G, aby ho počuli ďalej. Pri 10 autách to pomohlo vzdialeným autám, pri 30 menej a pri 100 vôbec. K blízkym autám to nepomohlo nikdy. Druhý nápad: keď je Wi-Fi preplnená, presunúť časť áut na 5G. Preplnenie kleslo, ale spoľahlivosť ostala rovnaká. Dôvod je, že správy sa v meste strácajú hlavne kvôli budovám, nie kvôli preplneniu. Hustota aj vyťaženie problém len nepriamo naznačujú. **Poučenie: treba merať priamo výsledok, teda koľko správ naozaj prišlo.**

## Snímka 7: Krok 3, prepínač medzi sieťami

Aby auto mohlo vyberať, postavil som prepínač. Služba, ktorá vytvára správy o polohe, rozhodne podľa normy, **kedy** správu poslať. Prepínač potom rozhodne, **ktorou sieťou** odíde. Pre susedné autá ide každá správa práve jednou priamou sieťou: buď Wi-Fi, alebo priamym 5G. Tú volám **hlavná sieť**. Druhou sieťou ide len občasná skúšobná správa, o ktorej hovorím o chvíľu. Mobilná sieť sa pridá len tam, kde to potrebuje križovatka, teda keď je auto na príjazde ďalej než 150 metrov od nej. Toto pravidlo je rovnaké pri všetkých porovnávaných spôsoboch, takže mobilnú sieť nezaťažujem viac. Keď niektorá sieť vypadne, prepínač ju nahradí okamžite, bez straty správy. Senzorové správy idú tiež hlavnou sieťou.

## Snímka 8: Ako auto meria kvalitu

Auto si meria kvalitu každej siete samo, z toho, čo prijíma. Každá správa nesie malé **poradové číslo** a značku, ktorou sieťou odišla. Je to jeden bajt v nevyužitom poli hlavičky, takže správa sa nezväčší. Keď od suseda prídu správy 1, 2, 4, 5, auto vie, že trojka sa stratila. Počítajú sa len susedia do 100 metrov a len správy, ktoré prišli do 100 milisekúnd, lebo neskoro doručená správa o polohe je nepoužiteľná. Výsledkom je podiel doručených správ zvlášť pre Wi-Fi a pre 5G, s pamäťou asi 2 sekundy. Aby auto poznalo aj sieť, ktorú práve nepoužíva, pošle cez ňu približne raz za dve sekundy **skúšobnú správu**. Predpokladám, že ak ja dobre počujem susedov cez nejakú sieť, aj oni počujú mňa.

## Snímka 9: Kedy auto prepne na druhú sieť

Rozhoduje sa raz za sekundu. Na začiatku má polovica áut ako hlavnú sieť Wi-Fi a polovica 5G, presne ako pri náhodnom rozdelení. Auto prepne na druhú sieť, len ak platia tri podmienky:

1. kvalitu oboch sietí pozná, teda má aspoň 10 očakávaných správ od susedov,
2. druhá sieť doručuje lepšie aspoň o 5 zo 100 správ,
3. druhá sieť nie je preplnená: kanál je obsadený menej než 30 % času, čo je hranica z normy.

Ak to platí, auto prepne len s určitou pravdepodobnosťou: **zisk delený 0,4**. Keď napríklad 5G doručuje o 20 bodov lepšie, za sekundu prepne polovica áut. Je to preto, aby celé mesto neskočilo naraz na jednu sieť a nepreplnilo ju.

## Snímka 10: Ako som to testoval

Aby bolo vidno, kedy výber pomáha, urobil som **zóny**: kruhy s polomerom 150 metrov okolo niektorých križovatiek, kde jedna sieť stratí 70 % správ. V dvoch zónach je zlá Wi-Fi, v dvoch 5G. Predstavujú miesta, ako je priestor za budovou, rušenie alebo výpadok. V scenári A sú zóny stále. V scenári C sa v 60. sekunde vymenia. Scenár D má iné rozloženie, ktoré žiadna metóda predtým nevidela. Scenár T nemá zóny vôbec, je to len samotné mesto. Každé porovnanie som zopakoval s 10 rôznymi náhodami a porovnával som v pároch s rovnakou náhodou, s 95 % intervalom istoty. Kritérium úspechu som stanovil vopred a na ladenie som použil iné náhody než na finálne meranie.

## Snímka 11: Výsledok bez zón, kvalita oproti pevným voľbám

Toto je hlavné porovnanie v meste bez zón, s 5 náhodami. Keď majú všetky autá len priame 5G, doručí sa 75,2 % správ o polohe. Pri samotnej Wi-Fi je to 85,5 % a pri náhodnom rozdelení pol na pol 89,0 %. **Výber podľa kvality dosiahne 90,8 %.** Je to o 15,6 bodu viac než pri samotnom 5G, o 5,3 bodu viac než pri samotnej Wi-Fi a o 1,8 bodu viac než pri náhodnom rozdelení. Všetky tri rozdiely sú preukázané. Senzorové správy sa pri výbere podľa kvality doručujú lepšie než pri jednej sieti, ale o 1 bod horšie než pri náhodnom rozdelení. Keby autá mali len mobilnú sieť, susedné autá by si nedoručili nič, lebo správa ide len na server. To ukázal už skorší test. Posielanie všetkými sieťami naraz dá 94,5 %, ale za cenu, ktorú ukazuje ďalšia snímka.

## Snímka 12: Cena posielania všetkými sieťami naraz

Posielať každú správu všetkými tromi sieťami vyzerá ako najistejšie riešenie, ale má veľkú cenu. Cez mobilnú sieť ide 4,2-krát viac správ. Kanály sú preplnenejšie, takže oproti výberu podľa kvality sa doručí o 10,4 bodu menej senzorových správ a o 11,7 bodu menej správ zo semaforov. Správy o polohe sú síce o 3,7 bodu lepšie a do cestnej jednotky o 3,8 bodu, ale ostatné služby výrazne trpia. Výber podľa kvality posiela každú správu len jednou priamou sieťou a mobilnú sieť nezaťažuje viac než pevné voľby.

## Snímka 13: Výsledok so zónami

Keď sú v meste miesta, kde jedna sieť zlyháva, je prínos výberu oveľa väčší (10 náhod). Oproti náhodnému rozdeleniu doručí výber podľa kvality o 13,4 bodu viac správ o polohe v scenári A, o 12,9 v C, o 8,1 v D a o 2,1 v meste bez zón. Zároveň sa lepšie doručujú senzorové správy (o 6 až 12 bodov) aj správy do cestnej jednotky (o 7 až 9 bodov). Na porovnanie: auto, ktoré by zóny vopred presne poznalo (čo v realite nejde), by bolo lepšie len o ďalšie 2 až 3 body. Jednoduchý výber podľa kvality je teda blízko maxima.

## Snímka 14: Krok 4, predikcia je zlý smer

Skúšal som, či by pomohlo predpovedať kvalitu dopredu. **Mapa kvality**, do ktorej autá hlásia, kde ako dobre doručovali, vyšla pri stálych zónach rovnako ako výber podľa kvality. Keď sa však zóny zmenili, držala staré hodnoty a hneď po zmene doručovala len 60,9 % správ namiesto 72,5 %. **Naučené rozhodovacie stromy** predpovedali kvalitu na záznamoch presnejšie, ale v živej simulácii boli o pol bodu horšie. A hlavne: aj dokonalé videnie 3 sekundy dopredu by pridalo len 0,7 až 1,1 bodu. Na predikciu teda nie je priestor, preto ostávam pri výbere podľa aktuálnej kvality.

## Snímka 15: Záver a čo ďalej

Opravil som simulátor a ukázal, že nepriame ukazovatele, ako je hustota a vyťaženie, nepomáhajú. Výber podľa priamo nameranej kvality je preukázateľne lepší než jedna sieť aj než náhodné rozdelenie a na rozdiel od posielania všetkými sieťami nepoškodzuje ostatné služby. Najviac pomáha tam, kde sa kvalita sietí v meste líši. Ďalej ho chcem zlepšovať:

1. kombinovať ho s pevným rozdelením podľa druhu správy,
2. znížiť počet skúšobných správ,
3. overiť ho pri autách, ktoré nemajú všetky siete.

Obmedzenia: je to simulácia jednej mapy a zóny sú umelý pokus, nie model skutočnej fyziky.
