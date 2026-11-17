#!/usr/bin/env python3
"""Bake the PSX3D palm-and-grid scene at native top-screen resolution."""
from pathlib import Path
import argparse
import math
from PIL import Image, ImageDraw

SKY = ((40, 8, 80), (110, 20, 120), (230, 40, 120), (255, 120, 60))
SUN = ((255, 236, 110), (255, 196, 60), (255, 120, 60), (255, 80, 100))
FLOOR = (16, 2, 44)
GRID = ((150, 60, 255), (40, 90, 255), (255, 70, 200))
SCALE = 2


def mix(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def box(draw, xy, fill):
    draw.rectangle(tuple(round(v * SCALE) for v in xy), fill=fill)


def sky(image):
    draw = ImageDraw.Draw(image)
    stops = (0, 56, 109.2, 140)
    for y in range(240 * SCALE):
        at = y / SCALE
        if at >= 140:
            color = mix(FLOOR, (4, 1, 12), (at - 140) / 100)
        else:
            i = next(i for i in range(3) if at < stops[i + 1])
            color = mix(SKY[i], SKY[i + 1], (at - stops[i]) / (stops[i + 1] - stops[i]))
        draw.line((0, y, 400 * SCALE, y), fill=color)
    for half_y in range(110, 280):
        y = half_y / 2
        cy, k, cut = 75 * .62, 0, False
        while cy < 150:
            gap = 75 * (.02 + .016 * k)
            cut |= cy <= y - 55 <= cy + gap
            cy += gap + 75 * .08
            k += 1
        if cut:
            continue
        half = math.sqrt(max(0, 75 * 75 - (y + .25 - 130) ** 2))
        t = (y - 55) / 150
        color = mix(SUN[0], SUN[1], t * 2) if t < .5 else mix(SUN[1], SUN[2], (t - .5) / .35)
        box(draw, (200 - half, y, 200 + half, y + .5), color)


def palms(image, art):
    mask = Image.new('L', image.size)
    for name, x, height, flip in (('a', .13, 310, False), ('b', .86, 320, True)):
        source = Image.open(art / f'palms_pair_{name}.png').convert('RGBA')
        source = source.crop(source.getchannel('A').getbbox())
        source = source.crop((0, 0, source.width, round(source.height * .94)))
        source = source.resize((round(source.width * height / source.height), height), Image.Resampling.LANCZOS)
        if flip:
            source = source.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        alpha = source.getchannel('A').point(lambda v: 255 if v >= 110 else 0)
        mask.paste(alpha, (round(x * 800 - source.width / 2), 310 - height))
    rim = Image.new('L', mask.size)
    rim.paste(mask, (1, 0))
    image.paste((255, 120, 90), (0, 0), rim)
    image.paste((18, 22, 52), (0, 0), mask)


def grid(image):
    draw = ImageDraw.Draw(image)
    points, heights, colors = [], [], []
    for j in range(29):
        z = .45 + j * 1.4
        near = max(0, min(1, (z - 1) / 6))
        power = max(0, min(1, (z - 1) / 25)) ** .7
        row, rise, ink = [], [], []
        for i in range(37):
            x = i - 18
            h = max(0, abs(x) - 2) ** 1.15 * .32
            h *= .8 + .22 * math.sin(x * .9 + z * .35) + .12 * math.sin(x * 1.7 - z * .5)
            h *= .35 + .65 * near
            row.append(((200 + x / z * 225) * SCALE, (140 + (1 - h) / z * 225 * .45) * SCALE))
            rise.append(h)
            side = min(1, abs(x) / 9)
            color = mix(GRID[0], GRID[1], side ** .8)
            color = mix(color, GRID[2], power * (1 - side) * .85)
            ink.append(color)
        points.append(row)
        heights.append(rise)
        colors.append(ink)
    for j in range(27, -1, -1):
        for i in range(36):
            if sum((heights[j][i], heights[j][i + 1], heights[j + 1][i], heights[j + 1][i + 1])) >= .01:
                draw.polygon((points[j][i], points[j][i + 1], points[j + 1][i + 1], points[j + 1][i]), fill=FLOOR)
        for i in range(36):
            draw.line((points[j + 1][i], points[j + 1][i + 1]), fill=colors[j + 1][i], width=2)
        for i in range(37):
            draw.line((points[j][i], points[j + 1][i]), fill=colors[j][i], width=2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--art', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[2] / 'assets/gfx/psx3d_backdrop.png')
    args = parser.parse_args()
    image = Image.new('RGB', (400 * SCALE, 240 * SCALE))
    sky(image)
    palms(image, args.art)
    grid(image)
    image = image.resize((400, 240), Image.Resampling.LANCZOS)
    # Leave a little contrast margin beneath the title and played line.
    pixels = image.load()
    for y in range(168, 240):
        for x in range(400):
            pixels[x, y] = tuple(round(c * .9) for c in pixels[x, y])
    image.save(args.output)


if __name__ == '__main__':
    main()
