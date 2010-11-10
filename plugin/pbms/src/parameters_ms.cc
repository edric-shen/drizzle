/* Copyright (c) 2010 PrimeBase Technologies GmbH, Germany
 *
 * PrimeBase Media Stream for MySQL
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Barry Leslie
 *
 * 2010-05-25
 *
 * PBMS daemon global parameters.
 *
 */

#ifdef DRIZZLED
#include "config.h"
#include <set>
#include <drizzled/common.h>
#include <drizzled/plugin.h>
#include <drizzled/session.h>
using namespace std;
using namespace drizzled;
using namespace drizzled::plugin;

#define my_strdup(a,b) strdup(a)

#include "cslib/CSConfig.h"
#else
#include "cslib/CSConfig.h"
#include "mysql_priv.h"
#include <mysql/plugin.h>
#include <my_dir.h>
#endif 

#include <inttypes.h>
#include <string.h>

#include "cslib/CSDefs.h"
#include "cslib/CSObject.h"
#include "cslib/CSGlobal.h"
#include "cslib/CSThread.h"
#include "cslib/CSStrUtil.h"
#include "cslib/CSPath.h"
#include "cslib/CSLog.h"

#include "defs_ms.h"
#include "database_ms.h"
#include "parameters_ms.h"

#ifndef PBMS_PORT
#define PBMS_PORT 8080
#endif


uint32_t pbms_port_number = PBMS_PORT;

static char		*my_repository_threshold = NULL;
static char		*my_temp_log_threshold = NULL;
static char		*my_http_metadata_headers = NULL;

static u_long	my_temp_blob_timeout = MS_DEFAULT_TEMP_LOG_WAIT;
static u_long	my_garbage_threshold = MS_DEFAULT_GARBAGE_LEVEL;
static u_long	my_max_keep_alive = MS_DEFAULT_KEEP_ALIVE;

static u_long	my_backup_db_id = 1;
static uint32_t my_server_id = 1;

#ifdef DRIZZLED
static set<string> my_black_list;
static bool my_events_enabled = true;
static CSMutex my_table_list_lock;

typedef enum {MATCH_ALL, MATCH_DBS, MATCH_SOME, MATCH_NONE, MATCH_UNKNOWN, MATCH_ERROR} TableMatchState;
static char *my_table_list = NULL;
static const char *dflt_my_table_list = "*";

static TableMatchState my_table_match = MATCH_UNKNOWN;

static int32_t my_before_insert_position= 1;      // Call this event observer first.
static int32_t my_before_update_position= 1;

using namespace drizzled;
using namespace drizzled::plugin;

#define st_mysql_sys_var drizzled::drizzle_sys_var
#else

struct st_mysql_sys_var
{
  MYSQL_PLUGIN_VAR_HEADER;
};

#endif

#if MYSQL_VERSION_ID < 60000

#if MYSQL_VERSION_ID >= 50124
#define CONST_SAVE const
#endif

#else

#if MYSQL_VERSION_ID >= 60005
#define CONST_SAVE const
#endif

#endif

#ifndef CONST_SAVE
#define CONST_SAVE 
#endif

//--------------
uint32_t PBMSParameters::getPortNumber(){ return pbms_port_number;}

//--------------
uint32_t PBMSParameters::getServerID(){ return my_server_id;}

//--------------
uint64_t PBMSParameters::getRepoThreshold()
{
	if (my_repository_threshold)
		return((uint64_t) cs_byte_size_to_int8(my_repository_threshold));

	return((uint64_t) cs_byte_size_to_int8(MS_REPO_THRESHOLD_DEF));
}

//--------------
uint64_t PBMSParameters::getTempLogThreshold()
{
	if (my_temp_log_threshold)
		return((uint64_t) cs_byte_size_to_int8(my_temp_log_threshold));

	return((uint64_t) cs_byte_size_to_int8(MS_TEMP_LOG_THRESHOLD_DEF));
}

//--------------
uint32_t PBMSParameters::getTempBlobTimeout(){ return my_temp_blob_timeout;}

//--------------
uint32_t PBMSParameters::getGarbageThreshold(){ return my_garbage_threshold;}

//--------------
uint32_t PBMSParameters::getMaxKeepAlive(){ return my_max_keep_alive;}

//--------------
const char * PBMSParameters::getDefaultMetaDataHeaders()
{
	if (my_http_metadata_headers)
		return my_http_metadata_headers; 
		
	return MS_HTTP_METADATA_HEADERS_DEF; 
}

//-----------------
uint32_t PBMSParameters::getBackupDatabaseID() { return my_backup_db_id;}

