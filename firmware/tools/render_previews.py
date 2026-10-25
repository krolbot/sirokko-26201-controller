#!/usr/bin/env python3
"""Convert the actual render_ui P6 buffers to PNG using only the standard library."""
import argparse
import struct
import zlib
from pathlib import Path


def png(path: Path, width: int, height: int, pixels: bytes) -> None:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    images = []
    for source in sorted(args.directory.glob('*.ppm')):
        magic, dimensions, maximum, pixels = source.read_bytes().split(b'\n', 3)
        width, height = map(int, dimensions.split())
        if (magic, maximum, width, height) != (b'P6', b'255', 240, 320) or len(pixels) != width * height * 3:
            raise ValueError(f'invalid renderer output: {source}')
        png(source.with_suffix('.png'), width, height, pixels)
        images.append((source.stem, pixels))
    if not images:
        raise ValueError('no P6 render buffers found')
    columns, cell_w, cell_h = 4, 260, 340
    rows = (len(images) + columns - 1) // columns
    width, height = columns * cell_w, rows * cell_h
    sheet = bytearray([32] * (width * height * 3))
    for index, (_, pixels) in enumerate(images):
        for y in range(320):
            start = ((index // columns * cell_h + y + 10) * width + index % columns * cell_w + 10) * 3
            sheet[start:start + 240*3] = pixels[y*240*3:(y+1)*240*3]
    png(args.directory / 'contact-sheet.png', width, height, bytes(sheet))
    print('HOST SIMULATION: ' + ', '.join(name for name, _ in images))
    print(args.directory / 'contact-sheet.png')


if __name__ == '__main__':
    main()
