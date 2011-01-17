/*
 * Copyright (C) 2001  Marco Ziech (mmz@gmx.net)
 * Copyright (C) 2005  Bryan Biedenkapp (gatekeep@gmail.com)
 * Copyright (C) 2006,2007,2008  Pelish (pelish@gmail.com)
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
 
#define CONNECTION_INTERNAL_ACCESS
#include "common/setup_before.h"
#include "common/util.h"
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
# ifdef HAVE_MEMORY_H
#  include <memory.h>
# endif
#endif
#include "compat/strdup.h"
#include "compat/strcasecmp.h"
#include <errno.h>
#include "compat/strerror.h"
#include "common/irc_protocol.h"
#include "common/packet.h"
#include "common/eventlog.h"
#include "connection.h"
#include "common/bn_type.h"
#include "common/field_sizes.h"
#include "common/addr.h"
#include "common/version.h"
#include "common/queue.h"
#include "common/list.h"
#include "common/bnethash.h"
#include "common/bnethashconv.h"
#include "message.h"
#include "account.h"
#include "account_wrap.h"
#include "channel.h"
#include "irc.h"
#include "prefs.h"
#include "tick.h"
#include "timer.h"
#include "server.h"
#include "command.h"
#include "handle_irc.h"
#include "topic.h"
#include "command_groups.h"
#include "common/tag.h"
#include "common/xalloc.h"
#include "ctype.h"
#include "common/setup_after.h"

typedef int (* t_irc_command)(t_connection * conn, int numparams, char ** params, char * text);

typedef struct {
	const char     * irc_command_string;
	t_irc_command    irc_command_handler;
} t_irc_command_table_row;

static int _handle_nick_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_user_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_ping_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_pong_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_pass_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_privmsg_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_notice_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_quit_command(t_connection * conn, int numparams, char ** params, char * text);


static int _handle_who_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_list_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_topic_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_join_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_names_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_mode_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_userhost_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_ison_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_whois_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_part_command(t_connection * conn, int numparams, char ** params, char * text);


static int _handle_cvers_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_verchk_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_lobcount_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_whereto_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_apgar_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_serial_command(t_connection * conn, int numparams, char ** params, char * text);

static int _handle_squadinfo_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_setopt_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_setcodepage_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_setlocale_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_getcodepage_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_getlocale_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_joingame_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_gameopt_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_finduserex_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_page_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_startg_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_kick_command(t_connection * conn, int numparams, char ** params, char * text);

static int _handle_listsearch_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_rungsearch_command(t_connection * conn, int numparams, char ** params, char * text);
static int _handle_highscore_command(t_connection * conn, int numparams, char ** params, char * text);

/* state "connected" handlers */
static const t_irc_command_table_row irc_con_command_table[] =
{
	{ "NICK"		, _handle_nick_command },
	{ "USER"		, _handle_user_command },
	{ "PING"		, _handle_ping_command },
	{ "PONG"		, _handle_pong_command },
	{ "PASS"		, _handle_pass_command },
	{ "PRIVMSG"		, _handle_privmsg_command },
	{ "NOTICE"		, _handle_notice_command },
	{ "QUIT"		, _handle_quit_command },

	{ "CVERS"		, _handle_cvers_command },	
	{ "VERCHK"		, _handle_verchk_command },	
	{ "LOBCOUNT"	, _handle_lobcount_command },
	{ "WHERETO"		, _handle_whereto_command },
	{ "APGAR"		, _handle_apgar_command },	
	{ "SERIAL"		, _handle_serial_command },	

    /* Ladder server commands */
	{ "LISTSEARCH"	, _handle_listsearch_command },
	{ "RUNGSEARCH"	, _handle_rungsearch_command },
	{ "HIGHSCORE"	, _handle_highscore_command },

	{ NULL			, NULL }
};

/* state "logged in" handlers */
static const t_irc_command_table_row irc_log_command_table[] =
{
	{ "WHO"			, _handle_who_command },
	{ "LIST"		, _handle_list_command },
	{ "TOPIC"		, _handle_topic_command },
	{ "JOIN"		, _handle_join_command },
	{ "NAMES"		, _handle_names_command },
	{ "MODE"		, _handle_mode_command },
	{ "USERHOST"		, _handle_userhost_command },
	{ "ISON"		, _handle_ison_command },
	{ "WHOIS"		, _handle_whois_command },
	{ "PART"		, _handle_part_command },

	{ "SQUADINFO"	, _handle_squadinfo_command },	
	{ "SETOPT"		, _handle_setopt_command },		
	{ "SETCODEPAGE"	, _handle_setcodepage_command },
	{ "SETLOCALE"	, _handle_setlocale_command },	
	{ "GETCODEPAGE"	, _handle_getcodepage_command },
	{ "GETLOCALE"	, _handle_getlocale_command },
	{ "JOINGAME"	, _handle_joingame_command },
	{ "GAMEOPT"		, _handle_gameopt_command },
	{ "FINDUSEREX"	, _handle_finduserex_command },
	{ "PAGE"		, _handle_page_command },
	{ "STARTG"		, _handle_startg_command },
	{ "KICK"		, _handle_kick_command },	

	{ NULL			, NULL }
};


static int handle_irc_con_command(t_connection * conn, char const * command, int numparams, char ** params, char * text)
{
  t_irc_command_table_row const *p;

  for (p = irc_con_command_table; p->irc_command_string != NULL; p++) {
    if (strcasecmp(command, p->irc_command_string)==0) {
	  if (p->irc_command_handler != NULL) 
		  return ((p->irc_command_handler)(conn,numparams,params,text));
	}
  }
  return -1;
}

static int handle_irc_log_command(t_connection * conn, char const * command, int numparams, char ** params, char * text)
{
  t_irc_command_table_row const *p;

  for (p = irc_log_command_table; p->irc_command_string != NULL; p++) {
    if (strcasecmp(command, p->irc_command_string)==0) {
	  if (p->irc_command_handler != NULL) 
		  return ((p->irc_command_handler)(conn,numparams,params,text));
	}
  }
  return -1;
}

static int handle_irc_line(t_connection * conn, char const * ircline)
{   
	/* [:prefix] <command> [[param1] [param2] ... [paramN]] [:<text>] */
    char * line; /* copy of ircline */
    char * prefix = NULL; /* optional; mostly NULL */
    char * command; /* mandatory */
    char ** params = NULL; /* optional (array of params) */
    char * text = NULL; /* optional */
	char * bnet_command = NULL;  /* amadeo: used for battle.net.commands */
    int unrecognized_before = 0;
	int linelen; /* amadeo: counter for stringlenghts */

    int numparams = 0;
    char * tempparams;
	int i;
 	char paramtemp[MAX_IRC_MESSAGE_LEN*2];
	int first = 1;

    if (!conn) {
	eventlog(eventlog_level_error,__FUNCTION__,"got NULL connection");
	return -1;
    }
    if (!ircline) {
	eventlog(eventlog_level_error,__FUNCTION__,"got NULL ircline");
	return -1;
    }
    if (ircline[0] == '\0') {
	eventlog(eventlog_level_error,__FUNCTION__,"got empty ircline");
	return -1;
    }
	//amadeo: code was sent by some unknown fellow of pvpgn, prevents buffer-overflow for
	// too long irc-lines

    if (strlen(ircline)>254) {
        char * tmp = (char *)ircline;
	eventlog(eventlog_level_warn,__FUNCTION__,"line to long, truncation...");
	tmp[254]='\0';
    }    

    line = xstrdup(ircline);

    /* split the message */
    if (line[0] == ':') {
	/* The prefix is optional and is rarely provided */
	prefix = line;
	if (!(command = strchr(line,' '))) {
	    eventlog(eventlog_level_warn,__FUNCTION__,"got malformed line (missing command)");
	    xfree(line);
	    return -1;
	}
	*command++ = '\0';
    } 
	else {
	/* In most cases command is the first thing on the line */
	command = line;
    }
    
    tempparams = strchr(command,' ');
    if (tempparams) {
	*tempparams++ = '\0';
	 if (tempparams[0]==':') {
	    text = tempparams+1; /* theres just text, no params. skip the colon */
	} else {
	    for (i=0;tempparams[i]!='\0';i++) {
	    	if ((tempparams[i]==' ')&&(tempparams[i+1]==':')) {
		    text = tempparams+i; 
		    *text++ = '\0';
		    text++; /* skip the colon */
		    break; /* text found, stop search */
	    	}
	    }
	    params = irc_get_paramelems(tempparams);
	}
    }

    if (params) {
	/* count parameters */
	for (numparams=0;params[numparams];numparams++);
    }

	memset(paramtemp,0,sizeof(paramtemp));
    	for (i=0;((numparams>0)&&(params[i]));i++) {
		if (!first) 
			strcat(paramtemp," ");
	    strcat(paramtemp,"\"");
	    strcat(paramtemp,params[i]);
	    strcat(paramtemp,"\"");
	    first = 0;
    	}

    	eventlog(eventlog_level_debug,__FUNCTION__,"[%d] got \"%s\" \"%s\" [%s] \"%s\"",conn_get_socket(conn),((prefix)?(prefix):("")),command,paramtemp,((text)?(text):("")));

    if (conn_get_state(conn)==conn_state_connected) {
	t_timer_data temp;
	
	conn_set_state(conn,conn_state_bot_username);
	temp.n = prefs_get_irc_latency();
	conn_test_latency(conn,time(NULL),temp);
    }

	if (handle_irc_con_command(conn, command, numparams, params, text)!=-1) {}
    else if (conn_get_state(conn)!=conn_state_loggedin) {
	char temp[MAX_IRC_MESSAGE_LEN+1];
	
	if ((38+strlen(command)+16+1)<sizeof(temp)) {
	    sprintf(temp,":Unrecognized command \"%s\" (before login)",command);
	    irc_send(conn,ERR_UNKNOWNCOMMAND,temp);
	} else {
	    irc_send(conn,ERR_UNKNOWNCOMMAND,":Unrecognized command (before login)");
	}
    } else {
        /* command is handled later */
	unrecognized_before = 1;
    }
    /* --- The following should only be executable after login --- */
    if ((conn_get_state(conn)==conn_state_loggedin)&&(unrecognized_before)) {

		if (handle_irc_log_command(conn, command, numparams, params, text)!=-1) {}
		else if ((strstart(command,"LAG")!=0)&&(strstart(command,"JOIN")!=0)){
			linelen = strlen (ircline);
			bnet_command = xmalloc(linelen + 2);
			bnet_command[0]='/';
			strcpy(bnet_command + 1, ircline);
			handle_command(conn,bnet_command); 
			xfree((void*)bnet_command);
		}
    } /* loggedin */
    if (params)
	irc_unget_paramelems(params);
    xfree(line);
    return 0;
}

