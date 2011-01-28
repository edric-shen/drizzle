/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2011 David Shrewsbury
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
#include "plugin/slave/queue_manager.h"
#include "drizzled/message/transaction.pb.h"
#include "drizzled/plugin/listen.h"
#include "drizzled/plugin/client.h"
#include "drizzled/catalog/local.h"
#include "drizzled/execute.h"
#include "drizzled/internal/my_pthread.h"
#include <string>
#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace drizzled;

namespace slave
{

void QueueManager::processQueue(void)
{
  boost::posix_time::seconds duration(getCheckInterval());
  
  /* thread setup needed to do things like create a Session */
  internal::my_thread_init();
  boost::this_thread::at_thread_exit(&internal::my_thread_end);

  drizzled::Session::shared_ptr session;

  if (session= Session::make_shared(plugin::Listen::getNullClient(), catalog::local()))
  {
    currentSession().release();
    currentSession().reset(session.get());
  }
  else
  {
    printf("Slave thread could not create a Session\n"); fflush(stdout);
    return;
  }

  int i= 0;
  while (1)
  {
    /* This uninterruptable block processes the message queue */
    {
      boost::this_thread::disable_interruption di;
      drizzled::Execute execute(*(session.get()), true);

      string sql("DELETE FROM ");
      sql.append(getSchema());
      sql.append(".");
      sql.append(getTable());
      sql.append(" WHERE id = ");
      sql.append(boost::lexical_cast<std::string>(i));

      printf("executing: %s\n", sql.c_str()); fflush(stdout);
      execute.run(sql);

      i++;
      if (i > 100)
        return;
    }
    
    /* Interruptable only when not doing work (aka, sleeping) */
    try
    {
      boost::this_thread::sleep(duration);
    }
    catch (boost::thread_interrupted &)
    {
      return;
    }
  }
}

bool QueueManager::executeMessage(const message::Transaction &transaction)
{
  (void)transaction;
  return true;
}

} /* namespace slave */