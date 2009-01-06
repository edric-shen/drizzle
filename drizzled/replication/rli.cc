/* Copyright (C) 2000-2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <drizzled/server_includes.h>
#include <drizzled/replication/mi.h>
#include <drizzled/replication/rli.h>
// For check_binlog_magic
#include <drizzled/replication/replication.h>
#include <drizzled/replication/utility.h>
#include <drizzled/data_home.h>
#include <drizzled/sql_parse.h>
#include <drizzled/gettext.h>
#include <drizzled/log_event.h>
#include <drizzled/sql_base.h>

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif



static int32_t count_relay_log_space(Relay_log_info* rli);

// Defined in slave.cc
int32_t init_intvar_from_file(int32_t* var, IO_CACHE* f, int32_t default_val);
int32_t init_strvar_from_file(char *var, int32_t max_size, IO_CACHE *f,
                              const char *default_val);


Relay_log_info::Relay_log_info()
  :Slave_reporting_capability("SQL"),
   no_storage(false), replicate_same_server_id(::replicate_same_server_id),
   info_fd(-1), cur_log_fd(-1), save_temporary_tables(0),
#if defined(HAVE_purify) && HAVE_purify
   is_fake(false),
#endif
   cur_log_old_open_count(0), group_relay_log_pos(0), event_relay_log_pos(0),
   group_master_log_pos(0), log_space_total(0), ignore_log_space_limit(0),
   last_master_timestamp(0), slave_skip_counter(0),
   abort_pos_wait(0), slave_run_id(0), sql_session(0),
   inited(0), abort_slave(0), slave_running(0), until_condition(UNTIL_NONE),
   until_log_pos(0), retried_trans(0),
   tables_to_lock(0), tables_to_lock_count(0),
   last_event_start_time(0), m_flags(0)
{
  group_relay_log_name[0]= event_relay_log_name[0]=
    group_master_log_name[0]= 0;
  until_log_name[0]= ign_master_log_name_end[0]= 0;
  memset(&info_file, 0, sizeof(info_file));
  memset(&cache_buf, 0, sizeof(cache_buf));
  pthread_mutex_init(&run_lock, MY_MUTEX_INIT_FAST);
  pthread_mutex_init(&data_lock, MY_MUTEX_INIT_FAST);
  pthread_mutex_init(&log_space_lock, MY_MUTEX_INIT_FAST);
  pthread_cond_init(&data_cond, NULL);
  pthread_cond_init(&start_cond, NULL);
  pthread_cond_init(&stop_cond, NULL);
  pthread_cond_init(&log_space_cond, NULL);
  relay_log.init_pthread_objects();
  return;
}


Relay_log_info::~Relay_log_info()
{
  pthread_mutex_destroy(&run_lock);
  pthread_mutex_destroy(&data_lock);
  pthread_mutex_destroy(&log_space_lock);
  pthread_cond_destroy(&data_cond);
  pthread_cond_destroy(&start_cond);
  pthread_cond_destroy(&stop_cond);
  pthread_cond_destroy(&log_space_cond);
  relay_log.cleanup();
  return;
}


int32_t init_relay_log_info(Relay_log_info* rli,
                            const char* info_fname)
{
  char fname[FN_REFLEN+128];
  int32_t info_fd;
  const char* msg = 0;
  int32_t error = 0;
  assert(!rli->no_storage);         // Don't init if there is no storage

  if (rli->inited)                       // Set if this function called
    return(0);
  fn_format(fname, info_fname, drizzle_data_home, "", 4+32);
  pthread_mutex_lock(&rli->data_lock);
  info_fd = rli->info_fd;
  rli->cur_log_fd = -1;
  rli->slave_skip_counter=0;
  rli->abort_pos_wait=0;
  rli->log_space_limit= relay_log_space_limit;
  rli->log_space_total= 0;
  rli->tables_to_lock= 0;
  rli->tables_to_lock_count= 0;

  /*
    The relay log will now be opened, as a SEQ_READ_APPEND IO_CACHE.
    Note that the I/O thread flushes it to disk after writing every
    event, in flush_master_info(mi, 1).
  */

  /*
    For the maximum log size, we choose max_relay_log_size if it is
    non-zero, max_binlog_size otherwise. If later the user does SET
    GLOBAL on one of these variables, fix_max_binlog_size and
    fix_max_relay_log_size will reconsider the choice (for example
    if the user changes max_relay_log_size to zero, we have to
    switch to using max_binlog_size for the relay log) and update
    rli->relay_log.max_size (and drizzle_bin_log.max_size).
  */
  {
    char buf[FN_REFLEN];
    const char *ln;
    static bool name_warning_sent= 0;
    ln= rli->relay_log.generate_name(opt_relay_logname, "-relay-bin",
                                     1, buf);
    /* We send the warning only at startup, not after every RESET SLAVE */
    if (!opt_relay_logname && !opt_relaylog_index_name && !name_warning_sent)
    {
      /*
        User didn't give us info to name the relay log index file.
        Picking `hostname`-relay-bin.index like we do, causes replication to
        fail if this slave's hostname is changed later. So, we would like to
        instead require a name. But as we don't want to break many existing
        setups, we only give warning, not error.
      */
      errmsg_printf(ERRMSG_LVL_WARN, _("Neither --relay-log nor --relay-log-index were used;"
                        " so replication "
                        "may break when this Drizzle server acts as a "
                        "slave and has his hostname changed!! Please "
                        "use '--relay-log=%s' to avoid this problem."), ln);
      name_warning_sent= 1;
    }
    /*
      note, that if open() fails, we'll still have index file open
      but a destructor will take care of that
    */
    if (rli->relay_log.open_index_file(opt_relaylog_index_name, ln) ||
        rli->relay_log.open(ln, LOG_BIN, 0, SEQ_READ_APPEND, 0,
                            (max_relay_log_size ? max_relay_log_size :
                            max_binlog_size), 1))
    {
      pthread_mutex_unlock(&rli->data_lock);
      errmsg_printf(ERRMSG_LVL_ERROR, _("Failed in open_log() called from "
                        "init_relay_log_info()"));
      return(1);
    }
  }

  /* if file does not exist */
  if (access(fname,F_OK))
  {
    /* Create a new file */
  }
  else // file exists
  {
    /* Open up fname here and pull out the relay.info data */
  }

  /*
    Now change the cache from READ to WRITE - must do this
    before flush_relay_log_info
  */
  reinit_io_cache(&rli->info_file, WRITE_CACHE,0L,0,1);
  if ((error= flush_relay_log_info(rli)))
    errmsg_printf(ERRMSG_LVL_ERROR, _("Failed to flush relay log info file"));
  if (count_relay_log_space(rli))
  {
    msg=_("Error counting relay log space");
    goto err;
  }
  rli->inited= 1;
  pthread_mutex_unlock(&rli->data_lock);
  return(error);

