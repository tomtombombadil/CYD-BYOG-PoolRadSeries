"""Makes a journal's rectangle table (coordinates only) for the converter.

Claude's side tool, run on the player's own journal PDF in scratch space -
never on files in the repo. Extract the page images first:
    pdfimages -j "<journal>.pdf" <dir>/p
then
    python3 -I tools/journal/make_table.py curse <dir> src/engine/journal_tables.cpp
It rewrites the whole file from all the journals' saved tables
(tools/journal/tables.json, coordinates only), adding or replacing this one.

How it works: a heading ("Journal Entry N", "Tavern Tale N") starts with a
hollow blue box; the boxes are found on each page spread (two book pages,
two columns each). An entry runs from its heading to the next one, column
by column, page by page; each piece is then trimmed to the rows with ink.
Per-journal facts (where the runs stop, entries out of order, pictures
across both columns) are in LAYOUTS below; they were checked by eye
against the rendered entries.
"""
import glob
import json
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

TOP, BOTTOM = 95, 1200        # below the page ornament, above the page number


def find_boxes(path):
    a = np.asarray(Image.open(path).convert('RGB')).astype(int)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    blue = ((b - r) > 45) & ((r + g + b) < 600)
    lab, _ = ndimage.label(blue)
    out = []
    for i, sl in enumerate(ndimage.find_objects(lab)):
        h = sl[0].stop - sl[0].start
        w = sl[1].stop - sl[1].start
        if 15 <= h <= 30 and 15 <= w <= 30 and abs(h - w) <= 6:
            sub = lab[sl] == i + 1
            inner = sub[3:-3, 3:-3]
            if inner.size and inner.mean() < 0.25 and sub.mean() > 0.2:
                out.append((sl[1].start, sl[0].start))
    return out


def curse_layout(t):
    # Page 22: Entry 51 ends at the top of both columns; Entry 52 is a map
    # across both
    t['J'][51] = t['J'][51][:-1] + [[12, 868, 95, 384, 274 - 14 - 95], [12, 1254, 95, 382, 274 - 14 - 95]]
    t['J'][52] = [[12, 868, 274 - 14, 768, BOTTOM - 260]]
    # Page 24: Entry 59 is a map across both columns under Entry 58
    t['J'][58] = [[13, 1254, 102, 382, 471 - 14 - 102]]
    t['J'][59] = [[13, 868, 471 - 14, 768, BOTTOM - 457]]
    # Page 28: a picture between the columns
    for n in (44, 45, 46, 47, 48):
        t['T'][n] = [[p, x, y, min(w, 1232 - x), h] for p, x, y, w, h in t['T'][n]]
    for n in (52, 53):
        t['T'][n] = [[p, 1343, y, 1636 - 1343, h] for p, x, y, w, h in t['T'][n]]
    # Page 1: Entry 1 under the introduction's title, both columns
    t['J'][1] = [[2, 55, 250, 382, BOTTOM - 250], [2, 440, 232, 377, BOTTOM - 232]]


def ocr_lines(path):
    """The page's text lines (tesseract): [(x, y, h, words)] - words as
    (x, y, h, text); the OCR is kept beside the picture (<page>.tsv)"""
    import subprocess
    if os.path.exists(path + '.tsv'):
        out = open(path + '.tsv').read()
    else:
        out = subprocess.run(['tesseract', path, '-', 'tsv'], capture_output=True, text=True).stdout
        with open(path + '.tsv', 'w') as f:
            f.write(out)
    lines = {}
    for row in out.splitlines()[1:]:
        f = row.split('\t')
        if len(f) < 12 or not f[11].strip():
            continue
        k = (f[2], f[3], f[4])
        lines.setdefault(k, []).append((int(f[6]), int(f[7]), int(f[9]), f[11]))
    res = []
    for ws in lines.values():
        res.append((min(w[0] for w in ws), min(w[1] for w in ws), max(w[2] for w in ws), ws))
    return res


def pool_heads(files, L):
    """Pool's headings are italic text, not boxes: "Journal Entry N:" lines
    and, on the tavern page, "Tale N:" at a column's start (the number
    token "N:" just right of "Tale", which OCR reads more surely)"""
    import re
    heads = []
    for fi, f in enumerate(files):
        if fi not in L['pages']:
            continue
        for x, y, h, ws in ocr_lines(f):
            text = ' '.join(w[3] for w in ws)
            m = re.match(r'\s*J\w*rnal\s+Entry\s+(\d+)\s*:', text)
            if m:
                heads.append(('J', int(m.group(1)), fi, x, y))
                continue
            if fi == L['tale_page']:
                for wx, wy, wh, t in ws:
                    m = re.fullmatch(r'(\d+):', t)
                    if m:
                        for c, (a, _) in enumerate(L['cols']):
                            if 20 <= wx - a <= 72:
                                heads.append(('T', int(m.group(1)), fi, a, wy))
    return heads


