/*  slackmpoint - see if a directory or file is a mount point .  \
    also a minimalistic replacement for the standard mountpoint from SystemV .
    #################################################################
    Writen by => Andre Bobrovskiy [hignu22-git] <hianon228@yandex.fr>

    GNU's not Unix & Keep It Simple ,Stupid & Keep It Short and Simple !!
*/

#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <getopt.h>
#include <stdio.h>

#if defined (__linux__) || defined(__GNU__)
#include <sys/sysmacros.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 2048
#endif

static int 
dostat(char *path, struct stat *st, int do_lstat, int quiet) {
	int		n;
	if (do_lstat)	n = lstat(path, st);
	else			n = stat(path, st);
	if (n != 0) {
		if (!quiet) fprintf(stderr, "slackmpoint: %s: %s\n", path, strerror(errno));
		return -1;
	}
	return 0;
}


/* MAIN FUNCTION */
int 
main(int argc, char **argv) {
	struct stat		st, st2;
	char			buf[PATH_MAX + 1];
	char			*path;
	int				quiet = 0;
	int				showdev = 0;
	int				xdev = 0;
	int				c, r;
    int             check_proc = 0;

	while ((c = getopt(argc, argv, "dpqx")) != EOF) switch(c) {
		case 'd':
			showdev = 1;
			break;
                case 'p':
                    check_proc = 1;
                    break;
		case 'q':
			quiet = 1;
			break;
		case 'x':
			xdev = 1;
			break;
		default:
			usage();
			break;
	}
	if (optind != argc - 1) usage();
	path = argv[optind];

	if (dostat(path, &st, !xdev, quiet) < 0)
		return 1;

	if (xdev) {
#ifdef __linux__
		if (!S_ISBLK(st.st_mode))
#else
		if (!S_ISBLK(st.st_mode) && !S_ISCHR(st.st_mode))
#endif
		{
			if (quiet)
				printf("\n");
			else
			fprintf(stderr, "mountpoint: %s: not a block device\n",
				path);
			return 1;
		}
		printf("%u:%u\n", major(st.st_rdev), minor(st.st_rdev));
		return 0;
	}

	if (!S_ISDIR(st.st_mode)) {
		if (!quiet)
			fprintf(stderr, "mountpoint: %s: not a directory\n",
				path);
		return 1;
	}

	memset(buf, 0, sizeof(buf));
	strncpy(buf, path, sizeof(buf) - 4);
	strncat(buf, "/..", 3);
	if (dostat(buf, &st2, 0, quiet) < 0)
		return 1;

	/* r = ( (st.st_dev != st2.st_dev) ||
	    ( (st.st_dev == st2.st_dev) && (st.st_ino == st2.st_ino) ) ;

            (A || (!A && B)) is the same as (A || B) so simplifying this below.
            Thanks to David Binderman for pointing this out. -- Jesse
        */
        r = ( (st.st_dev != st2.st_dev) || (st.st_ino == st2.st_ino) );
              
        /* Mount point was not found yet. If we have access
           to /proc we can check there too. */
        if ( (!r) && (check_proc) )
        {
           if ( do_proc_check(path) )
              r = 1;
        }

	if (!quiet && !showdev)
		printf("%s is %sa mountpoint\n", path, r ? "" : "not ");
	if (showdev)
		printf("%u:%u\n", major(st.st_dev), minor(st.st_dev));

	return r ? 0 : 1;
}
