#ifndef ISH_CMD_H_
#define ISH_CMD_H_

#include "array.h"

typedef enum {
        C_SEQ,
        C_BGRD,
        C_PIPE,
        C_PIPEERR
} cmode_t;

typedef struct cmd {
        char *name;
        array_t *args;
        cmode_t mode;
        struct cmd *next;
        char *filein;        
        char *fileout;
        _Bool redirerr;
        _Bool append;        
} cmd_t;

extern cmd_t *cmd_new(void);
extern void cmd_free(cmd_t *);
extern cmd_t *cmd_last(const cmd_t *);
extern void cmd_run(cmd_t *);
extern char *cmd_str(const cmd_t *);

#endif  /* ISH_CMD_H_ */