def entry_heads(files, L):
    """Headings "Journal Entry N" at a line's start (Secret, Darkness,
    Gateway: printed text, a box glyph before it in some books - OCR reads
    it as a short junk word); Darkness's "JOURNAL" is sometimes lost, the
    "ENTRY N" then starts the line"""
    import re
    heads = []
    for fi, f in enumerate(files):
        if fi not in L['pages']:
            continue
        for x, y, h, ws in ocr_lines(f):
            ws = sorted(ws)
            toks = [w[3] for w in ws]
            for i, t in enumerate(toks[:4]):
                if not re.fullmatch(r'(?i)entry', t) or i + 1 >= len(toks):
                    continue
                m = re.fullmatch(r'(\d+)[.:]?', toks[i + 1])
                if not m:
                    continue
                j = i - 1 if i > 0 and re.search(r'(?i)rnal$', toks[i - 1]) else i
                if any(len(toks[k]) > 3 for k in range(j)):
                    continue            # (inside a sentence)
                heads.append(('J', int(m.group(1)), fi, ws[j][0], min(ws[j][1], ws[i][1])))
                break
    return heads


def P(fi):
    """A PDF page number (1-based) from the picture's index"""
    return fi + 1


def secret_layout(t):
    J = t['J']
    # PDF page 16: Journal Entry 1 under the section's title, the map
    # across the left page
    J[1] = [[P(15), 45, 105, 785 - 45, 1222 - 105]]
    # PDF page 19: maps beside / under the text
    p = P(18)
    J[32] = [[p, 423, 95, 407, 520 - 95]]
    J[33] = [J[33][0], [p, 423, 530, 407, 772 - 530]]
    J[35] = [[p, 860, 97, 1333 - 860, 995 - 97]]
    J[36] = [[p, 1333, 97, 1625 - 1333, 505 - 97]]
    J[37] = [[p, 860, 1000, 1333 - 860, 1222 - 1000], [p, 1333, 510, 1625 - 1333, 1222 - 510]]
    # PDF page 20: the map of entry 38 across the left page, 39 and 40 under it
    p = P(19)
    J[38] = [[p, 45, 97, 785 - 45, 470 - 97]]
    J[39] = [[p, 45, 557, 423 - 45, 1222 - 557]]
    J[40] = [[p, 423, 467, 407, 1222 - 467]]
    # PDF page 21: entry 44 goes on over the page above the rule; 45's map
    # across the left page; 47 under it on the right
    J[44] = J[44][:1] + [[P(20), 45, 85, 378, 295 - 85], [P(20), 423, 85, 407, 295 - 85]]
    p = P(20)
    J[45] = [[p, 45, 308, 795 - 45, 678 - 308]]       # (the map wraps round 47's heading: it shows there)
    J[46] = [[p, 45, 685, 423 - 45, 1222 - 685]]
    J[47] = [[p, 423, 620, 407, 1222 - 620]]
    J[48] = [[p, 860, 97, 1246 - 860, 280 - 97], [p, 1246, 97, 1625 - 1246, 270 - 97]]
    J[49] = [[p, 860, 272, 1625 - 860, 725 - 272]]
    J[50] = [[p, 860, 728, 1246 - 860, 1222 - 728], [p, 1246, 728, 1625 - 1246, 1222 - 728]]
    # PDF page 24: 70 and 71 above a rule across the right page, 72's map under it
    p = P(23)
    J[70] = [[p, 860, 366, 1246 - 860, 540 - 366], [p, 1246, 85, 1625 - 1246, 210 - 85]]
    J[71] = [[p, 1246, 210, 1625 - 1246, 540 - 210]]
    J[72] = [[p, 860, 554, 1625 - 860, 880 - 554]]


