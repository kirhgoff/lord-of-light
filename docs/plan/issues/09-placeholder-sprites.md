# Placeholder sprites and atlas

Type: task
Status: resolved
Blocked by: 02, 15

## Question

A Python (Pillow) script `tools/gen_sprites.py` that generates janky but readable pixel art:

- One PNG sheet per Part sprite id in `data/parts.json` (16–32 px frames: idle ×2, act ×3, hurt ×1, broken ×1) + `assets/parts/<id>.json` atlas with frame rects and the attach pivot. Side view; colours hint at Element.
- Floor tiles, obstacle, each Pickup and Trap, and an effect sheet per Element (fire flicker, frost shards, arcs, radiance glow, venom cloud, mind swirl, void ripple, kinetic sparks).
- Generated PNGs are committed; the script is rerunnable. Add a check that every sprite id referenced by data has an atlas.

## Answer

Merged `09-placeholder-sprites` after a Fable design review and re-review. `tools/gen_sprites.py` (`uv run --with pillow==12.3.0 python tools/gen_sprites.py`) writes 81 sheets + atlases `{frame_w, frame_h, pivot, frames}`: external Parts 32-px frames (idle×2 with bob, act×3, hurt, broken), internal Parts 16-px, floor 16-px ×4 variants, rock 32×32, pillar 32×64, Pickups/Traps 24-px, a 5-frame effect sheet per Element; generator fails if art touches a frame edge. `tools/compose_body.py` previews a Deity or Body Plan from sprites. Test checks every referenced atlas + png. Known weak placeholders: Frost Fang, Mind Lobe, hurt = red tint.
