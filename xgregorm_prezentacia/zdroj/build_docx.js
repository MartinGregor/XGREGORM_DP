// Prevod Markdown -> Word (.docx). Pouzitie: NODE_PATH=<node_modules s docx@9.0.2> node build_docx.js a.md [b.md ...] out.docx
// Kazdy dalsi subor zacina na novej strane.
// Podporuje: # / ## / ### nadpisy, odseky, zoznamy "- " a "1. " (aj vnorene "   - " pod cislovanym), tabulky "| ... |", **tucne**, *kurziva*.
const fs = require('fs');
const d = require('docx');
const args = process.argv.slice(2);
const out = args.pop();
const lines = args.map(f => fs.readFileSync(f, 'utf8')).join('\n').split('\n');

const DK = '0B3C49', ACC = '2A9D8F', LIGHT = 'E3F1EF', BORDER = 'B8C2C7';
const PAGE_W = 11906, MARGIN = 1300, TEXT_W = PAGE_W - 2 * MARGIN;

function runs(text, base = {}) {
  const out = [];
  for (const part of text.split(/(\*\*[^*]+\*\*|\*[^*]+\*)/)) {
    if (!part) continue;
    if (part.startsWith('**')) out.push(new d.TextRun({ ...base, text: part.slice(2, -2), bold: true }));
    else if (part.startsWith('*')) out.push(new d.TextRun({ ...base, text: part.slice(1, -1), italics: true }));
    else out.push(new d.TextRun({ ...base, text: part }));
  }
  return out;
}

let numInst = 0;
const children = [];
let afterTitle = false;
let i = 0;
while (i < lines.length) {
  const l = lines[i];
  if (!l.trim()) { i++; continue; }
  const wasTitle = afterTitle; afterTitle = false;
  if (l.startsWith('# ')) {
    children.push(new d.Paragraph({ heading: d.HeadingLevel.TITLE, pageBreakBefore: children.length > 0, children: runs(l.slice(2)) }));
    afterTitle = true; i++; continue;
  } else if (l.startsWith('## ')) {
    children.push(new d.Paragraph({ heading: d.HeadingLevel.HEADING_1, children: runs(l.slice(3)) }));
  } else if (l.startsWith('### ')) {
    children.push(new d.Paragraph({ heading: d.HeadingLevel.HEADING_2, children: runs(l.slice(4)) }));
  } else if (l.startsWith('|')) {
    const rows = [];
    while (i < lines.length && lines[i].startsWith('|')) {
      const cells = lines[i].trim().slice(1, -1).split('|').map(s => s.trim());
      if (!cells.every(c => /^-+$/.test(c))) rows.push(cells);
      i++;
    }
    const n = rows[0].length;
    // prvy stlpec uzsi, ostatne rovnomerne
    const first = Math.round(TEXT_W * (n >= 5 ? 0.24 : 0.22));
    const rest = Math.floor((TEXT_W - first) / (n - 1));
    const widths = [first, ...Array(n - 1).fill(rest)];
    widths[n - 1] += TEXT_W - widths.reduce((a, b) => a + b, 0);
    const bd = { style: d.BorderStyle.SINGLE, size: 4, color: BORDER };
    children.push(new d.Table({
      width: { size: TEXT_W, type: d.WidthType.DXA },
      columnWidths: widths,
      rows: rows.map((r, ri) => new d.TableRow({
        tableHeader: ri === 0,
        children: r.map((c, ci) => new d.TableCell({
          width: { size: widths[ci], type: d.WidthType.DXA },
          borders: { top: bd, bottom: bd, left: bd, right: bd },
          shading: ri === 0 ? { type: d.ShadingType.CLEAR, color: 'auto', fill: DK } : (ri % 2 === 0 ? { type: d.ShadingType.CLEAR, color: 'auto', fill: LIGHT } : undefined),
          margins: { top: 60, bottom: 60, left: 100, right: 100 },
          children: [new d.Paragraph({ spacing: { after: 0 }, children: runs(c, ri === 0 ? { bold: true, color: 'FFFFFF', size: 19 } : { size: 19 }) })],
        })),
      })),
    }));
    children.push(new d.Paragraph({ spacing: { after: 120 }, children: [] }));
    continue;
  } else if (/^- /.test(l)) {
    while (i < lines.length && /^- /.test(lines[i])) {
      children.push(new d.Paragraph({ numbering: { reference: 'bul', level: 0 }, spacing: { after: 60 }, children: runs(lines[i].slice(2)) }));
      i++;
    }
    continue;
  } else if (/^\d+\. /.test(l)) {
    const inst = numInst++;
    while (i < lines.length && (/^\d+\. /.test(lines[i]) || /^\s+- /.test(lines[i]))) {
      if (/^\s+- /.test(lines[i])) // vnoreny bod pod cislovanym
        children.push(new d.Paragraph({ numbering: { reference: 'bul', level: 1 }, spacing: { after: 40 }, children: runs(lines[i].replace(/^\s+- /, '')) }));
      else
        children.push(new d.Paragraph({ numbering: { reference: 'num', level: 0, instance: inst }, spacing: { after: 80 }, children: runs(lines[i].replace(/^\d+\. /, '')) }));
      i++;
    }
    continue;
  } else {
    const first = wasTitle; // podnadpis pod titulom
    children.push(new d.Paragraph({ spacing: { after: 140 }, children: runs(l, first ? { italics: true, color: '5C7480' } : {}) }));
  }
  i++;
}

const H = (size, color, before, after) => ({ run: { font: 'Cambria', size, bold: true, color }, paragraph: { spacing: { before, after }, keepNext: true, keepLines: true } });
const doc = new d.Document({
  creator: 'Martin Gregor',
  title: 'Diplomová práca jednoducho',
  styles: {
    default: {
      document: { run: { font: 'Calibri', size: 22, color: '1B2730' }, paragraph: { spacing: { line: 276 } } },
      title: { run: { font: 'Cambria', size: 44, bold: true, color: DK }, paragraph: { spacing: { before: 0, after: 200 }, keepNext: true } },
      heading1: H(30, DK, 360, 140),
      heading2: H(25, ACC, 240, 100),
    },
  },
  numbering: {
    config: [
      { reference: 'bul', levels: [{ level: 0, format: d.LevelFormat.BULLET, text: '•', alignment: d.AlignmentType.LEFT, style: { paragraph: { indent: { left: 500, hanging: 280 } } } }, { level: 1, format: d.LevelFormat.BULLET, text: '–', alignment: d.AlignmentType.LEFT, style: { paragraph: { indent: { left: 950, hanging: 280 } } } }] },
      { reference: 'num', levels: [{ level: 0, format: d.LevelFormat.DECIMAL, text: '%1.', alignment: d.AlignmentType.LEFT, style: { paragraph: { indent: { left: 500, hanging: 320 } } } }] },
    ],
  },
  sections: [{
    properties: { page: { size: { width: PAGE_W, height: 16838 }, margin: { top: 1200, bottom: 1200, left: MARGIN, right: MARGIN } } },
    footers: { default: new d.Footer({ children: [new d.Paragraph({ alignment: d.AlignmentType.CENTER, children: [new d.TextRun({ children: [d.PageNumber.CURRENT], size: 18, color: '5C7480' })] })] }) },
    children,
  }],
});
d.Packer.toBuffer(doc).then(b => { fs.writeFileSync(out, b); console.log('written', out, children.length, 'blocks'); });
