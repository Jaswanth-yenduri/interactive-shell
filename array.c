#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "utils.h"

array_t *
array_new(void)
{
        array_t *a;

        a = malloc_or_die(sizeof(*a));
        a->len = 0;
        a->cap = 0;
        a->buf = NULL;

        return (a);
}

void
array_free(array_t *a)
{
        if (a->buf) {
                for (int i = 0; i < a->len; i++)
                        free(array_get(a, i));        
                free(a->buf);
        }
        free(a);
}

void *
array_get(array_t *a, int i)
{
        assert(a);
        assert(i >= 0 && i < a->len);

        return (a->buf[i]);
}

void
array_append(array_t *a, void *elm)
{
        assert(a);
        assert(elm);
        if (a->len >= a->cap) {
                int ncap = a->cap > 0 ? a->cap: 1;
                while (a->len >= ncap)
                        ncap *= 2;
                a->buf = realloc_or_die(a->buf, ncap*sizeof(*a->buf));
                a->cap = ncap;
        }

        a->buf[a->len++] = elm;
}