extern int handle_irc_packet(t_connection * conn, t_packet const * const packet)
{
    unsigned int i;
    char ircline[MAX_IRC_MESSAGE_LEN];
    int ircpos;
    char const * data;

    if (!packet) {
	eventlog(eventlog_level_error,__FUNCTION__,"got NULL packet");
	return -1;
    }
    if ((conn_get_class(conn) != conn_class_irc) &&
		(conn_get_class(conn) != conn_class_wol)) {
	eventlog(eventlog_level_error,__FUNCTION__,"FIXME: handle_irc_packet without any reason (conn->class != conn_class_irc)");
	return -1;
    }
	
    /* eventlog(eventlog_level_debug,__FUNCTION__,"got \"%s\"",packet_get_raw_data_const(packet,0)); */

    memset(ircline,0,sizeof(ircline));
    data = conn_get_ircline(conn); /* fetch current status */
    if (data)
	strcpy(ircline,data);
    ircpos = strlen(ircline);
    data = packet_get_raw_data_const(packet,0);	
    for (i=0; i < packet_get_size(packet); i++) {
	if ((data[i] == '\r')||(data[i] == '\0')) {
	    /* kindly ignore \r and NUL ... */
	} else if (data[i] == '\n') {
	    /* end of line */
	    handle_irc_line(conn,ircline);
	    memset(ircline,0,sizeof(ircline));
	    ircpos = 0;
	} else {
	    if (ircpos < MAX_IRC_MESSAGE_LEN-1)
		ircline[ircpos++] = data[i];
	    else {
		ircpos++; /* for the statistic :) */
	    	eventlog(eventlog_level_warn,__FUNCTION__,"[%d] client exceeded maximum allowed message length by %d characters",conn_get_socket(conn),ircpos-MAX_IRC_MESSAGE_LEN);
		if ((ircpos-MAX_IRC_MESSAGE_LEN)>100) {
		    /* automatic flood protection */
		    eventlog(eventlog_level_error,__FUNCTION__,"[%d] excess flood",conn_get_socket(conn));
		    return -1;
		}
	    }
	}
    }
    conn_set_ircline(conn,ircline); /* write back current status */
    return 0;
}

static int _handle_nick_command(t_connection * conn, int numparams, char ** params, char * text)
{
	/* FIXME: more strict param checking */

	if ((conn_get_loggeduser(conn))&&
	    (conn_get_state(conn)!=conn_state_bot_password && 
	     conn_get_state(conn)!=conn_state_bot_username)) {
	    irc_send(conn,ERR_RESTRICTED,":You can't change your nick after login");
	} 
	else {
	    if ((params)&&(params[0])) {
			if (conn_get_loggeduser(conn))
			    irc_send_cmd2(conn,conn_get_loggeduser(conn),"NICK","",params[0]);
			conn_set_loggeduser(conn,params[0]);
	    }
	    else if (text) {
			if (conn_get_loggeduser(conn))
			    irc_send_cmd2(conn,conn_get_loggeduser(conn),"NICK","",text);
			conn_set_loggeduser(conn,text);
	    }
	    else
	        irc_send(conn,ERR_NEEDMOREPARAMS,":Too few arguments to NICK");
	    if ((conn_get_user(conn))&&(conn_get_loggeduser(conn)))
			irc_welcome(conn); /* only send the welcome if we have USER and NICK */
	}
	return 0;
}

static int _handle_user_command(t_connection * conn, int numparams, char ** params, char * text)
{
	/* RFC 2812 says: */
	/* <user> <mode> <unused> :<realname>*/
	/* ircII and X-Chat say: */
	/* mz SHODAN localhost :Marco Ziech */
	/* BitchX says: */
	/* mz +iws mz :Marco Ziech */
	/* Don't bother with, params 1 and 2 anymore they don't contain what they should. */
	char * user = NULL;
	char * realname = NULL;
	t_account * a;

	if ((numparams>=3)&&(params[0])&&(text)) {
	    user = params[0];
	    realname = text;

		if (conn_get_wol(conn) == 1) {
			user = (char *)conn_get_loggeduser(conn);
			realname = (char *)conn_get_loggeduser(conn);
			
        	if (conn_get_user(conn)) {
		irc_send(conn,ERR_ALREADYREGISTRED,":You are already registred");
        } 
			else {
				eventlog(eventlog_level_debug,__FUNCTION__,"[%d][** WOL **] got USER: user=\"%s\"",conn_get_socket(conn),user);

                a = accountlist_find_account(user);
                if (!a) {
                   if((conn_get_wol(conn) == 1)) {
                        t_account * tempacct;
                        t_hash pass_hash;
                        char * pass = xstrdup(conn_wol_get_apgar(conn)); /* FIXME: Do not use bnet passhash when we have wol passhash */
                        int j;
            
            			for (j=0; j<strlen(pass); j++)
            				if (isupper((int)pass[j])) pass[j] = tolower((int)pass[j]);
            
            			bnet_hash(&pass_hash,strlen(pass),pass);
            
            			tempacct = accountlist_create_account(user,hash_get_str(pass_hash));
            			if (!tempacct) {
                            return 0;
            			}
                   }
                }

			conn_set_user(conn,user);
			conn_set_owner(conn,realname);
			if (conn_get_loggeduser(conn))
				irc_welcome(conn); /* only send the welcome if we have USER and NICK */
	    	}
    	} 
		else {
			if (conn_get_user(conn)) {
				irc_send(conn,ERR_ALREADYREGISTRED,":You are already registred");
			} 
			else {
				eventlog(eventlog_level_debug,__FUNCTION__,"[%d] got USER: user=\"%s\" realname=\"%s\"",conn_get_socket(conn),user,realname);
				conn_set_user(conn,user);
				conn_set_owner(conn,realname);
				if (conn_get_loggeduser(conn))
					irc_welcome(conn); /* only send the welcome if we have USER and NICK */
    		}
		}
   	} 
	else {
	    irc_send(conn,ERR_NEEDMOREPARAMS,":Too few arguments to USER");
    	}
	return 0;
}

static int _handle_ping_command(t_connection * conn, int numparams, char ** params, char * text)
{
	if((conn_get_wol(conn) == 1))
	    return 0;

	/* Dizzy: just ignore this because RFC says we should not reply client PINGs
	 * NOTE: RFC2812 doesn't seem to be very expressive about this ... */
	if (numparams)
	    irc_send_pong(conn,params[0]);
	else
	    irc_send_pong(conn,text);
	return 0;
}

