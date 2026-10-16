# Resizable window, full-window Arena, overlay tech panel

Type: task
Status: claimed
Blocked by: none

## Question

1. **Resizable window.** `FLAG_WINDOW_RESIZABLE` before `InitWindow`. When `IsWindowResized()` fires, take `GetScreenWidth()`/`GetScreenHeight()` into the Viewport and re-clamp. The HUD, footer, Clay layout, tech panel, lineup and end screens all follow the new size. Set a sensible minimum window size (`SetWindowMinSize`).
2. **Arena fills the window.** Zoom becomes fractional (float). The lowest zoom is the *cover* zoom: the smallest zoom at which the Arena covers the whole window, i.e. `max(width / ARENA_WIDTH_PX, height / ARENA_HEIGHT_PX)`. The Arena never shows empty margins at any window size. `=` steps up to the next whole zoom above the current one, `-` steps down to the previous whole zoom, and neither goes below the cover zoom or above `max(ZOOM_MAX, cover)`. Resizing keeps the world point at the screen centre where it was, then clamps. Starting zoom = cover zoom.
3. **The camera uses the whole window.** The Arena is drawn under the HUD bar (already translucent) and under the tech panel. Delete `inset_right`/`viewport_set_inset` and the `HUD_HEIGHT` subtraction from the camera centre, clamp and screen↔world maths. Overlay labels keep their current rule of not going under the HUD.
4. **Tech panel overlays the Arena.** `PANEL_BACKGROUND` (and the Battle Log panel if separate) becomes translucent so the Arena shows through; text stays readable (keep contrast, e.g. ~70–80 % opaque dark). Picking still ignores the mouse over the panel.

## Acceptance

- Unit tests in `tests/test_view.c` for the pure Viewport maths: cover zoom at a few window sizes (wider and taller than the Arena's aspect), zoom stepping (cover 1.67 → `=` 2 → 3 → 4; `-` from 2 → 1.67), clamping keeps the Arena covering the window, resize keeps the centre world point, world↔screen round trip with fractional zoom.
- `make test`, `make asan`, `make game` green, zero warnings.
- `make screenshots` still produces its shots; also take screenshots at 1280×720, 1920×1080 and 900×900 (a `--size WxH` flag on `build/game` is fine if needed) with the tech view on and off, and list their paths in the hand-back.
- Saves/loads still work (check whether the save stores the zoom; if it does, keep loading old saves working).