err:
  errmsg_printf(ERRMSG_LVL_ERROR, "%s",msg);
  end_io_cache(&rli->info_file);
  if (info_fd >= 0)
    my_close(info_fd, MYF(0));
  rli->info_fd= -1;
  rli->relay_log.close(LOG_CLOSE_INDEX | LOG_CLOSE_STOP_EVENT);
  pthread_mutex_unlock(&rli->data_lock);
  return(1);
}


static inline int32_t add_relay_log(Relay_log_info* rli,LOG_INFO* linfo)
{
  struct stat s;
  if (stat(linfo->log_file_name,&s))
  {
    errmsg_printf(ERRMSG_LVL_ERROR, _("log %s listed in the index, but failed to stat"),
                    linfo->log_file_name);
    return(1);
  }
  rli->log_space_total += s.st_size;
  return(0);
}


static int32_t count_relay_log_space(Relay_log_info* rli)
{
  LOG_INFO linfo;
  rli->log_space_total= 0;
  if (rli->relay_log.find_log_pos(&linfo, NULL, 1))
  {
    errmsg_printf(ERRMSG_LVL_ERROR, _("Could not find first log while counting relay "
                      "log space"));
    return(1);
  }
  do
  {
    if (add_relay_log(rli,&linfo))
      return(1);
  } while (!rli->relay_log.find_next_log(&linfo, 1));
  /*
     As we have counted everything, including what may have written in a
     preceding write, we must reset bytes_written, or we may count some space
     twice.
  */
  rli->relay_log.reset_bytes_written();
  return(0);
}


/*
   Reset UNTIL condition for Relay_log_info

   SYNOPSYS
    clear_until_condition()
      rli - Relay_log_info structure where UNTIL condition should be reset
 */

