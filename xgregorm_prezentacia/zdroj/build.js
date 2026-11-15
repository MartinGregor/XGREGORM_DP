// xgregorm: skratena prezentacia (15 snimok) zamerana na vyber siete podla namerane kvality (Q).
// Cisla: Dennik v CLAUDE.md; porovnanie Q s FW/FN/MIX/ALL prepocitane z xgregorm_logs/idea1 (seedy 201-205).
// Poznamky pre prednasajuceho sa beru zo zdroj/text_prezentacie.md (sekcie "## Snímka N: ...").
// Spustenie: NODE_PATH=<node_modules: pptxgenjs@3.12.0 react react-dom react-icons jszip> node build.js out.pptx
const pptxgen = require('pptxgenjs');
const React = require('react');
const RDS = require('react-dom/server');
const { execFileSync } = require('child_process');
const fa = require('react-icons/fa');
const fs = require('fs');
const path = require('path');
const { applyTheme } = require('/home/marti/.claude/skills/synced/5869e613-1ca4-4b3d-bc5c-18e5a33d9888_825bbd1e-c49b-4409-ac72-7f83a2186ff6/pptx/scripts/apply_theme.js');

const OUT = process.argv[2] || 'deck.pptx';
const IMG = path.join(__dirname, 'img');

// ---------- poznamky z textu ----------
const NOTES = {};
{
  const md = fs.readFileSync(path.join(__dirname, 'text_prezentacie.md'), 'utf8');
  const parts = md.split(/^## Snímka (\d+): .*$/m);
  for (let i = 1; i < parts.length; i += 2)
    NOTES[+parts[i]] = parts[i + 1].trim().replace(/\*\*/g, '').replace(/\n{2,}/g, '\n\n');
}

const THEME = {
  name: 'xgregorm V2X',
  headFontFace: 'Cambria',
  bodyFontFace: 'Calibri',
  colors: {
    dk1: '1B2730', lt1: 'FFFFFF', dk2: '0B3C49', lt2: 'EEF3F4',
    accent1: '2A9D8F', accent2: 'E76F51', accent3: 'F2A541', accent4: '7B6FB0', accent5: '5C7480', accent6: 'B8C2C7',
    hlink: '2A9D8F', folHlink: '7B6FB0',
  },
};
const HEX = THEME.colors;
// farby sieti a politik (rovnake na vsetkych snimkach)
const COL = { wifi: HEX.accent1, nr: HEX.accent2, lte: HEX.accent4, q: HEX.dk2, mix: HEX.accent6, all: HEX.accent3, gray: HEX.accent5 };

const pres = new pptxgen();
pres.layout = 'LAYOUT_WIDE'; // 13.333 x 7.5
pres.author = 'Martin Gregor';
pres.title = 'Výber siete pre komunikáciu áut podľa nameranej kvality';
pres.subject = 'Diplomová práca, VaN3Twin (ns-3 + SUMO)';
pres.theme = { headFontFace: THEME.headFontFace, bodyFontFace: THEME.bodyFontFace };
const C = pres.SchemeColor;
const W = 13.333, M = 0.6;

// ---------- layouty ----------
const footer = (dark) => [
  { text: { text: 'Výber siete podľa nameranej kvality · diplomová práca', options: { x: M, y: 7.02, w: 8, h: 0.3, fontSize: 10, color: dark ? C.background2 : C.accent5, margin: 0 } } },
];
pres.defineSlideMaster({
  title: 'COVER', background: { color: C.text2 },
  objects: [
    { placeholder: { options: { name: 'title', type: 'title', x: M, y: 1.6, w: 8.6, h: 2.4, fontSize: 40, bold: true, color: C.background1, valign: 'bottom', margin: 0 }, text: '' } },
    { placeholder: { options: { name: 'body', type: 'body', x: M, y: 4.25, w: 8.6, h: 1.6, fontSize: 18, color: C.background2, valign: 'top', margin: 0 }, text: '' } },
  ],
});
pres.defineSlideMaster({
  title: 'CONTENT', background: { color: C.background1 },
  objects: [
    ...footer(false),
    { placeholder: { options: { name: 'title', type: 'title', x: M, y: 0.35, w: W - 2 * M, h: 0.85, fontSize: 30, bold: true, color: C.text2, valign: 'middle', margin: 0 }, text: '' } },
  ],
  slideNumber: { x: 12.2, y: 7.0, w: 0.6, h: 0.3, fontSize: 10, color: C.accent5, align: 'right' },
});

// ---------- pomocne funkcie ----------
const iconCache = {};
async function icon(name, hex) {
  const key = name + hex;
  if (iconCache[key]) return iconCache[key];
  const svg = RDS.renderToStaticMarkup(React.createElement(fa[name], { color: '#' + hex, size: 256 }));
  const buf = execFileSync(process.env.HOME + '/xg_venv/bin/python', ['-c', 'import sys,cairosvg;sys.stdout.buffer.write(cairosvg.svg2png(bytestring=sys.stdin.buffer.read(),output_width=256,output_height=256))'], { input: svg });
  return (iconCache[key] = 'image/png;base64,' + buf.toString('base64'));
}
let objN = 0;
const nm = (p) => `${p}-${++objN}`;
async function iconCircle(s, name, x, y, d, fill, iconHex) {
  s.addShape(pres.shapes.OVAL, { x, y, w: d, h: d, fill: { color: fill }, line: { color: fill }, objectName: nm('icon-bg') });
  const p = d * 0.24;
  s.addImage({ data: await icon(name, iconHex || 'FFFFFF'), x: x + p, y: y + p, w: d - 2 * p, h: d - 2 * p, objectName: nm('icon'), altText: name });
}
function card(s, x, y, w, h, fill) {
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y, w, h, rectRadius: 0.08, fill: { color: fill || C.background2 }, line: { color: fill || C.background2 }, objectName: nm('card') });
}
function text(s, t, o) {
  s.addText(t, Object.assign({ isTextBox: true, margin: 0, fontSize: 15, color: C.text1, valign: 'top', objectName: nm('text') }, o));
}
function bullets(s, items, o) {
  const runs = items.map((it, i) => {
    const arr = Array.isArray(it) ? it : [{ text: it }];
    return arr.map((r, j) => ({ text: r.text, options: Object.assign({}, r.options || {}, j === 0 ? { bullet: { indent: 16 } } : {}, j === arr.length - 1 && i < items.length - 1 ? { breakLine: true } : {}) }));
  }).flat();
  s.addText(runs, Object.assign({ isTextBox: true, margin: 0, fontSize: 15, color: C.text1, valign: 'top', paraSpaceAfter: 6, objectName: nm('list') }, o));
}
const b = (t) => ({ text: t, options: { bold: true } });
const r = (t) => ({ text: t });
let slideNo = 0;
function content(title, section) {
  const s = pres.addSlide({ masterName: 'CONTENT', sectionTitle: section });
  s.addText(title, { placeholder: 'title' });
  if (NOTES[++slideNo]) s.addNotes(NOTES[slideNo]);
  return s;
}
function chartBase(extra) {
  return Object.assign({
    catAxisLabelColor: HEX.accent5, valAxisLabelColor: HEX.accent5,
    catAxisLabelFontFace: '+mn-lt', valAxisLabelFontFace: '+mn-lt', catAxisLabelFontSize: 12, valAxisLabelFontSize: 11,
    dataLabelFontFace: '+mn-lt', dataLabelFontSize: 12, dataLabelColor: HEX.dk1,
    legendFontFace: '+mn-lt', legendFontSize: 12, legendColor: HEX.dk1,
    titleFontFace: '+mn-lt', titleFontSize: 14, titleColor: HEX.dk2,
    valGridLine: { color: 'DDE3E6', size: 0.75 }, catGridLine: { style: 'none' },
    catAxisLineShow: true, valAxisLineShow: false,
  }, extra);
}
// male vysvetlenie pod grafom
function source(s, t, y) {
  text(s, t, { x: M, y: y || 6.62, w: W - 2 * M, h: 0.32, fontSize: 11, color: C.accent5, italic: true });
}
// zvyrazneny box so zaverom
function takeaway(s, t, x, y, w, h) {
  s.addShape(pres.shapes.RECTANGLE, { x, y, w, h, fill: { color: C.background2 }, line: { color: C.background2 }, objectName: nm('take-bg') });
  s.addShape(pres.shapes.RECTANGLE, { x, y, w: 0.09, h, fill: { color: C.accent1 }, line: { color: C.accent1 }, objectName: nm('take-bar') });
  text(s, t, { x: x + 0.3, y: y + 0.12, w: w - 0.45, h: h - 0.24, fontSize: 15, valign: 'middle' });
}

