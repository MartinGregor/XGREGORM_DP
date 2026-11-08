# Hruby nahlad slidov (bez LibreOffice): tvary, obrazky, text so zalamovanim podla skutocnych pisiem; hlasi pretecenie textu.
import sys, io, os
from pptx import Presentation
from pptx.util import Emu
from pptx.enum.shapes import MSO_SHAPE_TYPE
from PIL import Image, ImageDraw, ImageFont

F = '/mnt/c/Windows/Fonts/'
FONTS = {('Calibri', 0): 'calibri.ttf', ('Calibri', 1): 'calibrib.ttf', ('Cambria', 0): 'cambria.ttc', ('Cambria', 1): 'cambriab.ttf'}
THEME = {'tx1': '1B2730', 'dk1': '1B2730', 'tx2': '0B3C49', 'dk2': '0B3C49', 'bg1': 'FFFFFF', 'lt1': 'FFFFFF', 'bg2': 'EEF3F4', 'lt2': 'EEF3F4',
         'accent1': '2A9D8F', 'accent2': 'E76F51', 'accent3': 'F2A541', 'accent4': '7B6FB0', 'accent5': '5C7480', 'accent6': 'B8C2C7'}
DPI = 100
fcache = {}
def font(face, bold, pt):
    k = (face, bold, pt)
    if k not in fcache:
        fcache[k] = ImageFont.truetype(F + FONTS[(face, 1 if bold else 0)], int(round(pt * DPI / 72)))
    return fcache[k]
def px(e): return int(round(Emu(e).inches * DPI))

def color_of(el):
    from lxml import etree
    ns = {'a': 'http://schemas.openxmlformats.org/drawingml/2006/main'}
    s = el.find('.//a:solidFill', ns) if el is not None else None
    if s is None: return None
    c = s[0]
    v = c.get('val')
    return '#' + (THEME.get(v, v) if c.tag.endswith('schemeClr') else v)

def wrap(txt, f, width):
    lines = []
    for para in txt.split('\n'):
        words, cur = para.split(' '), ''
        for w in words:
            t = (cur + ' ' + w).strip()
            if f.getlength(t) <= width or not cur: cur = t
            else: lines.append(cur); cur = w
        lines.append(cur)
    return lines

def layout_ph_size(shape):
    try:
        lp = shape.placeholder_format
        for ph in shape.part.slide.slide_layout.placeholders:
            if ph.placeholder_format.idx == lp.idx or ph.placeholder_format.type == lp.type:
                for p in ph.text_frame.paragraphs:
                    for r in p.runs:
                        if r.font.size: return r.font.size.pt, r.font.bold
                rp = ph._element.find('.//{http://schemas.openxmlformats.org/drawingml/2006/main}defRPr')
                if rp is not None and rp.get('sz'): return int(rp.get('sz')) / 100, rp.get('b') == '1'
    except Exception: pass
    return 18, False

issues = []
def draw_text(d, shape, x, y, w, h, label, default_face='Calibri'):
    tf = shape.text_frame
    ins = 0
    total = 0; items = []
    for p in tf.paragraphs:
        txt = ''.join(r.text for r in p.runs)
        if not p.runs: txt = ''
        size, bold = None, False
        for r in p.runs:
            if r.font.size: size = r.font.size.pt; bold = bool(r.font.bold); break
        if size is None:
            if shape.is_placeholder: size, bold = layout_ph_size(shape)
            else: size = 18
        face = 'Cambria' if (shape.is_placeholder and shape.placeholder_format.type in (1, 3)) else default_face
        if p.runs and p.runs[0].font.name and 'Cambria' in p.runs[0].font.name: face = 'Cambria'
        f = font(face, bold, size)
        indent = 20 if p._p.find('.//{http://schemas.openxmlformats.org/drawingml/2006/main}buChar') is not None or p._p.find('.//{http://schemas.openxmlformats.org/drawingml/2006/main}buAutoNum') is not None else 0
        lines = wrap(txt, f, w - indent - 2)
        lh = size * 1.2 * DPI / 72
        col = None
        if p.runs: col = color_of(p.runs[0]._r.find('{http://schemas.openxmlformats.org/drawingml/2006/main}rPr'))
        items.append((lines, f, lh, indent, col or '#1B2730'))
        total += lh * len(lines) + 6
    total -= 6
    if total > h + 2:
        issues.append(f'{label}: text needs {total:.0f}px, box {h}px :: {tf.text[:60]!r}')
    yy = y
    for lines, f, lh, ind, col in items:
        for ln in lines:
            d.text((x + ind, yy), ln, font=f, fill=col)
            yy += lh
        yy += 6

