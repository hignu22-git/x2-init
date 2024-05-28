#ifndef VERSION
#define VERSION "0.1"
#endif

/* Keep't simple ,stupid */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/kd.h>
#endif
#include <sys/resource.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#ifdef __FreeBSD__
#include <utmpx.h>
#else
#include <utmp.h>
#endif
#include <ctype.h>
#include <stdarg.h>
#include <sys/ttydefaults.h>
#include <sys/syslog.h>
#include <sys/time.h>
/*
 * rc.d
 */
#include <sys/types.h>
#include <dirent.h>

#ifdef WITH_SELINUX
#include <selinux/selinux.h>
#endif
#ifdef __FreeBSD__
extern char **environ;
#endif

#ifdef __i386__
#ifdef __GLIBC__
/* GNU libc 2.x */
#define STACK_DEBUG 1
#if (__GLIBC__ == 2 && __GLIBC_MINOR__ == 0)
/* Only glibc 2.0 needs this */
#include <sigcontext.h>
#elif (__GLIBC__ > 2) && ((__GLIBC__ == 2) && (__GLIBC_MINOR__ >= 1))
#include <bits/sigcontext.h>
#endif
#endif
#endif

#include "init.h"
#include "initreq.h"
#include "paths.h"
#include "reboot.h"
#include "runlevellog.h"
#include "set.h"
#include "x2init-struct.h"

#ifndef SIGPWR
#define SIGPWR SIGUSR2
#endif

#ifndef CBAUD
#define CBAUD 0
#endif
#ifndef CBAUDEX
#define CBAUDEX 0
#endif

/*  Sleep a number of milliseconds.
 *	This only works correctly because Linux select updates
 *	the elapsed time in the struct timeval passed to select! */
static void do_msleep(int msec) {
	struct timeval tv;
	tv.tv_sec = msec / 1000;
	tv.tv_usec = (msec % 1000) * 1000;
	while (select(0, NULL, NULL, NULL, &tv) < 0 && errno == EINTR) ;
}

/* Non-failing allocation routines (init cannot fail). */
static void *imalloc(size_t size) {
	void *m;
	while ((m = malloc(size)) == NULL) {
		initlog(L_VB, "out of memory");
		do_msleep(SHORT_SLEEP);
	} memset(m, 0, size);
	return m;
}

static char *istrdup(const char *s) {
	char *m;
	int l;
	l = strlen(s) + 1;
	m = imalloc(l);
	memcpy(m, s, l);
	return m;
}

/*
 *	Send the state info of the previous running init to
 *	the new one, in a version-independant way.
 */
static void send_state(int fd)
{
	FILE *fp;
	CHILD *p;
	int i, val;
	fp = fdopen(fd, "w");
	fprintf(fp, "VER%s\n", Version);
	fprintf(fp, "-RL%c\n", runlevel);
	fprintf(fp, "-TL%c\n", thislevel);
	fprintf(fp, "-PL%c\n", prevlevel);
	fprintf(fp, "-SI%u\n", got_signals);
	fprintf(fp, "-WR%d\n", wrote_wtmp_reboot);
	fprintf(fp, "-WU%d\n", wrote_utmp_reboot);
	fprintf(fp, "-ST%d\n", sleep_time);
	fprintf(fp, "-DB%d\n", did_boot);

	for (p = family; p; p = p->next)
	{
		fprintf(fp, "REC%s\n", p->id);
		fprintf(fp, "LEV%s\n", p->rlevel);
		for (i = 0, val = p->flags; flags[i].mask; i++)
			if (val & flags[i].mask)
			{
				val &= ~flags[i].mask;
				fprintf(fp, "FL %s\n", flags[i].name);
			}
		fprintf(fp, "PID%d\n", p->pid);
		fprintf(fp, "EXS%u\n", p->exstat);
		for (i = 0; actions[i].act; i++)
			if (actions[i].act == p->action)
			{
				fprintf(fp, "AC %s\n", actions[i].name);
				break;
			}
		fprintf(fp, "CMD%s\n", p->process);
		fprintf(fp, "EOR\n");
	}
	fprintf(fp, "END\n");
	fclose(fp);
}

/*
 *	Read a string from a file descriptor.
 *	Q: why not use fgets() ?
 *      A: Answer: looked into this. Turns out after all the checks
 *      required in the fgets() approach (removing newline, read errors, etc)
 *      the function is longer and takes approximately the same amount of
 *      time to do one big fgets and checks as it does to do a pile of getcs.
 *      We don't benefit from switching.
 *      - Jesse (From sysvinit )
 */
static int get_string(char *p, int size, FILE *f)
{
	int c;

	while ((c = getc(f)) != EOF && c != '\n')
	{
		if (--size > 0)
			*p++ = c;
	}
	*p = '\0';
	return (c != EOF) && (size > 0);
}

/*
 *	Read trailing data from the state pipe until we see a newline.
 */
static int get_void(FILE *f) {
	int c;
	while ((c = getc(f)) != EOF && c != '\n') ;
	return (c != EOF);
}

/*
 *	Read the next "command" from the state pipe.
 */
static int get_cmd(FILE *f) {
	char cmd[4] = "   ";
	int i;

	if (fread(cmd, 1, sizeof(cmd) - 1, f) != sizeof(cmd) - 1)
		return C_EOF;

	for (i = 0; cmds[i].cmd && strcmp(cmds[i].name, cmd) != 0; i++)
		;
	return cmds[i].cmd;
}

/*
 *	Read a CHILD * from the state pipe.
 */
static CHILD *get_record(FILE *f) {
	int cmd;
	char s[32];
	int i;
	CHILD *p;

	do
	{
		errno = 0;
		switch (cmd = get_cmd(f))
		{
		case C_END:
			get_void(f);
			return NULL;
		case 0:
			get_void(f);
			break;
		case C_REC:
			break;
		case D_RUNLEVEL:
			if (fscanf(f, "%c\n", &runlevel) == EOF && errno != 0)
			{
				fprintf(stderr, "%s (%d): %s\n", __FILE__, __LINE__, strerror(errno));
			}
			break;
		case D_THISLEVEL:
			if (fscanf(f, "%c\n", &thislevel) == EOF && errno != 0)
			{
				fprintf(stderr, "%s (%d): %s\n", __FILE__, __LINE__, strerror(errno));
			}
			break;
		case D_PREVLEVEL:
			if (fscanf(f, "%c\n", &prevlevel) == EOF && errno != 0)
			{
				fprintf(stderr, "%s (%d): %s\n", __FILE__, __LINE__, strerror(errno));
			}
			break;
		case D_GOTSIGN:
			if (fscanf(f, "%u\n", &got_signals) == EOF && errno != 0)
			{
				fprintf(stderr, "%s (%d): %s\n", __FILE__, __LINE__, strerror(errno));
			}
			break;
		case D_WROTE_WTMP_REBOOT:
			if (fscanf(f, "%d\n", &wrote_wtmp_reboot) == EOF && errno != 0)
			{
				fprintf(stderr, "%s (%d): %s\n", __FILE__, __LINE__, strerror(errno));
			}
			break;
		case D_WROTE_UTMP_REBOOT:
			if (fscanf(f, "%d\n", &wrote_utmp_reboot) == EOF && errno != 0)
			{
				fprintf(stderr, "%s (%d): %s\n", __FILE__, __LINE__, strerror(errno));
			}
			break;
		case D_SLTIME:
			if (fscanf(f, "%d\n", &sleep_time) == EOF && errno != 0)
			{
				fprintf(stderr, "%s (%d): %s\n", __FILE__, __LINE__, strerror(errno));
			}
			break;
		case D_DIDBOOT:
			if (fscanf(f, "%d\n", &did_boot) == EOF && errno != 0)
			{
				fprintf(stderr, "%s (%d): %s\n", __FILE__, __LINE__, strerror(errno));
			}
			break;
		case D_WROTE_WTMP_RLEVEL:
			if (fscanf(f, "%d\n", &wrote_wtmp_rlevel) == EOF && errno != 0)
			{
				fprintf(stderr, "%s (%d): %s\n", __FILE__, __LINE__, strerror(errno));
			}
			break;
		case D_WROTE_UTMP_RLEVEL:
			if (fscanf(f, "%d\n", &wrote_utmp_rlevel) == EOF && errno != 0)
			{
				fprintf(stderr, "%s (%d): %s\n", __FILE__, __LINE__, strerror(errno));
			}
			break;
		default:
			if (cmd > 0 || cmd == C_EOF)
			{
				oops_error = -1;
				return NULL;
			}
		}
	} while (cmd != C_REC);

	p = imalloc(sizeof(CHILD));
	get_string(p->id, sizeof(p->id), f);

	do
		switch (cmd = get_cmd(f))
		{
		case 0:
		case C_EOR:
			get_void(f);
			break;
		case C_PID:
			if (fscanf(f, "%d\n", &(p->pid)) == EOF && errno != 0)
			{
				fprintf(stderr, "%s (%d): %s\n", __FILE__, __LINE__, strerror(errno));
			}
			break;
		case C_EXS:
			if (fscanf(f, "%u\n", &(p->exstat)) == EOF && errno != 0)
			{
				fprintf(stderr, "%s (%d): %s\n", __FILE__, __LINE__, strerror(errno));
			}
			break;
		case C_LEV:
			get_string(p->rlevel, sizeof(p->rlevel), f);
			break;
		case C_PROCESS:
			get_string(p->process, sizeof(p->process), f);
			break;
		case C_FLAG:
			get_string(s, sizeof(s), f);
			for (i = 0; flags[i].name; i++)
			{
				if (strcmp(flags[i].name, s) == 0)
					break;
			}
			p->flags |= flags[i].mask;
			break;
		case C_ACTION:
			get_string(s, sizeof(s), f);
			for (i = 0; actions[i].name; i++)
			{
				if (strcmp(actions[i].name, s) == 0)
					break;
			}
			p->action = actions[i].act ? actions[i].act : OFF;
			break;
		default:
			free(p);
			oops_error = -1;
			return NULL;
		}
	while (cmd != C_EOR);

	return p;
}

/*
 *	Read the complete state info from the state pipe.
 *	Returns 0 on success
 */
static int receive_state(int fd)
{
	FILE *f;
	char old_version[256];
	CHILD **pp;

	f = fdopen(fd, "r");

	if (get_cmd(f) != C_VER)
	{
		fclose(f);
		return -1;
	}
	get_string(old_version, sizeof(old_version), f);
	oops_error = 0;
	for (pp = &family; (*pp = get_record(f)) != NULL; pp = &((*pp)->next))
		;
	fclose(f);
	return oops_error;
}

/*
 *	Set the process title.
 */
#ifdef __GNUC__
#ifndef __FreeBSD__
__attribute__((format(printf, 1, 2)))
#endif
#endif
/* This function already exists on FreeBSD. No need to declare it. */
#ifndef __FreeBSD__
static int
setproctitle(char *fmt, ...)
{
	va_list ap;
	int len;
	char buf[256];

	buf[0] = 0;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (maxproclen > 1)
	{
		memset(argv0, 0, maxproclen);
		strncpy(argv0, buf, maxproclen - 1);
	}

	return len;
}
#endif

/*
 *	Set console_dev to a working console.
 */
static void console_init(void) {
	int fd;
	int tried_devcons = 0;
	int tried_vtmaster = 0;
	char *s;

	if ((s = getenv("CONSOLE")) != NULL)		console_dev = s;
	else {
		console_dev = CONSOLE;
		tried_devcons++;
	}
	while ((fd = open(console_dev, O_RDONLY | O_NONBLOCK)) < 0)	{
		if (!tried_devcons)	{
			tried_devcons++;
			console_dev = CONSOLE;
			continue;
		}
		if (!tried_vtmaster) {
			tried_vtmaster++;
			console_dev = VT_MASTER;
			continue;
		}
		break;
	}
	if (fd < 0)		console_dev = "/dev/null";
	else			close(fd);
}

