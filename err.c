#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "err.h"

void
err_quit(const char *fmt, ...)
{
        char buf[ERRMAXLINE];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        errx(EXIT_FAILURE, "%s", buf);
        va_end(ap);        
}

void
err_sys(const char *fmt, ...)
{
        char buf[ERRMAXLINE];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        err(EXIT_FAILURE, "%s", buf);
        va_end(ap);
}