def render(path, outdir):
    prs = Presentation(path)
    W, H = px(prs.slide_width), px(prs.slide_height)
    os.makedirs(outdir, exist_ok=True)
    for i, s in enumerate(prs.slides, 1):
        bg = '#FFFFFF'
        name = s.slide_layout.name
        if name in ('COVER', 'SECTION'): bg = '#0B3C49'
        img = Image.new('RGB', (W, H), bg); d = ImageDraw.Draw(img)
        shapes = list(s.slide_layout.placeholders) if False else []
        for sh in list(s.slide_layout.shapes) + list(s.shapes):
            if sh.is_placeholder and sh in s.slide_layout.shapes: continue
            if sh.left is None: continue
            x, y, w, h = px(sh.left), px(sh.top), px(sh.width), px(sh.height)
            if x + w > W + 1 or y + h > H + 1 or x < 0 or y < 0:
                issues.append(f'slide {i}: {sh.name} off-slide ({x},{y},{w},{h})')
            if sh.shape_type == MSO_SHAPE_TYPE.PICTURE:
                im = Image.open(io.BytesIO(sh.image.blob)).convert('RGBA').resize((max(w,1), max(h,1)))
                img.paste(im, (x, y), im)
            elif sh.has_chart if hasattr(sh, 'has_chart') else False:
                d.rectangle([x, y, x + w, y + h], outline='#999', width=2); d.text((x + 10, y + 10), 'CHART: ' + (sh.chart.chart_title.text_frame.text if sh.chart.has_title else ''), fill='#555', font=font('Calibri', 0, 12))
            elif sh.has_table if hasattr(sh, 'has_table') else False:
                tb = sh.table; cy = y
                for ri, row in enumerate(tb.rows):
                    rh = px(row.height); cx = x; need = rh
                    for ci, cell in enumerate(row.cells):
                        cw = px(tb.columns[ci].width)
                        d.rectangle([cx, cy, cx + cw, cy + rh], outline='#ccc')
                        f = None; sz = 12; bold = False
                        for p in cell.text_frame.paragraphs:
                            for r in p.runs:
                                if r.font.size: sz = r.font.size.pt; bold = bool(r.font.bold)
                        f = font('Calibri', bold, sz)
                        lines = wrap(cell.text_frame.text, f, cw - 12)
                        hh = len(lines) * sz * 1.2 * DPI / 72 + 8
                        need = max(need, hh)
                        for k, ln in enumerate(lines): d.text((cx + 6, cy + 4 + k * sz * 1.2 * DPI / 72), ln, font=f, fill='#000')
                        cx += cw
                    if need > rh + 2: issues.append(f'slide {i}: table row {ri} grows {rh}->{need:.0f}px')
                    cy += max(rh, need)
                if cy > H - 45: issues.append(f'slide {i}: table bottom at {cy}px (slide {H})')
            else:
                fill = None
                try:
                    if sh.fill.type == 1: fill = color_of(sh._element.find('.//{http://schemas.openxmlformats.org/presentationml/2006/main}spPr'))
                except Exception: pass
                if fill:
                    if 'Oval' in sh.name or (sh.auto_shape_type is not None and 'OVAL' in str(sh.auto_shape_type)):
                        d.ellipse([x, y, x + w, y + h], fill=fill)
                    else:
                        d.rounded_rectangle([x, y, x + w, y + h], radius=8, fill=fill)
                if sh.has_text_frame and sh.text_frame.text.strip():
                    draw_text(d, sh, x, y, w, h, f'slide {i} {sh.name}')
        img.save(os.path.join(outdir, f's{i:02d}.png'))
    return len(prs.slides)

n = render(sys.argv[1], sys.argv[2])
print(n, 'slides'); print('\n'.join(issues) or 'no issues')
