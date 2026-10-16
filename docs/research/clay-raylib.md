# Clay with raylib in C

Answers ticket 14 for ticket 12 (Clay UI). Every claim cites the Clay source at tag **v0.14** (commit `b25a31c`, 2025-06-06) as `clay.h:<line>`, `README.md:<line>` or `renderers/raylib/clay_renderer_raylib.c:<line>` (shortened to `renderer:<line>`). Line numbers refer to that tag. GitHub issues are cited as `#n`. The example at the end was compiled and run on macOS against raylib 5.5 built from source.

## Which release to pin

**Pin `v0.14`** (tag `v0.14`, commit `b25a31c1a152915cd7dd6796e6592273e5a10aac`). It is the latest tagged release (`gh release list -R nicbarker/clay`; "0.14 - Aspect Ratio Elements, Better Clipping", marked Latest). `clay.h` opens with `// VERSION: 0.14`.

- `main` is 99 commits ahead of the tag as of 2026-10-16, with no newer tag. The changes break the API: #492 removes `.id` from `Clay_ElementDeclaration` and adds a separate `CLAY_WITHID` macro, #334 changes `Clay_OnHover`'s userData from `intptr_t` to `void *`, and #579 adds a transitions API. Code written for v0.14 will not compile against `main` without edits, so don't take files from `main`.
- One bug that `main` fixes affects us: #606, where `Clay_GetScrollOffset` matches by pointer (see the scrolling section). The workaround is cheap.
- Vendor `clay.h` (sha256 `c97241cc…599d`) and `renderers/raylib/clay_renderer_raylib.c` (sha256 `024a9543…274a`) from the tag, and record both in `vendor/VERSIONS` as `clay v0.14 b25a31c`.
- The README is partly stale against the header at v0.14. It shows a `uintptr_t userData` measure-function signature (`README.md:580`), but the header uses `void *` (`clay.h:889`). It documents `#define CLAY_DISABLE_CULLING` (`README.md:474`), but the code uses `Clay_SetCullingEnabled(bool)` (`clay.h:901`, `:4308`). It uses `.attachment` in floating examples (`README.md:1768`), but the field is `.attachPoints` (`clay.h:489`). **Trust `clay.h` over the README.**

## Vendoring the raylib renderer

Layout:

```
vendor/clay/clay.h
vendor/clay/clay_renderer_raylib.c   # verbatim from renderers/raylib/ at v0.14
```

- **Don't copy the `raylib.h` and `raymath.h` that ship next to the renderer** in `renderers/raylib/`. They are raylib 5.5 copies, and because the renderer uses `#include "raylib.h"` with quotes (`renderer:1-2`), a copy in the same directory would win over `vendor/raylib/src`. Leaving it out makes the renderer pick up our vendored raylib through `-I`/`-isystem`.
- The renderer is a `.c` file meant to be `#include`d into the translation unit (TU) that defines `CLAY_IMPLEMENTATION`, after `clay.h`. It doesn't include `clay.h` itself and uses `CLAY__MAX` (see `examples/raylib-sidebar-scrolling-container/main.c:1-3`). It defines non-static globals and functions (`Raylib_camera`, `Clay_Raylib_Render`, `Clay_Raylib_Close`, `GetScreenToWorldPointWithZDistance`; `renderer:11, 35, 139, 148`), so include it in **exactly one** TU (e.g. `src/view/ui.c`) and declare the two functions we call in our own header.
- The renderer's public surface:
  - `void Clay_Raylib_Render(Clay_RenderCommandArray, Font *fonts)` (`renderer:148`) draws text with `fonts[fontId]`. The `Font` array is indexed by `Clay_TextElementConfig.fontId`.
  - `void Clay_Raylib_Initialize(int w, int h, const char *title, unsigned flags)` (`renderer:127`) is just `SetConfigFlags` plus `InitWindow`. Skip it, because the view already owns the window.
  - `void Clay_Raylib_Close()` (`renderer:139`) frees the text scratch buffer **and calls `CloseWindow()`**. Call it instead of `CloseWindow`, or skip it and accept a one-off leak at exit.
