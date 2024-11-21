#define _POSIX_C_SOURCE 200809L

#include <sys/types.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "err.h"
#include "jobs.h"
#include "utils.h"

static const int minjobsnum = 4; /* minimum number of jobs to allocate */

static struct {
        job_t *buf;           /* array of jobs */
        int num;              /* number of elements in buf */
        job_t *all;           /* list of all current jobs */
        job_t *free;          /* list of unused slots backed by buf */
        int nfree;            /* number of elements in freelist */
} jobs;

static int ttyfd = -1;    /* controlling tty file descriptor */
static pid_t shellpgrp = -1; /* shell process group */
static pid_t shellpid = -1;  /* shell process id */

static void
sigaction_or_die(int signo,
                 const struct sigaction *act,
                 struct sigaction *oldact)
{

        if (sigaction(signo, act, oldact) == -1)
                err_sys("sigaction");
}

/*
 * Change the handler of the given signal.
 *
 * If "oldact" is non-null, store the previous value of the
 * corresponds sigaction there.
 */
static void
handlesig(int signo, void (*handler)(int), struct sigaction *oldact)
{
        struct sigaction sa;

        sa.sa_handler = handler;
        sigemptyset(&sa.sa_mask);
        sigaction_or_die(signo, &sa, oldact);
}

/*
 * Ignore the given signal.
 */
static inline void
ignoresig(int signo)
{

        handlesig(signo, SIG_IGN, NULL);
}

/*
 * Terminate all the suspended jobs.
 *
 * Send to all of them a SIGTERM followed by a SIGCONT so that they
 * all can terminate gracefully.
 */
void
killsusjobs(void)
{

        for (job_t *jp = jobs.all; jp; jp = jp->next)
                for (short i = 0; i < jp->nprocs; i++) {
                        if (WIFSTOPPED(jp->ps[i].status)) {
                                pid_t pgid = jp->ps[0].pid;
                                if (kill(-pgid, SIGTERM) == -1 ||
                                    kill(-pgid, SIGCONT) == -1)
                                        warn("kill");
                                break;
                        }
                }
}

/*
 * Shell signal handler of SIGTERM.
 *
 * According to POSIX.1, a SIGHUP followed by a SIGCONT is sent to
 * every suspended process of an orphaned processes group.  We want
 * these processes to terminate gracefully before we exit.  So we send
 * to each one of them a SIGTERM followed by a SIGGONT instead.
 */
static void
termhandler(int signo)
{

        UNUSED(signo);
        killsusjobs();
        exit(EXIT_FAILURE);
}

/*
 * Initialize various variables used for job control by the shell and
 * some of its properties.
 */
void
initjobs(void)
{

        ttyfd = open_or_die(_PATH_TTY, O_RDWR | O_CLOEXEC);
        shellpgrp = getpgrp();
        shellpid = getpid();

        // Handle various signals.
        ignoresig(SIGQUIT);
        ignoresig(SIGINT);
        handlesig(SIGTERM, termhandler, NULL);
}

static void
increasebuf(void)
{
        job_t *jp;
        int nnum;

        nnum = jobs.num == 0 ? minjobsnum: jobs.num*2;
        jp = malloc_or_die(nnum * sizeof(*jp));
        memcpy(jp, jobs.buf, jobs.num*sizeof(*jp));

        /* Relocate list heads. */
        if (jobs.all)
                jobs.all = &jp[jobs.all - jobs.buf];
        if (jobs.free)
                jobs.free = &jp[jobs.free - jobs.buf];

        /* Relocate `next' and `ps' pointers. */
        for (int i = 0; i < jobs.num; i++) {
                if (jp[i].next)
                        jp[i].next = &jp[jp[i].next - jobs.buf];
                if (jp[i].ps == &jobs.buf[i].ps0)
                        jp[i].ps = &jp[i].ps0;
        }

        /* Add the new free slots. */
        jp[nnum-1].next = jobs.free;
        jobs.free = &jp[jobs.num];
        for (int i = jobs.num; i < nnum-1; i++)
                jp[i].next = jp+i+1;
        jobs.nfree += nnum - jobs.num;

        free(jobs.buf);
        jobs.buf = jp;
        jobs.num = nnum;
}

