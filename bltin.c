#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bltin.h"
#include "env.h"
#include "jobs.h"
#include "utils.h"

static int exitcmd(int, char **);
static int cdcmd(int, char **);
static int jobscmd(int, char **);
static int killcmd(int, char **);
static int bgcmd(int, char **);
static int fgcmd(int, char **);
static int setenvcmd(int, char **);
static int unsetenvcmd(int, char **);

static struct {
        const char *name;
        const builtin_t func;
} builtins[] = {
        {"exit", exitcmd},
        {"cd", cdcmd},
        {"jobs", jobscmd},
        {"kill", killcmd},
        {"bg", bgcmd},
        {"fg", fgcmd},
        {"setenv", setenvcmd},
        {"unsetenv", unsetenvcmd},
};

#define NELELMS(x)	(sizeof(x)/sizeof((x)[0]))

builtin_t
lookupbltin(const char *name)
{

        for (size_t i = 0; i < NELELMS(builtins); i++)
                if (!strcmp(name, builtins[i].name))
                        return (builtins[i].func);

        return (NULL);
}

static inline int
usage(const char *msg)
{

        fprintf(stderr, "usage: %s\n", msg);
        return (1);        
}

static int
exitcmd(int argc, char **argv)
{

        UNUSED(argv);
        if (argc > 0)
                return (usage("exit"));
        killsusjobs();
        
        exit(0);
}

static int
cdcmd(int argc, char *argv[])
{
        const char *pathname;

        if (argc > 1)
                return (usage("cd [dir]"));

        pathname = argc == 0 ? gethomedir(): argv[0];
        if (pathname == NULL) {
                warn("cd: home directory");
                return (-errno);
        }

        if (chdir(pathname) == -1) {
                warn("cd: %s", pathname);
                return (-errno);
        }

        return (0);
}

static int
jobscmd(int argc, char *argv[])
{

        UNUSED(argv);
        if (argc > 0)
                return (usage("jobs"));
        reapjobs(1);
        prjobs();

        return (0);
}

static long
jobnum(const char *jobid)
{
        char *end;
        long n;

        if (*jobid != '%' || !isdigit(*(jobid+1)))
                return (-1);

        n = strtol(jobid+1, &end, 10);
        if (*end != '\0')
                return (-1);

        return (n);
}

static inline int
invalidjob(const char *name, const char *jobid)
{

        warnx("%s: invalid job: %s", name, jobid);
        return (1);
}

static int
killcmd(int argc, char *argv[])
{

        if (argc == 0)
                return (usage("kill %job ..."));

        for (int i = 0; i < argc; i++) {
                long n = jobnum(argv[i]);
                if (n == -1)
                        return (invalidjob("kill", argv[i]));
                if (killjob(n, 1) == -1)
                        return (2);
        }

        return (0);
}

static int
bgcmd(int argc, char *argv[])
{

        if (argc == 0)
                return (usage("bg %job ..."));

        for (int i = 0; i < argc; i++) {
                long n = jobnum(argv[i]);
                if (n == -1)
                        return (invalidjob("bg", argv[i]));
                if (killjob(n, 0) == -1)
                        return (2);
        }

        return (0);
}

static int
fgcmd(int argc, char *argv[])
{
        long n;

        if (argc != 1)
                return (usage("fg %job"));
        if ((n = jobnum(argv[0])) == -1)
                return (invalidjob("fg", argv[0]));
        if (fgjob(n) == -1)
                return (2);

        return (0);
}

static int
setenvcmd(int argc, char *argv[])
{

        if (argc > 2)
                return (usage("setenv [var [val]]"));
        if (argc == 0)
                env_display();
        else
                env_set(argv[0], argc == 1 ? NULL: argv[1]);

        return (0);
}

static int
unsetenvcmd(int argc, char *argv[])
{

        if (argc != 1)
                return (usage("unsetenv var"));
        env_unset(argv[0]);
        
        return (0);        
}
