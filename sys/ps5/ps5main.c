/* ps5main.c - PS5 entry point and DualSense input glue for NetHack.
 *
 * Two builds share the input glue:
 *
 * - The payload (nethack.elf): SDL's PS5 main() calls SDL_main().  We
 *   prepare the playground directory, install an SDL event filter that turns
 *   controller input into key events PDCurses understands, then hand off to
 *   NetHack's unixmain (renamed nethack_main at compile time).
 *
 * - The native title (PS5_NATIVE, eboot.bin): ps5-opengl's SDL2 has only an
 *   OpenGL window, no window surface.  We give PDCurses an off-screen
 *   surface and present it with OpenGL in place of SDL_UpdateWindowSurface.
 *   The game data is read-only at the title root, /app0; saves and scores go to
 *   /download0/nethack (NetHack's VAR_PLAYGROUND).  A title may not exit, so
 *   exit() waits for the player to close the title from the home screen.
 */
#include <SDL.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef PS5_NATIVE
#include <GL/glcorearb.h>
#endif

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

/* What each button sends, as an SDL keycode: printable characters go to
 * PDCurses as text, everything else (Return, Escape, arrows, function keys)
 * as key presses. */
#ifdef PS5_CONTROLLER
/* The native title: the d-pad is the arrow keys and every other button a
 * function key, so NetHack's curses code (win/curses, PS5_CONTROLLER) can
 * give each its meaning for what is on screen: a highlight bar in menus,
 * buttons in prompts, commands on the map.  Circle is Escape. */
static const SDL_Keycode button_map[B_COUNT] = {
    [B_CROSS] = SDLK_F1,    /* confirm, select; on the map: actions here */
    [B_CIRCLE] = SDLK_ESCAPE, /* cancel, back */
    [B_SQUARE] = SDLK_F2,   /* actions here; in menus: toggle */
    [B_TRIANGLE] = SDLK_F3, /* inventory; in menus: done */
    [B_OPTIONS] = SDLK_F4,  /* command menu */
    [B_SHARE] = SDLK_F5,    /* Create: save */
    [B_TOUCHPAD] = SDLK_F6, /* look here */
    [B_L1] = SDLK_F7,       /* go up; in menus: previous page */
    [B_R1] = SDLK_F8,       /* go down; in menus: next page */
    [B_L2] = SDLK_F9,       /* search */
    [B_R2] = SDLK_F10,      /* rest */
    [B_L3] = SDLK_F11,      /* look at something */
    [B_R3] = SDLK_F12,      /* help */
    [B_UP] = SDLK_UP,
    [B_DOWN] = SDLK_DOWN,
    [B_LEFT] = SDLK_LEFT,
    [B_RIGHT] = SDLK_RIGHT,
};
/* The left stick: the arrows, and the keypad's corners for diagonals. */
static const SDL_Keycode stick_keys[3][3] = {
    /* dy=-1      dy=0         dy=1 */
    { SDLK_KP_7, SDLK_LEFT, SDLK_KP_1 },  /* dx=-1 */
    { SDLK_UP, 0, SDLK_DOWN },            /* dx=0 */
    { SDLK_KP_9, SDLK_RIGHT, SDLK_KP_3 }, /* dx=1 */
};
#else
static const SDL_Keycode button_map[B_COUNT] = {
    [B_CROSS] = SDLK_RETURN,  /* confirm / continue --More-- */
    [B_CIRCLE] = SDLK_ESCAPE, /* cancel */
    [B_SQUARE] = ',',    /* pick up */
    [B_TRIANGLE] = 'i',  /* inventory */
    [B_TOUCHPAD] = ':',  /* look here */
    [B_SHARE] = '?',     /* help */
    [B_OPTIONS] = '#',   /* extended command */
    [B_L3] = ';',        /* farlook */
    [B_R3] = 'x',        /* swap weapons */
    [B_L1] = '<',        /* go up */
    [B_R1] = '>',        /* go down */
    [B_UP] = 'k',
    [B_DOWN] = 'j',
    [B_LEFT] = 'h',
    [B_RIGHT] = 'l',
    [B_L2] = 's',        /* search */
    [B_R2] = '.',        /* rest */
};
static const SDL_Keycode stick_keys[3][3] = {
    /* dy=-1   dy=0   dy=1 */
    { 'y', 'h', 'b' }, /* dx=-1 */
    { 'k', 0, 'j' },   /* dx=0 */
    { 'u', 'l', 'n' }, /* dx=1 */
};
#endif

#define STICK_DEADZONE 16000
#define REPEAT_DELAY_MS 350
#define REPEAT_RATE_MS 110

static SDL_mutex *rep_lock;
static SDL_Keycode rep_key;     /* key currently auto-repeating, or 0 */
static Uint32 rep_next;         /* SDL_GetTicks() of next repeat */
static int stick_x, stick_y;
static SDL_Keycode stick_key;

static void push_key(SDL_Keycode sym)
{
    SDL_Event e;

    memset(&e, 0, sizeof e);
    if (sym >= ' ' && sym < 127) {
        e.type = SDL_TEXTINPUT;
        e.text.text[0] = (char) sym;
        SDL_PushEvent(&e);
    } else {
        e.type = SDL_KEYDOWN;
        e.key.state = SDL_PRESSED;
        e.key.keysym.sym = sym;
        e.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
        SDL_PushEvent(&e);
        e.type = SDL_KEYUP;
        e.key.state = SDL_RELEASED;
        SDL_PushEvent(&e);
    }
}

