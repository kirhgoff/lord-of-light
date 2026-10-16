"""Compose a Body from placeholder Part sprites at its slot anchors.

Usage (run from the repo root):
    uv run --with pillow==12.3.0 python tools/compose_body.py deity Agni
    uv run --with pillow==12.3.0 python tools/compose_body.py plan quadruped body=carapace head=fanged_head ...

Writes <out>_1x.png and <out>_2x.png (default out: build/body_<name>), one row of
four views: all Parts, External Parts only, anchors marked, all Parts broken.
"""
import argparse
import glob
import json
import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VIEW_W, VIEW_H, ORIGIN = 128, 112, (64, 60)
BACKGROUND = (70, 64, 58, 255)


def load(*path):
    with open(os.path.join(ROOT, *path)) as f:
        return json.load(f)


def load_frame(sprite, state):
    atlas = load("assets", "parts", sprite + ".json")
    sheet = Image.open(os.path.join(ROOT, "assets", "parts", sprite + ".png")).convert("RGBA")
    size = atlas["frame_w"]
    left = atlas["frames"][state][0] * size
    return sheet.crop((left, 0, left + size, atlas["frame_h"])), (atlas["pivot"]["x"], atlas["pivot"]["y"])


def deity_placements(name):
    for path in sorted(glob.glob(os.path.join(ROOT, "data", "pantheons", "*.json"))):
        for deity in load(path)["deities"]:
            if deity["name"].lower() == name.lower():
                return [(p["part"], p["x"], p["y"]) for p in deity["body"]["parts"]]
    sys.exit(f"no deity named {name}")


def plan_placements(plan_id, fills):
    plan = next((b for b in load("data", "bodyplans.json")["bodyplans"] if b["id"] == plan_id), None)
    if plan is None:
        sys.exit(f"no body plan {plan_id}")
    placements = []
    for slot in plan["slots"]:
        fill = fills.get(slot["id"], slot.get("part"))
        if fill:
            placements.append((fill, slot["x"], slot["y"]))
        else:
            print(f"slot {slot['id']} has no fill, skipped", file=sys.stderr)
    return placements


def render_view(placements, parts, state="idle", show_internal=True, markers=False):
    view = Image.new("RGBA", (VIEW_W, VIEW_H), BACKGROUND)
    draw_order = sorted(placements, key=lambda p: parts[p[0]]["kind"] == "internal")
    for part_id, x, y in draw_order:
        part = parts[part_id]
        if part["kind"] == "internal" and not show_internal:
            continue
        frame, (px, py) = load_frame(part["sprite"], state)
        view.alpha_composite(frame, (ORIGIN[0] + x - px, ORIGIN[1] + y - py))
    if markers:
        draw = ImageDraw.Draw(view)
        for part_id, x, y in placements:
            colour = (255, 0, 255, 255) if parts[part_id]["kind"] == "external" else (0, 255, 255, 255)
            draw.line([(ORIGIN[0] + x - 1, ORIGIN[1] + y), (ORIGIN[0] + x + 1, ORIGIN[1] + y)], fill=colour)
    return view


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", choices=["deity", "plan"])
    parser.add_argument("name", help="deity name or body plan id")
    parser.add_argument("fills", nargs="*", help="slot=part for a body plan")
    parser.add_argument("--out", help="output path prefix")
    args = parser.parse_args()
    parts = {p["id"]: p for p in load("data", "parts.json")["parts"]}
    if args.source == "deity":
        placements = deity_placements(args.name)
    else:
        placements = plan_placements(args.name, dict(f.split("=", 1) for f in args.fills))
    views = [
        render_view(placements, parts),
        render_view(placements, parts, show_internal=False),
        render_view(placements, parts, show_internal=False, markers=True),
        render_view(placements, parts, state="broken"),
    ]
    row = Image.new("RGBA", (VIEW_W * 4 + 12, VIEW_H), (40, 36, 34, 255))
    for i, view in enumerate(views):
        row.paste(view, (i * (VIEW_W + 4), 0))
    out = args.out or os.path.join(ROOT, "build", "body_" + args.name.lower())
    os.makedirs(os.path.dirname(out), exist_ok=True)
    row.save(out + "_1x.png")
    row.resize((row.width * 2, row.height * 2), Image.NEAREST).save(out + "_2x.png")
    print(out + "_1x.png", out + "_2x.png")


if __name__ == "__main__":
    main()
