/* Copyright (c) 1995 Regents of the University of California */

#ifndef lint
static char SCCSid[] = "$SunId$ LBL";
#endif

/*
 * Calibrate a scanned MacBeth Color Checker Chart
 *
 * Produce a .cal file suitable for use with pcomb.
 */

#include <stdio.h>
#ifdef MSDOS
#include <fcntl.h>
#endif
#include "color.h"
#include "resolu.h"
#include "pmap.h"

				/* MacBeth colors (CIE 1931, absolute white) */
				/* computed from spectral measurements */
float	mbxyY[24][3] = {
		{0.462, 0.3769, 0.0932961},	/* DarkSkin */
		{0.4108, 0.3542, 0.410348},	/* LightSkin */
		{0.2626, 0.267, 0.181554},	/* BlueSky */
		{0.36, 0.4689, 0.108447},	/* Foliage */
		{0.2977, 0.2602, 0.248407},	/* BlueFlower */
		{0.2719, 0.3485, 0.401156},	/* BluishGreen */
		{0.52, 0.4197, 0.357899},	/* Orange */
		{0.229, 0.1866, 0.103911},	/* PurplishBlue */
		{0.4909, 0.3262, 0.242615},	/* ModerateRed */
		{0.3361, 0.2249, 0.0600102},	/* Purple */
		{0.3855, 0.4874, 0.42963},	/* YellowGreen */
		{0.4853, 0.4457, 0.476343},	/* OrangeYellow */
		{0.2026, 0.1369, 0.0529249},	/* Blue */
		{0.3007, 0.4822, 0.221226},	/* Green */
		{0.5805, 0.3238, 0.162167},	/* Red */
		{0.4617, 0.472, 0.64909},	/* Yellow */
		{0.4178, 0.2625, 0.233662},	/* Magenta */
		{0.2038, 0.2508, 0.167275},	/* Cyan */
		{0.3358, 0.337, 0.916877},	/* White */
		{0.3338, 0.3348, 0.604678},	/* Neutral.8 */
		{0.3333, 0.3349, 0.364566},	/* Neutral.65 */
		{0.3353, 0.3359, 0.200238},	/* Neutral.5 */
		{0.3363, 0.336, 0.0878721},	/* Neutral.35 */
		{0.3346, 0.3349, 0.0308383}	/* Black */
	};

COLOR	mbRGB[24];		/* MacBeth RGB values */

#define	NMBNEU		6	/* Number of MacBeth neutral colors */
short	mbneu[NMBNEU] = {23,22,21,20,19,18};
#define NMBMOD		3	/* Number of MacBeth moderate colors */
short	mbmod[NMBMOD] = {1,3,2};
#define NMBSAT		6	/* Number of MacBeth saturated colors */
short	mbsat[NMBSAT] = {14,12,13,15,16,17};

int	xmax, ymax;		/* input image dimensions */
int	bounds[4][2];		/* image coordinates of chart corners */
double	imgxfm[3][3];		/* coordinate transformation matrix */

COLOR	picRGB[24];		/* picture colors */

double	bramp[NMBNEU][2];	/* brightness ramp */
double	solmat[3][3];		/* color mapping matrix */

FILE	*debugfp = NULL;
char	*progname;

extern char	*malloc();


main(argc, argv)
int	argc;
char	**argv;
{
	int	i;

	progname = argv[0];
	if (argc > 2 && !strcmp(argv[1], "-d")) {	/* debug output */
		if ((debugfp = fopen(argv[2], "w")) == NULL) {
			perror(argv[2]);
			exit(1);
		}
#ifdef MSDOS
		setmode(fileno(debugfp), O_BINARY);
#endif
		newheader("RADIANCE", debugfp);
		printargs(argc, argv, debugfp);
		argv += 2;
		argc -= 2;
	}
	if (argc != 3 && argc != 11)
		goto userr;
	if (strcmp(argv[1], "-") && freopen(argv[1], "r", stdin) == NULL) {
		perror(argv[1]);
		exit(1);
	}
	if (strcmp(argv[2], "-") && freopen(argv[2], "w", stdout) == NULL) {
		perror(argv[2]);
		exit(1);
	}
#ifdef MSDOS
	setmode(fileno(stdin), O_BINARY);
#endif
	if (checkheader(stdin, COLRFMT, NULL) < 0 ||
			fgetresolu(&xmax, &ymax, stdin) < 0) {
		fprintf(stderr, "%s: bad input picture\n", progname);
		exit(1);
	}
					/* get chart boundaries */
	if (argc == 11) {
		for (i = 0; i < 4; i++) {
			if (!isint(argv[2*i+3]) | !isint(argv[2*i+4]))
				goto userr;
			bounds[i][0] = atoi(argv[2*i+3]);
			bounds[i][1] = atoi(argv[2*i+4]);
		}
	} else {
		bounds[0][0] = bounds[2][0] = .029*xmax + .5;
		bounds[0][1] = bounds[1][1] = .956*ymax + .5;
		bounds[1][0] = bounds[3][0] = .971*xmax + .5;
		bounds[2][1] = bounds[3][1] = .056*ymax + .5;
	}
	init();				/* initialize */
	getcolors();			/* get picture colors */
	putmapping();			/* put out color mapping */
	putdebug();			/* put out debug picture */
	exit(0);
userr:
	fprintf(stderr, "Usage: %s [-d dbg.pic] input.pic output.cal [xul yul xur yur xll yll xlr ylr]\n",
			progname);
	exit(1);
}


