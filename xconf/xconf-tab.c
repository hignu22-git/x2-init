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

#ifdef WITH_SELINUX
#  include <selinux/selinux.h>
#endif
#ifdef __FreeBSD__
extern char **environ;
#endif

#ifdef __i386__
#  ifdef __GLIBC__
     /* GNU libc 2.x */
#    define STACK_DEBUG 1
#    if (__GLIBC__ == 2 && __GLIBC_MINOR__ == 0)
       /* Only glibc 2.0 needs this */
#      include <sigcontext.h>
#    elif ( __GLIBC__ > 2) && ((__GLIBC__ == 2) && (__GLIBC_MINOR__ >= 1))
#      include <bits/sigcontext.h>
#    endif
#  endif
#endif

#include "../init.h"
#include "../initreq.h"
#include "../paths.h"
#include "../reboot.h"
#include "../runlevellog.h"
#include "../set.h"

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
 	char * argv[] )
{

	FILE *fp ;
	char* dmStr ;

	if (!strcmp(argv[1], "--change-dm")){
		if (!strcmp(argv[2], "/usr/bin/sddm")){
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
		fp = fopen(
			RUNLEVEL_DEF_DM ,
			w
		);
		fgets(dmStr, fp);
	}else if (!strcmp(argv[1], "--help" || !strcmp(argv[1], "-h") ) {
		printf("Made By hignu22, Andre Bobrovskiy <hianon228@yandex.fr> ");
	}
    return 0;
}




