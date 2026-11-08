# NetHack for jailbroken PS5

A native PS5 build of NetHack 5.0 (dev). It uses NetHack's **curses** interface
drawn by **PDCursesMod's SDL2 backend**, on top of the
[ps5-payload-dev SDL2 port](https://github.com/ps5-payload-dev/SDL). It shows
on the TV and takes input from a USB keyboard and/or a DualSense.

> Status: builds and links (`nethack.elf`, x86-64). Not yet tested on hardware.

## Requirements

- A jailbroken PS5 that can run ELF payloads (elfldr on port 9021), plus an
  FTP server payload (e.g. ftpsrv on port 2121) to copy files over.
- To launch: either the websrv homebrew launcher, or shsrv (`hbldr`).
- Build host: Linux or WSL2 (tested on Ubuntu 24.04) with
  `clang-18 lld-18 cmake make git wget unzip python3`.

## Build

```sh
cd sys/ps5
./setup.sh          # once: SDK, SDL2 for PS5, PDCursesMod, host data files
make                # -> nethack.elf
make package        # -> pkg/NetHack/
```

## Install and run

1. Copy `pkg/NetHack/` to `/data/NetHack/` on the PS5 over FTP.
2. Launch it in one of two ways:
   - **Homebrew launcher (websrv):** NetHack shows up as a tile, driven by
     `homebrew.js`.
   - **shsrv:** run `telnet <ps5> 2323`, then
     `hbldr /data/NetHack/nethack.elf`.

Saves, the high-score list and `.nethackrc` live in `/data/NetHack/`.

## Controls

A USB keyboard works for every command. On the DualSense:

| Button | Key | Action |
|---|---|---|
| D-pad / left stick | `h j k l` / `y u b n` | Move (the stick does diagonals; hold to keep walking) |
| Cross | Enter | Confirm, continue past --More-- |
| Circle | Esc | Cancel |
| Square | `,` | Pick up |
| Triangle | `i` | Inventory |
| L1 / R1 | `<` / `>` | Go up / down stairs |
| L2 | `s` | Search |
| R2 | `.` | Rest |
| Options | `#` | Extended command |
| Share | `?` | Help |
| Touchpad click | `:` | Look here |
| L3 | `;` | Farlook |
| R3 | `x` | Swap weapons |

To change a mapping, edit the `button_map` table in `ps5main.c`.

## Files

| File | Purpose |
|---|---|
| `ps5main.c` | `SDL_main` entry point. Sets up the data directory, maps the controller to keys, preloads a 2× font, then calls NetHack's `main` (compiled as `nethack_main`). |
| `Makefile` | Cross-compiles NetHack, Lua, and PDCursesMod (pdcurses plus the sdl2 backend) and links them against SDL2. |
| `setup.sh` | One-time setup of the dependencies, into `deps/` (gitignored). |
| `homebrew.js` | Entry for the websrv homebrew launcher. |
| `nethackrc` | Default options (curses, DECgraphics, colour). |
| `mkicon.py` | Generates the 512×512 `@` icon from the built-in font. |

---

## Findings (porting notes)

### Toolchain
- The [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) ships
  `prospero-clang`, `prospero-cmake`, `prospero.mk` and so on. It installs
  without root: just unzip it anywhere. It uses the host's clang-18 and lld.
- The SDK's CMake toolchain sets `CMAKE_INSTALL_PREFIX=/user/homebrew`, which
  is the path on the PS5. Install libraries with
  `DESTDIR=$SDK/target`, otherwise you get
  `file cannot create directory: /user/homebrew/lib`.
- SDL's configure step takes several minutes. That's normal: each feature check
  is a full cross-compile and link (about 1.1 s each, and there are hundreds).
- The sysroot has **no `libm`** and **no `libsamplerate`**, even though
  SDL's generated `sdl2-config` lists `-lsamplerate`. Drop both: the math
  functions are in libc.

### Graphics and input
- The SDL2 port has real PS5 backends: video (`sceVideoOut`), joystick
  (`scePad`), and keyboard (`sceKeyboard`, so USB keyboards work). There is
  no SDL_ttf.
- PDCursesMod's SDL2 backend, built narrow (non-`PDC_WIDE`), needs no TTF. It
  falls back to a built-in 8×16 CP437 bitmap font, `font437`, a 1bpp BMP.
- PDCurses uses `pdc_font` if it is set before `initscr()`. The port preloads
  the font scaled 2× (16×32 cells, 2-entry palette), which gives a 120×33
  grid on 1920×1080.
- `font437` is defined in `pdcscrn.c`. Declare it `extern` rather than
  including `font437.h` a second time, or you get a duplicate-symbol link
  error.
- PS5 SDL's `main()` hides the splash screen, calls `SDL_main`, then calls
  `sceSystemServiceLoadExec("exit")`. The app needs a logged-in user and the
  GUI.
- The DualSense shows up as an SDL joystick: 17 buttons, 6 axes, 1 hat.
  Button numbers: 0 Cross, 1 Circle, 2 Square, 3 Triangle, 4 Touchpad,
  5 Share, 6 Options, 7 L3, 8 R3, 9 L1, 10 R1, 11–14 D-pad (up, down, left,
  right), 15 L2, 16 R2. The D-pad is reported both as buttons and as the hat.
- PDCurses reads printable characters from `SDL_TEXTINPUT`, and
  Enter and Escape from `SDL_KEYDOWN`. An `SDL_SetEventFilter` callback turns
  joystick events into those, and a 30 ms SDL timer handles hold-to-walk.

### Launching GUI homebrew
- GUI SDL apps such as LakeSnes and yquake2 aren't sent to port 9021 the way
  plain payloads are. They are launched as homebrew apps, either through the
  websrv homebrew menu (a folder with the `.elf`, a `homebrew.js` and an
  `sce_sys/icon0.png`) or through shsrv's `hbldr <elf> [args]`.

### NetHack 5.0 specifics
- Lua comes with the source (`nhlua/lua/src`), so there's nothing extra to
  download. PDCursesMod is a git submodule.
- The PS5 build needs no source patches. The config already has opt-out
  switches: `-DNOTTYGRAPHICS -DCURSES_GRAPHICS -DDEFAULT_WINDOW_SYS=\"curses\"
  -DPDCURSES -DNOMAIL -DNOSUSPEND -DNOSHELL -DNOPANICTRACE`.
- `unixmain.c` is compiled with `-Dmain=nethack_main` so that
  `ps5main.c` can own `SDL_main`.
- `src/date.c` isn't in `HOBJ`, because the Unix Makefile builds it separately.
  Leave it out and the link fails with `undefined symbol: nomakedefs`.
- Data files come from a native host build (`hints/linux-minimal`,
  no DLB). Only `include/date.h` is generated as a header.
- At runtime the game reads `NETHACKDIR`. `ps5main.c` sets it, and `HOME`, to
  the directory holding the `.elf`, falling back to `/data/NetHack`. It also
  creates `save/`, `perm`, `record`, `logfile`, `xlogfile` and `livelog`.

### Build-environment gotchas (Windows + WSL)
- If you run `wsl -- bash -lc '... $VAR ...'` from PowerShell, `$VAR`
  is expanded before bash sees it, so it often comes out empty. Put commands
  in a script file instead.
- Likewise, PowerShell expands `~` to the Windows home folder in
  `wsl -- bash ~/x.sh`. `cd` inside a `bash -lc '...'` string instead.

## Credits and licenses
NetHack is under the NetHack General Public License. SDL2 is zlib-licensed.
PDCursesMod is public domain. Lua is MIT-licensed. The PS5 SDK, the SDL
port, and the launcher conventions come from
[ps5-payload-dev](https://github.com/ps5-payload-dev) (John Törnblom et al.).
`homebrew.js` is modeled on LakeSnes's, which is GPLv3+.
