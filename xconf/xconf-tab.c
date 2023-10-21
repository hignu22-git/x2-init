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
 * inittab.d
 */
#include <sys/types.h>
#include <dirent.h>

#include "../src/init.h"
#include "../src/initreq.h"
#include "../src/paths.h"
#include "../src/reboot.h"
#include "../src/runlevellog.h"
#include "../src/set.h"

#ifndef SIGPWR
#  define SIGPWR SIGUSR2
#endif

#ifndef CBAUD
#  define CBAUD		0
#endif
#ifndef CBAUDEX
#  define CBAUDEX	0
#endif


/* MAIN FUNCTION */ 
int 
main( int argc,
 	char **argv )
{

	FILE *fp  ; 		/* pach/to/inittab */
	char* dmStr   ; 	/* value argv [ 2 ] */
	char  gtStr[255] ; 	/* string for input in the utility */

	if (!strcmp(argv[1], "--change-dm")){
		if (!strcmp(argv[2], "/usr/bin/sddm") || (!strcmp(argv[2], "sddm"))){
			dmStr = " exec /usr/bin/sddm ";
		}
		else if(!strcmp(argv[2], "/opt/kde/bin/kdm")) {
			dmStr = " exec /opt/kde/bin/kdm -nodaemon " ;
		}
		else if(!strcmp(argv[2], "/usr/bin/kdm")) {
			dmStr = "exec /usr/bin/kdm -nodaemon " ;
		}
		else if(!strcmp(argv[2], "/usr/bin/xdm")) {
			dmStr = "exec /usr/bin/xdm -nodaemon -config /etc/X11/xdm/liveslak-xdm/xdm-config" ;
		}
		else if(!strcmp(argv[2], "/usr/X11R6/bin/xdm")) {
			dmStr = "/usr/X11R6/bin/xdm -nodaemon -config /etc/X11/xdm/liveslak-xdm/xdm-config" ;
		}
		/*2*/
		fp = fopen( RUNLEVEL_DEF_DM ,
			"w" );
		fputs(dmStr, fp);
		fclose(fp);
	}else if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h") ) {
		printf("Made By hignu22, Andre Bobrovskiy <hianon228@yandex.fr> ");
	}/*else if (!strcmp(argv[1], "--set-mode") || !strcmp(argv[1], "-S")){

	}*/

    return 0;
}







