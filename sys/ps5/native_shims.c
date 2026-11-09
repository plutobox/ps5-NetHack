/* native_shims.c - C library functions for the native PS5 title that
 * neither the console nor mihawk-99's platform layer provide.
 *
 * package-native.sh binds libc's names to these nethack_ps5_ functions with
 * --defsym, as it binds the platform layer's ps5_ functions: a title must
 * not define libc's own names.  Each answers as a system without the
 * feature would, and NetHack's callers already handle that answer. */

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

/* lstat() fails for a title. A title has no symbolic links, so stat()
 * answers the same. */
int nethack_ps5_lstat(const char *restrict path, struct stat *restrict sb)
{
    return stat(path, sb);
}

/* Only called in the message handler's child after fork(), which fails
 * for a title. */
int nethack_ps5_setgid(gid_t gid)
{
    (void) gid;
    errno = EPERM;
    return -1;
}

/* gettty() records the terminal's speed; a title has no terminal. */
speed_t nethack_ps5_cfgetospeed(const struct termios *t)
{
    (void) t;
    return B9600;
}

/* A title has no user accounts: NetHack then uses getpwuid() and $USER. */
struct passwd *nethack_ps5_getpwnam(const char *name)
{
    (void) name;
    errno = ENOENT;
    return NULL;
}

/* Lua's os.tmpname(), which NetHack's scripts do not call. */
char *nethack_ps5_tmpnam(char *s)
{
    (void) s;
    errno = ENOSYS;
    return NULL;
}

/* unixtty.c asks for the terminal's disabled-character value; a title has
 * no terminal, so there is no limit to report. */
long nethack_ps5_fpathconf(int fd, int name)
{
    (void) fd;
    (void) name;
    errno = EINVAL;
    return -1;
}

/* ps5-opengl's runtime resolves relative fopen() paths against /app0, as if
 * the title had started there; NetHack also open()s some data files by
 * relative path (SYSCF_FILE).  Bound with --wrap=open, this does the same
 * for open(). */
int __real_open(const char *path, int flags, ...);

int __wrap_open(const char *path, int flags, ...)
{
    char resolved[1024];
    mode_t mode = 0;

    if (flags & O_CREAT) {
        va_list args;

        va_start(args, flags);
        mode = (mode_t) va_arg(args, int);
        va_end(args);
    }
    if (path && path[0] && path[0] != '/') {
        int length = snprintf(resolved, sizeof resolved, "/app0/%s", path);

        if (length > 0 && (size_t) length < sizeof resolved)
            return __real_open(resolved, flags, mode);
    }
    return __real_open(path, flags, mode);
}

/* A title may not chdir (it fails with EPERM), but ps5-opengl's runtime
 * resolves relative fopen() paths against /app0, as if the title had
 * started there. NetHack's HACKDIR is /app0, where its data files are, so
 * changing to it already holds; anything else cannot. */
int nethack_ps5_chdir(const char *path)
{
    if (path && (!strcmp(path, "/app0") || !strcmp(path, "/app0/")))
        return 0;
    errno = EPERM;
    return -1;
}
