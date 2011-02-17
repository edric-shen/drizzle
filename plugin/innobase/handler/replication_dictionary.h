/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
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


#ifndef PLUGIN_INNOBASE_HANDLER_REPLICATION_DICTIONARY_H
#define PLUGIN_INNOBASE_HANDLER_REPLICATION_DICTIONARY_H 

#include <drizzled/plugin/table_function.h>
#include <drizzled/field.h>
#include <drizzled/message/transaction.pb.h>

class InnodbReplicationTable : public drizzled::plugin::TableFunction
{
public:
  InnodbReplicationTable();

  class Generator : public drizzled::plugin::TableFunction::Generator
  {
    std::string transaction_text;
    drizzled::message::Transaction message;
    struct read_replication_state_st *replication_state;

  public:
    Generator(drizzled::Field **arg);
    ~Generator();
                        
    bool populate();
  private:
  };

  Generator *generator(drizzled::Field **arg)
  {
    return new Generator(arg);
  }
};

#endif /* PLUGIN_INNOBASE_HANDLER_REPLICATION_DICTIONARY_H */
