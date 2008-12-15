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

#ifndef DRIZZLED_SERVER_ITEM_FUNC_H
#define DRIZZLED_SERVER_ITEM_FUNC_H

/* Function items used by mysql */


#ifdef HAVE_IEEEFP_H
extern "C"				/* Bug in BSDI include file */
{
#include <ieeefp.h>
}
#endif

/* If you fix the parser to no longer create functions these can be moved to create.cc */
#include <drizzled/function/ascii.h>
#include <drizzled/function/divide.h>
#include <drizzled/function/get_user_var.h>
#include <drizzled/function/int.h>
#include <drizzled/function/int_divide.h>
#include <drizzled/function/minus.h>
#include <drizzled/function/mod.h>
#include <drizzled/function/multiply.h>
#include <drizzled/function/neg.h>
#include <drizzled/function/plus.h>
#include <drizzled/function/rollup_const.h>
#include <drizzled/function/round.h>
#include <drizzled/function/user_var_as_out_param.h>

/* For type casts */

enum Cast_target
{
  ITEM_CAST_BINARY, ITEM_CAST_SIGNED_INT, ITEM_CAST_UNSIGNED_INT,
  ITEM_CAST_DATE, ITEM_CAST_TIME, ITEM_CAST_DATETIME, ITEM_CAST_CHAR,
  ITEM_CAST_DECIMAL
};

#endif /* DRIZZLE_SERVER_ITEM_FUNC_H */
