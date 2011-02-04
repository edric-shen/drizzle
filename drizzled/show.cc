/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Brian Aker
 *  Copyright (C) 2008 Sun Microsystems, Inc.
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


/* Function with list databases, tables or fields */
#include "config.h"
#include <drizzled/sql_select.h>
#include <drizzled/show.h>
#include <drizzled/gettext.h>
#include <drizzled/util/convert.h>
#include <drizzled/error.h>
#include <drizzled/tztime.h>
#include <drizzled/data_home.h>
#include <drizzled/item/blob.h>
#include <drizzled/item/cmpfunc.h>
#include <drizzled/item/return_int.h>
#include <drizzled/item/empty_string.h>
#include <drizzled/item/return_date_time.h>
#include <drizzled/sql_base.h>
#include <drizzled/db.h>
#include <drizzled/field/epoch.h>
#include <drizzled/field/decimal.h>
#include <drizzled/lock.h>
#include <drizzled/item/return_date_time.h>
#include <drizzled/item/empty_string.h>
#include "drizzled/session/cache.h"
#include <drizzled/message/schema.pb.h>
#include <drizzled/plugin/client.h>
#include <drizzled/cached_directory.h>
#include "drizzled/sql_table.h"
#include "drizzled/global_charset_info.h"
#include "drizzled/pthread_globals.h"
#include "drizzled/internal/m_string.h"
#include "drizzled/internal/my_sys.h"
#include "drizzled/message/statement_transform.h"

#include "drizzled/statement/show.h"
#include "drizzled/statement/show_errors.h"
#include "drizzled/statement/show_warnings.h"


#include <sys/stat.h>

#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>

using namespace std;

