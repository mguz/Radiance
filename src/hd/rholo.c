/* Copyright (c) 1997 Silicon Graphics, Inc. */

#ifndef lint
static char SCCSid[] = "$SunId$ SGI";
#endif

/*
 * Radiance holodeck generation controller
 */

#include "rholo.h"
#include "random.h"
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

			/* the following must be consistent with rholo.h */
int	NVARS = NRHVARS;		/* total number of variables */

VARIABLE	vv[] = RHVINIT;		/* variable-value pairs */

char	*progname;		/* our program name */
char	*hdkfile;		/* holodeck file name */
char	froot[256];		/* root file name */

int	nowarn = 0;		/* turn warnings off? */

int	ncprocs = 0;		/* desired number of compute processes */

char	*outdev = NULL;		/* output device name */

int	readinp = 0;		/* read commands from stdin */

int	force = 0;		/* allow overwrite of holodeck */

time_t	starttime;		/* time we got started */
time_t	endtime;		/* time we should end by */
time_t	reporttime;		/* time for next report */

long	maxdisk;		/* maximum file space (bytes) */

int	rtargc = 1;		/* rtrace command */
char	*rtargv[128] = {"rtrace", NULL};

int	orig_mode = -1;		/* original file mode (-1 if unchanged) */

long	nraysdone = 0L;		/* number of rays done */
long	npacksdone = 0L;	/* number of packets done */

PACKET	*freepacks;		/* available packets */

char  *sigerr[NSIG];		/* signal error messages */

extern time_t	time();


main(argc, argv)
int	argc;
char	*argv[];
{
	int	i;
						/* mark start time */
	starttime = time(NULL);
	initurand(16384);			/* initialize urand */
	progname = argv[0];			/* get arguments */
	for (i = 1; i < argc && argv[i][0] == '-'; i++)
		switch (argv[i][1]) {
		case 'w':			/* turn off warnings */
			nowarn++;
			break;
		case 'f':			/* force overwrite */
			force++;
			break;
		case 'i':			/* read input from stdin */
			readinp++;
			break;
		case 'n':			/* compute processes */
			if (i >= argc-2)
				goto userr;
			ncprocs = atoi(argv[++i]);
			break;
		case 'o':			/* output display */
			if (i >= argc-2)
				goto userr;
			outdev = argv[++i];
			break;
		default:
			goto userr;
		}
						/* get root file name */
	if (i >= argc)
		goto userr;
	rootname(froot, hdkfile=argv[i++]);
						/* load variables? */
	if (i < argc)
		if (argv[i][0] != '-' && argv[i][0] != '+')
			loadvars(argv[i]);	/* load variables from file */

	if (i >= argc || argv[i][0] == '+')
		loadholo();			/* load existing holodeck */

	while (++i < argc)			/* get command line settings */
		if (setvariable(argv[i], matchvar) < 0) {
			sprintf(errmsg, "unknown variable: %s", argv[i]);
			error(USER, errmsg);
		}
						/* check settings */
	checkvalues();
						/* load RIF if any */
	getradfile();

	if (hdlist[0] == NULL) {		/* create new holodeck */
		HDGRID	hdg[HDMAX];
							/* set defaults */
		setdefaults(hdg);
							/* holodeck exists? */
		if (!force && access(hdkfile, R_OK|W_OK) == 0)
			error(USER,
				"holodeck file exists -- use -f to overwrite");
							/* create holodeck */
		creatholo(hdg);
	} else					/* else just set defaults */
		setdefaults(NULL);
						/* initialize */
	initrholo();
						/* main loop */
	while (rholo())
		;
						/* done */
	quit(0);
userr:
	fprintf(stderr,
"Usage: %s [-n nprocs][-o disp][-w][-f] output.hdk [control.hif|+|- [VAR=val ..]]\n",
			progname);
	quit(1);
}


onsig(signo)				/* fatal signal */
int  signo;
{
	static int  gotsig = 0;

	if (gotsig++)			/* two signals and we're gone! */
		_exit(signo);

	alarm(60);			/* allow 60 seconds to clean up */
	signal(SIGALRM, SIG_DFL);	/* make certain we do die */
	eputs("signal - ");
	eputs(sigerr[signo]);
	eputs("\n");
	quit(3);
}


sigdie(signo, msg)			/* set fatal signal */
int  signo;
char  *msg;
{
	if (signal(signo, onsig) == SIG_IGN)
		signal(signo, SIG_IGN);
	sigerr[signo] = msg;
}


