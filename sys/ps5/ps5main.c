/* ps5main.c - PS5 entry point and DualSense input glue for NetHack.
 *
 * SDL's PS5 main() calls SDL_main().  We prepare the playground directory,
 * install an SDL event filter that turns controller input into key events
 * PDCurses understands, then hand off to NetHack's unixmain (renamed
 * nethack_main at compile time).
 */
#include <SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* PDCursesMod's built-in 8x16 CP437 font (1bpp BMP), defined in pdcscrn.c */
extern unsigned char font437[4158];

extern int nethack_main(int argc, char **argv);
extern SDL_Surface *pdc_font; /* PDCurses uses this if set before initscr */

#define DEFAULT_HACKDIR "/data/NetHack"

/* DualSense button indices from SDL's PS5 joystick driver. */
enum {
    B_CROSS = 0, B_CIRCLE, B_SQUARE, B_TRIANGLE, B_TOUCHPAD, B_SHARE,
    B_OPTIONS, B_L3, B_R3, B_L1, B_R1, B_UP, B_DOWN, B_LEFT, B_RIGHT,
    B_L2, B_R2, B_COUNT
};

/* What each button sends.  A single printable char is sent as text;
 * "\r" and "\x1b" are sent as Return / Escape key presses. */
static const char *button_map[B_COUNT] = {
    [B_CROSS] = "\r",    /* confirm / continue --More-- */
    [B_CIRCLE] = "\x1b", /* cancel */
    [B_SQUARE] = ",",    /* pick up */
    [B_TRIANGLE] = "i",  /* inventory */
    [B_TOUCHPAD] = ":",  /* look here */
    [B_SHARE] = "?",     /* help */
    [B_OPTIONS] = "#",   /* extended command */
    [B_L3] = ";",        /* farlook */
    [B_R3] = "x",        /* swap weapons */
    [B_L1] = "<",        /* go up */
    [B_R1] = ">",        /* go down */
    [B_UP] = "k",
    [B_DOWN] = "j",
    [B_LEFT] = "h",
    [B_RIGHT] = "l",
    [B_L2] = "s",        /* search */
    [B_R2] = ".",        /* rest */
};

#define STICK_DEADZONE 16000
#define REPEAT_DELAY_MS 350
#define REPEAT_RATE_MS 110

static SDL_mutex *rep_lock;
static const char *rep_key;     /* key currently auto-repeating, or NULL */
static Uint32 rep_next;         /* SDL_GetTicks() of next repeat */
static int stick_x, stick_y;
static const char *stick_key;

static void push_key(const char *k)
{
    SDL_Event e;

    memset(&e, 0, sizeof e);
    if (k[0] == '\r' || k[0] == '\x1b') {
        SDL_Keycode sym = (k[0] == '\r') ? SDLK_RETURN : SDLK_ESCAPE;

        e.type = SDL_KEYDOWN;
        e.key.state = SDL_PRESSED;
        e.key.keysym.sym = sym;
        e.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
        SDL_PushEvent(&e);
        e.type = SDL_KEYUP;
        e.key.state = SDL_RELEASED;
        SDL_PushEvent(&e);
    } else {
        e.type = SDL_TEXTINPUT;
        SDL_strlcpy(e.text.text, k, sizeof e.text.text);
        SDL_PushEvent(&e);
    }
}

static int is_movement(const char *k)
{
    return k && k[0] && k[1] == '\0' && strchr("hjklyubn", k[0]) != NULL;
}

static void start_repeat(const char *k)
{
    SDL_LockMutex(rep_lock);
    rep_key = is_movement(k) ? k : NULL;
    rep_next = SDL_GetTicks() + REPEAT_DELAY_MS;
    SDL_UnlockMutex(rep_lock);
}

static void stop_repeat(const char *k)
{
    SDL_LockMutex(rep_lock);
    if (rep_key == k)
        rep_key = NULL;
    SDL_UnlockMutex(rep_lock);
}

/* Timer thread: re-sends a held movement key so you can walk by holding. */
static Uint32 repeat_tick(Uint32 interval, void *unused)
{
    (void) unused;
    SDL_LockMutex(rep_lock);
    if (rep_key && SDL_TICKS_PASSED(SDL_GetTicks(), rep_next)) {
        push_key(rep_key);
        rep_next = SDL_GetTicks() + REPEAT_RATE_MS;
    }
    SDL_UnlockMutex(rep_lock);
    return interval;
}

static const char *stick_direction(int x, int y)
{
    int dx = (x > STICK_DEADZONE) - (x < -STICK_DEADZONE);
    int dy = (y > STICK_DEADZONE) - (y < -STICK_DEADZONE);
    static const char *dirs[3][3] = {
        /* dy=-1   dy=0   dy=1 */
        { "y", "h", "b" }, /* dx=-1 */
        { "k", NULL, "j" }, /* dx=0 */
        { "u", "l", "n" }, /* dx=1 */
    };

    return dirs[dx + 1][dy + 1];
}