static void
decreasebuf(void)
{
        job_t *newjp;
        int nnum;
        int i;

        if (jobs.num <= minjobsnum)
                return;
        nnum = jobs.num / 2;
        newjp = malloc_or_die(nnum * sizeof(*newjp));

        /* Relocate list heads. */
        i = 0;
        for (job_t *jp = jobs.all; jp; jp = jp->next, i++) {
                memcpy(newjp+i, jp, sizeof(*jp));
                if (jp->ps == &jp->ps0)
                        newjp[i].ps = &newjp[i].ps0;
                if (jp->next)
                        newjp[i].next = newjp+i+1;
        }
        jobs.all = jobs.all ? newjp: NULL;
        jobs.free = newjp + i;
        jobs.nfree = nnum - i;
        for (; i < nnum-1; i++)
                newjp[i].next = newjp + i + 1;
        newjp[nnum-1].next = NULL;

        free(jobs.buf);
        jobs.buf = newjp;
        jobs.num = nnum;
}

/*
 * Free all the memories allocated in the job structure.
 */
static void
freealljobs(void)
{
        job_t *jp;
        job_t *next;

        for (jp = jobs.all; jp; jp = next) {
                next = jp->next;
                if (jp->ps != &jp->ps0)
                        free(jp->ps);
                free(jp->cmd);
        }
        jobs.all = NULL;
        jobs.free = NULL;
        jobs.num = 0;
        free(jobs.buf);
}

/*
 * Return a new job composed of "nprocs" processes.
 */
job_t *
makejob(int nprocs, char *cmd)
{
        job_t *jp;

        if (jobs.num >= jobs.nfree*2)
                increasebuf();

        jp = jobs.free;
        jobs.free = jobs.free->next;
        jobs.nfree--;
        jp->next = jobs.all;
        jobs.all = jp;

        jp->cmd = cmd;
        jp->nprocs = 0;
        if (nprocs == 1)
                jp->ps = &jp->ps0;
        else
                jp->ps = malloc_or_die(nprocs * sizeof(*jp->ps));

        return (jp);
}

/*
 * Free the resources used by the given job.
 */
static void
freejob(job_t *jp)
{
        job_t *prev;

        assert(jobs.all);
        if (jobs.all == jp) {
                jobs.all = jp->next;
                goto found;
        }
        for (prev = jobs.all; prev->next; prev = prev->next)
                if (prev->next == jp) {
                        prev->next = jp->next;
                        goto found;
                }
        err_quit("freejob: job not found: %p", jp);
found:
        if (jp->ps != &jp->ps0) {
                free(jp->ps);
                jp->ps = &jp->ps0;
        }
        free(jp->cmd);
        jp->next = jobs.free;
        jobs.free = jp;
        jobs.nfree++;
}

/*
 * Set the given process group as the foreground process group of the
 * tty.
 */
static void
setfggrp(pid_t pgrp)
{
        struct sigaction ttin;
        struct sigaction ttou;

        /*
         * We need first to ignore these signals for a background
         * process. Otherwise, they might be raised when calling
         * tcsetpgrp().  We'll restore their statuses later.
         */
        handlesig(SIGTTIN, SIG_IGN, &ttin);
        handlesig(SIGTTOU, SIG_IGN, &ttou);

        if (tcsetpgrp(ttyfd, pgrp) == -1)
                err_sys("tcsetpgrp");

        sigaction_or_die(SIGTTIN, &ttin, NULL);
        sigaction_or_die(SIGTTOU, &ttou, NULL);

        /*
         * The foreground process should handle signals sent by the
         * keyboard unless it's the shell itself.
         */
        if (shellpid != getpid()) {
                handlesig(SIGINT, SIG_DFL, NULL);
                handlesig(SIGQUIT, SIG_DFL, NULL);
                handlesig(SIGHUP, SIG_DFL, NULL);
        }
}

/*
 * Fork a subshell.
 *
 * The function has the same semantics as fork().  If "background" is
 * true, we create a background process, otherwise a foreground one.
 * The new job is added to the "jp" structure.
 */