static int _handle_pong_command(t_connection * conn, int numparams, char ** params, char * text)
{
	if((conn_get_wol(conn) == 1))
	    return 0;
	
	/* NOTE: RFC2812 doesn't seem to be very expressive about this ... */
	if (conn_get_ircping(conn)==0) {
	    eventlog(eventlog_level_warn,__FUNCTION__,"[%d] PONG without PING",conn_get_socket(conn));
	} 
	else {
	    unsigned int val = 0;
	    char * sname;

	    if (numparams>=1) {  
	        val = strtoul(params[0],NULL,10);
		sname = params[0];
	    }
	    else if (text) {
	    	val = strtoul(text,NULL,10);
		sname = text;
	    }
	    else {
		val = 0;
		sname = 0;
	    }

	    if (conn_get_ircping(conn) != val) {
	    	if ((!(sname)) || (strcmp(sname,server_get_hostname())!=0)) {
			/* Actually the servername should not be always accepted but we aren't that pedantic :) */
			eventlog(eventlog_level_warn,__FUNCTION__,"[%d] got bad PONG (%u!=%u && %s!=%s)",conn_get_socket(conn),val,conn_get_ircping(conn),sname,server_get_hostname());
			return -1;
		}
	    }
	    conn_set_latency(conn,get_ticks()-conn_get_ircping(conn));
	    eventlog(eventlog_level_debug,__FUNCTION__,"[%d] latency is now %d (%u-%u)",conn_get_socket(conn),get_ticks()-conn_get_ircping(conn),get_ticks(),conn_get_ircping(conn));
	    conn_set_ircping(conn,0);
	}
	return 0;
}

static int _handle_pass_command(t_connection * conn, int numparams, char ** params, char * text)
{
	if ((!conn_get_ircpass(conn))&&(conn_get_state(conn)==conn_state_bot_username)) {
		t_hash h;

	    if (numparams>=1) {
			bnet_hash(&h,strlen(params[0]),params[0]);
			conn_set_ircpass(conn,hash_get_str(h));
	    } 
		else
			irc_send(conn,ERR_NEEDMOREPARAMS,":Too few arguments to PASS");
    } 
	else {
	    eventlog(eventlog_level_warn,__FUNCTION__,"[%d] client tried to set password twice with PASS",conn_get_socket(conn));
    }
	return 0;
}

static int _handle_privmsg_command(t_connection * conn, int numparams, char ** params, char * text)
{
	if ((numparams>=1)&&(text)) 
	{
	    int i;
	    char ** e;
	
	    e = irc_get_listelems(params[0]);
	    /* FIXME: support wildcards! */
		
		/* start amadeo: code was sent by some unkown fellow of pvpgn (maybe u wanna give us your name 
		   for any credits), it adds nick-registration, i changed some things here and there... */
	    for (i=0;((e)&&(e[i]));i++) {
    		if (strcasecmp(e[i],"NICKSERV")==0) {
 				char * pass;
				char * p;
			
 				pass = strchr(text,' ');
 				if (pass)
 		    		*pass++ = '\0';

				if (strcasecmp(text,"identify")==0) {
				    switch (conn_get_state(conn)) {
					case conn_state_bot_password:
					{
							if (pass) {
 		    				t_hash h;
 
						for (p = pass; *p; p++)
						    if (isupper((int)*p)) *p = tolower(*p);
 		    				bnet_hash(&h,strlen(pass),pass);
 		    				irc_authenticate(conn,hash_get_str(h));
 					    }
							else {
								irc_send_cmd(conn,"NOTICE",":Syntax: IDENTIFY <password> (max 16 characters)");
					    }
					    break;
					}
					case conn_state_loggedin:
					{
					    irc_send_cmd(conn,"NOTICE",":You don't need to IDENTIFY");
					    break;
					}
					default: ;
					    eventlog(eventlog_level_trace,__FUNCTION__,"got /msg in unexpected connection state (%s)",conn_state_get_str(conn_get_state(conn)));
				    }
				} 
				else if (strcasecmp(text,"register")==0) {
					unsigned int j;
					t_hash       passhash;
					t_account  * temp;
					char         msgtemp[MAX_MESSAGE_LEN];
					char       * username=(char *)conn_get_loggeduser(conn);						
					
					if (account_check_name(username)<0) {
						message_send_text(conn,message_type_error,conn,"Account name contains invalid symbol!");
						break;
					}

					if (!pass || pass[0]=='\0' || (strlen(pass)>16) ) {
						message_send_text(conn,message_type_error,conn,":Syntax: REGISTER <password> (max 16 characters)");
						break;
					}
	
					
					for (j=0; j<strlen(pass); j++)
						if (isupper((int)pass[j])) pass[j] = tolower((int)pass[j]);
	
					bnet_hash(&passhash,strlen(pass),pass);
	
					sprintf(msgtemp,"Trying to create account \"%s\" with password \"%s\"",username,pass);
					message_send_text(conn,message_type_info,conn,msgtemp);					

					temp = accountlist_create_account(username,hash_get_str(passhash));
					if (!temp) {
						message_send_text(conn,message_type_error,conn,"Failed to create account!");
						eventlog(eventlog_level_debug,__FUNCTION__,"[%d] account \"%s\" not created (failed)",conn_get_socket(conn),username);
						conn_unget_chatname(conn,username);
						break;
					}

					sprintf(msgtemp,"Account "UID_FORMAT" created.",account_get_uid(temp));
					message_send_text(conn,message_type_info,conn,msgtemp);
					eventlog(eventlog_level_debug,__FUNCTION__,"[%d] account \"%s\" created",conn_get_socket(conn),username);
					conn_unget_chatname(conn,username);
				}
				else {
					char tmp[MAX_IRC_MESSAGE_LEN+1];
					
 					irc_send_cmd(conn,"NOTICE",":Invalid arguments for NICKSERV");
					sprintf(tmp,":Unrecognized command \"%s\"",text);
					irc_send_cmd(conn,"NOTICE",tmp);
 				}
 	        } 
			else if (conn_get_state(conn)==conn_state_loggedin) {
				if (e[i][0]=='#') {
					/* channel message */
					t_channel * channel;

					if ((channel = channellist_find_channel_by_name(irc_convert_ircname(e[i]),NULL,NULL))) {
						if ((strlen(text)>=9)&&(strncmp(text,"\001ACTION ",8)==0)&&(text[strlen(text)-1]=='\001')) { 
							/* at least "\001ACTION \001" */
							/* it's a CTCP ACTION message */
							text = text + 8;
							text[strlen(text)-1] = '\0';
							channel_message_send(channel,message_type_emote,conn,text);
						} 
						else {
                            if ((conn_get_wol(conn)==1) && (text[0] == '/')) {
                                /* "/" commands (like "/help..." */
                                handle_command(conn, text);
                            }
                            else {
                                channel_message_log(channel, conn, 1, text);
                                channel_message_send(channel,message_type_talk,conn,text);
                            }
						}
					}
					else {
						irc_send(conn,ERR_NOSUCHCHANNEL,":No such channel");
					}				
	    	    } 
				else {
					/* whisper */
					t_connection * user;

					if ((user = connlist_find_connection_by_accountname(e[i]))) 
					{
						message_send_text(user,message_type_whisper,conn,text);
					}
					else 
					{
						irc_send(conn,ERR_NOSUCHNICK,":No such user");
					}
	    	    }
	        }
	    }
	    if (e)
	         irc_unget_listelems(e);
	} 
	else
	    irc_send(conn,ERR_NEEDMOREPARAMS,":Too few arguments to PRIVMSG");
	return 0;
}

static int _handle_notice_command(t_connection * conn, int numparams, char ** params, char * text)
{
	if ((numparams>=1)&&(text)) {
	    int i;
	    char ** e;
	
	    e = irc_get_listelems(params[0]);
	    /* FIXME: support wildcards! */
		
	    for (i=0;((e)&&(e[i]));i++) {
			if (conn_get_state(conn)==conn_state_loggedin) {
				t_connection * user;

				if ((user = connlist_find_connection_by_accountname(e[i]))) {
				        message_send_text(user,message_type_notice,conn,text);
				}
				else {
					irc_send(conn,ERR_NOSUCHNICK,":No such user");
				}
	        	}
	    	}
	    	if (e)
	        irc_unget_listelems(e);
	} 
	else
	    irc_send(conn,ERR_NEEDMOREPARAMS,":Too few arguments to PRIVMSG");
	return 0;
}

static int _handle_who_command(t_connection * conn, int numparams, char ** params, char * text)
{
	if (numparams>=1) {
	    int i;
	    char ** e;

	    e = irc_get_listelems(params[0]);
	    for (i=0; ((e)&&(e[i]));i++) {
	    	irc_who(conn,e[i]);
	    }
	    irc_send(conn,RPL_ENDOFWHO,":End of WHO list"); /* RFC2812 only requires this to be sent if a list of names was given. Undernet seems to always send it, so do we :) */
        if (e)
			irc_unget_listelems(e);
	} 
	else 
	    irc_send(conn,ERR_NEEDMOREPARAMS,":Too few arguments to WHO");
	return 0;
}

