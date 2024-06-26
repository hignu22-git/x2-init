
/* Version information */
char *Version = "@(#) x2-init " VERSION " <hianon228@yandex.fr> ";
char *bootmsg = "version " VERSION " %s";

#define E_VERSION "INIT_VERSION=x2init-" VERSION

short pOpenExt_DX  ;
char* type_DX      ;
char runlevel = 'S';				   /* The current run level */
char thislevel = 'S';				   /* The current runlevel */
char prevlevel = 'N';				   /* Previous runlevel */
int dfl_level = 0;					   /* Default runlevel */
sig_atomic_t got_cont = 0;			   /* Set if we received the SIGCONT signal */
sig_atomic_t got_signals;			   /* Set if we received a signal. */
int emerg_shell = 0;				   /* Start emergency shell? */
int wrote_wtmp_reboot = 1;			   /* Set when we wrote the reboot record */
int wrote_utmp_reboot = 1;			   /* Set when we wrote the reboot record */
int wrote_wtmp_rlevel = 1;			   /* Set when we wrote the runlevel record */
int wrote_utmp_rlevel = 1;			   /* Set when we wrote the runlevel record */
int sleep_time = WAIT_BETWEEN_SIGNALS; /* Sleep time between TERM and KILL */
char *argv0;						   /* First arguments; show up in ps listing */
int maxproclen;						   /* Maximal length of argv[0] with \0 */
struct utmp utproto;				   /* Only used for sizeof(utproto.ut_id) */
char *console_dev;					   /* Console device. */
int pipe_fd = -1;					   /* /run/initctl */
int did_boot = 0;					   /* Did we already do BOOT* stuff? */



struct actions {char *name; int act; } 
actions[] = {
	{"respawn", 	RESPAWN},
	{"wait", 		WAIT},
	{"once", 		ONCE},
	{"boot", 		BOOT},
	{"bootwait", 	BOOTWAIT},
	{"powerfail", 	POWERFAIL},
	{"powerfailnow",POWERFAILNOW},
	{"powerwait", 	POWERWAIT},
	{"powerokwait", POWEROKWAIT},
	{"ctrlaltdel", 	CTRLALTDEL},
	{"off", 		OFF},
	{"ondemand", 	ONDEMAND},
	{"initdefault", INITDEFAULT},
	{"sysinit", 	SYSINIT},
	{"kbrequest", 	KBREQUEST},
	{NULL,			0},
};

/*	Used by re-exec part */
int reload = 0;						/* Should we do initialization stuff? */
char *myname = INIT;				/* What should we exec */
int oops_error;						/* Used by some of the re-exec code. */
const char *Signature = "12567362"; /* Signature for re-exec fd */

/* State parser token table (see receive_state) */
struct { char name[4]; int cmd; } 
cmds[] = {
	{"VER", C_VER},
	{"END", C_END},
	{"REC", C_REC},
	{"EOR", C_EOR},
	{"LEV", C_LEV},
	{"FL ", C_FLAG},
	{"AC ", C_ACTION},
	{"CMD", C_PROCESS},
	{"PID", C_PID},
	{"EXS", C_EXS},
	{"-RL", D_RUNLEVEL},
	{"-TL", D_THISLEVEL},
	{"-PL", D_PREVLEVEL},
	{"-SI", D_GOTSIGN},
	{"-WR", D_WROTE_WTMP_REBOOT},
	{"-WU", D_WROTE_UTMP_REBOOT},
	{"-ST", D_SLTIME},
	{"-DB", D_DIDBOOT},
	{"-LW", D_WROTE_WTMP_RLEVEL},
	{"-LU", D_WROTE_UTMP_RLEVEL},
	{"", 0}
};

struct { char *name; int mask; } flags[] = {
	{"RU", RUNNING},
	{"DE", DEMAND},
	{"XD", XECUTED},
	{"WT", WAITING},
	{NULL, 0}
};

/* Set a signal handler. */
#define SETSIG(sa, sig, fun, flags) \
	do                              \
	{                               \
		memset(&sa, 0, sizeof(sa)); \
		sa.sa_handler = fun;        \
		sa.sa_flags = flags;        \
		sigemptyset(&sa.sa_mask);   \
		sigaction(sig, &sa, NULL);  \
	} while (0)

CHILD *family = NULL;	 /* The linked list of all entries */
CHILD *newFamily = NULL; /* The list after inittab re-read */

int main(int, char **);

/* Macro to see if this is a special action */
#define ISPOWER(i) ((i) == POWERWAIT || (i) == POWERFAIL ||      \
					(i) == POWEROKWAIT || (i) == POWERFAILNOW || \
					(i) == CTRLALTDEL)
/* w_journal2x */
#define J1_INIT  "init"
#define J2_DEBUG "DEBUG"
#define J2_ERROR "ERROR"
#define J2_WARN	 "WARNING"
/* 1 FLAGS ,EXSTAT ,PID ,tm ,count ,id ,rlevel ,action ,process ,new ,next*/
CHILD ch_emerg = {/* Emergency shell */
				  WAITING, 0, 0, 0, 0,"~~",
				  "S",3,"/sbin/sulogin",
				  NULL,NULL};

#define NR_EXTRA_ENV 16
char *extra_env[NR_EXTRA_ENV];

#define MINI_SLEEP 10	 /* ten milliseconds */
#define SHORT_SLEEP 5000 /* five seconds */
#define LONG_SLEEP 30000 /* 30 seconds sleep to deal with memory issues*/