int
resfmode(fd, mod)		/* restrict open file access mode */
int	fd, mod;
{
	struct stat	stbuf;
					/* get original mode */
	if (fstat(fd, &stbuf) < 0)
		error(SYSTEM, "cannot stat open holodeck file");
	mod &= stbuf.st_mode;		/* always more restrictive */
	if (mod == stbuf.st_mode)
		return(-1);		/* already set */
					/* else change it */
	if (fchmod(fd, mod) < 0) {
		error(WARNING, "cannot change holodeck file access mode");
		return(-1);
	}
	return(stbuf.st_mode);		/* return original mode */
}


initrholo()			/* get our holodeck running */
{
	extern int	global_packet();
	register int	i;
						/* close holodeck on exec() */
	fcntl(hdlist[0]->fd, F_SETFD, FD_CLOEXEC);

	if (outdev != NULL)			/* open output device */
		disp_open(outdev);
	else if (ncprocs > 0)			/* else use global ray feed */
		init_global();
						/* record disk space limit */
	if (!vdef(DISKSPACE))
		maxdisk = 0;
	else
		maxdisk = 1024.*1024.*vflt(DISKSPACE);
						/* record end time */
	if (!vdef(TIME) || vflt(TIME) <= FTINY)
		endtime = 0;
	else
		endtime = starttime + vflt(TIME)*3600. + .5;
						/* set up memory cache */
	if (outdev == NULL)
		hdcachesize = 0;	/* manual flushing */
	else if (vdef(CACHE))
		hdcachesize = 1024.*1024.*vflt(CACHE);
						/* open report file */
	if (vdef(REPORT)) {
		register char	*s = sskip2(vval(REPORT), 1);
		if (*s && freopen(s, "a", stderr) == NULL)
			quit(2);
	}
						/* start rtrace */
	if (ncprocs > 0) {
		i = start_rtrace();
		if (i < 1)
			error(USER, "cannot start rtrace process(es)");
		if (vdef(REPORT)) {		/* make first report */
			printargs(rtargc, rtargv, stderr);
			report(0);
		}
						/* allocate packets */
		freepacks = (PACKET *)bmalloc(i*sizeof(PACKET));
		if (freepacks == NULL)
			goto memerr;
		freepacks[--i].nr = 0;
		freepacks[i].next = NULL;
		if (!vdef(OBSTRUCTIONS) || !vbool(OBSTRUCTIONS)) {
			freepacks[i].offset = (float *)bmalloc(
					RPACKSIZ*sizeof(float)*(i+1) );
			if (freepacks[i].offset == NULL)
				goto memerr;
		} else
			freepacks[i].offset = NULL;
		while (i--) {
			freepacks[i].nr = 0;
			freepacks[i].offset = freepacks[i+1].offset == NULL ?
					NULL : freepacks[i+1].offset+RPACKSIZ ;
			freepacks[i].next = &freepacks[i+1];
		}
	}
					/* set up signal handling */
	sigdie(SIGINT, "Interrupt");
	sigdie(SIGHUP, "Hangup");
	sigdie(SIGTERM, "Terminate");
	sigdie(SIGPIPE, "Broken pipe");
	sigdie(SIGALRM, "Alarm clock");
#ifdef	SIGXCPU
	sigdie(SIGXCPU, "CPU limit exceeded");
	sigdie(SIGXFSZ, "File size exceeded");
#endif
						/* protect holodeck file */
	orig_mode = resfmode(hdlist[0]->fd, ncprocs>0 ? 0 : 0444);
	return;
memerr:
	error(SYSTEM, "out of memory in initrholo");
}


rholo()				/* holodeck main loop */
{
	static int	idle = 0;
	PACKET	*pl = NULL, *plend;
	register PACKET	*p;
	time_t	t;
	long	l;
					/* check display */
	if (nprocs <= 0)
		idle = 1;
	if (outdev != NULL) {
		if (!disp_check(idle))
			return(0);	/* quit request */
		if (nprocs <= 0)
			return(1);
	} else if (idle)
		return(0);		/* all done */
					/* check file size */
	if (maxdisk > 0 && hdfilen(hdlist[0]->fd) >= maxdisk) {
		error(WARNING, "file limit exceeded");
		done_rtrace();
		return(1);	/* comes back */
	}
					/* check time */
	if (endtime > 0 || reporttime > 0)
		t = time(NULL);
	if (endtime > 0 && t >= endtime) {
		error(WARNING, "time limit exceeded");
		done_rtrace();
		return(1);	/* comes back */
	}
	if (reporttime > 0 && t >= reporttime)
		report(t);
	idle = 0;			/* get packets to process */
	while (freepacks != NULL) {
		p = freepacks; freepacks = p->next; p->next = NULL;
		if (!next_packet(p)) {
			p->next = freepacks; freepacks = p;
			idle = 1;
			break;
		}
		if (pl == NULL) pl = p;
		else plend->next = p;
		plend = p;
	}
					/* process packets */
	done_packets(do_packets(pl));
	return(1);			/* and continue */
}


