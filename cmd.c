#define _POSIX_C_SOURCE 200809L

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bltin.h"
#include "cmd.h"
#include "err.h"
#include "env.h"
#include "jobs.h"
#include "utils.h"

extern char **environ;

cmd_t *
cmd_new(void)
{
        cmd_t *c;

        c = malloc_or_die(sizeof(*c));
        c->name = NULL;
        c->args = array_new();
        c->mode = C_SEQ;
        c->next = NULL;
        c->filein = NULL;
        c->fileout = NULL;
        c->redirerr = 0;
        c->append = 0;

        return (c);
}

void
cmd_free(cmd_t *c)
{

        while (c) {
                cmd_t *n = c->next;
                if (c->name)
                        free(c->name);
                if (c->args)
                        array_free(c->args);
                if (c->filein)
                        free(c->filein);
                if (c->fileout)
                        free(c->fileout);
                free(c);
                c = n;
        }
}

cmd_t *
cmd_last(const cmd_t *c)
{
        cmd_t *last;

        assert(c);
        for (last = (cmd_t *)c; last->next; last = last->next)
                ;

        return (last);
}

/*
 * Return a pointer to the first colon character in the string or the
 * terminal null character if there's none.
 */
static const char *
findcolon(const char *s)
{

        for (; *s; s++)
                if (*s == ':')
                        break;
        return (s);
}

/*
 * Look up the given command.
 *
 * Return the full pathname of the command if any.  The various
 * directories separated by a colon in the PATH environment variable
 * are searched in order.
 */
static const char *
lookupcmd(const char *name)
{
        const char *pathenv;
        const char *start;
        const char *end;
        const size_t namelen = strlen(name);
        char *pathname;
        size_t pathsize;

        if (name[0] == '/' ||
            (name[0] == '.' &&
             (name[1] == '/' || (name[1] == '.' && name[2] == '/'))))
                return (name);

        if ((pathenv = env_get("PATH")) == NULL)
                goto err;

        pathname = NULL;
        pathsize = 0;
        for (start = end = pathenv; *end; start = end + 1) {
                int dirfd;
                size_t dlen;
                size_t psiz;
                int retval;

                end = findcolon(start);
                dlen = end - start;
                psiz = namelen + dlen + 2;
                if (pathname == NULL || pathsize < psiz) {
                        pathname = realloc_or_die(pathname, psiz);
                        pathsize = psiz;
                }
                
                memcpy(pathname, start, dlen);
                pathname[dlen] = '\0';                
                if ((dirfd = open(pathname, O_RDONLY)) == -1)
                        continue;

                retval = faccessat(dirfd, name, F_OK, AT_EACCESS);
                close(dirfd);                
                if (retval == 0) {
                        pathname[dlen] = '/';
                        memcpy(pathname + dlen + 1, name, namelen + 1);
                        return (pathname);                        
                }
        }
err:
        err_quit("%s: command not found", name);
        return (NULL);          /* NOTREACHED */
}

static void
runcmd(int argc, char **argv)
{
        const char *pathname;
        builtin_t func;

        if ((func = lookupbltin(argv[0])) != NULL)
                exit(func(argc-1, argv+1));

        pathname = lookupcmd(argv[0]);
        argv[0] = basename(argv[0]);
        execve(pathname, argv, env_execargs());

        // Only executed if execve fails.
        err_sys("%s", pathname);
}

static void
redirect(int from, int to)
{

        if (dup2(to, from) == -1)
                err_sys("dup2: %d %d", to, from);
}

/*
 * Remove quotes and escape characters.
 */
static char *
process_arg(const char *arg)
{
        char *s;
        size_t len;
        size_t i;

        assert(*arg);
        len = strlen(arg);
        if (arg[0] == '\'' || arg[0] == '\"') {
                assert(len > 1 && arg[len-1] == arg[0]);
                arg++;
                len -= 2;
        }

        s = malloc_or_die(len+1);
        i = 0;
        for (size_t j = 0; j < len; j++)
                if (arg[j] != '\\')
                        s[i++] = arg[j];

        s[i] = '\0';

        return (s);
}

/*
 * Return NULL-terminated array of the command arguments.
 *
 * The first element is a copy of the command name.  In the subsequent
 * ones, quotes and escape characters are removed.
 */
