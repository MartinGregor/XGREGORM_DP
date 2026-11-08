// xgregorm: prezentacia priebehu prace (VaN3Twin, vyber RAT). Vsetky cisla su z Dennika v CLAUDE.md a z xgregorm_PLAN_PREDIKCIA.md.
const pptxgen = require('pptxgenjs');
const React = require('react');
const RDS = require('react-dom/server');
const { execFileSync } = require('child_process');
const fa = require('react-icons/fa');
const path = require('path');
const { applyTheme } = require('/home/marti/.claude/skills/synced/5869e613-1ca4-4b3d-bc5c-18e5a33d9888_825bbd1e-c49b-4409-ac72-7f83a2186ff6/pptx/scripts/apply_theme.js');

const OUT = process.argv[2] || 'deck.pptx';
const IMG = path.join(__dirname, 'img');

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

const pres = new pptxgen();
pres.layout = 'LAYOUT_WIDE'; // 13.333 x 7.5
pres.author = 'Martin Gregor';
pres.title = 'Výber rádiovej technológie vo V2X – priebeh práce';
pres.subject = 'Diplomová práca, VaN3Twin (ns-3 + SUMO)';
pres.theme = { headFontFace: THEME.headFontFace, bodyFontFace: THEME.bodyFontFace };
const C = pres.SchemeColor;
const W = 13.333, H = 7.5, M = 0.6;