def darkness_layout(t):
    J = t['J']
    # PDF page 19: 36's map reaches into the right column, 38's text
    # wraps round it (38's heading two lines: OCR found "ENTRY 38")
    p = P(18)
    J[36] = [[p, 35, 190, 383, 760 - 190], [p, 35, 760, 560 - 35, 1125 - 760]]   # (bits of 37 and 38 show beside the map)
    J[37] = J[37][:-1] + [J[37][-1][:4] + [795 - J[37][-1][2]]]
    J[38] = [[p, 470, 795, 835 - 470, 1123 - 795], [p, 418, 1123, 835 - 418, 1240 - 1123]] + J[38][1:]


def gateway_layout(t):
    J = t['J']
    # PDF page 12: the section's title over the right page's columns
    J[2] = J[2][:1]
    J[4] = J[4][:1]
    # PDF page 13: entry 5 under the map of the Savage Frontier (no entry's),
    # along the foot of both pages
    p = P(12)
    J[5] = [[p, 22, 970, 376, 1245 - 970], [p, 398, 948, 417, 1245 - 948],
            [p, 850, 948, 385, 1245 - 948], [p, 1235, 948, 390, 1245 - 948]]
    # PDF page 14: 7's map reaches into the right column, 9 and 10 wrap round it
    p = P(13)
    # (one piece, so the map stays whole: a few letters of 9 / 10 at its right edge)
    J[7] = [[p, 22, 727, 458 - 22, 1245 - 727]]
    J[9] = [[p, 398, 499, 417, 835 - 499], [p, 470, 835, 345, 922 - 835]]
    J[10] = [[p, 420, 922, 815 - 420, 1245 - 922]] + J[10][1:]
    # PDF page 21: 61's map under the left column, 62 ends at its column's middle
    p = P(20)
    J[61] = [[p, 22, 829, 432 - 22, 1245 - 829]]
    J[62] = [[p, 398, 81, 417, 700 - 81]]
    # PDF page 3: "Introduction: The First Night in Yartar", the left page's
    # two columns - the start's text: "... as the introduction to your
    # journal" (kind 'I', number 1)
    p = P(2)
    t['I'][1] = [[p, 26, 70, 392, 1165], [p, 418, 70, 392, 1165]]


# Gateway's picture-and-quote boxes between the columns' text (no entry's):
# (PDF page, x0, x1, y0, y1)
GATEWAY_SKIP = [(14, 0, 398, 70, 345), (15, 1235, 1625, 327, 564), (16, 1235, 1625, 300, 545),
                (17, 0, 398, 227, 500), (18, 0, 398, 264, 527), (19, 1235, 1625, 364, 610),
                (20, 0, 398, 218, 477)]


def pool_layout(t):
    # Entry 2 ends at its column's foot (above entry 3: the section's title
    # and introduction across the right page)
    t['J'][2] = t['J'][2][:1]
    # PDF page 10: the "Adventurer's note: Legend for all maps" box across
    # the left book page's foot belongs to no entry
    for kind in 'JT':
        for n in t[kind]:
            t[kind][n] = [[p, x, y, w, min(h, 778 - y) if p == 10 and x < 660 else h]
                          for p, x, y, w, h in t[kind][n]]
    # Entry 37 (PDF page 14): an atlas across the whole spread under its
    # heading - the left book page, then the right one
    t['J'][37] = [[14, 40, 15, 615, 935], [14, 690, 15, 620, 935]]
    # Tale 5 ends at its column's foot (the title above tale 6 is the intro's)
    t['T'][5] = t['T'][5][:1]