#ifdef PS5_CONTROLLER
/* What the controller is driving (win/curses/cursmisc.c); the values are
   PS5_DIALOG_ and PS5_CONTEXT_ in win/curses/cursdial.h. */
int ps5_curses_context(void);
#define CONTEXT_MENU 1      /* a menu, pick any */
#define CONTEXT_PICK 8      /* a menu, pick one */
#endif

/* Held, these repeat: walking, scrolling through menus, and in menus
 * (only: on the map they search and rest) L2 and R2 setting a count. */
static int is_movement(SDL_Keycode k)
{
    switch (k) {
    case 'h': case 'j': case 'k': case 'l':
    case 'y': case 'u': case 'b': case 'n':
    case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT:
    case SDLK_KP_7: case SDLK_KP_9: case SDLK_KP_1: case SDLK_KP_3:
        return 1;
#ifdef PS5_CONTROLLER
    case SDLK_F9: case SDLK_F10: {
        int context = ps5_curses_context();

        return context == CONTEXT_MENU || context == CONTEXT_PICK;
    }
#endif
    default:
        return 0;
    }
}

/* Stop any repeat, as when the controller goes away with a button held:
 * otherwise the hero would walk on forever. */
static void cancel_repeat(void)
{
    SDL_LockMutex(rep_lock);
    rep_key = 0;
    SDL_UnlockMutex(rep_lock);
    stick_x = stick_y = 0;
    stick_key = 0;
}

static void start_repeat(SDL_Keycode k)
{
    SDL_LockMutex(rep_lock);
    rep_key = is_movement(k) ? k : 0;
    rep_next = SDL_GetTicks() + REPEAT_DELAY_MS;
    SDL_UnlockMutex(rep_lock);
}

static void stop_repeat(SDL_Keycode k)
{
    SDL_LockMutex(rep_lock);
    if (rep_key == k)
        rep_key = 0;
    SDL_UnlockMutex(rep_lock);
}

/* Re-sends a held movement key so you can walk by holding; 1 if it did. */
static int repeat_due(void)
{
    int sent = 0;

    SDL_LockMutex(rep_lock);
    if (rep_key && SDL_TICKS_PASSED(SDL_GetTicks(), rep_next)) {
        push_key(rep_key);
        rep_next = SDL_GetTicks() + REPEAT_RATE_MS;
        sent = 1;
    }
    SDL_UnlockMutex(rep_lock);
    return sent;
}

#ifdef PS5_CONTROLLER
/* The native title repeats from PDCurses' own polling (SDL_PollEvent is
 * wrapped at link time): only after the controller's pending events have
 * been read, so a released button is never taken for a held one.  A timer
 * thread would repeat whenever a slow frame kept the release unread, and a
 * single press on the d-pad moved twice. */
static void refresh_hints(void);

int __real_SDL_PollEvent(SDL_Event *e);
int __wrap_SDL_PollEvent(SDL_Event *e)
{
    if (__real_SDL_PollEvent(e))
        return 1;
    if (repeat_due())
        return __real_SDL_PollEvent(e);
    /* waiting for a key: the screen was last drawn mid-turn, perhaps for
     * another context than the one now asking */
    refresh_hints();
    return 0;
}
#else
/* Timer thread for the payload. */
static Uint32 repeat_tick(Uint32 interval, void *unused)
{
    (void) unused;
    repeat_due();
    return interval;
}
#endif

