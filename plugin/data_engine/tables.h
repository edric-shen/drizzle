/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Sun Microsystems
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

#ifndef PLUGIN_DATA_ENGINE_TABLES_H
#define PLUGIN_DATA_ENGINE_TABLES_H

class TablesTool : public Tool
{
public:

  TablesTool();
};

class TablesNameTool : public Tool
{
public:

  TablesNameTool();

  class Generator : public Tool::Generator 
  {
    std::set<std::string> schema_names;
    std::set<std::string> table_names;
    std::set<std::string>::iterator schema_iterator;
    std::set<std::string>::iterator table_iterator;
    std::string db_name;

  public:
    Generator();

    bool populate(Field ** fields);
  };

  Generator *generator()
  {
    return new Generator;
  }
};

class TablesInfoTool : public Tool
{
public:

  TablesInfoTool();

  class Generator : public Tool::Generator 
  {
    std::set<std::string> schema_names;
    std::set<std::string> table_names;
    std::set<std::string>::iterator schema_iterator;
    std::set<std::string>::iterator table_iterator;
    std::string db_name;
    uint32_t schema_counter;
    uint32_t table_counter;

  public:
    Generator();

    bool populate(Field ** fields);
  };

  Generator *generator()
  {
    return new Generator;
  }
};

#endif // PLUGIN_DATA_ENGINE_TABLES_H