pid_t
forkshell(_Bool background, job_t *jp)
{
        pid_t pid;
        pid_t pgrp;
        struct procstat *ps;

        if ((pid = fork_or_die()) == 0) {
                /* child */
                /*
                 * A single process will become a de facto process
                 * leader.  For a pipeline, it corresponds to the
                 * first process and all the remaining ones are added
                 * to the same group.
                 */
                pgrp = jp->nprocs == 0 ? getpid(): jp->ps[0].pid;
                if (setpgid(0, pgrp) == -1)
                        err_sys("setpgid");

                if (!background) {
                        /*
                         * Each process in a pipeline must be part of
                         * of the foreground process group before
                         * executing its code.
                         */
                        setfggrp(pgrp);
                }

                // Only the main shell will need these.
                freealljobs();
                return (pid);
        }

        ps = jp->ps + jp->nprocs++;
        ps->pid = pid;
        ps->status = -1;

        return (pid);
}

static inline long
jobnum(const job_t *jp)
{

        return (1 + jp-jobs.buf);
}

void
prbgrd(const job_t *jp)
{

        fprintf(stderr, "[%ld] %d\n", jobnum(jp), jp->ps[0].pid);
}

static inline void
prstatus(const job_t *jp, const char *status)
{

        fprintf(stderr, "[%ld] %s\t%s\n", jobnum(jp), status, jp->cmd);
}

/*
 * Flags used by showstatus() to display a process status or not.
 */
#define S_STOP	1
#define S_KILL	2
#define S_TERM	4
#define S_DONE  8
#define S_RUN	16
#define S_ALL	(S_STOP|S_KILL|S_TERM|S_DONE|S_RUN)

/*
 * Print the status of the given job.
 *
 * The "flags" argument must be bitwise-or'd of the S_* macros defined
 * above.
 *
 * Return true if and only if the job is finished.
 */
static _Bool
showstatus(const job_t *jp, int flags)
{
        short nexited;
        _Bool killed;
        _Bool terminated;

        nexited = 0;
        killed = 0;
        terminated = 0;
        for (short i = 0; i < jp->nprocs; i++) {
                if (jp->ps[i].status == -1 || WIFCONTINUED(jp->ps[i].status)) {
                        if (flags & S_RUN)
                                prstatus(jp, "Running");
                        return (0);
                }
                if (WIFSTOPPED(jp->ps[i].status)) {
                        if (flags & S_STOP)
                                prstatus(jp, "Stopped");
                        return (0);
                }
                if (WIFEXITED(jp->ps[i].status))
                        nexited++;
                else if (WIFSIGNALED(jp->ps[i].status)) {
                        if (WTERMSIG(jp->ps[i].status) == SIGTERM)
                                terminated = 1;
                        else
                                killed = 1;
                }
        }

        if (nexited == jp->nprocs) {
                if (flags & S_DONE)
                        prstatus(jp, "Done");
                return (1);
        }
        if (killed) {
                if (flags & S_KILL)
                        prstatus(jp, "Killed");
                return (1);
        }
        if (terminated) {
                if (flags & S_TERM)
                        prstatus(jp, "Terminated");
                return (1);
        }

        prstatus(jp, "Unknown");
        exit(EXIT_FAILURE);
}

/*
 * Show all the current jobs.
 *
 * The "flags" parameter is the same as showstatus().
 */
static void
showjobs(int flags)
{
        job_t *next;

        for (job_t *jp = jobs.all; jp; jp = next) {
                next = jp->next;
                if (showstatus(jp, flags))
                        freejob(jp);
        }
}

void
prjobs(void)
{

        showjobs(S_ALL);
}

static procstat_t *
findproc(pid_t pid, job_t *jp)
{

        for (short i = 0; i < jp->nprocs; i++)
                if (jp->ps[i].pid == pid)
                        return (jp->ps + i);

        return (NULL);
}

static procstat_t *
findproc_nofail(pid_t pid, job_t *jp)
{
        procstat_t *ps;

        if ((ps = findproc(pid, jp)) == NULL)
                err_quit("process %d not found in job %d", pid, jobnum(jp));
        return (ps);
}

/*
 * Wait for all the processes in the given job to finish.
 *
 * This function is called by the shell. Note that the job could have
 * been stopped and continued later on.
 */