static SDL_Keycode stick_direction(int x, int y)
{
    int dx = (x > STICK_DEADZONE) - (x < -STICK_DEADZONE);
    int dy = (y > STICK_DEADZONE) - (y < -STICK_DEADZONE);

    return stick_keys[dx + 1][dy + 1];
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
        SDL_Keycode k;

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
    case SDL_JOYDEVICEREMOVED:
        /* switched off or out of charge: let go of what it was holding.
         * (Not closed here: the filter can run while SDL is still walking
         * its list of joysticks.) */
        cancel_repeat();
        return 0;
    case SDL_JOYHATMOTION:
    case SDL_JOYBALLMOTION:
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

#ifndef PS5_NATIVE
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
#endif

/* The built-in font scaled up by an integer factor so text is readable on a
 * TV.  Keeps a 2-entry palette so PDCurses colours it the same way it
 * colours the original 1bpp font. */
static SDL_Surface *scaled_font(int factor)
{
    SDL_Surface *src, *dst;
    SDL_Palette *pal;
    int x, y, i, j;

    src = SDL_LoadBMP_RW(SDL_RWFromMem(font437, sizeof font437), 1);
    if (!src || !src->format->palette)
        return NULL;
    if (factor <= 1)
        return src;
    dst = SDL_CreateRGBSurfaceWithFormat(0, src->w * factor, src->h * factor,
                                         8, SDL_PIXELFORMAT_INDEX8);
    pal = SDL_AllocPalette(2);
    if (!dst || !pal)
        return src;
    SDL_SetPaletteColors(pal, src->format->palette->colors, 0, 2);
    SDL_SetSurfacePalette(dst, pal);
    SDL_FreePalette(pal);

    SDL_LockSurface(src);
    SDL_LockSurface(dst);
    for (y = 0; y < src->h; y++) {
        const Uint8 *row = (const Uint8 *) src->pixels + y * src->pitch;

        for (x = 0; x < src->w; x++) {
            /* SDL 2.30 expands the 1bpp BMP to a byte per pixel on load;
             * older SDL keeps it packed, eight pixels to a byte. */
            Uint8 bit = src->format->BitsPerPixel == 1
                            ? (row[x >> 3] >> (7 - (x & 7))) & 1
                            : row[x] & 1;

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
    return dst;
}

#ifdef PS5_NATIVE

/* ---- Native title: an OpenGL presenter for PDCurses's surface ---------- */

SDL_Window *__real_SDL_CreateWindow(const char *title, int x, int y, int w,
                                    int h, Uint32 flags);

int sceKernelUsleep(unsigned int microseconds);
int sceUserServiceInitialize(void *params);
int sceUserServiceGetForegroundUser(int *user_id);
int sceUserServiceGetUserName(int user_id, char *name, size_t size);

/* The PS5 user's name, for NetHack's -u: a controller cannot answer the
 * "Who are you?" prompt, and ps5-opengl's SDL2 has no keyboard support. */
static const char *player_name(void)
{
    static char name[32];
    int user = -1;
    char *c;

    sceUserServiceInitialize(NULL); /* fails harmlessly if already done */
    if (sceUserServiceGetForegroundUser(&user) != 0
        || sceUserServiceGetUserName(user, name, sizeof name) != 0
        || !name[0])
        return "player";
    for (c = name; *c; c++)
        if (*c == ' ' || *c == '/')
            *c = '_';
    return name;
}

#define PLAYGROUND "/download0/nethack"

static SDL_Window *window;
static SDL_Surface *screen;     /* what PDCurses draws into */
static GLuint texture, program, vertex_array;

static PFNGLGENTEXTURESPROC gl_GenTextures;
static PFNGLBINDTEXTUREPROC gl_BindTexture;
static PFNGLTEXPARAMETERIPROC gl_TexParameteri;
static PFNGLTEXIMAGE2DPROC gl_TexImage2D;
static PFNGLTEXSUBIMAGE2DPROC gl_TexSubImage2D;
static PFNGLPIXELSTOREIPROC gl_PixelStorei;
static PFNGLCREATESHADERPROC gl_CreateShader;
static PFNGLSHADERSOURCEPROC gl_ShaderSource;
static PFNGLCOMPILESHADERPROC gl_CompileShader;
static PFNGLGETSHADERIVPROC gl_GetShaderiv;
static PFNGLGETSHADERINFOLOGPROC gl_GetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC gl_CreateProgram;
static PFNGLATTACHSHADERPROC gl_AttachShader;
static PFNGLLINKPROGRAMPROC gl_LinkProgram;
static PFNGLGETPROGRAMIVPROC gl_GetProgramiv;
static PFNGLUSEPROGRAMPROC gl_UseProgram;
static PFNGLGETUNIFORMLOCATIONPROC gl_GetUniformLocation;
static PFNGLUNIFORM1IPROC gl_Uniform1i;
static PFNGLGENVERTEXARRAYSPROC gl_GenVertexArrays;
static PFNGLBINDVERTEXARRAYPROC gl_BindVertexArray;
static PFNGLDRAWARRAYSPROC gl_DrawArrays;
static PFNGLVIEWPORTPROC gl_Viewport;
static PFNGLACTIVETEXTUREPROC gl_ActiveTexture;

#define GL_LOAD(var, name) \
    ((var = (void *) SDL_GL_GetProcAddress(name)) != NULL)

/* A triangle covering the screen; row 0 of the surface is the top. */
static const char *vertex_source =
    "#version 330 core\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    uv = vec2(p.x, 1.0 - p.y);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
static const char *fragment_source =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "uniform sampler2D surface;\n"
    "out vec4 colour;\n"
    "void main() { colour = vec4(texture(surface, uv).rgb, 1.0); }\n";

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = gl_CreateShader(type);
    GLint ok = 0;

    gl_ShaderSource(shader, 1, &source, NULL);
    gl_CompileShader(shader);
    gl_GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = "";

        gl_GetShaderInfoLog(shader, sizeof log, NULL, log);
        printf("nethack: shader compile failed: %s\n", log);
    }
    return shader;
}

static int setup_presenter(void)
{
    SDL_DisplayMode mode;
    SDL_GLContext context;
    GLint linked = 0;

    if (SDL_GetDesktopDisplayMode(0, &mode) < 0)
        return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    /* ps5-opengl's driver supports one fixed window the size of the output.
     * PDCurses's own SDL_CreateWindow is wrapped to return it. */
    window = __real_SDL_CreateWindow("NetHack", SDL_WINDOWPOS_UNDEFINED,
                                     SDL_WINDOWPOS_UNDEFINED, mode.w, mode.h,
                                     SDL_WINDOW_OPENGL);
    if (!window || !(context = SDL_GL_CreateContext(window))
        || SDL_GL_MakeCurrent(window, context) < 0)
        return -1;
    SDL_GL_SetSwapInterval(1);

    if (!GL_LOAD(gl_GenTextures, "glGenTextures")
        || !GL_LOAD(gl_BindTexture, "glBindTexture")
        || !GL_LOAD(gl_TexParameteri, "glTexParameteri")
        || !GL_LOAD(gl_TexImage2D, "glTexImage2D")
        || !GL_LOAD(gl_TexSubImage2D, "glTexSubImage2D")
        || !GL_LOAD(gl_PixelStorei, "glPixelStorei")
        || !GL_LOAD(gl_CreateShader, "glCreateShader")
        || !GL_LOAD(gl_ShaderSource, "glShaderSource")
        || !GL_LOAD(gl_CompileShader, "glCompileShader")
        || !GL_LOAD(gl_GetShaderiv, "glGetShaderiv")
        || !GL_LOAD(gl_GetShaderInfoLog, "glGetShaderInfoLog")
        || !GL_LOAD(gl_CreateProgram, "glCreateProgram")
        || !GL_LOAD(gl_AttachShader, "glAttachShader")
        || !GL_LOAD(gl_LinkProgram, "glLinkProgram")
        || !GL_LOAD(gl_GetProgramiv, "glGetProgramiv")
        || !GL_LOAD(gl_UseProgram, "glUseProgram")
        || !GL_LOAD(gl_GetUniformLocation, "glGetUniformLocation")
        || !GL_LOAD(gl_Uniform1i, "glUniform1i")
        || !GL_LOAD(gl_GenVertexArrays, "glGenVertexArrays")
        || !GL_LOAD(gl_BindVertexArray, "glBindVertexArray")
        || !GL_LOAD(gl_DrawArrays, "glDrawArrays")
        || !GL_LOAD(gl_Viewport, "glViewport")
        || !GL_LOAD(gl_ActiveTexture, "glActiveTexture")) {
        SDL_SetError("missing OpenGL function");
        return -1;
    }

    /* Bytes R, G, B, A in memory: uploaded as GL_RGBA without conversion. */
    screen = SDL_CreateRGBSurfaceWithFormat(0, mode.w, mode.h, 32,
                                            SDL_PIXELFORMAT_ABGR8888);
    if (!screen)
        return -1;
    SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 0, 0));

    gl_GenTextures(1, &texture);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, texture);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl_PixelStorei(GL_UNPACK_ROW_LENGTH, screen->pitch / 4);
    gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screen->w, screen->h, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, screen->pixels);

    program = gl_CreateProgram();
    gl_AttachShader(program, compile_shader(GL_VERTEX_SHADER, vertex_source));
    gl_AttachShader(program,
                    compile_shader(GL_FRAGMENT_SHADER, fragment_source));
    gl_LinkProgram(program);
    gl_GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        SDL_SetError("shader program failed to link");
        return -1;
    }
    gl_UseProgram(program);
    gl_Uniform1i(gl_GetUniformLocation(program, "surface"), 0);
    gl_GenVertexArrays(1, &vertex_array);
    gl_BindVertexArray(vertex_array);
    gl_Viewport(0, 0, mode.w, mode.h);
    return 0;
}

