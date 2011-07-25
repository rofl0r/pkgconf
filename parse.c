/*
 * parse.c
 * Parser for .pc file.
 *
 * Copyright (c) 2011 William Pitcock <nenolod@dereferenced.org>.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "pkg.h"

static pkg_tuple_t *
tuple_add(pkg_tuple_t *parent, const char *key, const char *value)
{
	pkg_tuple_t *tuple = calloc(sizeof(pkg_tuple_t), 1);

	tuple->key = strdup(key);
	tuple->value = strdup(value);

	tuple->next = parent;
	if (tuple->next != NULL)
		tuple->next->prev = tuple;

	return tuple;
}

char *
tuple_find(pkg_tuple_t *head, const char *key)
{
	pkg_tuple_t *node;

	foreach_list_entry(head, node)
	{
		if (!strcasecmp(node->key, key))
			return node->value;
	}

	return NULL;
}

static char *
strdup_parse(pkg_t *pkg, const char *value)
{
	char buf[BUFSIZ];
	const char *ptr;
	char *bptr = buf;

	for (ptr = value; *ptr != '\0' && bptr - buf < BUFSIZ; ptr++)
	{
		if (*ptr != '$')
			*bptr++ = *ptr;
		else if (*(ptr + 1) == '{')
		{
			static char varname[BUFSIZ];
			char *vptr = varname;
			const char *pptr;
			char *kv, *parsekv;

			*vptr = '\0';

			for (pptr = ptr + 2; *pptr != '\0'; pptr++)
			{
				if (*pptr != '}')
					*vptr++ = *pptr;
				else
				{
					*vptr = '\0';
					break;
				}
			}

			ptr += (pptr - ptr);
			kv = tuple_find(pkg->vars, varname);

			if (kv != NULL)
			{
				parsekv = strdup_parse(pkg, kv);

				strncpy(bptr, parsekv, BUFSIZ - (bptr - buf));
				bptr += strlen(parsekv);

				free(parsekv);
			}
		}
	}

	*bptr = '\0';

	return strdup(buf);
}

/*
 * parse_deplist(pkg, depends)
 *
 * Add requirements to a .pc file.
 * Commas are counted as whitespace, as to allow:
 *    @SUBSTVAR@, zlib
 * getting substituted to:
 *    , zlib.
 */
typedef enum {
	OUTSIDE_MODULE = 0,
	INSIDE_MODULE_NAME = 1,
	BEFORE_OPERATOR = 2,
	INSIDE_OPERATOR = 3,
	AFTER_OPERATOR = 4,
	INSIDE_VERSION = 5
} parse_state_t;

static pkg_dependency_t *
pkg_dependency_add(pkg_dependency_t *head, const char *package, const char *version, pkg_comparator_t compare)
{
	pkg_dependency_t *dep;

	dep = calloc(sizeof(pkg_dependency_t), 1);
	dep->package = strdup(package);

	if (dep->version != NULL)
		dep->version = strdup(version);

	dep->compare = compare;

	dep->prev = head;
	if (dep->prev != NULL)
		dep->prev->next = dep;

	return dep;
}

#define MODULE_SEPARATOR(c) ((c) == ',' || isspace ((c)))
#define OPERATOR_CHAR(c) ((c) == '<' || (c) == '>' || (c) == '!' || (c) == '=')

