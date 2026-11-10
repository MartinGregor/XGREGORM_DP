# Docistenie .docx z docx-js: odstrani polozky priecinkov a nepripojeny word/fontTable.xml (+ jeho rels a content type).
import sys, zipfile, re, shutil, os
path = sys.argv[1]
tmp = path + '.tmp'
drop = {'word/fontTable.xml', 'word/_rels/fontTable.xml.rels'}
with zipfile.ZipFile(path) as zi, zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as zo:
    for it in zi.infolist():
        if it.filename.endswith('/') or it.filename in drop:
            continue
        data = zi.read(it.filename)
        if it.filename == '[Content_Types].xml':
            data = re.sub(rb'<Override[^>]*fontTable\.xml"[^>]*/>', b'', data)
        zo.writestr(it.filename, data)
shutil.move(tmp, path)
print('fixed', path)