static void upload_rect(const SDL_Rect *r)
{
    gl_PixelStorei(GL_UNPACK_SKIP_PIXELS, r->x);
    gl_PixelStorei(GL_UNPACK_SKIP_ROWS, r->y);
    gl_TexSubImage2D(GL_TEXTURE_2D, 0, r->x, r->y, r->w, r->h, GL_RGBA,
                     GL_UNSIGNED_BYTE, screen->pixels);
}

/* ---- The button hint bar, below PDCurses's lines ----------------------- */

#define HINT_BAR_HEIGHT 56

static SDL_Surface *hint_font; /* its own copy: PDCurses recolours its font */
static int hint_top, hint_context = -1;

struct hint {
    unsigned char icon; /* a CP437 glyph, or 0 for the text label alone */
    SDL_Color colour;
    const char *button, *action;
};

/* The DualSense's symbols in CP437, in their PlayStation colours. */
#define CROSS 'X', { 120, 160, 255, 255 }
#define CIRCLE 0x09, { 255, 110, 110, 255 }
#define SQUARE 0xFE, { 240, 130, 210, 255 }
#define TRIANGLE 0x1E, { 70, 220, 180, 255 }
#define OTHER(glyph) glyph, { 200, 200, 210, 255 }
#define END_HINTS { 0, { 0, 0, 0, 0 }, NULL, NULL }

