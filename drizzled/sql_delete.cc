/* Copyright (C) 2000 MySQL AB

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

/*
  Delete of records and truncate of tables.

  Multi-table deletes were introduced by Monty and Sinisa
*/
#include <drizzled/server_includes.h>
#include <drizzled/sql_select.h>
#include <drizzled/drizzled_error_messages.h>

/**
  Implement DELETE SQL word.

  @note Like implementations of other DDL/DML in MySQL, this function
  relies on the caller to close the thread tables. This is done in the
  end of dispatch_command().
*/

bool mysql_delete(THD *thd, TABLE_LIST *table_list, COND *conds,
                  SQL_LIST *order, ha_rows limit, uint64_t options,
                  bool reset_auto_increment)
{
  bool          will_batch;
  int		error, loc_error;
  Table		*table;
  SQL_SELECT	*select=0;
  READ_RECORD	info;
  bool          using_limit=limit != HA_POS_ERROR;
  bool		transactional_table, safe_update, const_cond;
  bool          const_cond_result;
  ha_rows	deleted= 0;
  uint usable_index= MAX_KEY;
  SELECT_LEX   *select_lex= &thd->lex->select_lex;
  THD::killed_state killed_status= THD::NOT_KILLED;
  

  if (open_and_lock_tables(thd, table_list))
    return(true);
  /* TODO look at this error */
  if (!(table= table_list->table))
  {
    my_error(ER_VIEW_DELETE_MERGE_VIEW, MYF(0), "", "");
    return(true);
  }
  thd_proc_info(thd, "init");
  table->map=1;

  if (mysql_prepare_delete(thd, table_list, &conds))
    goto err;

  /* check ORDER BY even if it can be ignored */
  if (order && order->elements)
  {
    TABLE_LIST   tables;
    List<Item>   fields;
    List<Item>   all_fields;

    memset(&tables, 0, sizeof(tables));
    tables.table = table;
    tables.alias = table_list->alias;

      if (select_lex->setup_ref_array(thd, order->elements) ||
	  setup_order(thd, select_lex->ref_pointer_array, &tables,
                    fields, all_fields, (ORDER*) order->first))
    {
      delete select;
      free_underlaid_joins(thd, &thd->lex->select_lex);
      goto err;
    }
  }

  const_cond= (!conds || conds->const_item());
  safe_update=test(thd->options & OPTION_SAFE_UPDATES);
  if (safe_update && const_cond)
  {
    my_message(ER_UPDATE_WITHOUT_KEY_IN_SAFE_MODE,
               ER(ER_UPDATE_WITHOUT_KEY_IN_SAFE_MODE), MYF(0));
    goto err;
  }

  select_lex->no_error= thd->lex->ignore;

  const_cond_result= const_cond && (!conds || conds->val_int());
  if (thd->is_error())
  {
    /* Error evaluating val_int(). */
    return(true);
  }

  /*
    Test if the user wants to delete all rows and deletion doesn't have
    any side-effects (because of triggers), so we can use optimized
    handler::delete_all_rows() method.

    We implement fast TRUNCATE for InnoDB even if triggers are
    present.  TRUNCATE ignores triggers.

    We can use delete_all_rows() if and only if:
    - We allow new functions (not using option --skip-new), and are
      not in safe mode (not using option --safe-mode)
    - There is no limit clause
    - The condition is constant
    - If there is a condition, then it it produces a non-zero value
    - If the current command is DELETE FROM with no where clause
      (i.e., not TRUNCATE) then:
      - We should not be binlogging this statement row-based, and
      - there should be no delete triggers associated with the table.
  */
  if (!using_limit && const_cond_result &&
      !(specialflag & (SPECIAL_NO_NEW_FUNC | SPECIAL_SAFE_MODE)) &&
      (thd->lex->sql_command == SQLCOM_TRUNCATE ||
       (!thd->current_stmt_binlog_row_based)))
  {
    /* Update the table->file->stats.records number */
    table->file->info(HA_STATUS_VARIABLE | HA_STATUS_NO_LOCK);
    ha_rows const maybe_deleted= table->file->stats.records;
    if (!(error=table->file->ha_delete_all_rows()))
    {
      error= -1;				// ok
      deleted= maybe_deleted;
      goto cleanup;
    }
    if (error != HA_ERR_WRONG_COMMAND)
    {
      table->file->print_error(error,MYF(0));
      error=0;
      goto cleanup;
    }
    /* Handler didn't support fast delete; Delete rows one by one */
  }
  if (conds)
  {
    Item::cond_result result;
    conds= remove_eq_conds(thd, conds, &result);
    if (result == Item::COND_FALSE)             // Impossible where
      limit= 0;
  }

  /* Update the table->file->stats.records number */
  table->file->info(HA_STATUS_VARIABLE | HA_STATUS_NO_LOCK);

  table->covering_keys.clear_all();
  table->quick_keys.clear_all();		// Can't use 'only index'
  select=make_select(table, 0, 0, conds, 0, &error);
  if (error)
    goto err;
  if ((select && select->check_quick(thd, safe_update, limit)) || !limit)
  {
    delete select;
    free_underlaid_joins(thd, select_lex);
    thd->row_count_func= 0;
    DRIZZLE_DELETE_END();
    my_ok(thd, (ha_rows) thd->row_count_func);
    /*
      We don't need to call reset_auto_increment in this case, because
      mysql_truncate always gives a NULL conds argument, hence we never
      get here.
    */
    return(0);				// Nothing to delete
  }

  /* If running in safe sql mode, don't allow updates without keys */
  if (table->quick_keys.is_clear_all())
  {
    thd->server_status|=SERVER_QUERY_NO_INDEX_USED;
    if (safe_update && !using_limit)
    {
      delete select;
      free_underlaid_joins(thd, select_lex);
      my_message(ER_UPDATE_WITHOUT_KEY_IN_SAFE_MODE,
                 ER(ER_UPDATE_WITHOUT_KEY_IN_SAFE_MODE), MYF(0));
      goto err;
    }
  }
  if (options & OPTION_QUICK)
    (void) table->file->extra(HA_EXTRA_QUICK);

  if (order && order->elements)
  {
    uint         length= 0;
    SORT_FIELD  *sortorder;
    ha_rows examined_rows;
    
    if ((!select || table->quick_keys.is_clear_all()) && limit != HA_POS_ERROR)
      usable_index= get_index_for_order(table, (ORDER*)(order->first), limit);

    if (usable_index == MAX_KEY)
    {
      table->sort.io_cache= (IO_CACHE *) my_malloc(sizeof(IO_CACHE),
                                                   MYF(MY_FAE | MY_ZEROFILL));
    
      if (!(sortorder= make_unireg_sortorder((ORDER*) order->first,
                                             &length, NULL)) ||
	  (table->sort.found_records = filesort(thd, table, sortorder, length,
                                                select, HA_POS_ERROR, 1,
                                                &examined_rows))
	  == HA_POS_ERROR)
      {
        delete select;
        free_underlaid_joins(thd, &thd->lex->select_lex);
        goto err;
      }
      /*
        Filesort has already found and selected the rows we want to delete,
        so we don't need the where clause
      */
      delete select;
      free_underlaid_joins(thd, select_lex);
      select= 0;
    }
  }

  /* If quick select is used, initialize it before retrieving rows. */
  if (select && select->quick && select->quick->reset())
  {
    delete select;
    free_underlaid_joins(thd, select_lex);
    goto err;
  }
  if (usable_index==MAX_KEY)
    init_read_record(&info,thd,table,select,1,1);
  else
    init_read_record_idx(&info, thd, table, 1, usable_index);

  thd_proc_info(thd, "updating");

  will_batch= !table->file->start_bulk_delete();


  table->mark_columns_needed_for_delete();

  while (!(error=info.read_record(&info)) && !thd->killed &&
	 ! thd->is_error())
  {
    // thd->is_error() is tested to disallow delete row on error
    if (!(select && select->skip_record())&& ! thd->is_error() )
    {
      if (!(error= table->file->ha_delete_row(table->record[0])))
      {
	deleted++;
	if (!--limit && using_limit)
	{
	  error= -1;
	  break;
	}
      }
      else
      {
	table->file->print_error(error,MYF(0));
	/*
	  In < 4.0.14 we set the error number to 0 here, but that
	  was not sensible, because then MySQL would not roll back the
	  failed DELETE, and also wrote it to the binlog. For MyISAM
	  tables a DELETE probably never should fail (?), but for
	  InnoDB it can fail in a FOREIGN KEY error or an
	  out-of-tablespace error.
	*/
 	error= 1;
	break;
      }
    }
    else
      table->file->unlock_row();  // Row failed selection, release lock on it
  }
  killed_status= thd->killed;
  if (killed_status != THD::NOT_KILLED || thd->is_error())
    error= 1;					// Aborted
  if (will_batch && (loc_error= table->file->end_bulk_delete()))
  {
    if (error != 1)
      table->file->print_error(loc_error,MYF(0));
    error=1;
  }
  thd_proc_info(thd, "end");
  end_read_record(&info);
  if (options & OPTION_QUICK)
    (void) table->file->extra(HA_EXTRA_NORMAL);

  if (reset_auto_increment && (error < 0))
  {
    /*
      We're really doing a truncate and need to reset the table's
      auto-increment counter.
    */
    int error2= table->file->ha_reset_auto_increment(0);

    if (error2 && (error2 != HA_ERR_WRONG_COMMAND))
    {
      table->file->print_error(error2, MYF(0));
      error= 1;
    }
  }

cleanup:

  delete select;
  transactional_table= table->file->has_transactions();

  if (!transactional_table && deleted > 0)
    thd->transaction.stmt.modified_non_trans_table= true;
  
  /* See similar binlogging code in sql_update.cc, for comments */
  if ((error < 0) || thd->transaction.stmt.modified_non_trans_table)
  {
    if (mysql_bin_log.is_open())
    {
      if (error < 0)
        thd->clear_error();
      /*
        [binlog]: If 'handler::delete_all_rows()' was called and the
        storage engine does not inject the rows itself, we replicate
        statement-based; otherwise, 'ha_delete_row()' was used to
        delete specific rows which we might log row-based.
      */
      int log_result= thd->binlog_query(THD::ROW_QUERY_TYPE,
                                        thd->query, thd->query_length,
                                        transactional_table, false, killed_status);

      if (log_result && transactional_table)
      {
	error=1;
      }
    }
    if (thd->transaction.stmt.modified_non_trans_table)
      thd->transaction.all.modified_non_trans_table= true;
  }
  assert(transactional_table || !deleted || thd->transaction.stmt.modified_non_trans_table);
  free_underlaid_joins(thd, select_lex);

  DRIZZLE_DELETE_END();
  if (error < 0 || (thd->lex->ignore && !thd->is_fatal_error))
  {
    thd->row_count_func= deleted;
    my_ok(thd, (ha_rows) thd->row_count_func);
  }
  return(error >= 0 || thd->is_error());

err:
  DRIZZLE_DELETE_END();
  return(true);
}


