"""Генерирует тестовые STL: куб, пирамиду (ASCII), шестерню и тор."""

import math
import os
import struct
import sys


def write_binary_stl(path, triangles):
    with open(path, "wb") as handle:
        handle.write(b"STL test model".ljust(80, b"\0"))
        handle.write(struct.pack("<I", len(triangles)))
        for a, b, c in triangles:
            handle.write(struct.pack("<3f", 0.0, 0.0, 0.0))
            for vertex in (a, b, c):
                handle.write(struct.pack("<3f", *vertex))
            handle.write(struct.pack("<H", 0))


def write_ascii_stl(path, name, triangles):
    with open(path, "w", encoding="ascii") as handle:
        handle.write(f"solid {name}\n")
        for a, b, c in triangles:
            handle.write("  facet normal 0 0 0\n    outer loop\n")
            for vertex in (a, b, c):
                handle.write("      vertex %.6f %.6f %.6f\n" % vertex)
            handle.write("    endloop\n  endfacet\n")
        handle.write(f"endsolid {name}\n")


def quad(a, b, c, d):
    return [(a, b, c), (a, c, d)]


def cube(size=40.0):
    h = size / 2.0
    p = [
        (-h, -h, -h), (h, -h, -h), (h, h, -h), (-h, h, -h),
        (-h, -h, h), (h, -h, h), (h, h, h), (-h, h, h),
    ]
    triangles = []
    triangles += quad(p[0], p[3], p[2], p[1])
    triangles += quad(p[4], p[5], p[6], p[7])
    triangles += quad(p[0], p[1], p[5], p[4])
    triangles += quad(p[1], p[2], p[6], p[5])
    triangles += quad(p[2], p[3], p[7], p[6])
    triangles += quad(p[3], p[0], p[4], p[7])
    return triangles


def pyramid(size=45.0, height=55.0):
    h = size / 2.0
    base = [(-h, -h, 0.0), (h, -h, 0.0), (h, h, 0.0), (-h, h, 0.0)]
    apex = (0.0, 0.0, height)
    triangles = quad(base[3], base[2], base[1], base[0])
    for index in range(4):
        triangles.append((base[index], base[(index + 1) % 4], apex))
    return triangles


def torus(major=45.0, minor=16.0, major_steps=240, minor_steps=120):
    def point(u, v):
        cu, su = math.cos(u), math.sin(u)
        cv, sv = math.cos(v), math.sin(v)
        return ((major + minor * cv) * cu, (major + minor * cv) * su, minor * sv)

    triangles = []
    for i in range(major_steps):
        u0 = 2.0 * math.pi * i / major_steps
        u1 = 2.0 * math.pi * (i + 1) / major_steps
        for j in range(minor_steps):
            v0 = 2.0 * math.pi * j / minor_steps
            v1 = 2.0 * math.pi * (j + 1) / minor_steps
            a = point(u0, v0)
            b = point(u1, v0)
            c = point(u1, v1)
            d = point(u0, v1)
            triangles.append((a, b, c))
            triangles.append((a, c, d))
    return triangles


def gear(radius=40.0, teeth=14, tooth_height=9.0, thickness=14.0):
    outline = []
    for index in range(teeth):
        base_angle = 2.0 * math.pi * index / teeth
        step = 2.0 * math.pi / teeth
        for fraction, r in (
            (0.00, radius),
            (0.18, radius),
            (0.26, radius + tooth_height),
            (0.44, radius + tooth_height),
            (0.52, radius),
        ):
            angle = base_angle + step * fraction
            outline.append((r * math.cos(angle), r * math.sin(angle), 0.0))

    top = [(x, y, thickness) for x, y, _ in outline]
    bottom = [(x, y, 0.0) for x, y, _ in outline]

    triangles = []
    center_top = (0.0, 0.0, thickness)
    center_bottom = (0.0, 0.0, 0.0)
    count = len(outline)
    for index in range(count):
        nxt = (index + 1) % count
        triangles.append((center_top, top[index], top[nxt]))
        triangles.append((center_bottom, bottom[nxt], bottom[index]))
        triangles += quad(bottom[index], bottom[nxt], top[nxt], top[index])
    return triangles


def main():
    output = sys.argv[1] if len(sys.argv) > 1 else "testdata"
    os.makedirs(output, exist_ok=True)

    write_binary_stl(os.path.join(output, "cube.stl"), cube())
    write_ascii_stl(os.path.join(output, "pyramid.stl"), "pyramid", pyramid())
    write_binary_stl(os.path.join(output, "gear.stl"), gear())
    write_binary_stl(os.path.join(output, "torus.stl"), torus())

    for name in ("cube.stl", "pyramid.stl", "gear.stl", "torus.stl"):
        path = os.path.join(output, name)
        print("%-12s %8.1f KB" % (name, os.path.getsize(path) / 1024.0))


if __name__ == "__main__":
    main()
