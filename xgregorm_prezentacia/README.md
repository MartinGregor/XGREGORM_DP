# Prezentácia a vysvetlenie práce

- **`xgregorm_priebeh_prace.pptx`** (15 snímok, 16:9): skrátená prezentácia zameraná na výber siete podľa nameranej
  kvality. Text, čo povedať pri každej snímke, je v poznámkach pre prednášajúceho.
- **`xgregorm_vysvetlenie_prace.docx`** (Word): 1. časť = ten istý text k prezentácii po snímkach,
  2. časť = podrobné vysvetlenie celej práce bez skratiek.

Čísla sú prevzaté z Denníka v `CLAUDE.md`. Porovnanie výberu podľa kvality s jednou sieťou, s náhodným
rozdelením a so „všetkým naraz“ je prepočítané z logov `xgregorm_logs/idea1/` (náhody 201–205).
Pôvodná dlhá prezentácia (40 snímok, 2026-11-08) je v histórii gitu (commit `0ad05920`).

## Obsah prezentácie

| Snímky | Čo obsahuje |
|---|---|
| 1–4 | titul, cesta k výsledku v 5 krokoch, tri siete (Wi-Fi pre autá, priame 5G, mobilná sieť), simulácia mesta |
| 5–6 | krok 1: opravy simulátora (grafy pred/po); krok 2: hustota a vyťaženie nepomohli (grafy) |
| 7–9 | krok 3: prepínač medzi sieťami, ako auto meria kvalitu, kedy prepne (podmienky a pravdepodobnosť) |
| 10–13 | ako sa testovalo (zóny, scenáre A, C, D, T), výsledok bez zón oproti len 5G / len Wi-Fi / 50/50 / všetko naraz, cena „všetko naraz“, výsledok so zónami |
| 14–15 | krok 4: predikcia je zlý smer (časový priebeh, rozdiely); záver, ďalšie kroky, obmedzenia |

Grafy sú natívne grafy PowerPointu (pravý klik → Upraviť údaje).

## Ako znova vygenerovať

Zdroj je v `zdroj/`:

- `build.js`: prezentácia ako skript (pptxgenjs); poznámky berie z `text_prezentacie.md`.
- `text_prezentacie.md`: text k snímkam (sekcie `## Snímka N: ...`).
- `vysvetlenie.md`: podrobné vysvetlenie práce.
- `build_docx.js` + `fix_docx.py`: prevod Markdown → Word.
- `mapfig.py`: obrázky mapy (`img/map.png`, `img/mapA.png`); `preview.py`: hrubý náhľad snímok a kontrola pretečenia textu.

```bash
cd xgregorm_prezentacia
npm install --prefix <priečinok> pptxgenjs@3.12.0 react@18.3.1 react-dom@18.3.1 react-icons@5.2.1 jszip@3.10.1 docx@9.0.2
NM=<priečinok>/node_modules
(cd zdroj && NODE_PATH=$NM node build.js ../xgregorm_priebeh_prace.pptx)       # ikony cez cairosvg v ~/xg_venv
NODE_PATH=$NM node zdroj/build_docx.js zdroj/text_prezentacie.md zdroj/vysvetlenie.md xgregorm_vysvetlenie_prace.docx
~/xg_venv/bin/python zdroj/fix_docx.py xgregorm_vysvetlenie_prace.docx            # odstráni nepripojený fontTable.xml
~/xg_venv/bin/python zdroj/preview.py xgregorm_priebeh_prace.pptx /tmp/nahlad    # náhľad a pretečenie textu
```

`build.js` volá `apply_theme.js` zo skillu pptx v Claude Code (zapíše farby témy). Na inom počítači treba túto cestu
v hlavičke skriptu upraviť alebo riadok s `applyTheme` vynechať. Kontrola: validátor Office XML (bez chýb) a náhľad
(bez pretečenia textu). Vykreslenie v LibreOffice nebolo možné, grafy sú overené len obsahom.
