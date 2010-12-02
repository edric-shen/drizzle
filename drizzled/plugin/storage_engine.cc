/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#include <fcntl.h>
#include <unistd.h>

#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <algorithm>
#include <functional>

#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "drizzled/cached_directory.h"

#include <drizzled/definitions.h>
#include <drizzled/base.h>
#include <drizzled/cursor.h>
#include <drizzled/plugin/storage_engine.h>
#include <drizzled/session.h>
#include <drizzled/error.h>
#include <drizzled/gettext.h>
#include <drizzled/unireg.h>
#include <drizzled/data_home.h>
#include "drizzled/errmsg_print.h"
#include "drizzled/xid.h"
#include "drizzled/sql_table.h"
#include "drizzled/global_charset_info.h"
#include "drizzled/charset.h"
#include "drizzled/internal/my_sys.h"
#include "drizzled/db.h"

#include <drizzled/table_proto.h>
#include <drizzled/plugin/event_observer.h>

#include <drizzled/table/shell.h>

#include "drizzled/message/cache.h"

#include <boost/algorithm/string/compare.hpp>

static bool shutdown_has_begun= false; // Once we put in the container for the vector/etc for engines this will go away.

using namespace std;

namespace drizzled
{

namespace plugin
{

static EngineVector vector_of_engines;
static EngineVector vector_of_schema_engines;

const std::string DEFAULT_STRING("default");
const std::string UNKNOWN_STRING("UNKNOWN");
const std::string DEFAULT_DEFINITION_FILE_EXT(".dfe");

static std::set<std::string> set_of_table_definition_ext;

EngineVector &StorageEngine::getSchemaEngines()
{
  return vector_of_schema_engines;
}

StorageEngine::StorageEngine(const string name_arg,
                             const bitset<HTON_BIT_SIZE> &flags_arg) :
  Plugin(name_arg, "StorageEngine"),
  MonitoredInTransaction(), /* This gives the storage engine a "slot" or ID */
  flags(flags_arg)
{
}

StorageEngine::~StorageEngine()
{
}

void StorageEngine::setTransactionReadWrite(Session& session)
{
  TransactionContext &statement_ctx= session.transaction.stmt;
  statement_ctx.markModifiedNonTransData();
}


int StorageEngine::renameTable(Session &session, const TableIdentifier &from, const TableIdentifier &to)
{
  int error;
  setTransactionReadWrite(session);

  if (unlikely(plugin::EventObserver::beforeRenameTable(session, from, to)))
  {
    error= ER_EVENT_OBSERVER_PLUGIN;
  }
  else
  {
    error =  doRenameTable(session, from, to);
    if (unlikely(plugin::EventObserver::afterRenameTable(session, from, to, error)))
    {
      error= ER_EVENT_OBSERVER_PLUGIN;
    }
  }
  
  return error;
}

/**
  Delete all files with extension from bas_ext().

  @param name		Base name of table

  @note
    We assume that the Cursor may return more extensions than
    was actually used for the file.

  @retval
    0   If we successfully deleted at least one file from base_ext and
    didn't get any other errors than ENOENT
  @retval
    !0  Error
*/
int StorageEngine::doDropTable(Session&, const TableIdentifier &identifier)
                               
{
  int error= 0;
  int enoent_or_zero= ENOENT;                   // Error if no file was deleted
  char buff[FN_REFLEN];

  for (const char **ext= bas_ext(); *ext ; ext++)
  {
    internal::fn_format(buff, identifier.getPath().c_str(), "", *ext,
                        MY_UNPACK_FILENAME|MY_APPEND_EXT);
    if (internal::my_delete_with_symlink(buff, MYF(0)))
    {
      if ((error= errno) != ENOENT)
        break;
    }
    else
    {
      enoent_or_zero= 0;                        // No error for ENOENT
    }

    error= enoent_or_zero;
  }
  return error;
}

bool StorageEngine::addPlugin(StorageEngine *engine)
{

  vector_of_engines.push_back(engine);

  if (engine->getTableDefinitionFileExtension().length())
  {
    assert(engine->getTableDefinitionFileExtension().length() == DEFAULT_DEFINITION_FILE_EXT.length());
    set_of_table_definition_ext.insert(engine->getTableDefinitionFileExtension());
  }

  if (engine->check_flag(HTON_BIT_SCHEMA_DICTIONARY))
    vector_of_schema_engines.push_back(engine);

  return false;
}

void StorageEngine::removePlugin(StorageEngine *)
{
  if (shutdown_has_begun == false)
  {
    vector_of_engines.clear();
    vector_of_schema_engines.clear();

    shutdown_has_begun= true;
  }
}

class FindEngineByName
  : public unary_function<StorageEngine *, bool>
{
  const string &predicate;

public:
  explicit FindEngineByName(const string &target_arg) :
    predicate(target_arg)
  {
  }

  result_type operator() (argument_type engine)
  {
    return boost::iequals(engine->getName(), predicate);
  }
};

StorageEngine *StorageEngine::findByName(const string &predicate)
{
  EngineVector::iterator iter= find_if(vector_of_engines.begin(),
                                       vector_of_engines.end(),
                                       FindEngineByName(predicate));
  if (iter != vector_of_engines.end())
  {
    StorageEngine *engine= *iter;
    if (engine->is_user_selectable())
      return engine;
  }

  return NULL;
}

StorageEngine *StorageEngine::findByName(Session& session, const string &predicate)
{
  if (boost::iequals(predicate, DEFAULT_STRING))
    return session.getDefaultStorageEngine();

  EngineVector::iterator iter= find_if(vector_of_engines.begin(),
                                       vector_of_engines.end(),
                                       FindEngineByName(predicate));
  if (iter != vector_of_engines.end())
  {
    StorageEngine *engine= *iter;
    if (engine->is_user_selectable())
      return engine;
  }

  return NULL;
}

class StorageEngineCloseConnection : public unary_function<StorageEngine *, void>
{
  Session *session;
public:
  StorageEngineCloseConnection(Session *session_arg) : session(session_arg) {}
  /*
    there's no need to rollback here as all transactions must
    be rolled back already
  */
  inline result_type operator() (argument_type engine)
  {
    if (*session->getEngineData(engine))
      engine->close_connection(session);
  }
};

/**
  @note
    don't bother to rollback here, it's done already
*/
void StorageEngine::closeConnection(Session* session)
{
  for_each(vector_of_engines.begin(), vector_of_engines.end(),
           StorageEngineCloseConnection(session));
}

bool StorageEngine::flushLogs(StorageEngine *engine)
{
  if (engine == NULL)
  {
    if (find_if(vector_of_engines.begin(), vector_of_engines.end(),
                mem_fun(&StorageEngine::flush_logs))
        != vector_of_engines.begin())
      return true;
  }
  else
  {
    if (engine->flush_logs())
      return true;
  }
  return false;
}

class StorageEngineGetTableDefinition: public unary_function<StorageEngine *,bool>
{
  Session& session;
  const TableIdentifier &identifier;
  message::Table &table_message;
  int &err;

public:
  StorageEngineGetTableDefinition(Session& session_arg,
                                  const TableIdentifier &identifier_arg,
                                  message::Table &table_message_arg,
                                  int &err_arg) :
    session(session_arg), 
    identifier(identifier_arg),
    table_message(table_message_arg), 
    err(err_arg) {}

