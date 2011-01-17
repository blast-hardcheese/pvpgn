/*
 * Copyright (C) 1998,1999,2000,2001  Ross Combs (rocombs@cs.nmsu.edu)
 * Copyright (C) 2000,2001  Marco Ziech (mmz@gmx.net)
 * Copyright (C) 2002,2003,2004 Dizzy 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include "common/setup_before.h"
#include <stdio.h>
#ifdef HAVE_STDDEF_H
# include <stddef.h>
#else
# ifndef NULL
#  define NULL ((void *)0)
# endif
#endif
#ifdef STDC_HEADERS
# include <stdlib.h>
#else
# ifdef HAVE_MALLOC_H
#  include <malloc.h>
# endif
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#else
# ifdef HAVE_STRINGS_H
#  include <strings.h>
# endif
#endif
#include "compat/strchr.h"
#include "compat/strdup.h"
#include "compat/strcasecmp.h"
#include "compat/strncasecmp.h"
#include <ctype.h>
#ifdef HAVE_LIMITS_H
# include <limits.h>
#endif
#include "compat/char_bit.h"
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#include <errno.h>
#include "compat/strerror.h"
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include "compat/access.h"
#include "compat/rename.h"
#include "compat/pdir.h"
#include "common/eventlog.h"
#include "prefs.h"
#include "common/util.h"
#include "common/field_sizes.h"
#include "common/bnethash.h"
#define CLAN_INTERNAL_ACCESS
#define TEAM_INTERNAL_ACCESS
#include "common/introtate.h"
#include "team.h"
#include "account.h"
#include "common/hashtable.h"
#include "storage.h"
#include "storage_file.h"
#include "file_plain.h"
#include "file_cdb.h"
#include "common/list.h"
#include "connection.h"
#include "watch.h"
#include "clan.h"
#undef ACCOUNT_INTERNAL_ACCESS
#undef TEAM_INTERNAL_ACCESS
#undef CLAN_INTERNAL_ACCESS
#include "common/tag.h"
#include "common/xalloc.h"
#include "common/elist.h"
#include "common/setup_after.h"

/* file storage API functions */

static int file_init(const char *);
static int file_close(void);
static unsigned file_read_maxuserid(void);
static t_storage_info *file_create_account(char const *);
static t_storage_info *file_get_defacct(void);
static int file_free_info(t_storage_info *);
static int file_read_attrs(t_storage_info *, t_read_attr_func, void *);
static t_attr *file_read_attr(t_storage_info *, const char *);
static int file_write_attrs(t_storage_info *, const t_hlist *);
static int file_read_accounts(int,t_read_accounts_func, void *);
static t_storage_info *file_read_account(const char *, unsigned);
static int file_cmp_info(t_storage_info *, t_storage_info *);
static const char *file_escape_key(const char *);
static int file_load_clans(t_load_clans_func);
static int file_write_clan(void *);
static int file_remove_clan(int);
static int file_remove_clanmember(int);
static int file_load_teams(t_load_teams_func);
static int file_write_team(void *);
static int file_remove_team(unsigned int);

/* storage struct populated with the functions above */

t_storage storage_file = {
    file_init,
    file_close,
    file_read_maxuserid,
    file_create_account,
    file_get_defacct,
    file_free_info,
    file_read_attrs,
    file_write_attrs,
    file_read_attr,
    file_read_accounts,
    file_read_account,
    file_cmp_info,
    file_escape_key,
    file_load_clans,
    file_write_clan,
    file_remove_clan,
    file_remove_clanmember,
    file_load_teams,
    file_write_team,
    file_remove_team
};

/* start of actual file storage code */

static const char *accountsdir = NULL;
static const char *clansdir = NULL;
static const char *teamsdir = NULL;
static const char *defacct = NULL;
static t_file_engine *file = NULL;

static unsigned file_read_maxuserid(void)
{
    return maxuserid;
}

