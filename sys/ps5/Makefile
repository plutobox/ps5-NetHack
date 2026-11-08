# Cross-compile NetHack (curses interface on PDCursesMod/SDL2) for jailbroken PS5.
#   make           -> nethack.elf
#   make package   -> pkg/NetHack/  (copy this folder to /data/NetHack on the PS5)
# Run ./setup.sh once first; it fetches the SDK, builds SDL2 and the host tools.

PS5DIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
NH      := $(abspath $(PS5DIR)/../..)
DEPS    := $(PS5DIR)/deps
SDK     ?= $(DEPS)/ps5-payload-sdk
NHHOST  ?= $(DEPS)/nethack-host
include $(SDK)/toolchain/prospero.mk
PDC     := $(NH)/submodules/pdcursesmod
LUA     := $(NH)/nhlua/lua/src
HB      := $(SDK)/target/user/homebrew

OPT     := -O2 -g
NHDEFS  := -DNOTTYGRAPHICS -DCURSES_GRAPHICS -DDEFAULT_WINDOW_SYS=\"curses\" \
           -DPDCURSES -DNOMAIL -DNOSUSPEND -DNOSHELL -DNOPANICTRACE
NHINC   := -Igen -I$(NH)/include -I$(LUA) -I$(PDC)
SDLINC  := -I$(HB)/include/SDL2 -D_THREAD_SAFE

# ---- NetHack core / system / window port ---------------------------------
NHSRC_SRC := allmain alloc apply artifact attrib ball bones botl calendar \
  cfgfiles cmd coloratt dbridge decl detect dig display dlb do do_name do_wear \
  dog dogmove dokick dothrow drawing dungeon earlyarg eat end engrave exper \
  explode extralev files fountain getpos glyphs hack hacklib iactions insight \
  invent isaac64 light lock mail makemon mcastu mdlib mhitm mhitu minion mklev \
  mkmap mkmaze mkobj mkroom mon mondata monmove monst mplayer mthrowu muse \
  music nhlua nhlsel nhlobj nhmd4 objects o_init objnam options pager pickup \
  pline polyself potion pray priest quest questpgr read rect region report \
  restore revision rip rnd role rumors save selvar sfbase sfstruct shk shknam \
  sit sounds sp_lev spell stairs symbols sys steal steed strutil teleport \
  timeout topten track trap u_init uhitm utf8map vault vision weapon were \
  wield windows wizard wizcmds worm worn write zap version date
NHSRC_SHARE := posixregex ioctl unixtty
NHSRC_UNIX  := unixmain unixunix unixres
NHSRC_CURS  := cursmain curswins cursmisc cursdial cursstat cursinit cursmesg \
  cursinvt

LUASRC := lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject \
  lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib \
  lcorolib ldblib liolib lmathlib loadlib loslib lstrlib ltablib lutf8lib linit

PDCSRC := addch addchstr addstr attr beep bkgd border clear color debug delch \
  deleteln getch getstr getyx inch inchstr initscr inopts insch insstr instr \
  kernel keyname mouse move outopts overlay pad panel printw refresh scanw \
  scr_dump scroll slk termattr terminfo touch util window
PDCSDLSRC := pdcclip pdcdisp pdcgetsc pdckbd pdcscrn pdcsetsc pdcutil

OBJS := $(addprefix obj/nh/,$(addsuffix .o,$(NHSRC_SRC) $(NHSRC_SHARE) \
          $(NHSRC_UNIX) $(NHSRC_CURS))) \
        $(addprefix obj/lua/,$(addsuffix .o,$(LUASRC))) \
        $(addprefix obj/pdc/,$(addsuffix .o,$(PDCSRC) $(PDCSDLSRC))) \
        obj/ps5main.o

LIBS := -L$(HB)/lib -lSDL2main -lSDL2 -lSceSystemService -lSceUserService \
        -lScePad -lSceAudioOut -lSceVideoOut -lSceKeyboard \
        -lSceImeDialog -lkernel_sys

all: nethack.elf

nethack.elf: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

gen/date.h: $(NHHOST)/include/date.h
	mkdir -p gen && cp $< $@

obj/nh/%.o: $(NH)/src/%.c gen/date.h
	@mkdir -p $(@D)
	$(CC) $(OPT) $(NHDEFS) $(NHINC) -c -o $@ $<
obj/nh/%.o: $(NH)/sys/share/%.c gen/date.h
	@mkdir -p $(@D)
	$(CC) $(OPT) $(NHDEFS) $(NHINC) -c -o $@ $<
obj/nh/unixmain.o: $(NH)/sys/unix/unixmain.c gen/date.h
	@mkdir -p $(@D)
	$(CC) $(OPT) $(NHDEFS) $(NHINC) -Dmain=nethack_main -c -o $@ $<
obj/nh/%.o: $(NH)/sys/unix/%.c gen/date.h
	@mkdir -p $(@D)
	$(CC) $(OPT) $(NHDEFS) $(NHINC) -c -o $@ $<
obj/nh/%.o: $(NH)/win/curses/%.c gen/date.h
	@mkdir -p $(@D)
	$(CC) $(OPT) $(NHDEFS) $(NHINC) -c -o $@ $<

obj/lua/%.o: $(LUA)/%.c
	@mkdir -p $(@D)
	$(CC) $(OPT) -w -c -o $@ $<

obj/pdc/%.o: $(PDC)/pdcurses/%.c
	@mkdir -p $(@D)
	$(CC) $(OPT) -w -I$(PDC) -I$(PDC)/sdl2 $(SDLINC) -c -o $@ $<
obj/pdc/%.o: $(PDC)/sdl2/%.c
	@mkdir -p $(@D)
	$(CC) $(OPT) -w -I$(PDC) -I$(PDC)/sdl2 -I$(PDC)/common $(SDLINC) -c -o $@ $<

obj/ps5main.o: ps5main.c
	@mkdir -p $(@D)
	$(CC) $(OPT) -Wall -I$(PDC)/common $(SDLINC) -c -o $@ $<

# ---- Package: everything NetHack needs at runtime, in one folder ----------
DATFILES := bogusmon data engrave epitaph oracles options rumors \
  help hh cmdhelp keyhelp history opthelp optmenu usagehlp wizhelp tribute \
  license symbols

package: nethack.elf
	rm -rf pkg/NetHack
	mkdir -p pkg/NetHack/sce_sys
	cp nethack.elf pkg/NetHack/
	cd $(NHHOST)/dat && cp $(DATFILES) *.lua $(CURDIR)/pkg/NetHack/
	cp $(NH)/sys/unix/sysconf pkg/NetHack/sysconf
	cp homebrew.js pkg/NetHack/
	python3 mkicon.py $(PDC)/common/font437.h pkg/NetHack/sce_sys/icon0.png
	cp nethackrc pkg/NetHack/.nethackrc
	@echo "Package ready in pkg/NetHack"

clean:
	rm -rf obj gen nethack.elf pkg

distclean: clean
	rm -rf deps

.PHONY: all package clean