/* One set for each context ps5_curses_context() reports. */
static const struct hint map_hints[] = {
    { CROSS, NULL, "Actions" },     { TRIANGLE, NULL, "Inventory" },
    { OTHER(0xF0), NULL, "Commands" }, { OTHER(0), "L1 R1", "Stairs" },
    { OTHER(0), "L2", "Search" },  { OTHER(0), "R2", "Rest" },
    { OTHER(0x16), NULL, "Look here" }, { OTHER(0), "L3", "Look at" },
    { OTHER(0), "R3", "Help" },    { OTHER(0), "Create", "Save" }, END_HINTS
};
static const struct hint menu_hints[] = { /* pick any number */
    { OTHER(0x12), NULL, "Move" }, { CROSS, NULL, "Select" },
    { SQUARE, NULL, "Toggle" },    { TRIANGLE, NULL, "Done" },
    { OTHER(0), "L2 R2", "Count" }, { OTHER(0), "L1 R1", "Page" },
    { CIRCLE, NULL, "Back" },      END_HINTS
};
static const struct hint button_hints[] = {
    { OTHER(0x1D), NULL, "Choose" }, { CROSS, NULL, "Confirm" },
    { CIRCLE, NULL, "Cancel" }, END_HINTS
};
static const struct hint position_hints[] = {
    { OTHER(0x12), NULL, "Move cursor" }, { CROSS, NULL, "Select" },
    { TRIANGLE, NULL, "Yourself" }, { OTHER(0), "L1 R1", "Things" },
    { OTHER(0), "L2 R2", "Monsters" }, { OTHER(0), "R3", "Help" },
    { CIRCLE, NULL, "Cancel" }, END_HINTS
};
static const struct hint prompt_hints[] = {
    { CROSS, NULL, "Confirm" }, { TRIANGLE, NULL, "List" },
    { SQUARE, NULL, "All" },    { CIRCLE, NULL, "Cancel" }, END_HINTS
};
static const struct hint direction_hints[] = {
    { OTHER(0x12), NULL, "Direction" }, { CROSS, NULL, "Here" },
    { OTHER(0), "L1 R1", "Up / Down" }, { CIRCLE, NULL, "Cancel" }, END_HINTS
};
static const struct hint more_hints[] = {
    { CROSS, NULL, "Continue" }, { CIRCLE, NULL, "Skip messages" }, END_HINTS
};
static const struct hint text_hints[] = {
    { OTHER(0x12), NULL, "Page" }, { CROSS, NULL, "Close" }, END_HINTS
};
static const struct hint pick_hints[] = { /* pick one */
    { OTHER(0x12), NULL, "Move" }, { CROSS, NULL, "Select" },
    { OTHER(0), "L2 R2", "Count" }, { OTHER(0), "L1 R1", "Page" },
    { CIRCLE, NULL, "Back" }, END_HINTS
};
static const struct hint list_hints[] = {
    { CROSS, NULL, "Choose" }, { SQUARE, NULL, "Everything" },
    { CIRCLE, NULL, "Cancel" }, END_HINTS
};
static const struct hint keyboard_hints[] = {
    { OTHER(0x12), NULL, "Choose key" }, { CROSS, NULL, "Type" },
    { SQUARE, NULL, "Delete" }, { TRIANGLE, NULL, "Space" },
    { OTHER(0xF0), NULL, "Done" }, { CIRCLE, NULL, "Cancel" }, END_HINTS
};
static const struct hint *const hint_sets[] = {
    map_hints,       /* 0 the map */
    menu_hints,      /* 1 a menu, pick any */
    button_hints,    /* 2 a row of buttons */
    position_hints,  /* 3 picking a spot */
    prompt_hints,    /* 4 another prompt */
    direction_hints, /* 5 a direction */
    more_hints,      /* 6 >> */
    text_hints,      /* 7 text to read */
    pick_hints,      /* 8 a menu, pick one */
    list_hints,      /* 9 a prompt with a list to pick from */
    keyboard_hints,  /* 10 the keyboard drawn in-game */
};

static const SDL_Color hint_background = { 14, 16, 26, 255 };
static const SDL_Color hint_text = { 215, 215, 225, 255 };

/* Draw one CP437 character from the 2x font (16x32 cells). */
static int hint_glyph(int x, int y, unsigned char c, SDL_Color colour)
{
    SDL_Rect src = { (c % 32) * 16, (c / 32) * 32, 16, 32 };
    SDL_Rect dst = { x, y, 16, 32 };
    SDL_Color colours[2] = { hint_background, colour };

    SDL_SetPaletteColors(hint_font->format->palette, colours, 0, 2);
    SDL_BlitSurface(hint_font, &src, screen, &dst);
    return x + 16;
}

static int hint_string(int x, int y, const char *s, SDL_Color colour)
{
    while (*s)
        x = hint_glyph(x, y, (unsigned char) *s++, colour);
    return x;
}

static int hint_width(const struct hint *h)
{
    int glyphs = (int) strlen(h->action) + 1;

    glyphs += h->icon ? 1 : 0;
    glyphs += h->button ? (int) strlen(h->button) + (h->icon ? 1 : 0) : 0;
    return glyphs * 16;
}

/* The bar for what is on screen, centred, with a rule above it. */
static void draw_hints(int context)
{
    const struct hint *set =
        hint_sets[context >= 0 && context < (int) SDL_arraysize(hint_sets)
                      ? context : 0];
    const struct hint *h;
    SDL_Rect bar = { 0, hint_top, screen->w, screen->h - hint_top };
    SDL_Rect rule = { 0, hint_top, screen->w, 2 };
    int gap = 48, count = 0, total = 0, x, y = hint_top + (bar.h - 32) / 2 + 1;

    SDL_FillRect(screen, &bar, SDL_MapRGB(screen->format, hint_background.r,
                                          hint_background.g,
                                          hint_background.b));
    SDL_FillRect(screen, &rule, SDL_MapRGB(screen->format, 50, 56, 84));
    for (h = set; h->action; h++, count++)
        total += hint_width(h);
    /* closer together if a long bar would not fit, keeping a margin */
    if (count > 1 && total + gap * (count - 1) > screen->w - 64)
        gap = SDL_max(16, (screen->w - 64 - total) / (count - 1));
    total += gap * (count - 1);
    x = (screen->w - total) / 2;
    for (h = set; h->action; h++) {
        if (h != set)
            x += gap;
        if (h->icon)
            x = hint_glyph(x, y, h->icon, h->colour) + (h->button ? 16 : 0);
        if (h->button)
            x = hint_string(x, y, h->button, h->colour);
        x = hint_string(x + 16, y, h->action, hint_text);
    }
}

