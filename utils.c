#define _POSIX_C_SOURCE 200809L

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "err.h"
#include "utils.h"

void *
malloc_or_die(size_t size)
{
        void *ptr;

        if ((ptr = malloc(size)) == NULL)
                err_sys("malloc");
        return (ptr);
}

void *
realloc_or_die(void *ptr, size_t size)
{
        if ((ptr = realloc(ptr, size)) == NULL)
                err_sys("realloc");
        return (ptr);
}

pid_t
fork_or_die(void)
{
        pid_t pid;

        if ((pid = fork()) == -1)
                err_sys("fork");

        return (pid);        
}

void
close_or_die(int fd)
{
        if (close(fd) == -1)
                err_sys("close");
}

int
dup_or_die(int oldfd)
{
        int newfd;

        if ((newfd = dup(oldfd)) == -1)
                err_sys("dup");

        return (newfd);        
}

const char *
gethomedir(void)
{
        struct passwd *pw;

        errno = 0;
        if ((pw = getpwuid(getuid())) == NULL)
                return (NULL);

        return (pw->pw_dir);        
}

int
open_or_die(const char *path, int flags, ...)
{
        int fd;
        mode_t mode;        
        va_list ap;

        if (flags & O_RDONLY)
                fd = open(path, flags);
        else {
                va_start(ap, flags);
                mode = va_arg(ap, mode_t);
                va_end(ap);
                fd = open(path, flags, mode);
        }
        if (fd == -1)
                err_sys("%s", path);

        return (fd);        
}

char *
strdup_or_die(const char *s)
{
        char *d;

        if ((d = strdup(s)) == NULL)
                err_sys("strdup");
        return (d);        
}