void Relay_log_info::clear_until_condition()
{
  until_condition= Relay_log_info::UNTIL_NONE;
  until_log_name[0]= 0;
  until_log_pos= 0;
  return;
}


/*
  Open the given relay log

  SYNOPSIS
    init_relay_log_pos()
    rli                 Relay information (will be initialized)
    log                 Name of relay log file to read from. NULL = First log
    pos                 Position in relay log file
    need_data_lock      Set to 1 if this functions should do mutex locks
    errmsg              Store pointer to error message here
    look_for_description_event
                        1 if we should look for such an event. We only need
                        this when the SQL thread starts and opens an existing
                        relay log and has to execute it (possibly from an
                        offset >4); then we need to read the first event of
                        the relay log to be able to parse the events we have
                        to execute.

  DESCRIPTION
  - Close old open relay log files.
  - If we are using the same relay log as the running IO-thread, then set
    rli->cur_log to point to the same IO_CACHE entry.
  - If not, open the 'log' binary file.

  TODO
    - check proper initialization of group_master_log_name/group_master_log_pos

  RETURN VALUES
    0   ok
    1   error.  errmsg is set to point to the error message
*/

int32_t init_relay_log_pos(Relay_log_info* rli,const char* log,
                       uint64_t pos, bool need_data_lock,
                       const char** errmsg,
                       bool look_for_description_event)
{
  *errmsg=0;
  pthread_mutex_t *log_lock=rli->relay_log.get_log_lock();

  if (need_data_lock)
    pthread_mutex_lock(&rli->data_lock);

  /*
    Slave threads are not the only users of init_relay_log_pos(). CHANGE MASTER
    is, too, and init_slave() too; these 2 functions allocate a description
    event in init_relay_log_pos, which is not freed by the terminating SQL slave
    thread as that thread is not started by these functions. So we have to free
    the description_event here, in case, so that there is no memory leak in
    running, say, CHANGE MASTER.
  */
  delete rli->relay_log.description_event_for_exec;
  /*
    By default the relay log is in binlog format 3 (4.0).
    Even if format is 4, this will work enough to read the first event
    (Format_desc) (remember that format 4 is just lenghtened compared to format
    3; format 3 is a prefix of format 4).
  */
  rli->relay_log.description_event_for_exec= new
    Format_description_log_event(3);

  pthread_mutex_lock(log_lock);

  /* Close log file and free buffers if it's already open */
  if (rli->cur_log_fd >= 0)
  {
    end_io_cache(&rli->cache_buf);
    my_close(rli->cur_log_fd, MYF(MY_WME));
    rli->cur_log_fd = -1;
  }

  rli->group_relay_log_pos = rli->event_relay_log_pos = pos;

  /*
    Test to see if the previous run was with the skip of purging
    If yes, we do not purge when we restart
  */
  if (rli->relay_log.find_log_pos(&rli->linfo, NULL, 1))
  {
    *errmsg="Could not find first log during relay log initialization";
    goto err;
  }

  if (log && rli->relay_log.find_log_pos(&rli->linfo, log, 1))
  {
    *errmsg="Could not find target log during relay log initialization";
    goto err;
  }

  rli->group_relay_log_name.assign(rli->linfo.log_file_name);
  rli->event_relay_log_name.assign(rli->linfo.log_file_name);

  if (rli->relay_log.is_active(rli->linfo.log_file_name))
  {
    /*
      The IO thread is using this log file.
      In this case, we will use the same IO_CACHE pointer to
      read data as the IO thread is using to write data.
    */
    my_b_seek((rli->cur_log=rli->relay_log.get_log_file()), (off_t)0);
    if (check_binlog_magic(rli->cur_log,errmsg))
      goto err;
    rli->cur_log_old_open_count=rli->relay_log.get_open_count();
  }
  else
  {
    /*
      Open the relay log and set rli->cur_log to point at this one
    */
    if ((rli->cur_log_fd=open_binlog(&rli->cache_buf,
                                     rli->linfo.log_file_name,errmsg)) < 0)
      goto err;
    rli->cur_log = &rli->cache_buf;
  }
  /*
    In all cases, check_binlog_magic() has been called so we're at offset 4 for
    sure.
  */
  if (pos > BIN_LOG_HEADER_SIZE) /* If pos<=4, we stay at 4 */
  {
    Log_event* ev;
    while (look_for_description_event)
    {
      /*
        Read the possible Format_description_log_event; if position
        was 4, no need, it will be read naturally.
      */
      if (my_b_tell(rli->cur_log) >= pos)
        break;

      /*
        Because of we have rli->data_lock and log_lock, we can safely read an
        event
      */
      if (!(ev=Log_event::read_log_event(rli->cur_log,0,
                                         rli->relay_log.description_event_for_exec)))
      {
        if (rli->cur_log->error) /* not EOF */
        {
          *errmsg= "I/O error reading event at position 4";
          goto err;
        }
        break;
      }
      else if (ev->get_type_code() == FORMAT_DESCRIPTION_EVENT)
      {
        delete rli->relay_log.description_event_for_exec;
        rli->relay_log.description_event_for_exec= (Format_description_log_event*) ev;
        /*
          As ev was returned by read_log_event, it has passed is_valid(), so
          malloc() in ctor worked, no need to check again.
        */
        /*
          Ok, we found a Format_description event. But it is not sure that this
          describes the whole relay log; indeed, one can have this sequence
          (starting from position 4):
          Format_desc (of slave)
          Rotate (of master)
          Format_desc (of master)
          So the Format_desc which really describes the rest of the relay log
          is the 3rd event (it can't be further than that, because we rotate
          the relay log when we queue a Rotate event from the master).
          But what describes the Rotate is the first Format_desc.
          So what we do is:
          go on searching for Format_description events, until you exceed the
          position (argument 'pos') or until you find another event than Rotate
          or Format_desc.
        */
      }
      else
      {
        look_for_description_event= (ev->get_type_code() == ROTATE_EVENT);
        delete ev;
      }
    }
    my_b_seek(rli->cur_log,(off_t)pos);

  }

err:
  /*
    If we don't purge, we can't honour relay_log_space_limit ;
    silently discard it
  */
  if (!relay_log_purge)
    rli->log_space_limit= 0;
  pthread_cond_broadcast(&rli->data_cond);

  pthread_mutex_unlock(log_lock);

  if (need_data_lock)
    pthread_mutex_unlock(&rli->data_lock);
  if (!rli->relay_log.description_event_for_exec->is_valid() && !*errmsg)
    *errmsg= "Invalid Format_description log event; could be out of memory";

  return ((*errmsg) ? 1 : 0);
}