static void present(void)
{
    int context = ps5_curses_context();

    if (hint_font && context != hint_context) {
        SDL_Rect bar;

        hint_context = context;
        draw_hints(context);
        bar.x = 0, bar.y = hint_top, bar.w = screen->w;
        bar.h = screen->h - hint_top;
        upload_rect(&bar);
    }
    gl_DrawArrays(GL_TRIANGLES, 0, 3);
    SDL_GL_SwapWindow(window);
#ifdef PS5_SCREENSHOT
    /* Debug builds: the last frame shown, readable over FTP; written whole
     * and renamed, so a reader never sees half a file.  At most every three
     * seconds, as a save takes seconds: every frame would slow the game to
     * a crawl (and PDCurses's timeouts, which count its 50 ms naps). */
    {
        static Uint32 saved;

        if (!saved || SDL_TICKS_PASSED(SDL_GetTicks(), saved + 3000)) {
            if (SDL_SaveBMP(screen, "/data/nethack-screen.tmp") == 0)
                rename("/data/nethack-screen.tmp", "/data/nethack-screen.bmp");
            saved = SDL_GetTicks();
        }
    }
#endif
}

/* Show the hints for what is asking now, if the last frame showed others. */
static void refresh_hints(void)
{
    if (hint_font && ps5_curses_context() != hint_context)
        present();
}

/* ---- Typing: the system's on-screen keyboard (IME dialog) -------------- */

/* As ps5-payload-dev's SDL port declares them (src/video/ps5/
 * SDL_ps5keyboard.c).  The text is UTF-16: the PS5's wchar_t is 16 bits. */
typedef struct {
    int userId;
    int type;                   /* 1: basic Latin */
    uint64_t supportedLanguages;
    int enterLabel;             /* 0: default */
    int inputMethod;            /* 0: default */
    void *filter;
    uint32_t option;
    uint32_t maxTextLength;
    uint16_t *inputTextBuffer;
    float posx, posy;
    int halign, valign;         /* 1, 1: centred */
    const uint16_t *placeholder;
    const uint16_t *title;
    int8_t reserved[16];
} ime_dialog_param;

typedef struct {
    int outcome;                /* 0 OK, 1 cancelled, 2 aborted */
    int8_t reserved[12];
} ime_dialog_result;

enum { IME_STATUS_NONE, IME_STATUS_RUNNING, IME_STATUS_FINISHED };

/* A title is not given libSceImeDialog at start (its imports would be
 * left unresolved), so it is loaded when first needed and its functions
 * looked up by name. */
int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv,
                             uint32_t flags, void *option, int *result);
int sceKernelDlsym(int handle, const char *symbol, void **address);
/* A title's sandbox shows the system libraries under /<random word>/ */
const char *sceKernelGetFsSandboxRandomWord(void);

static int (*ime_init)(const ime_dialog_param *param, void *extended);
static int (*ime_status)(void);
static int (*ime_result)(ime_dialog_result *result);
static int (*ime_term)(void);

static int ime_load(void)
{
    static int state; /* 0 not tried, 1 loaded, -1 unavailable */
    const char *word = sceKernelGetFsSandboxRandomWord();
    char path[128];
    int handle;

    if (state)
        return state > 0;
    state = -1;
    snprintf(path, sizeof path, "/%s/common/lib/libSceImeDialog.sprx",
             word ? word : "system");
    handle = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, NULL);
    if (handle < 0) {
        printf("nethack: cannot load the on-screen keyboard from %s "
               "(0x%08x)\n", path, (unsigned) handle);
        return 0;
    }
    {
        int r1 = sceKernelDlsym(handle, "sceImeDialogInit",
                                (void **) &ime_init);
        int r2 = sceKernelDlsym(handle, "sceImeDialogGetStatus",
                                (void **) &ime_status);
        int r3 = sceKernelDlsym(handle, "sceImeDialogGetResult",
                                (void **) &ime_result);
        int r4 = sceKernelDlsym(handle, "sceImeDialogTerm",
                                (void **) &ime_term);

        if (r1 || r2 || r3 || r4 || !ime_init || !ime_status || !ime_result
            || !ime_term) {
            printf("nethack: the on-screen keyboard's functions are missing "
                   "(module 0x%08x: %08x %08x %08x %08x)\n", (unsigned) handle,
                   (unsigned) r1, (unsigned) r2, (unsigned) r3,
                   (unsigned) r4);
            return 0;
        }
    }
    state = 1;
    return 1;
}

/* Ask for a line of text with the on-screen keyboard, the prompt as its
 * title; for NetHack's getlin() (win/curses/cursdial.c).  Returns 1 with
 * the text in answer, 0 if cancelled, -1 if the keyboard cannot open (the
 * caller then reads keys as usual). */