setdefaults(gp)			/* set default values */
register HDGRID	*gp;
{
	extern char	*atos();
	register int	i;
	int	n;
	double	len[3], maxlen, d;
	char	buf[64];

	if (!vdef(SECTION)) {
		sprintf(errmsg, "%s must be defined", vnam(SECTION));
		error(USER, errmsg);
	}
	if (!vdef(OCTREE)) {
		if ((vval(OCTREE) = bmalloc(strlen(froot)+5)) == NULL)
			error(SYSTEM, "out of memory");
		sprintf(vval(OCTREE), "%s.oct", froot);
		vdef(OCTREE)++;
	}
	if (!vdef(VDIST)) {
		vval(VDIST) = "F";
		vdef(VDIST)++;
	}
				/* append rendering options */
	if (vdef(RENDER))
		rtargc += wordstring(rtargv+rtargc, vval(RENDER));
	
	if (gp == NULL)		/* already initialized? */
		return;
				/* set grid parameters */
	for (n = 0; n < vdef(SECTION); n++, gp++) {
		gp->grid[0] = gp->grid[1] = gp->grid[2] = 0;
		if (sscanf(nvalue(SECTION, n),
		"%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %hd %hd %hd",
				&gp->orig[0], &gp->orig[1], &gp->orig[2],
				&gp->xv[0][0], &gp->xv[0][1], &gp->xv[0][2],
				&gp->xv[1][0], &gp->xv[1][1], &gp->xv[1][2],
				&gp->xv[2][0], &gp->xv[2][1], &gp->xv[2][2],
				&gp->grid[0], &gp->grid[1], &gp->grid[2]) < 12)
			badvalue(SECTION);
		maxlen = 0.;
		for (i = 0; i < 3; i++)
			if ((len[i] = VLEN(gp->xv[i])) > maxlen)
				maxlen = len[i];
		if (!vdef(GRID))
			d = 0.125*maxlen;
		else if ((d = vflt(GRID)) <= FTINY)
			badvalue(GRID);
		for (i = 0; i < 3; i++)
			if (gp->grid[i] <= 0)
				gp->grid[i] = len[i]/d + (1.-FTINY);
	}
}


creatholo(gp)			/* create a holodeck output file */
HDGRID	*gp;
{
	extern char	VersionID[];
	int4	lastloc, nextloc;
	int	n;
	int	fd;
	FILE	*fp;
					/* open & truncate file */
	if ((fp = fopen(hdkfile, "w+")) == NULL) {
		sprintf(errmsg, "cannot open \"%s\" for writing", hdkfile);
		error(SYSTEM, errmsg);
	}
					/* write information header */
	newheader("RADIANCE", fp);
	fprintf(fp, "SOFTWARE= %s\n", VersionID);
	printvars(fp);
	fputformat(HOLOFMT, fp);
	fputc('\n', fp);
	putw(HOLOMAGIC, fp);		/* put magic number */
	fd = dup(fileno(fp));
	fclose(fp);			/* flush and close stdio stream */
	lastloc = lseek(fd, 0L, 2);
	for (n = vdef(SECTION); n--; gp++) {	/* initialize each section */
		nextloc = 0L;
		write(fd, (char *)&nextloc, sizeof(nextloc));
		hdinit(fd, gp);			/* writes beam index */
		if (!n)
			break;
		nextloc = hdfilen(fd);		/* write section pointer */
		if (lseek(fd, (long)lastloc, 0) < 0)
			error(SYSTEM,
				"cannot seek on holodeck file in creatholo");
		write(fd, (char *)&nextloc, sizeof(nextloc));
		lseek(fd, (long)(lastloc=nextloc), 0);
	}
}


headline(s)			/* process information header line */
char	*s;
{
	extern char	FMTSTR[];
	register char	*cp;
	char	fmt[32];

	if (formatval(fmt, s)) {
		if (strcmp(fmt, HOLOFMT)) {
			sprintf(errmsg, "%s file \"%s\" has %s%s",
					HOLOFMT, hdkfile, FMTSTR, fmt);
			error(USER, errmsg);
		}
		return;
	}
	for (cp = s; *cp; cp++)		/* take off any comments */
		if (*cp == '#') {
			*cp = '\0';
			break;
		}
	setvariable(s, matchvar);	/* don't flag errors */
}


