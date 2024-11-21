#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "env.h"
#include "utils.h"

typedef struct var {
        char *name;
        char *val;
        struct var *next;
} var_t;

static var_t *environ;

static var_t*
lookup(const char *name)
{
        var_t *vp;

        for (vp = environ; vp; vp = vp->next)
                if (!strcmp(name, vp->name))
                        return (vp);

        return (NULL);
}

void
env_set(const char *name, const char *val)
{
        var_t *vp;

        if ((vp = lookup(name)) == NULL) {
                vp = malloc_or_die(sizeof(*vp));
                vp->name = strdup_or_die(name);
                vp->val = NULL;                
                vp->next = environ;
                environ = vp;
        }
        if (vp->val)
                free(vp->val);        
        vp->val = val ? strdup_or_die(val): NULL;
}

const char *
env_get(const char *name)
{
        var_t *vp;

        vp = lookup(name);
        return (vp ? vp->val: NULL);
}

void
env_unset(const char *name)
{
        var_t *prev;
        var_t *vp;        

        if (environ == NULL)
                return;
        if (!strcmp(environ->name, name)) {
                vp = environ;                
                environ = environ->next;
                goto found;
        }
        for (prev = environ; prev->next; prev = prev->next)
                if (!strcmp(prev->next->name, name)) {
                        vp = prev->next;                        
                        prev->next = prev->next->next;
                        goto found;
                }
        return;
found:
        free(vp->name);
        if (vp->val)
                free(vp->val);
        free(vp);        
}

void
env_display(void)
{
        var_t *vp;

        for (vp = environ; vp; vp = vp->next)
                printf("%s=%s\n", vp->name, vp->val ? vp->val: "");
}

/*
 * Return a NULL-terminated array of the environment in the form
 * key=value.
 */
char **
env_execargs(void)
{
        char **env;
        size_t len;
        var_t *vp;
        size_t i;        
        
        len = 0;        
        for (vp = environ; vp; vp = vp->next)
                len++;

        env = malloc_or_die((len + 1) * sizeof(*env));
        env[len] = NULL;
        i = 0;        
        for (vp = environ; vp; vp = vp->next) {
                size_t nlen = strlen(vp->name);
                size_t vlen = strlen(vp->val);
                char *s = malloc_or_die(nlen + vlen + 2);
                memcpy(s, vp->name, nlen);
                s[nlen] = '=';
                memcpy(s + nlen + 1, vp->val, vlen + 1);
                env[i++] = s;                
        }

        return (env);        
}