static int file_init(const char *path)
{
    char *tok, *copy, *tmp, *p;
    const char *dir = NULL;
    const char *clan = NULL;
    const char *team = NULL;
    const char *def = NULL;
    const char *driver = NULL;

    if (path == NULL || path[0] == '\0')
    {
	eventlog(eventlog_level_error, __FUNCTION__, "got NULL or empty path");
	return -1;
    }

    copy = xstrdup(path);
    tmp = copy;
    while ((tok = strtok(tmp, ";")) != NULL)
    {
	tmp = NULL;
	if ((p = strchr(tok, '=')) == NULL)
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "invalid storage_path, no '=' present in token");
	    xfree((void *) copy);
	    return -1;
	}
	*p = '\0';
	if (strcasecmp(tok, "dir") == 0)
	    dir = p + 1;
	else if (strcasecmp(tok, "clan") == 0)
	    clan = p + 1;
	else if (strcasecmp(tok, "team") == 0)
	    team = p + 1;
	else if (strcasecmp(tok, "default") == 0)
	    def = p + 1;
	else if (strcasecmp(tok, "mode") == 0)
	    driver = p + 1;
	else
	    eventlog(eventlog_level_warn, __FUNCTION__, "unknown token in storage_path : '%s'", tok);
    }

    if (def == NULL || clan == NULL || team == NULL || dir == NULL || driver == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "invalid storage_path line for file module (doesnt have a 'dir', a 'clan', a 'team', a 'default' token and a 'mode' token)");
	xfree((void *) copy);
	return -1;
    }

    if (!strcasecmp(driver, "plain"))
	file = &file_plain;
    else if (!strcasecmp(driver, "cdb"))
	file = &file_cdb;
    else
    {
	eventlog(eventlog_level_error, __FUNCTION__, "unknown mode '%s' must be either plain or cdb", driver);
	xfree((void *) copy);
	return -1;
    }

    if (accountsdir)
	file_close();

    accountsdir = xstrdup(dir);
    clansdir = xstrdup(clan);
    teamsdir = xstrdup(team);
    defacct = xstrdup(def);

    xfree((void *) copy);

    return 0;
}

static int file_close(void)
{
    if (accountsdir)
	xfree((void *) accountsdir);
    accountsdir = NULL;

    if (clansdir)
	xfree((void *) clansdir);
    clansdir = NULL;

    if (teamsdir)
    	xfree((void *) teamsdir);
    teamsdir = NULL;

    if (defacct)
	xfree((void *) defacct);
    defacct = NULL;

    file = NULL;

    return 0;
}

static t_storage_info *file_create_account(const char *username)
{
    char *temp;

    if (accountsdir == NULL || file == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "file storage not initilized");
	return NULL;
    }

    if (prefs_get_savebyname())
    {
	char const *safename;

	if (!strcmp(username, defacct))
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "username as defacct not allowed");
	    return NULL;
	}

	if (!(safename = escape_fs_chars(username, strlen(username))))
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "could not escape username");
	    return NULL;
	}
	temp = xmalloc(strlen(accountsdir) + 1 + strlen(safename) + 1);	/* dir + / + name + NUL */
	sprintf(temp, "%s/%s", accountsdir, safename);
	xfree((void *) safename);	/* avoid warning */
    } else
    {
	temp = xmalloc(strlen(accountsdir) + 1 + 8 + 1);	/* dir + / + uid + NUL */
	sprintf(temp, "%s/%06u", accountsdir, maxuserid + 1);	/* FIXME: hmm, maybe up the %06 to %08... */
    }

    return temp;
}

static int file_write_attrs(t_storage_info * info, const t_hlist *attributes)
{
    char *tempname;

    if (accountsdir == NULL || file == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "file storage not initilized");
	return -1;
    }

    if (info == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "got NULL info storage");
	return -1;
    }

    if (attributes == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "got NULL attributes");
	return -1;
    }

    tempname = xmalloc(strlen(accountsdir) + 1 + strlen(BNETD_ACCOUNT_TMP) + 1);
    sprintf(tempname, "%s/%s", accountsdir, BNETD_ACCOUNT_TMP);

    if (file->write_attrs(tempname, attributes))
    {
	/* no eventlog here, it should be reported from the file layer */
	xfree(tempname);
	return -1;
    }

    if (p_rename(tempname, (const char *) info) < 0)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "could not rename account file to \"%s\" (rename: %s)", (char *) info, pstrerror(errno));
	xfree(tempname);
	return -1;
    }

    xfree(tempname);

    return 0;
}

