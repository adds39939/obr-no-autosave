# NoAutosave (OBSE64 plugin for Oblivion Remastered)

Truly disables autosaving in The Elder Scrolls IV: Oblivion Remastered.

## Why the in-game toggles don't work

The game has four autosave toggles (loading, fast travel, rest, wait). They are
stored in `Save_Settings.sav` and pushed into the embedded Gamebryo engine as the
`bSaveOnTravel`, `bSaveOnRest`, `bSaveOnWait` and `bSaveOnInteriorExteriorSwitch`
INI settings.

Tracing the 1.512.105 executable shows that the fast travel, rest, wait,
level-up, scripted and interior/exterior triggers all check those settings.
One trigger does not: when the player moves to a cell in a different
worldspace (most doors and area transitions), the engine unconditionally sets an
"autosave after this load" flag on the save manager. After the load finishes
that flag makes the engine call its autosave request function, which saves with
the name `autosave`. No setting is consulted anywhere on that path, so the
toggles cannot stop it.

## What the plugin does

At load time it signature-scans the game's `.text` section for two locations
and verifies them (the request function must reference the string `autosave`
and the trigger must be the exact `or dword [rax+30h], 8000h` instruction).
Nothing is patched if either check fails, and the reason is written to the log.

| INI option | Patch |
|---|---|
| `bDisableAllAutosaves=1` (default) | The autosave request function is made to return immediately. Every autosave trigger funnels through it, so no autosave of any kind is ever written. Manual saves and quicksaves use different code and are unaffected. |
| `bDisableAllAutosaves=0` | Only the un-gated worldspace-change trigger is removed. The in-game toggles then work as advertised for fast travel, rest, wait and interior/exterior switches. |
| `bEnableLog=1` | Writes `NoAutosave.log` next to the DLL on every launch. Off by default. |

Addresses found in 1.512.105 (for reference):

| What | RVA |
|---|---|
| Autosave request function | `0x6703BC0` |
| Un-gated worldspace-change trigger (`or` instruction) | `0x6602457` |

## Install

Requires [OBSE64](https://www.nexusmods.com/oblivionremastered/mods/282).

- **Amethyst / MO2-style managers:** install `dist/NoAutosave-OBSE64-<version>.zip`
  as a mod. It contains `OBSE/Plugins/NoAutosave.dll` and
  `OBSE/Plugins/NoAutosave.ini`, the same layout other OBSE64 plugins use.
- **Manual:** copy those two files to
  `OblivionRemastered/Binaries/Win64/OBSE/Plugins/`.

With `bEnableLog=1` in the INI, a `NoAutosave.log` is written next to the DLL
on every launch. A good run ends with `patched: all autosaves disabled`. The
log is off by default; turn it on to confirm the patch or to report a problem
after a game update.

## Build

Needs `mingw-w64-gcc` (for the DLL) and a host `g++` (for the test).

```
./build.sh 6703BC0 6602457
```

`build.sh` builds the DLL, runs `test/test_scan` against the installed game
exe to confirm the signatures match exactly once at the expected RVAs, and
packages the zip into `dist/`. `test/wine_load.cpp` is an optional loader
harness for checking the exports under Wine.

## Game updates

The plugin declares itself address-independent (signature scanning) so OBSE64
will load it on newer game versions. If a patch changes the code around either
location the scan fails, nothing is patched, and the log says so.