static int _handle_list_command(t_connection * conn, int numparams, char ** params, char * text)
{
    char temp[MAX_IRC_MESSAGE_LEN];

	irc_send(conn,RPL_LISTSTART,"Channel :Users  Name"); /* backward compatibility */
	if((conn_get_wol(conn) == 1)) {
 	    t_elem const * curr;
 	    
	    if((params) && (strcmp(params[0], "0") == 0)) {
			/* HACK: Currently, this is the best way to set the game type... */
			conn_wol_set_game_type(conn,atoi(params[1]));
			    
			eventlog(eventlog_level_debug,__FUNCTION__,"[** WOL **] LIST [Channel]");
   	    LIST_TRAVERSE_CONST(channellist(),curr) 
		{
    	    		t_channel const * channel = elem_get_data(curr);
	        	char const * tempname;

	        	tempname = irc_convert_channel(channel);

				if(strstr(tempname,"Lob") != NULL) {
					eventlog(eventlog_level_debug,__FUNCTION__,"[** WOL **] LIST [Channel: \"Lob\"] (%s)",tempname);
					if (strlen(tempname)+1+20+1+1<MAX_IRC_MESSAGE_LEN)
						sprintf(temp,"%s %u 0 388",tempname,channel_get_length(channel));
   					else
   						eventlog(eventlog_level_warn,__FUNCTION__,"LISTREPLY length exceeded");

					irc_send(conn,RPL_CHANNEL,temp);
				}
    		}
	    }
	    /**
        *  14 = Dune 2000, 18 = Tiberian Sun game channels, 21 = Red alert 1 channels, 
		*  33 = Red alert 2 channels, 41 = Yuri's Revenge
		*/
	    else if((params) && ((strcmp(params[0], "14") == 0) ||
				             (strcmp(params[0], "18") == 0) ||
				             (strcmp(params[0], "21") == 0) ||
				             (strcmp(params[0], "33") == 0) ||
				             (strcmp(params[0], "41") == 0))) {
    		eventlog(eventlog_level_debug,__FUNCTION__,"[** WOL **] LIST [Game]");
   			LIST_TRAVERSE_CONST(channellist(),curr) 
			{
    		    t_channel const * channel = elem_get_data(curr);
			    t_connection * m;
        	    char const * tempname;
				char * topic = channel_get_topic(channel_get_name(channel));

        	    tempname = irc_convert_channel(channel);
				if(strstr(tempname,"_game") != NULL) {
					m = channel_get_first(channel);
					if(channel_wol_get_game_type(channel) == conn_wol_get_game_type(conn)) {
						eventlog(eventlog_level_debug,__FUNCTION__,"[** WOL **] List [Channel: \"_game\"] (%s)",tempname);

						if (topic) {
	    						eventlog(eventlog_level_debug,__FUNCTION__,"[** WOL **] List [Channel: \"_game\"] %s %u 0 %u %u 0 %u 128::%s",tempname,
										 channel_get_length(channel),channel_wol_get_game_type(channel),channel_wol_get_game_tournament(channel),
										 channel_wol_get_game_ownerip(channel),topic);
								/**
								*  The layout of the game list entry is something like this:
                                *
								*   #game_channel_name users unknown gameType gameIsTournment unknown longIP 128::topic
								*/
	        		if (strlen(tempname)+1+20+1+1+strlen(topic)<MAX_IRC_MESSAGE_LEN)
	    							sprintf(temp,"%s %u 0 %u %u 0 %u 128::%s",tempname,
											channel_get_length(channel),channel_wol_get_game_type(channel),channel_wol_get_game_tournament(channel),
											channel_wol_get_game_ownerip(channel),topic);
	        		else
	            			eventlog(eventlog_level_warn,__FUNCTION__,"LISTREPLY length exceeded");
			}
						else {
        						if (strlen(tempname)+1+20+1+1<MAX_IRC_MESSAGE_LEN)
	    							sprintf(temp,"%s %u 0 %u %u 0 %u 128::",tempname,channel_get_length(channel),channel_wol_get_game_type(channel),
											channel_wol_get_game_tournament(channel),channel_wol_get_game_ownerip(channel));
			else
            							eventlog(eventlog_level_warn,__FUNCTION__,"LISTREPLY length exceeded");
						}
					}
					irc_send(conn,RPL_GAME_CHANNEL,temp);
				}
			}
		}	    

    	irc_send(conn,RPL_LISTEND,":End of LIST command");
    	return 0;
	}

	if (numparams==0) {
 	    t_elem const * curr;
 	    
   	    LIST_TRAVERSE_CONST(channellist(),curr) 
			{
    	    t_channel const * channel = elem_get_data(curr);
	        char const * tempname;
			char * topic = channel_get_topic(channel_get_name(channel));

	        tempname = irc_convert_channel(channel);

			/* FIXME: AARON: only list channels like in /channels command */
			if (topic) {
	        	if (strlen(tempname)+1+20+1+1+strlen(topic)<MAX_IRC_MESSAGE_LEN)
		    		sprintf(temp,"%s %u :%s",tempname,channel_get_length(channel),topic);
	        	else
	            	eventlog(eventlog_level_warn,__FUNCTION__,"LISTREPLY length exceeded");
			}
			else {
	        	if (strlen(tempname)+1+20+1+1<MAX_IRC_MESSAGE_LEN)
		    		sprintf(temp,"%s %u :",tempname,channel_get_length(channel));
	        		else
	            			eventlog(eventlog_level_warn,__FUNCTION__,"LISTREPLY length exceeded");
			}
	        	irc_send(conn,RPL_LIST,temp);
    	}
    }
	else if (numparams>=1) {
        int i;
        char ** e;
 
	e = irc_get_listelems(params[0]);
		/* FIXME: support wildcards! */

		for (i=0;((e)&&(e[i]));i++) {
		t_channel const * channel;
		char const * verytemp; /* another good example for creative naming conventions :) */
	       	char const * tempname;
		char * topic;
		
		verytemp = irc_convert_ircname(e[i]);
		if (!verytemp)
			continue; /* something is wrong with the name ... */
		channel = channellist_find_channel_by_name(verytemp,NULL,NULL);
		if (!channel)
			continue; /* channel doesn't exist */
				
		topic = channel_get_topic(channel_get_name(channel));
	       	tempname = irc_convert_channel(channel);
		
			if (topic) {
	       		if (strlen(tempname)+1+20+1+1+strlen(topic)<MAX_IRC_MESSAGE_LEN)
	    			sprintf(temp,"%s %u :%s",tempname,channel_get_length(channel),topic);
	       		else
	       			eventlog(eventlog_level_warn,__FUNCTION__,"LISTREPLY length exceeded");
		}
			else {
	       		if (strlen(tempname)+1+20+1+1<MAX_IRC_MESSAGE_LEN)
	    			sprintf(temp,"%s %u :",tempname,channel_get_length(channel));
	       		else
	       			eventlog(eventlog_level_warn,__FUNCTION__,"LISTREPLY length exceeded");
		}
	       	irc_send(conn,RPL_LIST,temp);
	}
        if (e)
		irc_unget_listelems(e);
    }
    irc_send(conn,RPL_LISTEND,":End of LIST command");
	return 0;
}

static int _handle_topic_command(t_connection * conn, int numparams, char ** params, char * text)
{
	char ** e = NULL;
	
	if((conn_get_wol(conn) == 1)) {
	    t_channel * channel = conn_get_channel(conn);
	    channel_set_topic(channel_get_name(channel),text,NO_SAVE_TOPIC);
	}

	if (params!=NULL) e = irc_get_listelems(params[0]);

	if ((e)&&(e[0])) {
		t_channel *channel = conn_get_channel(conn);
		
		if (channel) {	
			char * topic;
			char temp[MAX_IRC_MESSAGE_LEN];
			char const * ircname = irc_convert_ircname(e[0]);

			if ((ircname) && (strcasecmp(channel_get_name(channel),ircname)==0)) {
				if ((topic = channel_get_topic(channel_get_name(channel)))) { 
			  		sprintf(temp,"%s :%s",ircname,topic);
			    		irc_send(conn,RPL_TOPIC,temp);
				}
				else
			    		irc_send(conn,RPL_NOTOPIC,":No topic is set");
			}
			else
				irc_send(conn,ERR_NOTONCHANNEL,":You are not on that channel");
		}
		else {
			irc_send(conn,ERR_NOTONCHANNEL,":You're not on a channel");
		}
		irc_unget_listelems(e);
	}
	else
		irc_send(conn,ERR_NEEDMOREPARAMS,":too few arguments to TOPIC");
	return 0;
}

