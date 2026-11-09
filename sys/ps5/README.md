# NetHack for jailbroken PS5

NetHack 5.0 (dev) for the PS5, in two builds that share this directory:

- **The native title** (`PPSA92042`, an `eboot.bin` folder app): installs
  from ShadowMountPlus or the PS5 homebrew catalog, shows on the TV and is
  played with the DualSense. Tested on a PS5 (firmware 13.60).
- **The ELF payload** (`nethack.elf`), launched from the homebrew menu or
  shsrv. Builds and links; not yet tested on hardware.

Both use NetHack's **curses** interface drawn by **PDCursesMod's SDL2
backend**.

## The native title

### Playing with the DualSense

Every button shows its meaning in a hint bar at the bottom of the screen,
which changes with what is on screen.

| Button | On the map | In a menu | In a prompt |
|---|---|---|---|
| D-pad / left stick | Move (the stick does diagonals; hold to walk) | Move the highlight | Choose a button |
| ✕ Cross | Actions for where you stand | Select the highlighted entry | Confirm |
| ○ Circle | Cancel | Back | Cancel |
| □ Square | Actions for where you stand | Toggle the entry (pick several) | Everything (`*`) |
| △ Triangle | Inventory (pick an item for what you can do with it) | Done | List the choices |
| Options | Command menu, every command described | | |
| Create | Save | | |
| L1 / R1 | Go up / down stairs | Previous / next page | |
| L2 / R2 | Search / rest | | |
| Touchpad | Look here | | |
| L3 / R3 | Look at something / help | | |

Questions with a few answers ("Really save?", "Old game in progress…")
appear as a row of buttons. You play under your PSN name. Saves go to the
title's own storage. If you close the game from the home screen without
saving, NetHack offers to recover it next time.

### Build and install

On Linux or WSL with `clang-18 lld-18 llvm-18 libclang-rt-18-dev cmake
ninja-build`, after setting up the same toolchain as
`.github/workflows/ps5-native.yml` (ps5-native-app-boilerplate, ps5-opengl
and its SDK and SDL2, mihawk-99's platform layer):

```sh
cd sys/ps5
./setup.sh --native               # PDCursesMod and NetHack's data files
make -f Makefile.native           # -> nethack-native.objs
bash package-native.sh --build    # -> build-ps5/app/dist/PPSA92042{,.zip}
```

Copy the `PPSA92042` folder to `/data/homebrew/` on the PS5 (for example
over FTP), and ShadowMountPlus installs it. Tagging the ps5 branch with the
`contentVersion` from `/sce_sys/param.json` (for example `01.000.000`)
publishes the zip as a release for the catalog.

### Porting notes: what a native title may and may not do

- **No exit.** `exit()` raises SIGSYS. `ps5main.c` wraps it and shows a
  "close it from the home screen" screen instead.
- **Missing or refused C library functions.** These are supplied by
  [mihawk-99's platform layer](https://github.com/mihawk-99/PS5_PayloadSDK)
  (`getcwd`, `opendir`, `access`, `mkstemp`, `isatty`, `fork` and others) or
  by `native_shims.c` (`lstat`, `chdir`, `getpwnam`, `setgid`,
  `cfgetospeed`, `fpathconf`, `tmpnam`). `package-native.sh` binds the
  standard names with `--defsym`, because a title must not define libc's own
  names. It doesn't bind `sysconf` or `pthread_exit`: the platform's
  versions call the originals.
- **No chdir.** NetHack's data sits at the title root, `/app0`.
  ps5-opengl's runtime resolves relative `fopen()` paths there, and
  `native_shims.c` does the same for `open()`. Everything written goes to
  `/download0/nethack` (`VAR_PLAYGROUND`).
- **One OpenGL window, no window surface, no keyboard.** PDCurses draws into
  an off-screen surface that `ps5main.c` presents with OpenGL. It wraps
  `SDL_CreateWindow`, `SDL_GetWindowSurface` and
  `SDL_UpdateWindowSurface[Rects]`.
- **No fork.** Saves are compressed with zlib (`ZLIB_COMP`) rather than by
  running `compress`. Recovery happens in the game (`SELF_RECOVER`).
- **Locks.** There's no `link()`, and no way to tell whether a lock's
  process is alive. That's why the port uses `NO_FILE_LINKS` and
  `MAXPLAYERS=0` (one game per player name).
- **Debugging.** Building with `-DPS5_SCREENSHOT` writes each frame to
  `/data/nethack-screen.bmp` and types the contents of
  `/data/nethack-input.txt`, so the game can be driven and watched over FTP.
  The log is `/data/nethack-ps5.log`.

## The ELF payload

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

Prebuilt zips are on the [Releases](../../releases) page. `nightly` tracks
the `ps5` branch, and `v*` tags are stable releases. Both are built by
`.github/workflows/ps5-release.yml`.

1. Unzip the release (or use `pkg/NetHack/` from your own build) and copy the
   `NetHack` folder to `/data/NetHack/` on the PS5 over FTP.
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