void
waitforjob(job_t *jp)
{
        short nprocs;
        siginfo_t info;

        if (jp->nprocs == 1) {
                // We're waiting for a single foreground process.
                if (waitpid(jp->ps->pid, &jp->ps->status, WUNTRACED) == -1)
                        err_sys("waitpid");
                goto done;
        }

        /*
         * Find the number of processes in the pipeline that haven't
         * exited yet.
         */
        nprocs = 0;
        for (short i = 0; i < jp->nprocs; i++)
                if (jp->ps[i].status == -1 || WIFSTOPPED(jp->ps[i].status))
                        nprocs++;

        assert(nprocs);
        while (nprocs-- > 0) {
                int options = WEXITED | WSTOPPED;
                procstat_t *ps;
                /*
                 * All the processes in the pipeline are all part of
                 * the same process group and the first one is the
                 * leader.
                 */
                if (waitid(P_PGID, jp->ps[0].pid, &info, options) == -1)
                        err_sys("waitid");
                ps = findproc_nofail(info.si_pid, jp);
                if (info.si_code == CLD_STOPPED) {
                        /*
                         * All the other processes in the pipeline
                         * have been stopped too.  So the pipeline
                         * won't finish. We bail.
                         */
                        ps->status = (info.si_status << 8) | 0177;
                        break;
                }
                if (info.si_code == CLD_EXITED)
                        ps->status = info.si_status << 8;
                if (info.si_code == CLD_KILLED || info.si_code == CLD_DUMPED)
                        ps->status = info.si_status;
        }
done:
        /* Set the shell as the new foreground group. */
        setfggrp(shellpgrp);

        if (showstatus(jp, S_STOP|S_KILL|S_TERM))
                freejob(jp);
}

/*
 * Reap any finished background job.
 *
 * If "updateonly" is true, only update the statuses of all current
 * jobs.  If it's false, the statuses of finished jobs are displayed
 * and the resources used by them are freed.
 */
void
reapjobs(_Bool updateonly)
{
        pid_t pid;
        int wstatus;
        job_t *jp;
        procstat_t *ps;

loop:
        pid = waitpid(-1, &wstatus, WUNTRACED|WNOHANG|WCONTINUED);
        if (pid == 0 || (pid == -1 && errno == ECHILD)) {
                if (!updateonly)
                        showjobs(S_KILL|S_TERM|S_DONE);
                if (4*jobs.nfree >= 3*jobs.num)
                        decreasebuf();
                return;
        }
        if (pid == -1)
                err_sys("waitpid");
        for (jp = jobs.all; jp; jp = jp->next) {
                if ((ps = findproc(pid, jp)) == NULL)
                        continue;
                ps->status = wstatus;
                goto loop;
        }
        err_quit("process %d is not found", pid);
}

static job_t *
getjob(long jobid)
{
        job_t *jp;

        if (jobid < 1 || jobid > jobs.num)
                goto notfound;

        for (jp = jobs.all; jp; jp = jp->next)
                if (jp == jobs.buf + jobid - 1)
                        return (jp);
notfound:
        warnx("no such job: %ld", jobid);
        return (NULL);
}

/*
 * Kill the job identified by the given id.
 *
 * Send it a SIGTERM (if "terminate" is true) followed by a SIGCONT to
 * each process in the given job.
 *
 * Return 0 on success and -1 on failure.
 */
int
killjob(long jobid, _Bool terminate)
{
        job_t *jp;
        pid_t pgid;             /* process group id */

        if ((jp = getjob(jobid)) == NULL)
                return (-1);

        pgid = jp->ps[0].pid;
        if ((terminate && kill(-pgid, SIGTERM) == -1) ||
            kill(-pgid, SIGCONT) == -1) {
                warn("kill");
                return (-1);
        }

        return (0);
}

/*
 * Move the given job identified by the given id to the foreground.
 *
 * Return 0 on success and -1 on failure.
 */
int
fgjob(long jobid)
{
        job_t *jp;
        pid_t pgid;             /* process group id */

        if ((jp = getjob(jobid)) == NULL)
                return (-1);

        pgid = jp->ps[0].pid;
        setfggrp(pgid);
        if (kill(-pgid, SIGCONT) == -1) {
                warn("kill");
                return (-1);
        }

        fprintf(stderr, "%s\n", jp->cmd);
        waitforjob(jp);

        return (0);
}

/*
 * Return true if and only there's currently a suspended job.
 */
_Bool
suspjobexist(void)
{

        for (job_t *jp = jobs.all; jp; jp = jp->next)
                for (short i = 0; i < jp->nprocs; i++)
                        if (WIFSTOPPED(jp->ps[i].status))
                                return (1);

        return (0);        
}
