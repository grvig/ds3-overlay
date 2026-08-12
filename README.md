# ds3-overlay

A live completionist overlay for Dark Souls III. It reads the running game's
memory and displays your progress on top of the game as you play - no manual
checklist, no alt-tabbing.

Currently tracks:

- Every main boss in the base game and both DLCs (25), marked green once defeated
- Every bonfire in the base game and both DLCs (77), marked green once lit
- Questline rewards for 30 NPCs (80 items)
- **Questline rewards you can no longer get**, worked out from your actual save
- Per-area progress on each heading, so a finished area is obvious at a glance
- An overall completion percentage
- Your live souls count

The missed-item warnings are the point of the whole thing. A wiki can tell you
Siegward's questline exists; it can't tell you that *your* save already lost it
because you killed Yhorm without him.

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

## Tests

```
test.bat
```

Checks that the data files are well-formed, that every missable rule refers to
a flag actually being tracked, that the rules fire only on genuine loss, and
that the generated flag-reading code can never overwrite its own results area.
Runs without the game open.

## Running

Start Dark Souls III first, get into a save, then run `overlay.exe`. The overlay
appears in the top-left corner and updates once a second.

If the game isn't running yet, the overlay shows a waiting message and connects
on its own once the game appears. If you quit the game, it goes back to waiting.
Either way it waits a few seconds after spotting the game before attaching, so
it never interrupts the game while it's still starting up.

| Key | Does |
| --- | --- |
| `F9` | Move the overlay to the next screen corner |
| `F10` | Show / hide the overlay |
| `F11` | Close the overlay |

These work while the game has focus - handy, since the overlay is click-through
and can't be interacted with directly.

The overlay sizes itself to its contents and splits into side-by-side columns
when the list is too tall for the screen, since there's nothing to scroll.

### Previewing without the game

```
overlay.exe --demo
```

Fills in made-up progress so the layout can be checked with the game closed.
Demo mode never touches the game process.

### Checking a flag id

```
main.exe --watch
```

Reports the moment any tracked flag changes. Do the thing in game and see which
entry fires - or see nothing fire, which means that entry's flag id is wrong.
This is how the questline data should be verified.

## The tracked data

The lists live in `data/` as plain text, one entry per line:

```
Name | flag id | Group
```

| File | Holds |
| --- | --- |
| `data/bosses.txt` | Boss "defeated" flags |
| `data/bonfires.txt` | Bonfire "lit" flags |
| `data/quests.txt` | Questline reward flags |
| `data/missable.txt` | Rules for what closes off what |

Blank lines and `#` comments are ignored, and anything malformed is reported
with its line number rather than silently skipped. Editing these needs no
rebuild, which is the point - the quest data especially will be wrong before
it's right.

`data/missable.txt` has four columns instead of three:

```
What you lose | its flag | flag that closes it | What closes it
```

A rule fires only when the closing flag is set and the reward flag isn't - that
is, the window shut and you didn't get it. Nothing predicts what you're *about*
to do. That's deliberate: a tool that cries wolf gets ignored, and one that
wrongly reassures you costs a playthrough.

### Confidence in the data

| Data | Sources | Checked in game |
| --- | --- | --- |
| Bosses | Grand Archives cheat table | Yes |
| Bonfires | SoulSplitter, cross-checked against the cheat table | No |
| Questline rewards | Souls Modding Wiki only | **No** |
| Missable rules | Hand-written | **No** |

The questline flags come from a single source with nothing to cross-check
against, unlike the bonfires - where having two sources is exactly what caught
a wrong flag id. Treat them as provisional until `main.exe --watch` confirms
them.

## Settings

Read from `overlay-settings.txt`, kept next to `overlay.exe`:

```
x=10
y=10
fontSize=15
showSouls=true
showBosses=true
showBonfires=true
```

| Setting | Means |
| --- | --- |
| `x`, `y` | Top-left corner of the overlay, in screen pixels |
| `fontSize` | Text height in pixels, 8 to 48. Everything else scales to match |
| `showSouls` | Show the souls counter |
| `showBosses` | Show the boss list |
| `showBonfires` | Show the bonfire list |
| `showQuests` | Show the full questline reward list (off by default - it's long) |
| `showMissable` | Show rewards you can no longer get (on by default) |

Turning a section off shrinks the overlay to suit, which is the simplest way to
make it less intrusive. `true/false`, `yes/no`, `on/off` and `1/0` all work.

Moving the overlay with `F9` rewrites this file, so wherever you leave it is
where it comes back. Delete the file to return to defaults - a missing or
malformed file is ignored rather than treated as an error, and a single bad
line only discards that one setting.

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
   All 102 flags are checked in a single batch, split across several passes
   automatically if they don't fit in one.
4. **Draw it.** The results are rendered into a per-pixel-transparent window
   layered over the game.

## Notes

Memory offsets are tied to specific game versions. The overlay reads the
version straight off the game executable and picks the matching offsets from a
table covering 1.01.1 through 1.15.2. On a version that isn't in the table, the
souls counter shows `--` and everything else keeps working - boss and bonfire
tracking don't depend on version-specific offsets, since they find what they
need by pattern scanning.

Offsets and byte patterns are sourced from public Dark Souls III reverse
engineering work - the
[practice tool](https://github.com/veeenu/darksoulsiii-practice-tool),
[The Grand Archives' cheat table](https://github.com/The-Grand-Archives/Dark-Souls-III-CT-TGA)
and [SoulSplitter](https://github.com/FrankvdStam/SoulSplitter).

Where sources disagreed, the bonfire list follows SoulSplitter: the cheat table
reuses one flag for both Keep Ruins and Abyss Watchers, and omits Farron Keep
Perimeter entirely.

## Layout of the code

| File | What's in it |
| --- | --- |
| `overlay.cpp` | The overlay window: what to draw and where |
| `main.cpp` | The console tool, including `--watch` |
| `ds3reader.h` | Finding the game and reading its memory |
| `datafile.h` | Reading the `data/` files |
| `tracked.h` | The loaded lists, in one place |
| `missable.h` | Working out what can no longer be obtained |
| `layout.h` | Working out columns, kept free of drawing code so it can be tested |
| `settings.h` | Reading and writing the settings file |
| `tests.cpp` | Checks that run without the game |

`missable.h` and `layout.h` deliberately contain no Windows or game code, so
the tests can drive the real logic rather than a copy of it.