/*
  Waits until the SQL thread reaches (has executed up to) the
  log/position or timed out.

  SYNOPSIS
    wait_for_pos()
    session             client thread that sent SELECT MASTER_POS_WAIT
    log_name        log name to wait for
    log_pos         position to wait for
    timeout         timeout in seconds before giving up waiting

  NOTES
    timeout is int64_t whereas it should be uint32_t ; but this is
    to catch if the user submitted a negative timeout.

  RETURN VALUES
    -2          improper arguments (log_pos<0)
                or slave not running, or master info changed
                during the function's execution,
                or client thread killed. -2 is translated to NULL by caller
    -1          timed out
    >=0         number of log events the function had to wait
                before reaching the desired log/position
 */

int32_t Relay_log_info::wait_for_pos(Session* session, String* log_name,
                                    int64_t log_pos,
                                    int64_t timeout)
{
  int32_t event_count = 0;
  uint32_t init_abort_pos_wait;
  int32_t error=0;
  struct timespec abstime; // for timeout checking
  const char *msg;

  if (!inited)
    return(-2);

  set_timespec(abstime,timeout);
  pthread_mutex_lock(&data_lock);
  msg= session->enter_cond(&data_cond, &data_lock,
                       "Waiting for the slave SQL thread to "
                       "advance position");
  /*
     This function will abort when it notices that some CHANGE MASTER or
     RESET MASTER has changed the master info.
     To catch this, these commands modify abort_pos_wait ; We just monitor
     abort_pos_wait and see if it has changed.
     Why do we have this mechanism instead of simply monitoring slave_running
     in the loop (we do this too), as CHANGE MASTER/RESET SLAVE require that
     the SQL thread be stopped?
     This is becasue if someones does:
     STOP SLAVE;CHANGE MASTER/RESET SLAVE; START SLAVE;
     the change may happen very quickly and we may not notice that
     slave_running briefly switches between 1/0/1.
  */
  init_abort_pos_wait= abort_pos_wait;

  /*
    We'll need to
    handle all possible log names comparisons (e.g. 999 vs 1000).
    We use uint32_t for string->number conversion ; this is no
    stronger limitation than in find_uniq_filename in sql/log.cc
  */
  uint32_t log_name_extension;
  char log_name_tmp[FN_REFLEN]; //make a char[] from String
  size_t len= cmin(log_name->length(), (uint32_t)FN_REFLEN-1);
  strncpy(log_name_tmp, log_name->ptr(), len);
  log_name_tmp[len]= '\0';

  char *p= fn_ext(log_name_tmp);
  char *p_end;
  if (!*p || log_pos<0)
  {
    error= -2; //means improper arguments
    goto err;
  }
  // Convert 0-3 to 4
  log_pos= cmax(log_pos, (int64_t)BIN_LOG_HEADER_SIZE);
  /* p points to '.' */
  log_name_extension= strtoul(++p, &p_end, 10);
  /*
    p_end points to the first invalid character.
    If it equals to p, no digits were found, error.
    If it contains '\0' it means conversion went ok.
  */
  if (p_end==p || *p_end)
  {
    error= -2;
    goto err;
  }

  /* The "compare and wait" main loop */
  while (!session->killed &&
         init_abort_pos_wait == abort_pos_wait &&
         slave_running)
  {
    bool pos_reached;
    int32_t cmp_result= 0;

    /*
      group_master_log_name can be "", if we are just after a fresh
      replication start or after a CHANGE MASTER TO MASTER_HOST/PORT
      (before we have executed one Rotate event from the master) or
      (rare) if the user is doing a weird slave setup (see next
      paragraph).  If group_master_log_name is "", we assume we don't
      have enough info to do the comparison yet, so we just wait until
      more data. In this case master_log_pos is always 0 except if
      somebody (wrongly) sets this slave to be a slave of itself
      without using --replicate-same-server-id (an unsupported
      configuration which does nothing), then group_master_log_pos
      will grow and group_master_log_name will stay "".
    */
    if (group_master_log_name.length())
    {
      const char *basename= (group_master_log_name.c_str() +
                             dirname_length(group_master_log_name.c_str()));
      /*
        First compare the parts before the extension.
        Find the dot in the master's log basename,
        and protect against user's input error :
        if the names do not match up to '.' included, return error
      */
      char *q= (char*)(fn_ext(basename)+1);
      if (strncmp(basename, log_name_tmp, (int32_t)(q-basename)))
      {
        error= -2;
        break;
      }
      // Now compare extensions.
      char *q_end;
      uint32_t group_master_log_name_extension= strtoul(q, &q_end, 10);
      if (group_master_log_name_extension < log_name_extension)
        cmp_result= -1 ;
      else
        cmp_result= (group_master_log_name_extension > log_name_extension) ? 1 : 0 ;

      pos_reached= ((!cmp_result && group_master_log_pos >= (uint64_t)log_pos) ||
                    cmp_result > 0);
      if (pos_reached || session->killed)
        break;
    }

    //wait for master update, with optional timeout.

    /*
      We are going to pthread_cond_(timed)wait(); if the SQL thread stops it
      will wake us up.
    */
    if (timeout > 0)
    {
      /*
        Note that pthread_cond_timedwait checks for the timeout
        before for the condition ; i.e. it returns ETIMEDOUT
        if the system time equals or exceeds the time specified by abstime
        before the condition variable is signaled or broadcast, _or_ if
        the absolute time specified by abstime has already passed at the time
        of the call.
        For that reason, pthread_cond_timedwait will do the "timeoutting" job
        even if its condition is always immediately signaled (case of a loaded
        master).
      */
      error=pthread_cond_timedwait(&data_cond, &data_lock, &abstime);
    }
    else
      pthread_cond_wait(&data_cond, &data_lock);
    if (error == ETIMEDOUT || error == ETIME)
    {
      error= -1;
      break;
    }
    error=0;
    event_count++;
  }

err:
  session->exit_cond(msg);
  if (session->killed || init_abort_pos_wait != abort_pos_wait ||
      !slave_running)
  {
    error= -2;
  }
  return( error ? error : event_count );
}


