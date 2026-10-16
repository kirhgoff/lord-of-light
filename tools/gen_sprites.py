"""Generate placeholder pixel art and atlases from data/*.json.

Usage (run from the repo root):
    uv run --with pillow==12.3.0 python tools/gen_sprites.py

Writes assets/{parts,pickups,traps,effects,tiles}/ and assets/contact_sheet.png.
"""
import json
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
FRAME = 32
INTERNAL_FRAME = 16
OBJ = 16
WORLD = 24
ROCK = 32
PILLAR_W = 32
PILLAR_H = 64
BOB = 1
OUTLINE = (30, 25, 32, 255)
SKIN = (176, 138, 110)
WOOD = (122, 92, 62)
STEEL = (150, 156, 162)
BONE = (200, 190, 165)

ELEMENT_BASE = {
    "kinetic": (150, 132, 112),
    "fire": (190, 98, 52),
    "frost": (128, 170, 192),
    "lightning": (226, 198, 52),
    "radiance": (238, 226, 206),
    "venom": (112, 152, 82),
    "mind": (152, 112, 164),
    "void": (62, 56, 98),
}

BASE_OVERRIDE = {
    "crow_body": (50, 46, 58),
    "imp_body": (168, 68, 58),
    "carapace": (104, 86, 48),
    "gas_bladder": (212, 206, 190),
}

REAR_PARTS = {"serpent_tail", "venom_stinger"}

FRAME_LAYOUT = [
    ("idle", 0, 0.0, 0), ("idle", 1, 0.0, 1),
    ("act", 0, 0.25, 0), ("act", 1, 1.0, 0), ("act", 2, 0.6, 0),
    ("hurt", 0, 0.0, 0), ("broken", 0, 0.0, 0),
]


def mul(rgb, k):
    return tuple(max(0, min(255, int(v * k))) for v in rgb[:3])


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


class Palette:
    def __init__(self, element, base=None):
        self.element = element
        self.base = base or ELEMENT_BASE[element]
        self.dark = mul(self.base, 0.62)
        self.deep = mul(self.base, 0.4)
        self.light = mix(self.base, (240, 235, 220), 0.38)
        self.glow = mix(self.base, (255, 250, 230), 0.6)


def rot(points, origin, degrees):
    r = math.radians(degrees)
    c, s = math.cos(r), math.sin(r)
    return [(origin[0] + (x - origin[0]) * c - (y - origin[1]) * s,
             origin[1] + (x - origin[0]) * s + (y - origin[1]) * c) for x, y in points]


def local(points, origin, degrees):
    return rot([(origin[0] + x, origin[1] + y) for x, y in points], origin, degrees)


def ell(g, cx, cy, rx, ry, fill):
    g.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=fill)