/*
 *	Open the console with retries.
 */
static int console_open(int mode)
{
	int f, fd = -1;
	int m;

	/*
	 *	Open device in nonblocking mode.
	 */
	m = mode | O_NONBLOCK;

	/*
	 *	Retry the open five times.
	 */
	for (f = 0; f < 5; f++)
	{
		if ((fd = open(console_dev, m)) >= 0)
			break;
		usleep(10000);
	}

	if (fd < 0)
		return fd;

	/*
	 *	Set original flags.
	 */
	if (m != mode)
		fcntl(fd, F_SETFL, mode);
	return fd;
}

/*
 *	We got a signal (HUP PWR WINCH ALRM INT)
 */
static void signal_handler(int sig)
{
	ADDSET(got_signals, sig);
}

/*
 *	SIGCHLD: one of our children has died.
 */
static
#ifdef __GNUC__
	void
	chld_handler(int sig __attribute__((unused)))
#else
	void
	chld_handler(int sig)
#endif
{
	CHILD *ch;
	int pid, st;
	int saved_errno = errno;

	/*
	 *	Find out which process(es) this was (were)
	 */
	while ((pid = waitpid(-1, &st, WNOHANG)) != 0)
	{
		if (errno == ECHILD)
			break;
		for (ch = family; ch; ch = ch->next)
			if (ch->pid == pid && (ch->flags & RUNNING))
			{
				INITDBG(L_VB,
						"chld_handler: marked %d as zombie",
						ch->pid);
				ADDSET(got_signals, SIGCHLD);
				ch->exstat = st;
				ch->flags |= ZOMBIE;
				if (ch->new)
				{
					ch->new->exstat = st;
					ch->new->flags |= ZOMBIE;
				}
				break;
			}
		if (ch == NULL)
		{
			INITDBG(L_VB, "chld_handler: unknown child %d exited.",
					pid);
		}
	}
	errno = saved_errno;
}

/*
 *	Linux ignores all signals sent to init when the
 *	SIG_DFL handler is installed. Therefore we must catch SIGTSTP
 *	and SIGCONT, or else they won't work....
 *
 *	The SIGCONT handler
 */
static
#ifdef __GNUC__
	void
	cont_handler(int sig __attribute__((unused)))
#else
	void
	cont_handler(int sig)
#endif
{
	got_cont = 1;
}

/*
 *	Fork and dump core in /.
 */
static void coredump(void)
{
	static int dumped = 0;
	struct rlimit rlim;
	sigset_t mask;

	if (dumped)
		return;
	dumped = 1;

	if (fork() != 0)
		return;

	sigfillset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);

	rlim.rlim_cur = RLIM_INFINITY;
	rlim.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_CORE, &rlim);
	if (0 != chdir("/"))
		initlog(L_VB, "unable to chdir to /: %s",
				strerror(errno));

	signal(SIGSEGV, SIG_DFL);
	raise(SIGSEGV);
	sigdelset(&mask, SIGSEGV);
	sigprocmask(SIG_SETMASK, &mask, NULL);

	do_msleep(SHORT_SLEEP);
	exit(0);
}

/*
 *	OOPS: segmentation violation!
 *	If we have the info, print where it occurred.
 *	Then sleep 30 seconds and try to continue.
 */
static
#if defined(STACK_DEBUG) && defined(__linux__)
#ifdef __GNUC__
	void
	segv_handler(int sig __attribute__((unused)), struct sigcontext ctx)
#else
	void
	segv_handler(int sig, struct sigcontext ctx)
#endif
{
	char *p = "";
	int saved_errno = errno;

	if ((void *)ctx.eip >= (void *)do_msleep &&
		(void *)ctx.eip < (void *)main)
		p = " (code)";
	initlog(L_VB, "PANIC: segmentation violation at %p%s! "
				  "sleeping for 30 seconds.",
			(void *)ctx.eip, p);
	coredump();
	do_msleep(LONG_SLEEP);
	errno = saved_errno;
}
#else
#ifdef __GNUC__
	void
	segv_handler(int sig __attribute__((unused)))
#else
	void
	segv_handler(int sig)
#endif
{
	int saved_errno = errno;

	initlog(L_VB, "PANIC: segmentation violation! sleeping for 30 seconds.");
	coredump();
	do_msleep(LONG_SLEEP);
	errno = saved_errno;
}
#endif

/*
 *	The SIGSTOP & SIGTSTP handler
 */
static
#ifdef __GNUC__
	void
	stop_handler(int sig __attribute__((unused)))
#else
	void
	stop_handler(int sig)
#endif
{
	int saved_errno = errno;

	got_cont = 0;
	while (!got_cont)
		pause();
	got_cont = 0;
	errno = saved_errno;
}

/*
 *	Set terminal settings to reasonable defaults
 */
static void console_stty(void)
{
	struct termios tty;
	int fd;

	if ((fd = console_open(O_RDWR | O_NOCTTY)) < 0) {
		initlog(L_VB, "can't open %s", console_dev);
		return;
	}

#ifdef __FreeBSD_kernel__
	/*
	 * The kernel of FreeBSD expects userland to set TERM.  Usually, we want
	 * "xterm".  Later, gettys might disagree on this (i.e. we're not using
	 * syscons) but some boot scripts, like /etc/init.d/xserver-xorg, still
	 * need a non-dumb terminal.
	 */
	putenv("TERM=xterm");
#endif

	(void)tcgetattr(fd, &tty);

	tty.c_cflag &= CBAUD | CBAUDEX | CSIZE | CSTOPB | PARENB | PARODD;
	tty.c_cflag |= HUPCL | CLOCAL | CREAD;

	tty.c_cc[VINTR] = CINTR;
	tty.c_cc[VQUIT] = CQUIT;
	tty.c_cc[VERASE] = CERASE; /* ASCII DEL (0177) */
	tty.c_cc[VKILL] = CKILL;
	tty.c_cc[VEOF] = CEOF;
	tty.c_cc[VTIME] = 0;
	tty.c_cc[VMIN] = 1;
#ifdef VSWTC /* not defined on FreeBSD */
	tty.c_cc[VSWTC] = _POSIX_VDISABLE;
#endif /* VSWTC */
	tty.c_cc[VSTART] = CSTART;
	tty.c_cc[VSTOP] = CSTOP;
	tty.c_cc[VSUSP] = CSUSP;
	tty.c_cc[VEOL] = _POSIX_VDISABLE;
	tty.c_cc[VREPRINT] = CREPRINT;
	tty.c_cc[VDISCARD] = CDISCARD;
	tty.c_cc[VWERASE] = CWERASE;
	tty.c_cc[VLNEXT] = CLNEXT;
	tty.c_cc[VEOL2] = _POSIX_VDISABLE;

	/*
	 *	Set pre and post processing
	 */
	tty.c_iflag = IGNPAR | ICRNL | IXON | IXANY
#ifdef IUTF8 /* Not defined on FreeBSD */
				  | (tty.c_iflag & IUTF8)
#endif /* IUTF8 */
		;
	tty.c_oflag = OPOST | ONLCR;
	tty.c_lflag = ISIG | ICANON | ECHO | ECHOCTL | ECHOE | ECHOKE;

#if defined(SANE_TIO) && (SANE_TIO == 1)
	/*
	 *	Disable flow control (-ixon), ignore break (ignbrk),
	 *	and make nl/cr more usable (sane).
	 */
	tty.c_iflag |= IGNBRK;
	tty.c_iflag &= ~(BRKINT | INLCR | IGNCR | IXON);
	tty.c_oflag &= ~(OCRNL | ONLRET);
#endif
	/*
	 *	Now set the terminal line.
	 *	We don't care about non-transmitted output data
	 *	and non-read input data.
	 */
	(void)tcsetattr(fd, TCSANOW, &tty);
	(void)tcflush(fd, TCIOFLUSH);
	(void)close(fd);
}

static ssize_t
safe_write(int fd, const char *buffer, size_t count)
{
	ssize_t offset = 0;

	while (count > 0)
	{
		ssize_t block = write(fd, &buffer[offset], count);

		if (block < 0 && errno == EINTR)
			continue;
		if (block <= 0)
			return offset ? offset : block;
		offset += block;
		count -= block;
	}
	return offset;
}

/*
 *	Print to the system console
 */
void print(char *s)
{
	int fd;

	if ((fd = console_open(O_WRONLY | O_NOCTTY | O_NDELAY)) >= 0)
	{
		safe_write(fd, s, strlen(s));
		close(fd);
	}
}

/*	Log something to a logfile and the console. */
#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif
void  
initlog(int loglevel, char *s, ...)  {
	va_list va_alist;
	char buf[256];
	sigset_t nmask, omask;

	va_start(va_alist, s);
	vsnprintf(buf, sizeof(buf), s, va_alist);
	va_end(va_alist);

	if (loglevel & L_SY) {
		/*	Re-establish connection with syslogd every time.
		 *	Block signals while talking to syslog. */
		sigfillset(&nmask);
		sigprocmask(SIG_BLOCK, &nmask, &omask);
		openlog("x2-init", 0, LOG_DAEMON);
		syslog(LOG_INFO, "%s", buf);
		closelog();
		sigprocmask(SIG_SETMASK, &omask, NULL);
	} 
	/*	And log to the console. */
	if (loglevel & L_CO) {
		print("\rX2-INiT => ");
		print(buf);
		print("\r\n");
	}   
}

/*	Add or replace specific environment value */
int addnewenv(const char *new, char **curr, int n)
{
	size_t nlen = strcspn(new, "=");
	int i;
	for (i = 0; i < n; i++)
	{
		if (nlen != strcspn(curr[i], "="))
			continue;
		if (strncmp(new, curr[i], nlen) == 0)
			break;
	}
	if (i >= n)
		curr[n++] = istrdup(new);
	else
	{
		free(curr[i]);
		curr[i] = istrdup(new);
	}
	return n;
}

/*
 *	Build a new environment for execve().
 */
char **init_buildenv(int child)
{
	char i_lvl[] = "RUNLEVEL=x";
	char i_prev[] = "PREVLEVEL=x";
	char i_cons[128];
	char i_shell[] = "SHELL=" SHELL;
	char **e;
	int n, i;

	for (n = 0; environ[n]; n++)
		;
	n += NR_EXTRA_ENV + 1; /* Also room for last NULL */
	if (child)
		n += 8;

	while ((e = (char **)calloc(n, sizeof(char *))) == NULL)
	{
		initlog(L_VB, "out of memory");
		do_msleep(SHORT_SLEEP);
	}

	for (n = 0; environ[n]; n++)
		e[n] = istrdup(environ[n]);

	for (i = 0; i < NR_EXTRA_ENV; i++)
	{
		if (extra_env[i] == NULL || *extra_env[i] == '\0')
			continue;
		n = addnewenv(extra_env[i], e, n);
	}

	if (child)
	{
		snprintf(i_cons, sizeof(i_cons), "CONSOLE=%s", console_dev);
		i_lvl[9] = thislevel;
		i_prev[10] = prevlevel;
		n = addnewenv(i_shell, e, n);
		n = addnewenv(i_lvl, e, n);
		n = addnewenv(i_prev, e, n);
		n = addnewenv(i_cons, e, n);
		n = addnewenv(E_VERSION, e, n);
	}

	e[n++] = NULL;

	return e;
}

