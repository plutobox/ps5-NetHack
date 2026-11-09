# NetHack for PS5

A port of [NetHack](https://www.nethack.org/) 5.0 to jailbroken PS5
consoles. It runs as a native title (`PPSA92042`), shows on the TV and is
played with the DualSense. You don't need a keyboard.

This is the `ps5` branch of a NetHack fork. The upstream NetHack readme is
[`README`](../README) at the root, and the PS5 code lives in
[`sys/ps5/`](../sys/ps5).

## Install

- **From the homebrew catalog:** install NetHack from a store that reads the
  PS5 homebrew catalog, such as ProsperoStore.
- **By hand:** download `PPSA92042.zip` from [Releases](../../releases),
  unzip it, and copy the `PPSA92042` folder to `/data/homebrew/` over FTP.
  ShadowMountPlus then installs it on the home screen.

You play under your PSN name. Saves are kept in the title's own storage
(`/download0/nethack`).

## Controls

A hint bar at the bottom of the screen always shows what each button does
right now.

| Button | On the map | In a menu | Picking a spot | Choosing a direction |
|---|---|---|---|---|
| D-pad / left stick | Move (the stick does diagonals; hold to walk) | Move the highlight | Move the cursor | The direction |
| ✕ Cross | Actions for where you stand | Select | Select | Here (yourself) |
| ○ Circle | Cancel | Back | Cancel | Cancel |
| □ Square | Actions for where you stand | Toggle (pick several) | Select, briefly | |
| △ Triangle | Inventory: pick an item to see what you can do with it | Done | Back to yourself | |
| Options | Command menu, with every command described | | | |
| Create | Save | | | |
| L1 / R1 | Go up / down stairs | Previous / next page | Previous / next thing of interest | Up / down |
| L2 / R2 | Search / rest | Fewer / more of a count ("drop 5 arrows"); hold to count fast | Previous / next monster | |
| Touchpad | Look here | | Select and describe in full | |
| L3 / R3 | Look at something / help | | Select once / help | |

- **Yes/no questions** appear as a row of buttons. Left and right choose,
  Cross confirms, Circle cancels.
- **"What do you want to eat?"** and similar questions open a menu of the
  likely items, with "list everything" at the end.
- **`>>` (more messages):** Cross continues, and Circle skips the rest of
  this turn's messages. The d-pad does nothing there, so you can't walk past
  a message unread.
- **Typing text** (names, engravings, wishes) uses an on-screen keyboard.
  The d-pad picks a key, Cross types it, Square deletes, Triangle adds a
  space, Options finishes and Circle cancels.

## Saving and quitting

- **Save:** press Create, or choose Save in the command menu. NetHack then
  shows a closing screen. Press the PS button and close NetHack to leave.
- **Checkpoints:** the game saves a checkpoint whenever it has waited ten
  seconds for a command. If you close it without saving, NetHack offers to
  recover that game the next time it starts.

## What's missing

- **The system keyboard.** A title can load the PS5's keyboard module but
  can't call it (`sceKernelDlsym` finds none of its functions), so text is
  typed on NetHack's own on-screen keyboard instead.
- **USB keyboards.** The native title can't read them, so every command goes
  through the controller.
- **Graphics.** There are no tiles: the map uses NetHack's curses interface
  with DEC line-drawing symbols and colour.
- **More than one game per name.** The console can't tell whether a game's
  lock is still in use, so you can have one game in progress per PSN name.
- **Leaving from the menu.** A PS5 title may not exit by itself. After
  saving or dying, close NetHack from the home screen.
- **The ELF payload build** (`nethack.elf`, for the homebrew launcher or
  `hbldr`) compiles and links, but it hasn't been tested on a console.

## Build and release

[`sys/ps5/README.md`](../sys/ps5/README.md) covers building both versions,
releasing a new version through the **PS5 native title** workflow, and the
porting notes.

## License

NetHack is distributed under the [NetHack General Public License](../dat/license).
The PS5 port uses SDL2 (zlib), PDCursesMod (public domain) and Lua (MIT).