init()				/* initialize */
{
	double	quad[4][2];
	COLOR	ctmp;
	double	d;
	int	i;
					/* make coordinate transformation */
	quad[0][0] = bounds[0][0];
	quad[0][1] = bounds[0][1];
	quad[1][0] = bounds[1][0];
	quad[1][1] = bounds[1][1];
	quad[2][0] = bounds[3][0];
	quad[2][1] = bounds[3][1];
	quad[3][0] = bounds[2][0];
	quad[3][1] = bounds[2][1];

	if (pmap_quad_rect(0., 0., 6., 4., quad, imgxfm) == PMAP_BAD) {
		fprintf(stderr, "%s: bad chart boundaries\n", progname);
		exit(1);
	}
					/* map MacBeth colors to RGB space */
	for (i = 0; i < 24; i++) {
		d = mbxyY[i][2] / mbxyY[i][1];
		ctmp[0] = mbxyY[i][0] * d;
		ctmp[1] = mbxyY[i][2];
		ctmp[2] = (1. - mbxyY[i][0] - mbxyY[i][1]) * d;
		cie_rgb(mbRGB[i], ctmp);
	}
}


int
chartndx(x, y)				/* find color number for position */
int	x, y;
{
	double	ipos[3], cpos[3];
	int	ix, iy;
	double	fx, fy;

	ipos[0] = x;
	ipos[1] = y;
	ipos[2] = 1;
	mx3d_transform(ipos, imgxfm, cpos);
	cpos[0] /= cpos[2];
	cpos[1] /= cpos[2];
	if (cpos[0] < 0. || cpos[0] >= 6. || cpos[1] < 0. || cpos[1] >= 4.)
		return(-1);
	ix = cpos[0];
	iy = cpos[1];
	fx = cpos[0] - ix;
	fy = cpos[1] - iy;
	if (fx < .35 || fx >= .65 || fy < .35 || fy >= .65)
		return(-1);
	return(iy*6 + ix);
}


getcolors()				/* load in picture colors */
{
	COLR	*scanln;
	COLOR	pval;
	int	ccount[24];
	double	d;
	int	y;
	register int	x, i;

	scanln = (COLR *)malloc(xmax*sizeof(COLR));
	if (scanln == NULL) {
		perror(progname);
		exit(1);
	}
	for (i = 0; i < 24; i++) {
		setcolor(picRGB[i], 0., 0., 0.);
		ccount[i] = 0;
	}
	for (y = ymax-1; y >= 0; y--) {
		if (freadcolrs(scanln, xmax, stdin) < 0) {
			fprintf(stderr, "%s: error reading input picture\n",
					progname);
			exit(1);
		}
		for (x = 0; x < xmax; x++) {
			i = chartndx(x, y);
			if (i >= 0) {
				colr_color(pval, scanln[x]);
				addcolor(picRGB[i], pval);
				ccount[i]++;
			}
		}
	}
	for (i = 0; i < 24; i++) {
		if (ccount[i] == 0) {
			fprintf(stderr, "%s: bad chart boundaries\n",
					progname);
			exit(1);
		}
		d = 1.0/ccount[i];
		scalecolor(picRGB[i], d);
	}
	free((char *)scanln);
}


double
bresp(x)		/* piecewise linear interpolation of brightness */
double	x;
{
	register int	n = NMBNEU;

	while (n > 0 && x < bramp[--n][0])
		;
	return( ((bramp[n+1][0] - x)*bramp[n][1] +
				(x - bramp[n][0])*bramp[n+1][1]) /
			(bramp[n+1][0] - bramp[n][0]) );
}


