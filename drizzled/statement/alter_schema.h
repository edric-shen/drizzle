/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
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

#ifndef DRIZZLED_STATEMENT_ALTER_SCHEMA_H
#define DRIZZLED_STATEMENT_ALTER_SCHEMA_H

#include <drizzled/statement/create_schema.h>

namespace drizzled
{
class Session;

namespace statement
{

class AlterSchema : public CreateSchema
{
public:
  AlterSchema(Session *in_session)
    :
      CreateSchema(in_session)
  { 
    schema_message.set_update_timestamp(time(NULL));
  }

  bool execute();
};

} /* namespace statement */

} /* namespace drizzled */

#endif /* DRIZZLED_STATEMENT_ALTER_SCHEMA_H */