//-----------------
void PBMSParameters::setBackupDatabaseID(uint32_t id) { my_backup_db_id = id;}

#ifdef DRIZZLED
//-----------------
bool PBMSParameters::isPBMSEventsEnabled() { return my_events_enabled;}


#define NEXT_IN_TABLE_LIST(list) {\
	while ((*list) && (*list != ',')) list++;\
	if (*list) list++;\
}
	
static TableMatchState set_match_type(const char *list)
{
	const char *ptr = list;
	const char *name;
	int name_len;
	TableMatchState match_state;

	if (!list)
		return MATCH_ALL;
		
	if (!ptr) {
		return MATCH_NONE;
	}
	
	while ((*ptr) && isspace(*ptr)) ptr++;
	if (!*ptr) {
		return MATCH_NONE;
	}
	
	match_state = MATCH_UNKNOWN;

	while (*ptr) {
	
		// Check database name
		name = ptr;
		name_len = 0;
		while ((*ptr) && (!isspace(*ptr)) && (*ptr != ',') && (*ptr != '.')) {ptr++;name_len++;}
		while ((*ptr) && isspace(*ptr)) ptr++;
		
		if (*ptr != '.') {
			if ((name_len == 1) && (*name == '*'))
				match_state = MATCH_ALL;
			else
				goto bad_list; // Missing table
		} else {
		
			if ((match_state > MATCH_DBS) && (name_len == 1) && (*name == '*'))
				match_state = MATCH_DBS;
				
			ptr++; // Skip the '.'
			
			// Find the start of the table name.
			while ((*ptr) && isspace(*ptr)) ptr++;
			if ((!*ptr) || (*ptr == ',') || (*ptr == '.'))
				goto bad_list; // Missing table
				
			// Find the end of the table name.
			while ((*ptr) && (!isspace(*ptr)) && (*ptr != ',') && (*ptr != '.')) ptr++;
		}
		
		// Locate the end of the element.
		while ((*ptr) && isspace(*ptr)) ptr++;
				
		if ((*ptr) && (*ptr != ','))
			goto bad_list; // Bad table name
			
		if (match_state > MATCH_SOME)
			match_state = MATCH_SOME;
			
		if (*ptr) ptr++;
		while ((*ptr) && isspace(*ptr)) ptr++;
	}
	
	return match_state;
bad_list:

	char info[120];
	snprintf(info, 120, "pbms_watch_tables format error near character position %d", (int) (ptr - list));
	CSL.logLine(NULL, CSLog::Error, info);
	CSL.logLine(NULL, CSLog::Error, list);
	
	return MATCH_ERROR;
}

//-----------------
static const char* locate_db(const char *list, const char *db, int len)
{
	int match_len;
	
	while (*list) {
		while ((*list) && isspace(*list)) list++;
		if ((*list == 0) || (*(list+1) == 0) || (*(list+2) == 0)) // We need at least 3 characters
			return NULL;
		
		match_len = 0;
		if (*list == '*') 
			match_len = 1;
		else if (strncmp(list, db, len) == 0) 
			match_len = len;
		
		if (match_len) {
			list += match_len;
			
			// Find the '.'
			while ((*list) && isspace(*list)) list++;
			if ((*list == 0) || (*(list+1) == 0) ) // We need at least 2 characters
				return NULL;
						
			if (*list == '.') { 
				list++;
				while ((*list) && isspace(*list)) list++;
				if (*list == 0)
					 return NULL;
					 
				return list; // We have gound a table that could belong to this database;
			}
		}
		
		NEXT_IN_TABLE_LIST(list);
	}
	
	return NULL;
}

//--------------
// Parameter update functions are not called for parameters that are set on
// the command line. PBMSParameters::startUp() will perform any initialization required.
void PBMSParameters::startUp()
{ 
	my_table_match = set_match_type(my_table_list);
}

//-----------------
bool PBMSParameters::isBlackListedDB(const char *db)
{
	if (my_black_list.find(string(db)) == my_black_list.end())
		return false;
		
	return true;
}		

//-----------------
void PBMSParameters::blackListedDB(const char *db)
{
	my_black_list.insert(string(db));
}		

//-----------------
bool PBMSParameters::try_LocateDB(CSThread *self, const char *db, bool *found)
{
	volatile bool rtc = true;
	try_(a) {
		lock_(&my_table_list_lock);	
		
			
		*found = (locate_db(my_table_list, db, strlen(db)) != NULL);
			
		unlock_(&my_table_list_lock);
		rtc = false;
	}
	
	catch_(a)
	cont_(a);
	return rtc;
}

