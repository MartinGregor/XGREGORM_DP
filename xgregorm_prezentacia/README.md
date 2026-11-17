# Prezentácia a vysvetlenie práce

- **`xgregorm_priebeh_prace.pptx`** (16 snímok + 1 záložná, 16:9): skrátená prezentácia zameraná na výber siete podľa
  nameranej kvality. Text, čo povedať pri každej snímke, je v poznámkach pre prednášajúceho. Snímka 17 je záložná,
  odpoveď na otázku z obhajoby o presnom čase (meranie do 100 ms).
- **`xgregorm_vysvetlenie_prace.docx`** (Word): 1. časť = ten istý text k prezentácii po snímkach,
  2. časť = podrobné vysvetlenia (druhy správ, rozdiel sietí, cesta cez mobilnú sieť, výpočet obsadenosti, budovy,
  obsah správy o polohe, prepínač, hustota, „všetko naraz“, rozdelenie áut, počty správ),
  3. časť = pripomienky z obhajoby a rozšírenia (presný čas, prečo 802.11p nie je Wi-Fi, 1000 áut, ten istý pokus v OMNeT++),
  4. časť = čo všetko som skúšal okrem výberu podľa kvality a prečo to nefungovalo, 5. časť = vysvetlenie celej práce bez skratiek.

Čísla sú prevzaté z Denníka v `CLAUDE.md`. Porovnanie výberu podľa kvality s jednou sieťou, s náhodným
rozdelením a so „všetkým naraz“, podiel áut na sieťach, obsadenosť kanálov a počty správ sú prepočítané z logov `xgregorm_logs/idea1/` (náhody 201–205).
Pôvodná dlhá prezentácia (40 snímok, 2026-11-08) je v histórii gitu (commit `0ad05920`).

## Obsah prezentácie

| Snímky | Čo obsahuje |
|---|---|
| 1–5 | titul, cesta k výsledku v 5 krokoch, tri siete (Wi-Fi pre autá, priame 5G, mobilná sieť), druhy správ, simulácia mesta a scenáre so zónami |
| 6–7 | krok 1: opravy simulátora (grafy pred/po); krok 2: hustota a vyťaženie nepomohli (grafy) |
| 8–10 | krok 3: prepínač medzi sieťami, ako auto meria kvalitu, kedy prepne (podmienky a pravdepodobnosť) |
| 11–14 | koľko áut je na ktorej sieti (a obsadenosť kanálov), kam sa správy strácajú (doručené / stratené oproti len 5G, len Wi-Fi, 50/50, všetko naraz), prečo „všetko naraz“ dá viac správ o polohe, ale horšie kópie, výsledok so zónami |
| 15–16 | krok 4: predikcia je zlý smer (časový priebeh, rozdiely); záver, ďalšie kroky, obmedzenia |
| 17 (záložná) | ako auto vie, že správa prišla do 100 ms: hodiny GNSS, čas vzniku v CAM, výsledok s nepresnými hodinami |

Grafy sú natívne grafy PowerPointu (pravý klik → Upraviť údaje).

## Ako znova vygenerovať

Zdroj je v `zdroj/`:

- `build.js`: prezentácia ako skript (pptxgenjs); poznámky berie z `text_prezentacie.md`.
- `text_prezentacie.md`: text k snímkam (sekcie `## Snímka N: ...`).
- `podrobnosti.md`: podrobné vysvetlenia (2. časť Wordu).
- `rozsirenia.md`: pripomienky z obhajoby a rozšírenia (3. časť Wordu).
- `skusane.md`: všetko vyskúšané okrem výberu podľa kvality (4. časť Wordu).
- `vysvetlenie.md`: vysvetlenie celej práce (5. časť Wordu).
- `clk.json`: čísla pre záložnú snímku 17 (z `xgregorm_clk_eval.py`, séria `xgregorm_logs/clk/`).
- `build_docx.js` + `fix_docx.py`: prevod Markdown → Word.
- `mapfig.py`: obrázky mapy (`img/map.png`, `img/mapA.png`); `preview.py`: hrubý náhľad snímok a kontrola pretečenia textu.

```bash
cd xgregorm_prezentacia
npm install --prefix <priečinok> pptxgenjs@3.12.0 react@18.3.1 react-dom@18.3.1 react-icons@5.2.1 jszip@3.10.1 docx@9.0.2
NM=<priečinok>/node_modules
(cd zdroj && NODE_PATH=$NM node build.js ../xgregorm_priebeh_prace.pptx)       # ikony cez cairosvg v ~/xg_venv
NODE_PATH=$NM node zdroj/build_docx.js zdroj/text_prezentacie.md zdroj/podrobnosti.md zdroj/rozsirenia.md zdroj/skusane.md zdroj/vysvetlenie.md xgregorm_vysvetlenie_prace.docx
~/xg_venv/bin/python zdroj/fix_docx.py xgregorm_vysvetlenie_prace.docx            # odstráni nepripojený fontTable.xml
~/xg_venv/bin/python zdroj/preview.py xgregorm_priebeh_prace.pptx /tmp/nahlad    # náhľad a pretečenie textu
```

`build.js` volá `apply_theme.js` zo skillu pptx v Claude Code (zapíše farby témy). Na inom počítači treba túto cestu
v hlavičke skriptu upraviť alebo riadok s `applyTheme` vynechať. Kontrola: validátor Office XML (bez chýb) a náhľad
(bez pretečenia textu). Vykreslenie v LibreOffice nebolo možné, grafy sú overené len obsahom.
