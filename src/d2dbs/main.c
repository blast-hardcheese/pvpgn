/*
 * Copyright (C) 2000,2001	Onlyer	(onlyer@263.net)
 * Copyright (C) 2005	        Olaf Freyer (aaron@cs.tu-berlin.de)
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
#include "setup.h"

#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
#else
# ifdef HAVE_MALLOC_H
#  include <malloc.h>
# endif
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <errno.h>
#ifdef HAVE_STRING_H
# include <string.h>
#else
# ifdef HAVE_STRINGS_H
#  include <strings.h>
# endif
# ifdef HAVE_MEMORY_H
#  include <memory.h>
# endif
#endif
#include "compat/strdup.h"
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#include "prefs.h"
#include "cmdline.h"
#include "version.h"
#include "common/eventlog.h"
#ifdef WIN32
# include "win32/service.h"
#endif
#include "handle_signal.h"
#include "dbserver.h"
#include "common/xalloc.h"
#include "compat/strerror.h"
#ifdef WIN32_GUI
# include "win32/winmain.h"
#endif
#include "common/setup_after.h"

static FILE * eventlog_fp;

char serviceLongName[] = "d2dbs service";
char serviceName[] = "d2dbs";
char serviceDescription[] = "Diablo 2 DataBase Server";

int g_ServiceStatus = -1;

static int init(void);
static int cleanup(void);
static int config_init(int argc, char * * argv);
static int config_cleanup(void);
static int setup_daemon(void);
static char * write_to_pidfile(void);


#ifdef DO_DAEMONIZE
static int setup_daemon(void)
{
	int pid;
	
	if (chdir("/")<0) {
		eventlog(eventlog_level_error,__FUNCTION__,"can not change working directory to root directory (chdir: %s)",pstrerror(errno));
		return -1;
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	if (!cmdline_get_foreground()) {
		close(STDERR_FILENO);
	}

	switch ((pid=fork())) {
		case 0:
			break;
		case -1:
			eventlog(eventlog_level_error,__FUNCTION__,"error create child process (fork: %s)",pstrerror(errno));
			return -1;
		default:
			return pid;
	}
	umask(0);
	setsid();
	return 0;
}
#endif

static char * write_to_pidfile(void)
{
	char *pidfile = xstrdup(d2dbs_prefs_get_pidfile());
	
	if (pidfile[0]=='\0') {
		xfree((void *)pidfile); /* avoid warning */
		return NULL;
	}
	
	if (pidfile) {
#ifdef HAVE_GETPID
		FILE * fp;
		
		if (!(fp = fopen(pidfile,"w"))) {
			eventlog(eventlog_level_error,__FUNCTION__,"unable to open pid file \"%s\" for writing (fopen: %s)",pidfile,pstrerror(errno));
			xfree((void *)pidfile); /* avoid warning */
			return NULL;
		} else {
			fprintf(fp,"%u",(unsigned int)getpid());
			if (fclose(fp)<0)
				eventlog(eventlog_level_error,__FUNCTION__,"could not close pid file \"%s\" after writing (fclose: %s)",pidfile,pstrerror(errno));
		}
		
#else
		eventlog(eventlog_level_warn,__FUNCTION__,"no getpid() system call, disable pid file in d2dbs.conf");
		xfree((void *)pidfile); /* avoid warning */
		return NULL;
#endif
	}
	
	return pidfile;
}

static int init(void)
{
	return 0;
}

static int cleanup(void)
{
	return 0;
}
	
static int config_init(int argc, char * * argv)
{
    char const * levels;
    char *       temp;
    char const * tok;
    int		 pid;

	if (cmdline_load(argc, argv)<0) {
		return -1;
	}

#ifdef DO_DAEMONIZE
	if (!cmdline_get_foreground()) {
	    	if (!((pid = setup_daemon()) == 0)) {
		        return pid;
		}
	}
#endif

	if (d2dbs_prefs_load(cmdline_get_preffile())<0) {
		eventlog(eventlog_level_error,__FUNCTION__,"error loading configuration file %s",cmdline_get_preffile());
		return -1;
	}
	
    eventlog_clear_level();
    if ((levels = d2dbs_prefs_get_loglevels()))
    {
        temp = xstrdup(levels);
        tok = strtok(temp,","); /* strtok modifies the string it is passed */

        while (tok)
        {
        if (eventlog_add_level(tok)<0)
            eventlog(eventlog_level_error,__FUNCTION__,"could not add log level \"%s\"",tok);
        tok = strtok(NULL,",");
        }

        xfree(temp);
    }

#ifdef DO_DAEMONIZE
	if (cmdline_get_foreground()) {
		eventlog_set(stderr);
	} else
#endif
	{
	    if (cmdline_get_logfile()) {
		if (eventlog_open(cmdline_get_logfile())<0) {
			eventlog(eventlog_level_error,__FUNCTION__,"error open eventlog file %s",cmdline_get_logfile());
			return -1;
		}
	    } else {
		if (eventlog_open(d2dbs_prefs_get_logfile())<0) {
			eventlog(eventlog_level_error,__FUNCTION__,"error open eventlog file %s",d2dbs_prefs_get_logfile());
			return -1;
		}
	    }
	}
	return 0;
}

static int config_cleanup(void)
{
	d2dbs_prefs_unload();
	cmdline_unload(); 
	eventlog_close();
	if (eventlog_fp) fclose(eventlog_fp);
	return 0;
}

#ifdef WIN32_GUI
extern int server_main(int argc, char * * argv)
#else
extern int main(int argc, char * * argv)
#endif
{
	int pid;
	char * pidfile;
	
	eventlog_set(stderr);
	pid = config_init(argc, argv);
	if (!(pid == 0)) {
//		if (pid==1) pid=0;
		return pid;
	}
	pidfile = write_to_pidfile();
	eventlog(eventlog_level_info,__FUNCTION__,D2DBS_VERSION);
	if (init()<0) {
		eventlog(eventlog_level_error,__FUNCTION__,"failed to init");
		return -1;
	} else {
		eventlog(eventlog_level_info,__FUNCTION__,"server initialized");
	}
#ifndef WIN32 
	d2dbs_handle_signal_init();
#endif
	dbs_server_main();
	cleanup();
	if (pidfile) {
		if (remove(pidfile)<0)
			eventlog(eventlog_level_error,__FUNCTION__,"could not remove pid file \"%s\" (remove: %s)",pidfile,pstrerror(errno));
		xfree((void *)pidfile); /* avoid warning */
	}
	config_cleanup();
	return 0;
}