void Relay_log_info::inc_group_relay_log_pos(uint64_t log_pos,
                                                bool skip_lock)
{
  if (!skip_lock)
    pthread_mutex_lock(&data_lock);
  inc_event_relay_log_pos();
  group_relay_log_pos= event_relay_log_pos;
  group_relay_log_name.assign(event_relay_log_name);

  notify_group_relay_log_name_update();

  /*
    If the slave does not support transactions and replicates a transaction,
    users should not trust group_master_log_pos (which they can display with
    SHOW SLAVE STATUS or read from relay-log.info), because to compute
    group_master_log_pos the slave relies on log_pos stored in the master's
    binlog, but if we are in a master's transaction these positions are always
    the BEGIN's one (excepted for the COMMIT), so group_master_log_pos does
    not advance as it should on the non-transactional slave (it advances by
    big leaps, whereas it should advance by small leaps).
  */
  /*
    In 4.x we used the event's len to compute the positions here. This is
    wrong if the event was 3.23/4.0 and has been converted to 5.0, because
    then the event's len is not what is was in the master's binlog, so this
    will make a wrong group_master_log_pos (yes it's a bug in 3.23->4.0
    replication: Exec_master_log_pos is wrong). Only way to solve this is to
    have the original offset of the end of the event the relay log. This is
    what we do in 5.0: log_pos has become "end_log_pos" (because the real use
    of log_pos in 4.0 was to compute the end_log_pos; so better to store
    end_log_pos instead of begin_log_pos.
    If we had not done this fix here, the problem would also have appeared
    when the slave and master are 5.0 but with different event length (for
    example the slave is more recent than the master and features the event
    UID). It would give false MASTER_POS_WAIT, false Exec_master_log_pos in
    SHOW SLAVE STATUS, and so the user would do some CHANGE MASTER using this
    value which would lead to badly broken replication.
    Even the relay_log_pos will be corrupted in this case, because the len is
    the relay log is not "val".
    With the end_log_pos solution, we avoid computations involving lengthes.
  */
  if (log_pos) // 3.23 binlogs don't have log_posx
  {
    group_master_log_pos= log_pos;
  }
  pthread_cond_broadcast(&data_cond);
  if (!skip_lock)
    pthread_mutex_unlock(&data_lock);
  return;
}


