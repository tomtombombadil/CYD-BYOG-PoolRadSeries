"""Writes jpeg_vectors.h: two small synthetic JPEGs (a made-up pattern, made
here - nothing from any game) for the journal / JPEG tests, and the RGB
pixels PIL decodes them to (the decoder must come close).

    python3 tools/host_tests/make_jpeg_vectors.py > tools/host_tests/jpeg_vectors.h
"""
import io

from PIL import Image, ImageDraw


def picture(w, h, seed):
    im = Image.new('RGB', (w, h), (250, 248, 240))
    d = ImageDraw.Draw(im)
    for i in range(0, h, 6):                      # "text lines"
        d.rectangle([4 + (i * seed) % 7, i + 1, w - 6, i + 3], fill=(60, 40, 30))
    d.rectangle([w // 3, h // 4, w // 3 + 9, h // 4 + 9], fill=(30, 110, 180))
    return im


def main():
    out = ['// Made by make_jpeg_vectors.py - synthetic pictures only.', '#pragma once', '#include <cstdint>', '']
    for n, (w, h, seed) in enumerate([(64, 48, 3), (40, 32, 5)]):
        im = picture(w, h, seed)
        b = io.BytesIO()
        im.save(b, 'JPEG', quality=90, subsampling=2)   # 4:2:0 like the scans
        data = b.getvalue()
        rgb = Image.open(io.BytesIO(data)).convert('RGB').tobytes()
        out.append(f'static const int kJpeg{n}W = {w}, kJpeg{n}H = {h};')
        out.append(f'static const uint8_t kJpeg{n}[] = {{' + ','.join(str(x) for x in data) + '};')
        out.append(f'static const uint8_t kJpeg{n}Rgb[] = {{' + ','.join(str(x) for x in rgb) + '};')
        out.append('')
    print('\n'.join(out))


main()
