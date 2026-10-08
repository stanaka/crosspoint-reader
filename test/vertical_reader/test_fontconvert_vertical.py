import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[2] / 'lib/EpdFont/scripts'
sys.path.insert(0, str(SCRIPTS))
import fontconvert_sdcard as converter


class VerticalFontConverterTest(unittest.TestCase):
    def test_signed_pitch_and_partial_byte_ink(self):
        top = SimpleNamespace(width=3, rows=2, pitch=4, buffer=[0, 64, 128, 255, 192, 255, 0, 255])
        bottom = SimpleNamespace(width=3, rows=2, pitch=-4, buffer=[192, 255, 0, 255, 0, 64, 128, 255])
        expected = bytes([0b00011011, 0b11000000])
        self.assertEqual(converter.pack_freetype_bitmap(top), expected)
        self.assertEqual(converter.pack_freetype_bitmap(bottom), expected)

    def test_vrt2_overrides_vert_and_extension_substitution_is_supported(self):
        feature = lambda tag, index: SimpleNamespace(FeatureTag=tag, Feature=SimpleNamespace(LookupListIndex=[index]))
        lookups = [SimpleNamespace(SubTable=[SimpleNamespace(mapping={'comma': 'comma.vert', 'A': 'A.vert'})]),
                   SimpleNamespace(SubTable=[SimpleNamespace(ExtensionLookupType=1,
                                              ExtSubTable=SimpleNamespace(mapping={'comma': 'comma.vrt2'}))])]
        table = SimpleNamespace(FeatureList=SimpleNamespace(FeatureRecord=[feature('vert', 0), feature('vrt2', 1)]),
                                LookupList=SimpleNamespace(Lookup=lookups))
        class Font(dict):
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def getBestCmap(self): return {0x3001: 'comma', 65: 'A', 0x3002: 'period'}
            def getGlyphID(self, name): return {'comma.vert': 7, 'comma.vrt2': 8, 'A.vert': 9}[name]
        font = Font(GSUB=SimpleNamespace(table=table))
        with patch.dict(sys.modules, {'fontTools.ttLib': SimpleNamespace(TTFont=lambda path: font)}):
            self.assertEqual(converter.extract_vertical_glyph_indices('fake.otf'), {0x3001: 8})

    def test_v5_offsets_count_and_relative_bitmap_offsets(self):
        glyph = converter.GlyphProps(4, 4, 128, 0, 4, 4, 0, 0x3001)
        sd = converter.StyleRasterData(0, [(0x3001, 0x3001)], [(glyph, b'\x55'*4)], 4,
                                       8, 8, 0, [], [], [], 0, 0, [], [(glyph, b'\xaa'*4)])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'vertical.cpfont'
            with patch.object(converter, 'rasterize_font_style', return_value=sd):
                converter.generate_cpfont_multistyle({0: 'fake.otf'}, 12, [], str(output))
            data = output.read_bytes()
            self.assertEqual(struct.unpack_from('<HHB', data, 8), (5, 3, 1))
            offset = struct.unpack_from('<I', data, 60)[0]
            self.assertEqual(offset, 64 + 12 + 16 + 4)
            self.assertEqual(struct.unpack_from('<HI', data, offset), (1, 0x3001))
            self.assertEqual(struct.unpack_from(converter.GLYPH_STRUCT_FORMAT, data, offset+6)[-1], 0)
            self.assertEqual(data[offset+22:], b'\xaa'*4)

    def test_font_without_alternates_writes_zero_toc_offset(self):
        sd = converter.StyleRasterData(0, [], [], 0, 8, 8, 0, [], [], [], 0, 0, [])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'horizontal.cpfont'
            with patch.object(converter, 'rasterize_font_style', return_value=sd):
                converter.generate_cpfont_multistyle({0: 'fake.ttf'}, 12, [], str(output))
            data = output.read_bytes()
            self.assertEqual(struct.unpack_from('<H', data, 10)[0], 1)
            self.assertEqual(struct.unpack_from('<I', data, 60)[0], 0)


if __name__ == '__main__':
    unittest.main()