static int _handle_join_command(t_connection * conn, int numparams, char ** params, char * text)
{
	if (numparams>=1) {
	    char ** e;

	    e = irc_get_listelems(params[0]);
	    if ((e)&&(e[0])) {
            char temp[MAX_IRC_MESSAGE_LEN];
	    	char const * ircname = irc_convert_ircname(e[0]);
	    	char * old_channel_name = NULL;
	   	 	t_channel * old_channel = conn_get_channel(conn);
			t_channel * channel;
	   	    t_account * acc = conn_get_account(conn);

			if (old_channel)
			  old_channel_name = xstrdup(irc_convert_channel(old_channel));

            if ((ircname) && (channel = channellist_find_channel_by_name(ircname,NULL,NULL))) {
                if (channel_check_banning(channel,conn)) {
                    snprintf(temp, sizeof(temp), "%s :You are banned from that channel.",e[0]);
                    irc_send(conn,ERR_BANNEDFROMCHAN, temp);
                    if (e)
                        irc_unget_listelems(e);
                    return 0;
                }

                if ((account_get_auth_admin(acc,NULL)!=1) && (account_get_auth_admin(acc,ircname)!=1) &&
                    (account_get_auth_operator(acc,NULL)!=1) && (account_get_auth_operator(acc,ircname)!=1) &&
                    (channel_get_max(channel) != -1) && (channel_get_curr(channel)>=channel_get_max(channel))) {

                    snprintf(temp, sizeof(temp), "%s :The channel is currently full.",e[0]);
                    irc_send(conn,ERR_CHANNELISFULL, temp);
                    if (e)
                        irc_unget_listelems(e);
                    return 0;
                }
            }
			
			if ((!(ircname)) || (conn_set_channel(conn,ircname)<0)) {
				irc_send(conn,ERR_NOSUCHCHANNEL,":JOIN failed"); /* FIXME: be more precise; what is the real error code for that? */
			} 
			else {
    			char temp[MAX_IRC_MESSAGE_LEN];
				channel = conn_get_channel(conn);

			    if ((conn_get_wol(conn) == 1)) {
					channel_set_userflags(conn);
					message_send_text(conn,message_type_join,conn,NULL); /* we have to send the JOIN acknowledgement */
					ircname=irc_convert_channel(channel);

	    				irc_send_rpl_namreply(conn,channel);

					if ((strlen(ircname)+1+strlen(":End of NAMES list")+1)<MAX_IRC_MESSAGE_LEN) {
						sprintf(temp,"%s :End of NAMES list",ircname);
						irc_send(conn,RPL_ENDOFNAMES,temp);
					}
			    }
			    else {			    
					if (channel!=old_channel) {
					char * topic;

					channel_set_userflags(conn);
					message_send_text(conn,message_type_join,conn,NULL); /* we have to send the JOIN acknowledgement */
					ircname=irc_convert_channel(channel);

						if ((topic = channel_get_topic(channel_get_name(channel)))) {
							if ((strlen(ircname)+1+1+strlen(topic)+1)<MAX_IRC_MESSAGE_LEN) {
							sprintf(temp,"%s :%s",ircname,topic);
							irc_send(conn,RPL_TOPIC,temp);
						}

							if ((strlen(ircname)+1+strlen("FIXME 0")+1)<MAX_IRC_MESSAGE_LEN) {
							sprintf(temp,"%s FIXME 0",ircname);
							irc_send(conn,RPL_TOPICWHOTIME,temp); /* FIXME: this in an undernet extension but other servers support it too */
						}
					}
					else
						irc_send(conn,RPL_NOTOPIC,":No topic is set");

					irc_send_rpl_namreply(conn,channel);
					irc_send(conn,RPL_ENDOFNAMES,":End of NAMES list");

						if (old_channel_name) {
						irc_send_cmd2(conn,conn_get_loggeduser(conn),"PART",old_channel_name,"only one channel at once");
					}
		    		}
			}
			}
			if (old_channel_name) xfree((void *)old_channel_name);
		}
    		if (e)
			irc_unget_listelems(e);
	} 
	else 
	    irc_send(conn,ERR_NEEDMOREPARAMS,":Too few arguments to JOIN");
	return 0;
}

static int _handle_names_command(t_connection * conn, int numparams, char ** params, char * text)
{
	t_channel * channel;
      
    if (numparams>=1) {
		char ** e;
		char const * ircname;
		char const * verytemp;
		char temp[MAX_IRC_MESSAGE_LEN];
		int i;

		e = irc_get_listelems(params[0]);
		for (i=0;((e)&&(e[i]));i++) {			
			verytemp = irc_convert_ircname(e[i]);
			
			if (!verytemp)
				continue; /* something is wrong with the name ... */
			channel = channellist_find_channel_by_name(verytemp,NULL,NULL);
			if (!channel)
				continue; /* channel doesn't exist */
			irc_send_rpl_namreply(conn,channel);
			ircname=irc_convert_channel(channel);
			if ((strlen(ircname)+1+strlen(":End of NAMES list")+1)<MAX_IRC_MESSAGE_LEN) {
				sprintf(temp,"%s :End of NAMES list",ircname);
				irc_send(conn,RPL_ENDOFNAMES,temp);
			} 
			else 
				irc_send(conn,RPL_ENDOFNAMES,":End of NAMES list");
		}
		if (e)
		irc_unget_listelems(e);
    } 
	else if (numparams==0) {
		t_elem const * curr;
		LIST_TRAVERSE_CONST(channellist(),curr) 
		{
			channel = elem_get_data(curr);
			irc_send_rpl_namreply(conn,channel);
		}
		irc_send(conn,RPL_ENDOFNAMES,"* :End of NAMES list");		
    }
	return 0;
}

static int irc_send_banlist(t_connection * conn, t_channel * channel)
{
    t_elem const * curr;
    char const *   banned;
    char const * ircname = server_get_hostname();
    char temp[MAX_IRC_MESSAGE_LEN];

    if (!conn) {
        ERROR0("got NULL conn");
        return -1;
    }

    if (!channel) {
        ERROR0("got NULL channel");
        return -1;
    }

    LIST_TRAVERSE_CONST(channel_get_banlist(channel),curr) {
        banned = (char*)elem_get_data(curr);

        //FIXME: right now we lie about who have gives ban and also about bantime
        snprintf(temp,sizeof(temp),"%s %s!*@* %s 1208297879", irc_convert_channel(channel), banned, ircname);
        irc_send(conn,RPL_BANLIST,temp);
    }
    return 0;
}

static int _handle_mode_command(t_connection * conn, int numparams, char ** params, char * text)
{
    char temp[MAX_IRC_MESSAGE_LEN];
    t_account * acc = conn_get_account(conn);

   	memset(temp,0,sizeof(temp));

    if (numparams < 1) {
        irc_send(conn,ERR_NEEDMOREPARAMS,"MODE :Not enough parameters");
        return 0;
    }

    if (params[0][0]=='#') {
        /* Channel mode */
        t_channel * channel;
        char const * ircname = irc_convert_ircname(params[0]);

        if (!(channel = channellist_find_channel_by_name(ircname,NULL,NULL))) {
            snprintf(temp,sizeof(temp),"%s :No such channel", params[0]);
            irc_send(conn,ERR_NOSUCHCHANNEL,temp);
     	    return 0;
	    }

        if (numparams == 1) {
            /* FIXME: Send real CHANELMODE flags! */
            snprintf(temp,sizeof(temp),"%s +tns", params[0]);
            irc_send(conn,RPL_CHANNELMODEIS,temp);
            return 0;
        }

        if (numparams == 2) {
            if (strcmp(params[1], "b") == 0) {
                irc_send_banlist(conn, channel);
                snprintf(temp,sizeof(temp),"%s :End of channel ban list", params[0]);
                irc_send(conn,RPL_ENDOFBANLIST,temp);
                return 0;
            }
            else {
                snprintf(temp,sizeof(temp),":%s is unknown mode char to me for %s", params[1], params[0]);
                irc_send(conn,ERR_UNKNOWNMODE,temp);
                return 0;
            }
        }

        /* PELISH: Also tmpOP have setting modes alowed because all new channels have only tmpOP */
        if ((channel_conn_is_tmpOP(channel,conn)!=1) && 
            (account_get_auth_admin(acc,NULL)!=1) && (account_get_auth_admin(acc,ircname)!=1) &&
            (account_get_auth_operator(acc,NULL)!=1) && (account_get_auth_operator(acc,ircname)!=1)) {
            snprintf(temp,sizeof(temp),"%s :You're not channel operator", params[0]);
            irc_send(conn,ERR_CHANOPRIVSNEEDED,temp);
            return 0;
        }

        if (strcmp(params[1], "+b") == 0) {
            channel_ban_user (channel, params[2]);
            snprintf(temp,sizeof(temp),"%s %s %s!*@*", params[0], params[1], params[2]);
   	        channel_message_send(channel,message_type_mode,conn,temp);                
        }
        else if (strcmp(params[1], "-b") == 0) {
            channel_unban_user(channel, params[2]);
            snprintf(temp,sizeof(temp),"%s %s %s!*@*", params[0], params[1], params[2]);
   	        channel_message_send(channel,message_type_mode,conn,temp);                     
        }
        else if (strcmp(params[1], "+o") == 0) {
            snprintf(temp, sizeof(temp), "/op %s", params[2]);
            handle_command(conn, temp);
            snprintf(temp,sizeof(temp),"%s %s %s", params[0], params[1], params[2]);
  	        channel_message_send(channel,message_type_mode,conn,temp);                
        }
        else if (strcmp(params[1], "-o") == 0) {
            snprintf(temp, sizeof(temp), "/deop %s", params[2]);
            handle_command(conn, temp);
            snprintf(temp,sizeof(temp),"%s %s %s", params[0], params[1], params[2]);
   	        channel_message_send(channel,message_type_mode,conn,temp);                     
        }
        else if (strcmp(params[1], "+v") == 0) {
            snprintf(temp, sizeof(temp), "/voice %s", params[2]);
            handle_command(conn, temp);
            snprintf(temp,sizeof(temp),"%s %s %s", params[0], params[1], params[2]);
  	        channel_message_send(channel,message_type_mode,conn,temp);                
        }
        else if (strcmp(params[1], "-v") == 0) {
            snprintf(temp, sizeof(temp), "/devoice %s", params[2]);
            handle_command(conn, temp);

            snprintf(temp,sizeof(temp),"%s %s %s", params[0], params[1], params[2]);
       	    channel_message_send(channel,message_type_mode,conn,temp);                     
        }
        else if (strcmp(params[1], "+i") == 0) {
            /* FIXME: channel will be only for invited */
        }
        else if (strcmp(params[1], "+l") == 0) {
            channel_set_max(channel, atoi(params[2]));
            snprintf(temp,sizeof(temp),"%s %s %s", params[0], params[1], params[2]);
            channel_message_send(channel,message_type_mode,conn,temp);
        }
        else if (strcmp(params[1], "-l") == 0) {
            channel_set_max(channel, -1);
            snprintf(temp,sizeof(temp),"%s %s", params[0], params[1]);
   	        channel_message_send(channel,message_type_mode,conn,temp);
        }
       	else {
            snprintf(temp,sizeof(temp),":%s is unknown mode char to me for %s", params[1], params[0]);
            irc_send(conn,ERR_UNKNOWNMODE,temp);
        }
    }
    else {
        /* User modes */
        /* FIXME: Support user modes (away, invisible...) */
     	irc_send(conn,ERR_UMODEUNKNOWNFLAG,":Unknown MODE flag");
    }
    return 0;
}