static char **
create_args(const cmd_t *c)
{
        char **argv;

        argv = malloc_or_die(sizeof(*argv) * (c->args->len + 2));
        argv[0] = strdup_or_die(c->name);
        argv[c->args->len + 1] = NULL;
        for (int i = 0; i < c->args->len; i++)
                argv[i+1] = process_arg(array_get(c->args, i));

        return (argv);
}

/*
 * Free all the memories allocated by create_args().
 */
static void
free_args(char **argv)
{

        for (char **p = argv; *p; p++)
                free(*p);

        free(argv);
}

static void
handle_redirects(cmd_t *c)
{
        int fd;

        if (c->filein) {
                fd = open_or_die(c->filein, O_RDONLY);
                redirect(STDIN_FILENO, fd);
                close_or_die(fd);
        }

        if (c->fileout) {
                int flags = O_WRONLY|O_CREAT;
                mode_t mode = S_IWUSR|S_IRUSR;
                flags |= c->append ? O_APPEND: O_TRUNC;

                fd = open_or_die(c->fileout, flags, mode);
                redirect(STDOUT_FILENO, fd);
                if (c->redirerr)
                        redirect(STDERR_FILENO, fd);
                close_or_die(fd);
        }
}

/*
 * Execute a single command.
 */
static void
exec(cmd_t *c)
{
        char **argv;
        job_t *jp;
        _Bool background;

        background = c->mode == C_BGRD;
        argv = create_args(c);
        if (!background) {
                // Don't create a new process if it's a builtin.
                builtin_t func = lookupbltin(c->name);
                if (func) {
                        /*
                         * We need to save the standard input, output
                         * and error. We might redirect them since
                         * we're executing a builtin directly from the
                         * shell.
                         */
                        int fdin = dup_or_die(STDIN_FILENO);
                        int fdout = dup_or_die(STDOUT_FILENO);
                        int fderr = dup_or_die(STDERR_FILENO);

                        handle_redirects(c);

                        func(c->args->len, argv+1);

                        // Flush output buffer before continuing.
                        fflush(stdout);

                        // Restore.
                        redirect(STDIN_FILENO, fdin);
                        redirect(STDOUT_FILENO, fdout);
                        redirect(STDERR_FILENO, fderr);

                        close_or_die(fdin);
                        close_or_die(fdout);
                        close_or_die(fderr);

                        free_args(argv);
                        return;
                }
        }

        jp = makejob(1, cmd_str(c));
        if (forkshell(background, jp) == 0) {
                /* child */
                handle_redirects(c);
                runcmd(c->args->len+1, argv); /* doesn't return */
        }

        // Parent.
        free_args(argv);
        if (!background)                
                waitforjob(jp);
        else
                prbgrd(jp);
}

/*
 * Execute the pipeline command.
 *
 * Return the last command in the pipeline.
 */
static cmd_t *
execpipe(cmd_t *c)
{
        int fd[2];
        int nprocs;
        int prevfd;
        char **argv;
        cmd_t *last;
        job_t *jp;
        _Bool background;

        nprocs = 1;
        for (last = c;
             last->mode == C_PIPEERR || last->mode == C_PIPE;
             last = last->next)
                nprocs++;

        background = last->mode == C_BGRD;
        jp = makejob(nprocs, cmd_str(c));
        prevfd = -1;
        for (int i = 0; i < nprocs; i++, c = c->next) {
                argv = create_args(c);
                if (i < nprocs-1 && pipe(fd) == -1)
                        err_sys("pipe");

                if (forkshell(background, jp) == 0) {
                        /* child */
                        if (prevfd != -1 && prevfd != STDIN_FILENO) {
                                redirect(STDIN_FILENO, prevfd);
                                close_or_die(prevfd);
                        }
                        if (i < nprocs-1) {
                                close_or_die(fd[0]); /* unused */
                                if (fd[1] != STDOUT_FILENO) {
                                        redirect(STDOUT_FILENO, fd[1]);
                                        close_or_die(fd[1]);
                                }
                                if (c->mode == C_PIPEERR)
                                        redirect(STDERR_FILENO, STDOUT_FILENO);
                        }
                        handle_redirects(c);
                        runcmd(c->args->len+1, argv); /* doesn't return */
                }

                /* parent */
                if (prevfd != -1)
                        close_or_die(prevfd);
                if (i < nprocs-1) {
                        prevfd = fd[0];
                        close_or_die(fd[1]);
                }
                free_args(argv);
        }

        if (!background)
                waitforjob(jp);
        else
                prbgrd(jp);

        return (last);
}

