#ifndef ISH_BLTIN_H_
#define ISH_BLTIN_H_

typedef int (*builtin_t)(int, char **);

extern builtin_t lookupbltin(const char *);

#endif  /* !ISH_BLTIN_H_ */
