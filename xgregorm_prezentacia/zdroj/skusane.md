# Čo všetko som skúšal okrem výberu podľa kvality

Pri každom pokuse je popísané, čo to bolo, ako to fungovalo, čo vyšlo a prečo to (ne)fungovalo. Čísla sú z Denníka v CLAUDE.md. „PRR“ znamená, koľko zo 100 správ o polohe dostali susedia do 100 m. „Body“ sú rozdiely v týchto percentách. Ladiace série mali 3 náhody, finálne 10, nápady 5.

## 1. Algoritmy na šetrenie správ o polohe (október)

**Cieľ:** posielať menej správ, aby sa kanál menej plnil. Ešte to nebol výber siete.

| algoritmus | ako fungoval | výsledok (30 áut) | osud |
|---|---|---|---|
| 1. semafor | auto stojace na červenej posiela menej | len −0,8 % správ | odstránený |
| 2. potvrdenie | cestná jednotka posiela zoznam áut, ktoré počula; nepotvrdené auto správu zopakuje | +20 % vysielaní, straty mierne vyššie | odstránený |
| 3. relevancia | ďalej než 250 m pred ďalším semaforom najviac 1 správa za sekundu | −56 % správ (bez potvrdení −39 %), doručenie rovnaké | v kóde ostal, vo finálnych behoch vypnutý |

**Prečo 1 a 2 nefungovali:** norma vyžaduje aspoň 1 správu za sekundu, takže stojace auto aj tak nemá čo ušetriť. Pri 30 autách bol kanál takmer prázdny a opakovanie ho len zbytočne plnilo.

**Prečo 3 nejde ďalej:** norma dovoľuje obmedziť správy len kvôli preplneniu kanála, nie podľa relevancie. V práci je to odchýlka, ktorú treba uviesť.

## 2. Koordinácia križovatiek po kábli (október)

**Ako fungovala:** cestné jednotky si po kábli posielali, ktoré auto smeruje k susednej križovatke, aby sa jej semafor pripravil vopred.

**Výsledok:** horšie. Pri 30 autách sa najdlhšie státie zvýšilo z 18,8 na 22,4 s a nepomohol ani 5 alebo 10 s predstih.

**Prečo:** jednotka sa o aute aj tak dozvie sama, najneskôr do 1 s, takže informácia od suseda je takmer nová. Algoritmus semafora od autorov pri každej novej informácii hneď prepína, takže viac informácií znamená viac prepnutí a viac žltej.

## 3. Prvé adaptívne pravidlo podľa potvrdení cestnej jednotky (október)

**Ako fungovalo:**

1. Základ bol 802.11p.
2. Cestná jednotka posielala zoznam áut, ktorých správy počula.
3. Keď auto blízko križovatky nebolo v zozname, prepne na 5G. Ak nepomohlo ani to, pridalo zálohu cez mobilnú sieť.
4. Zálohu vyplo až po 1 s dobrého spojenia.

**Výsledok:**

- Výpadky zvládalo výborne: pri výpadku siete auto prešlo inam do sekundy a nestratila sa ani správa.
- V realistickom meste s 280 autami však pridávalo zálohu cez mobilnú sieť stále viac (22 → 54 % správ). Bunka bola plná (92 % času nad 30 %) a oneskorenie 70 ms, najpomalšie 1 % viac ako 1 s.
- Pri kontrole vyšlo najavo, že **60 % správ išlo všetkými tromi sieťami**.

**Prečo to nebolo dobré:** vysoké PRR (v ladení 94–96 %) malo za cenu 2,4- až 3-krát viac správ cez mobilnú sieť a horšie senzorové správy. Nebol to výber, ale zahlcovanie vlastným mechanizmom. Nahradilo ho čisté pravidlo: jedna priama sieť na správu a mobilná sieť rovnako pri všetkých spôsoboch.

## 4. Hustota áut (13. októbra)

**Ako fungovala:** auto si každých 100 ms rátalo susedov, ktorých počulo cez 802.11p a 5G za poslednú sekundu.

| verzia | pravidlo | výsledok |
|---|---|---|
| P1 | nikoho na 802.11p za 2 s → pridaj 5G | 10 áut: autá 450–600 m 17 → 68 %; 30 áut 16 → 32 %; 100 áut nič |
| P1b, P1c | 5G aj vtedy, keď niekoho počujem len cez 5G | dosah ako „všetko naraz“, ale 5G zapnuté v 82–87 % rozhodnutí |
| P1d | ako P1b, len kým je 5G pod 30 % obsadenosti | v bežnej premávke rovnaké ako P1b; pri preplnení obmedzí 5G a zhorší dosah |

**Prečo nefungovala:**

