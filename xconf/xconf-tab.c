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

#include "../src/init.h"
#include "../src/initreq.h"
#include "../src/paths.h"
#include "../src/reboot.h"
#include "../src/runlevellog.h"
#include "../src/set.h"


void read2_inittab(void){
	FILE *fp;			 		/* The INITTAB file */
	FILE *fp_tab;		 		/* The INITTABD files */
	CHILD *ch, *old, *i; 		/* Pointers to CHILD structure */
	CHILD *head = NULL;	 		/* Head of linked list */
#ifdef INITLVL
	struct stat st; 			/* To stat INITLVL */
#endif
	sigset_t nmask, omask; 		/* For blocking SIGCHLD. */
	char buf[256];		   		/* Line buffer */
	char err[64];		   		/* Error message. */
	char *id, *rlevel,
		*action, *process; 		/* Fields of a line */
	char *p;
	int lineNo = 0;			   	/* Line number in INITTAB file */
	int actionNo;			   	/* Decoded action field */
	int f;					   	/* Counter */
	int round;				   	/* round 0 for SIGTERM, 1 for SIGKILL */
	int foundOne = 0;		   	/* No killing no sleep */
	int talk;				   	/* Talk to the user */
	int done = -1;			   	/* Ready yet? , 2 level : -1 nothing done,
		0 inittab done, 1 inittab and inittab.d done */
	DIR *tabdir = NULL;		   	/* the rc.d dir */
	struct dirent *file_entry; 	/* rc.d entry */
	char f_name[272];		   	/* size d_name + strlen /etc/rc.d/ */

	if((fp = fopen(INITTAB, "r"))= NULL)
}


/* MAIN FUNCTION */ 
int 
main( int argc,
 	char **argv )
{

    return 0;
}