//-----------------
bool PBMSParameters::isBLOBDatabase(const char *db)
{
	CSThread *self;
	int		err;
	PBMSResultRec result;
	bool found = false;
	
	if (isBlackListedDB(db))
		return false;
		
	if (my_table_match == MATCH_UNKNOWN)
		my_table_match = set_match_type(my_table_list);

	if (my_table_match == MATCH_NONE)
		return false;

	if (my_table_match <= MATCH_DBS)
		return true;
	
	if ((err = MSEngine::enterConnectionNoThd(&self, &result)) == 0) {

		inner_();
		if (try_LocateDB(self, db, &found)) {
			err = MSEngine::exceptionToResult(&self->myException, &result);
		}		
		outer_();
	
	}
	
	if (err) {
		fprintf(stderr, "PBMSParameters::isBLOBDatabase(\"%s\") error (%d):'%s'\n", 
			db, result.mr_code,  result.mr_message);
	}
	
	return found;
}
	
//-----------------
bool PBMSParameters::try_LocateTable(CSThread *self, const char *db, const char *table, bool *found)
{
	volatile bool rtc = true;
	try_(a) {
		const char *ptr;
		int db_len, table_len, match_len;
		
		lock_(&my_table_list_lock);	
				
		db_len = strlen(db);
		table_len = strlen(table);
		
		ptr = my_table_list;
		while (ptr) {
			ptr = locate_db(ptr, db, db_len);
			if (ptr) {
				match_len = 0;
				if (*ptr == '*')
					match_len = 1;
				else if (strncmp(ptr, table, table_len) == 0)
					match_len = table_len;
					
				if (match_len) {
					ptr += match_len;
					if ((!*ptr) || (*ptr == ',') || isspace(*ptr)) {
						*found = true;
						break;
					}
				}
				
				NEXT_IN_TABLE_LIST(ptr);
			}
		}
			
		unlock_(&my_table_list_lock);
		rtc = false;
	}
	
	catch_(a)
	cont_(a);
	return rtc;
}

//-----------------
bool PBMSParameters::isBLOBTable(const char *db, const char *table)
{
	CSThread *self;
	int		err;
	PBMSResultRec result;
	bool found = false;
	
	if (isBlackListedDB(db))
		return false;
		
	if (my_table_match == MATCH_UNKNOWN)
		my_table_match = set_match_type(my_table_list);

	if (my_table_match == MATCH_NONE)
		return false;

	if (my_table_match <= MATCH_ALL)
		return true;

	if ((err = MSEngine::enterConnectionNoThd(&self, &result)) == 0) {

		inner_();
		if (try_LocateTable(self, db, table, &found)) {
			err = MSEngine::exceptionToResult(&self->myException, &result);
		}		
		outer_();
	
	}
	
	if (err) {
		fprintf(stderr, "PBMSParameters::isBLOBTable(\"%s\", \"%s\") error (%d):'%s'\n", 
			db, table, result.mr_code,  result.mr_message);
	}
	
	return found;
}


//-----------------
int32_t PBMSParameters::getBeforeUptateEventPosition() { return my_before_update_position;}

//-----------------
int32_t PBMSParameters::getBeforeInsertEventPosition() { return my_before_insert_position;}
#endif // DRIZZLED

//-----------------
static void pbms_temp_blob_timeout_func(THD *thd, struct st_mysql_sys_var *var, void *trg, CONST_SAVE void *save)
{
	CSThread		*self;
	PBMSResultRec	result;

	(void)thd;
	(void)var;
	
	*(u_long *)trg= *(u_long *) save;
	
	if (MSEngine::enterConnectionNoThd(&self, &result))
		return;
	try_(a) {
		MSDatabase::wakeTempLogThreads();
	}
	
	catch_(a);
	cont_(a);
}

//-----------------
//-----------------
static MYSQL_SYSVAR_UINT(port, pbms_port_number,
	PLUGIN_VAR_READONLY,
	"The port for the server stream-based communications.",
	NULL, NULL, PBMS_PORT, 0, 64*1024, 1);

static MYSQL_SYSVAR_STR(repository_threshold, my_repository_threshold,
	PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_MEMALLOC,
	"The maximum size of a BLOB repository file.",
	NULL, NULL, MS_REPO_THRESHOLD_DEF);

static MYSQL_SYSVAR_STR(temp_log_threshold, my_temp_log_threshold,
	PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_MEMALLOC,
	"The maximum size of a temorary BLOB log file.",
	NULL, NULL, MS_TEMP_LOG_THRESHOLD_DEF);

static MYSQL_SYSVAR_STR(http_metadata_headers, my_http_metadata_headers,
	PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
	"A ':' delimited list of metadata header names to be used to initialize the pbms_metadata_header table when a database is created.",
	NULL, NULL , MS_HTTP_METADATA_HEADERS_DEF);