void init_freeenv(char **e)
{
	int n;

	for (n = 0; e[n]; n++)
		free(e[n]);
	free(e);
}

/*
 *	Fork and execute.
 *
 *	This function is too long and indents too deep.
 *
 */
static pid_t 
spawn(CHILD *ch, int *res) {
	char *args[16];			  /* Argv array */
	char buf[136];			  /* Line buffer */
	int f, st;				  /* Scratch variables */
	char *ptr;				  /* Ditto */
	time_t t;				  /* System time */
	int oldAlarm;			  /* Previous alarm value */
	char *proc = ch->process; /* Command line */
	pid_t pid, pgrp;		  /* child, console process group. */
	sigset_t nmask, omask;	  /* For blocking SIGCHLD */
	struct sigaction sa;

	*res = -1;
	buf[sizeof(buf) - 1] = 0;

	/* Skip '+' if it's there */
	if (proc[0] == '+')	proc++;

	ch->flags |= XECUTED;

	if (ch->action == RESPAWN || ch->action == ONDEMAND)
	{
		/* Is the date stamp from less than 2 minutes ago? */
		time(&t);
		if (ch->tm + TESTTIME > t)
		{
			ch->count++;
		}
		else
		{
			ch->count = 0;
			ch->tm = t;
		}

		/* Do we try to respawn too fast? */
		if (ch->count >= MAXSPAWN)
		{

			initlog(L_VB,
					"Id \"%s\" respawning too fast: disabled for %d minutes",
					ch->id, SLEEPTIME / 60);
			ch->flags &= ~RUNNING;
			ch->flags |= FAILING;

			/* Remember the time we stopped */
			ch->tm = t;

			/* Try again in 5 minutes */
			oldAlarm = alarm(0);
			if (oldAlarm > SLEEPTIME || oldAlarm <= 0)
				oldAlarm = SLEEPTIME;
			alarm(oldAlarm);
			return (-1);
		}
	}

	/* See if there is an "initscript" (except in single user mode). */
	if (access(INITSCRIPT, R_OK) == 0 && runlevel != 'S')
	{
		/* Build command line using "initscript" */
		args[1] = SHELL;
		args[2] = INITSCRIPT;
		args[3] = ch->id;
		args[4] = ch->rlevel;
		args[5] = "unknown";
		for (f = 0; actions[f].name; f++)
		{
			if (ch->action == actions[f].act)
			{
				args[5] = actions[f].name;
				break;
			}
		}
		if (proc[0] == '@')
			proc++; /*skip leading backslash */
		args[6] = proc;
		args[7] = NULL;
	}
	else if ((strpbrk(proc, "~`!$^&*()=|\\{}[];\"'<>?")) && (proc[0] != '@'))
	{
		/* See if we need to fire off a shell for this command */
		/* Do not launch shell if first character in proc string is an at symbol  */
		/* Give command line to shell */
		args[1] = SHELL;
		args[2] = "-c";
		strcpy(buf, "exec ");
		strncat(buf, proc, sizeof(buf) - strlen(buf) - 1);
		args[3] = buf;
		args[4] = NULL;
	}
	else
	{
		/* Split up command line arguments */
		buf[0] = 0;
		if (proc[0] == '@')
			proc++;
		strncat(buf, proc, sizeof(buf) - 1);
		ptr = buf;
		for (f = 1; f < 15; f++)
		{
			/* Skip white space */
			while (*ptr == ' ' || *ptr == '\t')
				ptr++;
			args[f] = ptr;

		/* May be trailing space.. */
			if (*ptr == 0)
				break;

			/* Skip this `word' */
			while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '#')
				ptr++;

			/* If end-of-line, break */
			if (*ptr == '#' || *ptr == 0)
			{
				f++;
				*ptr = 0;
				break;
			}
			/* End word with \0 and continue */
			*ptr++ = 0;
		}
		args[f] = NULL;
	}
	args[0] = args[1];
	while (1)
	{
		/*
		 *	Block sigchild while forking.
		 */
		sigemptyset(&nmask);
		sigaddset(&nmask, SIGCHLD);
		sigprocmask(SIG_BLOCK, &nmask, &omask);

		if ((pid = fork()) == 0)
		{

			close(0);
			close(1);
			close(2);
			if (pipe_fd >= 0)
			{
				close(pipe_fd);
				pipe_fd = -1;
			}

			sigprocmask(SIG_SETMASK, &omask, NULL);

			/*
			 *	In sysinit, boot, bootwait or single user mode:
			 *	for any wait-type subprocess we _force_ the console
			 *	to be its controlling tty.
			 */
			if (strchr("*#sS", runlevel) && ch->flags & WAITING)
			{
				int ftty; /* Handler for tty controlling */
				/*
				 *	We fork once extra. This is so that we can
				 *	wait and change the process group and session
				 *	of the console after exit of the leader.
				 */
				setsid();
				if ((ftty = console_open(O_RDWR | O_NOCTTY)) >= 0)
				{
					/* Take over controlling tty by force */
					(void)ioctl(ftty, TIOCSCTTY, 1);

					if (dup(ftty) < 0)
					{
						initlog(L_VB, "cannot duplicate console fd");
					}

					if (dup(ftty) < 0)
					{
						initlog(L_VB, "cannot duplicate console fd");
					}
				}

				/*
				 * 4 Sep 2001, Andrea Arcangeli:
				 * Fix a race in spawn() that is used to deadlock init in a
				 * waitpid() loop: must set the childhandler as default before forking
				 * off the child or the chld_handler could run before the waitpid loop
				 * has a chance to find its zombie-child.
				 */
				SETSIG(sa, SIGCHLD, SIG_DFL, SA_RESTART);
				if ((pid = fork()) < 0)
				{
					initlog(L_VB, "cannot fork: %s",
							strerror(errno));
					exit(1);
				}
				if (pid > 0)
				{
					pid_t rc;
					/*
					 *	Ignore keyboard signals etc.
					 *	Then wait for child to exit.
					 */
					SETSIG(sa, SIGINT, SIG_IGN, SA_RESTART);
					SETSIG(sa, SIGTSTP, SIG_IGN, SA_RESTART);
					SETSIG(sa, SIGQUIT, SIG_IGN, SA_RESTART);

					while ((rc = waitpid(pid, &st, 0)) != pid)
						if (rc < 0 && errno == ECHILD)
							break;

					/*
					 *	Small optimization. See if stealing
					 *	controlling tty back is needed.
					 */
					pgrp = tcgetpgrp(ftty);
					if (pgrp != getpid())
						exit(0);

					/*
					 *	Steal controlling tty away. We do
					 *	this with a temporary process.
					 */
					if ((pid = fork()) < 0)
					{
						initlog(L_VB, "cannot fork: %s",
								strerror(errno));
						exit(1);
					}
					if (pid == 0)
					{
						setsid();
						(void)ioctl(ftty, TIOCSCTTY, 1);
						exit(0);
					}
					while ((rc = waitpid(pid, &st, 0)) != pid)
						if (rc < 0 && errno == ECHILD)
							break;
					exit(0);
				}

				/* Set ioctl settings to default ones */
				console_stty();
			}
			else
			{ /* parent */
				int fd;
				setsid();
				if ((fd = console_open(O_RDWR | O_NOCTTY)) < 0)
				{
					initlog(L_VB, "open(%s): %s", console_dev,
							strerror(errno));
					fd = open("/dev/null", O_RDWR);
				}

				if (dup(fd) < 0)
				{
					initlog(L_VB, "cannot duplicate /dev/null fd");
				}

				if (dup(fd) < 0)
				{
					initlog(L_VB, "cannot duplicate /dev/null fd");
				}
			}

			/*
			 * Update utmp/wtmp file prior to starting
			 * any child.  This MUST be done right here in
			 * the child process in order to prevent a race
			 * condition that occurs when the child
			 * process' time slice executes before the
			 * parent (can and does happen in a uniprocessor
			 * environment).  If the child is a getty and
			 * the race condition happens, then init's utmp
			 * update will happen AFTER the getty runs
			 * and expects utmp to be updated already!
			 *
			 * Do NOT log if process field starts with '+'
			 * This is for compatibility with *very*
			 * old getties - probably it can be taken out.
			 */
			if (ch->process[0] != '+')
				write_utmp_wtmp("", ch->id, getpid(), INIT_PROCESS, "");

			/* Reset all the signals, set up environment */
			for (f = 1; f < NSIG; f++)
				SETSIG(sa, f, SIG_DFL, SA_RESTART);
			environ = init_buildenv(1);

			/*
			 *	Execute prog. In case of ENOEXEC try again
			 *	as a shell script.
			 */
			execvp(args[1], args + 1);
			if (errno == ENOEXEC)
			{
				args[1] = SHELL;
				args[2] = "-c";
				strcpy(buf, "exec ");
				strncat(buf, proc, sizeof(buf) - strlen(buf) - 1);
				args[3] = buf;
				args[4] = NULL;
				execvp(args[1], args + 1);
			}
			initlog(L_VB, "cannot execute \"%s\"", args[1]);

			if (ch->process[0] != '+')
				write_utmp_wtmp("", ch->id, getpid(), DEAD_PROCESS, NULL);
			exit(1);
		}
		*res = pid;
		sigprocmask(SIG_SETMASK, &omask, NULL);

		INITDBG(L_VB, "Started id %s (pid %d)", ch->id, pid);

		if (pid == -1)
		{
			initlog(L_VB, "cannot fork, retry..");
			do_msleep(SHORT_SLEEP);
			continue;
		}
		return (pid);
	}
}

/*	Start a child running! */
static void startup(CHILD *ch)
{
	/* See if it's disabled */
	if (ch->flags & FAILING)	return;
	switch (ch->action)	{
	case SYSINIT:
	case BOOTWAIT:
	case WAIT:
	case POWERWAIT:
	case POWERFAILNOW:
	case POWEROKWAIT:
	case CTRLALTDEL:
		if (!(ch->flags & XECUTED))	ch->flags |= WAITING;
	case KBREQUEST:
	case BOOT:
	case POWERFAIL:
	case ONCE:
		if (ch->flags & XECUTED)	break;
	case ONDEMAND:
	case RESPAWN:
		ch->flags |= RUNNING;
		(void)spawn(ch, &(ch->pid));
		break;
	}
}

#ifdef __linux__
static void check_kernel_console()	{
	FILE *fp;
	char buf[4096];
	if ((fp = fopen("/proc/cmdline", "r")) == 0) return;
	if (fgets(buf, sizeof(buf), fp))	{
		char *p = buf;
		if (strstr(p, "init.autocon=1"))	{
			while ((p = strstr(p, "console=")))	{
				char *e;
				p += strlen("console=");
				for (e = p; *e; ++e)	{
					switch (*e)	{
					case '-' ... '9':
					case 'A' ... 'Z':
					case '_':
					case 'a' ... 'z':
						continue;
					}
					break;
				}
				if (p != e)	{
					CHILD *old;
					int dup = 0;
					char id[8] = {0};
					char dev[32] = {0};
					strncpy(dev, p, MIN(sizeof(dev), (unsigned)(e - p)));
					if (!strncmp(dev, "tty", 3))	strncpy(id, dev + 3, sizeof(id));
					else							strncpy(id, dev, sizeof(id));
					for (old = newFamily; old; old = old->next)	{
						if (!strcmp(old->id, id))	dup = 1;
					}
					if (!dup) {
						CHILD *ch = imalloc(sizeof(CHILD));
						ch->action = RESPAWN;
						strcpy(ch->id, id);
						strcpy(ch->rlevel, "2345");
						sprintf(ch->process, "/sbin/agetty -L -s 115200,38400,9600 %s vt102", dev);
						ch->next = NULL;
						for (old = family; old; old = old->next)	{
							if (strcmp(old->id, ch->id) == 0)		{
								old->new = ch;
								break;
							}
						}
						/* add to end */
						for (old = newFamily; old; old = old->next)	{
							if (!old->next)	{
								old->next = ch;
								break;
							}
						}	initlog(L_VB, "added agetty on %s with id %s", dev, id);
					}
				}
			}
		}
	}
	fclose(fp);
	return;
}

