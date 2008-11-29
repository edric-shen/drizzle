/* - mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
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

/*
 * Work around Sun Studio not supporting all of C99 math in C++
 */

#include <drizzled/global.h>
#if defined(NEED_ISNAN) || defined(NEED_ISINF) || defined(NEED_ISFINITE)
# if defined(HAVE_IEEEFP_H)
#  include <ieeefp.h>
# endif
#endif

#if defined(NEED_ISNAN)
int isnan (double a)
{
  return isnand(a);
}
#endif /* defined(NEED_ISNAN) */

#if defined(NEED_ISINF)
int isinf(double a)
{
  fpclass_t fc= fpclass(a);
  return ((fc==FP_NINF)||(fc==FP_PINF)) ? 0 : 1;
}
#endif /* !defined(NEED_ISINF) */

#if defined(NEED_ISFINITE)
int isfinite(double a)
{
  fpclass_t fc= fpclass(a);
  return ((fc!=FP_NINF)&&(fc!=FP_PINF)) ? 0 : 1;
}
#endif /* !defined(NEED_ISFINITE) */