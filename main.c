#define _POSIX_C_SOURCE 200112L

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cmd.h"
#include "err.h"
#include "jobs.h"
#include "utils.h"
#include "y.tab.h"

extern char **environ;
extern cmd_t *root;
extern void yyrestart(FILE *);

static void
print_prompt(void)
{
        static char hostname[_POSIX_HOST_NAME_MAX];

        if (gethostname(hostname, sizeof(hostname)) == -1)
                err_quit("gethostname");
        fprintf(stderr, "%s%% ", hostname);
}

static void
cmdloop(FILE *fp, _Bool interactive)
{
        _Bool userwarned;

        userwarned = 0;
        root = NULL;
        yyrestart(fp);
        for (;;) {
                reapjobs(0);
                if (interactive)
                        print_prompt();
                yyparse();
                if (root == (void *)-1) {
                        reapjobs(1);                        
                        if (interactive && !userwarned && suspjobexist()) {
                                fprintf(stderr, "There are suspended jobs.\n");
                                userwarned = 1;
                                rewind(fp);                                
                                continue;
                        }
                        break;                        
                }
                if (root) {
                        cmd_run(root);
                        cmd_free(root);
                        root = NULL;
                }
        }
}

static char *
joinpath(const char *p1, const char *p2)
{
        size_t len1;
        size_t len2;
        char *path;
        char *p;

        len1 = strlen(p1);
        len2 = strlen(p2);
        path = malloc_or_die(len1 + len2 + 2);

        memcpy(path, p1, len1);
        p = path + len1;
        *p++ = '/';
        memcpy(p, p2, len2);
        *(p+len2) = '\0';

        return (path);
}

static void
loadprofile(void)
{
        char *fullpath;
        FILE *fp;

        fullpath = joinpath(gethomedir(), ".ishrc");
        if ((fp = fopen(fullpath, "r")) != NULL) {
                cmdloop(fp, 0);
                fclose(fp);
        }
        free(fullpath);
}

int
main(void)
{

        // Don't inherit environment variables.
        environ = NULL;

        initjobs();
        loadprofile();
        cmdloop(stdin, 1);

        return (0);
}