putmapping()			/* compute and print mapping for pcomb -f */
{
	float	clrin[NMBMOD][3], clrout[NMBMOD][3];
	register int	i, j;
					/* compute piecewise luminance curve */
	for (i = 0; i < NMBNEU; i++) {
		bramp[i][0] = bright(picRGB[mbneu[i]]);
		bramp[i][1] = bright(mbRGB[mbneu[i]]);
	}
					/* compute color matrix */
	for (i = 0; i < NMBMOD; i++)
		for (j = 0; j < 3; j++) {
			clrin[i][j] = bresp(picRGB[mbmod[i]][j]);
			clrout[i][j] = mbRGB[mbmod[i]][j];
		}
	compsoln(clrin, clrout, NMBMOD);
					/* print brightness mapping */
	printf("xval(i) : select(i, 0");
	for (i = 0; i < NMBNEU; i++)
		printf(", %g", bramp[i][0]);
	printf(");\n");
	printf("yval(i) : select(i, 0");
	for (i = 0; i < NMBNEU; i++)
		printf(", %g", bramp[i][1]);
	printf(");\n");
	printf("ifind(x,f,n) : if(1.5-n, 1, if(x-f(n), n, ifind(x,f,n-1)));\n");
	printf("binterp(i,x) : ((xval(i+1)-x)*yval(i) + (x-xval(i))*yval(i+1))/\n");
	printf("\t\t(xval(i+1) - xval(i));\n");
	printf("bresp(x) : binterp(ifind(x,xval,xval(0)-1), x);\n");
	printf("nred = bresp(ri(1));\n");
	printf("ngrn = bresp(gi(1));\n");
	printf("nblu = bresp(bi(1));\n");
					/* print color mapping */
	printf("ro = %g*nred + %g*ngrn + %g*nblu\n",
			solmat[0][0], solmat[1][0], solmat[2][0]);
	printf("go = %g*nred + %g*ngrn + %g*nblu\n",
			solmat[0][1], solmat[1][1], solmat[2][1]);
	printf("bo = %g*nred + %g*ngrn + %g*nblu\n",
			solmat[0][2], solmat[1][2], solmat[2][2]);
}


compsoln(cin, cout, n)		/* solve 3x3 system */
float	cin[][3], cout[][3];
int	n;
{
	extern double	mx3d_adjoint(), fabs();
	double	mat[3][3], invmat[3][3];
	double	det;
	double	colv[3], rowv[3];
	register int	i, j;

	if (n != 3) {
		fprintf(stderr, "%s: inconsistent code!\n", progname);
		exit(1);
	}
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			mat[i][j] = cin[j][i];
	det = mx3d_adjoint(mat, invmat);
	if (fabs(det) < 1e-4) {
		fprintf(stderr, "%s: cannot compute color mapping\n",
				progname);
		solmat[0][0] = solmat[1][1] = solmat[2][2] = 1.;
		solmat[0][1] = solmat[0][2] = solmat[1][0] =
		solmat[1][2] = solmat[2][0] = solmat[2][1] = 0.;
		return;
	}
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			invmat[i][j] /= det;
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++)
			rowv[j] = cout[j][i];
		mx3d_transform(rowv, invmat, colv);
		for (j = 0; j < 3; j++)
			solmat[j][i] = colv[j];
	}
}


cvtcolor(cout, cin)		/* convert color according to our mapping */
COLOR	cout, cin;
{
	double	r, g, b;
	double	r1, g1, b1;

	r = bresp(colval(cin,RED));
	g = bresp(colval(cin,GRN));
	b = bresp(colval(cin,BLU));
	r1 = r*solmat[0][0] + g*solmat[1][0] + b*solmat[2][0];
	if (r1 < 0) r1 = 0;
	g1 = r*solmat[0][1] + g*solmat[1][1] + b*solmat[2][1];
	if (g1 < 0) g1 = 0;
	b1 = r*solmat[0][2] + g*solmat[1][2] + b*solmat[2][2];
	if (b1 < 0) b1 = 0;
	setcolor(cout, r1, g1, b1);
}


putdebug()			/* put out debugging picture */
{
	COLOR	*scan;
	int	y;
	register int	x, i;

	if (debugfp == NULL)
		return;
	if (fseek(stdin, 0L, 0) == EOF) {
		fprintf(stderr, "%s: cannot seek on input picture\n", progname);
		exit(1);
	}
	getheader(stdin, NULL, NULL);		/* skip input header */
	fgetresolu(&xmax, &ymax, stdin);
						/* allocate scanline */
	scan = (COLOR *)malloc(xmax*sizeof(COLOR));
	if (scan == NULL) {
		perror(progname);
		exit(1);
	}
						/* finish debug header */
	putc('\n', debugfp);
	fprtresolu(xmax, ymax, debugfp);
	for (y = ymax-1; y >= 0; y--) {
		if (freadscan(scan, xmax, stdin) < 0) {
			fprintf(stderr, "%s: error rereading input picture\n",
					progname);
			exit(1);
		}
		for (x = 0; x < xmax; x++) {
			i = chartndx(x, y);
			if (i < 0)
				cvtcolor(scan[x], scan[x]);
			else
				copycolor(scan[x], mbRGB[i]);
		}
		if (fwritescan(scan, xmax, debugfp) < 0) {
			fprintf(stderr, "%s: error writing debugging picture\n",
					progname);
			exit(1);
		}
	}
	free((char *)scan);
}