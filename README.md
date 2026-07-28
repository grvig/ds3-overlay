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
- Dark Souls III (versions 1.01.1 - 1.15.2; tested against 1.15.0)
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

| Key | Does |
| --- | --- |
| `F10` | Show / hide the overlay |
| `F11` | Close the overlay |

These work while the game has focus - handy, since the overlay is click-through
and can't be interacted with directly.

## Settings

Where the overlay sits on screen is read from `overlay-settings.txt`, kept next
to `overlay.exe`:

```
x=10
y=10
```

`x` and `y` are the top-left corner in screen pixels. Delete the file to go back
to the defaults - a missing or malformed file is ignored rather than treated as
an error.

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

Memory offsets are tied to specific game versions. The overlay reads the
version straight off the game executable and picks the matching offsets from a
table covering 1.01.1 through 1.15.2. On a version that isn't in the table, the
souls counter shows `--` and everything else keeps working - boss tracking
doesn't depend on version-specific offsets, since it finds what it needs by
pattern scanning.

Offsets and byte patterns are sourced from public Dark Souls III reverse
engineering work - the
[practice tool](https://github.com/veeenu/darksoulsiii-practice-tool) and
[The Grand Archives' cheat table](https://github.com/The-Grand-Archives/Dark-Souls-III-CT-TGA).