/*
  Prepare items in DELETE statement

  SYNOPSIS
    mysql_prepare_delete()
    thd			- thread handler
    table_list		- global/local table list
    conds		- conditions

  RETURN VALUE
    false OK
    true  error
*/
int mysql_prepare_delete(THD *thd, TABLE_LIST *table_list, Item **conds)
{
  SELECT_LEX *select_lex= &thd->lex->select_lex;
  
  List<Item> all_fields;

  /*
    Statement-based replication of DELETE ... LIMIT is not safe as order of
    rows is not defined, so in mixed mode we go to row-based.

    Note that we may consider a statement as safe if ORDER BY primary_key
    is present. However it may confuse users to see very similiar statements
    replicated differently.
  */
  if (thd->lex->current_select->select_limit)
  {
    thd->lex->set_stmt_unsafe();
    thd->set_current_stmt_binlog_row_based_if_mixed();
  }
  thd->lex->allow_sum_func= 0;
  if (setup_tables_and_check_access(thd, &thd->lex->select_lex.context,
                                    &thd->lex->select_lex.top_join_list,
                                    table_list, 
                                    &select_lex->leaf_tables, false) ||
      setup_conds(thd, table_list, select_lex->leaf_tables, conds))
    return(true);
  {
    TABLE_LIST *duplicate;
    if ((duplicate= unique_table(thd, table_list, table_list->next_global, 0)))
    {
      update_non_unique_table_error(table_list, "DELETE", duplicate);
      return(true);
    }
  }

  if (select_lex->inner_refs_list.elements &&
    fix_inner_refs(thd, all_fields, select_lex, select_lex->ref_pointer_array))
    return(-1);

  return(false);
}


