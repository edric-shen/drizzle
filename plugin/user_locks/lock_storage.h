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

#ifndef PLUGIN_USER_LOCKS_STORABLE_H_H
#define PLUGIN_USER_LOCKS_STORABLE_H_H

#include "drizzled/session.h"

#include <boost/unordered_set.hpp>

namespace user_locks {

/*
  We a storable to track any locks we might have open so that if we are disconnected before we release the locks, we release the locks during the deconstruction of Session.
*/

class Storable : public drizzled::util::Storable {
  typedef boost::unordered_set<std::string> LockList;
  LockList list_of_locks;
  drizzled::session_id_t id;

public:

  Storable(drizzled::session_id_t id_arg) :
    id(id_arg)
  {
  }

  ~Storable()
  {
    erase_all();
  }

  void insert(const std::string &arg)
  {
    list_of_locks.insert(arg);
  }

  void erase(const std::string &arg)
  {
    list_of_locks.erase(arg);
  }

  void erase_all()
  {
    for (LockList::iterator iter= list_of_locks.begin();
         iter != list_of_locks.end(); iter++)
    {
      (void)user_locks::Locks::getInstance().release(*iter, id);
    }
    list_of_locks.clear();
  }
};


} /* namespace user_locks */

#endif /* PLUGIN_USER_LOCKS_STORABLE_H_H */
