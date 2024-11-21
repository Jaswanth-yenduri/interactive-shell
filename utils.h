#ifndef ISH_UTILS_H_
#define ISH_UTILS_H_

#include <sys/types.h>

#include <fcntl.h>
#include <stddef.h>

#define UNUSED(var)	do {                    \
                (void)(var);                    \
        } while (0)                             \

extern void *malloc_or_die(size_t);
extern void *realloc_or_die(void *, size_t);
extern pid_t fork_or_die(void);
extern void close_or_die(int);
extern int dup_or_die(int);
extern int open_or_die(const char *, int, ...);
extern char *strdup_or_die(const char *);
extern const char *gethomedir(void);

#endif  /* !ISH_UTILS_H_ */