void
cmd_run(cmd_t *c)
{

        for (; c; c = c->next) {
                switch (c->mode) {
                case C_SEQ:     /* FALLTHROUGH */
                case C_BGRD:
                        exec(c);
                        break;
                case C_PIPE:    /* FALLTHROUGH */
                case C_PIPEERR:
                        assert(c->next);
                        c = execpipe(c);
                        break;
                default:
                        err_quit("unknown command mode: %d", c->mode);
                        break;
                }
        }

}

static size_t
cmdlen(const cmd_t *c)
{
        size_t len;

        len = strlen(c->name);
        for (int i = 0; i < c->args->len; i++)
                len += strlen(array_get(c->args, i)) + 1; /* add a space */

        return (len);
}

static inline char *
increasebuf(char *buf, size_t len, size_t *capp)
{

        if (len < *capp)
                return (buf);

        while (len >= *capp)
                *capp *= 2;

        return (realloc_or_die(buf, *capp));
}

static size_t
redirlen(const cmd_t *c)
{
        size_t len;

        len = 0;
        if (c->filein)
                len += strlen(c->filein) + 2; /* add a prefix space */
        if (c->fileout) {
                len += 2;       /* add a prefix space */
                if (c->append)
                        len++;
                if (c->redirerr)
                        len++;
                len += strlen(c->fileout);
        }

        return (len);
}

static size_t
seplen(const cmd_t *c)
{

        if (c->next) {
                if (c->mode == C_SEQ || c->mode == C_BGRD)
                        return (2);
                else if (c->mode == C_PIPE)
                        return (3);
                else if (c->mode == C_PIPEERR)
                        return (4);
        }

        return (0);
}

static inline size_t
strappend(const char *src, char *dst)
{
        size_t n;

        n = strlen(src);
        memcpy(dst, src, n);
        return (n);
}

/*
 * Return a null-terminated string representing the command.
 */
char *
cmd_str(const cmd_t *c)
{
        size_t cap;
        size_t len;
        char *buf;

        len = 0;
        cap = 8;
        buf = malloc_or_die(cap);
        for (; c; c = c->next) {
                /* Command name and its arguments. */
                size_t clen = cmdlen(c);
                buf = increasebuf(buf, len + clen, &cap);
                len += strappend(c->name, buf + len);
                for (int i = 0; i < c->args->len; i++) {
                        buf[len++] = ' ';
                        len += strappend(array_get(c->args, i), buf + len);
                }

                /* Redirection */
                size_t rlen = redirlen(c);
                if (rlen) {
                        buf = increasebuf(buf, len + rlen,&cap);
                        if (c->filein) {
                                buf[len++] = ' ';
                                buf[len++] = '<';
                                len += strappend(c->filein, buf + len);
                        }
                        if (c->fileout) {
                                buf[len++] = ' ';
                                buf[len++] = '>';
                                if (c->append)
                                        buf[len++] = '>';
                                if (c->redirerr)
                                        buf[len++] = '&';
                                len += strappend(c->fileout, buf + len);
                        }
                }

                /* Separator */
                size_t slen = seplen(c);
                if (slen) {
                        buf = increasebuf(buf, len + slen, &cap);
                        switch (c->mode) {
                        case C_SEQ:
                                buf[len++] = ';';
                                break;
                        case C_BGRD:
                                buf[len++] = '&';
                                break;
                        case C_PIPE: /* FALLTHROUGH */
                        case C_PIPEERR:
                                buf[len++] = ' ';
                                buf[len++] = '|';
                                if (c->mode == C_PIPEERR)
                                        buf[len++] = '&';
                                break;
                        default:
                                err_quit("unexpected mode: %d", c->mode);
                                break;
                        }
                        buf[len++] = ' ';
                }
        }

        assert(len < cap);
        buf[len++] = '\0';

        return (buf);
}
