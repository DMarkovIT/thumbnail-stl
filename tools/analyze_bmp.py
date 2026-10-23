"""Измеряет BMP, который сохраняют тесты: покрытие, габариты силуэта, цвета."""

import struct
import sys


def load_bmp(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:2] != b"BM":
        raise ValueError("не BMP: " + path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bit_count = struct.unpack_from("<H", data, 28)[0]
    if bit_count != 32:
        raise ValueError("ожидался 32-битный BMP, получен %d" % bit_count)

    top_down = height < 0
    height = abs(height)
    stride = width * 4
    rows = [data[offset + row * stride:offset + (row + 1) * stride] for row in range(height)]
    if not top_down:
        rows.reverse()
    return width, height, rows


def pixel(rows, x, y):
    offset = x * 4
    b, g, r, a = rows[y][offset:offset + 4]
    return r, g, b, a


def analyze(path):
    width, height, rows = load_bmp(path)
    background = [pixel(rows, 1, y)[:3] for y in range(height)]

    min_x, min_y, max_x, max_y = width, height, -1, -1
    total = 0
    colour_sum = [0, 0, 0]
    for y in range(height):
        bg = background[y]
        for x in range(width):
            r, g, b, _ = pixel(rows, x, y)
            if abs(r - bg[0]) + abs(g - bg[1]) + abs(b - bg[2]) > 12:
                total += 1
                colour_sum[0] += r
                colour_sum[1] += g
                colour_sum[2] += b
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)

    print(f"{path}: {width}x{height}")
    print(f"  фон: {background[0]} -> {background[-1]}")
    print(f"  модель: {100.0 * total / (width * height):.1f}% пикселей")
    if not total:
        print("  модель не нарисована")
        return
    print(f"  габариты: x {min_x}..{max_x}, y {min_y}..{max_y} "
          f"({100.0 * (max_x - min_x + 1) / width:.0f}% x {100.0 * (max_y - min_y + 1) / height:.0f}%)")
    print(f"  средний цвет: ({colour_sum[0] // total}, {colour_sum[1] // total}, {colour_sum[2] // total})")


if __name__ == "__main__":
    for argument in sys.argv[1:]:
        analyze(argument)