void Relay_log_info::close_temporary_tables()
{
  Table *table,*next;

  for (table=save_temporary_tables ; table ; table=next)
  {
    next=table->next;
    /*
      Don't ask for disk deletion. For now, anyway they will be deleted when
      slave restarts, but it is a better intention to not delete them.
    */
    close_temporary(table, 1, 0);
  }
  save_temporary_tables= 0;
  slave_open_temp_tables= 0;
  return;
}

/*
  purge_relay_logs()

  NOTES
    Assumes to have a run lock on rli and that no slave thread are running.
*/

int32_t purge_relay_logs(Relay_log_info* rli, Session *session, bool just_reset,
                     const char** errmsg)
{
  int32_t error=0;

  /*
    Even if rli->inited==0, we still try to empty rli->master_log_* variables.
    Indeed, rli->inited==0 does not imply that they already are empty.
    It could be that slave's info initialization partly succeeded :
    for example if relay-log.info existed but *relay-bin*.*
    have been manually removed, init_relay_log_info reads the old
    relay-log.info and fills rli->master_log_*, then init_relay_log_info
    checks for the existence of the relay log, this fails and
    init_relay_log_info leaves rli->inited to 0.
    In that pathological case, rli->master_log_pos* will be properly reinited
    at the next START SLAVE (as RESET SLAVE or CHANGE
    MASTER, the callers of purge_relay_logs, will delete bogus *.info files
    or replace them with correct files), however if the user does SHOW SLAVE
    STATUS before START SLAVE, he will see old, confusing rli->master_log_*.
    In other words, we reinit rli->master_log_* for SHOW SLAVE STATUS
    to display fine in any case.
  */

  rli->group_master_log_name[0]= 0;
  rli->group_master_log_pos= 0;

  if (!rli->inited)
  {
    return(0);
  }

  assert(rli->slave_running == 0);
  assert(rli->mi->slave_running == 0);

  rli->slave_skip_counter=0;
  pthread_mutex_lock(&rli->data_lock);

  /*
    we close the relay log fd possibly left open by the slave SQL thread,
    to be able to delete it; the relay log fd possibly left open by the slave
    I/O thread will be closed naturally in reset_logs() by the
    close(LOG_CLOSE_TO_BE_OPENED) call
  */
  if (rli->cur_log_fd >= 0)
  {
    end_io_cache(&rli->cache_buf);
    my_close(rli->cur_log_fd, MYF(MY_WME));
    rli->cur_log_fd= -1;
  }

  if (rli->relay_log.reset_logs(session))
  {
    *errmsg = "Failed during log reset";
    error=1;
    goto err;
  }
  /* Save name of used relay log file */
  rli->group_relay_log_name.assign(rli->relay_log.get_log_fname());
  rli->event_relay_log_name.assign(rli->relay_log.get_log_fname());
  rli->group_relay_log_pos= rli->event_relay_log_pos= BIN_LOG_HEADER_SIZE;
  if (count_relay_log_space(rli))
  {
    *errmsg= "Error counting relay log space";
    goto err;
  }
  if (!just_reset)
    error= init_relay_log_pos(rli, rli->group_relay_log_name.c_str(),
                              rli->group_relay_log_pos,
                              0 /* do not need data lock */, errmsg, 0);

err:
  pthread_mutex_unlock(&rli->data_lock);
  return(error);
}


