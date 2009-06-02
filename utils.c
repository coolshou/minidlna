/*  MiniDLNA media server
 *  Copyright (C) 2008-2009  Justin Maggard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

int
ends_with(const char * haystack, const char * needle)
{
	const char * end = strrchr(haystack, *needle);
	return (strcasecmp(end?end:"", needle) ? 0 : 1);
}

char *
trim(char *str)
{
        if (!str)
                return(NULL);
        int i;
        for (i=0; i <= strlen(str) && (isspace(str[i]) || str[i] == '"'); i++) {
		str++;
	}
        for (i=(strlen(str)-1); i >= 0 && (isspace(str[i]) || str[i] == '"'); i--) {
                str[i] = '\0';
        }
        return str;
}

char *
modifyString(char * string, const char * before, const char * after, short like)
{
	int oldlen, newlen, chgcnt = 0;
	char *s, *p, *t;

	oldlen = strlen(before);
	newlen = strlen(after);
	if( newlen > oldlen )
	{
		s = string;
		while( (p = strstr(s, before)) )
		{
			chgcnt++;
			s = p+oldlen;
		}
		string = realloc(string, strlen(string)+((newlen-oldlen)*chgcnt)+1+like);
	}

	s = string;
	while( s )
	{
		p = strcasestr(s, before);
		if( !p )
			return string;
		memmove(p + newlen, p + oldlen, strlen(p + oldlen) + 1);
		memcpy(p, after, newlen);
		if( like )
		{
			t = p+newlen;
			while( isspace(*t) )
				t++;
			if( *t == '"' )
				while( *++t != '"' )
					continue;
			memmove(t+1, t, strlen(t)+1);
			*t = '*';
		}
		s = p + newlen;
	}

	return string;
}

char *
escape_tag(const char *tag)
{
	char *esc_tag = NULL;

	if( strchr(tag, '&') || strchr(tag, '<') || strchr(tag, '>') )
	{
		esc_tag = strdup(tag);
		esc_tag = modifyString(esc_tag, "&", "&amp;amp;", 0);
		esc_tag = modifyString(esc_tag, "<", "&amp;lt;", 0);
		esc_tag = modifyString(esc_tag, ">", "&amp;gt;", 0);
	}

	return esc_tag;
}

void
strip_ext(char * name)
{
	char * period;

	period = strrchr(name, '.');
	if( period )
		*period = '\0';
}

/* Code basically stolen from busybox */
int
make_dir(char * path, mode_t mode)
{
	char * s = path;
	char c;
	struct stat st;

	do {
		c = 0;

		/* Bypass leading non-'/'s and then subsequent '/'s. */
		while (*s) {
			if (*s == '/') {
				do {
					++s;
				} while (*s == '/');
				c = *s;     /* Save the current char */
				*s = 0;     /* and replace it with nul. */
				break;
			}
			++s;
		}

		if (mkdir(path, mode) < 0) {
			/* If we failed for any other reason than the directory
			 * already exists, output a diagnostic and return -1.*/
			if (errno != EEXIST
			    || (stat(path, &st) < 0 || !S_ISDIR(st.st_mode))) {
				break;
			}
		}
	        if (!c)
			return 0;

		/* Remove any inserted nul from the path. */
		*s = c;

	} while (1);

	printf("make_dir: cannot create directory '%s'", path);
	return -1;
}