static int SDLCALL input_filter(void *unused, SDL_Event *e)
{
    (void) unused;
    switch (e->type) {
    case SDL_JOYDEVICEADDED:
        SDL_JoystickOpen(e->jdevice.which);
        return 0;
    case SDL_JOYBUTTONDOWN:
        if (e->jbutton.button < B_COUNT && button_map[e->jbutton.button]) {
            push_key(button_map[e->jbutton.button]);
            start_repeat(button_map[e->jbutton.button]);
        }
        return 0;
    case SDL_JOYBUTTONUP:
        if (e->jbutton.button < B_COUNT && button_map[e->jbutton.button])
            stop_repeat(button_map[e->jbutton.button]);
        return 0;
    case SDL_JOYAXISMOTION: {
        const char *k;

        if (e->jaxis.axis == 0)
            stick_x = e->jaxis.value;
        else if (e->jaxis.axis == 1)
            stick_y = e->jaxis.value;
        else
            return 0;
        k = stick_direction(stick_x, stick_y);
        if (k != stick_key) {
            if (stick_key)
                stop_repeat(stick_key);
            stick_key = k;
            if (k) {
                push_key(k);
                start_repeat(k);
            }
        }
        return 0;
    }
    case SDL_JOYHATMOTION:
    case SDL_JOYBALLMOTION:
    case SDL_JOYDEVICEREMOVED:
        return 0; /* d-pad arrives as buttons too; ignore the hat */
    default:
        return 1;
    }
}

static void ensure_file(const char *name)
{
    FILE *fp = fopen(name, "a");

    if (fp)
        fclose(fp);
}

static void setup_hackdir(const char *argv0)
{
    char dir[1024];
    char *slash;

    /* Run from the directory that holds the .elf (and NetHack's data). */
    if (argv0 && argv0[0] == '/' && strlen(argv0) < sizeof dir) {
        strcpy(dir, argv0);
        slash = strrchr(dir, '/');
        if (slash && slash != dir)
            *slash = '\0';
        else
            strcpy(dir, DEFAULT_HACKDIR);
    } else {
        strcpy(dir, DEFAULT_HACKDIR);
    }

    mkdir(dir, 0755);
    if (chdir(dir) != 0)
        fprintf(stderr, "nethack: chdir(%s): %s\n", dir, strerror(errno));
    mkdir("save", 0755);
    ensure_file("perm");
    ensure_file("record");
    ensure_file("logfile");
    ensure_file("xlogfile");
    ensure_file("livelog");

    setenv("NETHACKDIR", dir, 1);
    setenv("HOME", dir, 1); /* .nethackrc lives next to the game */
    if (!getenv("USER"))
        setenv("USER", "player", 1);
}

/* Preload the built-in font scaled up by an integer factor so text is
 * readable on a TV.  Keeps a 2-entry palette so PDCurses colours it the
 * same way it colours the original 1bpp font. */
static void preload_scaled_font(int factor)
{
    SDL_Surface *src, *dst;
    SDL_Palette *pal;
    int x, y, i, j;

    src = SDL_LoadBMP_RW(SDL_RWFromMem(font437, sizeof font437), 1);
    if (!src || !src->format->palette)
        return;
    if (factor <= 1) {
        pdc_font = src;
        return;
    }
    dst = SDL_CreateRGBSurfaceWithFormat(0, src->w * factor, src->h * factor,
                                         8, SDL_PIXELFORMAT_INDEX8);
    pal = SDL_AllocPalette(2);
    if (!dst || !pal) {
        pdc_font = src;
        return;
    }
    SDL_SetPaletteColors(pal, src->format->palette->colors, 0, 2);
    SDL_SetSurfacePalette(dst, pal);
    SDL_FreePalette(pal);

    SDL_LockSurface(src);
    SDL_LockSurface(dst);
    for (y = 0; y < src->h; y++) {
        const Uint8 *row = (const Uint8 *) src->pixels + y * src->pitch;

        for (x = 0; x < src->w; x++) {
            Uint8 bit = (row[x >> 3] >> (7 - (x & 7))) & 1;

            for (j = 0; j < factor; j++) {
                Uint8 *out = (Uint8 *) dst->pixels
                             + (y * factor + j) * dst->pitch + x * factor;

                for (i = 0; i < factor; i++)
                    out[i] = bit;
            }
        }
    }
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
    SDL_FreeSurface(src);
    pdc_font = dst;
}

int SDL_main(int argc, char **argv)
{
    int i;
    static char *nh_argv[8];
    int nh_argc = 0;

    setup_hackdir(argc > 0 ? argv[0] : NULL);

    /* 2x font = 16x32 cells: 120x33 fills a 1920x1080 screen. */
    setenv("PDC_COLS", "120", 0);
    setenv("PDC_LINES", "33", 0);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) != 0)
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    preload_scaled_font(2);
    rep_lock = SDL_CreateMutex();
    SDL_SetEventFilter(input_filter, NULL);
    for (i = 0; i < SDL_NumJoysticks(); i++)
        SDL_JoystickOpen(i);
    SDL_AddTimer(30, repeat_tick, NULL);

    nh_argv[nh_argc++] = (argc > 0) ? argv[0] : "nethack";
    for (i = 1; i < argc && nh_argc < 7; i++)
        nh_argv[nh_argc++] = argv[i];
    nh_argv[nh_argc] = NULL;

    return nethack_main(nh_argc, nh_argv);
}