- Pomáhala len vzdialeným autám, nie blízkym. Do 100 m bolo doručenie už predtým takmer 100 % a nezlepšilo sa.
- Pri bežnej premávke sa takmer nespustila: osamotené auto bolo pri 100 autách len v 0,7 % rozhodnutí.
- Prísnejšie verzie zapínali 5G skoro stále, takže od hustoty nezáviseli.
- Kanál nebol preplnený (pod 5 %), hustota sa prejavila ako riedkosť.

## 5. Vyťaženie kanála (20. októbra)

**Ako fungovalo:**

1. Auto si viedlo priemer obsadenosti 802.11p a 5G za poslednú sekundu a od siete dostávalo vyťaženie bunky mobilnej siete.
2. Raz za sekundu, keď bolo 802.11p obsadené viac než 30 % času a 5G menej, auto prešlo na 5G s pravdepodobnosťou (obsadenosť − 0,25) / obsadenosť. Presunula sa tak len časť áut.
3. Späť prešlo, keď obsadenosť klesla pod 20 %.
4. Zálohu cez mobilnú sieť nepridalo, keď bola bunka plná.

**Výsledok (280 áut, 1 beh):**

- Preplnenie 802.11p kleslo z 37 na 24 % času, ale PRR ostalo rovnaké (89,2 %).
- Brána mobilnej siete bola „všetko alebo nič“: všetky autá ju vypli naraz.
- V realistickej konfigurácii (ladenie) sa presun takmer nespustil, lebo 802.11p bolo nad 30 % len 2 % času. PRR 78,7 % oproti 78,4 % pri samotnom 802.11p.

**Prečo nefungovalo:** správy sa strácali za budovami, nie kvôli preplneniu. Odľahčenie kanála straty za rohom neodstráni.

## 6. Mapa kvality (predikcia z máp, november)

**Ako fungovala:**

1. Mesto sa rozdelí na štvorce 20 × 20 m.
2. Autá raz za sekundu hlásia cez mobilnú sieť, koľko správ v danom štvorci prišlo cez 802.11p a cez 5G.
3. Mapa postupne zabúda, staré merania strácajú váhu s časovou konštantou 30 s.
4. Auto z rýchlosti a trasy v navigácii určí, kde bude o 1, 2 a 3 sekundy. Kvalitu siete vypočíta ako priemer vlastného merania a hodnôt mapy v týchto bodoch.
5. Ďalej použije rovnaké pravidlo ako výber podľa kvality.

**Výsledok (finálna séria, 10 náhod), oproti výberu podľa kvality:**

- stále zóny (A): rovnako (+0,1 bodu), o 29 % menej presunov,
- zóny sa menia (C): **−2,9 bodu**,
- nevidené zóny (D): **−1,5 bodu**,
- bez zón (T): rovnako (−0,3).

**Prečo nefungovala:** po zmene zón mapa ešte desiatky sekúnd drží staré hodnoty a posiela autá na sieť, ktorá už zlyháva. V C v 60.–75. sekunde to bolo 60,9 % namiesto 72,5 %. Pri stálych podmienkach nepridá nič, lebo auto tú istú informáciu zmeria samo. Navyše hlásenia do mapy zaťažujú mobilnú sieť.

## 7. Naučený model, logistická regresia (november)

**Ako fungoval:** jednoduchý štatistický model, naučený na záznamoch z behov bez zón, predpovedal kvalitu sietí o 3 s.

**Výsledok:** v zónach o **2,6 bodu horší** než výber podľa kvality. Model naučený priamo na zónach bol bez preukázaného rozdielu (+0,5).

**Prečo:** model, ktorý nevidel zóny, sa naučil iný svet a v novej situácii sa mýli. To je všeobecná slabina učených modelov.

## 8. Výber algoritmu na záznamoch (november)

**Ako:** na záznamoch z behov (bez novej simulácie) som porovnal 6 spôsobov predpovede kvality o 3 s:

- rozhodovacie stromy (GBM),
- malú neurónovú sieť (MLP),
- neurónovú sieť s pamäťou histórie (GRU),
- Kalmanov filter,
- mapu,
- logistickú regresiu.

Vstupy boli len to, čo auto naozaj vie: vlastné merania a obsah prijatých správ (správy zo semafora po sieťach, senzorové správy, susedia zo správ o polohe, vzdialenosť ku križovatke…).

**Výsledok:**

- Najlepšie vyšli stromy. Napríklad v scenári C zvolili sieť so spoľahlivosťou 79,5 % oproti 77,8 % pri „zostane ako teraz“, pri strope 81,7 %.
- Pri podmienkach, ktoré model nevidel, sa však zhoršil (−1,6 bodu).
- Bez zón nepredpovedal nič (+0,08 bodu).
- Neurónové siete ani Kalmanov filter neboli lepšie.