#endif
#if RXRC_ENABLE


pid_t 
rst_xrcCall( pid_t pPid ,char* path ,FILE* pf, FILE* pss, char* tmp0res, char* cmd_str){ /* onRestart->onFailure->newPr */
	xrcParam *paramX ;

	if (pPid == 0){   /* Done . */
		while (get_string ( path, sizeof(path), pss )) { strcat(tmp0res, path); }
		
		if ( !strcmp(tmp0res ,"")) {		
			pclose(pf);		
			pf  = popen( cmd_str,"r");					
		}
								
		if (( pss || pf ) || ( pss && pf )) {
			pclose(pss);
			pclose(pf );
		}
	}
	else if(pPid == -1){
		exit(1);
	}	exit(0); 
}


#else
#endif
/*		
	READ xRC-dir
*/

int 
r_xrc(void){
#if RXRC_ENABLE

	FILE* pf,*fp_x  	;
	FILE* pss2, *pss	;
	
	pid_t reg_rst_errn  ;
	char  buf [256]   	;
	DIR*  tabdir		;
	char *p				;
	short done 			;
	int lineNo			;
	struct dirent *file_entry; 	/* rc.d entry */
	char *mainSEP ,endLine ; 	/* setarators */
	char f_name[272]	;		/* size d_name + strlen /etc/rc.d/ */
	xrcParam  *paramX   ;
	char *cmd_str[1035] ;
	char *cmd_st2[1035] ;
	char **buff         ;
	short break_v       ;

	char *path ,*tmp0res ;
	CHILD *ch /*,*old, *i */	; 		/* Pointers to CHILD structure */
	/*struct sigaction sa ; */
	pid_t pet            ; 

	initlog(L_CO,"x2 Start r_Xrc-fn 01  ");
	if ((tabdir = opendir(XRCD)) == NULL) 
		/*w_journal2x(L_XI,J1_INIT,J2_ERROR,"xrc.d is not found !" );*/
	while(done!=1)	{					/*	reading files in buffer	*/
		if(tabdir != NULL) {
			if((file_entry = readdir(tabdir)) != NULL) 
			{
				/*
				 * ignore files not like *.xrc
				 */		initlog( L_CO ,"x2 open directory in r_xrc " );
				if (!strcmp(file_entry->d_name, ".") || !strcmp(file_entry->d_name, "..")) 
					continue;
				if (strlen (file_entry->d_name)  < 5 || 
					strcmp (file_entry->d_name + strlen(file_entry->d_name) - 4, ".xrc")) 
					continue;
				/* 
				 * initialize filename 
				 */
				memset(f_name, 0, sizeof(char) * 272) ;
				snprintf(f_name, 272, "/etc/xrc.d/%s ", file_entry->d_name);
				initlog(L_VB, "Reading: %s", f_name) ;
			
				if ((fp_x= fopen(f_name ,"r")) == NULL)  		continue ;
				while (get_string(buf, sizeof(buf), fp_x) ) { 	lineNo++ ;
					for (p = buf; /* *p == ' ' || *p == '\t'*/ ; p++)    ;
					if (*p != '#' && *p != '\n') break                   ;
					buff[lineNo - 1] = p ;	
					/* 
					 * Parser
					 * Type => [Variable(mainSEP) ] : [State(endLine) ] ; \n 
					 */
					mainSEP = strsep(&p,":");
					endLine = strsep(&p,";");
					if(!strcmp(mainSEP, "BIN")) {
						paramX->bin = endLine ;
					} else if(!strcmp(mainSEP, "PARAM")) {	 
						sprintf(paramX->param , " " ,endLine);
					} else if (!strcmp(mainSEP, "NEED-FOR-START")) { /* for exec unit with N program.  */
						paramX->nfs = endLine ;
					}else if (!strcmp(mainSEP, "CONSOLE-STAT")) {  /* mode for unit restart */
						if (strcmp(endLine, "")) {
							paramX->wait_rt = (int*)endLine ;
						}
					} else if(!strcmp(mainSEP, "RESTART-ON") ){
						paramX->onRestart = endLine ;
					} else if(!strcmp(mainSEP, "TARGET")) {  /*This is a final argument in end file */
						if (!strcmp(endLine, ch->rlevel)) 
						{	
						    if (paramX->nfs) {
								char *sum00 ;
								char *pfps = "ps -eo comm  | grep "	;
								short d = -1;

								sprintf(sum00, pfps, paramX->nfs );
								pss = popen( sum00, "r" ) ;
								while (d = 2) {
									break_v = 1;
									if ( !strcmp(pss,NULL) || !strcmp(pss," ") || !strcmp(pss,"")) d = 1 ;
									else {
										d =2 ;
										sprintf(cmd_str ,paramX->bin ,paramX->param );
										system (cmd_str); 
									}
								}
								pclose(pss)		;
							}

							/* if on RST */
							if(paramX->onRestart) { 
								/*
								 *	onfailure    - if program is crash 
								 *  sigstop      - if send signal sigstop 
								 */
								break_v == 1;
								char *cmdp = "ps -eo comm | grep ";
								
								sprintf(cmd_str ,paramX->bin ,paramX->param);
								sprintf(cmd_st2 , cmdp ,paramX->bin        );

								if  ( !strcmp(paramX->onRestart,"sigstop"   ) ){
									system(cmd_str);
									pss = popen( cmd_st2 ,"r");
									if (  !strcmp(pss, "")) 
										kill( paramX->pid ,SIGCONT );
									if (pf) pclose(pss);
								}
								else if( !strcmp(paramX->onRestart,"onFailure") ){	
									pf  = popen( cmd_str, "r" );
									pss = popen( cmd_st2, "r" );
									/*  Calls  */
									pet = vfork() ;
									reg_rst_errn= rst_xrcCall(pet ,path ,pf ,pss ,tmp0res ,cmd_str) ;
									if (reg_rst_errn= -1){
										initlog(L_VB,"ERROR => xrc->rst->onFailure !!!!");
									}
								}	
							}
							sprintf(cmd_str ,paramX->bin ,paramX->param); /* noch einmal für UB */
							if  (break_v == 0) system(cmd_str); 
							
							paramX->rlevel = endLine;
						}
					}	
				}	
				if (strlen(p) == 0) continue;									
				if (fp_x)			fclose(fp_x);	
			} else {   
				if (tabdir) closedir(tabdir);
				done = 1;
				continue;
			}
		} else {	done = 1; continue; }
	}				
#else 
#endif
	return 0;
}		
/* 
 * Read the inittab file. 
 */