/***************************************************************************
  Delete multiple tables from join 
***************************************************************************/

#define MEM_STRIP_BUF_SIZE current_thd->variables.sortbuff_size

extern "C" int refpos_order_cmp(void* arg, const void *a,const void *b)
{
  handler *file= (handler*)arg;
  return file->cmp_ref((const uchar*)a, (const uchar*)b);
}

/*
  make delete specific preparation and checks after opening tables

  SYNOPSIS
    mysql_multi_delete_prepare()
    thd         thread handler

  RETURN
    false OK
    true  Error
*/

int mysql_multi_delete_prepare(THD *thd)
{
  LEX *lex= thd->lex;
  TABLE_LIST *aux_tables= (TABLE_LIST *)lex->auxiliary_table_list.first;
  TABLE_LIST *target_tbl;
  

  /*
    setup_tables() need for VIEWs. JOIN::prepare() will not do it second
    time.

    lex->query_tables also point on local list of DELETE SELECT_LEX
  */
  if (setup_tables_and_check_access(thd, &thd->lex->select_lex.context,
                                    &thd->lex->select_lex.top_join_list,
                                    lex->query_tables,
                                    &lex->select_lex.leaf_tables, false))
    return(true);


  /*
    Multi-delete can't be constructed over-union => we always have
    single SELECT on top and have to check underlying SELECTs of it
  */
  lex->select_lex.exclude_from_table_unique_test= true;
  /* Fix tables-to-be-deleted-from list to point at opened tables */
  for (target_tbl= (TABLE_LIST*) aux_tables;
       target_tbl;
       target_tbl= target_tbl->next_local)
  {
    if (!(target_tbl->table= target_tbl->correspondent_table->table))
    {
      assert(target_tbl->correspondent_table->merge_underlying_list &&
                  target_tbl->correspondent_table->merge_underlying_list->
                  next_local);
      my_error(ER_VIEW_DELETE_MERGE_VIEW, MYF(0), "", "");
      return(true);
    }

    /*
      Check that table from which we delete is not used somewhere
      inside subqueries/view.
    */
    {
      TABLE_LIST *duplicate;
      if ((duplicate= unique_table(thd, target_tbl->correspondent_table,
                                   lex->query_tables, 0)))
      {
        update_non_unique_table_error(target_tbl->correspondent_table,
                                      "DELETE", duplicate);
        return(true);
      }
    }
  }
  return(false);
}


