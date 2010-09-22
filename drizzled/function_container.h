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

#ifndef DRIZZLED_FUNCTION_CONTAINER_H
#define DRIZZLED_FUNCTION_CONTAINER_H

#include <set>
#include <boost/unordered_map.hpp>
#include "drizzled/util/string.h"

namespace drizzled {

struct Native_func_registry;
typedef boost::unordered_map<std::string, Native_func_registry *, util::insensitive_hash, util::insensitive_equal_to> NativeFunctionsMap;

class FunctionContainer {
public:
  static NativeFunctionsMap &getMap();
};

} /* namepsace drizzled */

#endif /* DRIZZLED_FUNCTION_CONTAINER_H */