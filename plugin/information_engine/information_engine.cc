/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
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

#include <plugin/information_engine/information_engine.h>
#include <drizzled/session.h>

#include <plugin/information_engine/information_cursor.h>

#include <string>

using namespace std;
using namespace drizzled;

static const string engine_name("INFORMATION_ENGINE");


Cursor *InformationEngine::create(TableShare &table, MEM_ROOT *mem_root)
{
  return new (mem_root) InformationCursor(*this, table);
}


int InformationEngine::doGetTableDefinition(Session &,
                                            const char *path,
                                            const char *,
                                            const char *,
                                            const bool,
                                            message::Table *table_proto)
{
  string tab_name(path);
  string i_s_prefix("./information_schema/");
  if (tab_name.compare(0, i_s_prefix.length(), i_s_prefix) != 0)
  {
    return ENOENT;
  }

  if (! table_proto)
  {
    return EEXIST;
  }

  tab_name.erase(0, i_s_prefix.length());
  plugin::InfoSchemaTable *schema_table= plugin::InfoSchemaTable::getTable(tab_name.c_str());

  if (! schema_table)
  {
    return ENOENT;
  }

  table_proto->set_name(tab_name);
  table_proto->set_type(message::Table::STANDARD);

  message::Table::StorageEngine *protoengine= table_proto->mutable_engine();
  protoengine->set_name(engine_name);

  message::Table::TableOptions *table_options= table_proto->mutable_options();
  table_options->set_collation_id(default_charset_info->number);
  table_options->set_collation(default_charset_info->name);

  const plugin::InfoSchemaTable::Columns &columns= schema_table->getColumns();
  plugin::InfoSchemaTable::Columns::const_iterator iter= columns.begin();

  while (iter != columns.end())
  {
    const plugin::ColumnInfo *column= *iter;
    /* get the various proto variables we need */
    message::Table::Field *proto_field= table_proto->add_field();
    message::Table::Field::FieldOptions *field_options=
      proto_field->mutable_options();
    message::Table::Field::FieldConstraints *field_constraints=
      proto_field->mutable_constraints();

    proto_field->set_name(column->getName());
    field_options->set_default_value("0");

    if (column->getFlags() & MY_I_S_MAYBE_NULL)
    {
      field_options->set_default_null(true);
      field_constraints->set_is_nullable(true);
    }
    else
    {
      field_options->set_default_null(false);
      field_constraints->set_is_nullable(false);
    }

    if (column->getFlags() & MY_I_S_UNSIGNED)
    {
      field_constraints->set_is_unsigned(true);
    }

    switch(column->getType())
    {
    case DRIZZLE_TYPE_LONG:
      proto_field->set_type(message::Table::Field::INTEGER);
      break;
    case DRIZZLE_TYPE_DOUBLE:
      proto_field->set_type(message::Table::Field::DOUBLE);
      break;
    case DRIZZLE_TYPE_NULL:
      assert(true);
      break;
    case DRIZZLE_TYPE_TIMESTAMP:
      proto_field->set_type(message::Table::Field::TIMESTAMP);
      field_options->set_default_value("NOW()");
      break;
    case DRIZZLE_TYPE_LONGLONG:
      proto_field->set_type(message::Table::Field::BIGINT);
      break;
    case DRIZZLE_TYPE_DATETIME:
      proto_field->set_type(message::Table::Field::DATETIME);
      field_options->set_default_value("NOW()");
      break;
    case DRIZZLE_TYPE_DATE:
      proto_field->set_type(message::Table::Field::DATE);
      field_options->set_default_value("NOW()");
      break;
    case DRIZZLE_TYPE_VARCHAR:
    {
      message::Table::Field::StringFieldOptions *str_field_options=
        proto_field->mutable_string_options();
      proto_field->set_type(message::Table::Field::VARCHAR);
      str_field_options->set_length(column->getLength());
      field_options->set_default_value("");
      str_field_options->set_collation_id(default_charset_info->number);
      str_field_options->set_collation(default_charset_info->name);
      break;
    }
    case DRIZZLE_TYPE_DECIMAL:
    {
      message::Table::Field::NumericFieldOptions *num_field_options=
        proto_field->mutable_numeric_options();
      proto_field->set_type(message::Table::Field::DECIMAL);
      num_field_options->set_precision(column->getLength());
      num_field_options->set_scale(column->getLength() % 10);
      break;
    }
    default:
      assert(true);
      break;
    }

    ++iter;
  }

  return EEXIST;
}


void InformationEngine::doGetTableNames(CachedDirectory&, 
                                        string &db, 
                                        set<string> &set_of_names)
{
  if (db.compare("information_schema"))
    return;

  plugin::InfoSchemaTable::getTableNames(set_of_names);
}

InformationEngine::Share *InformationEngine::getShare(const string &name_arg)
{
  InformationEngine::Share *share= NULL;
  pthread_mutex_lock(&mutex);

  OpenTables::iterator it= open_tables.find(name_arg);

  if (it != open_tables.end())
  {
    share= &((*it).second);
    share->incUseCount();
  }
  else
  {
    pair<OpenTables::iterator, bool> returned;

    returned=
      open_tables.insert(Record(name_arg, InformationEngine::Share(name_arg)));

    if (returned.second == false)
    {
      pthread_mutex_unlock(&mutex);
      return NULL;
    }

    Record &value= *(returned.first);
    share= &(value.second);
    share->setInfoSchemaTable(name_arg);
    share->initThreadLock();
  }


  pthread_mutex_unlock(&mutex);

  return share;
}


void InformationEngine::freeShare(InformationEngine::Share *share)
{
  pthread_mutex_lock(&mutex);

  share->decUseCount();

  if (share->getUseCount() == 0)
  {
    open_tables.erase(share->getName());
    share->deinitThreadLock();
  }

  pthread_mutex_unlock(&mutex);
}


static plugin::StorageEngine *information_engine= NULL;

static int init(plugin::Registry &registry)
{
  information_engine= new(std::nothrow) InformationEngine(engine_name);
  if (! information_engine)
  {
    return 1;
  }

  registry.add(information_engine);
  
  return 0;
}

static int finalize(plugin::Registry &registry)
{
  registry.remove(information_engine);
  delete information_engine;

  return 0;
}

DRIZZLE_DECLARE_PLUGIN
{
  "INFORMATION_ENGINE",
  "1.0",
  "Padraig O'Sullivan, Brian Aker",
  "Engine which provides information schema tables",
  PLUGIN_LICENSE_GPL,
  init,     /* Plugin Init */
  finalize,     /* Plugin Deinit */
  NULL,               /* status variables */
  NULL,               /* system variables */
  NULL                /* config options   */
}
DRIZZLE_DECLARE_PLUGIN_END;