multi_delete::multi_delete(TABLE_LIST *dt, uint num_of_tables_arg)
  : delete_tables(dt), deleted(0), found(0),
    num_of_tables(num_of_tables_arg), error(0),
    do_delete(0), transactional_tables(0), normal_tables(0), error_handled(0)
{
  tempfiles= (Unique **) sql_calloc(sizeof(Unique *) * num_of_tables);
}


int
multi_delete::prepare(List<Item> &values __attribute__((unused)),
                      SELECT_LEX_UNIT *u)
{
  
  unit= u;
  do_delete= 1;
  thd_proc_info(thd, "deleting from main table");
  return(0);
}


bool
multi_delete::initialize_tables(JOIN *join)
{
  TABLE_LIST *walk;
  Unique **tempfiles_ptr;
  

  if ((thd->options & OPTION_SAFE_UPDATES) && error_if_full_join(join))
    return(1);

  table_map tables_to_delete_from=0;
  for (walk= delete_tables; walk; walk= walk->next_local)
    tables_to_delete_from|= walk->table->map;

  walk= delete_tables;
  delete_while_scanning= 1;
  for (JOIN_TAB *tab=join->join_tab, *end=join->join_tab+join->tables;
       tab < end;
       tab++)
  {
    if (tab->table->map & tables_to_delete_from)
    {
      /* We are going to delete from this table */
      Table *tbl=walk->table=tab->table;
      walk= walk->next_local;
      /* Don't use KEYREAD optimization on this table */
      tbl->no_keyread=1;
      /* Don't use record cache */
      tbl->no_cache= 1;
      tbl->covering_keys.clear_all();
      if (tbl->file->has_transactions())
	transactional_tables= 1;
      else
	normal_tables= 1;
      tbl->prepare_for_position();
      tbl->mark_columns_needed_for_delete();
    }
    else if ((tab->type != JT_SYSTEM && tab->type != JT_CONST) &&
             walk == delete_tables)
    {
      /*
        We are not deleting from the table we are scanning. In this
        case send_data() shouldn't delete any rows a we may touch
        the rows in the deleted table many times
      */
      delete_while_scanning= 0;
    }
  }
  walk= delete_tables;
  tempfiles_ptr= tempfiles;
  if (delete_while_scanning)
  {
    table_being_deleted= delete_tables;
    walk= walk->next_local;
  }
  for (;walk ;walk= walk->next_local)
  {
    Table *table=walk->table;
    *tempfiles_ptr++= new Unique (refpos_order_cmp,
				  (void *) table->file,
				  table->file->ref_length,
				  MEM_STRIP_BUF_SIZE);
  }
  return(thd->is_fatal_error != 0);
}