static pkg_dependency_t *
parse_deplist(pkg_t *pkg, const char *depends)
{
	parse_state_t state = OUTSIDE_MODULE;
	parse_state_t last_state = OUTSIDE_MODULE;
	pkg_dependency_t *deplist = NULL;
	pkg_dependency_t *deplist_head = NULL;
	pkg_comparator_t compare = PKG_ANY;
	char *kvdepends = strdup_parse(pkg, depends);
	char *start = kvdepends;
	char *ptr = kvdepends;
	char *vstart = NULL;
	char *package, *version;

	while (*ptr)
	{
		switch (state)
		{
		case OUTSIDE_MODULE:
			if (!MODULE_SEPARATOR(*ptr))
				state = INSIDE_MODULE_NAME;
			break;

		case INSIDE_MODULE_NAME:
			if (isspace(*ptr))
			{
				const char *sptr = ptr;

				while (*sptr && isspace(*sptr))
					sptr++;

				if (*sptr == '\0')
					state = OUTSIDE_MODULE;
				else if (MODULE_SEPARATOR(*sptr))
					state = OUTSIDE_MODULE;
				else if (OPERATOR_CHAR(*sptr))
					state = BEFORE_OPERATOR;
				else
					state = OUTSIDE_MODULE;
			}
			else if (MODULE_SEPARATOR(*ptr))
				state = OUTSIDE_MODULE;

			if (state != INSIDE_MODULE_NAME && start != ptr)
			{
				char *iter = start;

				while (MODULE_SEPARATOR(*iter))
					iter++;

				package = strndup(iter, ptr - iter);
//				fprintf(stderr, "Found package: %s\n", package);
				start = ptr;
			}

			if (state == OUTSIDE_MODULE)
			{
				deplist = pkg_dependency_add(deplist, package, NULL, PKG_ANY);

				if (deplist_head == NULL)
					deplist_head = deplist;

				free(package);
				package = NULL;
				version = NULL;
			}

			break;

		case BEFORE_OPERATOR:
			if (OPERATOR_CHAR(*ptr))
				state = INSIDE_OPERATOR;
			break;

		case INSIDE_OPERATOR:
			if (!OPERATOR_CHAR(*ptr))
				state = AFTER_OPERATOR;
			break;

		case AFTER_OPERATOR:
			if (!isspace(*ptr))
			{
				vstart = ptr;
				state = INSIDE_VERSION;
			}
			break;

		case INSIDE_VERSION:
			if (MODULE_SEPARATOR(*ptr))
			{
				version = strndup(vstart, ptr - vstart);
				state = OUTSIDE_MODULE;

//				fprintf(stderr, "Found version: %s\n", version);
				deplist = pkg_dependency_add(deplist, package, version, PKG_ANY);

				if (deplist_head == NULL)
					deplist_head = deplist;

				free(package);
				package = NULL;

				free(version);
				version = NULL;
			}

			if (state == OUTSIDE_MODULE)
				start = ptr;
			break;
		}

		ptr++;
	}

	free(kvdepends);
	return deplist_head;
}

/*
 * parse_file(filename)
 *
 * Parse a .pc file into a pkg_t object structure.
 */
pkg_t *
parse_file(const char *filename)
{
	FILE *f;
	pkg_t *pkg;
	char readbuf[BUFSIZ];

	f = fopen(filename, "r");
	if (f == NULL)
		return NULL;

	pkg = calloc(sizeof(pkg_t), 1);
	pkg->filename = strdup(filename);

	while (fgets(readbuf, BUFSIZ, f) != NULL)
	{
		char op, *p, *key = NULL, *value = NULL;

		readbuf[strlen(readbuf) - 1] = '\0';

		p = readbuf;
		while (*p && (isalpha(*p) || isdigit(*p) || *p == '_' || *p == '.'))
			p++;

		key = strndup(readbuf, p - readbuf);
		if (!isalpha(*key) && !isdigit(*p))
			goto cleanup;

		while (*p && isspace(*p))
			p++;

		op = *p++;

		while (*p && isspace(*p))
			p++;

		value = strdup(p);

		switch (op)
		{
		case ':':
			if (!strcasecmp(key, "Name"))
				pkg->realname = strdup_parse(pkg, value);
			else if (!strcasecmp(key, "Description"))
				pkg->description = strdup_parse(pkg, value);
			else if (!strcasecmp(key, "Version"))
				pkg->version = strdup_parse(pkg, value);
			else if (!strcasecmp(key, "CFLAGS"))
				pkg->cflags = strdup_parse(pkg, value);
			else if (!strcasecmp(key, "LIBS"))
				pkg->libs = strdup_parse(pkg, value);
			else if (!strcasecmp(key, "Requires"))
				pkg->requires = parse_deplist(pkg, value);
			else if (!strcasecmp(key, "Conflicts"))
				pkg->conflicts = parse_deplist(pkg, value);
			break;
		case '=':
			pkg->vars = tuple_add(pkg->vars, key, value);
			break;
		default:
			break;
		}

cleanup:
		if (key)
			free(key);

		if (value)
			free(value);
	}

	fclose(f);
	return pkg;
}