/*
     Check if condition stated in UNTIL clause of START SLAVE is reached.
   SYNOPSYS
     Relay_log_info::is_until_satisfied()
     master_beg_pos    position of the beginning of to be executed event
                       (not log_pos member of the event that points to the
                        beginning of the following event)


   DESCRIPTION
     Checks if UNTIL condition is reached. Uses caching result of last
     comparison of current log file name and target log file name. So cached
     value should be invalidated if current log file name changes
     (see Relay_log_info::notify_... functions).

     This caching is needed to avoid of expensive string comparisons and
     strtol() conversions needed for log names comparison. We don't need to
     compare them each time this function is called, we only need to do this
     when current log name changes. If we have UNTIL_MASTER_POS condition we
     need to do this only after Rotate_log_event::do_apply_event() (which is
     rare, so caching gives real benifit), and if we have UNTIL_RELAY_POS
     condition then we should invalidate cached comarison value after
     inc_group_relay_log_pos() which called for each group of events (so we
     have some benefit if we have something like queries that use
     autoincrement or if we have transactions).

     Should be called ONLY if until_condition != UNTIL_NONE !
   RETURN VALUE
     true - condition met or error happened (condition seems to have
            bad log file name)
     false - condition not met
*/

bool Relay_log_info::is_until_satisfied(my_off_t master_beg_pos)
{
  const char *log_name;
  uint64_t log_pos;

  assert(until_condition != UNTIL_NONE);

  if (until_condition == UNTIL_MASTER_POS)
  {
    log_name= group_master_log_name.c_str();
    log_pos= master_beg_pos;
  }
  else
  { /* until_condition == UNTIL_RELAY_POS */
    log_name= group_relay_log_name.c_str();
    log_pos= group_relay_log_pos;
  }

  if (until_log_names_cmp_result == UNTIL_LOG_NAMES_CMP_UNKNOWN)
  {
    /*
      We have no cached comparison results so we should compare log names
      and cache result.
      If we are after RESET SLAVE, and the SQL slave thread has not processed
      any event yet, it could be that group_master_log_name is "". In that case,
      just wait for more events (as there is no sensible comparison to do).
    */

    if (*log_name)
    {
      const char *basename= log_name + dirname_length(log_name);

      const char *q= (const char*)(fn_ext(basename)+1);
      if (strncmp(basename, until_log_name, (int32_t)(q-basename)) == 0)
      {
        /* Now compare extensions. */
        char *q_end;
        uint32_t log_name_extension= strtoul(q, &q_end, 10);
        if (log_name_extension < until_log_name_extension)
          until_log_names_cmp_result= UNTIL_LOG_NAMES_CMP_LESS;
        else
          until_log_names_cmp_result=
            (log_name_extension > until_log_name_extension) ?
            UNTIL_LOG_NAMES_CMP_GREATER : UNTIL_LOG_NAMES_CMP_EQUAL ;
      }
      else
      {
        /* Probably error so we aborting */
        errmsg_printf(ERRMSG_LVL_ERROR, _("Slave SQL thread is stopped because UNTIL "
                          "condition is bad."));
        return(true);
      }
    }
    else
      return(until_log_pos == 0);
  }

  return(((until_log_names_cmp_result == UNTIL_LOG_NAMES_CMP_EQUAL &&
           log_pos >= until_log_pos) ||
          until_log_names_cmp_result == UNTIL_LOG_NAMES_CMP_GREATER));
}


