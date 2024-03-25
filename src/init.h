#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Standard configuration */
#define CHANGE_WAIT 0			/* Change runlevel while waiting for a process to exit? */
/* Debug and test modes */
#define DEBUG	   0			/* Debug code off */
#define INITDEBUG  0			/* Fork at startup to debug init. */

/* Some constants */
#define INITPID	   1			/* pid of first process */
#define PIPE_FD    10			/* Fileno of initfifo. */
#define STATE_PIPE 11			/* used to pass state through exec */
#define WAIT_BETWEEN_SIGNALS 3        /* default time to wait between TERM and KILL */

/* Failsafe configuration */
#define MAXSPAWN   10			/* Max times respawned in.. */
#define TESTTIME   120			/* this much seconds */
#define SLEEPTIME  300			/* Disable time */

/* Default path inherited by every child. */
#define PATH_DEFAULT   "/sbin:/usr/sbin:/bin:/usr/bin"


/* Prototypes. */
void write_utmp_wtmp(char *user, char *id, int pid, int type, char *line);
void write_wtmp(char *user, char *id, int pid, int type, char *line);
#ifdef __GNUC__
__attribute__ ((format (printf, 2, 3)))
#endif
void initlog(int loglevel, char *fmt, ...);
void set_term(int how);
void print(char *fmt);

/* from dowall.c */
void wall(const char *text, int remote);

#if DEBUG
#define INITDBG(level, fmt, args...) initlog(level, fmt, ##args)
#else 
#define INITDBG(level, fmt ,args...)
#endif 


/* Actions to be taken by init */
#define RESPAWN			1
#define WAIT			2
#define ONCE			3
#define	BOOT			4
#define BOOTWAIT		5
#define POWERFAIL		6
#define POWERWAIT		7
#define POWEROKWAIT		8
#define CTRLALTDEL		9
#define OFF		       10
#define	ONDEMAND	   11
#define	INITDEFAULT    12
#define SYSINIT	       13
#define POWERFAILNOW   14
#define KBREQUEST      15

/* Information about a process in the in-core inittab */
typedef struct _child_ {
    int flags;			/* Status of this entry */
    int exstat;			/* Exit status of process */
    int pid;			/* Pid of this process */
    time_t tm;			/* When respawned last */
    int count;			/* Times respawned in the last 2 minutes */
    char id[8];			/* Inittab id (must be unique) */
    char rlevel[12];		/* run levels */
    int action;			/* what to do (see list below) */
    char process[128];		/* The command line */
    struct _child_ *new;		/* New entry (after inittab re-read) */
    struct _child_ *next;		/* For the linked list */
} CHILD;

typedef struct  {
    char *cls    ;      /* exit command */
    char *initb  ;                         
    char *chroot ;      /* fast chroot to second system */    
}        iCMD    ;
typedef struct  {
    char *mainSep;
    char *endLine;
}       xrcStruct;
typedef struct  {
    char *user  ;
    char *group ;
    char *args  ;
    char *bin   ;
    char *param ;
    char *rlevel;
}       xrcParam;

int r_journal2x( char* _file_ ,char* desgin_viewing);
int w_journal2x(int loglevel ,char* source ,char* msgType ,char* s ,...);
struct journal { char* id; char* state; char* process; };

/* Values for the 'flags' field */
#define RUNNING			2	/* Process is still running */
#define KILLME			4	/* Kill this process */
#define DEMAND			8	/* "runlevels" a b c */
#define FAILING			16	/* process respawns rapidly */
#define WAITING			32	/* We're waiting for this process */
#define ZOMBIE			64	/* This process is already dead */
#define XECUTED		    128	/* Set if spawned once or more times */

#define  RBOOT    "#"
#define  RSYSINIT "*"

/* Log levels. */
#define L_CO	1		        /* Log on the console. */
#define L_SY	2		        /* Log with syslog() */
#define L_VB	(L_CO|L_SY)	    /* Log with both. */
#define L_XI    4               /* Log with journal2x */

#ifndef NO_PROCESS
#  define NO_PROCESS 0
#endif

/* Global variables. */
extern CHILD *family;
extern int wrote_wtmp_reboot;
extern int wrote_utmp_reboot;
extern int wrote_wtmp_rlevel;
extern int wrote_utmp_rlevel;
extern char thislevel;
extern char prevlevel;

/* Tokens in state parser */
#define C_VER		1
#define	C_END		2
#define C_REC		3
#define	C_EOR		4
#define	C_LEV		5
#define C_FLAG		6
#define	C_ACTION	7
#define C_PROCESS	8
#define C_PID		9
#define C_EXS	       10
#define C_EOF          -1
#define D_RUNLEVEL     -2
#define D_THISLEVEL    -3
#define D_PREVLEVEL    -4
#define D_GOTSIGN      -5
#define D_WROTE_WTMP_REBOOT -6
#define D_WROTE_UTMP_REBOOT -7
#define D_SLTIME       -8
#define D_DIDBOOT      -9
#define D_WROTE_WTMP_RLEVEL -16
#define D_WROTE_UTMP_RLEVEL -17

#ifdef __FreeBSD__
#define UTMP_FILE "/var/run/utmp"
#define RUN_LVL 1
struct utmp {
   char ut_id[4];
};
#endif

