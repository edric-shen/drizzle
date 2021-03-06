/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems, Inc.
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

#pragma once

#include <drizzled/function/numhybrid.h>

namespace drizzled
{

/* Base class for operations like '+', '-', '*' */

class Item_num_op :public Item_func_numhybrid
{
 public:
  Item_num_op(Item *a,Item *b) :Item_func_numhybrid(a, b) {}
  virtual void result_precision()= 0;

  virtual inline void print(String *str)
  {
    print_op(str);
  }

  void find_num_type();
  String *str_op(String *)
  { assert(0); return 0; }
};

} /* namespace drizzled */

