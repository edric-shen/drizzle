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
#include "plugin/status_dictionary/dictionary.h"

using namespace drizzled;

static StatementsTool *global_statements;
static StatementsTool *session_statements;
static StatusTool *global_status;
static StatusTool *session_status;
static VariablesTool *global_variables;
static VariablesTool *session_variables;


static int init(drizzled::plugin::Registry &registry)
{
  global_statements= new(std::nothrow)StatementsTool(true);
  global_status= new(std::nothrow)StatusTool(true);
  session_statements= new(std::nothrow)StatementsTool(false);
  session_status= new(std::nothrow)StatusTool(false);
  global_variables= new(std::nothrow)VariablesTool(true);
  session_variables= new(std::nothrow)VariablesTool(false);

  registry.add(global_statements);
  registry.add(global_status);
  registry.add(global_variables);
  registry.add(session_statements);
  registry.add(session_status);
  registry.add(session_variables);
  
  return 0;
}

static int finalize(drizzled::plugin::Registry &registry)
{
  registry.remove(global_statements);
  registry.remove(global_status);
  registry.remove(global_variables);
  registry.remove(session_statements);
  registry.remove(session_status);
  registry.remove(session_variables);

  delete global_statements;
  delete global_status;
  delete global_variables;
  delete session_statements;
  delete session_status;
  delete session_variables;

  return 0;
}

DRIZZLE_DECLARE_PLUGIN
{
  DRIZZLE_VERSION_ID,
  "status_dictionary",
  "1.0",
  "Brian Aker",
  "Dictionary for status, statement, and variable information.",
  PLUGIN_LICENSE_GPL,
  init,
  finalize,
  NULL,
  NULL,
  NULL
}
DRIZZLE_DECLARE_PLUGIN_END;