static int _handle_userhost_command(t_connection * conn, int numparams, char ** params, char * text)
{
	/* FIXME: Send RPL_USERHOST */
	return 0;
}

static int _handle_quit_command(t_connection * conn, int numparams, char ** params, char * text)
{
	if (conn_get_channel(conn))
	    conn_set_channel(conn, NULL);
	conn_set_state(conn, conn_state_destroy);
	return 0;
}

static int _handle_ison_command(t_connection * conn, int numparams, char ** params, char * text)
{
	char temp[MAX_IRC_MESSAGE_LEN];
	char first = 1;
	
	if (numparams>=1) {
	    int i;

	    temp[0]='\0';
	    for (i=0; (i<numparams && (params) && (params[i]));i++) {
    	  if (connlist_find_connection_by_accountname(params[i])) {
		    if (first)
		        strcat(temp,":");
		    else 
		        strcat(temp," ");
		    strcat(temp,params[i]);
		    first = 0;
		  }
	    }
	    irc_send(conn,RPL_ISON,temp);
	} 
	else 
	    irc_send(conn,ERR_NEEDMOREPARAMS,":Too few arguments to ISON");
	return 0;
}

static int _handle_whois_command(t_connection * conn, int numparams, char ** params, char * text)
{
	char temp[MAX_IRC_MESSAGE_LEN];
	char temp2[MAX_IRC_MESSAGE_LEN];
	if (numparams>=1) {
	    int i;
	    char ** e;
	    t_connection * c;
	    t_channel * chan;

	    temp[0]='\0';
	    temp2[0]='\0';
	    e = irc_get_listelems(params[0]);
	    for (i=0; ((e)&&(e[i]));i++) {
    	  if ((c = connlist_find_connection_by_accountname(e[i]))) {
		    if (prefs_get_hide_addr() && !(account_get_command_groups(conn_get_account(conn)) & command_get_group("/admin-addr")))
		      sprintf(temp,"%s %s hidden * :%s",e[i],clienttag_uint_to_str(conn_get_clienttag(c)),"PvPGN user");
		    else
		      sprintf(temp,"%s %s %s * :%s",e[i],clienttag_uint_to_str(conn_get_clienttag(c)),addr_num_to_ip_str(conn_get_addr(c)),"PvPGN user");
		    irc_send(conn,RPL_WHOISUSER,temp);
		    
		    if ((chan=conn_get_channel(conn))) {
			char flg;
			unsigned int flags;
			
			flags = conn_get_flags(c);
			
	            	if (flags & MF_BLIZZARD)
		            flg='@';
	            	else if ((flags & MF_BNET) || (flags & MF_GAVEL))
		            flg='%'; 
	            	else if (flags & MF_VOICE)
		            flg='+';
		        else flg = ' ';
			sprintf(temp2,"%s :%c%s",e[i],flg,irc_convert_channel(chan));
			irc_send(conn,RPL_WHOISCHANNELS,temp2);
		    }
		    
		  }
		  else
		    irc_send(conn,ERR_NOSUCHNICK,":No such nick/channel");
		  
	    }
	    irc_send(conn,RPL_ENDOFWHOIS,":End of /WHOIS list");
        if (e)
			irc_unget_listelems(e);
	} 
	else 
	    irc_send(conn,ERR_NEEDMOREPARAMS,":Too few arguments to WHOIS");
	return 0;
}

static int _handle_part_command(t_connection * conn, int numparams, char ** params, char * text)
{
    if ((conn_get_wol(conn) == 1) && (conn_wol_get_ingame(conn) == 1)) 
        conn_wol_set_ingame(conn,0);

    conn_set_channel(conn, NULL);
    message_send_text(conn,message_type_part,conn,NULL);
    return 0;
}    

/**
*  Westwood Online Extensions
*/
static int _handle_cvers_command(t_connection * conn, int numparams, char ** params, char * text)
{
	// Ignore command
	return 0;
}

static int _handle_verchk_command(t_connection * conn, int numparams, char ** params, char * text)
{
    char temp[MAX_IRC_MESSAGE_LEN];

    if (numparams == 2) {
        snprintf(temp, sizeof(temp), ":none none none 1 %s NONREQ", params[0]);
        eventlog(eventlog_level_debug,__FUNCTION__,"[** WOL **] VERCHK %s",temp);
        irc_send(conn,RPL_VERCHK_NONREQ,temp);
    }
    else
        irc_send(conn,ERR_NEEDMOREPARAMS,"VERCHK :Not enough parameters");

    return 0; 
}

static int _handle_lobcount_command(t_connection * conn, int numparams, char ** params, char * text)
{
    // Ignore command but, return 1	
	irc_send(conn,RPL_LOBCOUNT,"1");
	return 0;
}

static int _handle_whereto_command(t_connection * conn, int numparams, char ** params, char * text)
{
	// Ignore command, but output proper server information...
	char temp[MAX_IRC_MESSAGE_LEN];

	// Casted to avoid warnings
	const char * ircip;
	const char * ircname = prefs_get_servername();
	const char * irctimezone = prefs_get_wol_timezone();
	const char * irclong = prefs_get_wol_longitude();
	const char * irclat = prefs_get_wol_latitude();
	
    {			/* trans support */
       unsigned short port = conn_get_real_local_port(conn);
       unsigned int addr = conn_get_real_local_addr(conn);

       trans_net(conn_get_addr(conn), &addr, &port);

       ircip = addr_num_to_ip_str(addr);
    }

	sprintf(temp,":%s %d '0:%s' %s %s %s",ircip,BNETD_WOL_PORT,ircname,irctimezone,irclong,irclat);
	irc_send(conn,RPL_IRCSERV,temp);
	sprintf(temp,":%s %d 'Live chat server' %s %s %s",ircip,BNETD_IRC_PORT,irctimezone,irclong,irclat);
	irc_send(conn,RPL_IRCSERV,temp);

	sprintf(temp,":%s %d 'Gameres server' %s %s %s",ircip,BNETD_WOL_PORT,irctimezone,irclong,irclat);
	irc_send(conn,RPL_GAMERESSERV,temp);
	sprintf(temp,":%s %d 'Ladder server' %s %s %s",ircip,BNETD_WOL_PORT,irctimezone,irclong,irclat);
	irc_send(conn,RPL_LADDERSERV,temp);

	irc_send(conn,RPL_ENDSERVLIST,"");
	return 0;
}

static int _handle_apgar_command(t_connection * conn, int numparams, char ** params, char * text)
{
	char * apgar = NULL;
	
	if((numparams>=1)&&params[0]) {
	    apgar = params[0];
	    conn_wol_set_apgar(conn,apgar);
	}
    else
        irc_send(conn,ERR_NEEDMOREPARAMS,"APGAR :Not enough parameters");

	return 0;
}

static int _handle_serial_command(t_connection * conn, int numparams, char ** params, char * text)
{
    // Ignore command
	return 0;
}

static int _handle_squadinfo_command(t_connection * conn, int numparams, char ** params, char * text)
{
    if ((numparams>=1)&&(params[0]))
	    irc_send(conn,ERR_IDNOEXIST,":ID does not exist");
    else
        irc_send(conn,ERR_NEEDMOREPARAMS,"SQUADINFO :Not enough parameters");

    return 0;
}	    