## 9. Naučené stromy priamo v aute (november)

**Ako fungovali:**

- 200 stromov s 28 vstupmi, učené na iných náhodách.
- Predpoveď na 3 s nahradila aktuálne meranie, inak bolo pravidlo rovnaké ako pri výbere podľa kvality.
- Model sa po pohľade na nevidený scenár D už nemenil.

**Výsledok (finálna séria):**

- v scenári D aj bez zón **o 0,5 bodu horšie** (v ladení o 1,3 bodu),
- 2- až 6-krát menej presunov,
- tesne po zmene zón lepšie (D 50.–65. s +1,4 bodu), v ustálenom stave horšie.

**Prečo (hypotéza, neoverená):**

- Model predpovedá, čo auto **prijíma** od susedov, a do doručenia správ, ktoré auto **vysiela**, sa to premieta len nepriamo.
- Predpoveď zmenšuje rozdiel medzi sieťami. Rozdiel aspoň 5 bodov, pri ktorom auto prepne, nastal v D len v 50–55 % prípadov oproti 72–74 % pri meraní, takže auto reaguje pomalšie.
- Lepšia presnosť na záznamoch sa teda do siete nepreniesla.

## 10. Učenie za jazdy, „bandit“ (november)

**Ako fungoval:**

1. Auto rozlišovalo 8 situácií: či je pred semaforom, či stojí, či je okolo veľa áut.
2. Pre každú situáciu a sieť si pamätalo, ako dobre doručovala, s bonusom pre málo vyskúšané možnosti.
3. Skúšobné správy posielalo len vtedy, keď druhú sieť dlho nemeralo.

**Výsledok:**

- oproti výberu podľa kvality −3,2 bodu (A) a −3,5 bodu (C),
- s plnými skúškami o 2,3–2,6 bodu lepšie než bez nich, ale stále o 0,9 bodu horšie než výber podľa kvality.

**Prečo:** osem situácií je príliš hrubé rozdelenie. Priemer „ako to býva pred semaforom“ je horší ukazovateľ než to, čo auto práve teraz meria. Úbytok skúšok vysvetlil väčšinu straty, ale nie celú.

## 11. Veštec, horná hranica (november)

**Ako fungoval:** nerealizovateľný podvod, len na meranie. Variant O0 presne vie, kde sú zóny teraz. Variant O3 vie aj, aké budú o 3 s. Ani jeden nepotrebuje skúšobné správy.

**Výsledok:**

- O0 oproti výberu podľa kvality: +1,0 až +2,1 bodu.
- O3 oproti O0, teda samotné „videnie dopredu“: len +0,7 až +1,1 bodu.

**Čo z toho plynie:** ani dokonalá predpoveď by nepridala viac než 1 bod. Preto predikcia nemala kde získať a výber podľa aktuálnej kvality je blízko maxima.

## 12. Nápady 1–6 (overené na náhodách 201–205)

| nápad | ako funguje | výsledok |
|---|---|---|
| 1. obchádzka čakania | keď by správa na 802.11p čakala kvôli pravidlám proti preplneniu dlhšie než 20 ms, pošle sa cez 5G | ako výber podľa kvality, bez merania a bez skúšok (pri 100 ms nepomohol) |
| 2. výbava susedov | keď autá majú len jednu sieť, vysielaj tou, ktorú majú susedia | +10 až +14 bodov oproti pevnému výberu |
| 3. cestná jednotka na trase | pred jednotkou, ktorá má len jednu sieť, použi práve tú | jednotka dostane o 16–19 bodov viac správ, informácia 0,4 s namiesto 5–7 s |
| 4. kópia blízkym | autám veľmi blízko pošli správu aj cez 5G | pri 100 m o 1 bod nad výberom podľa kvality, senzory horšie |
| 5. podľa geometrie | rozhoduj podľa vzdialenosti a polohy | horšie než výber podľa kvality |
| 6. podľa druhu správy | polohu vždy cez 5G, senzory vždy cez 802.11p | bez zón 95,6 % (o 4,8 bodu nad výberom podľa kvality); v zónach sa nevie prispôsobiť, senzory o 12 bodov horšie |

## Spoločné poučenie

1. **Nepriame ukazovatele** (hustota, vyťaženie) netrafia skutočnú príčinu strát.
2. **Predpoveď dopredu** nemá kde získať, lebo podmienky sa menia rýchlejšie, než ich mapa či model zachytia. Aj dokonalý veštec by pridal najviac asi 1 bod.
3. **Pevné pravidlá** (nápady 1 a 6) sú dobré pri stálych podmienkach. Pri meniacich sa podmienkach vyhráva priame meranie.