static int file_read_attrs(t_storage_info * info, t_read_attr_func cb, void *data)
{
    if (accountsdir == NULL || file == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "file storage not initilized");
	return -1;
    }

    if (info == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "got NULL info storage");
	return -1;
    }

    if (cb == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "got NULL callback");
	return -1;
    }

    eventlog(eventlog_level_debug, __FUNCTION__, "loading \"%s\"", (char *) info);

    if (file->read_attrs((const char *) info, cb, data))
    {
	/* no eventlog, error reported earlier */
	return -1;
    }

    return 0;
}

static t_attr *file_read_attr(t_storage_info * info, const char *key)
{
    if (accountsdir == NULL || file == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "file storage not initilized");
	return NULL;
    }

    if (info == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "got NULL info storage");
	return NULL;
    }

    if (key == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "got NULL key");
	return NULL;
    }

    return file->read_attr((const char *) info, key);
}

static int file_free_info(t_storage_info * info)
{
    if (info)
	xfree((void *) info);
    return 0;
}

static t_storage_info *file_get_defacct(void)
{
    t_storage_info *info;

    if (defacct == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "file storage not initilized");
	return NULL;
    }

    info = xstrdup(defacct);

    return info;
}

static int file_read_accounts(int flag,t_read_accounts_func cb, void *data)
{
    char const *dentry;
    char *pathname;
    t_pdir *accountdir;

    if (accountsdir == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "file storage not initilized");
	return -1;
    }

    if (cb == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "got NULL callback");
	return -1;
    }

    if (!(accountdir = p_opendir(accountsdir)))
    {
	eventlog(eventlog_level_error, __FUNCTION__, "unable to open user directory \"%s\" for reading (p_opendir: %s)", accountsdir, pstrerror(errno));
	return -1;
    }

    while ((dentry = p_readdir(accountdir)))
    {
	if (dentry[0] == '.')
	    continue;

	pathname = xmalloc(strlen(accountsdir) + 1 + strlen(dentry) + 1);	/* dir + / + file + NUL */
	sprintf(pathname, "%s/%s", accountsdir, dentry);

	cb(pathname, data);
    }

    if (p_closedir(accountdir) < 0)
	eventlog(eventlog_level_error, __FUNCTION__, "unable to close user directory \"%s\" (p_closedir: %s)", accountsdir, pstrerror(errno));

    return 0;
}

static t_storage_info *file_read_account(const char *accname, unsigned uid)
{
    char *pathname;

    if (accountsdir == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "file storage not initilized");
	return NULL;
    }

    /* ONLY if requesting for a username and if savebyname() is true
     * PS: yes its kind of a hack, we will make a proper index file
     */
    if (accname && prefs_get_savebyname()) {
	pathname = xmalloc(strlen(accountsdir) + 1 + strlen(accname) + 1);	/* dir + / + file + NUL */
	sprintf(pathname, "%s/%s", accountsdir, accname);
	if (access(pathname, F_OK))	/* if it doesn't exist */
	{
	    xfree((void *) pathname);
	    return NULL;
	}
	return pathname;
    }

    return NULL;
}

static int file_cmp_info(t_storage_info * info1, t_storage_info * info2)
{
    return strcmp((const char *) info1, (const char *) info2);
}

static const char *file_escape_key(const char *key)
{
    return key;
}