int ps5_text_input(const char *prompt, char *answer, int size)
{
    static uint16_t title[128], text[256];
    ime_dialog_param param;
    ime_dialog_result result;
    int user = -1, status, rc, i;

    if (size < 2 || !screen || !ime_load())
        return -1;
    for (i = 0; prompt[i] && i < (int) SDL_arraysize(title) - 1; i++)
        title[i] = (unsigned char) prompt[i];
    title[i] = 0;
    memset(text, 0, sizeof text);

    memset(&param, 0, sizeof param);
    sceUserServiceGetForegroundUser(&user);
    param.userId = user;
    param.type = 1; /* basic Latin: NetHack's text is ASCII */
    param.maxTextLength = (uint32_t) SDL_min(size - 1,
                                             (int) SDL_arraysize(text) - 1);
    param.inputTextBuffer = text;
    param.posx = screen->w / 2.0f;
    param.posy = screen->h / 2.0f;
    param.halign = param.valign = 1;
    param.title = title;

    rc = ime_init(&param, NULL);
    if (rc < 0) {
        printf("nethack: on-screen keyboard unavailable (0x%08x)\n",
               (unsigned) rc);
        return -1;
    }
    printf("nethack: on-screen keyboard open: %s\n", prompt);
    /* the system draws the keyboard over the title; keep showing the game
     * underneath until it closes */
    while ((status = ime_status()) == IME_STATUS_RUNNING) {
        present();
        sceKernelUsleep(16000);
    }
    memset(&result, 0, sizeof result);
    rc = status == IME_STATUS_FINISHED ? ime_result(&result) : -1;
    ime_term();
    printf("nethack: on-screen keyboard closed (status %d, result 0x%08x, "
           "outcome %d)\n", status, (unsigned) rc, result.outcome);
    if (rc < 0 || result.outcome != 0)
        return 0;
    for (i = 0; text[i] && i < size - 1; i++)
        answer[i] = text[i] < 128 ? (char) text[i] : '?';
    answer[i] = '\0';
    return 1;
}

#ifdef PS5_SCREENSHOT
/* Debug builds: type the contents of /data/nethack-input.txt, then remove
 * it, so the game can be driven over FTP.  "\n" is Return, and ^ escapes
 * the controller: ^[ Circle (Escape), ^X Cross, ^S Square, ^T Triangle,
 * ^O Options, ^C Create, ^P the touchpad, ^U ^D ^L ^R the d-pad, ^1 ^2 L1
 * and R1, ^3 ^4 L2 and R2, ^5 ^6 L3 and R3. */
static Uint32 remote_keys(Uint32 interval, void *unused)
{
    static const struct { char code; SDL_Keycode sym; } escapes[] = {
        { '[', SDLK_ESCAPE }, { 'X', SDLK_F1 }, { 'S', SDLK_F2 },
        { 'T', SDLK_F3 },     { 'O', SDLK_F4 }, { 'C', SDLK_F5 },
        { 'P', SDLK_F6 },     { 'U', SDLK_UP },
        { 'D', SDLK_DOWN },   { 'L', SDLK_LEFT }, { 'R', SDLK_RIGHT },
        { '1', SDLK_F7 },     { '2', SDLK_F8 },   { '3', SDLK_F9 },
        { '4', SDLK_F10 },    { '5', SDLK_F11 },  { '6', SDLK_F12 },
    };
    FILE *fp = fopen("/data/nethack-input.txt", "r");
    char keys[256];
    size_t n, i, e;

    (void) unused;
    if (!fp)
        return interval;
    n = fread(keys, 1, sizeof keys - 1, fp);
    fclose(fp);
    remove("/data/nethack-input.txt");
    for (i = 0; i < n; i++) {
        if (keys[i] == '^' && i + 1 < n) {
            for (e = 0; e < SDL_arraysize(escapes); e++)
                if (escapes[e].code == keys[i + 1])
                    push_key(escapes[e].sym);
            i++;
        } else if (keys[i] == '\n') {
            push_key(SDLK_RETURN);
        } else {
            push_key((unsigned char) keys[i]);
        }
    }
    return interval;
}
#endif

/* PDCurses's calls, bound here with --wrap.  PDCurses creates and owns its
 * window as on a desktop (given a window, it would take the program to draw
 * a background behind the text and make the default background
 * transparent); the window it gets is the OpenGL one set up here, and its
 * surface is ours, as SDL has no window surface. */
SDL_Window *__wrap_SDL_CreateWindow(const char *title, int x, int y, int w,
                                    int h, Uint32 flags)
{
    (void) title, (void) x, (void) y, (void) w, (void) h, (void) flags;
    return window;
}

/* PDCurses calls this before each look for a key, to redraw the window
 * after it was hidden.  Its own takes the first event off the queue and,
 * if it is not such a redraw, pushes it back on at the end: with more than
 * one press waiting, the presses come out of order ("Ehi" typed as "pdd").
 * This one only looks.  (The title's one window is never hidden.) */
int __wrap_SDL_UpdateWindowSurface(SDL_Window *w);

void __wrap_PDC_pump_and_peep(void)
{
    SDL_Event e;

    SDL_PumpEvents();
    if (SDL_PeepEvents(&e, 1, SDL_PEEKEVENT, SDL_WINDOWEVENT,
                       SDL_WINDOWEVENT) == 1
        && (e.window.event == SDL_WINDOWEVENT_RESTORED
            || e.window.event == SDL_WINDOWEVENT_EXPOSED
            || e.window.event == SDL_WINDOWEVENT_SHOWN)) {
        SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_WINDOWEVENT, SDL_WINDOWEVENT);
        __wrap_SDL_UpdateWindowSurface(window);
    }
}

SDL_Surface *__wrap_SDL_GetWindowSurface(SDL_Window *w)
{
    (void) w;
    return screen;
}