static void
read_inittab(void) {
	FILE *fp;			 		/* The INITTAB file */
	FILE *fp_tab;		 		/* The INITTABD files */
	CHILD *head = NULL;	 		/* Head of linked list */
#ifdef INITLVL
	struct stat st; 			/* To stat INITLVL */
#endif
	sigset_t nmask, omask; 		/* For blocking SIGCHLD. */
	char buf[256];		   		/* Line buffer */
	char err[64];		   		/* Error message. */
	char *id, *rlevel, *action, *process; 		/* Fields of a line */
	char *p;
	int lineNo = 0;			   	/* Line number in INITTAB file */
	int actionNo;			   	/* Decoded action field */
	int16_t f;				    /* Counter */
	int round;				   	/* round 0 for SIGTERM, 1 for SIGKILL */
	int foundOne = 0;		   	/* No killing no sleep */
	int talk;				   	/* Talk to the user */
	int done = -1;			   	/* Ready yet? , 2 level : -1 nothing done,
		0 inittab done, 1 inittab and inittab.d done */
	DIR *tabdir = NULL;		   	/* the rc.d dir */
	struct dirent *file_entry; 	/* rc.d entry */
	char f_name[272];		   	/* size d_name + strlen /etc/rc.d/ */ 
	CHILD *ch, *old, *i; 		/* Pointers to CHILD structure */

#if DEBUG
	if (newFamily != NULL)
	{
		INITDBG(L_VB, "PANIC newFamily != NULL");
		exit(1);
	}
	INITDBG(L_VB, "Reading inittab");
#endif
	/* OPEN INITTAB READ */
	if ((fp = fopen(INITTAB, "r")) == NULL) 	initlog(L_VB, "No inittab file found");
	/* OPEN RC.D */
	if ((tabdir = opendir(INITTABD)) == NULL)	initlog(L_VB, "No rc.d directory found");
	/* Schleife *
	 * done !=1 */
	while (done != 1) {
		if (done == -1) {
			if (fp == NULL || fgets(buf, sizeof(buf), fp) == NULL) { 	done = 0;
				for (old = newFamily; old; old = old->next) 
					if (strpbrk(old->rlevel, "S")) break;
				if (old == NULL) snprintf(buf, sizeof(buf), "~~:S:wait:%s\n", SULOGIN);
				else continue;
			}
		} else if (done == 0)  {
			/*-------- parse /etc/rc.d and read all .tab files --------*/
			if(tabdir != NULL) {
				if((file_entry = readdir(tabdir)) != NULL) {
					/* ignore files not like *.tab */
					if (!strcmp(file_entry->d_name, ".") || !strcmp(file_entry->d_name, "..")) continue;
					if (strlen (file_entry->d_name) 	 < 5 || 
						strcmp (file_entry->d_name + strlen(file_entry->d_name) - 4, ".tab")) continue;
					/* initialize filename */
					memset(f_name, 0, sizeof(char) * 272) ;
					snprintf(f_name, 272, "/etc/rc.d/%s ", file_entry->d_name);
					initlog(L_VB, "Reading: %s", f_name) ;
					/* read file in rc.d only one entry per file */
					if ((fp_tab= fopen(f_name ,"r")) == NULL)  continue;
					/* read the file while the line contain comment */
					while (fgets(buf, sizeof(buf), fp_tab) != NULL) {
						for (p = buf; *p == ' ' || *p == '\t'; p++) ;
						if (*p != '#' && *p != '\n') break ;
					}
					fclose(fp_tab);
					/* do some checks */
					if (strlen(p) == 0) continue;
				} else {
					/* end of readdir, all is done */
					done = 1;
					continue;
				}
			} else {
				done = 1;
				continue;
			}
		} lineNo++;
		/* Skip comments and empty lines */
		for (p = buf; *p == ' ' || *p == '\t'; p++) ; /*skip empty lines */
		if  (*p == '#' || *p == '\n')	continue; 
		/* Decode the fields */
		id = 		strsep(&p, ":");
		rlevel = 	strsep(&p, ":");
		action = 	strsep(&p, ":");
		process = 	strsep(&p, "\n");
		/*	Check if syntax is OK. Be very verbose here, to
		 *	avoid newbie postings on comp.os.linux.setup :) */
		err[0] = 0;
		if (!id || !*id)			strcpy(err, "missing id field");
		if (!rlevel)				strcpy(err, "missing runlevel field");
		if (!process)				strcpy(err, "missing process field");
		if (!action || !*action)	strcpy(err, "missing action field");
		if (id && strlen(id) > sizeof(utproto.ut_id)) 
			sprintf(err, "id field too long (max %d characters)", (int)sizeof(utproto.ut_id));
		if (rlevel  && strlen(rlevel) > 11) 
			strcpy(err, "rlevel field too long (max 11 characters)");
		if (process && strlen(process)>127) 
			strcpy(err,"process field too long (max 127 characters)");
		if (action  && strlen(action) > 32) 
			strcpy(err, "action field too long");
		if (err[0] != 0) {
			initlog(L_VB, " %s[%d]: %s ", INITTAB, lineNo, err);
			INITDBG(L_VB, "%s:%s:%s:%s", id, rlevel, action, process);
			continue;
		}
		/* Decode the "action" field */
		actionNo = -1;
		for (f = 0; actions[f].name; f++) {
			if( strcasecmp(action, actions[f].name) == 0) {
				actionNo = actions[f].act ;
				break;
			}
		}
		if (actionNo == -1) {
			initlog(L_VB, "%s[%d]: %s: unknown action field", INITTAB, lineNo, action);
			continue;
		}
		/*	See if the id field is unique \/ */
		for (old = newFamily; old; old = old->next) {
			if (strcmp(old->id, id) == 0 && strcmp(id, "~~")) {
				initlog(L_VB, "%s[%d]: duplicate ID field \"%s\"", INITTAB, lineNo, id);
				break;
			}
		}	
		if (old) continue;
		ch = imalloc(sizeof(CHILD)); /* -- Allocate the << CHILD >> struct */
		ch->action = actionNo;
		strncpy(ch->id, id, sizeof(utproto.ut_id) + 1); /* Hack for different libs. */
		strncpy(ch->process, process, sizeof(ch->process) - 1) ; 
		if (rlevel[0]) {
			for (f = 0; f < (int16_t)sizeof(rlevel) - 1 && rlevel[f]; f++) {
				ch->rlevel[f] = rlevel[f] ;
				if (ch->rlevel[f] == 's') ch->rlevel[f] = 'S'; /* if small key-register */
			} strncpy(ch->rlevel, rlevel, sizeof(ch->rlevel) - 1);
		} else {
			strcpy(ch->rlevel, "0123456789");
			if (ISPOWER(ch->action)) strcpy(ch->rlevel, "S0123456789");
		}
		if (ch->action == SYSINIT )							strcpy(ch->rlevel, RSYSINIT	);
		if (ch->action == BOOT || ch->action == BOOTWAIT) 	strcpy(ch->rlevel, RBOOT	);
		/*	Now add it to the linked list. Special for powerfail. */
		if (ISPOWER(ch->action)) {
			/*	Disable by default */
			ch->flags |= XECUTED;
			/*	Tricky: insert at the front of the list ... */
			old = NULL;
			for (i = newFamily; i; i = i->next) {
				if (!ISPOWER(i->action)) break;
				old = i;
			}
			/* Now add after entry "old" */
			if (old) {
				ch->next = i;
				old->next = ch;
				if (i == NULL) head = ch;
			} else {
				ch->next = newFamily;
				newFamily = ch;
				if (ch->next == NULL) head = ch;
			}
		} else {	/*	Just add at end of the list */
			if (ch->action == KBREQUEST)	ch->flags |= XECUTED;
			ch->next = NULL;
			if (head)  	head->next = ch;
			else 		newFamily = ch;
			head = ch;
		}
		/*	Walk through the old list comparing id fields */
		for (old = family; old; old = old->next) {
			if (strcmp(old->id, ch->id) == 0) {
				old->new = ch;
				break;
			}
		}
	}
	/*	We're done. */
	if (fp) fclose(fp);
	if (tabdir) closedir(tabdir);
#ifdef __linux__
	check_kernel_console();
#endif

	/*	Loop through the list of children, and see if they need to be killed. */
	INITDBG(L_VB, "Checking for children to kill");
	for (round = 0; round < 2; round++) {
		talk = 1;
		for (ch = family; ch; ch = ch->next) {
			ch->flags &= ~KILLME;
			if (ch->new == NULL) ch->flags |= KILLME; /* Is this line deleted ? */
 			/*	If the entry has changed, kill it anyway. Note that
			 *	we do not check ch->process, only the "action" field.
			 *	This way, you can turn an entry "off" immediately, but
			 *	changes in the command line will only become effective
			 *	after the running version has exited. */
			if (ch->new && ch->action != ch->new->action) ch->flags |= KILLME;
			/*	Only BOOT processes may live in all levels */
			if (ch->action != BOOT && strchr(ch->rlevel, runlevel) == NULL) {
				/*	Ondemand procedures live always, except in single user   */
				if (runlevel == 'S' || !(ch->flags & DEMAND)) ch->flags |= KILLME;
			}
			/* Now, if this process may live note so in the new list */
			if ((ch->flags & KILLME) == 0) {
				ch->new->flags = ch->flags;
				ch->new->pid = ch->pid;
				ch->new->exstat = ch->exstat;
				continue;
			}
			/* 	Is this process still around? */
			if ((ch->flags & RUNNING) == 0) {
				ch->flags &= ~KILLME;
				continue;
			}
			INITDBG(L_VB, "Killing \"%s\"", ch->process);
			if (round == 0 ) {		
				/* Send TERM signal */
				if (talk)
					initlog(L_CO,"Sending processes configured via /etc/inittab the TERM signal");
				kill(-(ch->pid), SIGTERM);
				foundOne = 1;
				/*	See if we have to wait sleep_time seconds */
				if (foundOne) {
					for (f = 0; f < 100 * sleep_time; f++){
						for (ch = family; ch; ch = ch->next){
							if (!(ch->flags & KILLME)) 							continue;
							if ((ch->flags & RUNNING) && !(ch->flags & ZOMBIE)) break;
						}
						if (ch == NULL) {
							round = 1;
							foundOne = 0; 
							break;
						}
						do_msleep(MINI_SLEEP);
					}
				}		
			}
			else if (round == 1) {	/* Send KILL signal and collect status */
				if (talk)
					initlog(L_CO,"Sending processes configured via /etc/inittab the KILL signal");
				kill(-(ch->pid), SIGKILL);
			}	talk = 0;
		}
	}
	/*	Now give all processes the chance to die and collect exit statuses. */
	if (foundOne)	do_msleep(MINI_SLEEP);
	for (ch = family; ch; ch = ch->next) {
		if (ch->flags & KILLME) {
			if (!(ch->flags & ZOMBIE)) initlog(L_CO, "Pid %d [id %s] seems to hang", ch->pid, ch->id);
			else {
				INITDBG(L_VB, "Updating utmp for pid %d[id %s]",ch->pid, ch->id);
				ch->flags &= ~RUNNING;
				if (ch->process[0] != '+')	
					write_utmp_wtmp("", ch->id, ch->pid, DEAD_PROCESS, NULL);
			}
		}
	}
	/*	Both rounds done; clean up the list. */
	sigemptyset(&nmask);
	sigaddset(&nmask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &nmask, &omask);
	for (ch = family; ch; ch = old) {
		old = ch->next;
		free(ch);
	}
	family = newFamily;
	for (ch = family; ch; ch = ch->next) ch->new = NULL;
	newFamily = NULL;
	sigprocmask(SIG_SETMASK, &omask, NULL);

#ifdef INITLVL
	/*	Dispose of INITLVL file. */
	if (lstat(INITLVL, &st) >= 0 && S_ISLNK(st.st_mode)) {
		/*	INITLVL is a symbolic link, so just truncate the file */
		close(open(INITLVL, O_WRONLY | O_TRUNC));
	} else {
		/* Delete INITLVL file. */
		unlink(INITLVL);
	}
#endif
#ifdef INITLVL2
	/*	Dispose of INITLVL2 file. */
	if (lstat(INITLVL2, &st) >= 0 && S_ISLNK(st.st_mode)) {
		/*	INITLVL2 is a symbolic link, so just truncate the file.  */
		close(open(INITLVL2, O_WRONLY | O_TRUNC));
	} else {
		/*	Delete INITLVL2 file. */
		unlink(INITLVL2);
	}
#endif
r_xrc();		}

/*
 *	Walk through the family list and start up children.
 *	The entries that do not belong here at all are removed
 *	from the list.
 */
static void start_if_needed(void)	{
	CHILD *ch;	/* Pointer to child */
	int delete; /* Delete this entry from list? */

	INITDBG(L_VB, "Checking for children to start");
	for (ch = family; ch; ch = ch->next)	{

#if DEBUG
		if (ch->rlevel[0] == 'C')	
			INITDBG(L_VB, "%s: flags %d", ch->process, ch->flags);
#endif

		/* Are we waiting for this process? Then quit here. */
		if (ch->flags & WAITING)	break;
		/* Already running? OK, don't touch it */
		if (ch->flags & RUNNING)	continue;

		/* See if we have to start it up */
		delete = 1;
		if (strchr(ch->rlevel, runlevel) ||
			((ch->flags & DEMAND) && !strchr("#*Ss", runlevel))) {
			startup(ch);
			delete = 0;
		}
		if (delete)	{
			/* is this OK? */
			ch->flags &= ~(RUNNING | WAITING);
			if (!ISPOWER(ch->action) && ch->action != KBREQUEST)
				ch->flags &= ~XECUTED;
			ch->pid = 0;
		}	else
			/* Do we have to wait for this process? */
			if (ch->flags & WAITING)	break;
	}
}

/*
 *	Ask the user on the console for a runlevel
 */
static int ask_runlevel(void)
{
	const char prompt[] = "\nEnter runlevel: ";
	char buf[8];
	int lvl = -1;
	int fd;

	console_stty();
	fd = console_open(O_RDWR | O_NOCTTY);

	if (fd < 0)
		return ('S');

	while (!strchr("0123456789S", lvl))
	{
		safe_write(fd, prompt, sizeof(prompt) - 1);
		if (read(fd, buf, sizeof(buf)) <= 0)
			buf[0] = 0;
		if (buf[0] != 0 && (buf[1] == '\r' || buf[1] == '\n'))
			lvl = buf[0];
		if (islower(lvl))
			lvl = toupper(lvl);
	}
	close(fd);
	return lvl;
}






/*
 *	Search the INITTAB file for the 'initdefault' field, with the default
 *	runlevel. If this fails, ask the user to supply a runlevel.
 */
static int get_init_default(void)
{
	CHILD *ch;
	int lvl = -1;
	char *p;

	/*
	 *	Look for initdefault.
	 */
	for (ch = family; ch; ch = ch->next)
		if (ch->action == INITDEFAULT)
		{
			p = ch->rlevel;
			while (*p)
			{
				if (*p > lvl)
					lvl = *p;
				p++;
			}
			break;
		}
	/*
	 *	See if level is valid
	 */
	if (lvl > 0)
	{
		if (islower(lvl))
			lvl = toupper(lvl);
		if (strchr("0123456789S", lvl) == NULL)
		{
			initlog(L_VB,
					"Initdefault level '%c' is invalid", lvl);
			lvl = 0;
		}
	}
	/*
	 *	Ask for runlevel on console if needed.
	 */
	if (lvl <= 0)
		lvl = ask_runlevel();

	/*
	 *	Log the fact that we have a runlevel now.
	 */
	Write_Runlevel_Log(lvl);
	return lvl;
}

/*
 *	We got signaled.
 *
 *	Do actions for the new level. If we are compatible with
 *	the "old" INITLVL and arg == 0, try to read the new
 *	runlevel from that file first.
 */