void Relay_log_info::stmt_done(my_off_t event_master_log_pos,
                                  time_t event_creation_time)
{
  extern uint32_t debug_not_change_ts_if_art_event;
  clear_flag(IN_STMT);

  /*
    If in a transaction, and if the slave supports transactions, just
    inc_event_relay_log_pos(). We only have to check for OPTION_BEGIN
    (not OPTION_NOT_AUTOCOMMIT) as transactions are logged with
    BEGIN/COMMIT, not with SET AUTOCOMMIT= .

    CAUTION: opt_using_transactions means innodb || bdb ; suppose the
    master supports InnoDB and BDB, but the slave supports only BDB,
    problems will arise: - suppose an InnoDB table is created on the
    master, - then it will be MyISAM on the slave - but as
    opt_using_transactions is true, the slave will believe he is
    transactional with the MyISAM table. And problems will come when
    one does START SLAVE; STOP SLAVE; START SLAVE; (the slave will
    resume at BEGIN whereas there has not been any rollback).  This is
    the problem of using opt_using_transactions instead of a finer
    "does the slave support _transactional handler used on the
    master_".

    More generally, we'll have problems when a query mixes a
    transactional handler and MyISAM and STOP SLAVE is issued in the
    middle of the "transaction". START SLAVE will resume at BEGIN
    while the MyISAM table has already been updated.
  */
  if ((sql_session->options & OPTION_BEGIN) && opt_using_transactions)
    inc_event_relay_log_pos();
  else
  {
    inc_group_relay_log_pos(event_master_log_pos);
    flush_relay_log_info(this);
    /*
      Note that Rotate_log_event::do_apply_event() does not call this
      function, so there is no chance that a fake rotate event resets
      last_master_timestamp.  Note that we update without mutex
      (probably ok - except in some very rare cases, only consequence
      is that value may take some time to display in
      Seconds_Behind_Master - not critical).
    */
    if (!(event_creation_time == 0 && debug_not_change_ts_if_art_event > 0))
      last_master_timestamp= event_creation_time;
  }
}

void Relay_log_info::cleanup_context(Session *session, bool error)
{
  assert(sql_session == session);
  /*
    1) Instances of Table_map_log_event, if ::do_apply_event() was called on them,
    may have opened tables, which we cannot be sure have been closed (because
    maybe the Rows_log_event have not been found or will not be, because slave
    SQL thread is stopping, or relay log has a missing tail etc). So we close
    all thread's tables. And so the table mappings have to be cancelled.
    2) Rows_log_event::do_apply_event() may even have started statements or
    transactions on them, which we need to rollback in case of error.
    3) If finding a Format_description_log_event after a BEGIN, we also need
    to rollback before continuing with the next events.
    4) so we need this "context cleanup" function.
  */
  if (error)
  {
    ha_autocommit_or_rollback(session, 1); // if a "statement transaction"
    end_trans(session, ROLLBACK); // if a "real transaction"
  }
  m_table_map.clear_tables();
  close_thread_tables(session);
  clear_tables_to_lock();
  clear_flag(IN_STMT);
  /*
    Cleanup for the flags that have been set at do_apply_event.
  */
  session->options&= ~OPTION_NO_FOREIGN_KEY_CHECKS;
  session->options&= ~OPTION_RELAXED_UNIQUE_CHECKS;
  last_event_start_time= 0;
  return;
}


bool Relay_log_info::is_in_group() const {
  return (sql_session->options & OPTION_BEGIN) ||
      (m_flags & (1UL << IN_STMT));
}


void Relay_log_info::clear_tables_to_lock()
{
  while (tables_to_lock)
  {
    unsigned char* to_free= reinterpret_cast<unsigned char*>(tables_to_lock);
    if (tables_to_lock->m_tabledef_valid)
    {
      tables_to_lock->m_tabledef.table_def::~table_def();
      tables_to_lock->m_tabledef_valid= false;
    }
    tables_to_lock=
      static_cast<RPL_TableList*>(tables_to_lock->next_global);
    tables_to_lock_count--;
    free(to_free);
  }
  assert(tables_to_lock == NULL && tables_to_lock_count == 0);
}
