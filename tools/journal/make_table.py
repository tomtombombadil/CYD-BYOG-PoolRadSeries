"""Makes a journal's rectangle table (coordinates only) for the converter.

Claude's side tool, run on the player's own journal PDF in scratch space -
never on files in the repo. Extract the page images first:
    pdfimages -j "<journal>.pdf" <dir>/p
then
    python3 -I tools/journal/make_table.py curse <dir> <out.json>

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


LAYOUTS = {
    'curse': {
        'sha256': '7c918ead661b353e7da606b9137929f1dee2c9c203408b0b751cf719cd8529bb',
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

    items = []
    for i, (fi, c, y, _) in enumerate(kept):
        if i < L['first_tale']:
            items.append(('J', L['journal_numbers'][i], fi, c, y))
        else:
            items.append(('T', i - L['first_tale'] + 1, fi, c, y))

    def pieces(fi, c, y, end, stop):
        out = []
        while True:
            last = end is not None and end[0] == fi and end[1] == c
            y1 = end[2] - 14 if last else BOTTOM
            x0, x1 = cols[c]
            if y1 - y >= 30:
                out.append([fi + 1, x0, y, x1 - x0, y1 - y])
            if last or (fi, c) == stop:
                return out
            c += 1
            if c == 4:
                c, fi = 0, fi + 1
            y = TOP

    t = {'J': {}, 'T': {}}
    for k, (kind, n, fi, c, y) in enumerate(items):
        nxt = items[k + 1][2:] if k + 1 < len(items) and items[k + 1][0] == kind else None
        t[kind][n] = pieces(fi, c, y - 14, nxt, L['stop'][kind])
    L['fix'](t)

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

    try:
        table = json.load(open(out_path))
    except (OSError, ValueError):
        table = {'format': 1, 'dpi': 150, 'journals': []}
    table['journals'] = [j for j in table['journals'] if j['sha256'] != L['sha256']]
    table['journals'].append({'game': L['game'], 'title': L['title'], 'sha256': L['sha256'], 'entries': entries})
    with open(out_path, 'w') as f:
        json.dump(table, f, separators=(',', ':'))
    print(f"{name}: {sum(e['k'] == 'J' for e in entries)} journal entries, "
          f"{sum(e['k'] == 'T' for e in entries)} tavern tales")


if __name__ == '__main__':
    main()
