#ifndef lint
static const char	RCSid[] = "$Id$";
#endif
/*
 * Fix argv[0] for DOS environments
 *
 *  External symbols declared in paths.h
 */

#include "copyright.h"

#include <ctype.h>

extern char *
fixargv0(av0)			/* extract command name from full path */
char  *av0;
{
	register char  *cp = av0;

	while (*cp) cp++;		/* start from end */
	while (cp-- > av0)
		switch (*cp) {		/* fix up command name */
		case '.':			/* remove extension */
			*cp = '\0';
			continue;
		case '\\':			/* remove directory */
			return(cp+1);
		default:			/* convert to lower case */
			*cp = tolower(*cp);
			continue;
		}
	return(av0);
}