multi_delete::~multi_delete()
{
  for (table_being_deleted= delete_tables;
       table_being_deleted;
       table_being_deleted= table_being_deleted->next_local)
  {
    Table *table= table_being_deleted->table;
    table->no_keyread=0;
  }

  for (uint counter= 0; counter < num_of_tables; counter++)
  {
    if (tempfiles[counter])
      delete tempfiles[counter];
  }
}


bool multi_delete::send_data(List<Item> &values __attribute__((unused)))
{
  int secure_counter= delete_while_scanning ? -1 : 0;
  TABLE_LIST *del_table;
  

  for (del_table= delete_tables;
       del_table;
       del_table= del_table->next_local, secure_counter++)
  {
    Table *table= del_table->table;

    /* Check if we are using outer join and we didn't find the row */
    if (table->status & (STATUS_NULL_ROW | STATUS_DELETED))
      continue;

    table->file->position(table->record[0]);
    found++;

    if (secure_counter < 0)
    {
      /* We are scanning the current table */
      assert(del_table == table_being_deleted);
      table->status|= STATUS_DELETED;
      if (!(error=table->file->ha_delete_row(table->record[0])))
      {
        deleted++;
        if (!table->file->has_transactions())
          thd->transaction.stmt.modified_non_trans_table= true;
      }
      else
      {
        table->file->print_error(error,MYF(0));
        return(1);
      }
    }
    else
    {
      error=tempfiles[secure_counter]->unique_add((char*) table->file->ref);
      if (error)
      {
	error= 1;                               // Fatal error
	return(1);
      }
    }
  }
  return(0);
}


void multi_delete::send_error(uint errcode,const char *err)
{
  

  /* First send error what ever it is ... */
  my_message(errcode, err, MYF(0));

  return;
}


void multi_delete::abort()
{
  

  /* the error was handled or nothing deleted and no side effects return */
  if (error_handled ||
      (!thd->transaction.stmt.modified_non_trans_table && !deleted))
    return;

  /*
    If rows from the first table only has been deleted and it is
    transactional, just do rollback.
    The same if all tables are transactional, regardless of where we are.
    In all other cases do attempt deletes ...
  */
  if (do_delete && normal_tables &&
      (table_being_deleted != delete_tables ||
       !table_being_deleted->table->file->has_transactions()))
  {
    /*
      We have to execute the recorded do_deletes() and write info into the
      error log
    */
    error= 1;
    send_eof();
    assert(error_handled);
    return;
  }
  
  if (thd->transaction.stmt.modified_non_trans_table)
  {
    /* 
       there is only side effects; to binlog with the error
    */
    if (mysql_bin_log.is_open())
    {
      thd->binlog_query(THD::ROW_QUERY_TYPE,
                        thd->query, thd->query_length,
                        transactional_tables, false);
    }
    thd->transaction.all.modified_non_trans_table= true;
  }
  return;
}