static int _handle_setopt_command(t_connection * conn, int numparams, char ** params, char * text)
{
	// Ignore this command

	return 0;
}	    

static int _handle_setcodepage_command(t_connection * conn, int numparams, char ** params, char * text)
{
	char * codepage = NULL;
	
	if((numparams>=1)&&params[0]) {
	    codepage = params[0];
	    conn_wol_set_codepage(conn,atoi(codepage));
    	irc_send(conn,RPL_SET_CODEPAGE,codepage);
	}
	else
	    irc_send(conn,ERR_NEEDMOREPARAMS,"SETCODEPAGE :Not enough parameters");
	return 0;
}

static int _handle_setlocale_command(t_connection * conn, int numparams, char ** params, char * text)
{
	char * locale = NULL;
	
	if((numparams>=1)&&(params[0])) {
	    locale = params[0];
	    conn_wol_set_locale(conn,atoi(locale));
        irc_send(conn,RPL_SET_LOCALE,locale);
	}
	else
	    irc_send(conn,ERR_NEEDMOREPARAMS,"SETLOCALE :Not enough parameters");
	return 0;
}	    

static int _handle_getcodepage_command(t_connection * conn, int numparams, char ** params, char * text)
{
	char temp[MAX_IRC_MESSAGE_LEN];
	char _temp[MAX_IRC_MESSAGE_LEN];
	
	memset(temp,0,sizeof(temp));
	memset(_temp,0,sizeof(_temp));

	if((numparams>=1)) {
	    int i;
	    for (i=0; i<numparams; i++) {
    		t_connection * user;
            int codepage;
    		char const * name;
    		
    		if((user = connlist_find_connection_by_accountname(params[i]))) {
    		    codepage = conn_wol_get_codepage(user);
    		    name = conn_get_chatname(user);
    
    		    sprintf(_temp,"%s`%u",name,codepage);
    		    strcat(temp,_temp);
    		    if(i < numparams-1)
    			     strcat(temp,"`");
    		}
	    }
   	    irc_send(conn,RPL_GET_CODEPAGE,temp);	
	}
	else
	    irc_send(conn,ERR_NEEDMOREPARAMS,"GETCODEPAGE :Not enough parameters");
	return 0;
}

static int _handle_getlocale_command(t_connection * conn, int numparams, char ** params, char * text)
{
	char temp[MAX_IRC_MESSAGE_LEN];
	char _temp[MAX_IRC_MESSAGE_LEN];
	
	memset(temp,0,sizeof(temp));
	memset(_temp,0,sizeof(_temp));

	if((numparams>=1)) {
	    int i;
	    for (i=0; i<numparams; i++) {
    		t_connection * user;
    		int locale;
    		char const * name;
    		
    		if((user = connlist_find_connection_by_accountname(params[i]))) {
    		    locale = conn_wol_get_locale(user);
    		    name = conn_get_chatname(user);
    
    		    sprintf(_temp,"%s`%u",name,locale);
    		    strcat(temp,_temp);
    		    if(i < numparams-1)
           			strcat(temp,"`");
    		}
	    }
   	    irc_send(conn,RPL_GET_LOCALE,temp);	
	}
	else
	    irc_send(conn,ERR_NEEDMOREPARAMS,"GETLOCALE :Not enough parameters");
	return 0;
}	    

static int _handle_joingame_command(t_connection * conn, int numparams, char ** params, char * text)
{
	char _temp[MAX_IRC_MESSAGE_LEN];
	
	memset(_temp,0,sizeof(_temp));

	/**
	*  Basically this has 2 modes, Join Game and Create Game output is pretty much
	*  the same...input and output of JOINGAME is listed below. By the way, there is a
	*  hack in here, for Red Alert 1, it use's JOINGAME for some reason to join a lobby channel.
	*   
	*   Here is the input expected:
	*   JOINGAME #user's_game unknown numberOfPlayers gameType unknown unknown gameIsTournament unknown password
	*
	*   Heres the output expected:
	*   user!WWOL@hostname JOINGAME unknown numberOfPlayers gameType unknown clanID longIP gameIsTournament :#game_channel_name
	*/

	if((numparams==2)) {
	    char ** e;

	    eventlog(eventlog_level_debug,__FUNCTION__,"[** WOL **] JOINGAME: * Join * (%s, %s)",
		     params[0],params[1]);

	    e = irc_get_listelems(params[0]);
	    if ((e)&&(e[0])) {
    		char const * ircname = irc_convert_ircname(e[0]);
    		char * old_channel_name = NULL;
			t_channel * channel;
	   	 	t_channel * old_channel = conn_get_channel(conn);
	   	 	
            channel = channellist_find_channel_by_name(ircname,NULL,NULL);

            if (channel == NULL) {
	   	 	     snprintf(_temp, sizeof(_temp), "%s :Game channel has closed",e[0]);
	             irc_send(conn,ERR_GAMECHANCLOSED,_temp);
			     if (e)
		            irc_unget_listelems(e);
     	         return 0;
            }

            if (channel_get_length(channel) == channel_get_max(channel)) {
	   	 	     snprintf(_temp, sizeof(_temp), "%s :Channel is full",e[0]);
                 irc_send(conn,ERR_CHANNELISFULL,_temp);
			     if (e)
		            irc_unget_listelems(e);
			     return 0;
            }

			if (old_channel)
   	  		   old_channel_name = xstrdup(irc_convert_channel(old_channel));
			
			if ((!(ircname)) || (conn_set_channel(conn,ircname)<0))	{
				irc_send(conn,ERR_NOSUCHCHANNEL,":JOINGAME failed");
			} 
			else {
				t_channel * channel;
				char const * gameOptions;
				int gameType;

				conn_wol_set_ingame(conn,1);

				channel = conn_get_channel(conn);
				gameOptions = channel_wol_get_game_options(channel);
				gameType = channel_wol_get_game_type(channel);
				

				if (gameType == conn_wol_get_game_type(conn)) {
				    eventlog(eventlog_level_debug,__FUNCTION__,"[** WOL **] JOINGAME [Game Options] (%s) [Game Type] (%u) [Game Owner] (%s)",gameOptions,gameType,channel_wol_get_game_owner(channel));
				
				    if (channel!=old_channel) {
		    			char temp[MAX_IRC_MESSAGE_LEN];
    
    					channel_set_userflags(conn);
    					message_send_text(conn,message_wol_joingame,conn,gameOptions); /* we have to send the JOINGAME acknowledgement */
    					ircname=irc_convert_channel(channel);
    		    			
    					irc_send_rpl_namreply(conn,channel);

   	    				if ((strlen(ircname)+1+strlen(":End of NAMES list")+1)<MAX_IRC_MESSAGE_LEN) {
    						sprintf(temp,"%s :End of NAMES list",ircname);
    						irc_send(conn,RPL_ENDOFNAMES,temp);
    					}
	    		    }
				}
				else {
				    irc_send(conn,ERR_NOSUCHCHANNEL,":JOINGAME failed");
				}
			}
			if (old_channel_name) xfree((void *)old_channel_name);
		}
    		if (e)
		    irc_unget_listelems(e);
    	    return 0;
	}
	/**
	* HACK: Check for 3 params, because in that case we must be running RA1
	* then just forward to _handle_join_command
	*/
	else if((numparams==3)) {
	    _handle_join_command(conn,numparams,params,text);
	}
	else if((numparams>=8)) {
	    char ** e;

	    eventlog(eventlog_level_debug,__FUNCTION__,"[** WOL **] JOINGAME: * Create * (%s, %s)",
		     params[0],params[1]);
		     
	    sprintf(_temp,"%s %s %s %s 0 %u %s :%s",params[1],params[2],params[3],params[4],conn_get_addr(conn),params[6],params[0]);
	    eventlog(eventlog_level_debug,__FUNCTION__,"[** WOL **] JOINGAME [Game Options] (%s)",_temp);

	    e = irc_get_listelems(params[0]);
	    if ((e)&&(e[0])) {
    		char const * ircname = irc_convert_ircname(e[0]);
    		char * old_channel_name = NULL;
	   	 	t_channel * old_channel = conn_get_channel(conn);

			if (old_channel)
			  old_channel_name = xstrdup(irc_convert_channel(old_channel));
			
			if ((!(ircname)) || (conn_set_channel(conn,ircname)<0))	{
				irc_send(conn,ERR_NOSUCHCHANNEL,":JOINGAME failed"); /* FIXME: be more precise; what is the real error code for that? */
			} 
			else {
				t_channel * channel;

				channel = conn_get_channel(conn);
				if (channel!=old_channel) {
	    			char temp[MAX_IRC_MESSAGE_LEN];
					char * topic;

					channel_set_userflags(conn);
					channel_wol_set_game_options(channel,_temp);
					channel_wol_set_game_owner(channel,conn_get_chatname(conn));
					channel_wol_set_game_ownerip(channel,conn_get_addr(conn));
					channel_wol_set_game_type(channel,conn_wol_get_game_type(conn));
					channel_set_max(channel,atoi(params[2]));
					channel_wol_set_game_tournament(channel,atoi(params[6]));
					
					message_send_text(conn,message_wol_joingame,conn,_temp); /* we have to send the JOINGAME acknowledgement */
					ircname=irc_convert_channel(channel);
					
					if ((topic = channel_get_topic(channel_get_name(channel)))) {
						if ((strlen(ircname)+1+1+strlen(topic)+1)<MAX_IRC_MESSAGE_LEN) {
							sprintf(temp,"%s :%s",ircname,topic);
							irc_send(conn,RPL_TOPIC,temp);
						}
					}

	    			irc_send_rpl_namreply(conn,channel);
					
    				if ((strlen(ircname)+1+strlen(":End of NAMES list")+1)<MAX_IRC_MESSAGE_LEN) {
						sprintf(temp,"%s :End of NAMES list",ircname);
						irc_send(conn,RPL_ENDOFNAMES,temp);
					}
	    		}
			}
			if (old_channel_name) xfree((void *)old_channel_name);
		}
		if (e)
	       irc_unget_listelems(e);
	} 
	else
	    irc_send(conn,ERR_NEEDMOREPARAMS,"JOINGAME :Not enough parameters");
	return 0;
}	    

