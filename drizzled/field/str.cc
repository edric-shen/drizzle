/* - mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 MySQL
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


#include <drizzled/server_includes.h>
#include <drizzled/field/str.h>
#include <drizzled/error.h>
#include <drizzled/table.h>
#include <drizzled/session.h>


Field_str::Field_str(unsigned char *ptr_arg,uint32_t len_arg,
                     unsigned char *null_ptr_arg,
                     unsigned char null_bit_arg, utype unireg_check_arg,
                     const char *field_name_arg,
                     const CHARSET_INFO * const charset_arg)
  :Field(ptr_arg, len_arg, null_ptr_arg, null_bit_arg,
         unireg_check_arg, field_name_arg)
{
  field_charset= charset_arg;
  if (charset_arg->state & MY_CS_BINSORT)
    flags|=BINARY_FLAG;
  field_derivation= DERIVATION_IMPLICIT;
}

/*
  Check if we lost any important data and send a truncation error/warning

  SYNOPSIS
    Field_str::report_if_important_data()
    ptr                      - Truncated rest of string
    end                      - End of truncated string

  RETURN VALUES
    0   - None was truncated (or we don't count cut fields)
    2   - Some bytes was truncated

  NOTE
    Check if we lost any important data (anything in a binary string,
    or any non-space in others). If only trailing spaces was lost,
    send a truncation note, otherwise send a truncation error.
*/

int
Field_str::report_if_important_data(const char *field_ptr, const char *end)
{
  if ((field_ptr < end) && table->in_use->count_cuted_fields)
  {
    if (test_if_important_data(field_charset, field_ptr, end))
    {
      if (table->in_use->abort_on_warning)
        set_warning(DRIZZLE_ERROR::WARN_LEVEL_ERROR, ER_DATA_TOO_LONG, 1);
      else
        set_warning(DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_TRUNCATED, 1);
    }
    else /* If we lost only spaces then produce a NOTE, not a WARNING */
      set_warning(DRIZZLE_ERROR::WARN_LEVEL_NOTE, ER_WARN_DATA_TRUNCATED, 1);
    return 2;
  }
  return 0;
}

/**
  Decimal representation of Field_str.

  @param d         value for storing

  @note
    Field_str is the base class for fields like Field_enum,
    Field_date and some similar. Some dates use fraction and also
    string value should be converted to floating point value according
    our rules, so we use double to store value of decimal in string.

  @todo
    use decimal2string?

  @retval
    0     OK
  @retval
    !=0  error
*/

int Field_str::store_decimal(const my_decimal *d)
{
  char buff[DECIMAL_MAX_STR_LENGTH+1];
  String str(buff, sizeof(buff), &my_charset_bin);
  my_decimal2string(E_DEC_FATAL_ERROR, d, 0, 0, 0, &str);
  return store(str.ptr(), str.length(), str.charset());
}

my_decimal *Field_str::val_decimal(my_decimal *decimal_value)
{
  int64_t nr= val_int();
  int2my_decimal(E_DEC_FATAL_ERROR, nr, 0, decimal_value);
  return decimal_value;
}

/**
  Store double value in Field_varstring.

  Pretty prints double number into field_length characters buffer.

  @param nr            number
*/

int Field_str::store(double nr)
{
  char buff[DOUBLE_TO_STRING_CONVERSION_BUFFER_SIZE];
  uint32_t local_char_length= field_length / charset()->mbmaxlen;
  size_t length;
  bool error;

  length= my_gcvt(nr, MY_GCVT_ARG_DOUBLE, local_char_length, buff, &error);
  if (error)
  {
    if (table->in_use->abort_on_warning)
      set_warning(DRIZZLE_ERROR::WARN_LEVEL_ERROR, ER_DATA_TOO_LONG, 1);
    else
      set_warning(DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_TRUNCATED, 1);
  }
  return store(buff, length, charset());
}

/* If one of the fields is binary and the other one isn't return 1 else 0 */

bool Field_str::compare_str_field_flags(Create_field *new_field_ptr,
                                        uint32_t flag_arg)
{
  return (((new_field_ptr->flags & (BINCMP_FLAG | BINARY_FLAG)) &&
          !(flag_arg & (BINCMP_FLAG | BINARY_FLAG))) ||
         (!(new_field_ptr->flags & (BINCMP_FLAG | BINARY_FLAG)) &&
          (flag_arg & (BINCMP_FLAG | BINARY_FLAG))));
}


uint32_t Field_str::is_equal(Create_field *new_field_ptr)
{
  if (compare_str_field_flags(new_field_ptr, flags))
    return 0;

  return ((new_field_ptr->sql_type == real_type()) &&
          new_field_ptr->charset == field_charset &&
          new_field_ptr->length == max_display_length());
}


bool check_string_copy_error(Field_str *field,
                             const char *well_formed_error_pos,
                             const char *cannot_convert_error_pos,
                             const char *end,
                             const CHARSET_INFO * const cs)
{
  const char *pos, *end_orig;
  char tmp[64], *t;

  if (!(pos= well_formed_error_pos) &&
      !(pos= cannot_convert_error_pos))
    return false;

  end_orig= end;
  set_if_smaller(end, pos + 6);

  for (t= tmp; pos < end; pos++)
  {
    /*
      If the source string is ASCII compatible (mbminlen==1)
      and the source character is in ASCII printable range (0x20..0x7F),
      then display the character as is.

      Otherwise, if the source string is not ASCII compatible (e.g. UCS2),
      or the source character is not in the printable range,
      then print the character using HEX notation.
    */
    if (((unsigned char) *pos) >= 0x20 &&
        ((unsigned char) *pos) <= 0x7F &&
        cs->mbminlen == 1)
    {
      *t++= *pos;
    }
    else
    {
      *t++= '\\';
      *t++= 'x';
      *t++= _dig_vec_upper[((unsigned char) *pos) >> 4];
      *t++= _dig_vec_upper[((unsigned char) *pos) & 15];
    }
  }
  if (end_orig > end)
  {
    *t++= '.';
    *t++= '.';
    *t++= '.';
  }
  *t= '\0';
  push_warning_printf(field->table->in_use,
                      field->table->in_use->abort_on_warning ?
                      DRIZZLE_ERROR::WARN_LEVEL_ERROR :
                      DRIZZLE_ERROR::WARN_LEVEL_WARN,
                      ER_TRUNCATED_WRONG_VALUE_FOR_FIELD,
                      ER(ER_TRUNCATED_WRONG_VALUE_FOR_FIELD),
                      "string", tmp, field->field_name,
                      (uint32_t) field->table->in_use->row_count);
  return true;
}

uint32_t Field_str::max_data_length() const
{
  return field_length + (field_length > 255 ? 2 : 1);
}