int __wrap_SDL_UpdateWindowSurface(SDL_Window *w)
{
    SDL_Rect all = { 0, 0, 0, 0 };

    (void) w;
    all.w = screen->w;
    all.h = screen->h;
    upload_rect(&all);
    present();
    return 0;
}

int __wrap_SDL_UpdateWindowSurfaceRects(SDL_Window *w, const SDL_Rect *rects,
                                        int count)
{
    int i;

    (void) w;
    for (i = 0; i < count; i++) {
        SDL_Rect r = rects[i];

        if (r.x < 0)
            r.w += r.x, r.x = 0;
        if (r.y < 0)
            r.h += r.y, r.y = 0;
        if (r.x + r.w > screen->w)
            r.w = screen->w - r.x;
        if (r.y + r.h > screen->h)
            r.h = screen->h - r.y;
        if (r.w > 0 && r.h > 0)
            upload_rect(&r);
    }
    present();
    return 0;
}

/* NetHack has saved or ended the game: say so, and how to leave. */
static void farewell(int status)
{
    static const SDL_Color dim = { 150, 150, 165, 255 };
    /* status 0 follows a save, but also quitting or dying */
    const char *headline = status == 0 ? "Until next time."
                                       : "NetHack has stopped.";
    const char *advice = "Press the PS button and close NetHack to leave.";
    SDL_Rect all = { 0, 0, 0, 0 };
    int y;

    if (!screen || !hint_font)
        return;
    all.w = screen->w;
    all.h = screen->h;
    SDL_FillRect(screen, &all, SDL_MapRGB(screen->format, hint_background.r,
                                          hint_background.g,
                                          hint_background.b));
    y = screen->h / 2 - 48;
    hint_string((screen->w - (int) strlen(headline) * 16) / 2, y, headline,
                hint_text);
    hint_string((screen->w - (int) strlen(advice) * 16) / 2, y + 64, advice,
                dim);
    upload_rect(&all);
    hint_context = ps5_curses_context(); /* no hint bar over the message */
    present();
}

/* A native title may not exit: exit() raises SIGSYS.  NetHack has saved or
 * ended the game by now; wait for the player to close the title. */
void __wrap_exit(int status)
{
    printf("nethack: exited (status %d); close it from the home screen\n",
           status);
    fflush(stdout);
    farewell(status);
    for (;;)
        sceKernelUsleep(1000000);
}

static void setup_playground(void)
{
    /* NetHack's HACKDIR is the title root (/app0, read-only), where its data
     * files are; it writes saves, scores, bones and locks under
     * VAR_PLAYGROUND. */
    mkdir(PLAYGROUND, 0755);
    mkdir(PLAYGROUND "/save", 0755);
    ensure_file(PLAYGROUND "/perm");
    ensure_file(PLAYGROUND "/record");
    ensure_file(PLAYGROUND "/logfile");
    ensure_file(PLAYGROUND "/xlogfile");
    ensure_file(PLAYGROUND "/livelog");
    /* .nethackrc ships with the data; options set in the game last for it. */
    setenv("HOME", "/app0", 1);
    if (!getenv("USER"))
        setenv("USER", "player", 1);
}

int main(int argc, char **argv)
{
    int i;
    static char *nh_argv[8];
    int nh_argc = 0;

    /* A title has no console: NetHack's output goes to a file that can be
     * read back over FTP.  Unbuffered, so a crash loses nothing. */
    if (freopen("/data/nethack-ps5.log", "w", stdout)) {
        setvbuf(stdout, NULL, _IONBF, 0);
        dup2(fileno(stdout), fileno(stderr));
    }
    printf("NetHack starting\n");

    SDL_SetMainReady();
    setup_playground();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) != 0
        || setup_presenter() != 0) {
        printf("nethack: display setup failed: %s\n", SDL_GetError());
        __wrap_exit(1);
    }
    /* The 2x font's 16x32 cells fill the screen above the hint bar: 120x32
     * on 1920x1080. */
    {
        char size[16];
        int lines = (screen->h - HINT_BAR_HEIGHT) / 32;

        snprintf(size, sizeof size, "%d", screen->w / 16);
        setenv("PDC_COLS", size, 1);
        snprintf(size, sizeof size, "%d", lines);
        setenv("PDC_LINES", size, 1);
        hint_top = lines * 32;
    }
    pdc_font = scaled_font(2);
    hint_font = scaled_font(2);
    rep_lock = SDL_CreateMutex();
    SDL_SetEventFilter(input_filter, NULL);
    for (i = 0; i < SDL_NumJoysticks(); i++)
        SDL_JoystickOpen(i);
#ifdef PS5_SCREENSHOT
    SDL_AddTimer(500, remote_keys, NULL);
#endif

    nh_argv[nh_argc++] = "/app0/eboot.bin";
    nh_argv[nh_argc++] = "-u";
    nh_argv[nh_argc++] = (char *) player_name();
    printf("nethack: playing as %s\n", nh_argv[2]);
    for (i = 1; i < argc && nh_argc < 7; i++)
        nh_argv[nh_argc++] = argv[i];
    nh_argv[nh_argc] = NULL;
    nethack_main(nh_argc, nh_argv);
    __wrap_exit(0);
}

#else /* the payload */

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
    pdc_font = scaled_font(2);
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

#endif /* PS5_NATIVE */
