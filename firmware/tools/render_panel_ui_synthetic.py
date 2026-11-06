#!/usr/bin/env python3
"""Render a synthetic, secret-free inspection PNG for the Sirokko local panel.

This is a layout preview built from the same self-authored 5x7 style as panel_font.*.
It does not drive an ST7789 and does not represent a hardware test.
"""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

PANEL_W = 240
PANEL_H = 320
W = PANEL_W * 3
H = PANEL_H

# Bit 0 is the first (top) row. Compact, self-authored glyphs for the synthetic preview.
ASCII = {
    " ": (0, 0, 0, 0, 0), "-": (8, 8, 8, 8, 8), "/": (32, 16, 8, 4, 2),
    ":": (0, 54, 54, 0, 0), ".": (0, 96, 96, 0, 0), "%": (35, 19, 8, 100, 98),
    "0": (62, 81, 73, 69, 62), "1": (0, 66, 127, 64, 0), "2": (66, 97, 81, 73, 70),
    "3": (33, 65, 69, 75, 49), "4": (24, 20, 18, 127, 16), "5": (39, 69, 69, 69, 57),
    "6": (60, 74, 73, 73, 48), "7": (1, 113, 9, 5, 3), "8": (54, 73, 73, 73, 54),
    "9": (6, 73, 73, 41, 30),
    "A": (126, 17, 17, 17, 126), "B": (127, 73, 73, 73, 54), "C": (62, 65, 65, 65, 34),
    "D": (127, 65, 65, 34, 28), "E": (127, 73, 73, 73, 65), "F": (127, 9, 9, 9, 1),
    "G": (62, 65, 73, 73, 122), "H": (127, 8, 8, 8, 127), "I": (0, 65, 127, 65, 0),
    "J": (32, 64, 65, 63, 1), "K": (127, 8, 20, 34, 65), "L": (127, 64, 64, 64, 64),
    "M": (127, 2, 12, 2, 127), "N": (127, 4, 8, 16, 127), "O": (62, 65, 65, 65, 62),
    "P": (127, 9, 9, 9, 6), "Q": (62, 65, 81, 33, 94), "R": (127, 9, 25, 41, 70),
    "S": (70, 73, 73, 73, 49), "T": (1, 1, 127, 1, 1), "U": (63, 64, 64, 64, 63),
    "V": (31, 32, 64, 32, 31), "W": (127, 32, 24, 32, 127), "X": (99, 20, 8, 20, 99),
    "Y": (3, 4, 120, 4, 3), "Z": (97, 81, 73, 69, 67),
}
CYR = {
    "А": ASCII["A"], "Б": (127, 73, 73, 73, 49), "В": ASCII["B"], "Г": (127, 1, 1, 1, 1),
    "Д": (96, 62, 33, 33, 127), "Е": ASCII["E"], "Ё": (125, 84, 84, 84, 68),
    "Ж": (54, 8, 127, 8, 54), "З": (34, 65, 73, 73, 54), "И": (127, 16, 8, 4, 127),
    "Й": (125, 16, 8, 4, 125), "К": ASCII["K"], "Л": (96, 31, 1, 1, 127),
    "М": ASCII["M"], "Н": ASCII["H"], "О": ASCII["O"], "П": (127, 1, 1, 1, 127),
    "Р": ASCII["P"], "С": ASCII["C"], "Т": ASCII["T"], "У": (7, 72, 72, 72, 63),
    "Ф": (28, 34, 127, 34, 28), "Х": ASCII["X"], "Ц": (63, 64, 64, 64, 127),
    "Ч": (15, 16, 16, 16, 127), "Ш": (127, 64, 127, 64, 127), "Щ": (63, 64, 127, 64, 127),
    "Ъ": (1, 127, 72, 72, 48), "Ы": (127, 72, 48, 0, 127), "Ь": (127, 72, 72, 72, 48),
    "Э": (34, 65, 73, 73, 62), "Ю": (127, 8, 62, 65, 62), "Я": (70, 41, 25, 9, 127),
}

BG = (8, 18, 29)
HEADER = (10, 44, 82)
TEXT = (235, 245, 255)
DIM = (171, 195, 215)
CYAN = (0, 225, 255)
YELLOW = (255, 197, 48)
SELECT = (48, 118, 103)
BORDER = (38, 88, 122)


def make_canvas() -> bytearray:
    return bytearray(BG * (W * H))


def rect(canvas: bytearray, x: int, y: int, width: int, height: int, color: tuple[int, int, int]) -> None:
    for py in range(max(0, y), min(H, y + height)):
        start = (py * W + max(0, x)) * 3
        end = (py * W + min(W, x + width)) * 3
        canvas[start:end] = bytes(color) * max(0, min(W, x + width) - max(0, x))


