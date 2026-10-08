#!/usr/bin/env python3
"""Generate a self-contained EPUB for inline gaiji layout checks.

Usage: python3 scripts/generate_gaiji_test_epub.py [output.epub]
The default destination is test/epubs/test_gaiji.epub. Test horizontal and
vertical modes, font sizes, all four orientations, and cached reopening.
"""

import argparse
from pathlib import Path
import struct
import zipfile
import zlib


def png(width, height):
    """An original black glyph on a transparent RGBA canvas."""
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            horizontal = width // 5 <= x < width * 4 // 5 and (
                height // 5 <= y < height // 4
                or height * 3 // 4 <= y < height * 4 // 5)
            vertical = width * 9 // 20 <= x < width * 11 // 20 and height // 5 <= y < height * 4 // 5
            rows.extend((0, 0, 0, 255) if horizontal or vertical else (255, 255, 255, 0))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def generate(output):
    css = """img.gaiji { width:1em; height:1em; vertical-align:baseline; }
img.gaiji-line { width:1em; height:auto; }
img.gaiji-wide { width:auto; height:1em; }
img.custom { height:2em; }
p { text-indent:0; }
"""
    body = "<h1>Gaiji layout</h1>"
    for kind in ("gaiji", "gaiji-line", "gaiji-wide", "custom"):
        source = kind if kind != "custom" else "gaiji"
        body += f'<p>Before<img class="{kind}" src="{source}.png" alt="I"/>after.</p>'
        body += f'<p>日本語の<img class="{kind}" src="{source}.png" alt="字"/>文字。</p>'
    body += '<p>Before<img class="gaiji" src="gaiji.png"/><img class="gaiji" src="gaiji.png"/>after.</p>'
    body += '<p>Before<img class="gaiji" src="missing.svg" alt="I"/>after.</p>'
    body += '<p>Before<img class="gaiji" style="display:none" src="gaiji.png"/>after.</p>'
    body += '<p>Ordinary block illustration:</p><p><img src="illustration.png" style="width:60%"/></p>'
    body += '<p>Wrapping: ' + '日本語の<img class="gaiji" src="gaiji.png"/>文字。' * 100 + '</p>'
    chapters = {}
    for mode, writing in (("horizontal", "horizontal-tb"), ("vertical", "vertical-rl")):
        chapters[mode] = (f'<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml">'
                          f'<head><title>{mode}</title><link rel="stylesheet" href="style.css"/></head>'
                          f'<body style="writing-mode:{writing}">{body}</body></html>')
    manifest = ''.join(f'<item id="{mode}" href="{mode}.xhtml" media-type="application/xhtml+xml"/>' for mode in chapters)
    images = (("gaiji", 128, 128), ("gaiji-line", 128, 256), ("gaiji-wide", 256, 128), ("illustration", 480, 240))
    manifest += ''.join(f'<item id="{name}" href="{name}.png" media-type="image/png"/>' for name, _, _ in images)
    opf = (f'<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">'
           '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
           '<dc:identifier id="id">crosspoint-gaiji-regression</dc:identifier>'
           '<dc:title>Inline Gaiji Regression</dc:title><dc:language>ja</dc:language></metadata>'
           f'<manifest>{manifest}<item id="style" href="style.css" media-type="text/css"/>'
           '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest>'
           '<spine><itemref idref="horizontal"/><itemref idref="vertical"/></spine></package>')
    nav = ('<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">'
           '<head><title>Contents</title></head><body><nav epub:type="toc"><ol>'
           '<li><a href="horizontal.xhtml">Horizontal</a></li><li><a href="vertical.xhtml">Vertical</a></li>'
           '</ol></nav></body></html>')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w') as book:
        book.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        book.writestr('META-INF/container.xml', '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        for name, data in {'content.opf': opf, 'nav.xhtml': nav, 'style.css': css, **{f'{mode}.xhtml': source for mode, source in chapters.items()}}.items():
            book.writestr('OEBPS/' + name, data, compress_type=zipfile.ZIP_DEFLATED)
        for name, width, height in images:
            book.writestr(f'OEBPS/{name}.png', png(width, height), compress_type=zipfile.ZIP_DEFLATED)
    print(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', nargs='?', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'test/epubs/test_gaiji.epub')
    generate(parser.parse_args().output)
