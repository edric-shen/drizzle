/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
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

#ifndef DRIZZLED_GENERATOR_TABLE_DEFINITION_CACHE_H
#define DRIZZLED_GENERATOR_TABLE_DEFINITION_CACHE_H

#include "drizzled/definition/cache.h"

namespace drizzled {
namespace generator {

class TableDefinitionCache
{
  drizzled::TableShare::vector local_vector;
  drizzled::TableShare::vector::iterator iter;

public:

  TableDefinitionCache()
  {
    definition::Cache::singleton().CopyFrom(local_vector);
    iter= local_vector.begin();
  }

  ~TableDefinitionCache()
  {
  }

  operator drizzled::TableShare::shared_ptr()
  {
    while (iter != local_vector.end())
    {
      drizzled::TableShare::shared_ptr ret(*iter);
      iter++;
      return ret;
    }

    return drizzled::TableShare::shared_ptr();
  }
};

} /* namespace generator */
} /* namespace drizzled */

#endif /* DRIZZLED_GENERATOR_TABLE_DEFINITION_CACHE_H */