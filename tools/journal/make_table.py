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


def main():
    name, img_dir, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
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

    t = {'J': {}, 'T': {}}
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
    for kind in 'JT':
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
    table['journals'] = [j for j in table['journals'] if j['sha256'] != L['sha256']]
    table['journals'].append({'name': name, 'game': L['game'], 'title': L['title'], 'sha256': L['sha256'],
                              'pdf_size': L['pdf_size'], 'pdf_id': L['pdf_id'], 'entries': entries})
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
            for p, x, y, w, h in e['p']:
                pieces.append(f'    {{{p}, {x}, {y}, {w}, {h}}},')
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