LAYOUTS = {
    'secret': {
        'sha256': 'f26c90627c1cdb2dfacf92ad3f795545a659c7b8c6d0f44a8996f8f3a9a48a74',
        'pdf_size': 12954891,
        'pdf_id': 'fa8b4348516de925a6dc4187f9cee0e9',
        'game': 'SECRET',
        'title': "Secret of the Silver Blades - Adventurer's Journal (GOG, the copy in the game folder)",
        'cols': [(45, 423), (423, 830), (860, 1246), (1246, 1625)],
        'top': 85, 'bottom': 1222,
        'pages': range(15, 24),
        'heads': entry_heads,
        'stop': {'J': (23, 3)},
        'fix': secret_layout,
    },
    'darkness': {
        'sha256': '6677dc33be83d3fc1769dda9c01417b7a91e3433b7d39cdf9ecaaa995e32f2e8',
        'pdf_size': 12922380,
        'pdf_id': 'f4444e4fa1807022f22fd406bb68757d',
        'game': 'DARKNESS',
        'title': "Pools of Darkness - Adventurer's Journal (GOG, the copy in the game folder)",
        'cols': [(35, 418), (418, 835), (860, 1248), (1248, 1630)],
        'top': 95, 'bottom': 1240,
        'pages': range(14, 25),
        'heads': entry_heads,
        'stop': {'J': (24, 3)},
        'fix': darkness_layout,
    },
    'gateway': {
        'sha256': '1e3588f7738933d802d57499ff9648277dc7f824662240ded6a8f7b19d49ab41',
        'pdf_size': 10963455,
        'pdf_id': 'f893d7ffe8bf1c3f956c77942c401ddb',
        'game': 'GATEWAY',
        'title': "Gateway to the Savage Frontier - Journal (GOG, the copy in the game folder)",
        'cols': [(22, 398), (398, 815), (850, 1235), (1235, 1625)],
        'top': 70, 'bottom': 1245,
        'pages': range(11, 21),
        'heads': entry_heads,
        'stop': {'J': (20, 1)},
        'fix': gateway_layout,
        'skip': GATEWAY_SKIP,
    },
    'poolrad': {
        'sha256': '67cbfc0c833b835494310680ad298bc4de1cdcc0168115cc3608c2f6074c737c',
        'pdf_size': 6325424,
        'pdf_id': '1d9365f3923839543e16fb839aa4631a',
        'game': 'POOLRAD',
        'title': "Pool of Radiance - Adventurer's Journal (GOG, the copy in the game folder)",
        'cols': [(40, 345), (345, 655), (695, 1000), (1000, 1310)],
        'top': 12, 'bottom': [952, 968, 968, 952],      # (page numbers at the outer columns' feet)
        'pages': range(8, 18),                           # the entries' and tales' spreads (0-based)
        'tale_page': 17,
        'heads': pool_heads,
        'stop': {'J': (16, 3), 'T': (17, 3)},
        'fix': pool_layout,
    },
    'curse': {
        'sha256': '7c918ead661b353e7da606b9137929f1dee2c9c203408b0b751cf719cd8529bb',
        'pdf_size': 6779800,
        'pdf_id': '36ce730a4edce39469cb5215e05df63d',     # the trailer's /ID
        'game': 'CURSE',
        'title': "Curse of the Azure Bonds - Adventurer's Journal (GOG, the copy in the game folder)",
        'cols': [(55, 437), (440, 817), (868, 1252), (1254, 1636)],
        'first_tale': 58,                                 # heading number where Tavern Tales start
        'journal_numbers': list(range(2, 58)) + [59, 58],  # 59 comes before 58 in the book
        'stop': {'J': (12, 3), 'T': (15, 1)},            # last spread / column of each run
        'fix': curse_layout,
    },
}


# ---- Treasures of the Savage Frontier: several pictures a PDF page ----------
# Its GOG journal is a book spread a PDF page, each book page a picture of
# its own (72 dpi, grayscale) with the pages' heading strips as more small
# pictures. The table's pieces say which picture: [page, x, y, w, h, image],
# image = the index in the page's /XObject dictionary (the order the
# engine's pdf::page_image counts). Run on the PDF itself:
#     python3 -I tools/journal/make_table.py treasures <journal.pdf> src/engine/journal_tables.cpp
# (its pictures are written beside the PDF, in <pdf>_pictures/, for the
# contact sheets).

TREASURES = {
    'pdf_size': 2214499,
    'pdf_id': '85ba1d9c69464c31353b7ac318df3153',
    'game': 'TREASURES',
    'title': "Treasures of the Savage Frontier - Adventurer's Journal (GOG, the copy in the game folder)",
    'first': (13, 0), 'last': (27, 2),           # the book pages with the entries: PDF page, picture
    'not_entries': [(15, 0), (16, 2)],           # the Savage Frontier map across them (no entry's)
    'top': 30, 'bottom': 548,                    # below the heading strip
    # The introduction (entry 0: the game's "you record his words as the
    # introduction to your journal" - Tom, 2026-10-24): PDF page 1's right
    # book page (its title and the full-width opening, then both columns)
    # and page 2's left one (both columns); checked by eye
    'intro': [[1, 8, 36, 344, 123, 0], [1, 8, 165, 168, 307, 0], [1, 176, 165, 168, 347, 0],
              [2, 11, 83, 168, 408, 3], [2, 180, 82, 168, 363, 3]],
}