loadholo()			/* start loading a holodeck from fname */
{
	extern long	ftell();
	FILE	*fp;
	int	fd;
	int	n;
	int4	nextloc;
					/* open holodeck file */
	if ((fp = fopen(hdkfile, ncprocs>0 ? "r+" : "r")) == NULL) {
		sprintf(errmsg, "cannot open \"%s\" for %s", hdkfile,
				ncprocs>0 ? "appending" : "reading");
		error(SYSTEM, errmsg);
	}
					/* load variables from header */
	getheader(fp, headline, NULL);
					/* check magic number */
	if (getw(fp) != HOLOMAGIC) {
		sprintf(errmsg, "bad magic number in holodeck file \"%s\"",
				hdkfile);
		error(USER, errmsg);
	}
	nextloc = ftell(fp);			/* get stdio position */
	fd = dup(fileno(fp));
	fclose(fp);				/* done with stdio */
	for (n = 0; nextloc > 0L; n++) {	/* initialize each section */
		lseek(fd, (long)nextloc, 0);
		read(fd, (char *)&nextloc, sizeof(nextloc));
		hdinit(fd, NULL);
	}
	if (n != vdef(SECTION)) {
		sprintf(errmsg, "number of sections does not match %s setting",
				vnam(SECTION));
		error(WARNING, errmsg);
	}
}


done_packets(pl)		/* handle finished packets */
PACKET	*pl;
{
	static int	n2flush = 0;
	register PACKET	*p;

	while (pl != NULL) {
		p = pl; pl = p->next; p->next = NULL;
		if (p->nr > 0) {		/* add to holodeck */
			bcopy((char *)p->ra,
				(char *)hdnewrays(hdlist[p->hd],p->bi,p->nr),
				p->nr*sizeof(RAYVAL));
			if (outdev != NULL)	/* display it */
				disp_packet((PACKHEAD *)p);
			if (hdcachesize <= 0)	/* manual flushing */
				n2flush += p->nr;
			nraysdone += p->nr;
			npacksdone++;
		}
		p->nr = 0;			/* push onto free list */
		p->next = freepacks;
		freepacks = p;
	}
	if (n2flush > 1024*RPACKSIZ*nprocs) {
		hdflush(NULL);			/* flush holodeck buffers */
		n2flush = 0;
	}
}


rootname(rn, fn)		/* remove tail from end of fn */
register char	*rn, *fn;
{
	char	*tp, *dp;

	for (tp = NULL, dp = rn; *rn = *fn++; rn++)
		if (*rn == '/')
			dp = rn;
		else if (*rn == '.')
			tp = rn;
	if (tp != NULL && tp > dp)
		*tp = '\0';
}


badvalue(vc)			/* report bad variable value and exit */
int	vc;
{
	sprintf(errmsg, "bad value for variable '%s'", vnam(vc));
	error(USER, errmsg);
}


eputs(s)			/* put error message to stderr */
register char  *s;
{
	static int  midline = 0;

	if (!*s)
		return;
	if (!midline++) {	/* prepend line with program name */
		fputs(progname, stderr);
		fputs(": ", stderr);
	}
	fputs(s, stderr);
	if (s[strlen(s)-1] == '\n') {
		fflush(stderr);
		midline = 0;
	}
}


wputs(s)			/* put warning string to stderr */
char	*s;
{
	if (!nowarn)
		eputs(s);
}


quit(ec)			/* exit program gracefully */
int	ec;
{
	int	status = 0;

	if (hdlist[0] != NULL) {	/* close holodeck */
		if (nprocs > 0)
			status = done_rtrace();		/* calls hdsync() */
		if (ncprocs > 0 && vdef(REPORT)) {
			long	fsiz, fuse;
			fsiz = hdfilen(hdlist[0]->fd);
			fuse = hdfiluse(hdlist[0]->fd, 1);
			fprintf(stderr,
			"%s: %.1f Mbyte holodeck file, %.1f%% fragmentation\n",
					hdkfile, fsiz/(1024.*1024.),
					100.*(fsiz-fuse)/fsiz);
		}
	}
	if (orig_mode >= 0)		/* reset holodeck access mode */
		fchmod(hdlist[0]->fd, orig_mode);
	if (outdev != NULL)		/* close display */
		disp_close();
	exit(ec ? ec : status);		/* exit */
}
