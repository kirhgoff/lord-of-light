# Clay with raylib in C

Type: research
Status: resolved
Blocked by:

## Question

How to use Clay (nicbarker/clay) with raylib in plain C for ticket 12: which release to pin, the official raylib renderer file and how to vendor it, text measurement setup, scroll containers for the event log, overlays positioned over world coordinates, input (mouse/scroll) wiring, and known pitfalls. Write findings to `docs/research/clay-raylib.md` with a minimal example.

## Answer

See `docs/research/clay-raylib.md`. Pin Clay v0.14 (main has breaking changes); own UTF-8 text-measure function; scroll via `Clay_GetScrollOffset` during layout; declare the log panel before overlays (#606); overlays floated at `GetWorldToScreen2D`; include vendor dirs with `-isystem`.