  result_type operator() (argument_type engine)
  {
    int ret= engine->doGetTableDefinition(session, identifier, table_message);

    if (ret != ENOENT)
      err= ret;

    return err == EEXIST || err != ENOENT;
  }
};

class StorageEngineDoesTableExist: public unary_function<StorageEngine *, bool>
{
  Session& session;
  const TableIdentifier &identifier;

public:
  StorageEngineDoesTableExist(Session& session_arg, const TableIdentifier &identifier_arg) :
    session(session_arg), 
    identifier(identifier_arg) 
  { }

  result_type operator() (argument_type engine)
  {
    return engine->doDoesTableExist(session, identifier);
  }
};

/**
  Utility method which hides some of the details of getTableDefinition()
*/
bool plugin::StorageEngine::doesTableExist(Session &session,
                                           const TableIdentifier &identifier,
                                           bool include_temporary_tables)
{
  if (include_temporary_tables)
  {
    if (session.doDoesTableExist(identifier))
      return true;
  }

  EngineVector::iterator iter=
    find_if(vector_of_engines.begin(), vector_of_engines.end(),
            StorageEngineDoesTableExist(session, identifier));

  if (iter == vector_of_engines.end())
  {
    return false;
  }

  return true;
}

bool plugin::StorageEngine::doDoesTableExist(Session&, const drizzled::TableIdentifier&)
{
  cerr << " Engine was called for doDoesTableExist() and does not implement it: " << this->getName() << "\n";
  assert(0);
  return false;
}

/**
  Call this function in order to give the Cursor the possiblity
  to ask engine if there are any new tables that should be written to disk
  or any dropped tables that need to be removed from disk
*/
int StorageEngine::getTableDefinition(Session& session,
                                      const TableIdentifier &identifier,
                                      message::table::shared_ptr &table_message,
                                      bool include_temporary_tables)
{
  int err= ENOENT;

  if (include_temporary_tables)
  {
    Table *table= session.find_temporary_table(identifier);
    if (table)
    {
      table_message.reset(new message::Table(*table->getShare()->getTableProto()));
      return EEXIST;
    }
  }

  drizzled::message::table::shared_ptr table_ptr;
  if ((table_ptr= drizzled::message::Cache::singleton().find(identifier)))
  {
    table_message= table_ptr;
  }

  message::Table message;
  EngineVector::iterator iter=
    find_if(vector_of_engines.begin(), vector_of_engines.end(),
            StorageEngineGetTableDefinition(session, identifier, message, err));

  if (iter == vector_of_engines.end())
  {
    return ENOENT;
  }
  table_message.reset(new message::Table(message));

 drizzled::message::Cache::singleton().insert(identifier, table_message);

  return err;
}

/**
  An interceptor to hijack the text of the error message without
  setting an error in the thread. We need the text to present it
  in the form of a warning to the user.
*/

class Ha_delete_table_error_handler: public Internal_error_handler
{
public:
  Ha_delete_table_error_handler() : Internal_error_handler() {}
  virtual bool handle_error(uint32_t sql_errno,
                            const char *message,
                            DRIZZLE_ERROR::enum_warning_level level,
                            Session *session);
  char buff[DRIZZLE_ERRMSG_SIZE];
};


bool
Ha_delete_table_error_handler::
handle_error(uint32_t ,
             const char *message,
             DRIZZLE_ERROR::enum_warning_level ,
             Session *)
{
  /* Grab the error message */
  strncpy(buff, message, sizeof(buff)-1);
  return true;
}

/**
   returns ENOENT if the file doesn't exists.
*/
int StorageEngine::dropTable(Session& session,
                             const TableIdentifier &identifier)
{
  int error= 0;
  int error_proto;
  message::table::shared_ptr src_proto;
  StorageEngine *engine;

  error_proto= StorageEngine::getTableDefinition(session, identifier, src_proto);

  if (error_proto == ER_CORRUPT_TABLE_DEFINITION)
  {
    string error_message;
    identifier.getSQLPath(error_message);

    error_message.append(" : ");
    error_message.append(src_proto->InitializationErrorString());

    my_error(ER_CORRUPT_TABLE_DEFINITION, MYF(0), error_message.c_str());

    return ER_CORRUPT_TABLE_DEFINITION;
  }

  if (src_proto)
    engine= StorageEngine::findByName(session, src_proto->engine().name());
  else
    engine= StorageEngine::findByName(session, "");

  if (not engine)
  {
    string error_message;
    identifier.getSQLPath(error_message);
    my_error(ER_CORRUPT_TABLE_DEFINITION, MYF(0), error_message.c_str());

    return ER_CORRUPT_TABLE_DEFINITION;
  }

  error= StorageEngine::dropTable(session, *engine, identifier);

  if (error_proto && error == 0)
    return 0;

  return error;
}

int StorageEngine::dropTable(Session& session,
                             StorageEngine &engine,
                             const TableIdentifier &identifier)
{
  int error;

  engine.setTransactionReadWrite(session);
  
  if (unlikely(plugin::EventObserver::beforeDropTable(session, identifier)))
  {
    error= ER_EVENT_OBSERVER_PLUGIN;
  }
  else
  {
    error= engine.doDropTable(session, identifier);
    if (unlikely(plugin::EventObserver::afterDropTable(session, identifier, error)))
    {
      error= ER_EVENT_OBSERVER_PLUGIN;
    }
  }

  drizzled::message::Cache::singleton().erase(identifier);

  return error;
}


/**
  Initiates table-file and calls appropriate database-creator.

  @retval
   0  ok
  @retval
   1  error
*/
int StorageEngine::createTable(Session &session,
                               const TableIdentifier &identifier,
                               message::Table& table_message)
{
  int error= 1;
  TableShare share(identifier);
  table::Shell table(share);
  message::Table tmp_proto;

  if (share.parse_table_proto(session, table_message) || share.open_table_from_share(&session, identifier, "", 0, 0, table))
  { 
    // @note Error occured, we should probably do a little more here.
  }
  else
  {
    /* Check for legal operations against the Engine using the proto (if used) */
    if (table_message.type() == message::Table::TEMPORARY &&
        share.storage_engine->check_flag(HTON_BIT_TEMPORARY_NOT_SUPPORTED) == true)
    {
      error= HA_ERR_UNSUPPORTED;
    }
    else if (table_message.type() != message::Table::TEMPORARY &&
             share.storage_engine->check_flag(HTON_BIT_TEMPORARY_ONLY) == true)
    {
      error= HA_ERR_UNSUPPORTED;
    }
    else
    {
      share.storage_engine->setTransactionReadWrite(session);

      error= share.storage_engine->doCreateTable(session,
                                                 table,
                                                 identifier,
                                                 table_message);
    }

    if (error)
    {
      std::string path;
      identifier.getSQLPath(path);
      my_error(ER_CANT_CREATE_TABLE, MYF(ME_BELL+ME_WAITTANG), path.c_str(), error);
    }

    table.delete_table();
  }

  return(error != 0);
}

Cursor *StorageEngine::getCursor(Table &arg)
{
  return create(arg);
}

class AddTableIdentifier : 
  public unary_function<StorageEngine *, void>
{
  CachedDirectory &directory;
  const SchemaIdentifier &identifier;
  TableIdentifier::vector &set_of_identifiers;

public:

  AddTableIdentifier(CachedDirectory &directory_arg, const SchemaIdentifier &identifier_arg, TableIdentifier::vector &of_names) :
    directory(directory_arg),
    identifier(identifier_arg),
    set_of_identifiers(of_names)
  {
  }

  result_type operator() (argument_type engine)
  {
    engine->doGetTableIdentifiers(directory, identifier, set_of_identifiers);
  }
};


void StorageEngine::getIdentifiers(Session &session, const SchemaIdentifier &schema_identifier, TableIdentifier::vector &set_of_identifiers)
{
  static SchemaIdentifier INFORMATION_SCHEMA_IDENTIFIER("information_schema");
  static SchemaIdentifier DATA_DICTIONARY_IDENTIFIER("data_dictionary");

  CachedDirectory directory(schema_identifier.getPath(), set_of_table_definition_ext);

  if (schema_identifier == INFORMATION_SCHEMA_IDENTIFIER)
  { }
  else if (schema_identifier == DATA_DICTIONARY_IDENTIFIER)
  { }
  else
  {
    if (directory.fail())
    {
      errno= directory.getError();
      if (errno == ENOENT)
      {
        std::string path;
        schema_identifier.getSQLPath(path);
        my_error(ER_BAD_DB_ERROR, MYF(ME_BELL+ME_WAITTANG), path.c_str());
      }
      else
      {
        my_error(ER_CANT_READ_DIR, MYF(ME_BELL+ME_WAITTANG), directory.getPath(), errno);
      }

      return;
    }
  }

  for_each(vector_of_engines.begin(), vector_of_engines.end(),
           AddTableIdentifier(directory, schema_identifier, set_of_identifiers));

  session.doGetTableIdentifiers(directory, schema_identifier, set_of_identifiers);
}

class DropTable: public unary_function<TableIdentifier&, bool>
{
  Session &session;
  StorageEngine *engine;

public:

