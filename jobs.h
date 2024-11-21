#ifndef ISH_JOBS_H_
#define ISH_JOBS_H_

#include <sys/types.h>
#include <unistd.h>

typedef struct procstat {
        pid_t pid;              /* process id */
        int status;             /* process status information */
} procstat_t;

/*
 * A job is either a single process or multiple processes
 * participating in a single pipeline.
 */
typedef struct job {
        procstat_t ps0;         /* used for a single process */
        procstat_t *ps;         /* points to the processes statuses */
        short nprocs;           /* number of processes */
        pid_t pgrp;             /* job process group */
        char *cmd;              /* job command string */
        struct job *next;       /* job used after this one */
} job_t;

extern void initjobs(void);
extern job_t *makejob(int, char *);
extern pid_t forkshell(_Bool, job_t *);
extern void waitforjob(job_t *);
extern void prbgrd(const job_t *);
extern void prjobs(void);
extern void reapjobs(_Bool);
extern int killjob(long, _Bool);
extern int fgjob(long);
extern void killsusjobs(void);
extern _Bool suspjobexist(void);

#endif  /* !ISH_JOBS_H_ */