static MYSQL_SYSVAR_ULONG(temp_blob_timeout, my_temp_blob_timeout,
	PLUGIN_VAR_OPCMDARG,
	"The timeout, in seconds, for temporary BLOBs. Uploaded blob data is removed after this time, unless committed to the database.",
	NULL, pbms_temp_blob_timeout_func, MS_DEFAULT_TEMP_LOG_WAIT, 1, ~0L, 1);

static MYSQL_SYSVAR_ULONG(garbage_threshold, my_garbage_threshold,
	PLUGIN_VAR_OPCMDARG,
	"The percentage of garbage in a repository file before it is compacted.",
	NULL, NULL, MS_DEFAULT_GARBAGE_LEVEL, 0, 100, 1);


static MYSQL_SYSVAR_ULONG(max_keep_alive, my_max_keep_alive,
	PLUGIN_VAR_OPCMDARG,
	"The timeout, in milli-seconds, before the HTTP server will close an inactive HTTP connection.",
	NULL, NULL, MS_DEFAULT_KEEP_ALIVE, 1, UINT32_MAX, 1);

static MYSQL_SYSVAR_ULONG(next_backup_db_id, my_backup_db_id,
	PLUGIN_VAR_OPCMDARG,
	"The next backup ID to use when backing up a PBMS database.",
	NULL, NULL, 1, 1, UINT32_MAX, 1);


#ifdef DRIZZLED

//----------
static int check_table_list(THD *, struct st_mysql_sys_var *, void *save, drizzle_value *value)
{
	char buffer[100];
	int len = 100;
	const char *list = value->val_str(value, buffer, &len);
	TableMatchState state;
	
	state = set_match_type(list);
	if (state == MATCH_ERROR)
		return 1;
		
	char *new_list = strdup(list);
	if (!new_list)
		return 1;
	
	my_table_list_lock.lock();
	if (my_table_list && (my_table_list != dflt_my_table_list)) free(my_table_list);
	my_table_list = new_list;
	
	my_table_match = state;
	my_table_list_lock.unlock();

   *static_cast<const char**>(save)= my_table_list;

	return 0;
}

//----------
static void set_table_list(THD *, struct st_mysql_sys_var *, void *var_ptr, CONST_SAVE void *)
{
	// Everything was done in check_table_list()
  *static_cast<const char**>(var_ptr)= my_table_list;
}

//----------

static MYSQL_SYSVAR_STR(watch_tables,
                           my_table_list,
                           PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_MEMALLOC,
                           N_("A comma delimited list of tables to watch of the format: <database>.<table>, ..."),
                           check_table_list, /* check func */
                           set_table_list, /* update func */
                           dflt_my_table_list /* default */);

static MYSQL_SYSVAR_BOOL(watch_enable,
                           my_events_enabled,
                           PLUGIN_VAR_OPCMDARG,
                           N_("Enable PBMS daemon Insert/Update/Delete event scanning"),
                           NULL, /* check func */
                           NULL, /* update func */
                           true /* default */);

static MYSQL_SYSVAR_INT(before_insert_position,
                           my_before_insert_position,
                           PLUGIN_VAR_OPCMDARG,
                           N_("Before insert row event observer call position"),
                           NULL, /* check func */
                           NULL, /* update func */
                           1, /* default */
                           1, /* min */
                           INT32_MAX -1, /* max */
                           0 /* blk */);

static MYSQL_SYSVAR_INT(before_update_position,
                           my_before_update_position,
                           PLUGIN_VAR_OPCMDARG,
                           N_("Before update row event observer call position"),
                           NULL, /* check func */
                           NULL, /* update func */
                           1, /* default */
                           1, /* min */
                           INT32_MAX -1, /* max */
                           0 /* blk */);

#endif // DRIZZLED

struct st_mysql_sys_var* pbms_system_variables[] = {
	MYSQL_SYSVAR(port),
	MYSQL_SYSVAR(repository_threshold),
	MYSQL_SYSVAR(temp_log_threshold),
	MYSQL_SYSVAR(temp_blob_timeout),
	MYSQL_SYSVAR(garbage_threshold),
	MYSQL_SYSVAR(http_metadata_headers),
	MYSQL_SYSVAR(max_keep_alive),
	MYSQL_SYSVAR(next_backup_db_id),
	
#ifdef DRIZZLED
	MYSQL_SYSVAR(watch_tables),
	MYSQL_SYSVAR(watch_enable),
	
	MYSQL_SYSVAR(before_insert_position),
	MYSQL_SYSVAR(before_update_position),
#endif
  
	NULL
};