(async () => {
  const S0 = 'Úvod', S1 = 'Cesta k výsledku', S2 = 'Prepínač a pravidlo', S3 = 'Výsledky', S4 = 'Záver';

  // ============ 1. Titul ============
  pres.addSection({ title: S0 });
  {
    const s = pres.addSlide({ masterName: 'COVER', sectionTitle: S0 });
    slideNo++;
    s.addText('Výber siete pre komunikáciu áut podľa nameranej kvality', { placeholder: 'title' });
    s.addText('Diplomová práca · simulácia VaN3Twin (ns-3 + SUMO) · Martin Gregor', { placeholder: 'body' });
    s.addNotes(NOTES[1]);
    const ic = [['FaWifi', COL.wifi], ['FaSatelliteDish', COL.nr], ['FaBroadcastTower', COL.lte]];
    for (let i = 0; i < 3; i++) await iconCircle(s, ic[i][0], 10.2 + (i % 2) * 1.35, 1.7 + i * 1.3, 1.15, ic[i][1]);
  }

  // ============ 2. Cesta ============
  {
    const s = content('Ako som sa dostal k výsledku', S0);
    const steps = [
      ['FaTools', 'Opravil som simulátor', 'chyby autorov skresľovali všetky merania', COL.gray, 'nutný základ'],
      ['FaUsers', 'Hustota a vyťaženie', 'rozhodovanie podľa počtu áut a preplnenia', COL.nr, 'nepomohlo'],
      ['FaCheckCircle', 'Nameraná kvalita', 'auto meria, koľko správ naozaj prišlo', COL.wifi, 'pomohlo'],
      ['FaBinoculars', 'Predikcia dopredu', 'mapa kvality, naučené stromy', COL.nr, 'zlý smer'],
      ['FaArrowUp', 'Zlepšovať kvalitu', 'tu pokračujem', COL.q, 'ďalší krok'],
    ];
    const x0 = M, step = (W - 2 * M) / 5;
    s.addShape(pres.shapes.LINE, { x: x0 + step / 2, y: 2.35, w: step * 4, h: 0, line: { color: C.accent6, width: 3 }, objectName: 'timeline' });
    for (let i = 0; i < 5; i++) {
      const cx = x0 + step * i + step / 2;
      await iconCircle(s, steps[i][0], cx - 0.6, 1.75, 1.2, steps[i][3]);
      text(s, String(i + 1), { x: cx - 1.1, y: 1.6, w: 0.45, h: 0.45, fontSize: 20, bold: true, color: C.accent5, fontFace: THEME.headFontFace });
      text(s, steps[i][1], { x: cx - step / 2 + 0.1, y: 3.15, w: step - 0.2, h: 0.75, fontSize: 18, bold: true, color: C.text2, align: 'center' });
      text(s, steps[i][2], { x: cx - step / 2 + 0.15, y: 3.9, w: step - 0.3, h: 0.9, fontSize: 14, align: 'center' });
      s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: cx - 0.95, y: 4.95, w: 1.9, h: 0.5, rectRadius: 0.25, fill: { color: steps[i][3] }, line: { color: steps[i][3] }, objectName: nm('verdict') });
      text(s, steps[i][4], { x: cx - 0.95, y: 4.95, w: 1.9, h: 0.5, fontSize: 14, bold: true, color: C.background1, align: 'center', valign: 'middle' });
    }
    takeaway(s, 'Hlavná myšlienka: nehádať príčinu strát (hustota, preplnenie), ale priamo merať, koľko správ prichádza každou sieťou, a podľa toho vybrať.', M, 5.85, W - 2 * M, 0.85);
  }

  // ============ 3. Tri siete ============
  {
    const s = content('Tri siete, z ktorých auto vyberá', S0);
    const nets = [
      ['FaWifi', COL.wifi, 'Wi-Fi pre autá', '802.11p', [['ako ide', 'priamo autám okolo'], ['oneskorenie', 'prenos 0,3 ms, v plnom meste ~25 ms'], ['výhoda', 'rýchly prenos, počúva pred vysielaním'], ['slabina', 'krátky dosah, budovy ju tlmia']]],
      ['FaSatelliteDish', COL.nr, 'Priame 5G', 'NR-V2X', [['ako ide', 'priamo autám okolo'], ['oneskorenie', '~20 ms (čaká na svoje políčko)'], ['výhoda', 'väčší dosah, odolnejšia'], ['slabina', 'pri veľa autách sa správy zrážajú']]],
      ['FaBroadcastTower', COL.lte, 'Mobilná sieť', 'LTE', [['ako ide', 'vysielač → server → križovatka'], ['oneskorenie', '~70–90 ms'], ['výhoda', 'dosiahne všade'], ['slabina', 'susedné autá ju nedostanú']]],
    ];
    const cw = 3.85, gap = (W - 2 * M - 3 * cw) / 2;
    for (let i = 0; i < 3; i++) {
      const x = M + i * (cw + gap), y = 1.45;
      card(s, x, y, cw, 4.1);
      await iconCircle(s, nets[i][0], x + 0.3, y + 0.3, 0.95, nets[i][1]);
      text(s, [{ text: nets[i][2], options: { bold: true, fontSize: 20, color: C.text2, breakLine: true } }, { text: nets[i][3], options: { fontSize: 14, color: C.accent5 } }], { x: x + 1.45, y: y + 0.32, w: cw - 1.6, h: 0.95, valign: 'middle' });
      nets[i][4].forEach(([k, v], j) => {
        text(s, [{ text: k + ': ', options: { bold: true } }, { text: v }], { x: x + 0.3, y: y + 1.55 + j * 0.62, w: cw - 0.55, h: 0.58, fontSize: 14 });
      });
    }
    text(s, 'Čo autá posielajú', { x: M, y: 5.8, w: 3.0, h: 0.4, fontSize: 16, bold: true, color: C.text2 });
    text(s, [b('správa o polohe'), r(' (1–10× za sekundu: kde som, ako rýchlo, kam)  ·  '), b('správa zo senzorov'), r(' (čo vidí kamera)  ·  '), b('správa zo semafora'), r(' (kedy bude zelená)')], { x: M, y: 6.2, w: W - 2 * M, h: 0.4, fontSize: 14 });
  }

  // ============ 4. Simulacia a scenare ============
  {
    const s = content('Simulácia mesta a scenáre so zónami', S0);
    const mw = 5.5;
    s.addImage({ path: path.join(IMG, 'mapA.png'), x: M, y: 1.3, w: mw, h: mw * 821 / 970, objectName: 'map-zones-A', altText: 'Mapa city-center, scenár A: zóny pri RSU 0 a 5 (zlá Wi-Fi) a pri RSU 2 a 8 (zlé 5G)' });
    text(s, [{ text: '● zlá Wi-Fi   ', options: { color: C.accent1, bold: true } }, { text: '● zlé 5G   ', options: { color: C.accent2, bold: true } }, { text: 'RSU = cestná jednotka pri semafore', options: { color: C.accent5, italic: true } }], { x: M, y: 1.3 + mw * 821 / 970 + 0.05, w: mw, h: 0.32, fontSize: 12, align: 'center' });
    const X = 6.45, RW = W - M - 6.45;
    bullets(s, [
      [b('140 áut, 120 s, '), r('meria sa od 30. s')],
      [b('budovy a mestský signál '), r('(357 budov, model 3GPP pre mesto)')],
      [b('plná prevádzka: '), r('podpis správ, senzory všetkých áut, semafory, mobily')],
      [b('rušenie '), r('medzi Wi-Fi a 5G, pravidlá proti preplneniu podľa normy')],
    ], { x: X, y: 1.35, w: RW, h: 1.75, fontSize: 14, paraSpaceAfter: 3 });
    text(s, [b('Zóna'), r(' = kruh 150 m, kde jedna sieť stratí 70 % správ. Umelý, kontrolovaný pokus: miesto za budovou, rušenie, výpadok.')], { x: X, y: 3.15, w: RW, h: 0.65, fontSize: 14 });
    const sc = [['A', 'stále zóny (obrázok)'], ['C', 'v 60. s sa siete v zónach vymenia'], ['D', 'rozloženie, ktoré žiadna metóda nevidela'], ['T', 'bez zón, len mesto']];
    sc.forEach(([k, t], i) => {
      const y = 3.9 + i * 0.45;
      s.addShape(pres.shapes.OVAL, { x: X, y, w: 0.38, h: 0.38, fill: { color: i === 3 ? C.accent5 : C.accent1 }, line: { color: i === 3 ? C.accent5 : C.accent1 }, objectName: nm('sc-badge') });
      text(s, k, { x: X, y, w: 0.38, h: 0.38, fontSize: 13, bold: true, color: C.background1, align: 'center', valign: 'middle', fontFace: THEME.headFontFace });
      text(s, t, { x: X + 0.55, y: y + 0.02, w: RW - 0.55, h: 0.36, fontSize: 14, valign: 'middle' });
    });
    s.addShape(pres.shapes.RECTANGLE, { x: X, y: 5.8, w: RW, h: 0.85, fill: { color: C.text2 }, line: { color: C.text2 }, objectName: 'metric-bg' });
    text(s, [{ text: 'Hlavné číslo: spoľahlivosť doručenia', options: { bold: true, fontSize: 15, breakLine: true } }, { text: 'zo 100 správ o polohe, ktoré mali prísť autám do 100 m, koľko prišlo', options: { fontSize: 13 } }], { x: X + 0.2, y: 5.85, w: RW - 0.3, h: 0.75, color: C.background1, valign: 'middle' });
  }

  // ============ 5. Opravy ============
  pres.addSection({ title: S1 });
  {
    const s = content('Krok 1: opravil som chyby v simulátore', S1);
    s.addChart(pres.charts.BAR, [
      { name: 'pred opravou', labels: ['rýchlostné zóny', 'riadenie semaforov'], values: [34.7, 31.3] },
      { name: 'po oprave', labels: ['rýchlostné zóny', 'riadenie semaforov'], values: [99.7, 99.7] },
    ], chartBase({ x: M, y: 1.35, w: 4.6, h: 4.1, barDir: 'col', barGapWidthPct: 60, chartColors: [C.accent2, C.accent1], valAxisMinVal: 0, valAxisMaxVal: 100, valAxisMajorUnit: 25, showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '0.0', showLegend: true, legendPos: 'b', showTitle: true, title: 'Autá vysielali naraz: doručené správy [%], 30 áut', objectName: 'chart-desync' }));
    s.addChart(pres.charts.BAR, [
      { name: 'pred opravou', labels: ['10 áut', '30 áut'], values: [104.1, 105.8] },
      { name: 'po oprave', labels: ['10 áut', '30 áut'], values: [17.1, 23.8] },
    ], chartBase({ x: 5.4, y: 1.35, w: 4.4, h: 4.1, barDir: 'col', barGapWidthPct: 60, chartColors: [C.accent2, C.accent1], valAxisMinVal: 0, valAxisMaxVal: 120, valAxisMajorUnit: 30, showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '0.0', showLegend: true, legendPos: 'b', showTitle: true, title: 'Jednotky sa bili o semafory: najdlhšie státie [s]', objectName: 'chart-tls' }));
    text(s, 'Ďalšie opravy', { x: 10.1, y: 1.45, w: 2.7, h: 0.45, fontSize: 18, bold: true, color: C.text2 });
    bullets(s, [
      'zamrznutie pri prázdnej križovatke (delenie nulou)',
      'obrátená zelená na 5 z 9 križovatiek: zápchy',
      '3 chyby modulu 5G: pád pri viac ako 255 autách',
    ], { x: 10.1, y: 1.95, w: 2.7, h: 3.4, fontSize: 14 });
    takeaway(s, 'Bez týchto opráv by porovnanie sietí meralo chyby simulátora, nie siete. Rýchlostné zóny a riadenie semaforov sú dve aplikácie autorov.', M, 5.7, W - 2 * M, 0.8);
    source(s, 'Denník 2026-10-07 až 10-23; najdlhšie státie = priemer 5 behov s rôznou náhodou.', 6.6);
  }

  // ============ 6. Hustota a vytazenie ============
  {
    const s = content('Krok 2: hustota a vyťaženie nepomohli', S1);
    s.addChart(pres.charts.BAR, [
      { name: 'bez pravidla', labels: ['10 áut', '30 áut', '100 áut'], values: [17, 16, 15] },
      { name: 'osamotené auto pridá 5G', labels: ['10 áut', '30 áut', '100 áut'], values: [68, 32, 16] },
    ], chartBase({ x: M, y: 1.35, w: 6.0, h: 4.0, barDir: 'col', barGapWidthPct: 60, chartColors: [C.accent6, C.accent2], valAxisMinVal: 0, valAxisMaxVal: 100, valAxisMajorUnit: 25, showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '0', showLegend: true, legendPos: 'b', showTitle: true, title: 'Hustota: doručené správy autám 450–600 m ďaleko [%]', objectName: 'chart-density' }));
    s.addChart(pres.charts.BAR, [
      { name: 'bez pravidla', labels: ['čas, keď je Wi-Fi preplnená', 'doručené správy'], values: [37, 89.2] },
      { name: 'pri preplnení časť áut na 5G', labels: ['čas, keď je Wi-Fi preplnená', 'doručené správy'], values: [24, 89.2] },
    ], chartBase({ x: 6.8, y: 1.35, w: 5.95, h: 4.0, barDir: 'col', barGapWidthPct: 60, chartColors: [C.accent6, C.accent2], valAxisMinVal: 0, valAxisMaxVal: 100, valAxisMajorUnit: 25, showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '0.0', showLegend: true, legendPos: 'b', showTitle: true, title: 'Vyťaženie: preplnenie kleslo, doručenie nie [%]', objectName: 'chart-load' }));
    takeaway(s, 'Správy sa v meste strácajú hlavne kvôli budovám, nie kvôli preplneniu. Hustota a vyťaženie príčinu len nepriamo naznačujú, preto treba merať priamo výsledok.', M, 5.55, W - 2 * M, 0.9);
    source(s, 'Skoršie testy: hustota na menšej mape (5 alebo 3 náhody), vyťaženie 280 áut, 1 beh. K blízkym autám (do 100 m) hustota nepomohla ani pri 10 autách.', 6.55);
  }

  // ============ 7. Prepinac ============
  pres.addSection({ title: S2 });
  {
    const s = content('Krok 3: prepínač medzi sieťami', S2);
    const layers = [['Služba správ o polohe', 'podľa normy rozhodne, KEDY poslať', C.accent5], ['Sieťová vrstva normy (GeoNet)', 'adresovanie, hlavičky', C.accent5], ['Môj prepínač', 'rozhodne, KTOROU sieťou', C.text2]];
    layers.forEach(([t, d, col], i) => {
      const y = 1.5 + i * 1.1;
      s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: M, y, w: 4.3, h: 0.92, rectRadius: 0.08, fill: { color: col }, line: { color: col }, objectName: nm('layer') });
      text(s, [{ text: t, options: { bold: true, fontSize: 17, breakLine: true } }, { text: d, options: { fontSize: 13 } }], { x: M + 0.25, y: y + 0.08, w: 3.9, h: 0.78, color: C.background1, valign: 'middle' });
      if (i < 2) s.addShape(pres.shapes.DOWN_ARROW, { x: M + 2.0, y: y + 0.93, w: 0.3, h: 0.17, fill: { color: C.accent6 }, line: { color: C.accent6 }, objectName: nm('arrow-down') });
    });
    s.addShape(pres.shapes.RIGHT_ARROW, { x: 5.0, y: 3.73, w: 0.7, h: 0.45, fill: { color: C.accent3 }, line: { color: C.accent3 }, objectName: 'arrow-switch' });
    // rozvetvenie od prepinaca ku trom sietam
    s.addShape(pres.shapes.LINE, { x: 5.75, y: 1.98, w: 0, h: 2.2, line: { color: C.accent3, width: 2.5 }, objectName: 'branch-v' });
    for (let i = 0; i < 3; i++) s.addShape(pres.shapes.LINE, { x: 5.75, y: 1.98 + i * 1.1, w: 0.25, h: 0, line: { color: C.accent3, width: 2.5 }, objectName: nm('branch-h') });
    const paths = [
      ['FaWifi', COL.wifi, 'Wi-Fi pre autá', 'hlavná sieť pre susedov (možnosť 1)'],
      ['FaSatelliteDish', COL.nr, 'Priame 5G', 'hlavná sieť pre susedov (možnosť 2)'],
      ['FaBroadcastTower', COL.lte, 'Mobilná sieť', 'len pre križovatku: auto ďalej než 150 m na príjazde'],
    ];
    for (let i = 0; i < 3; i++) {
      const y = 1.5 + i * 1.1;
      await iconCircle(s, paths[i][0], 6.0, y + 0.03, 0.85, paths[i][1]);
      text(s, [{ text: paths[i][2], options: { bold: true, fontSize: 16, color: C.text2, breakLine: true } }, { text: paths[i][3], options: { fontSize: 13 } }], { x: 7.0, y, w: 5.75, h: 0.9, valign: 'middle' });
    }
    const rules = [
      [b('Jedna priama sieť na správu. '), r('Každá správa o polohe ide susedom buď Wi-Fi, alebo 5G (tú volám hlavná sieť). Druhou ide len občasná skúšobná správa.')],
      [b('Mobilná sieť rovnako pri všetkých spôsoboch. '), r('Porovnanie preto nezvýhodňuje nikoho a mobilnú sieť nezaťažujem viac.')],
      [b('Výpadok siete = okamžitá náhrada. '), r('Správa nikdy neodíde do vypadnutej siete.')],
      [b('Senzorové správy '), r('idú tiež hlavnou sieťou.')],
    ];
    bullets(s, rules, { x: M, y: 4.95, w: W - 2 * M, h: 1.65, fontSize: 14, paraSpaceAfter: 4 });
  }

  // ============ 8. Meranie kvality ============
  {
    const s = content('Ako auto meria kvalitu každej siete', S2);
    text(s, 'Správy od jedného suseda, ktoré prišli cez Wi-Fi', { x: M, y: 1.45, w: 7.0, h: 0.4, fontSize: 15, bold: true, color: C.text2 });
    const seq = [1, 2, 3, 4, 5, 6];
    seq.forEach((n, i) => {
      const lost = n === 3;
      const x = M + i * 1.12, y = 1.95;
      s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y, w: 0.95, h: 0.95, rectRadius: 0.1, fill: { color: lost ? C.background1 : C.accent1 }, line: { color: lost ? C.accent2 : C.accent1, width: lost ? 2 : 1, dashType: lost ? 'dash' : 'solid' }, objectName: nm('seq') });
      text(s, String(n), { x, y, w: 0.95, h: 0.95, fontSize: 26, bold: true, color: lost ? C.accent2 : C.background1, align: 'center', valign: 'middle', fontFace: THEME.headFontFace });
    });
    text(s, '„trojka chýba = stratila sa“', { x: M + 2 * 1.12 - 0.5, y: 2.98, w: 2.2, h: 0.35, fontSize: 13, italic: true, color: C.accent2, align: 'center' });
    // vysledok
    card(s, M, 3.55, 6.6, 1.35);
    text(s, [{ text: 'Wi-Fi: 5 zo 6 = 83 %', options: { bold: true, fontSize: 20, color: C.accent1, breakLine: true } }, { text: 'to isté zvlášť pre 5G, zo všetkých susedov naraz', options: { fontSize: 14 } }], { x: M + 0.3, y: 3.65, w: 6.1, h: 1.15, valign: 'middle' });
    const pts = [
      ['FaHashtag', 'Poradové číslo a značka siete', 'jeden bajt v nevyužitom poli hlavičky, správa sa nezväčší'],
      ['FaRulerHorizontal', 'Len susedia do 100 m', 'na nich záleží pre bezpečnosť'],
      ['FaStopwatch', 'Len správy do 100 ms', 'neskorá správa o polohe je nepoužiteľná (definícia 3GPP)'],
      ['FaHistory', 'Pamäť asi 2 sekundy', 'staršie správy postupne strácajú váhu'],
      ['FaFlask', 'Skúšobná správa 0,5× za s', 'druhou sieťou, aby auto poznalo aj ju'],
    ];
    for (let i = 0; i < 5; i++) {
      const y = 1.45 + i * 0.98;
      await iconCircle(s, pts[i][0], 7.65, y, 0.66, COL.q);
      text(s, [{ text: pts[i][1], options: { bold: true, fontSize: 15, color: C.text2, breakLine: true } }, { text: pts[i][2], options: { fontSize: 13 } }], { x: 8.5, y: y - 0.04, w: 4.25, h: 0.88 });
    }
    takeaway(s, 'Predpoklad: ak auto dobre počuje susedov cez nejakú sieť, aj susedia dobre počujú jeho.', M, 5.25, 6.6, 0.85);
  }

  // ============ 9. Kedy prepne ============
  {
    const s = content('Kedy auto prepne na druhú sieť', S2);
    text(s, 'Raz za sekundu. Začiatok: polovica áut na Wi-Fi, polovica na 5G (ako náhodné rozdelenie).', { x: M, y: 1.35, w: W - 2 * M, h: 0.4, fontSize: 15, color: C.accent5, italic: true });
    const steps = [
      ['1', 'Pozná kvalitu oboch sietí?', 'aspoň 10 očakávaných správ od susedov'],
      ['2', 'Druhá sieť lepšia aspoň o 5 bodov?', 'napr. Wi-Fi 78 %, 5G 93 % → zisk 15 bodov'],
      ['3', 'Druhá sieť nie je preplnená?', 'kanál obsadený menej než 30 % času (hranica z normy)'],
      ['4', 'Prepne so šancou zisk / 0,4', 'zisk 15 bodov → 37,5 % áut za sekundu'],
    ];
    const bw = 2.7, gap = (W - 2 * M - 4 * bw) / 3;
    for (let i = 0; i < 4; i++) {
      const x = M + i * (bw + gap), y = 2.0;
      s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y, w: bw, h: 2.2, rectRadius: 0.08, fill: { color: i === 3 ? C.text2 : C.background2 }, line: { color: i === 3 ? C.text2 : C.background2 }, objectName: nm('flow') });
      s.addShape(pres.shapes.OVAL, { x: x + 0.2, y: y + 0.2, w: 0.6, h: 0.6, fill: { color: C.accent1 }, line: { color: C.accent1 }, objectName: nm('flow-num') });
      text(s, steps[i][0], { x: x + 0.2, y: y + 0.2, w: 0.6, h: 0.6, fontSize: 20, bold: true, color: C.background1, align: 'center', valign: 'middle', fontFace: THEME.headFontFace });
      text(s, steps[i][1], { x: x + 0.2, y: y + 0.9, w: bw - 0.4, h: 0.65, fontSize: 14, bold: true, color: i === 3 ? C.background1 : C.text2 });
      text(s, steps[i][2], { x: x + 0.2, y: y + 1.55, w: bw - 0.4, h: 0.6, fontSize: 12, color: i === 3 ? C.background2 : C.text1 });
      if (i < 3) {
        s.addShape(pres.shapes.RIGHT_ARROW, { x: x + bw + 0.02, y: y + 0.9, w: gap - 0.04, h: 0.4, fill: { color: C.accent3 }, line: { color: C.accent3 }, objectName: nm('flow-arrow') });
        text(s, 'nie → ostane', { x: x + 0.2, y: y + 2.3, w: bw - 0.4, h: 0.3, fontSize: 12, italic: true, color: C.accent2 });
      }
    }
    s.addChart(pres.charts.LINE, [
      { name: 'podiel áut, ktoré za sekundu prepnú', labels: ['5', '10', '15', '20', '25', '30', '35', '40'], values: [12.5, 25, 37.5, 50, 62.5, 75, 87.5, 100] },
    ], chartBase({ x: M, y: 4.75, w: 6.2, h: 1.85, chartColors: [C.text2], lineSize: 3, lineDataSymbol: 'circle', lineDataSymbolSize: 7, valAxisMinVal: 0, valAxisMaxVal: 100, valAxisMajorUnit: 50, showLegend: false, showTitle: true, title: 'Zisk v bodoch → % áut, ktoré prepnú za 1 s', titleFontSize: 12, catAxisLabelFontSize: 11, objectName: 'chart-prob' }));
    takeaway(s, 'Prečo náhoda: keby pri zisku prepli všetky autá naraz, celé mesto skočí na jednu sieť a preplní ju. Takto sa presúvajú postupne.', 7.1, 4.85, W - M - 7.1, 1.6);
  }

  // ============ 10. Kolko aut na ktorej sieti ============
  pres.addSection({ title: S3 });
  const POL = ['len Wi-Fi', 'len 5G', 'náhodne 50/50', 'podľa kvality', 'všetko naraz'];
  {
    const s = content('Koľko áut je na ktorej sieti', S3);
    s.addChart(pres.charts.BAR, [
      { name: 'Wi-Fi', labels: POL, values: [100, 0, 47.7, 59.6, 0] },
      { name: '5G', labels: POL, values: [0, 100, 52.3, 40.4, 0] },
      { name: 'obe naraz', labels: POL, values: [0, 0, 0, 0, 100] },
    ], chartBase({ x: M, y: 1.3, w: 6.2, h: 4.35, barDir: 'col', barGrouping: 'stacked', barGapWidthPct: 45, chartColors: [C.accent1, C.accent2, C.accent3], valAxisMinVal: 0, valAxisMaxVal: 100, valAxisMajorUnit: 25, showValue: true, dataLabelPosition: 'ctr', dataLabelFormatCode: '0.0;-0.0;;', dataLabelColor: 'FFFFFF', showLegend: true, legendPos: 'b', showTitle: true, title: 'Hlavná sieť áut [% áut], priemer od 30. s', catAxisLabelFontSize: 11, objectName: 'chart-share' }));
    const tl = ['1', '5', '10', '15', '20', '30', '40', '50', '60', '70', '80', '90', '100', '110', '119'];
    const qw = [47.7, 71.3, 61.7, 62.3, 60.4, 63.0, 66.0, 64.6, 61.7, 59.0, 55.4, 55.4, 57.9, 57.6, 57.2];
    s.addChart(pres.charts.LINE, [
      { name: 'Wi-Fi', labels: tl, values: qw },
      { name: '5G', labels: tl, values: qw.map((v) => Math.round((100 - v) * 10) / 10) },
    ], chartBase({ x: 7.0, y: 1.3, w: 5.75, h: 2.45, chartColors: [C.accent1, C.accent2], lineSize: 3, lineDataSymbol: 'none', valAxisMinVal: 0, valAxisMaxVal: 100, valAxisMajorUnit: 25, showLegend: true, legendPos: 'r', showTitle: true, title: 'Podľa kvality v čase [% áut], čas v s', titleFontSize: 12, catAxisLabelFontSize: 10, objectName: 'chart-share-time' }));
    s.addChart(pres.charts.BAR, [
      { name: 'Wi-Fi', labels: POL, values: [11.3, 3.3, 7.4, 8.8, 12.7] },
      { name: '5G', labels: POL, values: [5.4, 56.7, 35.7, 29.4, 56.9] },
    ], chartBase({ x: 7.0, y: 3.85, w: 5.75, h: 2.5, barDir: 'col', barGapWidthPct: 50, chartColors: [C.accent1, C.accent2], valAxisMinVal: 0, valAxisMaxVal: 60, valAxisMajorUnit: 30, showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '0', dataLabelFontSize: 10, showLegend: true, legendPos: 'r', showTitle: true, title: 'Obsadenosť kanála [% času], hranica 30 %', titleFontSize: 12, catAxisLabelFontSize: 9, objectName: 'chart-occupancy' }));
    takeaway(s, 'Výber podľa kvality začne 50/50 a sám sa ustáli okolo 60 % Wi-Fi a 40 % 5G, keď 5G prestane byť preplnené (29 % času oproti 57 %).', M, 5.8, 6.2, 0.8);
    source(s, 'Mesto bez zón, 140 áut, 5 náhod (201–205). Obsadenosť 5G je odhad z prijatých vysielaní.', 6.65);
  }

  // ============ 11. Kam sa spravy stracaju ============
  {
    const s = content('Kam sa správy o polohe strácajú', S3);
    s.addChart(pres.charts.BAR, [
      { name: 'doručené', labels: POL, values: [329.0, 287.1, 341.5, 349.6, 362.7] },
      { name: 'stratené', labels: POL, values: [55.8, 95.0, 42.2, 35.5, 21.1] },
    ], chartBase({ x: M, y: 1.3, w: 7.0, h: 4.4, barDir: 'col', barGrouping: 'stacked', barGapWidthPct: 45, chartColors: [C.text2, C.accent3], valAxisMinVal: 0, valAxisMaxVal: 400, valAxisMajorUnit: 100, showValue: true, dataLabelPosition: 'ctr', dataLabelFormatCode: '0', dataLabelColor: 'FFFFFF', showLegend: true, legendPos: 'b', showTitle: true, title: 'Doručenia v jednom behu [tisíce]: správa × sused do 100 m', catAxisLabelFontSize: 11, objectName: 'chart-flow' }));
    card(s, 7.85, 1.35, 4.9, 1.75, C.text2);
    text(s, '≈ 385 tisíc', { x: 8.05, y: 1.42, w: 4.5, h: 0.75, fontSize: 36, bold: true, color: C.accent3, fontFace: THEME.headFontFace });
    text(s, 'doručení za beh: 140 áut × 3,4 správy za sekundu × asi 9 susedov × 90 s merania', { x: 8.05, y: 2.2, w: 4.5, h: 0.8, fontSize: 13, color: C.background1 });
    const rows = [
      ['', 'stratí sa zo 100', 'kvalita lepšia o'],
      ['len 5G', '24,9', '+15,6'],
      ['len Wi-Fi', '14,5', '+5,3'],
      ['náhodne 50/50', '11,0', '+1,8'],
      ['podľa kvality', '9,2', '–'],
      ['všetko naraz', '5,5', '−3,7 *'],
    ].map((row, i) => row.map((c, j) => ({ text: c, options: { fontSize: 12, bold: i === 0 || j === 0 || i === 4, color: i === 0 ? C.background1 : (c.startsWith('−') ? C.accent2 : C.text1), fill: i === 0 ? { color: C.text2 } : (i === 4 ? { color: C.background2 } : undefined), align: j ? 'center' : 'left', valign: 'middle', margin: [2, 5, 2, 5] } })));
    s.addTable(rows, { x: 7.85, y: 3.3, w: 4.9, colW: [1.7, 1.6, 1.6], rowH: 0.33, border: { type: 'solid', pt: 0.75, color: 'DDE3E6' }, objectName: 'table-flow' });
    text(s, 'body, všetky rozdiely preukázané (95 % interval); * dva pokusy na správu (Wi-Fi aj 5G)', { x: 7.85, y: 5.35, w: 4.9, h: 0.45, fontSize: 11, italic: true, color: C.accent5 });
    takeaway(s, 'Len mobilná sieť: autá si nedoručia nič, lebo správa ide len na server (skorší test). Preto nie je v grafe.', M, 5.9, W - 2 * M, 0.65);
    source(s, 'Mesto bez zón, 140 áut, 5 náhod (201–205), priemer behov, séria idea1.', 6.65);
  }

  // ============ 12. Paradox vsetko naraz ============
  {
    const s = content('Všetko naraz: viac správ o polohe, horšie kópie', S3);
    const lab = ['senzory cez Wi-Fi', 'senzory cez 5G', 'semafor cez Wi-Fi', 'semafor cez 5G'];
    s.addChart(pres.charts.BAR, [
      { name: 'podľa kvality', labels: lab, values: [80.0, 92.1, 78.8, 96.4] },
      { name: 'všetko naraz', labels: lab, values: [74.4, 73.4, 68.6, 83.2] },
    ], chartBase({ x: M, y: 1.3, w: 7.1, h: 4.4, barDir: 'col', barGapWidthPct: 50, chartColors: [C.text2, C.accent3], valAxisMinVal: 50, valAxisMaxVal: 100, valAxisMajorUnit: 10, showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '0.0', showLegend: true, legendPos: 'b', showTitle: true, title: 'Doručenie jednej kópie [%]', catAxisLabelFontSize: 11, objectName: 'chart-copy' }));
    const boxes = [
      ['FaCar', COL.wifi, 'Správa o polohe: stačí jedna kópia', 'ide Wi-Fi aj 5G = 2 pokusy, preto 94,5 %'],
      ['FaVideo', COL.nr, 'Senzory a semafor: každá kópia zvlášť', 'dvojnásobná prevádzka, každá kópia horšie'],
      ['FaTrafficLight', COL.all, '5G obsadené 57 % času', 'pri výbere podľa kvality 29 %'],
      ['FaBroadcastTower', COL.lte, '4,2× viac správ', 'cez mobilnú sieť'],
    ];
    for (let i = 0; i < 4; i++) {
      const y = 1.35 + i * 1.08;
      await iconCircle(s, boxes[i][0], 7.95, y, 0.7, boxes[i][1]);
      text(s, [{ text: boxes[i][2], options: { bold: true, fontSize: 15, color: C.text2, breakLine: true } }, { text: boxes[i][3], options: { fontSize: 13 } }], { x: 8.8, y: y - 0.04, w: 3.95, h: 0.95 });
    }
    takeaway(s, 'Koľko senzorových správ prišlo aspoň jednou kópiou, simulácia nemerala. Isté je: všetko naraz zahltí kanály, každá kópia trpí a mobilná sieť nesie 4,2× viac.', M, 5.85, W - 2 * M, 0.75);
    source(s, 'Mesto bez zón, 140 áut, 5 náhod (201–205). Pri všetkých spôsoboch posiela cestná jednotka správy zo semafora oboma sieťami.', 6.65);
  }

  // ============ 13. Vysledok so zonami ============
  {
    const s = content('Výsledok so zónami: tu výber pomáha najviac', S3);
    const lab = ['A: stále zóny', 'C: zóny sa menia', 'D: nevidené', 'T: bez zón'];
    s.addChart(pres.charts.BAR, [
      { name: 'náhodne 50/50 (bez výberu)', labels: lab, values: [65.3, 66.1, 73.4, 88.4] },
      { name: 'podľa kvality', labels: lab, values: [78.7, 79.0, 81.5, 90.6] },
    ], chartBase({ x: M, y: 1.3, w: 7.9, h: 4.6, barDir: 'col', barGapWidthPct: 55, chartColors: [C.accent6, C.text2], valAxisMinVal: 50, valAxisMaxVal: 100, valAxisMajorUnit: 10, showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '0.0', showLegend: true, legendPos: 'b', showTitle: true, title: 'Doručené správy o polohe [%], 10 náhod', objectName: 'chart-zones' }));
    text(s, 'Zisk oproti 50/50 (bodov)', { x: 8.85, y: 1.4, w: 3.9, h: 0.4, fontSize: 16, bold: true, color: C.text2 });
    const g = [['A', '+13,4', '+12,5 až +14,3'], ['C', '+12,9', '+12,1 až +13,7'], ['D', '+8,1', '+7,4 až +8,8'], ['T', '+2,1', '+1,8 až +2,4']];
    g.forEach(([k, v, ci], i) => {
      const y = 1.9 + i * 0.68;
      text(s, k, { x: 8.85, y, w: 0.45, h: 0.55, fontSize: 18, bold: true, color: C.accent5, valign: 'middle' });
      text(s, v, { x: 9.3, y, w: 1.3, h: 0.55, fontSize: 22, bold: true, color: C.accent1, valign: 'middle', fontFace: THEME.headFontFace });
      text(s, ci, { x: 10.65, y, w: 2.1, h: 0.55, fontSize: 12, color: C.accent5, valign: 'middle' });
    });
    bullets(s, [
      [b('senzorové správy '), r('+6 až +12 bodov')],
      [b('do cestnej jednotky '), r('+7 až +9 bodov')],
    ], { x: 8.85, y: 4.65, w: 3.9, h: 0.75, fontSize: 14 });
    takeaway(s, 'Ani dokonalá znalosť zón by nepridala viac než 2–3 body: výber je blízko maxima.', 8.85, 5.45, 3.9, 1.1);
    source(s, 'Finálna séria: 140 áut, náhody 1–10 (nepoužité pri ladení), párové porovnanie, 95 % interval. T tu má iné náhody než na snímke 11.', 6.65);
  }

  // ============ 14. Predikcia ============
  pres.addSection({ title: S4 });
  {
    const s = content('Krok 4: predikcia dopredu je zlý smer', S4);
    const lab = ['30–60 s', '60–75 s', '75–90 s', '90–105 s', '105–120 s'];
    s.addChart(pres.charts.LINE, [
      { name: 'náhodne 50/50', labels: lab, values: [60.7, 60.4, 69.7, 77.9, 76.9] },
      { name: 'podľa kvality', labels: lab, values: [76.1, 72.5, 84.3, 85.1, 86.4] },
      { name: 'mapa kvality (predikcia)', labels: lab, values: [76.4, 60.9, 76.6, 85.5, 86.5] },
    ], chartBase({ x: M, y: 1.3, w: 6.6, h: 4.3, chartColors: [C.accent6, C.text2, C.accent2], lineSize: 3, lineDataSymbol: 'circle', lineDataSymbolSize: 8, valAxisMinVal: 50, valAxisMaxVal: 90, valAxisMajorUnit: 10, showLegend: true, legendPos: 'b', showTitle: true, title: 'Scenár C: v 60. s sa zóny vymenia [% doručených]', objectName: 'chart-c-time' }));
    const lab2 = ['mapa, A', 'mapa, C', 'mapa, D', 'mapa, T', 'stromy, D', 'stromy, T'];
    s.addChart(pres.charts.BAR, [{ name: 'rozdiel oproti výberu podľa kvality', labels: lab2, values: [0.1, -2.9, -1.5, -0.3, -0.5, -0.5] }],
      chartBase({ x: 7.35, y: 1.3, w: 5.4, h: 3.0, barDir: 'bar', barGapWidthPct: 40, chartColors: [HEX.accent2], valAxisMinVal: -3.5, valAxisMaxVal: 1, valAxisMajorUnit: 1, showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '0.0', showLegend: false, showTitle: true, title: 'Predikcia oproti výberu podľa kvality [body]', catAxisLabelFontSize: 11, objectName: 'chart-pred-diff' }));
    bullets(s, [
      [b('Mapa '), r('drží staré hodnoty: po zmene 60,9 % namiesto 72,5 %.')],
      [b('Naučené stromy '), r('predpovedali presnejšie, ale v sieti o 0,5 bodu horšie.')],
      [b('Strop: '), r('aj dokonalé videnie 3 s dopredu pridá len 0,7–1,1 bodu.')],
    ], { x: 7.35, y: 4.45, w: 5.4, h: 1.6, fontSize: 14 });
    takeaway(s, 'Na predikciu nie je priestor, preto ostávam pri výbere podľa aktuálnej kvality.', M, 5.8, 6.6, 0.75);
    source(s, 'Finálna séria, 10 náhod. Mapa v C a D a stromy v D a T sú preukázateľne horšie, mapa v A a T bez preukázaného rozdielu.', 6.65);
  }

  // ============ 15. Zaver ============
  {
    const s = content('Záver a čo ďalej', S4);
    const msgs = [
      ['FaTools', COL.gray, 'Opravený simulátor', 'chyby autorov by inak skreslili každé porovnanie'],
      ['FaTimesCircle', COL.nr, 'Nepriame ukazovatele nefungujú', 'hustota a vyťaženie netrafia príčinu strát (budovy)'],
      ['FaCheckCircle', COL.wifi, 'Výber podľa kvality funguje', 'lepší než jedna sieť aj náhoda; so zónami o 8–13 bodov'],
      ['FaBalanceScale', COL.all, 'Kanály nepreplní', 'sám nájde rozdelenie ~60/40; všetko naraz zahltí 5G'],
    ];
    for (let i = 0; i < 4; i++) {
      const y = 1.45 + i * 1.05;
      await iconCircle(s, msgs[i][0], M, y, 0.8, msgs[i][1]);
      text(s, [{ text: msgs[i][2], options: { bold: true, fontSize: 17, color: C.text2, breakLine: true } }, { text: msgs[i][3], options: { fontSize: 14 } }], { x: M + 1.0, y: y - 0.02, w: 5.6, h: 0.9 });
    }
    card(s, 7.55, 1.45, 5.2, 3.9, C.text2);
    text(s, 'Ďalej budem zlepšovať', { x: 7.85, y: 1.65, w: 4.7, h: 0.5, fontSize: 19, bold: true, color: C.accent3 });
    bullets(s, [
      'kombinácia s pevným rozdelením podľa druhu správy (polohy cez 5G, senzory cez Wi-Fi)',
      'menej skúšobných správ',
      'autá, ktoré nemajú všetky siete',
    ], { x: 7.85, y: 2.25, w: 4.7, h: 2.9, fontSize: 15, color: C.background1 });
    takeaway(s, 'Obmedzenia: simulácia jednej mapy so 140 autami; zóny sú umelý pokus, nie model fyziky; poradové číslo v hlavičke je malé rozšírenie normy.', M, 5.75, W - 2 * M, 0.85);
  }

  await pres.writeFile({ fileName: OUT });
  await applyTheme(OUT, THEME);
  console.log('written', OUT, slideNo, 'slides');
})();
