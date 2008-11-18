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

#ifndef DRIZZLED_FUNCTIONS_TIME_YEAR_H
#define DRIZZLED_FUNCTIONS_TIME_YEAR_H

#include <drizzled/functions/int.h>

class Item_func_year :public Item_int_func
{
public:
  Item_func_year(Item *a) :Item_int_func(a) {}
  int64_t val_int();
  const char *func_name() const { return "year"; }
  enum_monotonicity_info get_monotonicity_info() const;
  int64_t val_int_endpoint(bool left_endp, bool *incl_endp);
  void fix_length_and_dec()
  {
    decimals=0;
    max_length=4*MY_CHARSET_BIN_MB_MAXLEN;
    maybe_null=1;
  }
};

#endif /* DRIZZLED_FUNCTIONS_TIME_YEAR_H */