static int read_level(int arg)
{
	CHILD *ch;				 /* Walk through list */
	unsigned char foo = 'X'; /* Contents of INITLVL */
	int ok = 1;
#ifdef INITLVL
	FILE *fp;
	struct stat stt;
	int st;
#endif

	if (arg)
		foo = arg;

#ifdef INITLVL
	ok = 0;

	if (arg == 0)
	{
		fp = NULL;
		if (stat(INITLVL, &stt) != 0 || stt.st_size != 0L)
			fp = fopen(INITLVL, "r");
#ifdef INITLVL2
		if (fp == NULL &&
			(stat(INITLVL2, &stt) != 0 || stt.st_size != 0L))
			fp = fopen(INITLVL2, "r");
#endif
		if (fp == NULL)   /* Don*t open */
		{
			/* INITLVL file empty or not there - act as 'init q' */
			initlog(L_SY, "Re-reading inittab");
			return (runlevel);
		}
		ok = fscanf(fp, "%c %d", &foo, &st);
		fclose(fp);
	}
	else
	{
		/* We go to the new runlevel passed as an argument. */
		foo = arg;
		ok = 1;
	}
	if (ok == 2)
		sleep_time = st;

#endif /* INITLVL */

	if (islower(foo))
		foo = toupper(foo);
	if (ok < 1 || ok > 2 || strchr("QS0123456789ABCU", foo) == NULL)
	{
		initlog(L_VB, "bad runlevel: %c", foo);
		return runlevel;
	}

	/* Log this action */
	switch (foo)
	{
	case 'S':
		initlog(L_VB, "Going single user");
		break;
	case 'Q':
		initlog(L_SY, "Re-reading inittab");
		break;
	case 'A':
	case 'B':
	case 'C':
		initlog(L_SY,
				"Activating demand-procedures for '%c'", foo);
		break;
	case 'U':
		initlog(L_SY, "Trying to re-exec init");
		return 'U';
	default:
		initlog(L_VB, "Switching to runlevel: %c", foo);
	}

	if (foo == 'Q')
	{
#if defined(SIGINT_ONLYONCE) && (SIGINT_ONLYONCE == 1)
		/* Re-enable signal from keyboard */
		struct sigaction sa;
		SETSIG(sa, SIGINT, signal_handler, 0);
#endif
		return runlevel;
	}

	/* Check if this is a runlevel a, b or c */
	if (strchr("ABC", foo))
	{
		if (runlevel == 'S')
			return (runlevel);

		/* Read inittab again first! */
		read_inittab();
		/* Read xrc.d now */
		r_xrc();

		/* Mark those special tasks */
		for (ch = family; ch; ch = ch->next)
			if (strchr(ch->rlevel, foo) != NULL ||
				strchr(ch->rlevel, tolower(foo)) != NULL)
			{
				ch->flags |= DEMAND;
				ch->flags &= ~XECUTED;
				INITDBG(L_VB,
						"Marking (%s) as ondemand, flags %d",
						ch->id, ch->flags);
			}
		return runlevel;
	}

	/* Store both the old and the new runlevel. */
	wrote_utmp_rlevel = 0;
	wrote_wtmp_rlevel = 0;
	write_utmp_wtmp("runlevel", "~~", foo + 256 * runlevel, RUN_LVL, "~");
	thislevel = foo;
	prevlevel = runlevel;
	Write_Runlevel_Log(runlevel);
	return foo;
}







/*
 *	This procedure is called after every signal (SIGHUP, SIGALRM..)
 *
 *	Only clear the 'failing' flag if the process is sleeping
 *	longer than 5 minutes, or inittab was read again due
 *	to user interaction.
 */
static void fail_check(void)
{
	CHILD *ch;			   /* Pointer to child structure */
	time_t t;			   /* System time */
	time_t next_alarm = 0; /* When to set next alarm */

	time(&t);

	for (ch = family; ch; ch = ch->next)
	{

		if (ch->flags & FAILING)
		{
			/* Can we free this sucker? */
			if (ch->tm + SLEEPTIME < t)
			{
				ch->flags &= ~FAILING;
				ch->count = 0;
				ch->tm = 0;
			}
			else
			{
				/* No, we'll look again later */
				if (next_alarm == 0 ||
					ch->tm + SLEEPTIME > next_alarm)
					next_alarm = ch->tm + SLEEPTIME;
			}
		}
	}
	if (next_alarm)
	{
		next_alarm -= t;
		if (next_alarm < 1)
			next_alarm = 1;
		alarm(next_alarm);
	}
}

/* Set all 'Fail' timers to 0 */
static void fail_cancel(void)
{
	CHILD *ch;

	for (ch = family; ch; ch = ch->next)
	{
		ch->count = 0;
		ch->tm = 0;
		ch->flags &= ~FAILING;
	}
}

/*
 *	Start up powerfail entries.
 */
static void do_power_fail(int pwrstat)
{
	CHILD *ch;

	/*
	 *	Tell powerwait & powerfail entries to start up
	 */
	for (ch = family; ch; ch = ch->next)
	{
		if (pwrstat == 'O')
		{
			/*
			 *	The power is OK again.
			 */
			if (ch->action == POWEROKWAIT)
				ch->flags &= ~XECUTED;
		}
		else if (pwrstat == 'L')
		{
			/*
			 *	Low battery, shut down now.
			 */
			if (ch->action == POWERFAILNOW)
				ch->flags &= ~XECUTED;
		}
		else
		{
			/*
			 *	Power is failing, shutdown imminent
			 */
			if (ch->action == POWERFAIL || ch->action == POWERWAIT)
				ch->flags &= ~XECUTED;
		}
	}
}

/*
 *	Check for state-pipe presence
 */
static int check_pipe(int fd)
{
	struct timeval t;
	fd_set s;
	char signature[8];

	FD_ZERO(&s);
	FD_SET(fd, &s);
	t.tv_sec = t.tv_usec = 0;

	if (select(fd + 1, &s, NULL, NULL, &t) != 1)
		return 0;
	if (read(fd, signature, 8) != 8)
		return 0;
	return strncmp(Signature, signature, 8) == 0;
}

/*
 *	 Make a state-pipe.
 */
static int make_pipe(int fd)
{
	int fds[2];

	if (pipe(fds))
	{
		initlog(L_VB, "pipe: %m");
		return -1;
	}
	dup2(fds[0], fd);
	close(fds[0]);
	fcntl(fds[1], F_SETFD, 1);
	fcntl(fd, F_SETFD, 0);
	safe_write(fds[1], Signature, 8);

	return fds[1];
}

/*
 *	Attempt to re-exec.
 *      Renaming to my_re_exec since re_exec is now a common function name
 *      which conflicts.
 */
static void my_re_exec(void)
{
	CHILD *ch;
	sigset_t mask, oldset;
	pid_t pid;
	char **env;
	int fd;

	if (strchr("S0123456", runlevel) == NULL)
		return;

	/*
	 *	Reset the alarm, and block all signals.
	 */
	alarm(0);
	sigfillset(&mask);
	sigprocmask(SIG_BLOCK, &mask, &oldset);

	/*
	 *	construct a pipe fd --> STATE_PIPE and write a signature
	 */
	if ((fd = make_pipe(STATE_PIPE)) < 0)
	{
		sigprocmask(SIG_SETMASK, &oldset, NULL);
		initlog(L_CO, "Attempt to re-exec failed");
	}

	fail_cancel();
	if (pipe_fd >= 0)
		close(pipe_fd);
	pipe_fd = -1;
	DELSET(got_signals, SIGCHLD);
	DELSET(got_signals, SIGHUP);
	DELSET(got_signals, SIGUSR1);
	DELSET(got_signals, SIGUSR2);

	/*
	 *	That should be cleaned.
	 */
	for (ch = family; ch; ch = ch->next)
		if (ch->flags & ZOMBIE)
		{
			INITDBG(L_VB, "Child died, PID= %d", ch->pid);
			ch->flags &= ~(RUNNING | ZOMBIE | WAITING);
			if (ch->process[0] != '+')
				write_utmp_wtmp("", ch->id, ch->pid, DEAD_PROCESS, NULL);
		}

	if ((pid = fork()) == 0)
	{
		/*
		 *	Child sends state information to the parent.
		 */
		send_state(fd);
		exit(0);
	}

	/*
	 *	The existing init process execs a new init binary.
	 */
	env = init_buildenv(0);
	execle(myname, myname, "--init", NULL, env);

	/*
	 *	We shouldn't be here, something failed.
	 *	Close the state pipe, unblock signals and return.
	 */
	init_freeenv(env);
	close(fd);
	close(STATE_PIPE);
	sigprocmask(SIG_SETMASK, &oldset, NULL);
	initlog(L_CO, "Attempt to re-exec failed");
}

/*
 *	Redo utmp/wtmp entries if required or requested
 *	Check for written records and size of utmp
 */
static void redo_utmp_wtmp(void)
{
	struct stat ustat;
	const int ret = stat(UTMP_FILE, &ustat);

	if ((ret < 0) || (ustat.st_size == 0))
		wrote_utmp_rlevel = wrote_utmp_reboot = 0;

	if ((wrote_wtmp_reboot == 0) || (wrote_utmp_reboot == 0))
		write_utmp_wtmp("reboot", "~~", 0, BOOT_TIME, "~");

	if ((wrote_wtmp_rlevel == 0) || (wrote_utmp_rlevel == 0))
		write_utmp_wtmp("runlevel", "~~", thislevel + 256 * prevlevel, RUN_LVL, "~");
}

/*
 *	We got a change runlevel request through the
 *	init.fifo. Process it.
 */
static void fifo_new_level(int level)
{
#if CHANGE_WAIT
	CHILD *ch;
#endif
	int oldlevel;

	if (level == runlevel)
		return;

#if CHANGE_WAIT
	/* Are we waiting for a child? */
	for (ch = family; ch; ch = ch->next)
		if (ch->flags & WAITING)
			break;
	if (ch == NULL)
#endif
	{
		/* We need to go into a new runlevel */
		oldlevel = runlevel;
		runlevel = read_level(level);
		if (runlevel == 'U')
		{
			runlevel = oldlevel;
			my_re_exec();
		}
		else
		{
			if (oldlevel != 'S' && runlevel == 'S')
				console_stty();
			if (runlevel == '6' || runlevel == '0' ||
				runlevel == '1')
				console_stty();
			if (runlevel > '1' && runlevel < '6')
				redo_utmp_wtmp();
			read_inittab();
			fail_cancel();
			setproctitle("init [%c]", (int)runlevel);
		}
	}
	Write_Runlevel_Log(runlevel);
}

/*
 *	Set/unset environment variables. The variables are
 *	encoded as KEY=VAL\0KEY=VAL\0\0. With "=VAL" it means
 *	setenv, without it means unsetenv.
 */
static void initcmd_setenv(char *data, int size)
{
	char *env, *p, *e;
	size_t sz;
	int i, eq;

	e = data + size;

	while (*data && data < e)
	{
		for (p = data; *p && p < e; p++)
			;
		if (*p)
			break;
		env = data;
		data = ++p;

		/*
		 *	We only allow INIT_* to be set.
		 */
		if (strncmp(env, "INIT_", 5) != 0)
			continue;

		sz = strcspn(env, "=");
		eq = (env[sz] == '=');

		/*initlog(L_SY, "init_setenv: %s, %d, %d", env, eq, sz);*/

		/* Free existing vars. */
		for (i = 0; i < NR_EXTRA_ENV; i++)
		{
			if (extra_env[i] == NULL)
				continue;
			if (sz != strcspn(extra_env[i], "="))
				continue;
			if (strncmp(extra_env[i], env, sz) == 0)
			{
				free(extra_env[i]);
				extra_env[i] = NULL;
			}
		}

		if (eq == 0)
			continue;

		/* Set new vars if needed. */
		for (i = 0; i < NR_EXTRA_ENV; i++)
		{
			if (extra_env[i] == NULL)
			{
				extra_env[i] = istrdup(env);
				break;
			}
		}
	}
}