def shaded_ell(g, p, cx, cy, rx, ry):
    ell(g, cx, cy, rx, ry, p.dark)
    ell(g, cx - 1, cy - 1, max(rx - 1, 1), max(ry - 1, 1), p.base)
    ell(g, cx - rx // 3, cy - ry // 3, max(rx // 3, 1), max(ry // 3, 1), p.light)


def shaded_poly(g, p, pts, fill=None):
    g.polygon(pts, fill=fill or p.base)


def arm(g, origin, deg, length=6, colour=SKIN):
    tip = local([(length, 0)], origin, deg)[0]
    g.line([origin, tip], fill=mul(colour, 0.7), width=4)
    g.line([origin, tip], fill=colour, width=2)
    ell(g, tip[0], tip[1], 2.5, 2.5, colour)
    return tip


def swing(a):
    return -60 + a * 60


def draw_item(g, p, items, origin, deg, scale=0.8):
    for pts, fill in items:
        g.polygon(local([(x * scale, y * scale) for x, y in pts], origin, deg), fill=fill)


def hand_with(g, p, a, b, items, shoulder=(5, 24)):
    deg = swing(a)
    tip = arm(g, shoulder, deg)
    draw_item(g, p, items(p), tip, deg)
    return shoulder


def sword_items(p):
    return [([(1, -1), (15, -1), (17, 0), (15, 1), (1, 1)], STEEL),
            ([(1, -1), (15, -1), (15, 0), (1, 0)], p.light),
            ([(0, -4), (2, -4), (2, 4), (0, 4)], p.dark)]


def spear_items(p):
    return [([(-3, -1), (14, -1), (14, 1), (-3, 1)], WOOD),
            ([(14, -3), (21, 0), (14, 3)], STEEL), ([(14, -1), (19, 0), (14, 0)], p.light)]


def lugh_items(p):
    return [([(-3, -1), (12, -1), (12, 1), (-3, 1)], WOOD),
            ([(11, -5), (24, 0), (11, 5)], p.base), ([(12, -3), (21, 0), (12, 3)], (255, 252, 226)),
            ([(8, -3), (11, -3), (11, 3), (8, 3)], p.glow)]


def gungnir_items(p):
    return [([(-4, -1), (15, -1), (15, 1), (-4, 1)], mul(WOOD, 0.8)),
            ([(14, -3), (22, 0), (14, 3)], p.light), ([(10, -2), (13, -2), (13, 2), (10, 2)], p.base)]


def club_items(p):
    return [([(-2, -1), (9, -1), (9, 1), (-2, 1)], WOOD),
            ([(8, -5), (16, -4), (17, 0), (16, 4), (8, 5)], mul(WOOD, 0.85)),
            ([(10, -7), (12, -5), (14, -7), (14, -4)], BONE), ([(10, 7), (12, 5), (14, 7), (14, 4)], BONE)]


def trident_items(p):
    return [([(-3, -1), (13, -1), (13, 1), (-3, 1)], STEEL),
            ([(12, -5), (14, -5), (14, 5), (12, 5)], STEEL),
            ([(14, -5), (20, -5), (20, -4), (14, -4)], p.light), ([(14, -1), (21, -1), (21, 0), (14, 0)], p.light),
            ([(14, 4), (20, 4), (20, 5), (14, 5)], p.light)]


def hammer_items(p):
    return [([(-2, -1), (10, -1), (10, 1), (-2, 1)], WOOD),
            ([(9, -6), (17, -6), (17, 6), (9, 6)], STEEL), ([(10, -5), (16, -5), (16, 0), (10, 0)], p.light),
            ([(12, -3), (14, -3), (14, 3), (12, 3)], p.glow)]


def sword_hand(g, p, a, b):
    return hand_with(g, p, a, b, sword_items)


def spear_hand(g, p, a, b):
    return hand_with(g, p, a, b, spear_items)


def lugh_spear(g, p, a, b):
    return hand_with(g, p, a, b, lugh_items)


def gungnir(g, p, a, b):
    return hand_with(g, p, a, b, gungnir_items)


def war_club(g, p, a, b):
    return hand_with(g, p, a, b, club_items)


def frost_trident(g, p, a, b):
    return hand_with(g, p, a, b, trident_items)


def lightning_hammer(g, p, a, b):
    hand_with(g, p, a, b, hammer_items)
    if a > 0.5:
        for i in range(3):
            g.line([(20 + i * 3, 3 + i * 5), (23 + i * 3, 6 + i * 5), (21 + i * 3, 8 + i * 5)], fill=p.glow)
    return (5, 24)


def open_hand(g, p, a, b, charge, reach=6, lift=0):
    deg = swing(a) * 0.5 - 10 - lift
    tip = arm(g, (5, 24), deg, reach)
    for i in range(4):
        fx, fy = local([(3 + (i % 2), -3 + i * 2)], tip, deg)[0]
        ell(g, fx, fy, 1.2, 1.2, SKIN)
    charge(g, p, tip, a)
    return (5, 24)


def flame(g, p, x, y, size):
    g.polygon([(x - size, y), (x - size // 2, y - size), (x, y - size * 2), (x + size // 2, y - size),
               (x + size, y), (x, y + size // 2)], fill=p.base)
    g.polygon([(x - size // 2, y), (x, y - size), (x + size // 2, y), (x, y + size // 3)], fill=p.glow)


def charge_flame(g, p, tip, a):
    flame(g, p, tip[0] + 4, tip[1] - 1, 4 + int(a * 2))


def charge_bolt(g, p, tip, a):
    x, y = tip[0] + 3, tip[1]
    zigzag = [(x + 1, y - 9), (x - 2, y - 4), (x + 3, y - 3), (x - 1, y + 3), (x + 2, y + 8)]
    g.line(zigzag, fill=(255, 236, 90), width=3)
    g.line(zigzag, fill=(255, 255, 230), width=1)


def charge_glow(g, p, tip, a):
    r = 4 + int(a * 2)
    x, y = tip[0] + 5, tip[1] - 2
    ell(g, x, y, r + 1, r + 1, (86, 140, 96))
    ell(g, x, y, r, r, (132, 204, 138))
    ell(g, x, y, r - 2, r - 2, (214, 246, 208))
    g.line([(x - 1, y), (x + 1, y)], fill=(70, 140, 80))
    g.line([(x, y - 1), (x, y + 1)], fill=(70, 140, 80))


def charge_hearth(g, p, tip, a):
    x, y = tip[0] + 5, tip[1] - 3
    g.chord([x - 5, y - 4, x + 5, y + 5], 0, 180, fill=(70, 56, 50))
    g.line([(x - 5, y), (x + 5, y)], fill=(130, 110, 96))
    for i in range(3):
        g.point((x - 3 + i * 3, y - 1 - (i + int(a * 3)) % 2), fill=(255, 168, 70))


def charge_serpent(g, p, tip, a):
    x, y = tip[0] + 2, tip[1]
    pts = [(x + i * 2, y - 2 + (3 if i % 2 else -3) * (1 + a)) for i in range(7)]
    g.line(pts, fill=p.dark, width=3)
    g.line(pts, fill=p.base, width=1)
    ell(g, pts[-1][0] + 1, pts[-1][1], 2, 2, p.base)
    g.point((pts[-1][0] + 2, pts[-1][1] - 1), fill=p.glow)


def fire_hand(g, p, a, b):
    return open_hand(g, p, a, b, charge_flame)


def thunderbolt_hand(g, p, a, b):
    return open_hand(g, p, a, b, charge_bolt)


def healing_hand(g, p, a, b):
    return open_hand(g, p, a, b, charge_glow, reach=11, lift=-30)


def hearth_hand(g, p, a, b):
    return open_hand(g, p, a, b, charge_hearth)


def serpent_hand(g, p, a, b):
    return open_hand(g, p, a, b, charge_serpent)


def shield_hand(g, p, a, b):
    lift = int(a * 3)
    arm(g, (4, 22), -10, 7)
    cx, cy = 17 - lift, 17
    ell(g, cx, cy, 9, 11, WOOD)
    ell(g, cx, cy, 7, 9, p.dark)
    ell(g, cx, cy, 5, 7, p.base)
    g.rectangle([cx - 1, cy - 8, cx, cy + 8], fill=STEEL)
    ell(g, cx, cy, 2, 2, p.light)
    return (4, 22)


def bow(g, p, a, b, wood):
    pull = a * 5
    bx = 16
    g.arc([bx - 8, 4, bx + 6, 28], 270, 90, fill=wood, width=2)
    g.line([(bx - 1, 4), (bx - 4 - pull, 16), (bx - 1, 28)], fill=BONE)
    if a > 0.1:
        g.line([(bx - 4 - pull, 16), (bx + 8, 16)], fill=p.light, width=1)
        g.polygon([(bx + 8, 14), (bx + 11, 16), (bx + 8, 18)], fill=STEEL)
    arm(g, (4, 20), -5, 8)
    return (4, 20)


def bow_hand(g, p, a, b):
    return bow(g, p, a, b, WOOD)


def sun_bow(g, p, a, b):
    bow(g, p, a, b, p.base)
    ell(g, 19, 16, 2, 2, p.glow)
    return (4, 20)


def claw(g, p, a, b):
    open_amount = int(a * 4)
    g.polygon([(2, 14), (10, 12), (12, 20), (4, 24)], fill=p.dark)
    g.polygon([(3, 14), (9, 13), (10, 19), (4, 22)], fill=p.base)
    for i in range(3):
        y = 11 + i * 6 + open_amount * (i - 1)
        g.polygon([(10, y), (19, y + 2), (26, y + 7 - i), (20, y + 7), (11, y + 5)], fill=BONE)
        g.line([(12, y + 2), (22, y + 5)], fill=p.light)
    return (4, 18)


def minion_claw(g, p, a, b):
    open_amount = int(a * 1.5)
    g.rectangle([4, 11, 7, 18], fill=p.dark)
    g.rectangle([4, 11, 6, 17], fill=p.base)
    for i in range(3):
        y = 11 + i * 3 + open_amount * (i - 1)
        g.polygon([(7, y), (10, y), (12, y + 1), (10, y + 1), (7, y + 1)], fill=BONE)
    return (4, 15)


def mandibles(g, p, a, b):
    gap = 2 + int(a * 6)
    ell(g, 6, 16, 5, 6, p.dark)
    ell(g, 6, 16, 4, 5, p.base)
    g.polygon([(8, 16 - gap), (18, 12 - gap), (26, 15 - gap // 2), (20, 16 - gap), (10, 18 - gap)], fill=BONE)
    g.polygon([(8, 16 + gap), (18, 20 + gap), (26, 17 + gap // 2), (20, 16 + gap), (10, 14 + gap)], fill=BONE)
    return (4, 16)


def fang_row(g, p, x0, y_top, count, length, colour):
    for i in range(count):
        x = x0 + i * 4
        g.polygon([(x, y_top), (x + 3, y_top), (x + 1.5, y_top + length)], fill=colour)


def frost_fang(g, p, a, b):
    gap = int(a * 4)
    g.polygon([(3, 8 - gap), (24, 11 - gap), (24, 13), (3, 15)], fill=p.dark)
    g.polygon([(3, 24 + gap), (24, 21 + gap), (24, 19), (3, 17)], fill=p.dark)
    fang_row(g, p, 8, 12 - gap, 4, 6, p.glow)
    fang_row(g, p, 8, 20 + gap, 4, -6, p.light)
    return (3, 16)


def ember_maw(g, p, a, b):
    gap = 2 + int(a * 4)
    top = 11 - gap // 2
    bottom = 21 + gap // 2
    shaded_ell(g, p, 14, top, 11, 6)
    shaded_ell(g, p, 14, bottom, 10, 5)
    ell(g, 14, 16, 8, max(gap, 2), (60, 20, 14))
    flame(g, p, 14, 16 + gap // 2, 3 + int(a * 2))
    fang_row(g, p, 8, top + 3, 3, 4, BONE)
    return (4, 16)


def void_maw(g, p, a, b):
    r = 8 + int(a * 2)
    ell(g, 16, 16, r + 3, r + 1, p.dark)
    ell(g, 16, 16, r, r - 2, (14, 10, 22))
    ell(g, 16, 16, r - 3, max(r - 5, 1), p.deep)
    for i in range(8):
        ang = i * math.pi / 4
        x, y = 16 + math.cos(ang) * (r + 1), 16 + math.sin(ang) * (r - 1)
        g.polygon([(x, y), (x - math.cos(ang) * 3 + 1, y - math.sin(ang) * 3), (x - math.cos(ang) * 3 - 1, y - math.sin(ang) * 3 + 1)], fill=p.light)
    return (16, 16)


def tentacle(g, p, a, b, tip_draw):
    pts = []
    for i in range(9):
        t = i / 8
        pts.append((5 + t * 17, 22 - t * 6 + math.sin(t * 7 + a * 4) * (2 + 2 * a)))
    for i in range(len(pts) - 1):
        w = max(6 - i * 0.6, 1.5)
        g.line([pts[i], pts[i + 1]], fill=p.dark, width=int(w + 2))
    for i in range(len(pts) - 1):
        w = max(6 - i * 0.6, 1.5)
        g.line([pts[i], pts[i + 1]], fill=p.base, width=int(w))
    for x, y in pts[1:7:2]:
        g.point((x, y + 1), fill=p.light)
    tip_draw(g, p, pts[-1], a)
    return (5, 22)


def tip_sparks(g, p, tip, a):
    x, y = tip
    for dx, dy in [(3, -4), (4, 1), (2, 5), (-1, -5)]:
        g.line([(x, y), (x + dx, y + dy), (x + dx + 1, y + dy - 2)], fill=p.glow)
    ell(g, x, y, 1.5, 1.5, p.glow)


def tip_void(g, p, tip, a):
    ell(g, tip[0], tip[1], 3, 3, (16, 12, 24))
    ell(g, tip[0], tip[1], 1.5, 1.5, p.light)


def tip_plain(g, p, tip, a):
    ell(g, tip[0], tip[1], 2, 2, p.light)


def electric_tentacle(g, p, a, b):
    return tentacle(g, p, a, b, tip_sparks)


def void_tendril(g, p, a, b):
    return tentacle(g, p, a, b, tip_void)


def stinger(g, p, a, b):
    thrust = int(a * 4)
    pts = [(3, 26), (10, 25), (17, 22), (22, 17), (22, 12 + thrust)]
    g.line(pts, fill=p.dark, width=6)
    g.line(pts, fill=p.base, width=4)
    g.line([(p0[0] - 1, p0[1] - 1) for p0 in pts], fill=p.light, width=1)
    g.polygon([(19, 12 + thrust), (25, 12 + thrust), (22, 6 + thrust)], fill=BONE)
    ell(g, 22, 7 + thrust, 1.5, 1.5, p.glow)
    return (3, 26)


def brain_lobe(g, p, a, b):
    shaded_ell(g, p, 15, 15, 11, 9)
    shaded_ell(g, p, 12, 12, 6, 5)
    for pts in [[(8, 14), (12, 17), (16, 13), (21, 18)], [(11, 9), (15, 11), (19, 8)], [(12, 22), (17, 21), (22, 23)]]:
        g.line(pts, fill=p.deep)
    pulse = int(a * 3)
    ell(g, 25, 8, 2 + pulse, 2 + pulse, p.glow)
    return (13, 22)


def lantern(g, p, a, b):
    r = 5 + int(a * 3)
    ell(g, 16, 16, r + 3, r + 3, mix(p.base, (60, 56, 50), 0.6))
    g.line([(16, 2), (16, 7)], fill=STEEL)
    g.rectangle([12, 7, 20, 9], fill=STEEL)
    g.rectangle([12, 23, 20, 25], fill=STEEL)
    g.rectangle([12, 9, 20, 23], fill=p.base)
    g.rectangle([14, 11, 18, 21], fill=p.glow)
    g.line([(16, 9), (16, 23)], fill=STEEL)
    return (16, 2)


def wisp_spark(g, p, a, b):
    x, y, s = 16, 16, 3
    g.polygon([(x, y - s), (x + 1, y - 1), (x + s, y), (x + 1, y + 1),
               (x, y + s), (x - 1, y + 1), (x - s, y), (x - 1, y - 1)], fill=p.base)
    ell(g, x, y, 1, 1, p.glow)
    if a > 0.5:
        for dx, dy in ((-2, -2), (2, -2), (-2, 2), (2, 2)):
            g.point((x + dx, y + dy), fill=p.light)
    return (x, y)


def crown(g, p, a, b):
    g.rectangle([8, 18, 24, 23], fill=p.dark)
    g.rectangle([8, 18, 24, 20], fill=p.base)
    for i, h in enumerate([8, 12, 8, 12, 8]):
        x = 8 + i * 4
        g.polygon([(x, 18), (x + 2, 18 - h + int(a * 2)), (x + 4, 18)], fill=p.light)
    ell(g, 16, 21, 2, 2, p.glow)
    return (16, 24)


def eyeball(g, p, cx, cy, r, iris, look=0):
    ell(g, cx, cy, r + 1, r, (232, 226, 210))
    ell(g, cx + look, cy, max(r // 2 + 1, 2), max(r // 2 + 1, 2), iris)
    ell(g, cx + look, cy, max(r // 4, 1), max(r // 4, 1), (20, 16, 20))
    g.point((cx + look - 1, cy - 1), fill=(255, 255, 255))


def head(g, p, a, b, fangs=False, keen=False):
    if keen:
        g.polygon([(3, 16), (-0 + 5, 13), (7, 19)], fill=SKIN)
    ell(g, 15, 16, 10, 11, SKIN if not fangs else p.dark)
    ell(g, 14, 15, 9, 10, SKIN if not fangs else p.base)
    g.rectangle([22, 15, 26, 17], fill=mul(SKIN, 0.8) if not fangs else p.dark)
    r = 3 if not keen else 4
    eyeball(g, p, 19, 12, r, (70, 160, 196) if keen else p.dark, 1)
    g.line([(15, 8), (22, 9 - (1 if keen else 0))], fill=mul(SKIN, 0.55), width=1)
    gap = int(a * 5)
    if fangs:
        g.polygon([(14, 19), (26, 19), (26, 20 + gap), (14, 22 + gap)], fill=(70, 24, 24))
        fang_row(g, p, 15, 19, 3, 4, BONE)
        fang_row(g, p, 16, 21 + gap, 2, -3, BONE)
    else:
        g.line([(17, 21), (24, 21)], fill=mul(SKIN, 0.5), width=1)
    g.polygon([(5, 10), (14, 3), (21, 8), (14, 7)], fill=mul(WOOD, 0.7))
    if keen:
        g.rectangle([5, 8, 23, 10], fill=(168, 62, 58))
    return (13, 28)


def keen_head(g, p, a, b):
    return head(g, p, a, b, keen=True)


def fanged_head(g, p, a, b):
    return head(g, p, a, b, fangs=True)


def plain_head(g, p, a, b):
    return head(g, p, a, b)


def eye_stalk(g, p, a, b):
    sway = int(math.sin(a * 3) * 2)
    pts = [(14, 30), (14, 24), (15 + sway, 18), (17 + sway, 13)]
    g.line(pts, fill=p.dark, width=5)
    g.line(pts, fill=p.base, width=3)
    eyeball(g, p, 18 + sway, 9, 5, p.dark, 1)
    return (14, 30)


def compound_eyes(g, p, a, b):
    ell(g, 15, 16, 7, 7, p.dark)
    spots = [(11, 12), (15, 11), (19, 12), (11, 16), (15, 16), (19, 16), (13, 20), (17, 20)]
    for i, (x, y) in enumerate(spots):
        ell(g, x, y, 2, 2, mix(p.glow, p.dark, 0.2 if (i + int(a * 3)) % 3 else 0.6))
        g.point((x - 1, y - 1), fill=(255, 255, 255))
    return (15, 24)


def dim_eye(g, p, a, b):
    ell(g, 15, 16, 8, 6, (110, 108, 100))
    ell(g, 16, 16, 4, 4, (66, 66, 70))
    ell(g, 16, 16, 2, 2, (22, 20, 24))
    g.line([(6, 12), (24, 12)], fill=(60, 56, 52), width=2)
    return (14, 26)


def vertical_eye(g, p, a, b, brow):
    h = 9 + int(a * 2)
    g.polygon([(16, 16 - h), (24, 16), (16, 16 + h), (8, 16)], fill=(228, 222, 206))
    g.polygon([(16, 16 - h), (24, 16), (16, 16 + h), (8, 16)], outline=p.dark)
    ell(g, 16, 16, 4, 6, p.base)
    ell(g, 16, 16, 2, 4, (20, 16, 20))
    g.point((15, 14), fill=(255, 255, 255))
    if brow:
        g.line([(5, 8), (16, 4), (27, 8)], fill=p.deep, width=2)
    else:
        for ang in range(0, 360, 45):
            x, y = 16 + math.cos(math.radians(ang)) * 13, 16 + math.sin(math.radians(ang)) * 13
            g.line([(16 + (x - 16) * 0.85, 16 + (y - 16) * 0.85), (x, y)], fill=p.glow)
    return (16, 28)


def third_eye(g, p, a, b):
    return vertical_eye(g, p, a, b, False)


def judging_eye(g, p, a, b):
    return vertical_eye(g, p, a, b, True)


def heart(g, p, a, b, scale):
    s = scale + a * 0.12 + b * 0.05
    cx, cy = 16, 16

    def pt(x, y):
        return (cx + x * s, cy + y * s)

    left = [pt(x, y) for x, y in [(-1, -4), (-5, -8), (-10, -6), (-11, -1), (-6, 6), (0, 11)]]
    right = [pt(x, y) for x, y in [(1, -4), (5, -8), (10, -6), (11, -1), (6, 6), (0, 11)]]
    g.polygon(left + right[::-1], fill=(150, 52, 58))
    g.polygon([pt(-8, -5), pt(-3, -6), pt(-2, -2), pt(-6, 0), pt(-9, -2)], fill=(196, 92, 92))
    g.polygon([pt(0, 4), pt(5, 0), pt(4, 4), pt(0, 9)], fill=(110, 36, 46))
    g.rectangle([cx - 2 * s, cy - 11 * s, cx + 2 * s, cy - 6 * s], fill=(120, 40, 48))
    return (16, 16)


def small_heart(g, p, a, b):
    return heart(g, p, a, b, 0.55)


def plain_heart(g, p, a, b):
    return heart(g, p, a, b, 0.7)


def large_heart(g, p, a, b):
    return heart(g, p, a, b, 0.8)


def great_heart(g, p, a, b):
    heart(g, p, a, b, 0.88)
    g.arc([5, 5, 27, 27], 200, 340, fill=p.glow)
    return (16, 16)


def gland_mark(g, p):
    if p.element == "void":
        ell(g, 17, 18, 4, 4, p.light)
        ell(g, 17, 18, 3, 3, (10, 6, 18))
    elif p.element == "mind":
        g.line([(14, 18), (17, 14), (20, 18), (17, 21), (15, 19)], fill=p.deep, width=1)
        ell(g, 17, 18, 1, 1, p.glow)
    elif p.element == "lightning":
        g.line([(18, 13), (15, 18), (19, 18), (16, 23)], fill=(255, 250, 200), width=2)
    elif p.element == "radiance":
        g.line([(17, 13), (17, 23)], fill=(255, 255, 255))
        g.line([(12, 18), (22, 18)], fill=(255, 255, 255))
        ell(g, 17, 18, 2, 2, (255, 255, 255))
    else:
        ell(g, 17, 18, 3, 3, p.glow)


def gland(g, p, a, b):
    shaded_ell(g, p, 16, 17, 9, 8)
    shaded_ell(g, p, 10, 11, 4, 4)
    shaded_ell(g, p, 22, 12, 3, 3)
    gland_mark(g, p)
    drop = int(a * 3)
    g.polygon([(25, 20 + drop), (27, 24 + drop), (23, 24 + drop)], fill=p.base)
    g.arc([4, 5, 28, 29], 20, 70, fill=STEEL)
    return (16, 16)


def sac(g, p, a, b, eggs, plume=False):
    r = 6 + int(a * 2)
    shaded_ell(g, p, 16, 17, r + 2, r)
    for i in range(eggs):
        ang = i * 2.4
        x, y = 16 + math.cos(ang) * 4, 17 + math.sin(ang) * 3
        ell(g, x, y, 2, 2, BONE)
        g.point((x - 1, y - 1), fill=(255, 250, 235))
    g.polygon([(14, 17 - r), (18, 17 - r), (16, 12 - r)], fill=p.dark)
    if plume:
        for i in range(3):
            g.line([(11 + i * 5, 17 - r + 1), (9 + i * 7, 6)], fill=(36, 34, 44), width=2)
    return (16, 16)


def spawning_sac(g, p, a, b):
    return sac(g, p, a, b, 3)


def brood_sac(g, p, a, b):
    lift = int(a * 2)
    shaded_ell(g, p, 11, 19, 6, 6)
    shaded_ell(g, p, 21, 17, 6, 6)
    shaded_ell(g, p, 16, 12 - lift, 5, 5)
    for x, y in [(10, 19), (13, 21), (21, 17), (19, 15), (16, 12 - lift), (23, 20)]:
        ell(g, x, y, 1.5, 1.5, BONE)
    return (16, 16)


def raven_sac(g, p, a, b):
    return sac(g, p, a, b, 3, plume=True)


def ganglion(g, p, a, b):
    nodes = [(16, 16), (9, 9), (23, 9), (9, 23), (23, 23)]
    for x, y in nodes[1:]:
        g.line([(16, 16), (x, y)], fill=p.dark, width=2)
    for i, (x, y) in enumerate(nodes):
        glow = (i + int(a * 4)) % 2 == 0
        ell(g, x, y, 3, 3, p.glow if glow else p.base)
    return (16, 16)


def torso(g, p, a, b):
    g.polygon([(7, 4), (22, 4), (25, 12), (22, 28), (9, 28), (6, 14)], fill=mul(SKIN, 0.7))
    g.polygon([(8, 5), (21, 5), (23, 12), (20, 27), (10, 27), (7, 14)], fill=SKIN)
    g.polygon([(8, 14), (21, 14), (20, 27), (10, 27)], fill=p.dark)
    g.rectangle([8, 22, 21, 23], fill=WOOD)
    g.line([(15, 6), (15, 13)], fill=mul(SKIN, 0.6))
    g.rectangle([14, 22, 17, 24], fill=STEEL)
    return (15, 16)


def carapace(g, p, a, b):
    g.chord([3, 6, 28, 34], 180, 360, fill=p.dark)
    g.chord([4, 7, 27, 32], 180, 360, fill=p.base)
    for x in (9, 15, 21):
        g.line([(x, 8), (x + (x - 15) // 3, 26)], fill=p.deep)
    g.arc([4, 8, 26, 30], 200, 260, fill=p.light, width=2)
    g.rectangle([4, 26, 27, 28], fill=p.deep)
    return (16, 18)


def blob(g, p, a, b, feet):
    squash = int(a * 2) + b
    shaded_ell(g, p, 16, 18 + squash // 2, 10 + squash, 9 - squash)
    for x, y in [(11, 14), (19, 17), (15, 21)]:
        ell(g, x, y, 2, 2, mix(p.light, p.base, 0.3))
    if feet:
        for x in (8, 16, 24):
            ell(g, x, 27, 3, 3, p.dark)
            ell(g, x, 26, 2, 2, p.base)
    return (16, 14 if feet else 18)


def gel_mass(g, p, a, b):
    return blob(g, p, a, b, False)


def pseudopods(g, p, a, b):
    return blob(g, p, a, b, True)


def coil(g, p, a, b):
    for i, (x, y, rx) in enumerate([(15, 25, 11), (16, 20, 9), (15, 15, 7)]):
        ell(g, x, y, rx, 4, p.dark)
        ell(g, x, y - 1, rx - 1, 3, p.base)
        for k in range(-rx + 3, rx - 2, 4):
            g.point((x + k, y - 2), fill=p.light)
    sway = int(a * 2) + b
    neck = [(15, 14), (18 + sway, 12)]
    g.line(neck, fill=p.dark, width=6)
    g.line(neck, fill=p.base, width=4)
    return (16, 27)


def serpent_coil(g, p, a, b):
    return coil(g, p, a, b)


def serpent_tail(g, p, a, b):
    pts = [(4, 8), (10, 14), (16, 20), (22, 24), (27, 22 - int(a * 6) - b)]
    for i in range(len(pts) - 1):
        g.line([pts[i], pts[i + 1]], fill=p.dark, width=max(7 - i, 3))
    for i in range(len(pts) - 1):
        g.line([pts[i], pts[i + 1]], fill=p.base, width=max(5 - i, 1))
    for x, y in pts[1:4]:
        g.point((x - 1, y - 1), fill=p.light)
    return (4, 8)


def gas_bladder(g, p, a, b):
    s = int(a * 2)
    shaded_ell(g, p, 16, 12, 8 + s, 8 + s)
    g.polygon([(14, 20), (18, 20), (16, 24)], fill=p.dark)
    g.line([(16, 24), (14, 28)], fill=p.deep)
    g.line([(16, 24), (19, 28)], fill=p.deep)
    ell(g, 21, 8, 1.5, 1.5, p.glow)
    return (16, 20)


def levitator(g, p, a, b):
    g.ellipse([5, 10, 27, 18], fill=p.dark)
    g.ellipse([5, 9, 27, 16], fill=p.base)
    g.ellipse([9, 10, 20, 13], fill=p.light)
    spread = 3 + int(a * 3)
    for dx in (-6, 0, 6):
        g.line([(16 + dx, 19), (16 + dx * 1.5, 19 + spread + 4 + b)], fill=p.glow)
    ell(g, 16, 18, 8, 2, mix(p.glow, p.base, 0.4))
    return (16, 14)


def imp_body(g, p, a, b):
    shaded_ell(g, p, 15, 17, 7, 7)
    shaded_ell(g, p, 17, 9, 4, 4)
    g.polygon([(13, 6), (12, 2), (16, 5)], fill=BONE)
    g.polygon([(19, 5), (21, 2), (21, 6)], fill=BONE)
    g.point((19, 9), fill=(255, 190, 80))
    flap = int(a * 3)
    g.polygon([(9, 15), (3, 10 - flap), (6, 19)], fill=p.dark)
    g.line([(13, 24), (12, 28)], fill=p.deep, width=2)
    g.line([(18, 24), (19, 28)], fill=p.deep, width=2)
    g.line([(8, 20), (4, 22 + b)], fill=p.dark, width=2)
    return (15, 17)


def crow_body(g, p, a, b):
    shaded_ell(g, p, 14, 17, 9, 7)
    g.polygon([(5, 15), (2, 20), (6, 22)], fill=p.deep)
    ell(g, 21, 11, 4, 4, p.dark)
    g.polygon([(24, 10), (28, 12), (24, 13)], fill=(190, 150, 70))
    g.point((22, 10), fill=(250, 240, 200))
    g.arc([7, 5 - int(a * 2), 21, 21], 200, 330, fill=p.light, width=2)
    g.line([(12, 24), (12, 28)], fill=p.deep)
    g.line([(17, 24), (17, 28)], fill=p.deep)
    return (14, 17)


def wisp_core(g, p, a, b):
    ell(g, 16, 17, 8, 8, p.base)
    g.polygon([(10, 15), (16, 3 - b), (22, 15)], fill=p.base)
    ell(g, 16, 17, 5, 5, p.glow)
    ell(g, 16, 18, 2, 3, (255, 255, 255))
    for i in range(3):
        x = 10 + i * 6
        g.polygon([(x - 2, 23), (x + 2, 23), (x, 27 + int(a * 2))], fill=p.dark)
    return (16, 17)


def legs(g, p, a, b, thick, hoof):
    step = int(a * 5) - 2
    for hip, off in ((13, -step), (19, step)):
        top, bottom = 10, 28
        g.line([(hip, top), (hip + off // 2, 19), (hip + off, bottom)], fill=p.dark, width=thick + 2)
        g.line([(hip, top), (hip + off // 2, 19), (hip + off, bottom)], fill=p.base, width=thick)
        fx = hip + off
        g.polygon([(fx - 3, 28), (fx + 5, 28), (fx + 5, 30), (fx - 3, 30)], fill=BONE if hoof else p.deep)
    return (16, 10)


def plain_legs(g, p, a, b):
    return legs(g, p, a, b, 3, False)


def beast_legs(g, p, a, b):
    return legs(g, p, a, b, 4, True)


def spider_legs(g, p, a, b):
    lift = int(a * 3)
    for i, dx in enumerate((-10, -6, 6, 10)):
        pts = [(16, 8), (16 + dx // 2, 3 + (lift if i % 2 else 0)), (16 + dx, 12), (16 + dx + (2 if dx > 0 else -2), 28)]
        g.line(pts, fill=p.dark, width=3)
        g.line(pts, fill=p.base, width=1)
    ell(g, 16, 9, 5, 4, p.dark)
    return (16, 8)


def bat_wings(g, p, a, b):
    drop = int(a * 4) + b
    root, wrist = (21, 27), (16, 12)
    tips = [(9, 4 + drop), (3, 11 + drop), (4, 21 + drop // 2)]
    scallops = [(9, 12), (7, 18), (13, 23)]
    outline = [root, wrist, tips[0], scallops[0], tips[1], scallops[1], tips[2], scallops[2]]
    g.polygon(outline, fill=p.dark)
    g.polygon([(x + (1 if x < 20 else -1), y + (1 if y < 26 else -1)) for x, y in outline], fill=p.base)
    for tip in tips:
        g.line([wrist, tip], fill=p.deep)
    g.line([root, wrist], fill=p.deep, width=2)
    return (root[0], root[1] - 4)


def wings_of_light(g, p, a, b):
    root = (24, 26)
    sweep = int(a * 8) + b * 3
    for i, length in enumerate([26, 25, 23, 20, 16]):
        angle = math.radians(35 + i * 15 + sweep)
        tip = (root[0] - math.sin(angle) * length, root[1] - math.cos(angle) * length)
        side = (math.cos(angle) * 2, -math.sin(angle) * 2)
        g.polygon([(root[0] - side[0], root[1] - side[1]), tip, (root[0] + side[0], root[1] + side[1])],
                  fill=p.light if i % 2 else p.base)
        g.line([root, tip], fill=p.glow)
    ell(g, root[0], root[1], 3, 3, p.glow)
    return root


def armour_plate(g, p, a, b):
    g.polygon([(6, 3), (26, 3), (28, 16), (16, 29), (4, 16)], fill=p.deep)
    g.polygon([(7, 4), (25, 4), (26, 15), (16, 27), (6, 15)], fill=STEEL)
    g.polygon([(9, 6), (16, 6), (16, 24), (8, 14)], fill=p.light)
    g.line([(16, 4), (16, 27)], fill=mul(STEEL, 0.6))
    for x, y in [(9, 7), (23, 7), (16, 24)]:
        g.point((x, y), fill=p.dark)
    return (16, 16)


KIND_DRAWERS = {
    "torso": torso, "carapace": carapace, "gel_mass": gel_mass, "pseudopods": pseudopods,
    "serpent_coil": serpent_coil, "serpent_tail": serpent_tail, "gas_bladder": gas_bladder, "levitator": levitator,
    "imp_body": imp_body, "crow_body": crow_body, "wisp_core": wisp_core,
    "head": plain_head, "keen_head": keen_head, "fanged_head": fanged_head,
    "eye_stalk": eye_stalk, "compound_eyes": compound_eyes, "dim_eye": dim_eye,
    "third_eye": third_eye, "judging_eye": judging_eye,
    "heart": plain_heart, "small_heart": small_heart, "large_heart": large_heart, "great_heart": great_heart,
    "legs": plain_legs, "beast_legs": beast_legs, "spider_legs": spider_legs,
    "bat_wings": bat_wings, "wings_of_light": wings_of_light,
    "sword_hand": sword_hand, "spear_hand": spear_hand, "lugh_spear": lugh_spear, "gungnir": gungnir,
    "war_club": war_club, "frost_trident": frost_trident, "lightning_hammer": lightning_hammer,
    "shield_hand": shield_hand, "bow_hand": bow_hand, "sun_bow": sun_bow,
    "claw": claw, "minion_claw": minion_claw, "wisp_spark": wisp_spark, "mandibles": mandibles, "frost_fang": frost_fang, "ember_maw": ember_maw, "void_maw": void_maw,
    "electric_tentacle": electric_tentacle, "void_tendril": void_tendril, "venom_stinger": stinger,
    "mind_lobe": brain_lobe, "glow_lantern": lantern, "radiant_crown": crown,
    "fire_hand": fire_hand, "thunderbolt_hand": thunderbolt_hand, "healing_hand": healing_hand,
    "hearth_hand": hearth_hand, "serpent_hand": serpent_hand,
    "spawning_sac": spawning_sac, "brood_sac": brood_sac, "raven_sac": raven_sac,
    "ganglion": ganglion, "armour_plate": armour_plate,
}


def drawer_for(part):
    if part["id"] in KIND_DRAWERS:
        return KIND_DRAWERS[part["id"]]
    if part["id"].endswith("_gland"):
        return gland
    sys.exit("no drawing routine for part " + part["id"])


def element_of(part, element_ids):
    for ability in part.get("abilities", []):
        if ability["element"] != "kinetic":
            return ability["element"]
    for element in element_ids:
        if part["id"].startswith(element):
            return element
    resist = part.get("resistances", {})
    boosted = [e for e, v in resist.items() if v > 0 and e != "kinetic"]
    return boosted[0] if boosted else "kinetic"


def tint(img, colour, amount):
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if a:
                px[x, y] = mix((r, g, b), colour, amount) + (a,)


def shift(img, dx, dy):
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.paste(img, (dx, dy))
    return out


def add_outline(img):
    src = img.load()
    out = img.copy()
    px = out.load()
    for y in range(img.height):
        for x in range(img.width):
            if src[x, y][3] > 0:
                continue
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if 0 <= nx < img.width and 0 <= ny < img.height and src[nx, ny][3] > 0:
                    px[x, y] = OUTLINE
                    break
    return out


def break_up(img, rng, margin):
    tint(img, (96, 92, 90), 0.65)
    img = shift_in_bounds(img, 0, 3, margin)
    px = img.load()
    solid = [(x, y) for y in range(img.height) for x in range(img.width) if px[x, y][3]]
    for x, y in rng.sample(solid, len(solid) // 7):
        px[x, y] = (0, 0, 0, 0)
    for _ in range(3):
        x, y = rng.choice(solid)
        for k in range(4):
            if x + k < img.width and y + k < img.height and px[x + k, y + k][3]:
                px[x + k, y + k] = (24, 20, 22, 255)
    return img


def shift_in_bounds(layer, dx, dy, margin):
    box = layer.getbbox()
    dx = max(margin - box[0], min(dx, layer.width - margin - box[2]))
    dy = max(margin - box[1], min(dy, layer.height - margin - box[3]))
    return shift(layer, dx, dy)


def bob_shift(layer, dy, margin):
    room_below = layer.height - margin - layer.getbbox()[3]
    return shift_in_bounds(layer, 0, dy if room_below >= dy else -dy, margin)


def downscale(layer):
    small = layer.resize((INTERNAL_FRAME, INTERNAL_FRAME), Image.BOX)
    px = small.load()
    for y in range(small.height):
        for x in range(small.width):
            r, g, b, a = px[x, y]
            px[x, y] = (r, g, b, 255) if a >= 100 else (0, 0, 0, 0)
    return small


def draw_raw(drawer, palette, a, part):
    layer = Image.new("RGBA", (FRAME, FRAME), (0, 0, 0, 0))
    pivot = drawer(ImageDraw.Draw(layer), palette, a, 0)
    if part["id"] in REAR_PARTS:
        layer = ImageOps.mirror(layer)
        pivot = (FRAME - 1 - pivot[0], pivot[1])
    return layer, pivot


def fit_offset(layers, margin, label):
    boxes = [layer.getbbox() for layer in layers]
    left, top = min(b[0] for b in boxes), min(b[1] for b in boxes)
    right, bottom = max(b[2] for b in boxes), max(b[3] for b in boxes)
    if right - left > FRAME - 2 * margin or bottom - top > FRAME - 2 * margin:
        EDGE_FAILURES.append(f"{label}: art {right - left}x{bottom - top} is larger than the frame allows")
        return 0, 0
    dx = max(margin - left, min(0, FRAME - margin - right))
    dy = max(margin - top, min(0, FRAME - margin - bottom))
    return dx, dy


def finish_frame(layer, state, b, rng, internal):
    margin = 4 if internal else 2
    if b:
        layer = bob_shift(layer, BOB * (2 if internal else 1), margin)
    if state == "hurt":
        tint(layer, (230, 70, 60), 0.5)
        layer = shift_in_bounds(layer, -2 if internal else -1, 0, margin)
    if state == "broken":
        layer = break_up(layer, rng, margin)
    if internal:
        layer = downscale(layer)
    return add_outline(layer)


def write_atlas(path, sheet, atlas):
    sheet.save(path + ".png")
    with open(path + ".json", "w") as f:
        json.dump(atlas, f, indent=1)
        f.write("\n")


EDGE_FAILURES = []


def check_clear_of_edges(label, img):
    box = img.getbbox()
    if box is None or box[0] == 0 or box[1] == 0 or box[2] == img.width or box[3] == img.height:
        EDGE_FAILURES.append(f"{label}: opaque pixels touch the frame edge, bbox {box}")


def split_frames(sheet, size_w, size_h):
    return [sheet.crop((x, 0, x + size_w, size_h)) for x in range(0, sheet.width, size_w)]


def make_part(part, element_ids, directory):
    palette = Palette(element_of(part, element_ids), BASE_OVERRIDE.get(part["id"]))
    drawer = drawer_for(part)
    rng = random.Random(part["id"])
    internal = part["kind"] == "internal"
    size = INTERNAL_FRAME if internal else FRAME
    raws = [draw_raw(drawer, palette, a, part) for _, _, a, _ in FRAME_LAYOUT]
    dx, dy = fit_offset([layer for layer, _ in raws], 4 if internal else 2, part["id"])
    sheet = Image.new("RGBA", (size * len(FRAME_LAYOUT), size), (0, 0, 0, 0))
    frames = {"idle": [], "act": [], "hurt": [], "broken": []}
    for index, ((state, number, _, b), (layer, _)) in enumerate(zip(FRAME_LAYOUT, raws)):
        img = finish_frame(shift(layer, dx, dy), state, b, rng, internal)
        check_clear_of_edges(f"{part['id']} {state} {number}", img)
        sheet.paste(img, (index * size, 0))
        frames[state].append(index)
    scale = FRAME // size
    pivot = ((raws[0][1][0] + dx) // scale, (raws[0][1][1] + dy) // scale)
    atlas = {"frame_w": size, "frame_h": size, "pivot": {"x": int(pivot[0]), "y": int(pivot[1])}, "frames": frames}
    write_atlas(os.path.join(directory, part["sprite"]), sheet, atlas)
    return sheet


def px_rect(g, x, y, w, h, fill):
    g.rectangle([x, y, x + w - 1, y + h - 1], fill=fill)


def floor_variant(index):
    rng = random.Random(100 + index)
    img = Image.new("RGBA", (OBJ, OBJ), (92, 84, 72, 255))
    g = ImageDraw.Draw(img)
    for _ in range(14):
        x, y = rng.randrange(OBJ), rng.randrange(OBJ)
        g.point((x, y), fill=rng.choice([(104, 96, 82, 255), (80, 74, 64, 255), (98, 88, 70, 255)]))
    if index == 1:
        g.line([(2, 10), (7, 8), (11, 12)], fill=(66, 60, 52, 255))
    if index == 2:
        g.rectangle([9, 3, 12, 5], fill=(110, 102, 88, 255))
        g.point((3, 12), fill=(70, 100, 60, 255))
    if index == 3:
        g.line([(3, 3), (8, 6), (6, 12)], fill=(70, 64, 56, 255))
    g.line([(0, 0), (OBJ - 1, 0)], fill=(84, 77, 66, 255))
    g.line([(0, 0), (0, OBJ - 1)], fill=(84, 77, 66, 255))
    return img


def rock():
    img = Image.new("RGBA", (ROCK, ROCK), (0, 0, 0, 0))
    g = ImageDraw.Draw(img)
    g.polygon([(3, 28), (4, 15), (10, 7), (19, 4), (27, 11), (29, 28)], fill=(106, 102, 100, 255))
    g.polygon([(6, 27), (7, 16), (12, 10), (19, 8), (19, 27)], fill=(138, 134, 130, 255))
    g.polygon([(19, 8), (26, 13), (28, 27), (19, 27)], fill=(80, 77, 78, 255))
    g.polygon([(9, 17), (13, 13), (16, 15), (12, 20)], fill=(156, 152, 146, 255))
    g.line([(14, 18), (17, 24), (15, 27)], fill=(66, 64, 64, 255))
    g.line([(22, 14), (24, 20)], fill=(62, 60, 62, 255))
    g.line([(5, 28), (28, 28)], fill=(70, 66, 62, 255))
    return add_outline(img)


def pillar():
    img = Image.new("RGBA", (PILLAR_W, PILLAR_H), (0, 0, 0, 0))
    g = ImageDraw.Draw(img)
    stone, light, dark = (124, 118, 110, 255), (150, 144, 134, 255), (88, 84, 80, 255)
    px_rect(g, 6, 3, 20, 6, light)
    px_rect(g, 4, 9, 24, 3, stone)
    px_rect(g, 8, 12, 16, 42, stone)
    px_rect(g, 8, 12, 5, 42, light)
    px_rect(g, 20, 12, 4, 42, dark)
    for x in (14, 17):
        g.line([(x, 13), (x, 53)], fill=dark)
    px_rect(g, 4, 54, 24, 3, stone)
    px_rect(g, 3, 57, 26, 4, light)
    px_rect(g, 3, 60, 26, 1, dark)
    g.line([(11, 26), (13, 31), (12, 37)], fill=(66, 62, 60, 255))
    g.line([(21, 40), (19, 46)], fill=(66, 62, 60, 255))
    return add_outline(img)


def obj_atlas(frames, w=OBJ, h=OBJ, pivot=None):
    return {"frame_w": w, "frame_h": h, "pivot": pivot or {"x": w // 2, "y": h // 2}, "frames": frames}


def sheet_of(images):
    w, h = images[0].size
    sheet = Image.new("RGBA", (w * len(images), h), (0, 0, 0, 0))
    for i, img in enumerate(images):
        sheet.paste(img, (i * w, 0))
    return sheet


def vial(colour, bob, rage=False):
    img = Image.new("RGBA", (WORLD, WORLD), (0, 0, 0, 0))
    g = ImageDraw.Draw(img)
    y = 2 + bob
    px_rect(g, 9, y, 6, 3, WOOD)
    px_rect(g, 10, y + 3, 4, 3, (200, 210, 214, 255))
    g.ellipse([5, y + 6, 18, y + 17], fill=(200, 210, 214, 255))
    g.ellipse([7, y + 8, 16, y + 16], fill=colour + (255,))
    g.rectangle([8, y + 9, 9, y + 10], fill=(255, 255, 255, 255))
    if rage:
        g.polygon([(10, y + 10), (14, y + 10), (12, y + 15)], fill=(250, 220, 160, 255))
    else:
        px_rect(g, 10, y + 11, 5, 2, (240, 230, 230, 255))
        px_rect(g, 11, y + 9, 3, 6, (240, 230, 230, 255))
    return add_outline(img)


def trap_base(g, p):
    g.ellipse([2, 10, 21, 20], fill=(74, 68, 64, 255))
    g.ellipse([4, 12, 19, 19], fill=p.deep)


def snare(element, armed):
    p = Palette(element)
    img = Image.new("RGBA", (WORLD, WORLD), (0, 0, 0, 0))
    g = ImageDraw.Draw(img)
    g.ellipse([2, 10, 21, 20], fill=p.dark)
    g.ellipse([4, 12, 19, 19], fill=p.base)
    spikes = [(6, 10), (12, 7), (18, 10)] if armed else [(5, 4), (12, 3), (19, 4)]
    for x, y in spikes:
        g.polygon([(x - 2, 15), (x + 2, 15), (x, y)], fill=p.glow)
    return add_outline(img)


def generic_trap(element, armed):
    p = Palette(element)
    img = Image.new("RGBA", (WORLD, WORLD), (0, 0, 0, 0))
    g = ImageDraw.Draw(img)
    g.ellipse([2, 10, 21, 20], fill=p.dark)
    g.ellipse([4, 12, 19, 19], fill=p.deep)
    if not armed:
        ell(g, 12, 7, 4, 4, p.glow)
    return add_outline(img)


def blank():
    img = Image.new("RGBA", (WORLD, WORLD), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def soma(bob):
    return vial((214, 160, 60), bob)


def ember_seed(bob):
    return vial((190, 98, 52), bob, rage=True)


def menders_moss(bob):
    img, g = blank()
    y = 2 + bob
    g.ellipse([4, y + 9, 19, y + 18], fill=(70, 110, 50, 255))
    g.ellipse([7, y + 6, 16, y + 14], fill=(112, 152, 82, 255))
    g.ellipse([9, y + 4, 14, y + 9], fill=(150, 190, 100, 255))
    return add_outline(img)


def quickening_leaf(bob):
    img, g = blank()
    y = 2 + bob
    g.polygon([(12, y + 2), (19, y + 8), (12, y + 18), (5, y + 8)], fill=(96, 170, 70, 255))
    g.line([(12, y + 3), (12, y + 17)], fill=(60, 120, 40, 255))
    return add_outline(img)


def eye_of_far_seeing(bob):
    img, g = blank()
    y = 2 + bob
    g.ellipse([3, y + 6, 20, y + 15], fill=(240, 236, 220, 255))
    g.ellipse([8, y + 6, 15, y + 15], fill=(152, 112, 164, 255))
    g.ellipse([10, y + 8, 13, y + 12], fill=OUTLINE)
    return add_outline(img)


def ward_stone(bob):
    img, g = blank()
    y = 2 + bob
    g.rounded_rectangle([5, y + 4, 18, y + 18], radius=4, fill=(120, 124, 132, 255))
    g.line([(12, y + 7), (12, y + 15)], fill=(238, 226, 206, 255))
    g.line([(9, y + 11), (15, y + 11)], fill=(238, 226, 206, 255))
    return add_outline(img)


PICKUP_DRAWERS = {
    "soma": soma, "menders_moss": menders_moss, "ember_seed": ember_seed,
    "quickening_leaf": quickening_leaf, "eye_of_far_seeing": eye_of_far_seeing, "ward_stone": ward_stone,
}


def spike_pit(element, armed):
    p = Palette(element)
    img, g = blank()
    trap_base(g, p)
    tips = [(7, 13), (12, 11), (17, 13)] if armed else [(7, 5), (12, 3), (17, 5)]
    for x, y in tips:
        g.polygon([(x - 2, 16), (x + 2, 16), (x, y)], fill=STEEL + (255,))
    return add_outline(img)


def spore_pod(element, armed):
    p = Palette(element)
    img, g = blank()
    trap_base(g, p)
    if armed:
        g.ellipse([7, 6, 17, 16], fill=p.base)
        g.ellipse([10, 8, 14, 12], fill=p.glow)
    else:
        for cx, cy in [(6, 6), (12, 4), (18, 6), (9, 10), (15, 10)]:
            ell(g, cx, cy, 2, 2, p.glow)
    return add_outline(img)


def lightning_pylon(element, armed):
    p = Palette(element)
    img, g = blank()
    trap_base(g, p)
    px_rect(g, 10, 4, 4, 11, STEEL + (255,))
    if armed:
        ell(g, 12, 4, 2, 2, p.glow)
    else:
        g.line([(12, 4), (6, 9), (14, 11), (8, 16)], fill=p.glow)
        g.line([(12, 4), (18, 9), (10, 11), (16, 16)], fill=p.glow)
    return add_outline(img)


def void_rift(element, armed):
    p = Palette(element)
    img, g = blank()
    g.ellipse([2, 10, 21, 20], fill=p.dark)
    g.ellipse([5, 12, 18, 18], fill=p.deep)
    g.ellipse([8, 13, 15, 17], fill=p.glow if not armed else p.base)
    return add_outline(img)


TRAP_DRAWERS = {"kinetic": spike_pit, "frost": snare, "venom": spore_pod, "lightning": lightning_pylon, "void": void_rift}


def make_pickup(obj, directory):
    drawer = PICKUP_DRAWERS[obj["id"]]
    images = [drawer(0), drawer(1)]
    for i, img in enumerate(images):
        check_clear_of_edges(f"{obj['id']} {i}", img)
    write_atlas(os.path.join(directory, obj["sprite"]), sheet_of(images), obj_atlas({"idle": [0, 1]}, WORLD, WORLD))
    return sheet_of(images)


def make_trap(obj, directory):
    drawer = TRAP_DRAWERS.get(obj["element"], generic_trap)
    images = [drawer(obj["element"], True), drawer(obj["element"], False)]
    for i, img in enumerate(images):
        check_clear_of_edges(f"{obj['id']} {i}", img)
    write_atlas(os.path.join(directory, obj["sprite"]), sheet_of(images), obj_atlas({"idle": [0], "act": [1]}, WORLD, WORLD))
    return sheet_of(images)


def effect_frame(element, i, count):
    p = Palette(element)
    t = i / (count - 1)
    rng = random.Random(f"{element}{i}")
    img = Image.new("RGBA", (FRAME, FRAME), (0, 0, 0, 0))
    g = ImageDraw.Draw(img)
    c = FRAME // 2
    if element == "fire":
        for k, dx in enumerate((-6, 0, 6)):
            flame(g, p, c + dx + rng.randint(-1, 1), 26 - int(t * 3), 4 + rng.randint(0, 2) + (k == 1) * 2)
    elif element == "frost":
        for k in range(6):
            ang = k * 60 + 15
            r = 4 + t * 11
            tip = (c + math.cos(math.radians(ang)) * r, c + math.sin(math.radians(ang)) * r)
            base = rot([(c + 3, c - 2), (c + 3, c + 2)], (c, c), ang)
            g.polygon([tip] + base, fill=p.light)
            g.line([(c, c), tip], fill=p.glow)
    elif element == "lightning":
        for k in range(2):
            x, y = c - 12 + k * 4, c - 10 + k * 20
            pts = [(x, y)]
            for _ in range(5):
                x += rng.randint(2, 4)
                y += rng.randint(-5, 5) + (c - y) // 3
                pts.append((x, y))
            g.line(pts, fill=p.base, width=3)
            g.line(pts, fill=(255, 250, 220), width=1)
    elif element == "radiance":
        r = 3 + t * 6
        ell(g, c, c, r, r, p.base)
        ell(g, c, c, r * 0.6, r * 0.6, p.glow)
        for k in range(8):
            ang = math.radians(k * 45 + i * 8)
            g.line([(c + math.cos(ang) * (r + 1), c + math.sin(ang) * (r + 1)), (c + math.cos(ang) * (r + 4), c + math.sin(ang) * (r + 4))], fill=p.light)
    elif element == "venom":
        for _ in range(7):
            x = c + rng.randint(-7, 7) * (0.4 + 0.6 * t)
            y = c + rng.randint(-6, 6) * (0.4 + 0.6 * t) - t * 2
            r = 3 + rng.randint(0, 3)
            ell(g, x, y, r, r, p.dark)
            ell(g, x - 1, y - 1, max(r - 1, 1), max(r - 1, 1), p.base)
        for _ in range(3):
            ell(g, c + rng.randint(-8, 8), c + rng.randint(-8, 8), 1, 1, p.glow)
    elif element == "mind":
        pts = []
        for k in range(30):
            ang = k * 0.45 + i * 0.9
            r = 1 + k * 0.38
            pts.append((c + math.cos(ang) * r, c + math.sin(ang) * r))
        g.line(pts, fill=p.base, width=2)
        g.line(pts[8:], fill=p.glow, width=1)
    elif element == "void":
        for k in range(3):
            r = 3 + ((t + k / 3) % 1.0) * 13
            g.ellipse([c - r, c - r, c + r, c + r], outline=mix(p.base, p.light, k / 3), width=2)
        ell(g, c, c, 3, 3, (12, 8, 20))
    else:
        for k in range(10):
            ang = math.radians(k * 36 + rng.randint(-8, 8))
            r0, r1 = 3 + t * 5, 6 + t * 7
            g.line([(c + math.cos(ang) * r0, c + math.sin(ang) * r0), (c + math.cos(ang) * r1, c + math.sin(ang) * r1)], fill=p.glow if k % 2 else p.base, width=2)
    return img


def make_effect(element, directory, count=5):
    frames = [effect_frame(element, i, count) for i in range(count)]
    for i, img in enumerate(frames):
        check_clear_of_edges(f"effect {element} {i}", img)
    sheet = sheet_of(frames)
    atlas = {"frame_w": FRAME, "frame_h": FRAME, "pivot": {"x": FRAME // 2, "y": FRAME // 2}, "frames": {"play": list(range(count))}}
    write_atlas(os.path.join(directory, element), sheet, atlas)
    return sheet


def make_tiles(directory):
    floors = [floor_variant(i) for i in range(4)]
    write_atlas(os.path.join(directory, "floor"), sheet_of(floors), obj_atlas({"variants": [0, 1, 2, 3]}))
    stone = rock()
    column = pillar()
    for label, img in (("rock", stone), ("pillar", column)):
        check_clear_of_edges(label, img)
    write_atlas(os.path.join(directory, "rock"), stone, obj_atlas({"idle": [0]}, ROCK, ROCK, {"x": ROCK // 2, "y": ROCK - 4}))
    write_atlas(os.path.join(directory, "pillar"), column, obj_atlas({"idle": [0]}, PILLAR_W, PILLAR_H, {"x": PILLAR_W // 2, "y": PILLAR_H - 4}))
    return sheet_of(floors), sheet_of([stone]), column


def contact_sheet(entries, path, columns=2, scale=4):
    font = ImageFont.load_default()
    cell_w = max(s.width for _, s in entries) * scale + 150
    cell_h = max(s.height for _, s in entries) * scale + 8
    rows = -(-len(entries) // columns)
    out = Image.new("RGBA", (cell_w * columns, cell_h * rows), (58, 54, 50, 255))
    d = ImageDraw.Draw(out)
    for i, (label, sheet) in enumerate(entries):
        x, y = (i % columns) * cell_w, (i // columns) * cell_h
        d.text((x + 6, y + cell_h // 2 - 5), label, fill=(235, 230, 220, 255), font=font)
        big = sheet.resize((sheet.width * scale, sheet.height * scale), Image.NEAREST)
        out.alpha_composite(big, (x + 150, y + 4))
    out.save(path)


def load(name):
    with open(os.path.join(ROOT, "data", name)) as f:
        return json.load(f)


def main():
    if "-h" in sys.argv or "--help" in sys.argv:
        print(__doc__)
        return
    element_ids = [e["id"] for e in load("elements.json")["elements"]]
    arena = load("arena_objects.json")
    dirs = {name: os.path.join(ASSETS, name) for name in ("parts", "pickups", "traps", "effects", "tiles")}
    for d in dirs.values():
        os.makedirs(d, exist_ok=True)
    entries = []
    for part in load("parts.json")["parts"]:
        entries.append(("part " + part["id"], make_part(part, element_ids, dirs["parts"])))
    for obj in arena["pickups"]:
        entries.append(("pickup " + obj["id"], make_pickup(obj, dirs["pickups"])))
    for obj in arena["traps"]:
        entries.append(("trap " + obj["id"], make_trap(obj, dirs["traps"])))
    for element in element_ids:
        entries.append(("effect " + element, make_effect(element, dirs["effects"])))
    floors, rocks, column = make_tiles(dirs["tiles"])
    entries += [("floor tiles", floors), ("rock", rocks), ("pillar", column)]
    contact_sheet(entries, os.path.join(ASSETS, "contact_sheet.png"))
    if EDGE_FAILURES:
        sys.exit("\n".join(EDGE_FAILURES))
    print(f"wrote {len(entries)} sprite sheets")


if __name__ == "__main__":
    main()