static int file_load_clans(t_load_clans_func cb)
{
    char const *dentry;
    t_pdir *clandir;
    char *pathname;
    int clantag;
    t_clan *clan;
    FILE *fp;
    char *clanname, *motd, *p;
    char line[1024];
    int cid, creation_time;
    int member_uid, member_join_time;
    char member_status;
    t_clanmember *member;

    if (cb == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "get NULL callback");
	return -1;
    }

    if (!(clandir = p_opendir(clansdir)))
    {
	eventlog(eventlog_level_error, __FUNCTION__, "unable to open clan directory \"%s\" for reading (p_opendir: %s)", clansdir, pstrerror(errno));
	return -1;
    }
    eventlog(eventlog_level_trace, __FUNCTION__, "start reading clans");

    pathname = xmalloc(strlen(clansdir) + 1 + 4 + 1);
    while ((dentry = p_readdir(clandir)) != NULL)
    {
	if (dentry[0] == '.')
	    continue;

	if (strlen(dentry) > 4)
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "found too long clan filename in clandir \"%s\"", dentry);
	    continue;
	}

	sprintf(pathname, "%s/%s", clansdir, dentry);

	clantag = str_to_clantag(dentry);

	if ((fp = fopen(pathname, "r")) == NULL)
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "can't open clanfile \"%s\"", pathname);
	    continue;
	}

	clan = xmalloc(sizeof(t_clan));
	clan->clantag = clantag;

	if (!fgets(line, 1024, fp))
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "invalid clan file: no first line");
	    xfree((void*)clan);
	    continue;
	}

	clanname = line;
	if (*clanname != '"')
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "invalid clan file: invalid first line");
	    xfree((void*)clan);
	    continue;
	}
	clanname++;
	p = strchr(clanname, '"');
	if (!p)
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "invalid clan file: invalid first line");
	    xfree((void*)clan);
	    continue;
	}
	*p = '\0';
	if (strlen(clanname) >= CLAN_NAME_MAX)
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "invalid clan file: invalid first line");
	    xfree((void*)clan);
	    continue;
	}

	p++;
	if (*p != ',')
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "invalid clan file: invalid first line");
	    xfree((void*)clan);
	    continue;
	}
	p++;
	if (*p != '"')
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "invalid clan file: invalid first line");
	    xfree((void*)clan);
	    continue;
	}
	motd = p + 1;
	p = strchr(motd, '"');
	if (!p)
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "invalid clan file: invalid first line");
	    xfree((void*)clan);
	    continue;
	}
	*p = '\0';

	if (sscanf(p + 1, ",%d,%d\n", &cid, &creation_time) != 2)
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "invalid first line in clanfile");
	    xfree((void*)clan);
	    continue;
	}
	clan->clanname = xstrdup(clanname);
	clan->clan_motd = xstrdup(motd);
	clan->clanid = cid;
	clan->creation_time = (time_t) creation_time;
	clan->created = 1;
	clan->modified = 0;
	clan->channel_type = prefs_get_clan_channel_default_private();

	eventlog(eventlog_level_trace, __FUNCTION__, "name: %s motd: %s clanid: %i time: %i", clanname, motd, cid, creation_time);

	clan->members = list_create();

	while (fscanf(fp, "%i,%c,%i\n", &member_uid, &member_status, &member_join_time) == 3)
	{
	    member = xmalloc(sizeof(t_clanmember));
	    if (!(member->memberacc = accountlist_find_account_by_uid(member_uid)))
	    {
		eventlog(eventlog_level_error, __FUNCTION__, "cannot find uid %u", member_uid);
		xfree((void *) member);
		continue;
	    }
	    member->status = member_status - '0';
	    member->join_time = member_join_time;
	    member->clan = clan;

	    if ((member->status == CLAN_NEW) && (time(NULL) - member->join_time > prefs_get_clan_newer_time() * 3600))
	    {
		member->status = CLAN_PEON;
		clan->modified = 1;
	    }

	    list_append_data(clan->members, member);

	    account_set_clanmember(member->memberacc, member);
	    eventlog(eventlog_level_trace, __FUNCTION__, "added member: uid: %i status: %c join_time: %i", member_uid, member_status + '0', member_join_time);
	}

	fclose(fp);

	cb(clan);

    }

    xfree((void *) pathname);

    if (p_closedir(clandir) < 0)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "unable to close clan directory \"%s\" (p_closedir: %s)", clansdir, pstrerror(errno));
    }
    eventlog(eventlog_level_trace, __FUNCTION__, "finished reading clans");

    return 0;
}

