import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from scripts import gen_assets


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _read_chunks(png_data: bytes):
    idx = len(PNG_SIGNATURE)
    chunks = []
    while idx < len(png_data):
        length = struct.unpack(">I", png_data[idx:idx + 4])[0]
        chunk_type = png_data[idx + 4:idx + 8]
        chunk_data = png_data[idx + 8:idx + 8 + length]
        crc = png_data[idx + 8 + length:idx + 12 + length]
        chunks.append((chunk_type, chunk_data, crc))
        idx += 12 + length
        if chunk_type == b"IEND":
            break
    return chunks


class TestMakePng(unittest.TestCase):
    def test_make_png_has_valid_signature_and_chunk_layout(self):
        data = gen_assets._make_png(4, 3, 0x11, 0x22, 0x33)
        self.assertTrue(data.startswith(PNG_SIGNATURE))

        chunks = _read_chunks(data)
        self.assertGreaterEqual(len(chunks), 3)
        self.assertEqual(chunks[0][0], b"IHDR")
        self.assertEqual(chunks[1][0], b"IDAT")
        self.assertEqual(chunks[-1][0], b"IEND")

        ihdr = chunks[0][1]
        width, height, bit_depth, color_type, comp, filt, interlace = struct.unpack(">IIBBBBB", ihdr)
        self.assertEqual(width, 4)
        self.assertEqual(height, 3)
        self.assertEqual(bit_depth, 8)
        self.assertEqual(color_type, 2)
        self.assertEqual(comp, 0)
        self.assertEqual(filt, 0)
        self.assertEqual(interlace, 0)

    def test_make_png_encodes_solid_rgb_rows(self):
        width, height = 3, 2
        color = (0xAA, 0xBB, 0xCC)
        data = gen_assets._make_png(width, height, *color)
        chunks = _read_chunks(data)
        idat = chunks[1][1]
        raw = zlib.decompress(idat)

        expected_row = b"\x00" + bytes(color) * width
        self.assertEqual(raw, expected_row * height)


class TestGenerateAssets(unittest.TestCase):
    def test_generate_assets_writes_all_expected_files_with_expected_dimensions(self):
        with tempfile.TemporaryDirectory() as tmp:
            project_dir = Path(tmp)
            generated = gen_assets.generate_assets(str(project_dir))

            expected_paths = [asset[0] for asset in gen_assets.ASSETS]
            self.assertEqual(generated, expected_paths)

            expected_dims = {path: (w, h) for path, w, h, *_ in gen_assets.ASSETS}
            for rel_path in expected_paths:
                full_path = project_dir / rel_path
                self.assertTrue(full_path.exists(), f"missing generated asset {rel_path}")
                data = full_path.read_bytes()
                self.assertTrue(data.startswith(PNG_SIGNATURE))
                chunks = _read_chunks(data)
                ihdr = chunks[0][1]
                width, height, *_ = struct.unpack(">IIBBBBB", ihdr)
                self.assertEqual((width, height), expected_dims[rel_path])


if __name__ == "__main__":
    unittest.main()
