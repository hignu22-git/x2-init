
#define VT_MASTER	"/dev/tty0"	/* Virtual console master */
#define CONSOLE		"/dev/console" /* Logical system console */
#define SECURETTY	"/etc/securetty" /* List of root terminals */
#define SDALLOW		"/etc/shutdown.allow" /* Users allowed to shutdown */
#define INITTAB		"/etc/inittab"	/* Location of inittab */
#define INITTABD	"/etc/rc.d"	/* Location of rc.d directory */
#define INIT		"/sbin/init" /* Location of init itself. */
#define NOLOGIN		"/etc/nologin"	/* Stop user logging in. */
#define FASTBOOT	"/fastboot"	 /* Enable fast boot. */
#define FORCEFSCK	"/forcefsck" /* Force fsck on boot */
#define SDPID		"/var/run/shutdown.pid"	/* PID of shutdown program */
#define SHELL		"/bin/sh" /* Default shell */
#define SULOGIN		"/sbin/sulogin" /* Sulogin */
#define INITSCRIPT	"/etc/initscript" /* Initscript. */
#define PWRSTAT_OLD	"/etc/powerstatus" /* COMPAT: SIGPWR reason (OK/BAD) */
#define PWRSTAT		"/var/run/powerstatus" /* COMPAT: SIGPWR reason (OK/BAD) */
#define RUNLEVEL_LOG "/var/run/runlevel" /* neutral place to store run level*/
#define RUNLEVEL_DEF_DM  "/etc/rc.d/rc.4" /*default runlevel with dm */

#if 0
#define INITLVL		"/etc/initrunlvl"	/* COMPAT: New runlevel */
#define INITLVL2	"/var/log/initrunlvl"	/* COMPAT: New runlevel */
				/* Note: INITLVL2 definition needs INITLVL */
#define HALTSCRIPT1	"/etc/init.d/halt"	/* Called by "fast" shutdown */
#define HALTSCRIPT2	"/etc/rc.d/rc.0"	/* Called by "fast" shutdown */
#define REBOOTSCRIPT1	"/etc/init.d/reboot"	/* Ditto. */
#define REBOOTSCRIPT2	"/etc/rc.d/rc.6"	/* Ditto. */
#endif

