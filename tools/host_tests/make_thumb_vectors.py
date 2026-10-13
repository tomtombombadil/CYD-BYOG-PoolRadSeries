"""Writes thumb_vectors.h: a small synthetic picture (made here - nothing
from any game or launcher) saved as a progressive and as a baseline JPEG,
and its quarter-size average the thumbnail decoder must come close to.

    python3 tools/host_tests/make_thumb_vectors.py > tools/host_tests/thumb_vectors.h
"""
import io

from PIL import Image, ImageDraw


def picture(w, h):
    im = Image.new('RGB', (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = (x * 255 // (w - 1), y * 255 // (h - 1), 128)
    d = ImageDraw.Draw(im)
    d.rectangle([16, 16, 31, 31], fill=(200, 30, 30))     # squares of a chroma block
    d.rectangle([48, 32, 63, 47], fill=(20, 40, 220))
    d.rectangle([4, 36, 7, 39], fill=(255, 255, 255))      # a quarter of a block
    return im


def main():
    w, h = 72, 56
    im = picture(w, h)
    out = ['// Made by make_thumb_vectors.py - a synthetic picture only.', '#pragma once', '#include <cstdint>', '']
    out.append(f'static const int kThumbW = {w}, kThumbH = {h};')
    for name, kw in (('Prog', dict(progressive=True)), ('Base', {})):
        b = io.BytesIO()
        im.save(b, 'JPEG', quality=95, subsampling=2, **kw)
        data = b.getvalue()
        out.append(f'static const uint8_t kThumb{name}[] = {{' + ','.join(str(x) for x in data) + '};')
    # The quarter-size reference: each 4 x 4 square's average of the decoded picture
    dec = Image.open(io.BytesIO(data)).convert('RGB')
    q = dec.resize((w // 4, h // 4), Image.BOX)
    out.append('static const uint8_t kThumbQuarter[] = {' + ','.join(str(x) for x in q.tobytes()) + '};')
    print('\n'.join(out))


main()
