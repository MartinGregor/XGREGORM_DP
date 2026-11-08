# Prezentácia priebehu práce

**`xgregorm_priebeh_prace.pptx`** (40 snímok, 16:9) zhŕňa celú prácu od prvého spustenia VaN3Twin
(7. 10. 2026) po vyhodnotenie niekoľkodňovej fronty behov (7. 11. 2026). Každé číslo je prevzaté
z Denníka v `CLAUDE.md` alebo z `xgregorm_PLAN_PREDIKCIA.md`, nič nie je dopočítané navyše.

## Obsah

| Časť | Snímky | Čo obsahuje |
|---|---|---|
| Úvod | 1–5 | titul, obsah a tri strany skratiek (siete, správy a metriky, politiky a označenia) |
| 01 Začiatky (7.–11. 10.) | 6–12 | VaN3Twin a TraCI, nezdokumentované pravidlá, mapy, chyba synchronizovaných CAM, boj RSU o semafory, algoritmy CAM podľa ETSI |
| 02 Hybridná sieť (12.–23. 10.) | 13–20 | prepínač ciest pod GeoNet, porovnanie technológií, pokus s hustotou, opravy 5G-LENA, mestský kanál, výber podľa kvality (Q), obrátené fázy semaforov |
| 03 Plný realizmus a plán (1.–3. 11.) | 21–24 | rušenie, DCC, výbava, kalibrácia, metodika (seedy, párový t-test, Holm, kritérium), scenáre A/C/D/T |
| 04 Alternatívy k Q (3.–4. 11.) | 25–28 | mapa, naučené stromy, bandit, orákulum; mapa po zmene; záznamy vs. sieť |
| 05 Niekoľkodňová fronta (4.–7. 11.) | 29–36 | spúšťač a 538 behov, finálna séria, rozdiely voči Q, časový priebeh D, nápady 1–6 a ich výsledky a ceny |
| Záver | 37–40 | celkový záver, obmedzenia, kde čo nájsť |

Grafy sú natívne grafy PowerPointu, dajú sa upraviť (pravý klik → Upraviť údaje). Poznámky
pre prednášajúceho sú pri titulnej a prvej sekčnej snímke.

## Ako vznikla a ako ju znova vygenerovať

Zdroj je v `zdroj/`:

- `build.js` – celá prezentácia ako skript (knižnica pptxgenjs): téma, layouty, každá snímka, dáta grafov.
- `mapfig.py` – obrázky mapy city-center s RSU a zónami scenára A (`img/map.png`, `img/mapA.png`),
  kreslené priamo z `src/automotive/examples/xgregorm_city_center/` (net.xml a stations.xml).
- `preview.py` – hrubý náhľad snímok bez LibreOffice: písma Calibri a Cambria z Windows,
  hlási text, ktorý sa nezmestí do rámčeka, a tvary mimo snímky.

```bash
cd xgregorm_prezentacia/zdroj
npm install pptxgenjs@3.12.0 react react-dom react-icons   # ikony sa prevádzajú cez cairosvg v ~/xg_venv
~/xg_venv/bin/python mapfig.py                               # obrázky mapy (cesty v skripte upraviť na img/)
NODE_PATH=$PWD/node_modules node build.js ../xgregorm_priebeh_prace.pptx
~/xg_venv/bin/python preview.py ../xgregorm_priebeh_prace.pptx /tmp/nahlad
```

`build.js` volá `apply_theme.js` zo skillu pptx v Claude Code (zapíše farby témy do súboru);
na inom počítači treba túto cestu v hlavičke skriptu upraviť alebo riadok s `applyTheme` vynechať.
Kontrola: validátor Office XML (bez chýb) a náhľad (bez pretečenia textu). Vizuálna kontrola
v LibreOffice nebola možná, grafy sa preto overili len obsahom, nie vykreslením.