static int _handle_gameopt_command(t_connection * conn, int numparams, char ** params, char * text)
{
	char temp[MAX_IRC_MESSAGE_LEN];

 	/**
 	*  Basically this has 2 modes, Game Owner Change and Game Joinee Change what the output
 	*  on this does is pretty much unknown, we just dump this to the client to deal with...
 	*   
 	*	Heres the output expected (from game owner):
 	*	user!WWOL@hostname GAMEOPT #game_channel_name :gameOptions
 	*   
 	*	Heres the output expected (from game joinee):
 	*	user!WWOL@hostname GAMEOPT game_owner_name :gameOptions
 	*/
	if ((numparams>=1)&&(text)) {
	    int i;
	    char ** e;
	
	    e = irc_get_listelems(params[0]);
	    /* FIXME: support wildcards! */
		
	    eventlog(eventlog_level_debug,__FUNCTION__,"[** WOL **] GAMEOPT: (%s :%s)",params[0],text);
	    conn_wol_set_game_options(conn,text);
	    
	    for (i=0;((e)&&(e[i]));i++) {
    		if (e[i][0]=='#') {
    		    /* game owner change */
    		    t_channel * channel;
    
    		    if ((channel = channellist_find_channel_by_name(irc_convert_ircname(params[0]),NULL,NULL))) {
        			sprintf(temp,":%s",text);
        			channel_message_send(channel,message_wol_gameopt_owner,conn,temp);
    		    }
    		    else {
        			irc_send(conn,ERR_NOSUCHCHANNEL,":No such channel");
    		    }
    		} 
    		else
    		{
    		    /* user change */
    		    t_connection * user;
    
    		    if ((user = connlist_find_connection_by_accountname(e[i]))) {
        			sprintf(temp,":%s",text);
        			message_send_text(user,message_wol_gameopt_join,conn,temp);
    		    }
    		    else {
          			irc_send(conn,ERR_NOSUCHNICK,":No such user");
    		    }
    		}
	    }
	}
	else
	    irc_send(conn,ERR_NEEDMOREPARAMS,"GAMEOPT :Not enough parameters");
	return 0;
}	    

static int _handle_finduserex_command(t_connection * conn, int numparams, char ** params, char * text)
{
	char _temp[MAX_IRC_MESSAGE_LEN];
	char const * ircname = NULL;
	
	memset(_temp,0,sizeof(_temp));

	if ((numparams>=1)) {
	    t_connection * user;
	    
	    if((user = connlist_find_connection_by_accountname(params[0]))) {
     		ircname = irc_convert_channel(conn_get_channel(user));
	    }
	    
	    sprintf(_temp,"0 :%s,0",ircname);
	    irc_send(conn,RPL_FIND_USER_EX,_temp);
    }
	else
	    irc_send(conn,ERR_NEEDMOREPARAMS,"FINDUSEREX :Not enough parameters");
	return 0;
}

static int _handle_page_command(t_connection * conn, int numparams, char ** params, char * text)
{
	char _temp[MAX_IRC_MESSAGE_LEN];
	
	memset(_temp,0,sizeof(_temp));

	if ((numparams>=1)&&(text)) {
	    t_connection * user;
	    
	    sprintf(_temp,":%s",text);
	    if((user = connlist_find_connection_by_accountname(params[0]))) {
     		message_send_text(user,message_wol_page,conn,_temp);
	    }
    }
	else
	    irc_send(conn,ERR_NEEDMOREPARAMS,"PAGE :Not enough parameters");
	return 0;
}	   

static int _handle_startg_command(t_connection * conn, int numparams, char ** params, char * text)
{
	char temp[MAX_IRC_MESSAGE_LEN];
	char _temp_a[MAX_IRC_MESSAGE_LEN];
	t_channel * channel;

	time_t now;

 	/**
 	*  Heres the output expected (this can have up-to 8 entries (ie 8 players): 
    *  (we are assuming for this example that user1 is the game owner)
    *
 	*   user1!WWOL@hostname STARTG u :user1 xxx.xxx.xxx.xxx user2 xxx.xxx.xxx.xxx :gameNumber t_Time
 	*/
	if((numparams>=1)) {
	    int i;
	    char ** e;
	
	    memset(temp,0,sizeof(temp));
	    memset(_temp_a,0,sizeof(_temp_a));	
	
	    e = irc_get_listelems(params[1]);
	    /* FIXME: support wildcards! */
		
	    strcat(temp,":");
	    for (i=0;((e)&&(e[i]));i++) {
    		t_connection * user;
    		const char * addr = NULL;
    		
    		if((user = connlist_find_connection_by_accountname(e[i]))) {
    		    addr = addr_num_to_ip_str(conn_get_addr(user));
    		}
    		sprintf(_temp_a,"%s %s ",e[i],addr);
    		strcat(temp,_temp_a);
	    }
	    
        strcat(temp,":");
        strcat(temp,"1337"); /* yes, ha ha funny, i just don't generate game numbers yet */
        strcat(temp," ");
        
        now = time(NULL);
        sprintf(_temp_a,"%lu",now);
    	strcat(temp,_temp_a);;
	    
	    eventlog(eventlog_level_debug,__FUNCTION__,"[** WOL **] STARTG: (%s)",temp);

	    if ((channel = channellist_find_channel_by_name(irc_convert_ircname(params[0]),NULL,NULL))) {
     		channel_message_send(channel,message_wol_start_game,conn,temp);
	    }
	    else {
     		irc_send(conn,ERR_NOSUCHCHANNEL,":No such channel");
	    }
    }
	else
	    irc_send(conn,ERR_NEEDMOREPARAMS,"STARTG :Not enough parameters");	    
   	return 0;
}

static int _handle_kick_command(t_connection * conn, int numparams, char ** params, char * text)
{
    char temp[MAX_IRC_MESSAGE_LEN];
    char ** e;
    /**
    *  Heres the imput expected
    *  KICK [channel] [kicked_user],[kicked_user2]
    *
    *  Heres the output expected
    *  :user!WWOL@hostname KICK [channel] [kicked_user] :[text]
    *
    *  WOL in [text] sends Admin name
    */

    if ((numparams != 2) || !(params[1])) {
	    irc_send(conn,ERR_NEEDMOREPARAMS,"KICK :Not enough parameters");
	    return 0;
    }

    e = irc_get_listelems(params[1]);

    /* Make standart PvPGN KICK from RFC2812 KICK */
    if (text)
        snprintf(temp, sizeof(temp), "/kick %s %s", e[0], text);
    else
        snprintf(temp, sizeof(temp), "/kick %s", e[0]);

    handle_command(conn, temp);

    if (e)
        irc_unget_listelems(e);

    return 0;
}


/**
 * LADDER Server commands:
 *
 * Client standartly initialise connection, sends command and after that
 * standing for server response. When all data are sended, server close
 * connection which is an infromation to client that transfer is done.
 * At the moment we only closing connection.
 */
static int _handle_listsearch_command(t_connection * conn, int numparams, char ** params, char * text)
{
	// FIXME: Not implemetned yet
	conn_set_state(conn, conn_state_destroy);
	return 0;
}

static int _handle_rungsearch_command(t_connection * conn, int numparams, char ** params, char * text)
{
	// FIXME: Not implemetned yet
	conn_set_state(conn, conn_state_destroy);
	return 0;
}

static int _handle_highscore_command(t_connection * conn, int numparams, char ** params, char * text)
{
	// FIXME: Not implemetned yet
	conn_set_state(conn, conn_state_destroy);
	return 0;
}
