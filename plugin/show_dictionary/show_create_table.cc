/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Brian Aker
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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
#include "plugin/show_dictionary/dictionary.h"
#include "drizzled/identifier.h"
#include "drizzled/message.h"
#include "drizzled/message/statement_transform.h"
#include <google/protobuf/text_format.h>
#include <string>

using namespace std;
using namespace drizzled;

ShowCreateTable::ShowCreateTable() :
  plugin::TableFunction("DATA_DICTIONARY", "TABLE_SQL_DEFINITION")
{
  add_field("TABLE_NAME", plugin::TableFunction::STRING, MAXIMUM_IDENTIFIER_LENGTH, false);
  add_field("TABLE_SQL_DEFINITION", plugin::TableFunction::STRING, TABLE_FUNCTION_BLOB_SIZE, false);
}

ShowCreateTable::Generator::Generator(Field **arg) :
  plugin::TableFunction::Generator(arg),
  is_primed(false)
{
  statement::Select *select= static_cast<statement::Select *>(getSession().lex->statement);

  if (not select->getShowTable().empty() && not select->getShowSchema().empty())
  {
    TableIdentifier identifier(select->getShowSchema(), select->getShowTable());

    int error= plugin::StorageEngine::getTableDefinition(getSession(),
                                                         identifier,
                                                         table_message);

    if (error == EEXIST)
      is_primed= true;
  }
}

bool ShowCreateTable::Generator::populate()
{
  enum drizzled::message::TransformSqlError transform_err;

  if (not is_primed)
    return false;

  std::string create_sql;
  transform_err= message::transformTableDefinitionToSql(table_message,
                                                        create_sql,
                                                        message::DRIZZLE,
                                                        false);
  if (transform_err != drizzled::message::NONE)
  {
    return false;
  }

  push(table_message.name());
  push(create_sql);
  is_primed= false;

  return true;
}