/*
  Do delete from other tables.
  Returns values:
	0 ok
	1 error
*/

int multi_delete::do_deletes()
{
  int local_error= 0, counter= 0, tmp_error;
  bool will_batch;
  
  assert(do_delete);

  do_delete= 0;                                 // Mark called
  if (!found)
    return(0);

  table_being_deleted= (delete_while_scanning ? delete_tables->next_local :
                        delete_tables);
 
  for (; table_being_deleted;
       table_being_deleted= table_being_deleted->next_local, counter++)
  { 
    ha_rows last_deleted= deleted;
    Table *table = table_being_deleted->table;
    if (tempfiles[counter]->get(table))
    {
      local_error=1;
      break;
    }

    READ_RECORD	info;
    init_read_record(&info,thd,table,NULL,0,1);
    /*
      Ignore any rows not found in reference tables as they may already have
      been deleted by foreign key handling
    */
    info.ignore_not_found_rows= 1;
    will_batch= !table->file->start_bulk_delete();
    while (!(local_error=info.read_record(&info)) && !thd->killed)
    {
      if ((local_error=table->file->ha_delete_row(table->record[0])))
      {
	table->file->print_error(local_error,MYF(0));
	break;
      }
      deleted++;
    }
    if (will_batch && (tmp_error= table->file->end_bulk_delete()))
    {
      if (!local_error)
      {
        local_error= tmp_error;
        table->file->print_error(local_error,MYF(0));
      }
    }
    if (last_deleted != deleted && !table->file->has_transactions())
      thd->transaction.stmt.modified_non_trans_table= true;
    end_read_record(&info);
    if (thd->killed && !local_error)
      local_error= 1;
    if (local_error == -1)				// End of file
      local_error = 0;
  }
  return(local_error);
}


/*
  Send ok to the client

  return:  0 sucess
	   1 error
*/

bool multi_delete::send_eof()
{
  THD::killed_state killed_status= THD::NOT_KILLED;
  thd_proc_info(thd, "deleting from reference tables");

  /* Does deletes for the last n - 1 tables, returns 0 if ok */
  int local_error= do_deletes();		// returns 0 if success

  /* compute a total error to know if something failed */
  local_error= local_error || error;
  killed_status= (local_error == 0)? THD::NOT_KILLED : thd->killed;
  /* reset used flags */
  thd_proc_info(thd, "end");

  if ((local_error == 0) || thd->transaction.stmt.modified_non_trans_table)
  {
    if (mysql_bin_log.is_open())
    {
      if (local_error == 0)
        thd->clear_error();
      if (thd->binlog_query(THD::ROW_QUERY_TYPE,
                            thd->query, thd->query_length,
                            transactional_tables, false, killed_status) &&
          !normal_tables)
      {
	local_error=1;  // Log write failed: roll back the SQL statement
      }
    }
    if (thd->transaction.stmt.modified_non_trans_table)
      thd->transaction.all.modified_non_trans_table= true;
  }
  if (local_error != 0)
    error_handled= true; // to force early leave from ::send_error()

  if (!local_error)
  {
    thd->row_count_func= deleted;
    ::my_ok(thd, (ha_rows) thd->row_count_func);
  }
  return 0;
}


/***************************************************************************
  TRUNCATE Table
****************************************************************************/

/*
  Optimize delete of all rows by doing a full generate of the table
  This will work even if the .ISM and .ISD tables are destroyed

  dont_send_ok should be set if:
  - We should always wants to generate the table (even if the table type
    normally can't safely do this.
  - We don't want an ok to be sent to the end user.
  - We don't want to log the truncate command
  - If we want to have a name lock on the table on exit without errors.
*/