static int file_write_clan(void *data)
{
    FILE *fp;
    t_elem *curr;
    t_clanmember *member;
    char *clanfile;
    t_clan *clan = (t_clan *) data;

    clanfile = xmalloc(strlen(clansdir) + 1 + 4 + 1);
    sprintf(clanfile, "%s/%c%c%c%c", clansdir, clan->clantag >> 24, (clan->clantag >> 16) & 0xff, (clan->clantag >> 8) & 0xff, clan->clantag & 0xff);

    if ((fp = fopen(clanfile, "w")) == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "can't open clanfile \"%s\"", clanfile);
	xfree((void *) clanfile);
	return -1;
    }

    fprintf(fp, "\"%s\",\"%s\",%i,%i\n", clan->clanname, clan->clan_motd, clan->clanid, (int) clan->creation_time);

    LIST_TRAVERSE(clan->members, curr)
    {
	if (!(member = elem_get_data(curr)))
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "got NULL elem in list");
	    continue;
	}
	if ((member->status == CLAN_NEW) && (time(NULL) - member->join_time > prefs_get_clan_newer_time() * 3600))
	    member->status = CLAN_PEON;
	fprintf(fp, "%i,%c,%u\n", account_get_uid(member->memberacc), member->status + '0', (unsigned) member->join_time);
    }

    fclose(fp);
    xfree((void *) clanfile);
    return 0;
}

static int file_remove_clan(int clantag)
{
    char *tempname;

    tempname = xmalloc(strlen(clansdir) + 1 + 4 + 1);
    sprintf(tempname, "%s/%c%c%c%c", clansdir, clantag >> 24, (clantag >> 16) & 0xff, (clantag >> 8) & 0xff, clantag & 0xff);
    if (remove((const char *) tempname) < 0)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "could not delete clan file \"%s\" (remove: %s)", (char *) tempname, pstrerror(errno));
	xfree(tempname);
	return -1;
    }
    xfree(tempname);
    return 0;
}

static int file_remove_clanmember(int uid)
{
    return 0;
}