/*
 *	Read from the init FIFO. Processes like telnetd and rlogind can
 *	ask us to create login processes on their behalf.
 */
static void check_init_fifo(void)
{
	struct init_request request;
	struct timeval tv;
	struct stat st, st2;
	fd_set fds;
	int n;
	int quit = 0;

	/*
	 *	First, try to create /run/initctl if not present.
	 */
	if (stat(INIT_FIFO, &st2) < 0 && errno == ENOENT)
		(void)mkfifo(INIT_FIFO, 0600);

	/*
	 *	If /run/initctl is open, stat the file to see if it
	 *	is still the _same_ inode.
	 */
	if (pipe_fd >= 0)
	{
		fstat(pipe_fd, &st);
		if (stat(INIT_FIFO, &st2) < 0 ||
			st.st_dev != st2.st_dev ||
			st.st_ino != st2.st_ino)
		{
			close(pipe_fd);
			pipe_fd = -1;
		}
	}

	/*
	 *	Now finally try to open /run/initctl if pipe_fd is -1
	 *    if it is -2, then we leave it closed
	 */
	if (pipe_fd == -1)
	{
		if ((pipe_fd = open(INIT_FIFO, O_RDWR | O_NONBLOCK)) >= 0)
		{
			fstat(pipe_fd, &st);
			if (!S_ISFIFO(st.st_mode))
			{
				initlog(L_VB, "%s is not a fifo", INIT_FIFO);
				close(pipe_fd);
				pipe_fd = -1;
			}
		}
		if (pipe_fd >= 0)
		{
			/*
			 *	Don't use fd's 0, 1 or 2.
			 */
			(void)dup2(pipe_fd, PIPE_FD);
			close(pipe_fd);
			pipe_fd = PIPE_FD;

			/*
			 *	Return to caller - we'll be back later.
			 */
		}
	}

	/* Wait for data to appear, _if_ the pipe was opened. */
	if (pipe_fd >= 0)
	{
		while (!quit)
		{

			/* Do select, return on EINTR. */
			FD_ZERO(&fds);
			FD_SET(pipe_fd, &fds);
			tv.tv_sec = 5;
			tv.tv_usec = 0;
			n = select(pipe_fd + 1, &fds, NULL, NULL, &tv);
			if (n <= 0)
			{
				if (n == 0 || errno == EINTR)
					return;
				continue;
			}

			/* Read the data, return on EINTR. */
			n = read(pipe_fd, &request, sizeof(request));
			if (n == 0)
			{
				/*
				 *	End of file. This can't happen under Linux (because
				 *	the pipe is opened O_RDWR - see select() in the
				 *	kernel) but you never know...
				 */
				close(pipe_fd);
				pipe_fd = -1;
				return;
			}
			if (n <= 0)
			{
				if (errno == EINTR)
					return;
				initlog(L_VB, "error reading initrequest");
				continue;
			}

			/*
			 *	This is a convenient point to also try to
			 *	find the console device or check if it changed.
			 */
			console_init();

			/*
			 *	Process request.
			 */
			if (request.magic != INIT_MAGIC || n != sizeof(request))
			{
				initlog(L_VB, "got bogus initrequest");
				continue;
			}
			switch (request.cmd)
			{
			case INIT_CMD_RUNLVL:
				sleep_time = request.sleeptime;
				fifo_new_level(request.runlevel);
				quit = 1;
				break;
			case INIT_CMD_POWERFAIL:
				sleep_time = request.sleeptime;
				do_power_fail('F');
				quit = 1;
				break;
			case INIT_CMD_POWERFAILNOW:
				sleep_time = request.sleeptime;
				do_power_fail('L');
				quit = 1;
				break;
			case INIT_CMD_POWEROK:
				sleep_time = request.sleeptime;
				do_power_fail('O');
				quit = 1;
				break;
			case INIT_CMD_SETENV:
				initcmd_setenv(request.i.data, sizeof(request.i.data));
				break;
			default:
				initlog(L_VB, "got unimplemented initrequest.");
				break;
			} /* end of switch */
		}	  /* end of while loop not quitting */
	}		  /* end of if the pipe is open */
	/*
	 *	We come here if the pipe couldn't be opened.
	 */
	if (pipe_fd == -1)
		pause();
}

/*
 *	This function is used in the transition
 *	sysinit (-> single user) boot -> multi-user.
 */
static void boot_transitions()
{
	CHILD *ch;
	static int newlevel = 0;
	static int warn = 1;
	int loglevel;
	int oldlevel;

	/* Check if there is something to wait for! */
	for (ch = family; ch; ch = ch->next)
		if ((ch->flags & RUNNING) && ch->action != BOOT)
			break;

	if (ch == NULL)
	{
		/* No processes left in this level, proceed to next level. */
		loglevel = -1;
		oldlevel = 'N';
		switch (runlevel)
		{
		case '#': /* SYSINIT -> BOOT */
			INITDBG(L_VB, "SYSINIT -> BOOT");

			/* Write a boot record. */
			wrote_utmp_reboot = 0;
			wrote_wtmp_reboot = 0;
			write_utmp_wtmp("reboot", "~~", 0, BOOT_TIME, "~");

			/* Get our run level */
			newlevel = dfl_level ? dfl_level : get_init_default();
			if (newlevel == 'S')
			{
				runlevel = newlevel;
				/* Not really 'S' but show anyway. */
				setproctitle("init [S]");
			}
			else
				runlevel = '*';
			break;
		case '*': /* BOOT -> NORMAL */
			INITDBG(L_VB, "BOOT -> NORMAL");
			if (runlevel != newlevel)
				loglevel = newlevel;
			runlevel = newlevel;
			did_boot = 1;
			warn = 1;
			break;
		case 'S': /* Ended SU mode */
		case 's':
			INITDBG(L_VB, "END SU MODE");
			newlevel = get_init_default();
			if (!did_boot && newlevel != 'S')
				runlevel = '*';
			else
			{
				if (runlevel != newlevel)
					loglevel = newlevel;
				runlevel = newlevel;
				oldlevel = 'S';
			}
			warn = 1;
			for (ch = family; ch; ch = ch->next)
				if (strcmp(ch->rlevel, "S") == 0)
					ch->flags &= ~(FAILING | WAITING | XECUTED);
			break;
		default:
			if (warn)
				initlog(L_VB,
						"no more processes left in this runlevel");
			warn = 0;
			loglevel = -1;
			if (got_signals == 0)
				check_init_fifo();
			break;
		}
		if (loglevel > 0)
		{
			initlog(L_VB, "Entering runlevel: %c", runlevel);
			wrote_utmp_rlevel = 0;
			wrote_wtmp_rlevel = 0;
			write_utmp_wtmp("runlevel", "~~", runlevel + 256 * oldlevel, RUN_LVL, "~");
			thislevel = runlevel;
			prevlevel = oldlevel;
			setproctitle("init [%c]", (int)runlevel);
		}
		Write_Runlevel_Log(runlevel);
	}
}

/*
 *	Init got hit by a signal. See which signal it is,
 *	and act accordingly.
 */
static void process_signals()
{
	CHILD *ch;
	int pwrstat;
	int oldlevel;
	int fd;
	char c;

	if (ISMEMBER(got_signals, SIGPWR))
	{
		INITDBG(L_VB, "got SIGPWR");
		/* See _what_ kind of SIGPWR this is. */
		pwrstat = 0;
		if ((fd = open(PWRSTAT, O_RDONLY)) >= 0)
		{
			if (read(fd, &c, 1) != 1)
				c = 0;
			pwrstat = c;
			close(fd);
			unlink(PWRSTAT);
		}
		else if ((fd = open(PWRSTAT_OLD, O_RDONLY)) >= 0)
		{
			/* Path changed 2010-03-20.  Look for the old path for a while. */
			initlog(L_VB, "warning: found obsolete path %s, use %s instead",
					PWRSTAT_OLD, PWRSTAT);
			if (read(fd, &c, 1) != 1)
				c = 0;
			pwrstat = c;
			close(fd);
			unlink(PWRSTAT_OLD);
		}
		do_power_fail(pwrstat);
		DELSET(got_signals, SIGPWR);
	}

	if (ISMEMBER(got_signals, SIGINT))
	{
#if defined(SIGINT_ONLYONCE) && (SIGINT_ONLYONCE == 1)
		/* Ignore any further signal from keyboard */
		struct sigaction sa;
		SETSIG(sa, SIGINT, SIG_IGN, SA_RESTART);
#endif
		INITDBG(L_VB, "got SIGINT");
		/* Tell ctrlaltdel entry to start up */
		for (ch = family; ch; ch = ch->next)
			if (ch->action == CTRLALTDEL)
				ch->flags &= ~XECUTED;
		DELSET(got_signals, SIGINT);
	}

	if (ISMEMBER(got_signals, SIGWINCH))
	{
		INITDBG(L_VB, "got SIGWINCH");
		/* Tell kbrequest entry to start up */
		for (ch = family; ch; ch = ch->next)
			if (ch->action == KBREQUEST)
				ch->flags &= ~XECUTED;
		DELSET(got_signals, SIGWINCH);
	}

	if (ISMEMBER(got_signals, SIGALRM))
	{
		INITDBG(L_VB, "got SIGALRM");
		/* The timer went off: check it out */
		DELSET(got_signals, SIGALRM);
	}

	if (ISMEMBER(got_signals, SIGCHLD))
	{
		INITDBG(L_VB, "got SIGCHLD");
		/* First set flag to 0 */
		DELSET(got_signals, SIGCHLD);

		/* See which child this was */
		for (ch = family; ch; ch = ch->next)
			if (ch->flags & ZOMBIE)
			{
				INITDBG(L_VB, "Child died, PID= %d", ch->pid);
				ch->flags &= ~(RUNNING | ZOMBIE | WAITING);
				if (ch->process[0] != '+')
					write_utmp_wtmp("", ch->id, ch->pid, DEAD_PROCESS, NULL);
			}
	}

	if (ISMEMBER(got_signals, SIGHUP))
	{
		INITDBG(L_VB, "got SIGHUP");
#if CHANGE_WAIT
		/* Are we waiting for a child? */
		for (ch = family; ch; ch = ch->next)
			if (ch->flags & WAITING)
				break;
		if (ch == NULL)
#endif
		{
			/* We need to go into a new runlevel */
			oldlevel = runlevel;
#ifdef INITLVL
			runlevel = read_level(0);
#endif
			if (runlevel == 'U')
			{
				runlevel = oldlevel;
				my_re_exec();
			}
			else
			{
				if (oldlevel != 'S' && runlevel == 'S')
					console_stty();
				if (runlevel == '6' || runlevel == '0' ||
					runlevel == '1')
					console_stty();
				read_inittab();
				fail_cancel();
				setproctitle("init [%c]", (int)runlevel);
				DELSET(got_signals, SIGHUP);
			}
			Write_Runlevel_Log(runlevel);
		}
	}
	if (ISMEMBER(got_signals, SIGUSR1))
	{
		/*
		 *	SIGUSR1 means close and reopen /run/initctl
		 */
		INITDBG(L_VB, "got SIGUSR1");
		if (pipe_fd)
			close(pipe_fd);
		pipe_fd = -1;
		DELSET(got_signals, SIGUSR1);
	}
	else if (ISMEMBER(got_signals, SIGUSR2))
	{
		/* SIGUSR1 mean close the pipe and leave it closed */
		INITDBG(L_VB, "got SIGUSR2");
		if (pipe_fd)
			close(pipe_fd);
		pipe_fd = -2;
		DELSET(got_signals, SIGUSR2);
	}
}