def pdf_pictures(path, out_dir):
    """Each page's pictures in its /XObject order: [(page, index, w, h, x)]
    (x: where the page puts it), the JPEGs written to out_dir"""
    import re
    import zlib
    pdf = open(path, 'rb').read()
    objs = {}
    for m in re.finditer(rb'(?<![0-9])(\d+) 0 obj', pdf):
        objs.setdefault(int(m.group(1)), m.end())

    def body(n):
        return pdf[objs[n]:pdf.find(b'endobj', objs[n])]

    def stream(n):
        b = body(n)
        d = b[b.find(b'stream') + 6:]
        d = d[2:] if d[:2] == b'\r\n' else d[1:] if d[:1] in (b'\r', b'\n') else d
        return b, d[:int(re.search(rb'/Length (\d+)', b).group(1))]

    def kids(n):
        b = body(n)
        if re.search(rb'/Type\s*/Page(?!s)', b):
            return [n]
        return [q for x in re.findall(rb'(\d+) 0 R', re.search(rb'/Kids\s*\[([^\]]*)\]', b).group(1))
                for q in kids(int(x))]

    root = int(re.search(rb'/Root (\d+) 0 R', pdf[pdf.rfind(b'trailer'):]).group(1))
    pages = kids(int(re.search(rb'/Pages (\d+) 0 R', body(root)).group(1)))
    os.makedirs(out_dir, exist_ok=True)
    res = []
    for pi, pn in enumerate(pages):
        pb = body(pn)
        rm = re.search(rb'/Resources (\d+) 0 R', pb)
        rs = body(int(rm.group(1))) if rm else pb
        names = re.findall(rb'/(\w+) (\d+) 0 R', re.search(rb'/XObject\s*<<(.*?)>>', rs, re.S).group(1))
        hdr, cs = stream(int(re.search(rb'/Contents (\d+) 0 R', pb).group(1)))
        if b'FlateDecode' in hdr:
            cs = zlib.decompress(cs)
        place = {m.group(2): float(m.group(1)) for m in
                 re.finditer(rb'(?:[-\d.]+ ){4}([-\d.]+) [-\d.]+ cm\s*/(\w+) Do', cs)}
        for k, (nm, on) in enumerate(names):
            ib, data = stream(int(on))
            w = int(re.search(rb'/Width (\d+)', ib).group(1))
            h = int(re.search(rb'/Height (\d+)', ib).group(1))
            open(os.path.join(out_dir, 't-%02d-%d.jpg' % (pi + 1, k)), 'wb').write(data)
            res.append((pi + 1, k, w, h, place.get(nm, 0.0)))
    return res