bool mysql_truncate(THD *thd, TABLE_LIST *table_list, bool dont_send_ok)
{
  HA_CREATE_INFO create_info;
  char path[FN_REFLEN];
  Table *table;
  bool error;
  uint path_length;
  

  memset(&create_info, 0, sizeof(create_info));
  /* If it is a temporary table, close and regenerate it */
  if (!dont_send_ok && (table= find_temporary_table(thd, table_list)))
  {
    handlerton *table_type= table->s->db_type();
    TABLE_SHARE *share= table->s;
    bool frm_only= (share->tmp_table == TMP_TABLE_FRM_FILE_ONLY);

    if (!ha_check_storage_engine_flag(table_type, HTON_CAN_RECREATE))
      goto trunc_by_del;

    table->file->info(HA_STATUS_AUTO | HA_STATUS_NO_LOCK);
    
    close_temporary_table(thd, table, 0, 0);    // Don't free share
    ha_create_table(thd, share->normalized_path.str,
                    share->db.str, share->table_name.str, &create_info, 1);
    // We don't need to call invalidate() because this table is not in cache
    if ((error= (int) !(open_temporary_table(thd, share->path.str,
                                             share->db.str,
					     share->table_name.str, 1,
                                             OTM_OPEN))))
      (void) rm_temporary_table(table_type, path, frm_only);
    free_table_share(share);
    my_free((char*) table,MYF(0));
    /*
      If we return here we will not have logged the truncation to the bin log
      and we will not my_ok() to the client.
    */
    goto end;
  }

  path_length= build_table_filename(path, sizeof(path), table_list->db,
                                    table_list->table_name, reg_ext, 0);

  if (!dont_send_ok)
  {
    enum legacy_db_type table_type;
    mysql_frm_type(thd, path, &table_type);
    if (table_type == DB_TYPE_UNKNOWN)
    {
      my_error(ER_NO_SUCH_TABLE, MYF(0),
               table_list->db, table_list->table_name);
      return(true);
    }

    if (!ha_check_storage_engine_flag(ha_resolve_by_legacy_type(thd, table_type),
                                      HTON_CAN_RECREATE))
      goto trunc_by_del;

    if (lock_and_wait_for_table_name(thd, table_list))
      return(true);
  }

  // Remove the .frm extension AIX 5.2 64-bit compiler bug (BUG#16155): this
  // crashes, replacement works.  *(path + path_length - reg_ext_length)=
  // '\0';
  path[path_length - reg_ext_length] = 0;
  VOID(pthread_mutex_lock(&LOCK_open));
  error= ha_create_table(thd, path, table_list->db, table_list->table_name,
                         &create_info, 1);
  VOID(pthread_mutex_unlock(&LOCK_open));

end:
  if (!dont_send_ok)
  {
    if (!error)
    {
      /*
        TRUNCATE must always be statement-based binlogged (not row-based) so
        we don't test current_stmt_binlog_row_based.
      */
      write_bin_log(thd, true, thd->query, thd->query_length);
      my_ok(thd);		// This should return record count
    }
    VOID(pthread_mutex_lock(&LOCK_open));
    unlock_table_name(thd, table_list);
    VOID(pthread_mutex_unlock(&LOCK_open));
  }
  else if (error)
  {
    VOID(pthread_mutex_lock(&LOCK_open));
    unlock_table_name(thd, table_list);
    VOID(pthread_mutex_unlock(&LOCK_open));
  }
  return(error);

trunc_by_del:
  /* Probably InnoDB table */
  uint64_t save_options= thd->options;
  table_list->lock_type= TL_WRITE;
  thd->options&= ~(OPTION_BEGIN | OPTION_NOT_AUTOCOMMIT);
  ha_enable_transaction(thd, false);
  mysql_init_select(thd->lex);
  bool save_binlog_row_based= thd->current_stmt_binlog_row_based;
  thd->clear_current_stmt_binlog_row_based();
  error= mysql_delete(thd, table_list, (COND*) 0, (SQL_LIST*) 0,
                      HA_POS_ERROR, 0LL, true);
  ha_enable_transaction(thd, true);
  /*
    Safety, in case the engine ignored ha_enable_transaction(false)
    above. Also clears thd->transaction.*.
  */
  error= ha_autocommit_or_rollback(thd, error);
  ha_commit(thd);
  thd->options= save_options;
  thd->current_stmt_binlog_row_based= save_binlog_row_based;
  return(error);
}
