# ds3-overlay

A live completionist overlay for Dark Souls III. It reads the running game's
memory and displays your progress on top of the game as you play - no manual
checklist, no alt-tabbing.

Currently tracks:

- Every main boss in the base game and both DLCs (25 total), marked green once defeated
- A defeated-count summary
- Your live souls count

## Requirements

- Windows
- Dark Souls III (tested against version 1.15.0.0)
- MinGW-w64 (`g++`) to build

## Building

```
build.bat
```

That produces two programs:

| File | What it is |
| --- | --- |
| `overlay.exe` | The actual overlay - a transparent, always-on-top, click-through window |
| `main.exe` | A console tool that prints the same info once and exits, handy for testing |

## Running

Start Dark Souls III first, get into a save, then run `overlay.exe`. The overlay
appears in the top-left corner and updates once a second.

If the game isn't running yet, the overlay shows a waiting message and connects
on its own once the game appears. If you quit the game, it goes back to waiting.

## How it works

Everything is read-only - the overlay never writes to your save file or modifies
the game.

1. **Find the game.** Look through running processes for `DarkSoulsIII.exe` and
   open a handle to it.
2. **Find the game's internals.** Some things live at a fixed spot relative to
   where the game is loaded (like the souls count). Others have to be found by
   scanning the game's memory for a distinctive sequence of bytes - that's how
   the event flag system and its lookup function are located.
3. **Ask the game directly.** Rather than guessing how boss-kill data is laid
   out in memory, the overlay writes a tiny snippet of machine code into the
   game and runs it, which calls the game's own "is this flag set?" function.
   All 25 bosses are checked in a single batch.
4. **Draw it.** The results are rendered into a per-pixel-transparent window
   layered over the game.

## Notes

Memory offsets are tied to specific game versions. The souls counter currently
assumes version 1.15.0.0 and would need its offset updated if the game ever
patches. Boss tracking is unaffected by version changes, since it finds what it
needs by pattern scanning rather than fixed offsets.

Offsets and byte patterns are sourced from public Dark Souls III reverse
engineering work - the
[practice tool](https://github.com/veeenu/darksoulsiii-practice-tool) and
[The Grand Archives' cheat table](https://github.com/The-Grand-Archives/Dark-Souls-III-CT-TGA).