def text(canvas: bytearray, x: int, y: int, value: str, color: tuple[int, int, int], scale: int = 1) -> None:
    cursor = x
    for char in value.upper():
        glyph = CYR.get(char, ASCII.get(char, ASCII[" "]))
        for column, bits in enumerate(glyph):
            for row in range(7):
                if bits & (1 << row):
                    rect(canvas, cursor + column * scale, y + row * scale, scale, scale, color)
        cursor += 6 * scale


def panel_frame(canvas: bytearray, x: int, title: str) -> None:
    rect(canvas, x, 0, PANEL_W, PANEL_H, BG)
    rect(canvas, x, 0, PANEL_W, 4, BORDER)
    rect(canvas, x, 4, PANEL_W, 27, HEADER)
    text(canvas, x + 6, 10, "СИРОККО", CYAN, 2)
    text(canvas, x + 6, 35, title, DIM, 2)
    rect(canvas, x, PANEL_H - 3, PANEL_W, 3, BORDER)


def draw_home(canvas: bytearray, x: int) -> None:
    panel_frame(canvas, x, "ОЖИДАНИЕ")
    text(canvas, x + 8, 70, "УСТАВКА", TEXT, 2)
    text(canvas, x + 8, 100, "22.0 C", CYAN, 3)
    text(canvas, x + 8, 152, "СТОП: 38 МИН", TEXT, 2)
    text(canvas, x + 8, 190, "ПОВОРОТ: ИЗМЕНИТЬ", DIM, 2)
    text(canvas, x + 8, 208, "КЛИК: МЕНЮ", DIM, 2)
    text(canvas, x + 8, 226, "KEY0: ПУСК / СТОП", YELLOW, 2)
    text(canvas, x + 8, 280, "СИНТЕТИЧЕСКИЙ МАКЕТ", DIM, 1)


def draw_menu(canvas: bytearray, x: int) -> None:
    panel_frame(canvas, x, "МЕНЮ")
    entries = [
        "РЕЖИМ", "РУЧНАЯ МОЩНОСТЬ", "ТЕМПЕРАТУРА", "ВЫКЛЮЧИТЬ ЧЕРЕЗ",
        "ДЛИТЕЛЬНОСТЬ ПУСКА", "ОТЛОЖЕННЫЙ ПУСК", "АВТОКАЛИБРОВКА",
        "ДИАГНОСТИКА", "WI-FI", "OTA: БРАУЗЕР", "НАЗАД",
    ]
    y = 55
    for index, entry in enumerate(entries):
        if index == 3:
            rect(canvas, x + 3, y - 2, 234, 16, SELECT)
            color = TEXT
        else:
            color = DIM
        text(canvas, x + 8, y, entry, color, 2)
        y += 17
    text(canvas, x + 8, 250, "КЛИК: ОТКРЫТЬ", DIM, 2)
    text(canvas, x + 8, 270, "ДВОЙНОЙ: НАЗАД", DIM, 2)


def draw_diag(canvas: bytearray, x: int) -> None:
    panel_frame(canvas, x, "РАБОТА")
    text(canvas, x + 8, 60, "ДИАГНОСТИКА", TEXT, 2)
    lines = [
        "ОШИБКИ: 00000000", "КОМАНДА: 61 %", "ДИАП: 810..3310 ОК",
        "ТОЧКИ: 4", "КАЛ: STABLE", "ПРИЧИНА: NONE", "КОМНАТА: НОРМА",
    ]
    y = 94
    for line in lines:
        text(canvas, x + 8, y, line, DIM, 2)
        y += 18
    text(canvas, x + 8, 250, "ДВОЙНОЙ: НАЗАД", DIM, 2)


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def write_png(path: Path, canvas: bytearray) -> None:
    raw = b"".join(b"\x00" + bytes(canvas[row * W * 3:(row + 1) * W * 3]) for row in range(H))
    payload = b"\x89PNG\r\n\x1a\n"
    payload += png_chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    payload += png_chunk(b"IDAT", zlib.compress(raw, 9))
    payload += png_chunk(b"IEND", b"")
    path.write_bytes(payload)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("panel_ui_synthetic.png"))
    args = parser.parse_args()
    canvas = make_canvas()
    draw_home(canvas, 0)
    draw_menu(canvas, PANEL_W)
    draw_diag(canvas, PANEL_W * 2)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_png(args.output, canvas)
    print(args.output)


if __name__ == "__main__":
    main()
