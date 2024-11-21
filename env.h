#ifndef ISH_ENV_H_
#define ISH_ENV_H_

extern void env_set(const char *, const char *);
extern const char *env_get(const char *);
extern void env_unset(const char *);
extern void env_display(void);
extern char **env_execargs(void);

#endif  /* !ISH_ENV_H_ */
