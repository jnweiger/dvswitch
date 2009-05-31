/* Copyright 2007 Ben Hutchings.
 * See the file "COPYING" for licence details.
 */
/* Configuration file reader */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char * string_join(const char * left, const char * right)
{
    size_t left_len = strlen(left), right_len = strlen(right);
    char * result;

    if ((result = malloc(left_len + right_len + 1)) == NULL)
    {
	perror("ERROR: malloc");
	exit(1);
    }
    memcpy(result, left, left_len);
    memcpy(result + left_len, right, right_len);
    result[left_len + right_len] = 0;
    return result;
}

/* Read configuration file.  Exit if it is unreadable or invalid.
 * The configuration format consists of Bourne shell variable assignments
 * and comments, with no variable references or escaped line breaks
 * allowed.
 */
static void read_config(const char * path,
			void (*item_handler)(const char *, const char *))
{
    FILE * file;
    unsigned line_no = 0;
    char line_buf[1000]; /* this is just going to have to be long enough */

    if ((file = fopen(path, "r")) == NULL)
    {
	/* Configuration files are optional so this is only an error if
	 * the file exists yet is unreadable.
	 */
	if (errno == ENOENT)
	    return;
	perror("ERROR: fopen");
	exit(1);
    }

    while (fgets(line_buf, sizeof(line_buf), file))
    {
	const char * name, * value;
	char * p;
	int ch;
	bool valid = true;

	++line_no;

	/* Find first non-space. */
	p = line_buf;
	while ((ch = (unsigned char)*p) && isspace(ch))
	    ++p;

	if (ch == 0 || ch == '#')
	{
	    /* This is a blank or comment line. */
	    continue;
	}
	else if (isalpha(ch) || ch == '_')
	{
	    /* Find end of name. */
	    name = p++;
	    while ((ch = (unsigned char)*p) && (isalnum(ch) || ch == '_'))
		++p;
	    *p = 0; /* ensure name is terminated */

	    if (ch != '=')
	    {
		valid = false;
	    }
	    else
	    {
		char * out;

		++p;
		value = out = p;

		while ((ch = (unsigned char)*p) && !isspace(ch))
		{
		    int quote = (ch == '\'' || ch == '"') ? ch : 0;

		    if (quote)
			++p;

		    while ((ch = (unsigned char)*p)
			   && !(quote ? ch == quote
				: (isspace(ch) || ch == '\'' || ch == '"')))
		    {
			++p;
			if (quote != '\'')
			{
			    if (ch == '$')
			    {
				/* We're not going to do $-expansion. */
				valid = false;
			    }
			    else if (ch == '\\')
			    {
				/* Check whether it's a valid escape. */
				ch = (unsigned char)*p;
				switch (ch)
				{
				case '$':
				case '\'':
				case '\"':
				case '\\':
				    ++p;
				    break;
				case ' ':
				    if (quote)
					valid = false;
				    else
					++p;
				    break;
				default:
				    valid = false;
				    break;
				}
			    }
			}
			*out++ = ch;
		    }

		    if (quote && ch)
			++p;
		}

		while ((ch = (unsigned char)*p) && isspace(ch) && ch != '\n')
		    ++p;
		if (ch != '\n')
		    valid = false; /* new-line was quoted or missing */

		*out = 0; /* terminate value */
	    }
	}
	else
	{
	    valid = false;
	}

	if (valid)
	{
	    item_handler(name, value);
	}
	else
	{
	    /* XXX This could be more informative! */
	    fprintf(stderr, "ERROR: syntax error at %s:%d\n", path, line_no);
	    exit(2);
	}
    }

    fclose(file);
}

void dvswitch_read_config(void (*item_handler)(const char *, const char *))
{
    read_config("/etc/dvswitchrc", item_handler);

    const char * home = getenv("HOME");
    if (home)
    {
	char * home_dvswitchrc = string_join(home, "/.dvswitchrc");
	read_config(home_dvswitchrc, item_handler);
	free(home_dvswitchrc);
    }
}
