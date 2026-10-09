"""Create small EPUBs for vertical-reading device checks, using only the standard library."""
import argparse
import struct
import zlib
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED


def image():
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\x00' + bytes(0 if (x // 16 + y // 16) % 2 else 255 for x in range(128)) for y in range(160))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 128, 160, 8, 0, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def create(output, language, progression, body_class):
    paragraph = ('「日本語の縦書き。」小さい文字ぁっゃヶ、比較つっケヶと長音ー、句読点。'
                 '<ruby>比較<rt>つっケヶ</rt></ruby>'
                 '<ruby>東京都<rt>とうきょうと</rt></ruby>12年、123年、ABCとlongsidewaysword。'
                 '<a href="#note">内部リンク</a>')
    text = '<p>' + paragraph * 200 + '</p>'
    chapter = ('<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml">'
               '<head><title>Vertical reader fixture</title><link rel="stylesheet" href="style.css"/></head>'
               f'<body class="{body_class}"><h1>縦書き検証</h1>{text}'
               '<p><ruby>一二三四五六七八九十<rt>いちにさんしごろくななはちきゅうじゅう</rt></ruby></p>'
               '<table><tr><td>第一列</td><td>第二列</td></tr><tr><td>三</td><td>四</td></tr></table>'
               '<p><img src="check.png" alt="checkerboard"/></p><p id="note">リンク先の注釈。</p></body></html>')
    package = (f'<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">'
               '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
               f'<dc:identifier id="id">vertical-{output.stem}</dc:identifier><dc:title>{output.stem}</dc:title>'
               f'<dc:creator>CrossPoint test fixture</dc:creator><dc:language>{language}</dc:language></metadata>'
               '<manifest><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/>'
               '<item id="style" href="style.css" media-type="text/css"/>'
               '<item id="image" href="check.png" media-type="image/png"/>'
               '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest>'
               f'<spine page-progression-direction="{progression}"><itemref idref="chapter"/></spine></package>')
    with ZipFile(output, 'w', ZIP_DEFLATED) as epub:
        epub.writestr('mimetype', 'application/epub+zip', compress_type=ZIP_STORED)
        epub.writestr('META-INF/container.xml', '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        epub.writestr('OEBPS/content.opf', package)
        epub.writestr('OEBPS/chapter.xhtml', chapter)
        epub.writestr('OEBPS/style.css', 'body.vertical { -epub-writing-mode: vertical-rl; } body.horizontal { writing-mode: horizontal-tb; }')
        epub.writestr('OEBPS/check.png', image())
        epub.writestr('OEBPS/nav.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Contents</title></head><body><nav epub:type="toc"><ol><li><a href="chapter.xhtml">縦書き検証</a></li></ol></nav></body></html>')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name, language, progression, body_class in (
            ('auto-ja-rtl', 'ja-JP', 'rtl', ''), ('auto-zh-rtl', 'zh-Hant', 'rtl', ''),
            ('css-vertical-en', 'en', 'ltr', 'vertical'), ('css-horizontal-ja', 'ja', 'rtl', 'horizontal'),
            ('horizontal-en', 'en', 'ltr', '')):
        path = args.output / f'{name}.epub'
        create(path, language, progression, body_class)
        print(path)