// ---------- layouty ----------
const footer = (dark) => [
  { text: { text: 'VaN3Twin · výber RAT vo V2X · diplomová práca', options: { x: M, y: 7.02, w: 8, h: 0.3, fontSize: 10, color: dark ? C.background2 : C.accent5, margin: 0 } } },
];
pres.defineSlideMaster({
  title: 'COVER', background: { color: C.text2 },
  objects: [
    { placeholder: { options: { name: 'title', type: 'title', x: M, y: 2.0, w: 8.2, h: 2.0, fontSize: 40, bold: true, color: C.background1, valign: 'bottom', margin: 0 }, text: '' } },
    { placeholder: { options: { name: 'body', type: 'body', x: M, y: 4.2, w: 8.2, h: 1.6, fontSize: 18, color: C.background2, valign: 'top', margin: 0 }, text: '' } },
  ],
});
pres.defineSlideMaster({
  title: 'SECTION', background: { color: C.text2 },
  objects: [
    ...footer(true),
    { placeholder: { options: { name: 'title', type: 'title', x: M, y: 2.0, w: 9.5, h: 1.95, fontSize: 40, bold: true, color: C.background1, valign: 'bottom', margin: 0 }, text: '' } },
    { placeholder: { options: { name: 'body', type: 'body', x: M, y: 4.05, w: 9.5, h: 1.4, fontSize: 18, color: C.background2, valign: 'top', margin: 0 }, text: '' } },
  ],
  slideNumber: { x: 12.2, y: 7.0, w: 0.6, h: 0.3, fontSize: 10, color: C.background2, align: 'right' },
});
pres.defineSlideMaster({
  title: 'CONTENT', background: { color: C.background1 },
  objects: [
    ...footer(false),
    { placeholder: { options: { name: 'title', type: 'title', x: M, y: 0.35, w: W - 2 * M, h: 0.85, fontSize: 32, bold: true, color: C.text2, valign: 'middle', margin: 0 }, text: '' } },
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
  s.addImage({ data: await icon(name, iconHex), x: x + p, y: y + p, w: d - 2 * p, h: d - 2 * p, objectName: nm('icon'), altText: name });
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
function content(title, section) {
  const s = pres.addSlide({ masterName: 'CONTENT', sectionTitle: section });
  s.addText(title, { placeholder: 'title' });
  return s;
}
function sectionSlide(num, title, sub, section) {
  pres.addSection({ title: section });
  const s = pres.addSlide({ masterName: 'SECTION', sectionTitle: section });
  s.addText(title, { placeholder: 'title' });
  s.addText(sub, { placeholder: 'body' });
  s.addText(String(num).padStart(2, '0'), { isTextBox: true, x: 10.4, y: 2.2, w: 2.4, h: 2.4, fontSize: 120, bold: true, color: C.accent1, fontFace: THEME.headFontFace, align: 'right', valign: 'middle', margin: 0, objectName: nm('section-num') });
  return s;
}
// spolocny vzhlad grafov
function chartBase(extra) {
  return Object.assign({
    catAxisLabelColor: HEX.accent5, valAxisLabelColor: HEX.accent5,
    catAxisLabelFontFace: '+mn-lt', valAxisLabelFontFace: '+mn-lt', catAxisLabelFontSize: 12, valAxisLabelFontSize: 11,
    dataLabelFontFace: '+mn-lt', dataLabelFontSize: 11, dataLabelColor: HEX.dk1,
    legendFontFace: '+mn-lt', legendFontSize: 12, legendColor: HEX.dk1,
    titleFontFace: '+mn-lt', titleFontSize: 14, titleColor: HEX.dk2,
    valGridLine: { color: 'DDE3E6', size: 0.75 }, catGridLine: { style: 'none' },
    catAxisLineShow: true, valAxisLineShow: false,
  }, extra);
}
function tcell(t, o) { return { text: t, options: Object.assign({ fontSize: 12, color: C.text1, valign: 'middle', margin: [3, 5, 3, 5] }, o || {}) }; }
function thead(t) { return tcell(t, { bold: true, color: C.background1, fill: { color: C.text2 } }); }

(async () => {
  // ============ 1. Titulna strana ============
  pres.addSection({ title: 'Úvod' });
  {
    const s = pres.addSlide({ masterName: 'COVER', sectionTitle: 'Úvod' });
    s.addText('Výber rádiovej technológie vo V2X sieti', { placeholder: 'title' });
    s.addText([
      { text: 'Priebeh diplomovej práce od prvého spustenia VaN3Twin po niekoľkodňové overovacie série', options: { breakLine: true } },
      { text: 'Martin Gregor · 7. 10. – 8. 11. 2026', options: { fontSize: 15, color: C.accent3 } },
    ], { placeholder: 'body' });
    // vizual: tri technologie ako kruhy
    const items = [['FaWifi', HEX.accent1, '802.11p'], ['FaBroadcastTower', HEX.accent4, 'LTE'], ['FaSatelliteDish', HEX.accent2, 'NR-V2X']];
    for (let i = 0; i < 3; i++) {
      const [ic, col, lab] = items[i];
      const x = 9.55, y = 1.35 + i * 1.75;
      await iconCircle(s, ic, x, y, 1.25, col, 'FFFFFF');
      text(s, lab, { x: x + 1.45, y: y + 0.35, w: 1.8, h: 0.55, fontSize: 20, bold: true, color: C.background1, valign: 'middle' });
    }
    s.addNotes('Prezentácia zhŕňa celý priebeh práce: od prvého spustenia simulátora VaN3Twin, cez opravy chýb a hybridnú sieť s tromi technológiami, až po niekoľkodňové série behov, ktoré porovnávali výber technológie podľa aktuálnej kvality s alternatívami.');
  }

  // ============ 2. Obsah ============
  {
    const s = content('O čom je táto prezentácia', 'Úvod');
    const parts = [
      ['01', 'Začiatky', 'VaN3Twin, mapy, prvé chyby autorov', '7.–11. 10.'],
      ['02', 'Hybridná sieť', '802.11p + LTE + NR-V2X a prepínač ciest', '12.–23. 10.'],
      ['03', 'Plný realizmus a plán', 'rušenie, DCC, výbava, metodika dôkazu', '1.–3. 11.'],
      ['04', 'Alternatívy k výberu podľa kvality', 'mapa, učenie, bandit, orákulum', '3.–4. 11.'],
      ['05', 'Niekoľkodňová fronta', 'finálna séria a overenie nápadov 1–6', '4.–7. 11.'],
    ];
    const cw = (W - 2 * M - 4 * 0.3) / 5;
    parts.forEach(([n, t, d, dt], i) => {
      const x = M + i * (cw + 0.3), y = 1.65;
      card(s, x, y, cw, 4.6);
      text(s, n, { x: x + 0.25, y: y + 0.3, w: cw - 0.5, h: 0.9, fontSize: 44, bold: true, color: C.accent1, fontFace: THEME.headFontFace });
      text(s, t, { x: x + 0.25, y: y + 1.35, w: cw - 0.5, h: 1.0, fontSize: 19, bold: true, color: C.text2 });
      text(s, d, { x: x + 0.25, y: y + 2.4, w: cw - 0.5, h: 1.3, fontSize: 14, color: C.text1 });
      text(s, dt, { x: x + 0.25, y: y + 3.85, w: cw - 0.5, h: 0.45, fontSize: 14, bold: true, color: C.accent2 });
    });
    text(s, 'Všetky čísla sú z Denníka v CLAUDE.md a z plánu xgregorm_PLAN_PREDIKCIA.md. Skratky vysvetľujú nasledujúce tri snímky.', { x: M, y: 6.45, w: W - 2 * M, h: 0.4, fontSize: 12, color: C.accent5, italic: true });
  }

  // ============ 3.–5. Skratky ============
  function glossary(title, rows) {
    const s = content(title, 'Úvod');
    const half = Math.ceil(rows.length / 2);
    [rows.slice(0, half), rows.slice(half)].forEach((col, ci) => {
      const x = M + ci * 6.2;
      const data = col.map(([k, v]) => [tcell(k, { bold: true, color: C.text2, fontSize: 14, fill: { color: C.background2 } }), tcell(v, { fontSize: 14 })]);
      s.addTable(data, { x, y: 1.45, w: 5.9, colW: [1.45, 4.45], border: { type: 'solid', pt: 0.75, color: 'FFFFFF' }, rowH: 0.6, objectName: nm('glossary') });
    });
    return s;
  }
  glossary('Skratky: siete a technológie', [
    ['V2X', 'Vehicle-to-Everything, komunikácia auta s okolím'],
    ['V2V / V2I', 'auto–auto / auto–infraštruktúra (RSU)'],
    ['RAT', 'Radio Access Technology, rádiová technológia'],
    ['802.11p', 'IEEE 802.11p (ETSI ITS-G5), priama Wi-Fi pre autá, 5.9 GHz'],
    ['LTE', '4G mobilná sieť; CAM ide cez základňovú stanicu a server'],
    ['NR-V2X', '5G New Radio, priamy kanál medzi autami (sidelink, PC5)'],
    ['eNB / UE', 'základňová stanica LTE / koncové zariadenie (auto)'],
    ['RSU', 'Roadside Unit, jednotka pri ceste, tu na každej križovatke'],
    ['ns-3', 'simulátor siete (rádio, protokoly, straty)'],
    ['SUMO', 'simulátor dopravy (autá, semafory)'],
    ['TraCI', 'rozhranie, ktorým ns-3 riadi a číta SUMO'],
    ['VaN3Twin', 'rámec autorov: ns-3 + SUMO + V2X aplikácie'],
    ['5G-LENA', 'model NR v ns-3 (z neho NR-V2X sidelink)'],
    ['GeoNet', 'GeoNetworking, sieťová vrstva ETSI pre V2X'],
    ['ETSI / 3GPP', 'normalizačné organizácie (ITS-G5 / LTE, NR)'],
    ['LOS / NLOS', 'priama viditeľnosť / za budovou'],
  ]);
  glossary('Skratky: správy a metriky', [
    ['CAM', 'Cooperative Awareness Message, „tlkot srdca“ auta, 1–10 Hz'],
    ['CPM', 'Collective Perception Message, objekty zo senzorov'],
    ['DENM', 'udalostná správa do oblasti (napr. výstraha)'],
    ['SPATEM', 'stav semafora z RSU, 10 Hz'],
    ['MAPEM', 'geometria križovatky z RSU, 1 Hz'],
    ['PRR', 'Packet Reception Ratio: podiel susedov do 100 m, ktorí CAM dostali'],
    ['uplink', 'podiel CAM, ktoré dostala RSU (do 100 m)'],
    ['vek info', 'ako stará je poloha auta, ktorú pozná RSU'],
    ['CBR', 'Channel Busy Ratio, obsadenosť kanála 802.11p'],
    ['SL-CBR', 'obsadenosť NR sidelinku (tu odhad)'],
    ['PRB', 'Physical Resource Block, zdroje bunky LTE; záťaž = podiel PRB'],
    ['DCC', 'Decentralized Congestion Control, brána 802.11p pri záťaži'],
    ['CR', 'Channel occupancy Ratio, limit vysielania NR'],
    ['p95', '95. percentil (napr. oneskorenia)'],
    ['CI 95 %', 'interval spoľahlivosti rozdielu; „bez nuly“ = preukázané'],
    ['Holm', 'korekcia pri viacerých testoch naraz'],
  ]);
  glossary('Skratky: politiky výberu a označenia', [
    ['FW / FN', 'všetky autá stále na 802.11p / na NR (bez výberu)'],
    ['MIX', '50/50: polovica áut 802.11p, polovica NR, bez zmeny'],
    ['ALL', 'každá CAM všetkými technológiami naraz'],
    ['Q', 'výber podľa aktuálne nameranej kvality (reaktívny)'],
    ['PM', 'predikcia z mapy kvality (hlásenia cez LTE)'],
    ['GS', 'naučené stromy (GBM) zo správ, bez mapy'],
    ['GBM', 'Gradient Boosting Machine, súbor rozhodovacích stromov'],
    ['B / B2', 'bandit (učenie za jazdy); B2 s plnými skúškami'],
    ['O0 / O3', 'orákulum: pozná zóny presne, výhľad 0 / 3 s'],
    ['I1–I6', 'vlastné nápady výberu RAT (sekcia 05)'],
    ['A / C / D / T', 'scenáre: stále zóny / meniace sa / nevidené / bez zón'],
    ['zóna', 'oblasť 150 m okolo RSU, kde jedna technológia stráca 70 %'],
    ['seed', 'náhodné nastavenie behu; rovnaký seed = párové porovnanie'],
    ['dev / final', 'ladiace série (seedy 101–103) / finálne (1–10)'],
    ['idea', 'overenie nápadov (seedy 201–205)'],
    ['n', 'počet seedov v porovnaní'],
  ]);

  // ============ SEKCIA 1 ============
  const S1 = 'Začiatky';
  {
    const s = sectionSlide(1, 'Začiatky: nástroj, mapy a chyby autorov', '7.–11. októbra · od prvého spustenia a zápisu pravidiel do CLAUDE.md', S1);
    s.addNotes('Prvý týždeň: rozbehnutie VaN3Twin, vlastný generátor scenárov, zápis nezdokumentovaných pravidiel a dve zásadné chyby v aplikáciách autorov.');
  }
  {
    const s = content('VaN3Twin: dva simulátory prepojené cez TraCI', S1);
    // diagram
    const boxes = [
      { x: 0.8, t: 'SUMO', d: 'autá, trasy, semafory, mapa mesta', ic: 'FaCar', col: C.accent3, hex: HEX.accent3 },
      { x: 4.9, t: 'TraCI', d: 'poloha áut každý krok, ovládanie semaforov', ic: 'FaProjectDiagram', col: C.accent5, hex: HEX.accent5 },
      { x: 9.0, t: 'ns-3', d: 'rádio 802.11p / LTE / NR, straty, oneskorenie', ic: 'FaSignal', col: C.accent1, hex: HEX.accent1 },
    ];
    for (const bx of boxes) {
      card(s, bx.x, 1.7, 3.5, 2.3);
      await iconCircle(s, bx.ic, bx.x + 0.3, 1.95, 0.85, bx.hex, 'FFFFFF');
      text(s, bx.t, { x: bx.x + 1.35, y: 2.05, w: 2.0, h: 0.6, fontSize: 22, bold: true, color: C.text2, valign: 'middle' });
      text(s, bx.d, { x: bx.x + 0.3, y: 2.95, w: 2.9, h: 0.9, fontSize: 14 });
    }
    [4.3, 8.4].forEach((x) => s.addShape(pres.shapes.RIGHT_ARROW, { x: x + 0.05, y: 2.65, w: 0.5, h: 0.4, fill: { color: C.accent6 }, line: { color: C.accent6 }, objectName: nm('arrow') }));
    text(s, 'Čo som si k tomu vytvoril', { x: M, y: 4.4, w: 6, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    bullets(s, [
      [b('xgregorm_gen_scenario.py'), r(' – generátor mapy, áut, RSU, spúšťač a súhrn výsledkov')],
      [b('CLAUDE.md'), r(' – pravidlá projektu a Denník: každá zmena a každé meranie s dátumom')],
      [b('prefix xgregorm_'), r(' – všetko moje je odlíšené od kódu autorov, zálohy v xgregorm_kod_zaloha/')],
    ], { x: M, y: 4.95, w: 6.3, h: 1.9 });
    bullets(s, [
      [r('Každá zmena kódu autorov má zálohu a „bez voľby bit po bite zhodné“ overenie')],
      [r('Simulácie sú deterministické: rovnaký seed = rovnaký výsledok do posledného miesta')],
      [r('Výsledky sa porovnávajú len pri nezmenenom scenári (autá, RSU, seed)')],
    ], { x: 7.2, y: 4.95, w: 5.5, h: 1.9 });
  }
  {
    const s = content('Pravidlá, ktoré VaN3Twin vyžaduje, no nikde nie sú', S1);
    const rules = [
      ['FaCar', 'ID áut musia byť veh<číslo>', 'aplikácie robia stol(id.substr(3))'],
      ['FaRoute', 'Autá len ako <vehicle>', 'nie flow ani trip; podľa nich vznikajú uzly ns-3'],
      ['FaBroadcastTower', 'RSU zo stations.xml', 'komentár V2X:STATION na tom istom riadku ako <poi>'],
      ['FaMap', 'Sieť musí byť geo-referencovaná', 'inak nefungujú CAM, DENM ani GeoArea'],
      ['FaTrafficLight', 'Semafor len na 4 ramenách', 'trafficManager indexuje vjazdy 0–3'],
      ['FaBug', 'Oprava vdpTraci.cc', 'V2I príklady padali na „poi_X is not known“'],
    ];
    for (let i = 0; i < 6; i++) {
      const col = i % 3, row = Math.floor(i / 3);
      const x = M + col * 4.1, y = 1.55 + row * 2.6;
      card(s, x, y, 3.85, 2.35);
      await iconCircle(s, rules[i][0], x + 0.25, y + 0.25, 0.75, i === 5 ? HEX.accent2 : HEX.accent1, 'FFFFFF');
      text(s, rules[i][1], { x: x + 0.25, y: y + 1.05, w: 3.4, h: 0.62, fontSize: 16, bold: true, color: C.text2 });
      text(s, rules[i][2], { x: x + 0.25, y: y + 1.72, w: 3.4, h: 0.55, fontSize: 13 });
    }
  }
  {
    const s = content('Dve mapy: malé mesto a centrum s 9 semaformi', S1);
    s.addImage({ path: path.join(IMG, 'map.png'), x: 7.3, y: 1.35, w: 5.4, h: 5.4 * 822 / 964, objectName: 'map-city-center', altText: 'Mapa city-center s 9 RSU' });
    text(s, 'xgregorm_mesto', { x: M, y: 1.5, w: 6, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    bullets(s, ['mriežka 3×3, bloky 200 m, 5 RSU, obchvat 450 m', 'prvé overenia aplikácií autorov s 10 a 30 autami'], { x: M, y: 2.0, w: 6.3, h: 1.0 });
    text(s, 'city-center (hlavná mapa)', { x: M, y: 3.2, w: 6, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    bullets(s, [
      '9 križovatiek so semaforom, 3 pruhy v smere, bloky 171–286 m',
      'okruh 200 m od centra, 9 RSU = 9 semaforov',
      'ladené v SUMO: 600 áut bez teleportov, v sériách 140 áut',
      'neskôr 357 budov z blokov mapy pre mestský kanál',
    ], { x: M, y: 3.7, w: 6.3, h: 2.2 });
    text(s, 'Vpravo city-center: sivé čiary = jazdné pruhy, body = RSU', { x: M, y: 6.3, w: 6.3, h: 0.35, fontSize: 12, color: C.accent5, italic: true });
  }
  {
    const s = content('Chyba 1: autá posielali CAM v rovnakom okamihu', S1);
    s.addChart(pres.charts.BAR, [
      { name: 'pred opravou', labels: ['areaSpeedAdvisor, 10 áut', 'trafficManager, 10 áut', 'areaSpeedAdvisor, 30 áut', 'trafficManager, 30 áut'], values: [0.660, 0.593, 0.347, 0.313] },
      { name: 'po oprave A', labels: ['areaSpeedAdvisor, 10 áut', 'trafficManager, 10 áut', 'areaSpeedAdvisor, 30 áut', 'trafficManager, 30 áut'], values: [0.996, 0.995, 0.997, 0.997] },
    ], chartBase({ x: M, y: 1.4, w: 7.4, h: 5.3, barDir: 'col', barGapWidthPct: 60, chartColors: [C.accent2, C.accent1], showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '0.00', valAxisMinVal: 0, valAxisMaxVal: 1.1, valAxisMajorUnit: 0.2, valAxisLabelFormatCode: '0.0', showLegend: true, legendPos: 'b', showTitle: true, title: 'PRR aplikácií V2I 802.11p', objectName: 'chart-sync' }));
    text(s, 'Príčina', { x: 8.4, y: 1.5, w: 4.3, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    text(s, 'Aplikácie V2I nastavovali náhodný posun CAM cez srand(čas). Autá vzniknuté v tom istom kroku dostali ten istý posun, vysielali naraz a kolidovali.', { x: 8.4, y: 2.0, w: 4.3, h: 1.8, fontSize: 15 });
    text(s, 'Oprava A', { x: 8.4, y: 3.9, w: 4.3, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    text(s, 'Generátor náhodných čísel ns-3 (UniformRandomVariable), ako to autori už opravili v jednej aplikácii. Zmena v 8 aplikáciách.', { x: 8.4, y: 4.4, w: 4.3, h: 1.5, fontSize: 15 });
    text(s, 'Staršie výsledky V2I sú „autori s chybou“ a porovnávajú sa oddelene.', { x: 8.4, y: 6.0, w: 4.3, h: 0.7, fontSize: 13, color: C.accent5, italic: true });
  }
  {
    const s = content('Chyba 2: RSU sa preťahovali o semafory', S1);
    s.addChart(pres.charts.BAR, [
      { name: '10 áut', labels: ['pôvodne', 'lokálne riadenie', 'lokálne + koordinácia'], values: [104.1, 17.1, 18.1] },
      { name: '30 áut', labels: ['pôvodne', 'lokálne riadenie', 'lokálne + koordinácia'], values: [105.8, 18.8, 22.4] },
    ], chartBase({ x: M, y: 1.4, w: 7.4, h: 5.3, barDir: 'col', barGapWidthPct: 60, chartColors: [C.accent5, C.accent3], showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '0', valAxisMinVal: 0, valAxisMaxVal: 120, valAxisMajorUnit: 20, showLegend: true, legendPos: 'b', showTitle: true, title: 'Najdlhšie státie auta [s], 5 seedov', objectName: 'chart-tls' }));
    text(s, 'Čo sa dialo', { x: 8.4, y: 1.5, w: 4.3, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    text(s, 'V trafficManager 802.11p každá RSU riadila všetky semafory, hoci videla len autá vo svojom dosahu. Semafory sa prepínali 5× častejšie, autá stáli až 107 s.', { x: 8.4, y: 2.0, w: 4.3, h: 1.8, fontSize: 15 });
    text(s, 'Riešenie', { x: 8.4, y: 3.9, w: 4.3, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    text(s, 'Každá RSU riadi len semafor svojej križovatky (TlsRadius 50 m), ako v realite. Koordinácia RSU po kábli navyše nepomohla, preto je vypnutá. Opravená aj nekonečná slučka pri prázdnej križovatke (0/0).', { x: 8.4, y: 4.4, w: 4.3, h: 2.3, fontSize: 15 });
  }
  {
    const s = content('Algoritmy CAM v rámci normy ETSI EN 302 637-2', S1);
    const rows = [
      [thead('konfigurácia'), thead('CAM'), thead('rámce/s'), thead('strata auto-auto'), thead('porušenia ETSI')],
      [tcell('bez algoritmov'), tcell('14 510'), tcell('120.9'), tcell('1 / 37 318'), tcell('0')],
      [tcell('1: SPaT, stojace auto šetrí CAM'), tcell('14 390 (−0.8 %)'), tcell('126.2'), tcell('1 / 36 779'), tcell('0')],
      [tcell('2: potvrdenie RSU + opakovanie'), tcell('14 517'), tcell('144.9 (+20 %)'), tcell('19 / 37 427'), tcell('0')],
      [tcell('3: relevancia (ďalej ako 250 m pred semaforom 1 CAM/s)', { bold: true }), tcell('6 399 (−56 %)', { bold: true, color: C.accent1 }), tcell('58.1 (−52 %)', { bold: true, color: C.accent1 }), tcell('0 / 23 477', { bold: true }), tcell('0')],
    ];
    s.addTable(rows, { x: M, y: 1.5, w: 8.2, colW: [3.5, 1.3, 1.2, 1.3, 0.9], border: { type: 'solid', pt: 0.75, color: 'DDE3E6' }, rowH: 0.55, objectName: nm('table-cam') });
    text(s, '30 áut, 120 s, 1 beh. Doprava vo všetkých rovnaká.', { x: M, y: 4.55, w: 8.2, h: 0.35, fontSize: 12, color: C.accent5, italic: true });
    card(s, 9.2, 1.5, 3.55, 4.9);
    await iconCircle(s, 'FaCheckCircle', 9.45, 1.75, 0.75, HEX.accent1, 'FFFFFF');
    text(s, 'Ponaučenie', { x: 9.45, y: 2.65, w: 3.1, h: 0.45, fontSize: 18, bold: true, color: C.text2 });
    text(s, 'Prvá verzia porušovala normu (CAM držané dlhšie než 1 s, opakovania pod 100 ms). Po úprave má zmysel len relevancia. Algoritmy 1 a 2 som odstránil, výsledky ostali v Denníku.', { x: 9.45, y: 3.15, w: 3.1, h: 3.1, fontSize: 14 });
    text(s, 'Odvtedy každý beh kontroluje odstupy CAM 100–1000 ms (XGREGORM-GAP); vo všetkých sériách 0 porušení.', { x: M, y: 5.2, w: 8.2, h: 0.9, fontSize: 15 });
  }

  // ============ SEKCIA 2 ============
  const S2 = 'Hybridná sieť';
  sectionSlide(2, 'Hybridná sieť 802.11p + LTE + NR-V2X', '12.–23. októbra · jedna CAM, tri možné cesty, auto vyberá', S2);
  {
    const s = content('Prepínač ciest pod GeoNet', S2);
    // vrstvy vlavo
    const layers = [['CA služba (ETSI)', 'rozhodne, KEDY poslať CAM', C.accent5], ['GeoNet / BTP', 'sieťová vrstva ETSI', C.accent5], ['XgHybridSocket', 'rozhodne, KTOROU cestou', C.accent3]];
    layers.forEach(([t, d, col], i) => {
      const y = 1.55 + i * 1.15;
      s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: M, y, w: 4.2, h: 0.95, rectRadius: 0.08, fill: { color: col }, line: { color: col }, objectName: nm('layer') });
      text(s, [{ text: t, options: { bold: true, fontSize: 17, breakLine: true } }, { text: d, options: { fontSize: 13 } }], { x: M + 0.25, y: y + 0.1, w: 3.8, h: 0.8, color: C.background1, valign: 'middle' });
    });
    const paths = [['FaWifi', HEX.accent1, '802.11p', 'priamo autám a RSU, ~0.3 ms'], ['FaSatelliteDish', HEX.accent2, 'NR-V2X sidelink', 'priamo, väčší dosah, ~10–20 ms'], ['FaBroadcastTower', HEX.accent4, 'LTE', 'cez bunku a server len k RSU, ~18–70 ms']];
    for (let i = 0; i < 3; i++) {
      const y = 1.45 + i * 1.2;
      await iconCircle(s, paths[i][0], 5.95, y, 0.9, paths[i][1], 'FFFFFF');
      text(s, [{ text: paths[i][2], options: { bold: true, fontSize: 17, color: C.text2, breakLine: true } }, { text: paths[i][3], options: { fontSize: 13 } }], { x: 7.0, y: y + 0.05, w: 3.0, h: 0.85, valign: 'middle' });
    }
    s.addShape(pres.shapes.RIGHT_ARROW, { x: 4.95, y: 2.95, w: 0.8, h: 0.5, fill: { color: C.accent3 }, line: { color: C.accent3 }, objectName: 'arrow-switch' });
    text(s, 'Čo sa dá', { x: 10.25, y: 1.45, w: 2.5, h: 0.45, fontSize: 18, bold: true, color: C.text2 });
    bullets(s, ['ľubovoľná podmnožina technológií', 'výpadky technológie počas behu', 'každá CAM všetkými (ALL)', 'adaptívne pravidlo'], { x: 10.25, y: 1.95, w: 2.5, h: 2.6, fontSize: 14 });
    text(s, 'Adaptívne pravidlo: základ je 802.11p; keď RSU nepotvrdzuje príjem, auto prejde na NR a pridá LTE ako zálohu. Pri výpadku sa cesta nahradí hneď, bez straty CAM.', { x: M, y: 5.25, w: W - 2 * M, h: 0.8, fontSize: 15 });
    text(s, 'Pri teste opravené: kolízia adries LTE a NR (dva EPC), RSU ignorovala autá po 33–65 s (pretečenie času CAM), skúška 802.11p sa rátala ako prepnutie.', { x: M, y: 6.1, w: W - 2 * M, h: 0.65, fontSize: 13, color: C.accent5, italic: true });
  }
  {
    const s = content('Jedna technológia nestačí na všetko', S2);
    const rows = [
      [thead('konfigurácia'), thead('auto-auto < 100 m'), thead('100–200 m'), thead('uplink RSU'), thead('vek info RSU'), thead('CAM/s 802.11p / LTE / NR')],
      [tcell('adaptívne (všetky 3)', { bold: true }), tcell('0.999'), tcell('0.52'), tcell('0.998'), tcell('0.21 s', { bold: true, color: C.accent1 }), tcell('99 / 11 / 28')],
      [tcell('len 802.11p'), tcell('1.000'), tcell('0.25'), tcell('1.000'), tcell('7.85 s', { color: C.accent2 }), tcell('120 / 0 / 0')],
      [tcell('len LTE'), tcell('0.000', { color: C.accent2, bold: true }), tcell('0'), tcell('1.000'), tcell('0.24 s'), tcell('0 / 118 / 0')],
      [tcell('len NR-V2X'), tcell('0.986'), tcell('0.98'), tcell('0.986'), tcell('0.23 s'), tcell('0 / 0 / 118')],
      [tcell('všetky naraz'), tcell('1.000'), tcell('0.98'), tcell('1.000'), tcell('0.21 s'), tcell('118 / 118 / 118', { color: C.accent2 })],
      [tcell('len 802.11p, výpadok 40–80 s'), tcell('0.545', { color: C.accent2, bold: true }), tcell('0.14'), tcell('0.596'), tcell('14.3 s'), tcell('80 / 0 / 0')],
    ];
    s.addTable(rows, { x: M, y: 1.45, w: W - 2 * M, colW: [3.3, 1.75, 1.35, 1.5, 1.6, 2.633], border: { type: 'solid', pt: 0.75, color: 'DDE3E6' }, rowH: 0.5, objectName: nm('table-tech') });
    text(s, 'xgregorm_mesto, 30 áut, 120 s, seedy 1–3 (kanál autorov, pred mestským kanálom)', { x: M, y: 5.05, w: 8, h: 0.35, fontSize: 12, color: C.accent5, italic: true });
    bullets(s, [
      [b('LTE '), r('nedoručí CAM susedom (len RSU cez server), 802.11p má malý dosah, NR samo stráca ~5 % kolíziami.')],
      [b('Adaptívne '), r('dá spoľahlivosť „všetko naraz“ pri blízkych autách a RSU s asi tretinou prevádzky.')],
    ], { x: M, y: 5.5, w: W - 2 * M, h: 1.3 });
  }
  {
    const s = content('Pokus: výber podľa hustoty áut (neskôr odstránený)', S2);
    const lab = ['0–100 m', '100–200 m', '200–300 m', '300–450 m', '450–600 m'];
    s.addChart(pres.charts.LINE, [
      { name: 'adaptívne', labels: lab, values: [0.999, 0.51, 0.29, 0.24, 0.17] },
      { name: '+ pravidlo P1 (izolované auto pridá NR)', labels: lab, values: [1.0, 0.69, 0.66, 0.70, 0.68] },
      { name: 'všetky technológie naraz', labels: lab, values: [1.0, 0.99, 0.97, 0.97, 0.93] },
    ], chartBase({ x: M, y: 1.4, w: 7.6, h: 5.3, chartColors: [C.accent5, C.accent3, C.accent1], lineSize: 3, lineDataSymbol: 'circle', lineDataSymbolSize: 8, valAxisMinVal: 0, valAxisMaxVal: 1, valAxisMajorUnit: 0.2, valAxisLabelFormatCode: '0.0', showLegend: true, legendPos: 'b', showTitle: true, title: 'Doručenie CAM auto-auto podľa vzdialenosti, 10 áut', objectName: 'chart-density' }));
    text(s, 'Čo som skúsil', { x: 8.6, y: 1.5, w: 4.1, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    text(s, 'Auto bez suseda na 802.11p za posledné 2 s pošle CAM aj cez NR (P1). Varianty P1b–P1d posielali NR takmer stále.', { x: 8.6, y: 2.0, w: 4.1, h: 1.6, fontSize: 15 });
    text(s, 'Prečo preč', { x: 8.6, y: 3.7, w: 4.1, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    text(s, 'Pri samotných CAM sa hustota prejaví ako riedkosť, nie zahltenie (CBR pod 0.05). P1 pomáhalo len pri 10 autách. Na želanie odstránené, kód zálohovaný ako diff.', { x: 8.6, y: 4.2, w: 4.1, h: 2.4, fontSize: 15 });
  }
  {
    const s = content('Opravy modelu 5G-LENA pre veľké mesto', S2);
    const bugs = [
      ['„Unknown IP type“', 'Pri 500 autách simulácia v 3. s skončila. Paket bez platnej IP hlavičky sa teraz zahodí a započíta.'],
      ['> 255 zariadení NR', 'SCI 2 niesla len 8 bitov ID odosielateľa, ID sa opakovali (IMSI 256 → 0). Plné ID ide ako značka paketu.'],
      ['SIGSEGV v sidelinku', 'Chyba autorov 5G-LENA: prijímací bearer sa vkladal do zlej mapy. Nájdené cez gdb, opravené.'],
    ];
    for (let i = 0; i < 3; i++) {
      const x = M + i * 4.1;
      card(s, x, 1.5, 3.85, 3.2);
      await iconCircle(s, 'FaBug', x + 0.25, 1.75, 0.75, HEX.accent2, 'FFFFFF');
      text(s, bugs[i][0], { x: x + 0.25, y: 2.65, w: 3.4, h: 0.5, fontSize: 18, bold: true, color: C.text2 });
      text(s, bugs[i][1], { x: x + 0.25, y: 3.2, w: 3.4, h: 1.4, fontSize: 14 });
    }
    text(s, 'Čo z toho vyplynulo pre veľkosť scenára', { x: M, y: 5.0, w: W - 2 * M, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    bullets(s, [
      '500 áut už nepadá, no keď sú všetky na NR, beh trvá viac než 1 h na 1 s simulácie',
      'Jedna bunka LTE zvládne asi 280 áut; s realistickým kanálom sa autá znova pripájajú',
      'Série preto bežia so 140 autami, 120 s, štatistiky od 30. s (rozbeh dopravy)',
    ], { x: M, y: 5.5, w: W - 2 * M, h: 1.4 });
  }
  {
    const s = content('Realizmus: budovy, mestský kanál a ďalšia prevádzka', S2);
    const lab = ['0–50', '50–100', '100–150', '150–200', '200–250', '250–300', '300–350', '350–400'];
    s.addChart(pres.charts.LINE, [
      { name: '802.11p, kanál autorov', labels: lab, values: [1.0, 1.0, 0.75, 0, 0, 0, 0, 0] },
      { name: '802.11p, mestský kanál', labels: lab, values: [0.99, 0.75, 0.43, 0.11, 0.11, 0.08, 0.03, 0] },
      { name: 'NR, mestský kanál', labels: lab, values: [0.93, 0.93, 0.86, 0.60, 0.48, 0.28, 0.17, 0.04] },
    ], chartBase({ x: M, y: 1.4, w: 7.4, h: 5.3, chartColors: [C.accent6, C.accent1, C.accent2], lineSize: 3, lineDataSymbol: 'circle', lineDataSymbolSize: 7, valAxisMinVal: 0, valAxisMaxVal: 1, valAxisMajorUnit: 0.2, valAxisLabelFormatCode: '0.0', showLegend: true, legendPos: 'b', showTitle: true, title: 'Doručenie auto-auto podľa vzdialenosti [m], 30 áut', objectName: 'chart-urban' }));
    const items = [
      ['FaBuilding', 'Budovy + 3GPP kanál', 'TR 37.885 (V2V Urban) a TR 38.901 (LTE); za rohom 802.11p padá už pod 100 m'],
      ['FaShieldAlt', 'Podpis ETSI TS 103 097', 'model veľkosti: +93 B, raz za s certifikát +242 B'],
      ['FaTrafficLight', 'SPATEM a MAPEM', 'RSU 10 Hz a 1 Hz cez 802.11p aj NR'],
      ['FaEye', 'CPM všetkých áut', 'objekty zo senzorov, 802.11p sa dostane do pásma DCC'],
    ];
    for (let i = 0; i < 4; i++) {
      const y = 1.45 + i * 1.32;
      await iconCircle(s, items[i][0], 8.3, y, 0.7, HEX.accent1, 'FFFFFF');
      text(s, [{ text: items[i][1], options: { bold: true, fontSize: 16, color: C.text2, breakLine: true } }, { text: items[i][2], options: { fontSize: 13 } }], { x: 9.15, y: y - 0.05, w: 3.6, h: 1.2 });
    }
  }
  {
    const s = content('Z adaptívneho pravidla vzniká výber podľa kvality (Q)', S2);
    text(s, 'Ako auto meria kvalitu', { x: M, y: 1.45, w: 6.4, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    bullets(s, [
      'pre každú priamu technológiu podiel CAM susedov do 100 m doručených načas (≤ 100 ms, 3GPP TS 22.186)',
      'straty určí z poradového čísla, ktoré prepínač zapíše do rezervovaného bajtu hlavičky GeoNet',
      'kĺzavo za ~2 s; skúška druhej technológie 0.5× za sekundu',
      'raz za 1 s prejde, ak je druhá lepšia aspoň o 0.05 a nie je zahltená, s pravdepodobnosťou zisk / 0.4',
    ], { x: M, y: 1.95, w: 6.4, h: 3.6 });
    s.addChart(pres.charts.BAR, [
      { name: 'A: adaptívne', labels: ['skutočné PRR', 'uplink do RSU'], values: [0.903, 0.962] },
      { name: 'E: + výber podľa kvality', labels: ['skutočné PRR', 'uplink do RSU'], values: [0.918, 0.973] },
    ], chartBase({ x: 7.3, y: 1.4, w: 5.45, h: 4.2, barDir: 'col', barGapWidthPct: 70, chartColors: [C.accent5, C.accent1], showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '0.000', valAxisMinVal: 0.85, valAxisMaxVal: 1.0, valAxisMajorUnit: 0.05, valAxisLabelFormatCode: '0.00', showLegend: true, legendPos: 'b', showTitle: true, title: '280 áut, 60 s, štatistiky od 30 s, 1 beh', objectName: 'chart-qos' }));
    card(s, M, 5.75, W - 2 * M, 1.0);
    text(s, [b('Prvý náznak: '), r('pravidlo kvality presunie časť áut na NR (CAM len cez NR 2 % → 35 %), PRR +1.5 p. b., 802.11p menej v pásme DCC. Zatiaľ jeden beh, preto neskôr séria so seedmi.')], { x: M + 0.25, y: 5.88, w: W - 2 * M - 0.5, h: 0.8, fontSize: 15, valign: 'middle' });
  }
  {
    const s = content('Kontrola v grafike: semafory mali obrátené fázy', S2);
    const lab = ['0–30 s', '30–60 s', '60–90 s', '90–120 s', '120–150 s'];
    s.addChart(pres.charts.LINE, [
      { name: 'čisté SUMO, pevné semafory', labels: lab, values: [22, 27, 28, 30, 28] },
      { name: 'ns-3 pred opravou', labels: lab, values: [25, 16, 14, 12, 9.5] },
      { name: 'ns-3 po oprave', labels: lab, values: [27, 30, 32, 32, 32] },
    ], chartBase({ x: M, y: 1.4, w: 7.4, h: 5.3, chartColors: [C.accent6, C.accent2, C.accent1], lineSize: 3, lineDataSymbol: 'circle', lineDataSymbolSize: 8, valAxisMinVal: 0, valAxisMaxVal: 35, valAxisMajorUnit: 5, showLegend: true, legendPos: 'b', showTitle: true, title: 'Priemerná rýchlosť áut [km/h], city-center, 280 áut', objectName: 'chart-phases' }));
    text(s, 'Čo bolo zle', { x: 8.4, y: 1.5, w: 4.3, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    text(s, 'trafficManager má fázu 0 natvrdo pre „sever-juh“, netconvert ju však dáva hlavnej ceste. Na 5 z 9 križovatiek dostal zelenú prázdny smer a rady rástli.', { x: 8.4, y: 2.0, w: 4.3, h: 1.9, fontSize: 15 });
    text(s, 'Oprava a dôsledok', { x: 8.4, y: 4.0, w: 4.3, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    text(s, 'Generátor posunie fázy takej križovatky o 2, kód autorov ostal. Zápchy umelo zlepšovali PRR (0.898 → 0.851 po oprave), preto som kľúčové behy zopakoval.', { x: 8.4, y: 4.5, w: 4.3, h: 2.1, fontSize: 15 });
  }

  // ============ SEKCIA 3 ============
  const S3 = 'Plný realizmus a plán';
  sectionSlide(3, 'Plný realizmus a plán dôkazu', '1.–3. novembra · rušenie, riadenie zahltenia, výbava áut a pravidlá porovnávania', S3);
  {
    const s = content('Čo pribudlo do modelu siete', S3);
    const items = [
      ['FaRandom', 'Rušenie 802.11p a NR', 'pri každom vysielaní sa cudzí signál pridá druhej technológii (coex)'],
      ['FaTachometerAlt', 'DCC podľa ETSI TS 102 687', 'brána 802.11p podľa CBR, fronta správ podľa priority'],
      ['FaSignal', 'Limit CR pre NR', 'ETSI TS 103 574: pri vysokom SL-CBR menej vysielania'],
      ['FaUsers', 'Zmes výbavy áut', 'len 802.11p / len NR / obe / bez V2X'],
      ['FaMobileAlt', 'Telefóny v autách', 'uplink cez LTE, ktorý zaberá bunku'],
      ['FaMap', 'Predikcia z mapy kvality', 'bunky 20 × 20 m, hlásenia cez LTE'],
    ];
    for (let i = 0; i < 6; i++) {
      const col = i % 2, row = Math.floor(i / 2);
      const x = M + col * 6.15, y = 1.45 + row * 1.25;
      await iconCircle(s, items[i][0], x, y, 0.8, HEX.accent1, 'FFFFFF');
      text(s, [{ text: items[i][1], options: { bold: true, fontSize: 17, color: C.text2, breakLine: true } }, { text: items[i][2], options: { fontSize: 14 } }], { x: x + 1.0, y: y - 0.02, w: 4.9, h: 1.0 });
    }
    card(s, M, 5.35, W - 2 * M, 1.4);
    text(s, [b('Kalibrácia a dva nálezy. '), r('Broadcast 802.11p išiel 3 Mbit/s, norma EN 302 663 má 6 Mbit/s. Prah príjmu 802.11p v ns-3 bol −82 dBm, norma žiada −88 dBm, takže 802.11p bolo znevýhodnené o 6 dB. Základ odteraz: 6 Mbit/s, NR 20 MHz na 5.885 GHz vedľa 802.11p, prah −88 dBm (C6U).')], { x: M + 0.25, y: 5.45, w: W - 2 * M - 0.5, h: 1.2, fontSize: 15, valign: 'middle' });
  }
  {
    const s = content('Ako sa porovnáva, aby výsledok platil', S3);
    const steps = [
      ['FaFlask', 'Ladenie', 'seedy 101–103', 'série dev až dev7: hľadanie, kde mechanizmus pomôže'],
      ['FaBalanceScale', 'Stanovenie vopred', 'plán, časť 9', 'kritérium a konfigurácie sa zapíšu pred behmi'],
      ['FaCheckCircle', 'Finálne overenie', 'seedy 1–10', 'nikdy nepoužité na ladenie'],
      ['FaLightbulb', 'Nápady 1–6', 'seedy 201–205', 'vlastné seedy, vlastná referencia'],
    ];
    for (let i = 0; i < 4; i++) {
      const x = M + i * 3.1;
      card(s, x, 1.5, 2.85, 2.8);
      await iconCircle(s, steps[i][0], x + 0.25, 1.75, 0.75, HEX.accent1, 'FFFFFF');
      text(s, steps[i][1], { x: x + 0.25, y: 2.65, w: 2.4, h: 0.45, fontSize: 18, bold: true, color: C.text2 });
      text(s, steps[i][2], { x: x + 0.25, y: 3.1, w: 2.4, h: 0.35, fontSize: 14, bold: true, color: C.accent2 });
      text(s, steps[i][3], { x: x + 0.25, y: 3.5, w: 2.4, h: 0.8, fontSize: 13 });
      if (i < 3) s.addShape(pres.shapes.RIGHT_ARROW, { x: x + 2.88, y: 2.75, w: 0.2, h: 0.3, fill: { color: C.accent6 }, line: { color: C.accent6 }, objectName: nm('arrow') });
    }
    text(s, 'Štatistika', { x: M, y: 4.6, w: 6, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    bullets(s, [
      'párový t-test: obe politiky na rovnakých seedoch, porovnáva sa rozdiel',
      '95 % interval spoľahlivosti; „bez nuly“ = rozdiel je preukázaný',
      'Holmova korekcia pre vedľajšie metriky a pre viac variantov',
    ], { x: M, y: 5.1, w: 6.2, h: 1.7 });
    text(s, 'Kritérium úspechu', { x: 7.1, y: 4.6, w: 5.6, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    bullets(s, [
      'PRR aspoň +0.01 voči referencii a interval bez nuly',
      'nehoršie doručenie CPM a záťaž LTE',
      'neúspešné varianty sa uvádzajú tiež',
    ], { x: 7.1, y: 5.1, w: 5.6, h: 1.7 });
  }
  {
    const s = content('Scenáre: kontrolované zóny zhoršeného príjmu', S3);
    s.addImage({ path: path.join(IMG, 'mapA.png'), x: M, y: 1.3, w: 5.5, h: 5.5 * 822 / 964, objectName: 'map-zones-A', altText: 'Scenár A: zóny pri RSU 0, 2, 5 a 8' });
    text(s, 'Zóna: 150 m okolo RSU, kde jedna technológia stráca 70 % príjmu (tyrkysová 802.11p, oranžová NR). Je to kontrolovaný experiment, nie fyzikálny model.', { x: 6.5, y: 1.45, w: 6.2, h: 1.1, fontSize: 15 });
    const sc = [
      ['A', 'stále zóny', 'RSU 0 a 5 802.11p, RSU 2 a 8 NR (obrázok)'],
      ['C', 'meniace sa v čase', 'v 60. s sa technológie vymenia, v 90. s iné miesta'],
      ['D', 'nevidené', 'zmeny v 50. a 85. s, žiadny model ho pri učení nevidel'],
      ['T', 'bez zón', 'záťaž vzniká len z premávky pri semaforoch'],
    ];
    sc.forEach(([k, t, d], i) => {
      const y = 2.75 + i * 1.0;
      s.addShape(pres.shapes.OVAL, { x: 6.5, y, w: 0.75, h: 0.75, fill: { color: i === 3 ? C.accent5 : C.accent1 }, line: { color: i === 3 ? C.accent5 : C.accent1 }, objectName: nm('sc-badge') });
      text(s, k, { x: 6.5, y, w: 0.75, h: 0.75, fontSize: 22, bold: true, color: C.background1, align: 'center', valign: 'middle', fontFace: THEME.headFontFace });
      text(s, [{ text: t, options: { bold: true, fontSize: 16, color: C.text2, breakLine: true } }, { text: d, options: { fontSize: 13 } }], { x: 7.45, y: y - 0.05, w: 5.3, h: 0.9 });
    });
    text(s, 'Základ: city-center, 140 áut, 120 s, od 30. s, plný realizmus z predošlej snímky.', { x: 6.5, y: 6.7, w: 6.2, h: 0.3, fontSize: 11, color: C.accent5, italic: true });
  }

  // ============ SEKCIA 4 ============
  const S4 = 'Alternatívy k Q';
  sectionSlide(4, 'Alternatívy k výberu podľa aktuálnej kvality', '3.–4. novembra · dá sa vybrať lepšie, ak auto pozerá dopredu alebo sa učí?', S4);
  {
    const s = content('Vyskúšané alternatívy k Q', S4);
    const alts = [
      ['FaMap', 'PM: mapa kvality', 'autá hlásia merania cez LTE do mapy (bunky 20 m); auto pozerá na body, kde bude o 1–3 s', HEX.accent1],
      ['FaRobot', 'GS: naučené stromy', 'GBM predpovedá kvalitu o 3 s zo SPATEM, CPM, CAM susedov a vlastných meraní, bez mapy', HEX.accent4],
      ['FaRandom', 'B / B2: bandit', 'auto sa učí za jazdy z vlastného doručenia v 8 situáciách (pred semaforom, stojí, husto)', HEX.accent3],
      ['FaCrosshairs', 'O0 / O3: orákulum', 'pozná zóny presne, nedá sa postaviť; O3 − O0 = horná hranica pozerania dopredu', HEX.accent5],
    ];
    for (let i = 0; i < 4; i++) {
      const col = i % 2, row = Math.floor(i / 2);
      const x = M + col * 6.15, y = 1.5 + row * 2.45;
      card(s, x, y, 5.9, 2.2);
      await iconCircle(s, alts[i][0], x + 0.25, y + 0.3, 0.85, alts[i][3], 'FFFFFF');
      text(s, alts[i][1], { x: x + 1.35, y: y + 0.3, w: 4.3, h: 0.5, fontSize: 19, bold: true, color: C.text2 });
      text(s, alts[i][2], { x: x + 1.35, y: y + 0.85, w: 4.3, h: 1.2, fontSize: 14 });
    }
    text(s, 'Pravidlo pre všetky: auto smie použiť len to, čo by malo aj v realite (prijaté správy, vlastné merania, navigácia, hlásenia siete). Pravda zo simulátora len pri O*.', { x: M, y: 6.45, w: W - 2 * M, h: 0.5, fontSize: 13, color: C.accent5, italic: true });
  }
  {
    const s = content('Mapa si po zmene pamätá staré hodnoty', S4);
    const lab = ['30–60 s', '60–75 s', '75–90 s', '90–105 s', '105–120 s'];
    s.addChart(pres.charts.LINE, [
      { name: 'MIX (bez výberu)', labels: lab, values: [0.607, 0.604, 0.697, 0.779, 0.769] },
      { name: 'Q', labels: lab, values: [0.761, 0.725, 0.843, 0.851, 0.864] },
      { name: 'PM (mapa)', labels: lab, values: [0.764, 0.609, 0.766, 0.855, 0.865] },
      { name: 'O3 (orákulum)', labels: lab, values: [0.790, 0.772, 0.840, 0.869, 0.870] },
    ], chartBase({ x: M, y: 1.4, w: 7.6, h: 5.3, chartColors: [C.accent6, C.accent1, C.accent2, C.accent5], lineSize: 3, lineDataSymbol: 'circle', lineDataSymbolSize: 8, valAxisMinVal: 0.55, valAxisMaxVal: 0.9, valAxisMajorUnit: 0.05, valAxisLabelFormatCode: '0.00', showLegend: true, legendPos: 'b', showTitle: true, title: 'PRR v úsekoch scenára C (zmena v 60. a 90. s), finálna séria n = 10', objectName: 'chart-map-C' }));
    text(s, 'Čo ukazuje graf', { x: 8.6, y: 1.5, w: 4.1, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    text(s, 'Po výmene technológií v 60. s padne PM na úroveň MIX (0.609), lebo mapa zabúda pomaly (30 s). Q reaguje do pár sekúnd.', { x: 8.6, y: 2.0, w: 4.1, h: 1.9, fontSize: 15 });
    card(s, 8.6, 4.1, 4.1, 2.5);
    text(s, [b('Celkovo PM − Q'), { text: '', options: { breakLine: true } }, r('A: +0.001 (rovnaké)'), { text: '', options: { breakLine: true } }, r('C: −0.029  ·  D: −0.015'), { text: '', options: { breakLine: true } }, r('pri stálych zónach o 29 % menej presunov')], { x: 8.85, y: 4.3, w: 3.6, h: 2.1, fontSize: 15, paraSpaceAfter: 4 });
  }
  {
    const s = content('Lepšia predikcia sa do siete neprenesie', S4);
    const rows = [
      [thead('test na záznamoch'), thead('Q (zostane)'), thead('mapa'), thead('Kalman'), thead('GBM, bez mapy'), thead('najlepšie možné')],
      [tcell('A'), tcell('0.787'), tcell('0.793'), tcell('0.787'), tcell('0.792'), tcell('0.807')],
      [tcell('B'), tcell('0.868'), tcell('0.869'), tcell('0.868'), tcell('0.869'), tcell('0.894')],
      [tcell('C (zmeny)'), tcell('0.778'), tcell('0.750', { color: C.accent2 }), tcell('0.780'), tcell('0.795', { bold: true, color: C.accent1 }), tcell('0.817')],
      [tcell('T (bez zón)'), tcell('0.881'), tcell('0.881'), tcell('0.882'), tcell('0.882'), tcell('0.910')],
    ];
    s.addTable(rows, { x: M, y: 1.45, w: 6.6, colW: [1.5, 1.05, 0.9, 0.95, 1.15, 1.05], border: { type: 'solid', pt: 0.75, color: 'DDE3E6' }, rowH: 0.48, objectName: nm('table-algo') });
    text(s, 'Spoľahlivosť technológie, ktorú by algoritmus zvolil o 3 s; test na seede mimo učenia. MLP a GRU neprekonali GBM.', { x: M, y: 3.95, w: 6.6, h: 0.6, fontSize: 12, color: C.accent5, italic: true });
    text(s, 'V simulácii siete (dev7 → final2)', { x: 7.6, y: 1.45, w: 5.1, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    bullets(s, [
      [b('GS − Q v nevidenom D: '), r('dev7 −0.013, final −0.005 (oboje preukázané)')],
      [b('na záznamoch D '), r('tie isté stromy volia lepšie než Q, v sieti PRR klesne')],
      [b('bandit '), r('aj s plnými skúškami horší než Q (A: −0.009)')],
    ], { x: 7.6, y: 1.95, w: 5.1, h: 2.5 });
    card(s, M, 4.75, W - 2 * M, 2.0);
    text(s, [b('Možné vysvetlenie (neoverené): '), r('predikcia zužuje rozdiel medzi technológiami (rozdiel aspoň 0.05 v 50–55 % prípadov namiesto 72–74 %), takže auto reaguje pomalšie. Hranica pozerania dopredu je aj tak malá: O3 − O0 = +0.007 až +0.011 PRR, kým okamžitá znalosť O0 − Q = +0.010 až +0.021.')], { x: M + 0.25, y: 4.9, w: W - 2 * M - 0.5, h: 1.7, fontSize: 15, valign: 'middle' });
  }

  // ============ SEKCIA 5 ============
  const S5 = 'Niekoľkodňová fronta';
  sectionSlide(5, 'Niekoľkodňová fronta behov', '3.–7. novembra · stovky behov bez dozoru, potom vyhodnotenie', S5);
  {
    const s = content('Ako bežali série bez dozoru', S5);
    const stats = [['538', 'behov simulácie'], ['≈ 588 h', 'výpočtu (súčet behov)'], ['6', 'behov naraz, ~3 GB RAM každý'], ['0', 'zlyhaní a porušení ETSI']];
    stats.forEach(([n, l], i) => {
      const x = M + i * 3.1;
      text(s, n, { x, y: 1.4, w: 2.9, h: 1.0, fontSize: 48, bold: true, color: i === 3 ? C.accent1 : C.text2, fontFace: THEME.headFontFace });
      text(s, l, { x, y: 2.4, w: 2.9, h: 0.5, fontSize: 14, color: C.accent5 });
    });
    const rows = [
      [thead('séria'), thead('behov'), thead('obsah'), thead('kedy')],
      [tcell('dev až dev5'), tcell('136'), tcell('ladenie, kalibrácia, zóny, horná hranica predikcie'), tcell('2.–3. 11.')],
      [tcell('dev6, dev7'), tcell('72'), tcell('mapa, bandit, naučené stromy, nevidený scenár D'), tcell('3.–4. 11.')],
      [tcell('final1 + final2', { bold: true }), tcell('200', { bold: true }), tcell('A, C, D, T × MIX, Q, PM, GS, O0, O3; seedy 1–10'), tcell('4.–6. 11.')],
      [tcell('idea1 + idea26', { bold: true }), tcell('130', { bold: true }), tcell('nápady 1–6 a ich referencie; seedy 201–205'), tcell('6. 11.')],
    ];
    s.addTable(rows, { x: M, y: 3.15, w: 7.6, colW: [1.7, 0.8, 3.9, 1.2], border: { type: 'solid', pt: 0.75, color: 'DDE3E6' }, rowH: 0.45, objectName: nm('table-series') });
    text(s, 'Spúšťač', { x: 8.6, y: 3.15, w: 4.1, h: 0.45, fontSize: 18, bold: true, color: C.text2 });
    bullets(s, [
      'xgregorm_series.py: konfigurácie v JSON, po prerušení preskočí hotové behy',
      'cgroup MemoryHigh 17 GB nechá rezervu pre systém',
      'reťaz spustí ďalšiu sériu sama; malé série spojené do idea26, aby nestáli jadrá',
    ], { x: 8.6, y: 3.65, w: 4.1, h: 3.1, fontSize: 14 });
  }
  {
    const s = content('Finálna séria: každý reaktívny výber porazí MIX', S5);
    s.addChart(pres.charts.BAR, [
      { name: 'Q − MIX', labels: ['A: stále zóny', 'C: meniace sa', 'D: nevidené', 'T: bez zón'], values: [0.134, 0.129, 0.081, 0.021] },
      { name: 'PM − MIX', labels: ['A: stále zóny', 'C: meniace sa', 'D: nevidené', 'T: bez zón'], values: [0.136, 0.100, 0.066, 0.019] },
    ], chartBase({ x: M, y: 1.4, w: 7.8, h: 5.3, barDir: 'col', barGapWidthPct: 60, chartColors: [C.accent1, C.accent4], showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '+0.000', valAxisMinVal: 0, valAxisMaxVal: 0.16, valAxisMajorUnit: 0.04, valAxisLabelFormatCode: '0.00', showLegend: true, legendPos: 'b', showTitle: true, title: 'Zisk PRR voči MIX (50/50 bez výberu), n = 10, všetky intervaly bez nuly', objectName: 'chart-final-mix' }));
    text(s, 'Čo to znamená', { x: 8.8, y: 1.5, w: 3.9, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    bullets(s, [
      'v zónach +0.08 až +0.13 PRR, bez zón ~+0.02',
      'v zónach Q zlepší aj CPM (+0.06 až +0.12), uplink (+0.07 až +0.09) a vek informácie RSU (−0.3 až −0.4 s)',
      'GS − MIX: D +0.076, T +0.017',
      'LTE sa nemení v žiadnom porovnaní',
    ], { x: 8.8, y: 2.0, w: 3.9, h: 4.7, fontSize: 14 });
  }
  {
    const s = content('Finálna séria: rozdiely voči Q a hranica predikcie', S5);
    const g = (t) => tcell(t, { color: C.accent1, bold: true });
    const rd = (t) => tcell(t, { color: C.accent2, bold: true });
    const n = (t) => tcell(t, { color: C.accent5 });
    const rows = [
      [thead('rozdiel PRR (95 % CI)'), thead('A: stále'), thead('C: meniace sa'), thead('D: nevidené'), thead('T: bez zón')],
      [tcell('PM − Q (mapa)'), n('+0.001 (−0.003, +0.006)'), rd('−0.029 (−0.033, −0.025)'), rd('−0.015 (−0.020, −0.010)'), n('−0.003 (−0.006, +0.001)')],
      [tcell('GS − Q (stromy)'), n('–'), n('–'), rd('−0.005 (−0.010, −0.000)'), rd('−0.005 (−0.008, −0.001)')],
      [tcell('O0 − Q (okamžitá znalosť)'), g('+0.010 (+0.006, +0.013)'), g('+0.016 (+0.009, +0.022)'), g('+0.021 (+0.017, +0.025)'), n('–')],
      [tcell('O3 − O0 (pozeranie 3 s dopredu)', { bold: true }), g('+0.011 (+0.008, +0.014)'), n('+0.009 (+0.005, +0.012)'), n('+0.007 (+0.005, +0.008)'), n('–')],
      [tcell('O3 − Q (celý priestor)'), g('+0.020 (+0.018, +0.023)'), g('+0.024 (+0.017, +0.031)'), g('+0.028 (+0.025, +0.031)'), n('–')],
    ];
    s.addTable(rows, { x: M, y: 1.45, w: W - 2 * M, colW: [3.333, 2.2, 2.2, 2.2, 2.2], border: { type: 'solid', pt: 0.75, color: 'DDE3E6' }, rowH: 0.52, objectName: nm('table-final-q') });
    text(s, 'Zelená = preukázane lepšie, oranžová = preukázane horšie, sivá = bez preukázaného rozdielu alebo pod prahom 0.01. n = 10.', { x: M, y: 4.75, w: W - 2 * M, h: 0.35, fontSize: 12, color: C.accent5, italic: true });
    bullets(s, [
      [b('Q ostáva najlepšou realizovateľnou politikou. '), r('Mapa škodí po zmenách, stromy sú o 0.005 horšie, no s 2–7× menej presunmi.')],
      [b('Pozerať dopredu sa oplatí málo: '), r('nad prahom 0.01 len v A. Viac by dala presná znalosť stavu bez skúšok (O0 − Q).')],
    ], { x: M, y: 5.25, w: W - 2 * M, h: 1.5 });
  }
  {
    const s = content('Časový priebeh v nevidenom scenári D', S5);
    const lab = ['30–50 s', '50–65 s', '65–85 s', '85–100 s', '100–120 s'];
    s.addChart(pres.charts.LINE, [
      { name: 'MIX', labels: lab, values: [0.665, 0.749, 0.773, 0.784, 0.753] },
      { name: 'Q', labels: lab, values: [0.745, 0.801, 0.861, 0.858, 0.867] },
      { name: 'PM', labels: lab, values: [0.742, 0.781, 0.833, 0.834, 0.858] },
      { name: 'GS', labels: lab, values: [0.731, 0.815, 0.854, 0.861, 0.850] },
      { name: 'O3', labels: lab, values: [0.794, 0.845, 0.865, 0.875, 0.872] },
    ], chartBase({ x: M, y: 1.4, w: 7.8, h: 5.3, chartColors: [C.accent6, C.accent1, C.accent2, C.accent4, C.accent5], lineSize: 3, lineDataSymbol: 'circle', lineDataSymbolSize: 8, valAxisMinVal: 0.6, valAxisMaxVal: 0.9, valAxisMajorUnit: 0.05, valAxisLabelFormatCode: '0.00', showLegend: true, legendPos: 'b', showTitle: true, title: 'PRR v úsekoch D (zmena zón v 50. a 85. s), n = 10', objectName: 'chart-D' }));
    text(s, 'Pozorovania', { x: 8.8, y: 1.5, w: 3.9, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    bullets(s, [
      'GS je tesne po zmene lepší než Q (50–65 s: 0.815 oproti 0.801), v ustálenom stave stráca',
      'PM zaostáva po každej zmene',
      'medzera Q → O3 je najväčšia na začiatku (0.745 oproti 0.794)',
      'úseky sú bez samostatného testu, len popis',
    ], { x: 8.8, y: 2.0, w: 3.9, h: 4.7, fontSize: 14 });
  }
  {
    const s = content('Nápady 1–6: iné cesty k dobrému výberu', S5);
    const ideas = [
      ['FaClock', 'I1: DCC po správach', 'správa, ktorá by na 802.11p čakala na bránu DCC dlhšie než 20 / 50 / 100 ms, ide cez NR'],
      ['FaUsers', 'I2: výbava susedov', 'technológia, ktorou z prijatých CAM dosiahne viac susedov (zmes áut W, N, WN)'],
      ['FaRoute', 'I3: RSU na trase', 'zo statickej mapy vie, akú technológiu má ďalšia RSU, a prepne vopred'],
      ['FaCopy', 'I4: kópia nablízku', 'kópia CAM druhou technológiou pri blízkej križovatke (30 / 60 / 100 m)'],
      ['FaBuilding', 'I5: geometria LOS/NLOS', 'pred slepou križovatkou (30 / 60 / 100 m) technológia odolnejšia za rohom'],
      ['FaLayerGroup', 'I6R: podľa typu správy', 'CAM cez NR, CPM cez 802.11p, bez merania (doplnené po dev3)'],
    ];
    for (let i = 0; i < 6; i++) {
      const col = i % 3, row = Math.floor(i / 3);
      const x = M + col * 4.1, y = 1.45 + row * 2.55;
      card(s, x, y, 3.85, 2.35);
      await iconCircle(s, ideas[i][0], x + 0.25, y + 0.25, 0.7, HEX.accent3, 'FFFFFF');
      text(s, ideas[i][1], { x: x + 1.1, y: y + 0.3, w: 2.6, h: 0.6, fontSize: 16, bold: true, color: C.text2, valign: 'middle' });
      text(s, ideas[i][2], { x: x + 0.25, y: y + 1.1, w: 3.4, h: 1.15, fontSize: 13 });
    }
    text(s, 'Referencia bez reaktívnej logiky: FW (len 802.11p); pre I2 a I3 rovnaká výbava bez výberu (EF, RF). Seedy 201–205, n = 5.', { x: M, y: 6.6, w: W - 2 * M, h: 0.35, fontSize: 12, color: C.accent5, italic: true });
  }
  {
    const s = content('Nápady: zisk PRR voči stavu bez reaktívnej logiky', S5);
    const lab = ['Q (ref.)', 'I1a 20 ms', 'I1b 50 ms', 'I1c 100 ms', 'I2a', 'I2b', 'I3a', 'I3b', 'I4a 30 m', 'I4b 60 m', 'I4c 100 m', 'I5a 30 m', 'I5b 60 m', 'I5c 100 m', 'I6R'];
    s.addChart(pres.charts.BAR, [
      { name: 'zisk PRR', labels: lab, values: [0.053, 0.053, 0.028, -0.002, 0.097, 0.142, 0.018, 0.017, 0.032, 0.046, 0.063, 0.030, 0.034, 0.038, 0.101] },
    ], chartBase({ x: M, y: 1.4, w: 8.6, h: 5.3, barDir: 'bar', barGapWidthPct: 35, chartColors: [C.accent1], showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '+0.000;-0.000', valAxisMinVal: -0.02, valAxisMaxVal: 0.16, valAxisMajorUnit: 0.04, valAxisLabelFormatCode: '0.00', catAxisOrientation: 'maxMin', catAxisLabelFontSize: 11, showLegend: false, showTitle: true, title: 'Rozdiel PRR voči vlastnej referencii (FW, pri I2 EF, pri I3 RF)', objectName: 'chart-ideas' }));
    text(s, 'Výsledok', { x: 9.5, y: 1.5, w: 3.2, h: 0.45, fontSize: 20, bold: true, color: C.text2 });
    bullets(s, [
      [b('kritérium splnili '), r('všetky varianty okrem I1c')],
      [b('najviac: '), r('I2b +0.142, I6R +0.101')],
      [b('I3 '), r('uplink +0.16 až +0.19, vek informácie RSU z 5–7 s na 0.4 s')],
      [b('I1c '), r('(100 ms) bez preukázaného rozdielu voči FW')],
    ], { x: 9.5, y: 2.0, w: 3.2, h: 4.7, fontSize: 14 });
  }
  {
    const s = content('Každý mechanizmus má inú cenu', S5);
    const lab = ['FW', 'MIX', 'Q', 'I1a', 'I4c', 'I6R', 'ALL'];
    s.addChart(pres.charts.BAR, [
      { name: 'PRR (CAM)', labels: lab, values: [0.855, 0.890, 0.908, 0.908, 0.918, 0.956, 0.945] },
      { name: 'doručenie CPM', labels: lab, values: [0.773, 0.853, 0.843, 0.853, 0.770, 0.833, 0.739] },
    ], chartBase({ x: M, y: 1.4, w: 7.8, h: 5.3, barDir: 'col', barGapWidthPct: 50, chartColors: [C.accent1, C.accent3], showValue: true, dataLabelPosition: 'outEnd', dataLabelFormatCode: '0.00', dataLabelFontSize: 10, valAxisMinVal: 0.6, valAxisMaxVal: 1.0, valAxisMajorUnit: 0.1, valAxisLabelFormatCode: '0.0', showLegend: true, legendPos: 'b', showTitle: true, title: 'Bez zón, seedy 201–205, priemer', objectName: 'chart-tradeoff' }));
    const notes = [
      ['I1a', 'rovnaké PRR aj CPM ako Q, bez merania kvality a bez skúšok'],
      ['I4c', 'PRR o 0.010 nad Q, ale CPM ostáva ako pri FW'],
      ['I6R', 'PRR +0.048 nad Q; CPM o 0.020 horšie než MIX. V zónach D sa neprispôsobí: CPM −0.122 voči Q'],
      ['ALL', 'vysoké PRR, ale 4.2× viac CAM cez LTE a horšie CPM aj SPATEM'],
    ];
    notes.forEach(([k, t], i) => {
      const y = 1.5 + i * 1.3;
      text(s, k, { x: 8.8, y, w: 0.9, h: 0.5, fontSize: 18, bold: true, color: C.accent2 });
      text(s, t, { x: 9.7, y, w: 3.0, h: 1.2, fontSize: 14 });
    });
  }

  // ============ ZAVER ============
  const S6 = 'Záver';
  pres.addSection({ title: S6 });
  {
    const s = pres.addSlide({ masterName: 'SECTION', sectionTitle: S6 });
    s.addText('Celkový záver', { placeholder: 'title' });
    s.addText('Čo ukázala celá fronta, posúdené voči stavu bez reaktívnej logiky', { placeholder: 'body' });
    await iconCircle(s, 'FaCheckCircle', 10.6, 2.3, 2.0, HEX.accent1, 'FFFFFF');
  }
  {
    const s = content('Čo z práce vyplýva', S6);
    const pts = [
      ['FaCheckCircle', 'Každý výber pomáha', 'Bez reaktívnej logiky (FW, FN, MIX) je PRR najnižšie. Q, PM, GS aj nápady 1–6 (okrem I1c) ho preukázateľne zlepšia.'],
      ['FaChartLine', 'Pri zmenách vyhráva reaktívny výber', 'V zónach dá Q +0.08 až +0.13 PRR voči MIX. Statické pravidlá ani mapa sa po zmene neprispôsobia.'],
      ['FaLightbulb', 'Bez zón stačia jednoduché pravidlá', 'I1a vyrovná Q bez merania, I6R ho v PRR prekoná. Cenou je CPM alebo SPATEM.'],
      ['FaEye', 'Predikcia má malý priestor', 'Pozeranie 3 s dopredu: najviac +0.011 PRR. Mapa ani stromy ho v sieti nevyužili.'],
      ['FaUsers', 'Najväčší prínos pri rôznej výbave', 'Výber podľa susedov +0.10 až +0.14 PRR; podľa RSU na trase vek informácie zo 5–7 s na 0.4 s.'],
    ];
    for (let i = 0; i < 5; i++) {
      const y = 1.4 + i * 1.08;
      await iconCircle(s, pts[i][0], M, y, 0.75, i === 3 ? HEX.accent5 : HEX.accent1, 'FFFFFF');
      text(s, pts[i][1], { x: M + 1.0, y: y + 0.02, w: 3.9, h: 0.75, fontSize: 17, bold: true, color: C.text2, valign: 'middle' });
      text(s, pts[i][2], { x: 5.6, y: y + 0.02, w: 7.1, h: 0.85, fontSize: 14, valign: 'middle' });
    }
  }
  {
    const s = content('Obmedzenia, ktoré treba v práci uviesť', S6);
    bullets(s, [
      [b('Zóny '), r('sú kontrolovaný experiment (strata 70 %), nie fyzikálny model; hranica predikcie platí len pre ne')],
      [b('SL-CBR pre NR '), r('je odhad z dekódovaných správ (dolná hranica), nie meranie podľa normy')],
      [b('Jedna bunka LTE '), r('a 140 áut; väčšie scenáre s NR sú výpočtovo nepoužiteľné')],
      [b('Rozšírenie normy: '), r('značka technológie a poradové číslo v rezervovanom bajte hlavičky GeoNet')],
      [b('Zjednodušenie vstupov: '), r('poloha suseda zo SUMO v čase príjmu, čas do zmeny semafora zo SUMO, keď auto má čerstvú SPATEM')],
      [b('DCC: '), r('Toff úmerné dĺžke rámca nad 1 ms je môj predpoklad (opora v TS 102 687 Annex B)')],
      [b('Nápady '), r('sa overili na 5 seedoch (finálna séria na 10); I6R bol zaradený až po pohľade na dev3')],
    ], { x: M, y: 1.5, w: 7.9, h: 5.3, fontSize: 15, paraSpaceAfter: 8 });
    card(s, 8.9, 1.5, 3.85, 5.0);
    await iconCircle(s, 'FaExclamationTriangle', 9.15, 1.75, 0.75, HEX.accent3, 'FFFFFF');
    text(s, 'Na čo si dať pozor pri písaní', { x: 9.15, y: 2.65, w: 3.4, h: 0.8, fontSize: 17, bold: true, color: C.text2 });
    text(s, 'Zmeny v kóde autorov (opravy 5G-LENA, trafficManager, oprava A) treba popísať oddelene od prínosu vlastných mechanizmov. Zoznam je v tabuľke „Moje zmeny“ v CLAUDE.md.', { x: 9.15, y: 3.5, w: 3.4, h: 2.8, fontSize: 14 });
  }
  {
    const s = content('Kde čo nájdeš v repozitári', S6);
    const files = [
      ['FaBook', 'CLAUDE.md', 'pravidlá, moje zmeny v kóde autorov, Denník všetkých meraní'],
      ['FaHistory', 'xgregorm_STAV_PRACE.md', 'aktuálny stav, časy behov, prenos na iný počítač'],
      ['FaFlask', 'xgregorm_PLAN_PREDIKCIA.md', 'hypotézy, kritériá stanovené pred behmi, celkový záver'],
      ['FaDatabase', 'xgregorm_logs/', 'logy všetkých sérií, definície v JSON, spúšťač'],
      ['FaChartLine', 'xgregorm_final_eval.py, xgregorm_idea_eval.py', 'prepočet tabuliek finálnej série a nápadov z logov'],
      ['FaCogs', 'xgregorm_gen_scenario.py, xgregorm-hybrid.cc', 'generátor scenárov a hybridný príklad v ns-3'],
    ];
    for (let i = 0; i < 6; i++) {
      const col = i % 2, row = Math.floor(i / 2);
      const x = M + col * 6.15, y = 1.5 + row * 1.7;
      card(s, x, y, 5.9, 1.45);
      await iconCircle(s, files[i][0], x + 0.25, y + 0.32, 0.8, HEX.accent1, 'FFFFFF');
      text(s, files[i][1], { x: x + 1.3, y: y + 0.2, w: 4.4, h: 0.5, fontSize: 15, bold: true, color: C.text2 });
      text(s, files[i][2], { x: x + 1.3, y: y + 0.68, w: 4.4, h: 0.7, fontSize: 13 });
    }
  }

  await pres.writeFile({ fileName: OUT });
  await applyTheme(OUT, THEME);
  console.log('written', OUT);
})().catch((e) => { console.error(e); process.exit(1); });
