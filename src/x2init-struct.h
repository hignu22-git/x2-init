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