namespace drizzled
{

inline const char *
str_or_nil(const char *str)
{
  return str ? str : "<nil>";
}

int wild_case_compare(const CHARSET_INFO * const cs, const char *str, const char *wildstr)
{
  register int flag;

  while (*wildstr)
  {
    while (*wildstr && *wildstr != internal::wild_many && *wildstr != internal::wild_one)
    {
      if (*wildstr == internal::wild_prefix && wildstr[1])
        wildstr++;
      if (my_toupper(cs, *wildstr++) != my_toupper(cs, *str++))
        return (1);
    }
    if (! *wildstr )
      return (*str != 0);
    if (*wildstr++ == internal::wild_one)
    {
      if (! *str++)
        return (1);	/* One char; skip */
    }
    else
    {						/* Found '*' */
      if (! *wildstr)
        return (0);		/* '*' as last char: OK */
      flag=(*wildstr != internal::wild_many && *wildstr != internal::wild_one);
      do
      {
        if (flag)
        {
          char cmp;
          if ((cmp= *wildstr) == internal::wild_prefix && wildstr[1])
            cmp= wildstr[1];
          cmp= my_toupper(cs, cmp);
          while (*str && my_toupper(cs, *str) != cmp)
            str++;
          if (! *str)
            return (1);
        }
        if (wild_case_compare(cs, str, wildstr) == 0)
          return (0);
      } while (*str++);
      return (1);
    }
  }

  return (*str != '\0');
}

/*
  Get the quote character for displaying an identifier.

  SYNOPSIS
    get_quote_char_for_identifier()

  IMPLEMENTATION
    Force quoting in the following cases:
      - name is empty (for one, it is possible when we use this function for
        quoting user and host names for DEFINER clause);
      - name is a keyword;
      - name includes a special character;
    Otherwise identifier is quoted only if the option OPTION_QUOTE_SHOW_CREATE
    is set.

  RETURN
    EOF	  No quote character is needed
    #	  Quote character
*/

int get_quote_char_for_identifier()
{
  return '`';
}

namespace show {

bool buildScemas(Session *session)
{
  session->getLex()->sql_command= SQLCOM_SELECT;
  session->getLex()->statement= new statement::Show(session);

  std::string column_name= "Database";
  if (session->getLex()->wild)
  {
    column_name.append(" (");
    column_name.append(session->getLex()->wild->ptr());
    column_name.append(")");
  }

  if (session->getLex()->current_select->where)
  {
    if (prepare_new_schema_table(session, session->getLex(), "SCHEMAS"))
      return false;
  }
  else
  {
    if (prepare_new_schema_table(session, session->getLex(), "SHOW_SCHEMAS"))
      return false;
  }

  Item_field *my_field= new Item_field(&session->lex->current_select->context, NULL, NULL, "SCHEMA_NAME");
  my_field->is_autogenerated_name= false;
  my_field->set_name(column_name.c_str(), column_name.length(), system_charset_info);

  if (session->add_item_to_list(my_field))
    return false;

  if (session->add_order_to_list(my_field, true))
    return false;

  return true;
}

bool buildTables(Session *session, const char *ident)
{
  session->getLex()->sql_command= SQLCOM_SELECT;

  drizzled::statement::Show *select= new statement::Show(session);
  session->getLex()->statement= select;

  std::string column_name= "Tables_in_";

  util::string::const_shared_ptr schema(session->schema());
  if (ident)
  {
    identifier::Schema identifier(ident);
    column_name.append(ident);
    session->getLex()->select_lex.db= const_cast<char *>(ident);
    if (not plugin::StorageEngine::doesSchemaExist(identifier))
    {
      my_error(ER_BAD_DB_ERROR, MYF(0), ident);
    }
    select->setShowPredicate(ident, "");
  }
  else if (schema and not schema->empty())
  {
    column_name.append(*schema);
    select->setShowPredicate(*schema, "");
  }
  else
  {
    my_error(ER_NO_DB_ERROR, MYF(0));
    return false;
  }


  if (session->getLex()->wild)
  {
    column_name.append(" (");
    column_name.append(session->getLex()->wild->ptr());
    column_name.append(")");
  }

  if (prepare_new_schema_table(session, session->getLex(), "SHOW_TABLES"))
    return false;

  Item_field *my_field= new Item_field(&session->lex->current_select->context, NULL, NULL, "TABLE_NAME");
  my_field->is_autogenerated_name= false;
  my_field->set_name(column_name.c_str(), column_name.length(), system_charset_info);

  if (session->add_item_to_list(my_field))
    return false;

  if (session->add_order_to_list(my_field, true))
    return false;

  return true;
}

bool buildTemporaryTables(Session *session)
{
  session->getLex()->sql_command= SQLCOM_SELECT;

  session->getLex()->statement= new statement::Show(session);


  if (prepare_new_schema_table(session, session->getLex(), "SHOW_TEMPORARY_TABLES"))
    return false;

  if (session->add_item_to_list( new Item_field(&session->lex->current_select->context, NULL, NULL, "*")))
    return false;

  (session->lex->current_select->with_wild)++;

  return true;
}

bool buildTableStatus(Session *session, const char *ident)
{
  session->getLex()->sql_command= SQLCOM_SELECT;
  drizzled::statement::Show *select= new statement::Show(session);
  session->getLex()->statement= select;

  std::string column_name= "Tables_in_";

  util::string::const_shared_ptr schema(session->schema());
  if (ident)
  {
    session->getLex()->select_lex.db= const_cast<char *>(ident);

    identifier::Schema identifier(ident);
    if (not plugin::StorageEngine::doesSchemaExist(identifier))
    {
      my_error(ER_BAD_DB_ERROR, MYF(0), ident);
    }

    select->setShowPredicate(ident, "");
  }
  else if (schema)
  {
    select->setShowPredicate(*schema, "");
  }
  else
  {
    my_error(ER_NO_DB_ERROR, MYF(0));
    return false;
  }

  if (prepare_new_schema_table(session, session->getLex(), "SHOW_TABLE_STATUS"))
    return false;

  if (session->add_item_to_list( new Item_field(&session->lex->current_select->
                                                  context,
                                                  NULL, NULL, "*")))
    return false;

  (session->lex->current_select->with_wild)++;

  return true;
}

bool buildEngineStatus(Session *session, LEX_STRING)
{
  session->getLex()->sql_command= SQLCOM_SELECT;
  drizzled::statement::Show *select= new statement::Show(session);
  session->getLex()->statement= select;

  my_error(ER_USE_DATA_DICTIONARY);
  return false;
}

bool buildColumns(Session *session, const char *schema_ident, Table_ident *table_ident)
{
  session->getLex()->sql_command= SQLCOM_SELECT;

  drizzled::statement::Show *select= new statement::Show(session);
  session->getLex()->statement= select;

  util::string::const_shared_ptr schema(session->schema());
  if (schema_ident)
  {
    select->setShowPredicate(schema_ident, table_ident->table.str);
  }
  else if (table_ident->db.str)
  {
    select->setShowPredicate(table_ident->db.str, table_ident->table.str);
  }
  else if (schema)
  {
    select->setShowPredicate(*schema, table_ident->table.str);
  }
  else
  {
    my_error(ER_NO_DB_ERROR, MYF(0));
    return false;
  }

  {
    drizzled::identifier::Table identifier(select->getShowSchema().c_str(), table_ident->table.str);
    if (not plugin::StorageEngine::doesTableExist(*session, identifier))
    {
      my_error(ER_TABLE_UNKNOWN, identifier);
    }
  }

  if (prepare_new_schema_table(session, session->getLex(), "SHOW_COLUMNS"))
    return false;

  if (session->add_item_to_list( new Item_field(&session->lex->current_select->context, NULL, NULL, "*")))
    return false;

  (session->lex->current_select->with_wild)++;

  return true;
}

bool buildWarnings(Session *session)
{
  session->getLex()->statement= new statement::ShowWarnings(session);

  return true;
}

bool buildErrors(Session *session)
{
  session->getLex()->statement= new statement::ShowErrors(session);

  return true;
}

bool buildIndex(Session *session, const char *schema_ident, Table_ident *table_ident)
{
  session->getLex()->sql_command= SQLCOM_SELECT;
  drizzled::statement::Show *select= new statement::Show(session);
  session->getLex()->statement= select;

  util::string::const_shared_ptr schema(session->schema());
  if (schema_ident)
  {
    select->setShowPredicate(schema_ident, table_ident->table.str);
  }
  else if (table_ident->db.str)
  {
    select->setShowPredicate(table_ident->db.str, table_ident->table.str);
  }
  else if (schema)
  {
    select->setShowPredicate(*schema, table_ident->table.str);
  }
  else
  {
    my_error(ER_NO_DB_ERROR, MYF(0));
    return false;
  }

  {
    drizzled::identifier::Table identifier(select->getShowSchema().c_str(), table_ident->table.str);
    if (not plugin::StorageEngine::doesTableExist(*session, identifier))
    {
      my_error(ER_TABLE_UNKNOWN, identifier);
    }
  }

  if (prepare_new_schema_table(session, session->getLex(), "SHOW_INDEXES"))
    return false;

  if (session->add_item_to_list( new Item_field(&session->lex->current_select->context, NULL, NULL, "*")))
    return false;

  (session->lex->current_select->with_wild)++;

  return true;
}

bool buildStatus(Session *session, const drizzled::sql_var_t is_global)
{
  session->getLex()->sql_command= SQLCOM_SELECT;
  session->getLex()->statement= new statement::Show(session);

  if (is_global == OPT_GLOBAL)
  {
    if (prepare_new_schema_table(session, session->getLex(), "GLOBAL_STATUS"))
      return false;
  }
  else
  {
    if (prepare_new_schema_table(session, session->getLex(), "SESSION_STATUS"))
      return false;
  }

  std::string key("Variable_name");
  std::string value("Value");

  Item_field *my_field= new Item_field(&session->lex->current_select->context, NULL, NULL, "VARIABLE_NAME");
  my_field->is_autogenerated_name= false;
  my_field->set_name(key.c_str(), key.length(), system_charset_info);

  if (session->add_item_to_list(my_field))
    return false;

  my_field= new Item_field(&session->lex->current_select->context, NULL, NULL, "VARIABLE_VALUE");
  my_field->is_autogenerated_name= false;
  my_field->set_name(value.c_str(), value.length(), system_charset_info);

  if (session->add_item_to_list(my_field))
    return false;

  return true;
}

bool buildCreateTable(Session *session, Table_ident *ident)
{
  session->getLex()->sql_command= SQLCOM_SELECT;
  statement::Show *select= new statement::Show(session);
  session->getLex()->statement= select;

  if (session->getLex()->statement == NULL)
    return false;

  if (prepare_new_schema_table(session, session->getLex(), "TABLE_SQL_DEFINITION"))
    return false;

  util::string::const_shared_ptr schema(session->schema());
  if (ident->db.str)
  {
    select->setShowPredicate(ident->db.str, ident->table.str);
  }
  else if (schema)
  {
    select->setShowPredicate(*schema, ident->table.str);
  }
  else
  {
    my_error(ER_NO_DB_ERROR, MYF(0));
    return false;
  }

  std::string key("Table");
  std::string value("Create Table");

  Item_field *my_field= new Item_field(&session->lex->current_select->context, NULL, NULL, "TABLE_NAME");
  my_field->is_autogenerated_name= false;
  my_field->set_name(key.c_str(), key.length(), system_charset_info);

  if (session->add_item_to_list(my_field))
    return false;

  my_field= new Item_field(&session->lex->current_select->context, NULL, NULL, "TABLE_SQL_DEFINITION");
  my_field->is_autogenerated_name= false;
  my_field->set_name(value.c_str(), value.length(), system_charset_info);

  if (session->add_item_to_list(my_field))
    return false;

  return true;
}

bool buildProcesslist(Session *session)
{
  session->getLex()->sql_command= SQLCOM_SELECT;
  session->getLex()->statement= new statement::Show(session);

  if (prepare_new_schema_table(session, session->getLex(), "PROCESSLIST"))
    return false;

  if (session->add_item_to_list( new Item_field(&session->lex->current_select->context, NULL, NULL, "*")))
    return false;

  (session->lex->current_select->with_wild)++;

  return true;
}

bool buildVariables(Session *session, const drizzled::sql_var_t is_global)
{
  session->getLex()->sql_command= SQLCOM_SELECT;
  session->getLex()->statement= new statement::Show(session);

  if (is_global == OPT_GLOBAL)
  {
    if (prepare_new_schema_table(session, session->getLex(), "GLOBAL_VARIABLES"))
      return false;
  }
  else
  {
    if (prepare_new_schema_table(session, session->getLex(), "SESSION_VARIABLES"))
      return false;
  }

  std::string key("Variable_name");
  std::string value("Value");

  Item_field *my_field= new Item_field(&session->lex->current_select->context, NULL, NULL, "VARIABLE_NAME");
  my_field->is_autogenerated_name= false;
  my_field->set_name(key.c_str(), key.length(), system_charset_info);

  if (session->add_item_to_list(my_field))
    return false;

  my_field= new Item_field(&session->lex->current_select->context, NULL, NULL, "VARIABLE_VALUE");
  my_field->is_autogenerated_name= false;
  my_field->set_name(value.c_str(), value.length(), system_charset_info);

  if (session->add_item_to_list(my_field))
    return false;

  return true;
}

bool buildCreateSchema(Session *session, LEX_STRING &ident)
{
  session->getLex()->sql_command= SQLCOM_SELECT;
  drizzled::statement::Show *select= new statement::Show(session);
  session->getLex()->statement= select;

  if (prepare_new_schema_table(session, session->getLex(), "SCHEMA_SQL_DEFINITION"))
    return false;

  util::string::const_shared_ptr schema(session->schema());
  if (ident.str)
  {
    select->setShowPredicate(ident.str);
  }
  else if (schema)
  {
    select->setShowPredicate(*schema);
  }
  else
  {
    my_error(ER_NO_DB_ERROR, MYF(0));
    return false;
  }

  std::string key("Database");
  std::string value("Create Database");

  Item_field *my_field= new Item_field(&session->lex->current_select->context, NULL, NULL, "SCHEMA_NAME");
  my_field->is_autogenerated_name= false;
  my_field->set_name(key.c_str(), key.length(), system_charset_info);

  if (session->add_item_to_list(my_field))
    return false;

  my_field= new Item_field(&session->lex->current_select->context, NULL, NULL, "SCHEMA_SQL_DEFINITION");
  my_field->is_autogenerated_name= false;
  my_field->set_name(value.c_str(), value.length(), system_charset_info);

  if (session->add_item_to_list(my_field))
    return false;

  return true;
}

bool buildDescribe(Session *session, Table_ident *ident)
{
  session->getLex()->lock_option= TL_READ;
  init_select(session->getLex());
  session->getLex()->current_select->parsing_place= SELECT_LIST;
  session->getLex()->sql_command= SQLCOM_SELECT;
  drizzled::statement::Show *select= new statement::Show(session);
  session->getLex()->statement= select;
  session->getLex()->select_lex.db= 0;

  util::string::const_shared_ptr schema(session->schema());
  if (ident->db.str)
  {
    select->setShowPredicate(ident->db.str, ident->table.str);
  }
  else if (schema)
  {
    select->setShowPredicate(*schema, ident->table.str);
  }
  else
  {
    my_error(ER_NO_DB_ERROR, MYF(0));
    return false;
  }

  {
    drizzled::identifier::Table identifier(select->getShowSchema().c_str(), ident->table.str);
    if (not plugin::StorageEngine::doesTableExist(*session, identifier))
    {
      my_error(ER_TABLE_UNKNOWN, identifier);
    }
  }

  if (prepare_new_schema_table(session, session->getLex(), "SHOW_COLUMNS"))
  {
    return false;
  }

  if (session->add_item_to_list( new Item_field(&session->lex->current_select->
                                                  context,
                                                  NULL, NULL, "*")))
  {
    return false;
  }

  (session->lex->current_select->with_wild)++;

  return true;
}

} /* namespace drizzled */

} /* namespace drizzled */
