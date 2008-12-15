/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
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

#include <drizzled/server_includes.h>
#include CSTDINT_H
#include <drizzled/item/cache_real.h>
#include CMATH_H

void Item_cache_real::store(Item *item)
{
  value= item->val_result();
  null_value= item->null_value;
}


int64_t Item_cache_real::val_int()
{
  assert(fixed == 1);
  return (int64_t) rint(value);
}


String* Item_cache_real::val_str(String *str)
{
  assert(fixed == 1);
  str->set_real(value, decimals, default_charset());
  return str;
}


my_decimal *Item_cache_real::val_decimal(my_decimal *decimal_val)
{
  assert(fixed == 1);
  double2my_decimal(E_DEC_FATAL_ERROR, value, decimal_val);
  return decimal_val;
}