- The renderer handles RECTANGLE (rounded through `DrawRectangleRounded`), BORDER, TEXT, IMAGE (`imageData` must point to a `Texture2D`), SCISSOR_START/END (`BeginScissorMode`), and CUSTOM (3D-model sample code we won't use). Any other command type, including `CLAY_RENDER_COMMAND_TYPE_NONE`, hits `exit(1)` (`renderer:259-262`). v0.14 never emits NONE (`clay.h` adds commands only through `Clay__AddRenderCommand`), but #202 reports that earlier versions did.
- On every TEXT command the renderer `malloc`s or copies into a growing scratch buffer to NUL-terminate the slice for `DrawTextEx` (`renderer:156-174`, #128). That's fine at our scale.

### Fonts

- The renderer needs a `Font fonts[]` array that **outlives every frame**. The same pointer goes to `Clay_SetMeasureTextFunction` as `userData` and to `Clay_Raylib_Render` (`examples/raylib-sidebar-scrolling-container/main.c`, `main()`). Use a file-scope `static Font fonts[FONT_COUNT]`, not a stack array in a function that returns.
- Load fonts **after** `InitWindow`, because they need a GL context. Use `GetFontDefault()` (baseSize 10, pixel font) or `LoadFontEx(path, size, NULL, 0)`. The example loads at 2× the display size with a bilinear filter (`LoadFontEx(..., 48, 0, 400)` plus `SetTextureFilter(..., TEXTURE_FILTER_BILINEAR)`).
- Clay's debug view always uses `fontId = 0` (`clay.h`, `Clay_TextElementConfig.fontId` doc comment), so slot 0 must be a valid font.

### Text measurement

The required signature, set once after `Clay_Initialize`, is (`clay.h:889`):

```c
void Clay_SetMeasureTextFunction(
    Clay_Dimensions (*measureTextFunction)(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData),
    void *userData);
```

- `text` is **not NUL-terminated** (`clay.h:185-194`, `README.md:585`). Clay calls the function per whitespace-separated word, never with a space or newline inside (`Clay__MeasureTextCached`, `clay.h:1553+`). It also measures `" "` once per config to learn the space width. Results are cached by content and config hash (`clay.h:1508`).
- The function is on the hot path (`README.md:587`).
- The pointer is a single **global** (`Clay__MeasureText`, `clay.h:1309`), shared by all contexts, while `userData` is per context.
- **Don't use the renderer's `Raylib_MeasureText`** (`renderer:84-125`). It indexes glyphs as `text.chars[i] - 32`, which reads out of bounds for any byte below 32 or any UTF-8 byte at or above 0x80 (`char` is signed on arm64 macOS, so the index goes negative; #371). It also applies `letterSpacing` using only the last line's character count (#365). Supply your own using raylib's `GetCodepointNext` and `GetGlyphIndex`, and match `DrawTextEx`'s advance (`advanceX`, or `recs.width + offsetX` when `advanceX` is 0) times `fontSize / baseSize`, plus `letterSpacing` between glyphs. The example's `MeasureTextUtf8` does this. Because the renderer's function is `static inline`, leaving it unused produces no warning.
- If fonts are reloaded or the scale changes, call `Clay_ResetMeasureTextCache()` (`clay.h:913`).

## Memory arena

```c
uint32_t bytes = Clay_MinMemorySize();                       // clay.h:3864
Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(bytes, malloc(bytes));
Clay_Initialize(arena, (Clay_Dimensions){ w, h }, (Clay_ErrorHandler){ OnClayError, NULL });
Clay_SetMeasureTextFunction(MeasureTextUtf8, fonts);
```

- Clay never allocates after this. Everything lives in the arena, split into persistent memory and per-frame ephemeral memory that `Clay_BeginLayout` reinitialises (`clay.h:4159-4163`).
- The defaults are 8192 elements and a 16384-word text cache (`clay.h:1019-1020`). `Clay_MinMemorySize()` sizes for whatever `Clay_SetMaxElementCount` / `Clay_SetMaxMeasureTextCacheWordCount` set **before** it is called.
- Exceeding a limit calls the error handler with `CLAY_ERROR_TYPE_ELEMENTS_CAPACITY_EXCEEDED` or `CLAY_ERROR_TYPE_TEXT_MEASUREMENT_CAPACITY_EXCEEDED`. Recovery means raising the limit, allocating a new arena and calling `Clay_Initialize` again (the sidebar example's `HandleClayErrors` and its main loop). We cap the Battle Log, so 8192 elements is plenty. Each log line costs one text element plus wrapped-line render commands. Treat the error handler as a crash, not a recovery path.
- A null `errorHandlerFunction` falls back to a default that only prints (`clay.h:3996`, `:1022`).
- `Clay_Initialize` returns a `Clay_Context *`, and the most recently initialised context is current (`README.md:513+`). Context state is not thread-safe, but our view is single-threaded.

## Frame loop wiring

The README orders the per-frame calls as SetLayoutDimensions → SetPointerState → UpdateScrollContainers → BeginLayout → declarations → EndLayout → render (`README.md:553-561`, `:136-142`). For raylib:

```c
Vector2 wheel = GetMouseWheelMoveV();
Clay_SetLayoutDimensions((Clay_Dimensions){ GetScreenWidth(), GetScreenHeight() });
Clay_SetPointerState((Clay_Vector2){ GetMousePosition().x, GetMousePosition().y },
                     IsMouseButtonDown(MOUSE_BUTTON_LEFT));
Clay_UpdateScrollContainers(false, (Clay_Vector2){ wheel.x, wheel.y }, GetFrameTime());
Clay_BeginLayout();
/* CLAY(...) declarations */
Clay_RenderCommandArray cmds = Clay_EndLayout();
BeginDrawing();  /* world first (BeginMode2D/EndMode2D), then: */
Clay_Raylib_Render(cmds, fonts);
EndDrawing();
```

- `Clay_SetLayoutDimensions` takes raylib's **screen** size (logical points). On the 2× retina Mac used for testing, the 1280×720 window rendered at 2560×1440 with correct layout and no extra work (`FLAG_WINDOW_HIGHDPI` not set). #116 reports wrong layout after resizing *with* `FLAG_WINDOW_HIGHDPI`, so leave that flag off.
- `Clay_SetPointerState` must come before anything that reads pointer state (`README.md:290`). It hit-tests against **last frame's** bounding boxes (`clay.h:3913+`, `README.md:342`) and advances the pressed/released state machine (`PRESSED_THIS_FRAME → PRESSED → RELEASED_THIS_FRAME → RELEASED`). Call it **exactly once per frame**, because a second call in the same frame advances the state again.
- Clicks: either use `Clay_OnHover(fn, userData)` inside an element and check `pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME` (`README.md:302-321`), or, simpler, after `SetPointerState` do `if (IsMouseButtonPressed(...) && Clay_PointerOver(CLAY_ID("ConfirmYes")))` (`README.md:325-336`). Use `Clay_Hovered()` during declaration for hover styling (`clay.h:871`).
- Game input versus UI: to stop a click on the panel from also selecting a Combatant, test `Clay_PointerOver(CLAY_ID("EventLog"))` (or the dialog's ID) before handling world clicks. `Clay_GetPointerOverIds()` lists every element under the pointer, but always includes the root container.
- `Clay_UpdateScrollContainers(enableDragScrolling, scrollDelta, dt)` (`clay.h:4045`): each wheel unit becomes 10 px (`clay.h:4113`), and raylib's sign convention (wheel up is positive) already matches Clay's. Pass `enableDragScrolling = false`. With `true`, holding the left button over a scroll container drag-scrolls it with momentum (`clay.h:4047`, `:4122+`), which fights click-to-select in a game.
- Pausing: Clay has no idea the simulation is paused. Keep running the UI loop every frame.

## Scroll containers (event log)

A scroll container is any element with `.clip = { .vertical = true, .childOffset = ... }` (`README.md:344-370`). Clip makes the renderer scissor the children (SCISSOR_START/END), and `childOffset` shifts them. Clay keeps per-container scroll state across frames, keyed by element ID (`clay.h:2121-2141`).

```c
CLAY({ .id = CLAY_ID("EventLog"),
       .layout = { .sizing = { CLAY_SIZING_FIXED(320), CLAY_SIZING_GROW(0) },
                   .layoutDirection = CLAY_TOP_TO_BOTTOM },
       .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
    for (...) CLAY_TEXT(line, &cfg);
}
```

- **Give the container a fixed `CLAY_ID`.** Anonymous elements get positional IDs (`clay.h:1754`), so the scroll state would jump whenever siblings change.
- **#606 (fixed only on `main`):** in v0.14, `Clay_GetScrollOffset()` finds the container by comparing last frame's `Clay_LayoutElement *` with this frame's (`clay.h:4025-4042`). If the number of elements declared *before* the log changes between frames, the offset reads as 0 for one frame and the log flickers. **Declare the panel before variable-count content**, such as overlays, dialogs or conditional siblings. The example declares the panel first.
- **Don't call `Clay_GetScrollContainerData()` during declaration.** It dereferences last frame's element pointer into ephemeral memory that `Clay_BeginLayout` has already wiped, so it returns `found = false` (`clay.h:4261-4280`; verified: inside the declaration it returned `found=0` on every frame, and the log never scrolled). Call it **before `Clay_BeginLayout`**, where it is valid.
- **Stick to the bottom** (newest Battle Log line visible): before `Clay_UpdateScrollContainers`, set `data.scrollPosition->y = -1e9f` while following. `UpdateScrollContainers` then clamps it to `-(contentHeight - containerHeight)` (`clay.h:4096`). Stop following when the wheel moves up over the panel, and resume once the user is back at the bottom (`StickLogToBottom` in the example). Scroll positions are ≤ 0, and more negative means further down.
- **Hiding the panel (T) drops its scroll state.** A container not declared in a frame is removed on the next `UpdateScrollContainers` (`clay.h:4052-4062`). Re-showing starts at the top, or at the bottom with the follow logic.
- For a scrollbar, read `Clay_GetScrollContainerData(id)` before layout. `scrollContainerDimensions`, `contentDimensions` and the writable `scrollPosition` give the thumb size and drag (`examples/raylib-sidebar-scrolling-container/main.c`, `UpdateDrawFrame`). Not needed for v1.

## Overlays at world positions (floating / attachTo)

Floating elements don't affect layout, draw on top according to `zIndex`, and are positioned relative to a parent (`README.md:374-393`; `Clay_FloatingElementConfig`, `clay.h` config block).

- **Use `CLAY_ATTACH_TO_ROOT` plus `.offset` as screen coordinates.** The position works out to `parentAttachPoint(root) + offset - elementAttachPoint(self)` (`clay.h:2627-2677`). With `.attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_TOP }`, `offset` is where the overlay's bottom-centre lands in screen pixels, so the label sits centred above the Combatant whatever its measured size.
- World to screen: `Vector2 s = GetWorldToScreen2D(world, camera);` with the same `Camera2D` used to draw the world, recomputed **every frame** before declaring the overlay. Pan and zoom then just work (verified at zoom 1.5 with the camera panned). `GetWorldToScreen2D` belongs in `src/view/`, never in `src/sim/` (ADR 0002).
- IDs: `CLAY_IDI("Overlay", index)` gives per-Combatant IDs without building strings (`clay.h:80`, `README.md:278`). Unnamed floating elements get `Clay__FloatingContainer` plus the root index (`clay.h:2093-2095`).
- `.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH` on overlays stops them from blocking hover and clicks on Clay elements underneath. The default, CAPTURE, stops hit-testing at the first floating root under the pointer (`clay.h:3966-3969`).
- `zIndex` (int16): floating roots are sorted ascending and the render-command array is already in draw order (`clay.h`, `Clay_RenderCommand.zIndex` comment). Use overlays at `1`, the confirm dialog at `100`, and leave the debug view's range (≥ 32765) alone.
- Culling: an element whose box is entirely off-screen produces no render commands (`clay.h:2465-2476`, enabled by default), so overlays for Combatants panned out of view cost nothing to draw. They still use element slots, so skip declaring them if the count matters.
- Confirm dialog: `.attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER }`, high `zIndex`. For a dimmed backdrop, add another root-attached floating element with `CLAY_SIZING_FIXED(screenW)`/`FIXED(screenH)` and a translucent background. In v0.14 a floating element sized by percent is computed wrongly, which `main` fixes ("Fix incorrect percentage sizing of floating elements", 2025-07-01).
- End-of-battle screen: an ordinary full-screen layout (`GROW`/`GROW`) declared instead of the battle UI, or a root-attached floating panel over a frozen world. It needs nothing new.

## Strings and their lifetimes

- `Clay_String` is `{ bool isStaticallyAllocated; int32_t length; const char *chars; }` (`clay.h:187-194`). **Clay never copies the characters.** Text render commands hold slices into your buffer (`Clay_TextRenderData.stringContents`), so the bytes must stay valid and unchanged **until `Clay_Raylib_Render` returns** for that frame. Reset a per-frame text buffer at the top of the frame, never between `Clay_EndLayout` and rendering.
- `CLAY_STRING("literal")` only accepts string literals (enforced by `CLAY__ENSURE_STRING_LITERAL`, `clay.h:99`) and sets `isStaticallyAllocated = true`. Using it with a `char[]` variable fails to compile.
- **Dynamic text** (overlay numbers, formatted log lines): build `(Clay_String){ .isStaticallyAllocated = false, .length = n, .chars = p }` yourself. **Never mark a reused buffer `isStaticallyAllocated = true`.** The measurement cache then hashes the *pointer and length* instead of the content (`clay.h:1510-1516`), so changing text of the same length in the same buffer keeps a stale width.
- Battle Log lines: store them in a persistent ring buffer of fixed-size `char[]` (they already outlive the frame), and append with `snprintf`. Per-frame formatted strings go in a frame scratch arena (`FrameString` in the example, reset at the start of each frame).
- `CLAY_SID`/`CLAY_SIDI` (dynamic-string IDs) carry the same rule: "should live until at least the next frame" (`README.md:954`).
- `CLAY_TEXT_CONFIG({...})` copies the config into the arena for the frame (`clay.h:56`, `:1324`), so inline configs are fine.

## C11, compiler flags and clang warnings

- `clay.h` needs C99 or later or C++20 (`#error`, `clay.h:31-37`). C11 is fine. Under C23 it switches `CLAY__DEFAULT_STRUCT` to `{}` (`clay.h:171-175`).
- Tested with Apple clang 21: the example (implementation TU plus renderer) builds with **`-std=c11 -Wall -Wextra -Werror`** and no warnings. A second TU that includes `clay.h` without the implementation and declares an element also builds clean (Clay's own `multi-compilation-unit.c` does the same).
- Adding `-pedantic` produces two warnings, both in the renderer: `Clay_Raylib_Close()` lacks `(void)` (`-Wstrict-prototypes`, `renderer:139`), and `(Vector2) {}` is an empty initializer (`-Wc23-extensions`, `renderer:186`). `-Wconversion` produces 64 warnings in `clay.h` and the renderer, and `-Wmissing-prototypes` produces 228 (all the macro-generated array helpers). See also #355.
- **Recommendation:** pass `-isystem vendor/clay` (and `-isystem vendor/raylib/src`) instead of `-I`. Warnings from those headers, including the `#include`d renderer `.c`, are then suppressed, and our own code still builds clean under `-pedantic -Wconversion -Wmissing-prototypes` (verified). Don't patch the vendored files.
- `CLAY_IMPLEMENTATION` must be defined in **exactly one** file before `#include "clay.h"` (`clay.h:3-10`). The implementation `#undef`s the macro after use (`clay.h:939`). Defining it twice gives duplicate-symbol link errors, because the implementation defines non-static globals such as `CLAY_LAYOUT_DEFAULT` and `Clay__currentContext`.
- `clay.h` declares `static uint8_t CLAY__ELEMENT_DEFINITION_LATCH` per TU for the `CLAY()` for-loop macro (`clay.h:103`, `:135-140`). This is harmless, but **don't `return` or `break` out of a `CLAY(...) { }` body**: the macro closes the element in the for-loop increment, so an early exit leaves the element open and corrupts the layout.
- SIMD: on arm64 it includes `<arm_neon.h>` (`clay.h:18-22`); define `CLAY_DISABLE_SIMD` to opt out. No action is needed for macOS arm64.
- The packed enums use `__attribute__((__packed__))` (`clay.h:168`), which works with clang and gcc.

## Minimal working example

This is one file, `main.c`, next to `vendor/clay/{clay.h,clay_renderer_raylib.c}`. It shows the scrolling event log toggled with T (following the tail), per-Combatant overlays anchored to world positions under a panned and zoomed `Camera2D` (arrows and `-`/`=`), and a confirm dialog on Q with clickable Yes/No. Built and run with:

```sh
clang -std=c11 -Wall -Wextra -Werror -isystem vendor/clay -isystem vendor/raylib/src \
  main.c vendor/raylib/src/libraylib.a \
  -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL -o demo
```

Checked by screenshot: the log follows the tail past 40 lines, survives a hide/show toggle, the overlays track the camera at zoom 1.5, and the dialog draws centred above the overlays.

```c
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "clay_renderer_raylib.c"

enum { FONT_BODY, FONT_COUNT };
static Font fonts[FONT_COUNT];

static Clay_Dimensions MeasureTextUtf8(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    Font font = ((Font *)userData)[config->fontId];
    float scale = (float)config->fontSize / (float)font.baseSize;
    float width = 0;
    int glyphs = 0;
    for (int i = 0; i < text.length;) {
        int size = 0;
        int index = GetGlyphIndex(font, GetCodepointNext(&text.chars[i], &size));
        width += font.glyphs[index].advanceX ? (float)font.glyphs[index].advanceX
                                             : font.recs[index].width + (float)font.glyphs[index].offsetX;
        i += size;
        glyphs++;
    }
    float spacing = glyphs > 0 ? (float)(glyphs - 1) * config->letterSpacing : 0;
    return (Clay_Dimensions){ width * scale + spacing, (float)config->fontSize };
}

static void HandleClayError(Clay_ErrorData error) {
    fprintf(stderr, "clay: %.*s\n", error.errorText.length, error.errorText.chars);
}

static char frameText[16 * 1024];
static int frameTextUsed;

static Clay_String FrameString(const char *format, ...) {
    char *out = frameText + frameTextUsed;
    va_list args;
    va_start(args, format);
    int length = vsnprintf(out, sizeof frameText - (size_t)frameTextUsed, format, args);
    va_end(args);
    if (length < 0) length = 0;
    int capacity = (int)sizeof frameText - frameTextUsed - 1;
    if (length > capacity) length = capacity;
    frameTextUsed += length + 1;
    return (Clay_String){ .isStaticallyAllocated = false, .length = length, .chars = out };
}

#define LOG_CAPACITY 256
#define LOG_LINE_BYTES 96
static char logLines[LOG_CAPACITY][LOG_LINE_BYTES];
static int logCount;

static void LogLine(const char *line) {
    snprintf(logLines[logCount % LOG_CAPACITY], LOG_LINE_BYTES, "%s", line);
    logCount++;
}

typedef struct { const char *name; Vector2 world; int integrity; } Combatant;

static void StickLogToBottom(bool *following, float wheelY) {
    Clay_ScrollContainerData log = Clay_GetScrollContainerData(CLAY_ID("EventLog"));
    if (!log.found) return;
    bool atBottom = log.scrollPosition->y <= log.scrollContainerDimensions.height - log.contentDimensions.height + 1;
    if (wheelY > 0 && Clay_PointerOver(CLAY_ID("EventLog"))) *following = false;
    else if (atBottom) *following = true;
    if (*following) log.scrollPosition->y = -1e9f;
}

static const Clay_Color PANEL = { 20, 20, 28, 230 };
static const Clay_Color TEXT = { 230, 230, 230, 255 };
static const Clay_Color BUTTON = { 70, 70, 90, 255 };
static const Clay_Color BUTTON_HOVER = { 110, 110, 150, 255 };

static void CombatantOverlay(int index, const Combatant *c, Camera2D camera) {
    Vector2 screen = GetWorldToScreen2D(c->world, camera);
    CLAY({
        .id = CLAY_IDI("Overlay", index),
        .layout = { .padding = CLAY_PADDING_ALL(4) },
        .backgroundColor = PANEL,
        .floating = {
            .attachTo = CLAY_ATTACH_TO_ROOT,
            .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_TOP },
            .offset = { screen.x, screen.y - 12 },
            .zIndex = 1,
            .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
        },
    }) {
        CLAY_TEXT(FrameString("%s  %d", c->name, c->integrity),
                  CLAY_TEXT_CONFIG({ .fontId = FONT_BODY, .fontSize = 20, .letterSpacing = 2, .textColor = TEXT }));
    }
}

static void EventLogPanel(void) {
    CLAY({
        .id = CLAY_ID("EventLog"),
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(320), CLAY_SIZING_GROW(0) },
            .padding = CLAY_PADDING_ALL(8),
            .childGap = 4,
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
        .backgroundColor = PANEL,
        .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() },
    }) {
        int first = logCount > LOG_CAPACITY ? logCount - LOG_CAPACITY : 0;
        for (int i = first; i < logCount; i++) {
            const char *line = logLines[i % LOG_CAPACITY];
            CLAY_TEXT(((Clay_String){ .length = (int32_t)strlen(line), .chars = line }),
                      CLAY_TEXT_CONFIG({ .fontId = FONT_BODY, .fontSize = 20, .letterSpacing = 2, .textColor = TEXT }));
        }
    }
}

static void ConfirmButton(Clay_ElementId id, Clay_String label) {
    CLAY({ .id = id, .layout = { .padding = { 16, 16, 8, 8 } }, .backgroundColor = Clay_Hovered() ? BUTTON_HOVER : BUTTON }) {
        CLAY_TEXT(label, CLAY_TEXT_CONFIG({ .fontId = FONT_BODY, .fontSize = 20, .letterSpacing = 2, .textColor = TEXT }));
    }
}

static void ConfirmDialog(void) {
    CLAY({
        .id = CLAY_ID("ConfirmDialog"),
        .layout = { .padding = CLAY_PADDING_ALL(16), .childGap = 12, .layoutDirection = CLAY_TOP_TO_BOTTOM },
        .backgroundColor = PANEL,
        .floating = {
            .attachTo = CLAY_ATTACH_TO_ROOT,
            .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER },
            .zIndex = 100,
        },
    }) {
        CLAY_TEXT(CLAY_STRING("Quit the battle?"), CLAY_TEXT_CONFIG({ .fontId = FONT_BODY, .fontSize = 20, .letterSpacing = 2, .textColor = TEXT }));
        CLAY({ .layout = { .childGap = 8 } }) {
            ConfirmButton(CLAY_ID("ConfirmYes"), CLAY_STRING("Yes"));
            ConfirmButton(CLAY_ID("ConfirmNo"), CLAY_STRING("No"));
        }
    }
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "clay + raylib");
    fonts[FONT_BODY] = GetFontDefault();

    uint32_t clayBytes = Clay_MinMemorySize();
    Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(clayBytes, malloc(clayBytes)),
                    (Clay_Dimensions){ (float)GetScreenWidth(), (float)GetScreenHeight() },
                    (Clay_ErrorHandler){ HandleClayError, NULL });
    Clay_SetMeasureTextFunction(MeasureTextUtf8, fonts);

    Combatant combatants[] = { { "Agni", { 200, 300 }, 12 }, { "Kali", { 520, 260 }, 9 }, { "Ghoul", { 700, 420 }, 5 } };
    Camera2D camera = { .zoom = 1.0f };
    bool logVisible = true, confirmOpen = false, followTail = true;
    int frame = 0;

    while (!WindowShouldClose()) {
        frameTextUsed = 0;
        if (IsKeyDown(KEY_RIGHT)) camera.target.x += 4;
        if (IsKeyDown(KEY_LEFT)) camera.target.x -= 4;
        if (IsKeyPressed(KEY_EQUAL)) camera.zoom *= 1.25f;
        if (IsKeyPressed(KEY_MINUS)) camera.zoom /= 1.25f;
        if (IsKeyPressed(KEY_T)) logVisible = !logVisible;
        if (IsKeyPressed(KEY_Q)) confirmOpen = true;
        if (++frame % 30 == 0) LogLine(TextFormat("tick %d: Agni burns Ghoul's left arm", frame / 30));

        Vector2 wheel = GetMouseWheelMoveV();
        Clay_SetLayoutDimensions((Clay_Dimensions){ (float)GetScreenWidth(), (float)GetScreenHeight() });
        Clay_SetPointerState((Clay_Vector2){ GetMousePosition().x, GetMousePosition().y }, IsMouseButtonDown(MOUSE_BUTTON_LEFT));
        StickLogToBottom(&followTail, wheel.y);
        Clay_UpdateScrollContainers(false, (Clay_Vector2){ wheel.x, wheel.y }, GetFrameTime());

        if (confirmOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (Clay_PointerOver(CLAY_ID("ConfirmYes"))) break;
            if (Clay_PointerOver(CLAY_ID("ConfirmNo"))) confirmOpen = false;
        }

        Clay_BeginLayout();
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
            if (logVisible) EventLogPanel();
        }
        for (int i = 0; i < (int)(sizeof combatants / sizeof combatants[0]); i++) CombatantOverlay(i, &combatants[i], camera);
        if (confirmOpen) ConfirmDialog();
        Clay_RenderCommandArray commands = Clay_EndLayout();

        BeginDrawing();
        ClearBackground((Color){ 40, 50, 40, 255 });
        BeginMode2D(camera);
        for (int i = 0; i < (int)(sizeof combatants / sizeof combatants[0]); i++) DrawCircleV(combatants[i].world, 10, RED);
        EndMode2D();
        Clay_Raylib_Render(commands, fonts);
        EndDrawing();
    }
    Clay_Raylib_Close();
    return 0;
}
```

## For ticket 12, in short

1. Vendor `clay.h` and `clay_renderer_raylib.c` from `v0.14` into `vendor/clay/` (without the bundled `raylib.h`), include them with `-isystem`, and put `CLAY_IMPLEMENTATION` plus the renderer `#include` in one view TU.
2. Write your own UTF-8 measure function; don't use `Raylib_MeasureText`.
3. Keep `static Font fonts[]` (slot 0 valid) and pass it to both the measure function and `Clay_Raylib_Render`.
4. Each frame: SetLayoutDimensions → SetPointerState (once) → follow-tail tweak → UpdateScrollContainers(`false`, wheel, dt) → BeginLayout → panel first, then overlays and dialog → EndLayout → draw world → `Clay_Raylib_Render`.
5. Overlays use `ATTACH_TO_ROOT` with `offset = GetWorldToScreen2D(...)` and `CENTER_BOTTOM` as the element attach point.
6. Dynamic text gets `isStaticallyAllocated = false` and a buffer that lives until rendering finishes.