  DropTable(Session &session_arg, StorageEngine *engine_arg) :
    session(session_arg),
    engine(engine_arg)
  { }

  result_type operator() (argument_type identifier)
  {
    return engine->doDropTable(session, identifier) == 0;
  } 
};

/* This will later be converted to TableIdentifiers */
class DropTables: public unary_function<StorageEngine *, void>
{
  Session &session;
  TableIdentifier::vector &table_identifiers;

public:

  DropTables(Session &session_arg, TableIdentifier::vector &table_identifiers_arg) :
    session(session_arg),
    table_identifiers(table_identifiers_arg)
  { }

  result_type operator() (argument_type engine)
  {
    // True returning from DropTable means the table has been successfully
    // deleted, so it should be removed from the list of tables to drop
    table_identifiers.erase(remove_if(table_identifiers.begin(),
                                      table_identifiers.end(),
                                      DropTable(session, engine)),
                            table_identifiers.end());
  }
};

/*
  This only works for engines which use file based DFE.

  Note-> Unlike MySQL, we do not, on purpose, delete files that do not match any engines. 
*/
void StorageEngine::removeLostTemporaryTables(Session &session, const char *directory)
{
  CachedDirectory dir(directory, set_of_table_definition_ext);
  TableIdentifier::vector table_identifiers;

  if (dir.fail())
  {
    errno= dir.getError();
    my_error(ER_CANT_READ_DIR, MYF(0), directory, errno);

    return;
  }

  CachedDirectory::Entries files= dir.getEntries();

  for (CachedDirectory::Entries::iterator fileIter= files.begin();
       fileIter != files.end(); fileIter++)
  {
    size_t length;
    string path;
    CachedDirectory::Entry *entry= *fileIter;

    /* We remove the file extension. */
    length= entry->filename.length();
    entry->filename.resize(length - DEFAULT_DEFINITION_FILE_EXT.length());

    path+= directory;
    path+= FN_LIBCHAR;
    path+= entry->filename;
    message::Table definition;
    if (StorageEngine::readTableFile(path, definition))
    {
      TableIdentifier identifier(definition.schema(), definition.name(), path);
      table_identifiers.push_back(identifier);
    }
  }

  for_each(vector_of_engines.begin(), vector_of_engines.end(),
           DropTables(session, table_identifiers));
  
  /*
    Now we just clean up anything that might left over.

    We rescan because some of what might have been there should
    now be all nice and cleaned up.
  */
  set<string> all_exts= set_of_table_definition_ext;

  for (EngineVector::iterator iter= vector_of_engines.begin();
       iter != vector_of_engines.end() ; iter++)
  {
    for (const char **ext= (*iter)->bas_ext(); *ext ; ext++)
      all_exts.insert(*ext);
  }

  CachedDirectory rescan(directory, all_exts);

  files= rescan.getEntries();
  for (CachedDirectory::Entries::iterator fileIter= files.begin();
       fileIter != files.end(); fileIter++)
  {
    string path;
    CachedDirectory::Entry *entry= *fileIter;

    path+= directory;
    path+= FN_LIBCHAR;
    path+= entry->filename;

    unlink(path.c_str());
  }
}


/**
  Print error that we got from Cursor function.

  @note
    In case of delete table it's only safe to use the following parts of
    the 'table' structure:
    - table->getShare()->path
    - table->alias
*/
void StorageEngine::print_error(int error, myf errflag, Table &table)
{
  print_error(error, errflag, &table);
}

void StorageEngine::print_error(int error, myf errflag, Table *table)
{
  int textno= ER_GET_ERRNO;
  switch (error) {
  case EACCES:
    textno=ER_OPEN_AS_READONLY;
    break;
  case EAGAIN:
    textno=ER_FILE_USED;
    break;
  case ENOENT:
    textno=ER_FILE_NOT_FOUND;
    break;
  case HA_ERR_KEY_NOT_FOUND:
  case HA_ERR_NO_ACTIVE_RECORD:
  case HA_ERR_END_OF_FILE:
    textno=ER_KEY_NOT_FOUND;
    break;
  case HA_ERR_WRONG_MRG_TABLE_DEF:
    textno=ER_WRONG_MRG_TABLE;
    break;
  case HA_ERR_FOUND_DUPP_KEY:
  {
    assert(table);
    uint32_t key_nr= table->get_dup_key(error);
    if ((int) key_nr >= 0)
    {
      const char *err_msg= ER(ER_DUP_ENTRY_WITH_KEY_NAME);

      print_keydup_error(key_nr, err_msg, *table);

      return;
    }
    textno=ER_DUP_KEY;
    break;
  }
  case HA_ERR_FOREIGN_DUPLICATE_KEY:
  {
    assert(table);
    uint32_t key_nr= table->get_dup_key(error);
    if ((int) key_nr >= 0)
    {
      uint32_t max_length;

      /* Write the key in the error message */
      char key[MAX_KEY_LENGTH];
      String str(key,sizeof(key),system_charset_info);

      /* Table is opened and defined at this point */
      key_unpack(&str,table,(uint32_t) key_nr);
      max_length= (DRIZZLE_ERRMSG_SIZE-
                   (uint32_t) strlen(ER(ER_FOREIGN_DUPLICATE_KEY)));
      if (str.length() >= max_length)
      {
        str.length(max_length-4);
        str.append(STRING_WITH_LEN("..."));
      }
      my_error(ER_FOREIGN_DUPLICATE_KEY, MYF(0), table->getShare()->getTableName(),
        str.c_ptr(), key_nr+1);
      return;
    }
    textno= ER_DUP_KEY;
    break;
  }
  case HA_ERR_FOUND_DUPP_UNIQUE:
    textno=ER_DUP_UNIQUE;
    break;
  case HA_ERR_RECORD_CHANGED:
    textno=ER_CHECKREAD;
    break;
  case HA_ERR_CRASHED:
    textno=ER_NOT_KEYFILE;
    break;
  case HA_ERR_WRONG_IN_RECORD:
    textno= ER_CRASHED_ON_USAGE;
    break;
  case HA_ERR_CRASHED_ON_USAGE:
    textno=ER_CRASHED_ON_USAGE;
    break;
  case HA_ERR_NOT_A_TABLE:
    textno= error;
    break;
  case HA_ERR_CRASHED_ON_REPAIR:
    textno=ER_CRASHED_ON_REPAIR;
    break;
  case HA_ERR_OUT_OF_MEM:
    textno=ER_OUT_OF_RESOURCES;
    break;
  case HA_ERR_WRONG_COMMAND:
    textno=ER_ILLEGAL_HA;
    break;
  case HA_ERR_OLD_FILE:
    textno=ER_OLD_KEYFILE;
    break;
  case HA_ERR_UNSUPPORTED:
    textno=ER_UNSUPPORTED_EXTENSION;
    break;
  case HA_ERR_RECORD_FILE_FULL:
  case HA_ERR_INDEX_FILE_FULL:
    textno=ER_RECORD_FILE_FULL;
    break;
  case HA_ERR_LOCK_WAIT_TIMEOUT:
    textno=ER_LOCK_WAIT_TIMEOUT;
    break;
  case HA_ERR_LOCK_TABLE_FULL:
    textno=ER_LOCK_TABLE_FULL;
    break;
  case HA_ERR_LOCK_DEADLOCK:
    textno=ER_LOCK_DEADLOCK;
    break;
  case HA_ERR_READ_ONLY_TRANSACTION:
    textno=ER_READ_ONLY_TRANSACTION;
    break;
  case HA_ERR_CANNOT_ADD_FOREIGN:
    textno=ER_CANNOT_ADD_FOREIGN;
    break;
  case HA_ERR_ROW_IS_REFERENCED:
  {
    String str;
    get_error_message(error, &str);
    my_error(ER_ROW_IS_REFERENCED_2, MYF(0), str.c_ptr_safe());
    return;
  }
  case HA_ERR_NO_REFERENCED_ROW:
  {
    String str;
    get_error_message(error, &str);
    my_error(ER_NO_REFERENCED_ROW_2, MYF(0), str.c_ptr_safe());
    return;
  }
  case HA_ERR_TABLE_DEF_CHANGED:
    textno=ER_TABLE_DEF_CHANGED;
    break;
  case HA_ERR_NO_SUCH_TABLE:
    assert(table);
    my_error(ER_NO_SUCH_TABLE, MYF(0), table->getShare()->getSchemaName(),
             table->getShare()->getTableName());
    return;
  case HA_ERR_RBR_LOGGING_FAILED:
    textno= ER_BINLOG_ROW_LOGGING_FAILED;
    break;
  case HA_ERR_DROP_INDEX_FK:
  {
    assert(table);
    const char *ptr= "???";
    uint32_t key_nr= table->get_dup_key(error);
    if ((int) key_nr >= 0)
      ptr= table->key_info[key_nr].name;
    my_error(ER_DROP_INDEX_FK, MYF(0), ptr);
    return;
  }
  case HA_ERR_TABLE_NEEDS_UPGRADE:
    textno=ER_TABLE_NEEDS_UPGRADE;
    break;
  case HA_ERR_TABLE_READONLY:
    textno= ER_OPEN_AS_READONLY;
    break;
  case HA_ERR_AUTOINC_READ_FAILED:
    textno= ER_AUTOINC_READ_FAILED;
    break;
  case HA_ERR_AUTOINC_ERANGE:
    textno= ER_WARN_DATA_OUT_OF_RANGE;
    break;
  case HA_ERR_LOCK_OR_ACTIVE_TRANSACTION:
    my_message(ER_LOCK_OR_ACTIVE_TRANSACTION,
               ER(ER_LOCK_OR_ACTIVE_TRANSACTION), MYF(0));
    return;
  default:
    {
      /* 
        The error was "unknown" to this function.
        Ask Cursor if it has got a message for this error 
      */
      bool temporary= false;
      String str;
      temporary= get_error_message(error, &str);
      if (!str.is_empty())
      {
        const char* engine_name= getName().c_str();
        if (temporary)
          my_error(ER_GET_TEMPORARY_ERRMSG, MYF(0), error, str.ptr(),
                   engine_name);
        else
          my_error(ER_GET_ERRMSG, MYF(0), error, str.ptr(), engine_name);
      }
      else
      {
	      my_error(ER_GET_ERRNO,errflag,error);
      }
      return;
    }
  }
  my_error(textno, errflag, table->getShare()->getTableName(), error);
}


/**
  Return an error message specific to this Cursor.

  @param error  error code previously returned by Cursor
  @param buf    pointer to String where to add error message

  @return
    Returns true if this is a temporary error
*/
bool StorageEngine::get_error_message(int , String* )
{
  return false;
}


void StorageEngine::print_keydup_error(uint32_t key_nr, const char *msg, Table &table)
{
  /* Write the duplicated key in the error message */
  char key[MAX_KEY_LENGTH];
  String str(key,sizeof(key),system_charset_info);

  if (key_nr == MAX_KEY)
  {
    /* Key is unknown */
    str.copy("", 0, system_charset_info);
    my_printf_error(ER_DUP_ENTRY, msg, MYF(0), str.c_ptr(), "*UNKNOWN*");
  }
  else
  {
    /* Table is opened and defined at this point */
    key_unpack(&str, &table, (uint32_t) key_nr);
    uint32_t max_length=DRIZZLE_ERRMSG_SIZE-(uint32_t) strlen(msg);
    if (str.length() >= max_length)
    {
      str.length(max_length-4);
      str.append(STRING_WITH_LEN("..."));
    }
    my_printf_error(ER_DUP_ENTRY, msg,
		    MYF(0), str.c_ptr(), table.key_info[key_nr].name);
  }
}


int StorageEngine::deleteDefinitionFromPath(const TableIdentifier &identifier)
{
  string path(identifier.getPath());

  path.append(DEFAULT_DEFINITION_FILE_EXT);

  return internal::my_delete(path.c_str(), MYF(0));
}

int StorageEngine::renameDefinitionFromPath(const TableIdentifier &dest, const TableIdentifier &src)
{
  message::Table table_message;
  string src_path(src.getPath());
  string dest_path(dest.getPath());

  src_path.append(DEFAULT_DEFINITION_FILE_EXT);
  dest_path.append(DEFAULT_DEFINITION_FILE_EXT);

  bool was_read= StorageEngine::readTableFile(src_path.c_str(), table_message);

  if (not was_read)
  {
    return ENOENT;
  }

  dest.copyToTableMessage(table_message);

  int error= StorageEngine::writeDefinitionFromPath(dest, table_message);

  if (not error)
  {
    if (unlink(src_path.c_str()))
      perror(src_path.c_str());
  }

  return error;
}

int StorageEngine::writeDefinitionFromPath(const TableIdentifier &identifier, message::Table &table_message)
{
  char definition_file_tmp[FN_REFLEN];
  string file_name(identifier.getPath());

  file_name.append(DEFAULT_DEFINITION_FILE_EXT);

  snprintf(definition_file_tmp, sizeof(definition_file_tmp), "%sXXXXXX", file_name.c_str());

  int fd= mkstemp(definition_file_tmp);

  if (fd == -1)
  {
    perror(definition_file_tmp);
    return errno;
  }

  google::protobuf::io::ZeroCopyOutputStream* output=
    new google::protobuf::io::FileOutputStream(fd);

  bool success;

  try {
    success= table_message.SerializeToZeroCopyStream(output);
  }
  catch (...)
  {
    success= false;
  }

  if (not success)
  {
    my_error(ER_CORRUPT_TABLE_DEFINITION, MYF(0),
             table_message.InitializationErrorString().c_str());
    delete output;

    if (close(fd) == -1)
      perror(definition_file_tmp);

    if (unlink(definition_file_tmp) == -1)
      perror(definition_file_tmp);

    return ER_CORRUPT_TABLE_DEFINITION;
  }

  delete output;

  if (close(fd) == -1)
  {
    int error= errno;
    perror(definition_file_tmp);

    if (unlink(definition_file_tmp))
      perror(definition_file_tmp);

    return error;
  }

  if (rename(definition_file_tmp, file_name.c_str()) == -1)
  {
    int error= errno;
    perror(definition_file_tmp);

    if (unlink(definition_file_tmp))
      perror(definition_file_tmp);

    return error;
  }

  return 0;
}

class CanCreateTable: public unary_function<StorageEngine *, bool>
{
  const TableIdentifier &identifier;

public:
  CanCreateTable(const TableIdentifier &identifier_arg) :
    identifier(identifier_arg)
  { }

  result_type operator() (argument_type engine)
  {
    return not engine->doCanCreateTable(identifier);
  }
};


/**
  @note on success table can be created.
*/
bool StorageEngine::canCreateTable(const TableIdentifier &identifier)
{
  EngineVector::iterator iter=
    find_if(vector_of_engines.begin(), vector_of_engines.end(),
            CanCreateTable(identifier));

  if (iter == vector_of_engines.end())
  {
    return true;
  }

  return false;
}

bool StorageEngine::readTableFile(const std::string &path, message::Table &table_message)
{
  fstream input(path.c_str(), ios::in | ios::binary);

  if (input.good())
  {
    try {
      if (table_message.ParseFromIstream(&input))
      {
        return true;
      }
    }
    catch (...)
    {
      my_error(ER_CORRUPT_TABLE_DEFINITION, MYF(0),
               table_message.InitializationErrorString().empty() ? "": table_message.InitializationErrorString().c_str());
    }
  }
  else
  {
    perror(path.c_str());
  }

  return false;
}



} /* namespace plugin */
} /* namespace drizzled */