def treasures_main(pdf_path):
    import re
    import subprocess
    L = TREASURES
    out_dir = pdf_path + '_pictures'
    pics = pdf_pictures(pdf_path, out_dir)
    # The book pages in reading order: each PDF page's big pictures, left first
    book = sorted([(p, x, k) for p, k, w, h, x in pics if h >= 500], key=lambda a: (a[0], a[1]))
    book = [(p, k) for p, x, k in book]
    book = book[book.index(L['first']):book.index(L['last']) + 1]
    book = [b for b in book if b not in L['not_entries']]
    files = {b: os.path.join(out_dir, 't-%02d-%d.jpg' % b) for b in book}
    grey = {b: np.asarray(Image.open(files[b]).convert('L')).astype(int) for b in book}

    # Each book page's two columns: split at the gutter, out to the ink (a
    # page where a map crosses the gutter: the others' usual split)
    cols = {}
    for b in book:
        c = (grey[b] < 160)[60:520].sum(axis=0)
        # (the gutter: the widest run of empty columns there; none - a map
        # crosses it - the others' split)
        runs, start = [], None
        for x in range(140, 221):
            if x < 220 and c[x] == 0:
                start = x if start is None else start
            elif start is not None:
                runs.append((x - start, start, x - 1))
                start = None
        xs = np.nonzero((c > 0) & (c < 300))[0]       # (not the scan's dark edge)
        edges = (max(0, int(xs[0]) - 4), min(grey[b].shape[1], int(xs[-1]) + 5))
        if runs and max(runs)[0] >= 3:
            _, a, z = max(runs)
            cols[b] = [(edges[0], (a + z + 1) // 2), ((a + z + 1) // 2, edges[1])]
        else:
            cols[b] = [edges]
    usual = int(np.median([cols[b][0][1] for b in cols if len(cols[b]) == 2]))
    for b in book:
        if len(cols[b]) == 1:
            cols[b] = [(cols[b][0][0], usual), (usual, cols[b][0][1])]

    # The quote boxes between a column's text (a picture and a saying between
    # two dark rules across the column): no entry's
    skip = {}
    for b in book:
        ink = grey[b] < 180             # (some pages' rules are a light grey)
        for c, (x0, x1) in enumerate(cols[b]):
            runs = []
            for y in range(L['top'], grey[b].shape[0]):
                if ink[y, x0 + 5:x1 - 5].sum() >= 0.75 * (x1 - x0 - 20):
                    if runs and y - runs[-1][1] <= 2:
                        runs[-1][1] = y
                    else:
                        runs.append([y, y])
            i = 0
            while i < len(runs):
                pair = [r for r in runs[i + 1:] if 90 <= r[0] - runs[i][0] <= 130]
                if pair:
                    skip.setdefault((b, c), []).append((runs[i][0] - 4, pair[0][1] + 5))
                    i = runs.index(pair[0]) + 1
                else:
                    i += 1

    # The headings: "JOURNAL ENTRY N" at a line's start (tesseract on the
    # picture at 3x); numbers read wrong are put right by the book's order
    heads = []
    for b in book:
        big = files[b][:-4] + '_3x.png'
        if not os.path.exists(big):
            Image.open(files[b]).convert('L').resize((grey[b].shape[1] * 3, grey[b].shape[0] * 3),
                                                     Image.LANCZOS).save(big)
        if not os.path.exists(big + '.tsv'):
            with open(big + '.tsv', 'w') as f:
                f.write(subprocess.run(['tesseract', big, '-', 'tsv'], capture_output=True, text=True).stdout)
        lines = {}
        for row in open(big + '.tsv').read().splitlines()[1:]:
            f = row.split('\t')
            if len(f) < 12 or not f[11].strip():
                continue
            lines.setdefault((f[2], f[3], f[4]), []).append((int(f[6]) // 3, int(f[7]) // 3, f[11]))
        for ws in lines.values():
            ws.sort()
            toks = [w[2] for w in ws]
            for i, t in enumerate(toks[:3]):
                m = (re.fullmatch(r'(\d+)[.:]?', toks[i + 1])
                     if re.fullmatch(r'(?i)entry', t) and i + 1 < len(toks) else None)
                if m:
                    j = i - 1 if i > 0 and re.search(r'(?i)rnal$', toks[i - 1]) else i   # (not a junk mark before it)
                    c = 0 if ws[j][0] < 140 else 1
                    heads.append((book.index(b), c, ws[j][1], int(m.group(1))))
                    break
    heads.sort()
    for i in range(1, len(heads) - 1):
        a, n, z = heads[i - 1][3], heads[i][3], heads[i + 1][3]
        if n != a + 1 and z == a + 2:
            heads[i] = heads[i][:3] + (a + 1,)
    got = [h[3] for h in heads]
    print('J', len(got), 'missing', sorted(set(range(1, max(got) + 1)) - set(got)),
          'out of order', [n for i, n in enumerate(got) if i and n != got[i - 1] + 1])

    def pieces(bi, c, y, end):
        out = []
        while True:
            b = book[bi]
            last = end is not None and end[:2] == (bi, c)
            y1 = end[2] - 4 if last else L['bottom']
            x0, x1 = cols[b][c]
            parts = [(y, y1)]
            for s0, s1 in skip.get((b, c), []):
                parts = [q for a, z in parts for q in ((a, min(z, s0)), (max(a, s1), z)) if q[1] - q[0] >= 12]
            out += [[b[0], x0, a, x1 - x0, z - a, b[1]] for a, z in parts if z - a >= 12]
            if last or (end is None and bi == len(book) - 1 and c == 1):
                return out
            c += 1
            if c == 2:
                c, bi = 0, bi + 1
            y = L['top']

    entries = []
    for i, (bi, c, y, n) in enumerate(heads):
        ps = []
        for p, x, y0, w, h, k in pieces(bi, c, y - 4, heads[i + 1] if i + 1 < len(heads) else None):
            g = grey[(p, k)]
            m = g[y0:y0 + h, x + 3:x + w - 3] < 160
            low = max(0, 533 - y0)          # (a page number at the foot's outer corner: not the entry's)
            m[low:, :24] = False
            m[low:, -24:] = False
            rows = np.nonzero(m.sum(axis=1) >= 2)[0]
            if len(rows) == 0:
                continue
            a = max(y0, y0 + int(rows[0]) - 3)
            z = min(y0 + h, y0 + int(rows[-1]) + 4)
            ps.append([p, x, a, w, z - a, k])
        entries.append({'k': 'J', 'n': n, 'p': ps})
    # Checked by eye: entry 2 ends at its column's foot; maps wider than a
    # column: entry 65's under its heading (82's map in its column whole)
    # on PDF page 24's left page (67's text wraps round it - its column
    # starts right of the map there), 83's on page 27's left page
    for e in entries:
        if e['n'] == 2:             # (the right column's top is the section's title)
            e['p'] = e['p'][:1]
        if e['n'] == 65:
            p = e['p'][0]
            e['p'] = [[24, p[1], p[2], 189 - p[1], 488 - p[2], 2]]
        if e['n'] == 67:
            e['p'] = [[q[0], 189, q[2], q[1] + q[3] - 189, q[4], q[5]] if q[0] == 24 and q[5] == 2 else q
                      for q in e['p']]
        if e['n'] == 82:            # (its map's long walls are no quote box's rules)
            p = e['p'][0]
            e['p'] = [[26, p[1], p[2], p[3], 462 - p[2], 0]]
        if e['n'] == 83:
            p = e['p'][0]
            e['p'] = [[27, p[1], p[2], 336 - p[1], 420 - p[2], 2]]
    return [{'k': 'J', 'n': 0, 'p': L['intro']}] + entries


def main():
    name, img_dir, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    if name == 'treasures':
        import hashlib
        L = TREASURES
        entries = treasures_main(img_dir)
        store = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tables.json')
        table = json.load(open(store))
        table['journals'] = [j for j in table['journals'] if j['name'] != name]
        table['journals'].append({'name': name, 'game': L['game'], 'title': L['title'],
                                  'sha256': hashlib.sha256(open(img_dir, 'rb').read()).hexdigest(),
                                  'pdf_size': L['pdf_size'], 'pdf_id': L['pdf_id'], 'entries': entries})
        with open(store, 'w') as f:
            json.dump(table, f, separators=(',', ':'))
        write_cpp(table, out_path)
        print(f"{name}: {len(entries)} journal entries")
        return
    L = LAYOUTS[name]
    cols = L['cols']
    files = sorted(glob.glob(img_dir + '/p-*.jpg'))

    def col_of(x):
        for i, (a, _) in enumerate(cols):
            if a - 30 <= x <= a + 105:
                return i
        return None

    top, bottom = L.get('top', TOP), L.get('bottom', BOTTOM)
    items = []
    if 'heads' in L:
        for kind, n, fi, x, y in L['heads'](files, L):
            c = col_of(x)
            if c is not None:
                items.append((kind, n, fi, c, y))
        # one each, in the book's order (journal entries, then tales)
        seen, uniq = set(), []
        for it in sorted(items, key=lambda i: (i[0] != 'J', i[2], i[3], i[4])):
            if it[:2] not in seen:
                seen.add(it[:2])
                uniq.append(it)
        items = uniq
        for kind in 'JT':
            got = sorted(i[1] for i in items if i[0] == kind)
            print(kind, len(got), 'missing', sorted(set(range(1, max(got) + 1)) - set(got)) if got else '-')
    else:
        heads = []
        for fi, f in enumerate(files):
            if fi in (0, len(files) - 1):
                continue
            for x, y in find_boxes(f):
                c = col_of(x)
                if c is not None:
                    heads.append((fi, c, y, x))
        heads.sort()
        kept = []
        for h in heads:      # letters like "o" look like boxes too: keep the leftmost on a line
            if kept and kept[-1][:2] == h[:2] and abs(kept[-1][2] - h[2]) < 15:
                if h[3] < kept[-1][3]:
                    kept[-1] = h
                continue
            kept.append(h)
        for i, (fi, c, y, _) in enumerate(kept):
            if i < L['first_tale']:
                items.append(('J', L['journal_numbers'][i], fi, c, y))
            else:
                items.append(('T', i - L['first_tale'] + 1, fi, c, y))

    def pieces(fi, c, y, end, stop):
        out = []
        while True:
            last = end is not None and end[0] == fi and end[1] == c
            y1 = end[2] - 14 if last else (bottom[c] if isinstance(bottom, list) else bottom)
            x0, x1 = cols[c]
            if y1 - y >= 30:
                out.append([fi + 1, x0, y, x1 - x0, y1 - y])
            if last or (fi, c) == stop:
                return out
            c += 1
            if c == 4:
                c, fi = 0, fi + 1
            y = top

    t = {'J': {}, 'T': {}, 'I': {}}
    for k, (kind, n, fi, c, y) in enumerate(items):
        nxt = items[k + 1][2:] if k + 1 < len(items) and items[k + 1][0] == kind else None
        t[kind][n] = pieces(fi, c, y - 14, nxt, L['stop'][kind])
    L['fix'](t)
    # Cut out what belongs to no entry (boxes between a column's text)
    for kind in t:
        for n in t[kind]:
            out = []
            for p, x, y, w, h in t[kind][n]:
                parts = [(y, y + h)]
                for sp, x0, x1, s0, s1 in L.get('skip', []):
                    if sp != p or x >= x1 or x + w <= x0:
                        continue
                    parts = [q for a, b in parts for q in ((a, min(b, s0)), (max(a, s1), b)) if q[1] - q[0] >= 30]
                out += [[p, x, a, w, b - a] for a, b in parts]
            t[kind][n] = out

    # Trim each piece to the rows with ink (not the faint show-through)
    pages = {}

    def ink(p):
        if p not in pages:
            a = np.asarray(Image.open(files[p - 1]).convert('RGB')).astype(int)
            lum = a.sum(axis=2) / 3
            blue = (a[..., 2] - a[..., 0]) > 45
            pages[p] = (lum < 140) | (blue & (lum < 200))
        return pages[p]

    entries = []
    for kind in 'JTI':
        for n in sorted(t[kind]):
            ps = []
            for p, x, y, w, h in t[kind][n]:
                rows = np.nonzero(ink(p)[y:y + h, x + 6:x + w - 6].sum(axis=1) >= 2)[0]
                if len(rows) == 0:
                    continue
                y0 = max(y, y + int(rows[0]) - 6)
                y1 = min(y + h, y + int(rows[-1]) + 8)
                ps.append([p, x, y0, w, y1 - y0])
            entries.append({'k': kind, 'n': n, 'p': ps})

    store = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tables.json')
    try:
        table = json.load(open(store))
    except (OSError, ValueError):
        table = {'format': 1, 'dpi': 150, 'journals': []}
    new = {'name': name, 'game': L['game'], 'title': L['title'], 'sha256': L['sha256'],
           'pdf_size': L['pdf_size'], 'pdf_id': L['pdf_id'], 'entries': entries}
    at = [i for i, j in enumerate(table['journals']) if j['sha256'] == L['sha256']]
    if at:
        table['journals'][at[0]] = new          # (in its place: the C++ file's order stays)
    else:
        table['journals'].append(new)
    with open(store, 'w') as f:
        json.dump(table, f, separators=(',', ':'))
    write_cpp(table, out_path)
    print(f"{name}: {sum(e['k'] == 'J' for e in entries)} journal entries, "
          f"{sum(e['k'] == 'T' for e in entries)} tavern tales")


def write_cpp(table, path):
    out = ['// Made by tools/journal/make_table.py - do not edit.',
           '// Where each journal entry is in the player\'s own journal PDF: PDF page',
           '// numbers and pixel rectangles of the page pictures. No journal content.',
           '#include "journal.h"', '', 'namespace journal {', '', 'namespace {', '']
    names = []
    for j in table['journals']:
        n = j['name']
        names.append(n)
        pieces, defs = [], []
        for e in j['entries']:
            defs.append(f"    {{'{e['k']}', {e['n']}, {len(pieces)}, {len(e['p'])}}},")
            for q in e['p']:
                # (a 6th number: which of the page's pictures - Treasures)
                pieces.append('    {' + ', '.join(str(v) for v in q) + '},')
        out.append(f"// {j['title']}")
        out.append(f'const Piece k_{n}_pieces[] = {{')
        out += pieces
        out.append('};')
        out.append(f'const EntryDef k_{n}_entries[] = {{')
        out += defs
        out.append('};')
        out.append(f'const Table k_{n} = {{"{j["title"]}", {j["pdf_size"]}u, "{j["pdf_id"]}", k_{n}_entries,')
        out.append(f'    {len(defs)}, k_{n}_pieces, {len(pieces)}}};')
        out.append('')
    out.append('} // namespace')
    out.append('')
    out.append('const Table* const kTables[] = {' + ', '.join(f'&k_{n}' for n in names) + '};')
    out.append(f'const int kTableCount = {len(names)};')
    out.append('')
    out.append('} // namespace journal')
    with open(path, 'w') as f:
        f.write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