/******************\
\*	THE MAIN LOOP */
/******************\
\******************/
static void init_main(void) {
	CHILD *ch;
	struct sigaction sa;
	sigset_t sgt;
	int f, st;

	if (!reload) {
#if INITDEBUG
		/* Fork so we can debug the init process.*/
		if ((f = fork()) > 0) {
			static const char killmsg[] = "PRNT: init killed.\r\n";
			pid_t rc;
			while ((rc = wait(&st)) != f) if (rc < 0 && errno == ECHILD) break;
			safe_write(1, killmsg, sizeof(killmsg) - 1);
			while (1) pause();
		}
#endif /* Tell the kernel to send us SIGINT when CTRL-ALT-DEL \
is pressed, and that we want to handle keyboard signals. */
#ifdef __linux__
		init_reboot(BMAGIC_SOFT);
		if ((f = open(VT_MASTER, O_RDWR | O_NOCTTY)) >= 0) {
			(void)ioctl(f, KDSIGACCEPT, SIGWINCH);
			close(f);
		} else (void)ioctl(0, KDSIGACCEPT, SIGWINCH);
#endif
		/* Ignore all signals.*/
		for (f = 1; f <= NSIG; f++)	SETSIG(sa, f, SIG_IGN, SA_RESTART);
	}
	SETSIG(sa, SIGALRM, signal_handler, 0);
	SETSIG(sa, SIGHUP, signal_handler, 0);
	SETSIG(sa, SIGINT, signal_handler, 0);
	SETSIG(sa, SIGCHLD, chld_handler, SA_RESTART);
	SETSIG(sa, SIGPWR, signal_handler, 0);
	SETSIG(sa, SIGWINCH, signal_handler, 0);
	SETSIG(sa, SIGUSR1, signal_handler, 0);
	SETSIG(sa, SIGUSR2, signal_handler, 0);
	SETSIG(sa, SIGSTOP, stop_handler, SA_RESTART);
	SETSIG(sa, SIGTSTP, stop_handler, SA_RESTART);
	SETSIG(sa, SIGCONT, cont_handler, SA_RESTART);
	SETSIG(sa, SIGSEGV, (void (*)(int))segv_handler, SA_RESTART);
	console_init(); /* /dev/null ;/dev/console - start */

	if (!reload) {
		int fd; /* Close whatever files are open, and reset the console. */
		close(0);
		close(1);
		close(2);
		console_stty(); /* start tty-s */
		setsid();
		/*	Set default PATH variable.*/
		setenv("PATH", PATH_DEFAULT, 1 /* Overwrite */);
		/*	Initialize /var/run/utmp (only works if /var is on root and mounted rw) */
		if ((fd = open(UTMP_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644)) >= 0) close(fd);
		/*	Say hello to the world */
		initlog(L_CO, bootmsg, "booting");
		/*	See if we have to start an emergency shell. */
		if (emerg_shell) {
			pid_t rc;
			SETSIG(sa, SIGCHLD, SIG_DFL, SA_RESTART);
			if (spawn(&ch_emerg, &f) > 0) {
				while ((rc = wait(&st)) != f) 
					if (rc < 0 && errno == ECHILD) break;
			} 	SETSIG(sa, SIGCHLD, chld_handler, SA_RESTART);
		} 
		/* Start normal boot procedure */
		runlevel = '#';
		/*inittab */read_inittab() ;
	}
	else {
		/* Restart: unblock signals and let the show go on */
		initlog(L_CO, bootmsg, "reloading");
		sigfillset(&sgt);
		sigprocmask(SIG_UNBLOCK, &sgt, NULL);

		/* Set default PATH variable. */
		setenv("PATH", PATH_DEFAULT, 0 /* Don't overwrite */);
	} start_if_needed();
	while (1) {
		/* See if we need to make the boot transitions. */
		boot_transitions();
		INITDBG(L_VB, "init_main: waiting..");
		/* Check if there are processes to be waited on. */
		for (ch = family; ch; ch = ch->next) 
			if ((ch->flags & RUNNING) && ch->action != BOOT) break;
#if CHANGE_WAIT
		/* Wait until we get hit by some signal. */
		while (ch != NULL && got_signals == 0) {
			if (ISMEMBER(got_signals, SIGHUP)) {
				/* See if there are processes to be waited on. */
				for (ch = family; ch; ch = ch->next) if (ch->flags & WAITING) break;
			}
			if (ch != NULL) check_init_fifo();
		}
#else  /* CHANGE_WAIT */
		if (ch != NULL && got_signals == 0)	check_init_fifo();
#endif /* CHANGE_WAIT */
		/* Check the 'failing' flags */				fail_check();
		/* Process any signals. */					process_signals();
		/* See what we need to start up (again) */	start_if_needed();
	} /*NOTREACHED*/
}
static void 
usage(char *s){
	fprintf(stderr, 
		"Usage X2-INiT:\n command => %s{-e VAR[=VAL]|[-t SECONDS]{0|1|2|3|4|5|6|S|s|Q|q|A|a|B|b|C|c|U|u}}\n AUTHOR: Andre Bobrovskiy [hignu22] \n\n", s);
	exit(1);
}
static int
x2init(char *progname, int argc, char **argv) {

#ifdef TELINIT_USES_INITLVL
	FILE *fp;
#endif
	struct 	init_request request		;
	struct 	sigaction sa				;
	int 	f, fd, l					;
	char 	*env = NULL					;

	memset(&request, 0, sizeof(request));
	request.magic = INIT_MAGIC;

	while ((f = getopt(argc, argv, "t:e:")) != EOF)
		switch (f) {
		case 't':
			sleep_time = atoi(optarg);
			break;
		case 'e':
			if (env == NULL) env = request.i.data;
			l = strlen(optarg);
			if (env + l + 2 > request.i.data + sizeof(request.i.data)) {
				fprintf(stderr, "%s: -e option data " "too large\n", progname);
				exit(1);
			}
			memcpy(env, optarg, l);
			env += l;
			*env++ = 0;
			break;
		default:
			usage(progname);
			break;
		}

	if (env) *env++ = 0;
	if (env) {
		if (argc != optind) usage(progname);
		request.cmd = INIT_CMD_SETENV;
	}
	else {
		if (argc - optind != 1 || strlen(argv[optind]) != 1)    usage(progname);
		if (!strchr("0123456789SsQqAaBbCcUu", argv[optind][0])) usage(progname);
		request.cmd = INIT_CMD_RUNLVL;
		request.runlevel = argv[optind][0];
		request.sleeptime = sleep_time;
	}
	/* Change to the root directory. */
	if (0 != chdir("/")) initlog(L_VB, "unable to chdir to / : %s", strerror(errno));

	/* Open the fifo and write a command. */
	/* Make sure we don't hang on opening /run/initctl */
	SETSIG(sa, SIGALRM, signal_handler, 0);
	alarm(3);
	if ((fd = open(INIT_FIFO, O_WRONLY)) >= 0) {
		ssize_t p = 0;
		size_t s = sizeof(request);
		void *ptr = &request;
		while (s > 0) {
			p = write(fd, ptr, s);
			if (p < 0) {
				if (errno == EINTR || errno == EAGAIN) continue;
				break;
			}
			ptr += p;
			s -= p;
		}
		close(fd);
		alarm(0);
		return 0;
	}

#ifdef TELINIT_USES_INITLVL
	if (request.cmd == INIT_CMD_RUNLVL) {
		/* Fallthrough to the old method. */
		/* Now write the new runlevel. */
		if ((fp = fopen(INITLVL, "w")) == NULL) {
			fprintf(stderr, "%s: cannot create %s\n", progname, INITLVL);
			exit(1);
		}
		fprintf(fp, "%s %d", argv[optind], sleep_time);
		fclose(fp);
		/* And tell init about the pending runlevel change. */
		if (kill(INITPID, SIGHUP) < 0) perror(progname);
		return 0;
	}
#endif
	fprintf(stderr, "%s: ", progname);
	if (ISMEMBER(got_signals, SIGALRM)) 
		fprintf(stderr, "timeout opening/writing control channel %s\n", INIT_FIFO);
	else  perror(INIT_FIFO);
	return 1;
}

/* PRE BOOT OPERATIONS */
int 
main(int argc, char **argv) { 
	char *p;
	int f;
	int isinit;
#ifdef WITH_SELINUX
	int enforce = 0;
#endif
	/* Get my own name */
	if 	((p = strrchr(argv[0], '/')) != NULL) p++;
	else p = argv[0];
	if 	( (argc == 2) && (!strcmp(argv[1], "--version")) 
		|| (argc == 2) && (!strcmp(argv[1], "vers"    )) ) {
		printf("x2-init version: 0.1 \n");
		exit(0);
	}
	/* Common umask */
	umask(umask(077) | 022);
	/* Quick check */
	if (geteuid() != 0) {
		fprintf(stderr, "%s: must be doas/sudo/su :( .\n", p);
		exit(1);
	}
	/* Is this x2init or init ? */
	isinit = (getpid() == 1);
	for (f = 1; f < argc; f++) {
		if (!strcmp(argv[f], "-i") || !strcmp(argv[f], "--init")) {
			isinit = 1;
			break;
		}
	}
	if (!isinit) exit(x2init(p, argc, argv));
	/*	Check for re-exec */
	if (check_pipe(STATE_PIPE)) {
		receive_state(STATE_PIPE);
		myname = istrdup(argv[0]);
		argv0 = argv[0];
		maxproclen = 0;
		for (f = 0; f < argc; f++)
			maxproclen += strlen(argv[f]) + 1;
		reload = 1;
		setproctitle("init [%c]", (int)runlevel);
		init_main();
	}

	/* Check command line arguments */
	maxproclen = strlen(argv[0]) + 1;
	for (f = 1; f < argc; f++)
	{
		if (!strcmp(argv[f], "single") || !strcmp(argv[f], "-s"))
			dfl_level = 'S';
		else if (!strcmp(argv[f], "-a") || !strcmp(argv[f], "auto"))
			putenv("AUTOBOOT=YES");
		else if (!strcmp(argv[f], "-b") || !strcmp(argv[f], "emergency"))
			emerg_shell = 1;
		else if (!strcmp(argv[f], "-z"))
		{
			/* Ignore -z xxx */
			if (argv[f + 1])
				f++;
		}
		else if (strchr("0123456789sS", argv[f][0]) && strlen(argv[f]) == 1)
			dfl_level = argv[f][0];
		/* "init u" in the very beginning makes no sense */
		if (dfl_level == 's')
			dfl_level = 'S';
		maxproclen += strlen(argv[f]) + 1;
	}

#ifdef WITH_SELINUX
	if (getenv("SELINUX_INIT") == NULL) {
		if (is_selinux_enabled() != 1) {
			if (selinux_init_load_policy(&enforce) == 0) {
				putenv("SELINUX_INIT=YES");
				execv(myname, argv);
			}
			else {
				if (enforce > 0) {
					/* SELinux in enforcing mode but load_policy failed */
					/* At this point, we probably can't open /dev/console, so log() won't work */
					fprintf(stderr, "Unable to load SELinux Policy. Machine is in enforcing mode. Halting now.\n");
					exit(1);
				}
			}
		}
	}
#endif
	/* Start booting. */
	argv0 = argv[0];
	argv[1] = NULL;
	setproctitle("init boot");
	init_main();

	/*NOTREACHED*/
	return 0;
}