static int file_load_teams(t_load_teams_func cb)
{
    char const *dentry;
    t_pdir *teamdir;
    char *pathname;
    unsigned int teamid;
    t_team *team;
    FILE *fp;
    char * line;
    unsigned int fteamid,lastgame;
    unsigned char size;
    char clienttag[5];
    int i;

    if (cb == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "get NULL callback");
	return -1;
    }

    if (!(teamdir = p_opendir(teamsdir)))
    {
	eventlog(eventlog_level_error, __FUNCTION__, "unable to open team directory \"%s\" for reading (p_opendir: %s)", teamsdir, pstrerror(errno));
	return -1;
    }
    eventlog(eventlog_level_trace, __FUNCTION__, "start reading teams");

    pathname = xmalloc(strlen(teamsdir) + 1 + 8 + 1);
    while ((dentry = p_readdir(teamdir)) != NULL)
    {
	if (dentry[0] == '.')
	    continue;

	if (strlen(dentry) != 8)
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "found invalid team filename in teamdir \"%s\"", dentry);
	    continue;
	}

	sprintf(pathname, "%s/%s", teamsdir, dentry);

	teamid = (unsigned int)strtoul(dentry,NULL,16); // we use hexadecimal teamid as filename

	if ((fp = fopen(pathname, "r")) == NULL)
	{
	    eventlog(eventlog_level_error, __FUNCTION__, "can't open teamfile \"%s\"", pathname);
	    continue;
	}

	team = xmalloc(sizeof(t_team));
	team->teamid = teamid;

	if (!(line = file_get_line(fp)))
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"invalid team file: file is empty");
	    goto load_team_failure;
	}
	
	if (sscanf(line,"%u,%c,%4s,%u",&fteamid,&size,clienttag,&lastgame)!=4)
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"invalid team file: invalid number of arguments on first line");
	    goto load_team_failure;
	}
	
	if (fteamid != teamid)
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"invalid team file: filename and stored teamid don't match");
	    goto load_team_failure;
	}
	
	size -='0';
	if ((size<2) || (size>4))
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"invalid team file: invalid team size");
	    goto load_team_failure;
	}
	team->size = size;
	
	if (!(tag_check_client(team->clienttag = tag_str_to_uint(clienttag))))
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"invalid team file: invalid clienttag");
	    goto load_team_failure;
	}
	team->lastgame = (time_t)lastgame;

	if (!(line= file_get_line(fp)))
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"invalid team file: missing 2nd line");
	    goto load_team_failure;
	}

	if (sscanf(line,"%u,%u,%u,%u",&team->teammembers[0],&team->teammembers[1],&team->teammembers[2],&team->teammembers[3])!=4)
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"invalid team file: invalid number of arguments on 2nd line");
	    goto load_team_failure;
	}

	for (i=0; i<MAX_TEAMSIZE;i++)
	{
	   if (i<size)
	   {
		if ((team->teammembers[i]==0))
		{
	    		eventlog(eventlog_level_error,__FUNCTION__,"invalid team file: too few members");
	    		goto load_team_failure;
		}
	   }
	   else
	   {
	   	if ((team->teammembers[i]!=0))
		{
	    		eventlog(eventlog_level_error,__FUNCTION__,"invalid team file: too many members");
	    		goto load_team_failure;
		}

	   }
	   team->members[i] = NULL;
	}

	if (!(line= file_get_line(fp)))
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"invalid team file: missing 3rd line");
	    goto load_team_failure;
	}

	if (sscanf(line,"%d,%d,%d,%d,%d",&team->wins,&team->losses,&team->xp,&team->level,&team->rank)!=5)
	{
	    eventlog(eventlog_level_error,__FUNCTION__,"invalid team file: invalid number of arguments on 3rd line");
	    goto load_team_failure;
	}

	eventlog(eventlog_level_trace,__FUNCTION__,"succesfully loaded team %s",dentry);
	cb(team);
	
	goto load_team_success;
	load_team_failure:
	  xfree((void*)team);
	  eventlog(eventlog_level_error,__FUNCTION__,"error while reading file \"%s\"",dentry);

	load_team_success:

	file_get_line(NULL); // clear file_get_line buffer
	fclose(fp);


    }

    xfree((void *) pathname);

    if (p_closedir(teamdir) < 0)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "unable to close team directory \"%s\" (p_closedir: %s)", teamsdir, pstrerror(errno));
    }
    eventlog(eventlog_level_trace, __FUNCTION__, "finished reading teams");

    return 0;
}

static int file_write_team(void *data)
{
    FILE *fp;
    char *teamfile;
    t_team *team = (t_team *) data;

    teamfile = xmalloc(strlen(teamsdir) + 1 + 8 + 1);
    sprintf(teamfile, "%s/%08x", teamsdir, team->teamid);

    if ((fp = fopen(teamfile, "w")) == NULL)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "can't open teamfile \"%s\"", teamfile);
	xfree((void *) teamfile);
	return -1;
    }

    fprintf(fp,"%u,%c,%s,%u\n",team->teamid,team->size+'0',clienttag_uint_to_str(team->clienttag),(unsigned int)team->lastgame);
    fprintf(fp,"%u,%u,%u,%u\n",team->teammembers[0],team->teammembers[1],team->teammembers[2],team->teammembers[3]);
    fprintf(fp,"%d,%d,%d,%d,%d\n",team->wins,team->losses,team->xp,team->level,team->rank);

    fclose(fp);
    xfree((void *) teamfile);
    
    return 0;
}

static int file_remove_team(unsigned int teamid)
{

    char *tempname;

    tempname = xmalloc(strlen(clansdir) + 1 + 8 + 1);
    sprintf(tempname, "%s/%08x", clansdir, teamid);
    if (remove((const char *) tempname) < 0)
    {
	eventlog(eventlog_level_error, __FUNCTION__, "could not delete team file \"%s\" (remove: %s)", (char *) tempname, pstrerror(errno));
	xfree(tempname);
	return -1;
    }
    xfree(tempname);
    
    return 0;
}
