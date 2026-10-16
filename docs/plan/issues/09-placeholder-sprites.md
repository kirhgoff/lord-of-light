# Placeholder sprites and atlas

Type: task
Status: claimed
Blocked by: 02, 15

## Question

A Python (Pillow) script `tools/gen_sprites.py` that generates janky but readable pixel art:

- One PNG sheet per Part sprite id in `data/parts.json` (16–32 px frames: idle ×2, act ×3, hurt ×1, broken ×1) + `assets/parts/<id>.json` atlas with frame rects and the attach pivot. Side view; colours hint at Element.
- Floor tiles, obstacle, each Pickup and Trap, and an effect sheet per Element (fire flicker, frost shards, arcs, radiance glow, venom cloud, mind swirl, void ripple, kinetic sparks).
- Generated PNGs are committed; the script is rerunnable. Add a check that every sprite id referenced by data has an atlas.
