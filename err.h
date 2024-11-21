#ifndef ISH_ERR_H_
#define ISH_ERR_H_

#define ERRMAXLINE      100     /* maximum length of error message */

extern void err_quit(const char *, ...);
extern void err_sys(const char *, ...);

#endif
