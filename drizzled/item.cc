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
#include CMATH_H

#include <drizzled/sql_select.h>
#include <drizzled/error.h>
#include <drizzled/show.h>
#include <drizzled/item/cmpfunc.h>
#include <drizzled/item/cache_row.h>
#include <drizzled/item/type_holder.h>
#include <drizzled/item/sum.h>
#include <drizzled/item/copy_string.h>
#include <drizzled/function/str/conv_charset.h>
#include <drizzled/virtual_column_info.h>
#include <drizzled/sql_base.h>


#include <drizzled/field/str.h>
#include <drizzled/field/longstr.h>
#include <drizzled/field/num.h>
#include <drizzled/field/blob.h>
#include <drizzled/field/enum.h>
#include <drizzled/field/null.h>
#include <drizzled/field/date.h>
#include <drizzled/field/decimal.h>
#include <drizzled/field/real.h>
#include <drizzled/field/double.h>
#include <drizzled/field/long.h>
#include <drizzled/field/int64_t.h>
#include <drizzled/field/num.h>
#include <drizzled/field/timetype.h>
#include <drizzled/field/timestamp.h>
#include <drizzled/field/datetime.h>
#include <drizzled/field/varstring.h>


#if defined(CMATH_NAMESPACE)
using namespace CMATH_NAMESPACE;
#endif

const String my_null_string("NULL", 4, default_charset_info);

/*****************************************************************************
** Item functions
*****************************************************************************/

/**
  Init all special items.
*/

void item_init(void)
{
}


bool Item::is_expensive_processor(unsigned char *)
{
  return false;
}

void Item::fix_after_pullout(st_select_lex *, Item **)
{}


Field *Item::tmp_table_field(Table *)
{
  return NULL;
}


const char *Item::full_name(void) const
{
  return name ? name : "???";
}


int64_t Item::val_int_endpoint(bool, bool *)
{
  assert(0);
  return 0;
}


/**
  @todo
    Make this functions class dependent
*/

bool Item::val_bool()
{
  switch(result_type()) {
  case INT_RESULT:
    return val_int() != 0;
  case DECIMAL_RESULT:
  {
    my_decimal decimal_value;
    my_decimal *val= val_decimal(&decimal_value);
    if (val)
      return !my_decimal_is_zero(val);
    return false;
  }
  case REAL_RESULT:
  case STRING_RESULT:
    return val_real() != 0.0;
  case ROW_RESULT:
  default:
    assert(0);
    return false;                                   // Wrong (but safe)
  }
}


String *Item::val_string_from_real(String *str)
{
  double nr= val_real();
  if (null_value)
    return NULL;					/* purecov: inspected */

  str->set_real(nr,decimals, &my_charset_bin);
  return str;
}


String *Item::val_string_from_int(String *str)
{
  int64_t nr= val_int();
  if (null_value)
    return NULL;

  str->set_int(nr, unsigned_flag, &my_charset_bin);
  return str;
}


String *Item::val_string_from_decimal(String *str)
{
  my_decimal dec_buf, *dec= val_decimal(&dec_buf);
  if (null_value)
    return NULL;

  my_decimal_round(E_DEC_FATAL_ERROR, dec, decimals, false, &dec_buf);
  my_decimal2string(E_DEC_FATAL_ERROR, &dec_buf, 0, 0, 0, str);
  return str;
}


my_decimal *Item::val_decimal_from_real(my_decimal *decimal_value)
{
  double nr= val_real();
  if (null_value)
    return NULL;

  double2my_decimal(E_DEC_FATAL_ERROR, nr, decimal_value);
  return (decimal_value);
}


my_decimal *Item::val_decimal_from_int(my_decimal *decimal_value)
{
  int64_t nr= val_int();
  if (null_value)
    return NULL;

  int2my_decimal(E_DEC_FATAL_ERROR, nr, unsigned_flag, decimal_value);
  return decimal_value;
}


my_decimal *Item::val_decimal_from_string(my_decimal *decimal_value)
{
  String *res;
  char *end_ptr;
  if (!(res= val_str(&str_value)))
    return NULL;                                   // NULL or EOM

  end_ptr= (char*) res->ptr()+ res->length();
  if (str2my_decimal(E_DEC_FATAL_ERROR & ~E_DEC_BAD_NUM,
                     res->ptr(), res->length(), res->charset(),
                     decimal_value) & E_DEC_BAD_NUM)
  {
    push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                        ER_TRUNCATED_WRONG_VALUE,
                        ER(ER_TRUNCATED_WRONG_VALUE), "DECIMAL",
                        str_value.c_ptr());
  }
  return decimal_value;
}


my_decimal *Item::val_decimal_from_date(my_decimal *decimal_value)
{
  assert(fixed == 1);
  DRIZZLE_TIME ltime;
  if (get_date(&ltime, TIME_FUZZY_DATE))
  {
    my_decimal_set_zero(decimal_value);
    null_value= 1;                               // set NULL, stop processing
    return NULL;
  }
  return date2my_decimal(&ltime, decimal_value);
}


my_decimal *Item::val_decimal_from_time(my_decimal *decimal_value)
{
  assert(fixed == 1);
  DRIZZLE_TIME ltime;
  if (get_time(&ltime))
  {
    my_decimal_set_zero(decimal_value);
    return NULL;
  }
  return date2my_decimal(&ltime, decimal_value);
}


double Item::val_real_from_decimal()
{
  /* Note that fix_fields may not be called for Item_avg_field items */
  double result;
  my_decimal value_buff, *dec_val= val_decimal(&value_buff);
  if (null_value)
    return 0.0;
  my_decimal2double(E_DEC_FATAL_ERROR, dec_val, &result);
  return result;
}


int64_t Item::val_int_from_decimal()
{
  /* Note that fix_fields may not be called for Item_avg_field items */
  int64_t result;
  my_decimal value, *dec_val= val_decimal(&value);
  if (null_value)
    return 0;
  my_decimal2int(E_DEC_FATAL_ERROR, dec_val, unsigned_flag, &result);
  return result;
}

int Item::save_time_in_field(Field *field)
{
  DRIZZLE_TIME ltime;
  if (get_time(&ltime))
    return set_field_to_null(field);
  field->set_notnull();
  return field->store_time(&ltime, DRIZZLE_TIMESTAMP_TIME);
}


int Item::save_date_in_field(Field *field)
{
  DRIZZLE_TIME ltime;
  if (get_date(&ltime, TIME_FUZZY_DATE))
    return set_field_to_null(field);
  field->set_notnull();
  return field->store_time(&ltime, DRIZZLE_TIMESTAMP_DATETIME);
}


/*
  Store the string value in field directly

  SYNOPSIS
    Item::save_str_value_in_field()
    field   a pointer to field where to store
    result  the pointer to the string value to be stored

  DESCRIPTION
    The method is used by Item_*::save_in_field implementations
    when we don't need to calculate the value to store
    See Item_string::save_in_field() implementation for example

  IMPLEMENTATION
    Check if the Item is null and stores the NULL or the
    result value in the field accordingly.

  RETURN
    Nonzero value if error
*/

int Item::save_str_value_in_field(Field *field, String *result)
{
  if (null_value)
    return set_field_to_null(field);
  field->set_notnull();
  return field->store(result->ptr(), result->length(),
		      collation.collation);
}


Item::Item():
  is_expensive_cache(-1), name(0), orig_name(0), name_length(0),
  fixed(0), is_autogenerated_name(true),
  collation(&my_charset_bin, DERIVATION_COERCIBLE)
{
  marker= 0;
  maybe_null= false;
  null_value= false;
  with_sum_func= false;
  unsigned_flag= false;
  decimals= 0;
  max_length= 0;
  with_subselect= 0;
  cmp_context= (Item_result)-1;

  /* Put item in free list so that we can free all items at end */
  Session *session= current_session;
  next= session->free_list;
  session->free_list= this;
  /*
    Item constructor can be called during execution other then SQL_COM
    command => we should check session->lex->current_select on zero (session->lex
    can be uninitialised)
  */
  if (session->lex->current_select)
  {
    enum_parsing_place place=
      session->lex->current_select->parsing_place;
    if (place == SELECT_LIST ||
	place == IN_HAVING)
      session->lex->current_select->select_n_having_items++;
  }
}

/**
  Constructor used by Item_field, Item_ref & aggregate (sum)
  functions.

  Used for duplicating lists in processing queries with temporary
  tables.
*/
Item::Item(Session *session, Item *item):
  is_expensive_cache(-1),
  str_value(item->str_value),
  name(item->name),
  orig_name(item->orig_name),
  max_length(item->max_length),
  marker(item->marker),
  decimals(item->decimals),
  maybe_null(item->maybe_null),
  null_value(item->null_value),
  unsigned_flag(item->unsigned_flag),
  with_sum_func(item->with_sum_func),
  fixed(item->fixed),
  collation(item->collation),
  cmp_context(item->cmp_context)
{
  next= session->free_list;				// Put in free list
  session->free_list= this;
}


uint32_t Item::decimal_precision() const
{
  Item_result restype= result_type();

  if ((restype == DECIMAL_RESULT) || (restype == INT_RESULT))
    return cmin(my_decimal_length_to_precision(max_length, decimals, unsigned_flag),
               (unsigned int)DECIMAL_MAX_PRECISION);
  return cmin(max_length, (uint32_t)DECIMAL_MAX_PRECISION);
}


int Item::decimal_int_part() const
{
  return my_decimal_int_part(decimal_precision(), decimals);
}


void Item::print(String *str, enum_query_type)
{
  str->append(full_name());
}


void Item::print_item_w_name(String *str, enum_query_type query_type)
{
  print(str, query_type);

  if (name)
  {
    str->append(STRING_WITH_LEN(" AS "));
    str->append_identifier(name, (uint) strlen(name));
  }
}


void Item::split_sum_func(Session *, Item **, List<Item> &)
{}


void Item::cleanup()
{
  fixed=0;
  marker= 0;
  if (orig_name)
    name= orig_name;
  return;
}


/**
  cleanup() item if it is 'fixed'.

  @param arg   a dummy parameter, is not used here
*/

bool Item::cleanup_processor(unsigned char *)
{
  if (fixed)
    cleanup();
  return false;
}


/**
  rename item (used for views, cleanup() return original name).

  @param new_name	new name of item;
*/

void Item::rename(char *new_name)
{
  /*
    we can compare pointers to names here, because if name was not changed,
    pointer will be same
  */
  if (!orig_name && new_name != name)
    orig_name= name;
  name= new_name;
}


/**
  Traverse item tree possibly transforming it (replacing items).

  This function is designed to ease transformation of Item trees.
  Re-execution note: every such transformation is registered for
  rollback by Session::change_item_tree() and is rolled back at the end
  of execution by Session::rollback_item_tree_changes().

  Therefore:
  - this function can not be used at prepared statement prepare
  (in particular, in fix_fields!), as only permanent
  transformation of Item trees are allowed at prepare.
  - the transformer function shall allocate new Items in execution
  memory root (session->mem_root) and not anywhere else: allocated
  items will be gone in the end of execution.

  If you don't need to transform an item tree, but only traverse
  it, please use Item::walk() instead.


  @param transformer    functor that performs transformation of a subtree
  @param arg            opaque argument passed to the functor

  @return
    Returns pointer to the new subtree root.  Session::change_item_tree()
    should be called for it if transformation took place, i.e. if a
    pointer to newly allocated item is returned.
*/

Item* Item::transform(Item_transformer transformer, unsigned char *arg)
{
  return (this->*transformer)(arg);
}


bool Item::check_cols(uint32_t c)
{
  if (c != 1)
  {
    my_error(ER_OPERAND_COLUMNS, MYF(0), c);
    return 1;
  }
  return false;
}


void Item::set_name(const char *str, uint32_t length, const CHARSET_INFO * const cs)
{
  if (!length)
  {
    /* Empty string, used by AS or internal function like last_insert_id() */
    name= (char*) str;
    name_length= 0;
    return;
  }
  if (cs->ctype)
  {
    uint32_t orig_len= length;
    /*
      This will probably need a better implementation in the future:
      a function in CHARSET_INFO structure.
    */
    while (length && !my_isgraph(cs,*str))
    {						// Fix problem with yacc
      length--;
      str++;
    }
    if (orig_len != length && !is_autogenerated_name)
    {
      if (length == 0)
        push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                            ER_NAME_BECOMES_EMPTY, ER(ER_NAME_BECOMES_EMPTY),
                            str + length - orig_len);
      else
        push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                            ER_REMOVED_SPACES, ER(ER_REMOVED_SPACES),
                            str + length - orig_len);
    }
  }
  if (!my_charset_same(cs, system_charset_info))
  {
    size_t res_length;
    name= sql_strmake_with_convert(str, name_length= length, cs,
				   MAX_ALIAS_NAME, system_charset_info,
				   &res_length);
  }
  else
    name= sql_strmake(str, (name_length= cmin(length,(unsigned int)MAX_ALIAS_NAME)));
}


/**
  @details
  This function is called when:
  - Comparing items in the WHERE clause (when doing where optimization)
  - When trying to find an order_st BY/GROUP BY item in the SELECT part
*/

bool Item::eq(const Item *item, bool) const
{
  /*
    Note, that this is never true if item is a Item_param:
    for all basic constants we have special checks, and Item_param's
    type() can be only among basic constant types.
  */
  return type() == item->type() && name && item->name &&
    !my_strcasecmp(system_charset_info,name,item->name);
}


Item *Item::safe_charset_converter(const CHARSET_INFO * const tocs)
{
  Item_func_conv_charset *conv= new Item_func_conv_charset(this, tocs, 1);
  return conv->safe ? conv : NULL;
}


/**
  Get the value of the function as a DRIZZLE_TIME structure.
  As a extra convenience the time structure is reset on error!
*/

bool Item::get_date(DRIZZLE_TIME *ltime,uint32_t fuzzydate)
{
  if (result_type() == STRING_RESULT)
  {
    char buff[40];
    String tmp(buff,sizeof(buff), &my_charset_bin),*res;
    if (!(res=val_str(&tmp)) ||
        str_to_datetime_with_warn(res->ptr(), res->length(),
                                  ltime, fuzzydate) <= DRIZZLE_TIMESTAMP_ERROR)
      goto err;
  }
  else
  {
    int64_t value= val_int();
    int was_cut;
    if (number_to_datetime(value, ltime, fuzzydate, &was_cut) == -1L)
    {
      char buff[22], *end;
      end= int64_t10_to_str(value, buff, -10);
      make_truncated_value_warning(current_session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
                                   buff, (int) (end-buff), DRIZZLE_TIMESTAMP_NONE,
                                   NULL);
      goto err;
    }
  }
  return false;

err:
  memset(ltime, 0, sizeof(*ltime));
  return true;
}

/**
  Get time of first argument.\

  As a extra convenience the time structure is reset on error!
*/

bool Item::get_time(DRIZZLE_TIME *ltime)
{
  char buff[40];
  String tmp(buff,sizeof(buff),&my_charset_bin),*res;
  if (!(res=val_str(&tmp)) ||
      str_to_time_with_warn(res->ptr(), res->length(), ltime))
  {
    memset(ltime, 0, sizeof(*ltime));
    return true;
  }
  return false;
}


bool Item::get_date_result(DRIZZLE_TIME *ltime,uint32_t fuzzydate)
{
  return get_date(ltime,fuzzydate);
}


bool Item::is_null()
{
  return false;
}


void Item::update_null_value ()
{
  (void) val_int();
}


void Item::top_level_item(void)
{}


void Item::set_result_field(Field *)
{}


bool Item::is_result_field(void)
{
  return false;
}


bool Item::is_bool_func(void)
{
  return false;
}


void Item::save_in_result_field(bool)
{}


void Item::no_rows_in_result(void)
{}


Item *Item::copy_or_same(Session *)
{
  return this;
}


Item *Item::copy_andor_structure(Session *)
{
  return this;
}


Item *Item::real_item(void)
{
  return this;
}


Item *Item::get_tmp_table_item(Session *session)
{
  return copy_or_same(session);
}


const CHARSET_INFO *Item::default_charset()
{
  return current_session->variables.getCollation();
}


const CHARSET_INFO *Item::compare_collation()
{
  return NULL;
}


bool Item::walk(Item_processor processor, bool, unsigned char *arg)
{
  return (this->*processor)(arg);
}


Item* Item::compile(Item_analyzer analyzer, unsigned char **arg_p,
                    Item_transformer transformer, unsigned char *arg_t)
{
  if ((this->*analyzer) (arg_p))
    return ((this->*transformer) (arg_t));
  return NULL;
}


void Item::traverse_cond(Cond_traverser traverser, void *arg, traverse_order)
{
  (*traverser)(this, arg);
}


bool Item::remove_dependence_processor(unsigned char *)
{
  return false;
}


bool Item::remove_fixed(unsigned char *)
{
  fixed= 0;
  return false;
}


bool Item::collect_item_field_processor(unsigned char *)
{
  return false;
}


bool Item::find_item_in_field_list_processor(unsigned char *)
{
  return false;
}


bool Item::change_context_processor(unsigned char *)
{
  return false;
}

bool Item::reset_query_id_processor(unsigned char *)
{
  return false;
}


bool Item::register_field_in_read_map(unsigned char *)
{
  return false;
}


bool Item::register_field_in_bitmap(unsigned char *)
{
  return false;
}


bool Item::subst_argument_checker(unsigned char **arg)
{
  if (*arg)
    *arg= NULL;
  return true;
}


bool Item::check_vcol_func_processor(unsigned char *)
{
  return true;
}


Item *Item::equal_fields_propagator(unsigned char *)
{
  return this;
}


bool Item::set_no_const_sub(unsigned char *)
{
  return false;
}


Item *Item::replace_equal_field(unsigned char *)
{
  return this;
}


uint32_t Item::cols()
{
  return 1;
}


Item* Item::element_index(uint32_t)
{
  return this;
}


Item** Item::addr(uint32_t)
{
  return NULL;
}


bool Item::null_inside()
{
  return false;
}


void Item::bring_value()
{}


Item *Item::neg_transformer(Session *)
{
  return NULL;
}


Item *Item::update_value_transformer(unsigned char *)
{
  return this;
}


void Item::delete_self()
{
  cleanup();
  delete this;
}

bool Item::result_as_int64_t()
{
  return false;
}


bool Item::is_expensive()
{
  if (is_expensive_cache < 0)
    is_expensive_cache= walk(&Item::is_expensive_processor, 0,
                             (unsigned char*)0);
  return test(is_expensive_cache);
}


int Item::save_in_field_no_warnings(Field *field, bool no_conversions)
{
  int res;
  Table *table= field->table;
  Session *session= table->in_use;
  enum_check_fields tmp= session->count_cuted_fields;
  ulong sql_mode= session->variables.sql_mode;
  session->variables.sql_mode&= ~(MODE_NO_ZERO_DATE);
  session->count_cuted_fields= CHECK_FIELD_IGNORE;
  res= save_in_field(field, no_conversions);
  session->count_cuted_fields= tmp;
  session->variables.sql_mode= sql_mode;
  return res;
}


/*
 need a special class to adjust printing : references to aggregate functions
 must not be printed as refs because the aggregate functions that are added to
 the front of select list are not printed as well.
*/
class Item_aggregate_ref : public Item_ref
{
public:
  Item_aggregate_ref(Name_resolution_context *context_arg, Item **item,
                  const char *table_name_arg, const char *field_name_arg)
    :Item_ref(context_arg, item, table_name_arg, field_name_arg) {}

  virtual inline void print (String *str, enum_query_type query_type)
  {
    if (ref)
      (*ref)->print(str, query_type);
    else
      Item_ident::print(str, query_type);
  }
};


/**
  Move SUM items out from item tree and replace with reference.

  @param session			Thread handler
  @param ref_pointer_array	Pointer to array of reference fields
  @param fields		All fields in select
  @param ref			Pointer to item
  @param skip_registered       <=> function be must skipped for registered
                               SUM items

  @note
    This is from split_sum_func() for items that should be split

    All found SUM items are added FIRST in the fields list and
    we replace the item with a reference.

    session->fatal_error() may be called if we are out of memory
*/

void Item::split_sum_func(Session *session, Item **ref_pointer_array,
                          List<Item> &fields, Item **ref,
                          bool skip_registered)
{
  /* An item of type Item_sum  is registered <=> ref_by != 0 */
  if (type() == SUM_FUNC_ITEM && skip_registered &&
      ((Item_sum *) this)->ref_by)
    return;
  if ((type() != SUM_FUNC_ITEM && with_sum_func) ||
      (type() == FUNC_ITEM &&
       (((Item_func *) this)->functype() == Item_func::ISNOTNULLTEST_FUNC ||
        ((Item_func *) this)->functype() == Item_func::TRIG_COND_FUNC)))
  {
    /* Will split complicated items and ignore simple ones */
    split_sum_func(session, ref_pointer_array, fields);
  }
  else if ((type() == SUM_FUNC_ITEM || (used_tables() & ~PARAM_TABLE_BIT)) &&
           type() != SUBSELECT_ITEM &&
           type() != REF_ITEM)
  {
    /*
      Replace item with a reference so that we can easily calculate
      it (in case of sum functions) or copy it (in case of fields)

      The test above is to ensure we don't do a reference for things
      that are constants (PARAM_TABLE_BIT is in effect a constant)
      or already referenced (for example an item in HAVING)
      Exception is Item_direct_view_ref which we need to convert to
      Item_ref to allow fields from view being stored in tmp table.
    */
    Item_aggregate_ref *item_ref;
    uint32_t el= fields.elements;
    Item *real_itm= real_item();

    ref_pointer_array[el]= real_itm;
    if (!(item_ref= new Item_aggregate_ref(&session->lex->current_select->context,
                                           ref_pointer_array + el, 0, name)))
      return;                                   // fatal_error is set
    if (type() == SUM_FUNC_ITEM)
      item_ref->depended_from= ((Item_sum *) this)->depended_from();
    fields.push_front(real_itm);
    session->change_item_tree(ref, item_ref);
  }
}

/*
  Functions to convert item to field (for send_fields)
*/

/* ARGSUSED */
bool Item::fix_fields(Session *, Item **)
{

  // We do not check fields which are fixed during construction
  assert(fixed == 0 || basic_const_item());
  fixed= 1;
  return false;
}


/**
  Mark item and SELECT_LEXs as dependent if item was resolved in
  outer SELECT.

  @param session             thread handler
  @param last            select from which current item depend
  @param current         current select
  @param resolved_item   item which was resolved in outer SELECT(for warning)
  @param mark_item       item which should be marked (can be differ in case of
                         substitution)
*/

void mark_as_dependent(Session *session, SELECT_LEX *last, SELECT_LEX *current,
                              Item_ident *resolved_item,
                              Item_ident *mark_item)
{
  const char *db_name= (resolved_item->db_name ?
                        resolved_item->db_name : "");
  const char *table_name= (resolved_item->table_name ?
                           resolved_item->table_name : "");
  /* store pointer on SELECT_LEX from which item is dependent */
  if (mark_item)
    mark_item->depended_from= last;
  current->mark_as_dependent(last);
  if (session->lex->describe & DESCRIBE_EXTENDED)
  {
    char warn_buff[DRIZZLE_ERRMSG_SIZE];
    sprintf(warn_buff, ER(ER_WARN_FIELD_RESOLVED),
            db_name, (db_name[0] ? "." : ""),
            table_name, (table_name [0] ? "." : ""),
            resolved_item->field_name,
	    current->select_number, last->select_number);
    push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_NOTE,
		 ER_WARN_FIELD_RESOLVED, warn_buff);
  }
}


/**
  Mark range of selects and resolved identifier (field/reference)
  item as dependent.

  @param session             thread handler
  @param last_select     select where resolved_item was resolved
  @param current_sel     current select (select where resolved_item was placed)
  @param found_field     field which was found during resolving
  @param found_item      Item which was found during resolving (if resolved
                         identifier belongs to VIEW)
  @param resolved_item   Identifier which was resolved

  @note
    We have to mark all items between current_sel (including) and
    last_select (excluding) as dependend (select before last_select should
    be marked with actual table mask used by resolved item, all other with
    OUTER_REF_TABLE_BIT) and also write dependence information to Item of
    resolved identifier.
*/

void mark_select_range_as_dependent(Session *session,
                                    SELECT_LEX *last_select,
                                    SELECT_LEX *current_sel,
                                    Field *found_field, Item *found_item,
                                    Item_ident *resolved_item)
{
  /*
    Go from current SELECT to SELECT where field was resolved (it
    have to be reachable from current SELECT, because it was already
    done once when we resolved this field and cached result of
    resolving)
  */
  SELECT_LEX *previous_select= current_sel;
  for (; previous_select->outer_select() != last_select;
       previous_select= previous_select->outer_select())
  {
    Item_subselect *prev_subselect_item=
      previous_select->master_unit()->item;
    prev_subselect_item->used_tables_cache|= OUTER_REF_TABLE_BIT;
    prev_subselect_item->const_item_cache= 0;
  }
  {
    Item_subselect *prev_subselect_item=
      previous_select->master_unit()->item;
    Item_ident *dependent= resolved_item;
    if (found_field == view_ref_found)
    {
      Item::Type type= found_item->type();
      prev_subselect_item->used_tables_cache|=
        found_item->used_tables();
      dependent= ((type == Item::REF_ITEM || type == Item::FIELD_ITEM) ?
                  (Item_ident*) found_item :
                  0);
    }
    else
      prev_subselect_item->used_tables_cache|=
        found_field->table->map;
    prev_subselect_item->const_item_cache= 0;
    mark_as_dependent(session, last_select, current_sel, resolved_item,
                      dependent);
  }
}


/**
  Search a GROUP BY clause for a field with a certain name.

  Search the GROUP BY list for a column named as find_item. When searching
  preference is given to columns that are qualified with the same table (and
  database) name as the one being searched for.

  @param find_item     the item being searched for
  @param group_list    GROUP BY clause

  @return
    - the found item on success
    - NULL if find_item is not in group_list
*/

static Item** find_field_in_group_list(Item *find_item, order_st *group_list)
{
  const char *db_name;
  const char *table_name;
  const char *field_name;
  order_st      *found_group= NULL;
  int         found_match_degree= 0;
  Item_ident *cur_field;
  int         cur_match_degree= 0;
  char        name_buff[NAME_LEN+1];

  if (find_item->type() == Item::FIELD_ITEM ||
      find_item->type() == Item::REF_ITEM)
  {
    db_name=    ((Item_ident*) find_item)->db_name;
    table_name= ((Item_ident*) find_item)->table_name;
    field_name= ((Item_ident*) find_item)->field_name;
  }
  else
    return NULL;

  if (db_name && lower_case_table_names)
  {
    /* Convert database to lower case for comparison */
    strncpy(name_buff, db_name, sizeof(name_buff)-1);
    my_casedn_str(files_charset_info, name_buff);
    db_name= name_buff;
  }

  assert(field_name != 0);

  for (order_st *cur_group= group_list ; cur_group ; cur_group= cur_group->next)
  {
    if ((*(cur_group->item))->real_item()->type() == Item::FIELD_ITEM)
    {
      cur_field= (Item_ident*) *cur_group->item;
      cur_match_degree= 0;

      assert(cur_field->field_name != 0);

      if (!my_strcasecmp(system_charset_info,
                         cur_field->field_name, field_name))
        ++cur_match_degree;
      else
        continue;

      if (cur_field->table_name && table_name)
      {
        /* If field_name is qualified by a table name. */
        if (my_strcasecmp(table_alias_charset, cur_field->table_name, table_name))
          /* Same field names, different tables. */
          return NULL;

        ++cur_match_degree;
        if (cur_field->db_name && db_name)
        {
          /* If field_name is also qualified by a database name. */
          if (strcmp(cur_field->db_name, db_name))
            /* Same field names, different databases. */
            return NULL;
          ++cur_match_degree;
        }
      }

      if (cur_match_degree > found_match_degree)
      {
        found_match_degree= cur_match_degree;
        found_group= cur_group;
      }
      else if (found_group && (cur_match_degree == found_match_degree) &&
               ! (*(found_group->item))->eq(cur_field, 0))
      {
        /*
          If the current resolve candidate matches equally well as the current
          best match, they must reference the same column, otherwise the field
          is ambiguous.
        */
        my_error(ER_NON_UNIQ_ERROR, MYF(0),
                 find_item->full_name(), current_session->where);
        return NULL;
      }
    }
  }

  if (found_group)
    return found_group->item;
  else
    return NULL;
}


/**
  Resolve a column reference in a sub-select.

  Resolve a column reference (usually inside a HAVING clause) against the
  SELECT and GROUP BY clauses of the query described by 'select'. The name
  resolution algorithm searches both the SELECT and GROUP BY clauses, and in
  case of a name conflict prefers GROUP BY column names over SELECT names. If
  both clauses contain different fields with the same names, a warning is
  issued that name of 'ref' is ambiguous. We extend ANSI SQL in that when no
  GROUP BY column is found, then a HAVING name is resolved as a possibly
  derived SELECT column. This extension is allowed only if the
  MODE_ONLY_FULL_GROUP_BY sql mode isn't enabled.

  @param session     current thread
  @param ref     column reference being resolved
  @param select  the select that ref is resolved against

  @note
    The resolution procedure is:
    - Search for a column or derived column named col_ref_i [in table T_j]
    in the SELECT clause of Q.
    - Search for a column named col_ref_i [in table T_j]
    in the GROUP BY clause of Q.
    - If found different columns with the same name in GROUP BY and SELECT
    - issue a warning and return the GROUP BY column,
    - otherwise
    - if the MODE_ONLY_FULL_GROUP_BY mode is enabled return error
    - else return the found SELECT column.


  @return
    - NULL - there was an error, and the error was already reported
    - not_found_item - the item was not resolved, no error was reported
    - resolved item - if the item was resolved
*/

Item**
resolve_ref_in_select_and_group(Session *session, Item_ident *ref, SELECT_LEX *select)
{
  Item **group_by_ref= NULL;
  Item **select_ref= NULL;
  order_st *group_list= (order_st*) select->group_list.first;
  bool ambiguous_fields= false;
  uint32_t counter;
  enum_resolution_type resolution;

  /*
    Search for a column or derived column named as 'ref' in the SELECT
    clause of the current select.
  */
  if (!(select_ref= find_item_in_list(ref, *(select->get_item_list()),
                                      &counter, REPORT_EXCEPT_NOT_FOUND,
                                      &resolution)))
    return NULL; /* Some error occurred. */
  if (resolution == RESOLVED_AGAINST_ALIAS)
    ref->alias_name_used= true;

  /* If this is a non-aggregated field inside HAVING, search in GROUP BY. */
  if (select->having_fix_field && !ref->with_sum_func && group_list)
  {
    group_by_ref= find_field_in_group_list(ref, group_list);

    /* Check if the fields found in SELECT and GROUP BY are the same field. */
    if (group_by_ref && (select_ref != not_found_item) &&
        !((*group_by_ref)->eq(*select_ref, 0)))
    {
      ambiguous_fields= true;
      push_warning_printf(session, DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_NON_UNIQ_ERROR,
                          ER(ER_NON_UNIQ_ERROR), ref->full_name(),
                          current_session->where);

    }
  }

  if (select_ref != not_found_item || group_by_ref)
  {
    if (select_ref != not_found_item && !ambiguous_fields)
    {
      assert(*select_ref != 0);
      if (!select->ref_pointer_array[counter])
      {
        my_error(ER_ILLEGAL_REFERENCE, MYF(0),
                 ref->name, "forward reference in item list");
        return NULL;
      }
      assert((*select_ref)->fixed);
      return (select->ref_pointer_array + counter);
    }
    if (group_by_ref)
      return group_by_ref;
    assert(false);
    return NULL; /* So there is no compiler warning. */
  }

  return (Item**) not_found_item;
}

void Item::init_make_field(Send_field *tmp_field,
			   enum enum_field_types field_type_arg)
{
  char *empty_name= (char*) "";
  tmp_field->db_name=		empty_name;
  tmp_field->org_table_name=	empty_name;
  tmp_field->org_col_name=	empty_name;
  tmp_field->table_name=	empty_name;
  tmp_field->col_name=		name;
  tmp_field->charsetnr=         collation.collation->number;
  tmp_field->flags=             (maybe_null ? 0 : NOT_NULL_FLAG) |
                                (my_binary_compare(collation.collation) ?
                                 BINARY_FLAG : 0);
  tmp_field->type=              field_type_arg;
  tmp_field->length=max_length;
  tmp_field->decimals=decimals;
}

void Item::make_field(Send_field *tmp_field)
{
  init_make_field(tmp_field, field_type());
}


enum_field_types Item::string_field_type() const
{
  enum_field_types f_type= DRIZZLE_TYPE_VARCHAR;
  if (max_length >= 65536)
    f_type= DRIZZLE_TYPE_BLOB;
  return f_type;
}

enum_field_types Item::field_type() const
{
  switch (result_type()) {
  case STRING_RESULT:  return string_field_type();
  case INT_RESULT:     return DRIZZLE_TYPE_LONGLONG;
  case DECIMAL_RESULT: return DRIZZLE_TYPE_NEWDECIMAL;
  case REAL_RESULT:    return DRIZZLE_TYPE_DOUBLE;
  case ROW_RESULT:
  default:
    assert(0);
    return DRIZZLE_TYPE_VARCHAR;
  }
}


bool Item::is_datetime()
{
  switch (field_type())
  {
    case DRIZZLE_TYPE_DATE:
    case DRIZZLE_TYPE_DATETIME:
    case DRIZZLE_TYPE_TIMESTAMP:
      return true;
    default:
      break;
  }
  return false;
}


String *Item::check_well_formed_result(String *str, bool send_error)
{
  /* Check whether we got a well-formed string */
  const CHARSET_INFO * const cs= str->charset();
  int well_formed_error;
  uint32_t wlen= cs->cset->well_formed_len(cs,
                                       str->ptr(), str->ptr() + str->length(),
                                       str->length(), &well_formed_error);
  if (wlen < str->length())
  {
    Session *session= current_session;
    char hexbuf[7];
    enum DRIZZLE_ERROR::enum_warning_level level;
    uint32_t diff= str->length() - wlen;
    set_if_smaller(diff, 3);
    octet2hex(hexbuf, str->ptr() + wlen, diff);
    if (send_error)
    {
      my_error(ER_INVALID_CHARACTER_STRING, MYF(0),
               cs->csname,  hexbuf);
      return NULL;
    }
    {
      level= DRIZZLE_ERROR::WARN_LEVEL_ERROR;
      null_value= 1;
      str= 0;
    }
    push_warning_printf(session, level, ER_INVALID_CHARACTER_STRING,
                        ER(ER_INVALID_CHARACTER_STRING), cs->csname, hexbuf);
  }
  return str;
}

/*
  Compare two items using a given collation

  SYNOPSIS
    eq_by_collation()
    item               item to compare with
    binary_cmp         true <-> compare as binaries
    cs                 collation to use when comparing strings

  DESCRIPTION
    This method works exactly as Item::eq if the collation cs coincides with
    the collation of the compared objects. Otherwise, first the collations that
    differ from cs are replaced for cs and then the items are compared by
    Item::eq. After the comparison the original collations of items are
    restored.

  RETURN
    1    compared items has been detected as equal
    0    otherwise
*/

bool Item::eq_by_collation(Item *item, bool binary_cmp, const CHARSET_INFO * const cs)
{
  const CHARSET_INFO *save_cs= 0;
  const CHARSET_INFO *save_item_cs= 0;
  if (collation.collation != cs)
  {
    save_cs= collation.collation;
    collation.collation= cs;
  }
  if (item->collation.collation != cs)
  {
    save_item_cs= item->collation.collation;
    item->collation.collation= cs;
  }
  bool res= eq(item, binary_cmp);
  if (save_cs)
    collation.collation= save_cs;
  if (save_item_cs)
    item->collation.collation= save_item_cs;
  return res;
}


/**
  Create a field to hold a string value from an item.

  If max_length > CONVERT_IF_BIGGER_TO_BLOB create a blob @n
  If max_length > 0 create a varchar @n
  If max_length == 0 create a CHAR(0)

  @param table		Table for which the field is created
*/

Field *Item::make_string_field(Table *table)
{
  Field *field;
  assert(collation.collation);
  if (max_length/collation.collation->mbmaxlen > CONVERT_IF_BIGGER_TO_BLOB)
    field= new Field_blob(max_length, maybe_null, name,
                          collation.collation);
  else
    field= new Field_varstring(max_length, maybe_null, name, table->s,
                               collation.collation);

  if (field)
    field->init(table);
  return field;
}


/**
  Create a field based on field_type of argument.

  For now, this is only used to create a field for
  IFNULL(x,something) and time functions

  @retval
    NULL  error
  @retval
    \#    Created field
*/

Field *Item::tmp_table_field_from_field_type(Table *table, bool)
{
  /*
    The field functions defines a field to be not null if null_ptr is not 0
  */
  unsigned char *null_ptr= maybe_null ? (unsigned char*) "" : 0;
  Field *field;

  switch (field_type()) {
  case DRIZZLE_TYPE_NEWDECIMAL:
    field= new Field_new_decimal((unsigned char*) 0, max_length, null_ptr, 0,
                                 Field::NONE, name, decimals, 0,
                                 unsigned_flag);
    break;
  case DRIZZLE_TYPE_LONG:
    field= new Field_long((unsigned char*) 0, max_length, null_ptr, 0, Field::NONE,
			  name, 0, unsigned_flag);
    break;
  case DRIZZLE_TYPE_LONGLONG:
    field= new Field_int64_t((unsigned char*) 0, max_length, null_ptr, 0, Field::NONE,
			      name, 0, unsigned_flag);
    break;
  case DRIZZLE_TYPE_DOUBLE:
    field= new Field_double((unsigned char*) 0, max_length, null_ptr, 0, Field::NONE,
			    name, decimals, 0, unsigned_flag);
    break;
  case DRIZZLE_TYPE_NULL:
    field= new Field_null((unsigned char*) 0, max_length, Field::NONE,
			  name, &my_charset_bin);
    break;
  case DRIZZLE_TYPE_DATE:
    field= new Field_date(maybe_null, name, &my_charset_bin);
    break;
  case DRIZZLE_TYPE_TIME:
    field= new Field_time(maybe_null, name, &my_charset_bin);
    break;
  case DRIZZLE_TYPE_TIMESTAMP:
    field= new Field_timestamp(maybe_null, name, &my_charset_bin);
    break;
  case DRIZZLE_TYPE_DATETIME:
    field= new Field_datetime(maybe_null, name, &my_charset_bin);
    break;
  default:
    /* This case should never be chosen */
    assert(0);
    /* Fall through to make_string_field() */
  case DRIZZLE_TYPE_ENUM:
  case DRIZZLE_TYPE_VARCHAR:
    return make_string_field(table);
  case DRIZZLE_TYPE_BLOB:
    if (this->type() == Item::TYPE_HOLDER)
      field= new Field_blob(max_length, maybe_null, name, collation.collation,
                            1);
    else
      field= new Field_blob(max_length, maybe_null, name, collation.collation);
    break;					// Blob handled outside of case
  }
  if (field)
    field->init(table);
  return field;
}


/*
  This implementation can lose str_value content, so if the
  Item uses str_value to store something, it should
  reimplement it's ::save_in_field() as Item_string, for example, does
*/

int Item::save_in_field(Field *field, bool no_conversions)
{
  int error;
  if (result_type() == STRING_RESULT)
  {
    String *result;
    const CHARSET_INFO * const cs= collation.collation;
    char buff[MAX_FIELD_WIDTH];		// Alloc buffer for small columns
    str_value.set_quick(buff, sizeof(buff), cs);
    result=val_str(&str_value);
    if (null_value)
    {
      str_value.set_quick(0, 0, cs);
      return set_field_to_null_with_conversions(field, no_conversions);
    }

    /* NOTE: If null_value == false, "result" must be not NULL.  */

    field->set_notnull();
    error=field->store(result->ptr(),result->length(),cs);
    str_value.set_quick(0, 0, cs);
  }
  else if (result_type() == REAL_RESULT &&
           field->result_type() == STRING_RESULT)
  {
    double nr= val_real();
    if (null_value)
      return set_field_to_null_with_conversions(field, no_conversions);
    field->set_notnull();
    error= field->store(nr);
  }
  else if (result_type() == REAL_RESULT)
  {
    double nr= val_real();
    if (null_value)
      return set_field_to_null(field);
    field->set_notnull();
    error=field->store(nr);
  }
  else if (result_type() == DECIMAL_RESULT)
  {
    my_decimal decimal_value;
    my_decimal *value= val_decimal(&decimal_value);
    if (null_value)
      return set_field_to_null_with_conversions(field, no_conversions);
    field->set_notnull();
    error=field->store_decimal(value);
  }
  else
  {
    int64_t nr=val_int();
    if (null_value)
      return set_field_to_null_with_conversions(field, no_conversions);
    field->set_notnull();
    error=field->store(nr, unsigned_flag);
  }
  return error;
}


/**
  This is only called from items that is not of type item_field.
*/

bool Item::send(Protocol *protocol, String *buffer)
{
  bool result= false;
  enum_field_types f_type;

  switch ((f_type=field_type())) {
  default:
  case DRIZZLE_TYPE_NULL:
  case DRIZZLE_TYPE_ENUM:
  case DRIZZLE_TYPE_BLOB:
  case DRIZZLE_TYPE_VARCHAR:
  case DRIZZLE_TYPE_NEWDECIMAL:
  {
    String *res;
    if ((res=val_str(buffer)))
      result= protocol->store(res->ptr(),res->length(),res->charset());
    break;
  }
  case DRIZZLE_TYPE_LONG:
  {
    int64_t nr;
    nr= val_int();
    if (!null_value)
      result= protocol->store_long(nr);
    break;
  }
  case DRIZZLE_TYPE_LONGLONG:
  {
    int64_t nr;
    nr= val_int();
    if (!null_value)
      result= protocol->store_int64_t(nr, unsigned_flag);
    break;
  }
  case DRIZZLE_TYPE_DOUBLE:
  {
    double nr= val_real();
    if (!null_value)
      result= protocol->store(nr, decimals, buffer);
    break;
  }
  case DRIZZLE_TYPE_DATETIME:
  case DRIZZLE_TYPE_TIMESTAMP:
  {
    DRIZZLE_TIME tm;
    get_date(&tm, TIME_FUZZY_DATE);
    if (!null_value)
    {
      if (f_type == DRIZZLE_TYPE_DATE)
	return protocol->store_date(&tm);
      else
	result= protocol->store(&tm);
    }
    break;
  }
  case DRIZZLE_TYPE_TIME:
  {
    DRIZZLE_TIME tm;
    get_time(&tm);
    if (!null_value)
      result= protocol->store_time(&tm);
    break;
  }
  }
  if (null_value)
    result= protocol->store_null();
  return result;
}


bool Item_default_value::eq(const Item *item, bool binary_cmp) const
{
  return item->type() == DEFAULT_VALUE_ITEM &&
    ((Item_default_value *)item)->arg->eq(arg, binary_cmp);
}


bool Item_default_value::fix_fields(Session *session, Item **)
{
  Item *real_arg;
  Item_field *field_arg;
  Field *def_field;
  assert(fixed == 0);

  if (!arg)
  {
    fixed= 1;
    return false;
  }
  if (!arg->fixed && arg->fix_fields(session, &arg))
    goto error;


  real_arg= arg->real_item();
  if (real_arg->type() != FIELD_ITEM)
  {
    my_error(ER_NO_DEFAULT_FOR_FIELD, MYF(0), arg->name);
    goto error;
  }

  field_arg= (Item_field *)real_arg;
  if (field_arg->field->flags & NO_DEFAULT_VALUE_FLAG)
  {
    my_error(ER_NO_DEFAULT_FOR_FIELD, MYF(0), field_arg->field->field_name);
    goto error;
  }
  if (!(def_field= (Field*) sql_alloc(field_arg->field->size_of())))
    goto error;
  memcpy(def_field, field_arg->field, field_arg->field->size_of());
  def_field->move_field_offset((my_ptrdiff_t)
                               (def_field->table->s->default_values -
                                def_field->table->record[0]));
  set_field(def_field);
  return false;

error:
  context->process_error(session);
  return true;
}


void Item_default_value::print(String *str, enum_query_type query_type)
{
  if (!arg)
  {
    str->append(STRING_WITH_LEN("default"));
    return;
  }
  str->append(STRING_WITH_LEN("default("));
  arg->print(str, query_type);
  str->append(')');
}


int Item_default_value::save_in_field(Field *field_arg, bool no_conversions)
{
  if (!arg)
  {
    if (field_arg->flags & NO_DEFAULT_VALUE_FLAG)
    {
      if (field_arg->reset())
      {
        my_message(ER_CANT_CREATE_GEOMETRY_OBJECT,
                   ER(ER_CANT_CREATE_GEOMETRY_OBJECT), MYF(0));
        return -1;
      }

      {
        push_warning_printf(field_arg->table->in_use,
                            DRIZZLE_ERROR::WARN_LEVEL_WARN,
                            ER_NO_DEFAULT_FOR_FIELD,
                            ER(ER_NO_DEFAULT_FOR_FIELD),
                            field_arg->field_name);
      }
      return 1;
    }
    field_arg->set_default();
    return 0;
  }
  return Item_field::save_in_field(field_arg, no_conversions);
}


/**
  This method like the walk method traverses the item tree, but at the
  same time it can replace some nodes in the tree.
*/

Item *Item_default_value::transform(Item_transformer transformer, unsigned char *args)
{
  Item *new_item= arg->transform(transformer, args);
  if (!new_item)
    return NULL;

  /*
    Session::change_item_tree() should be called only if the tree was
    really transformed, i.e. when a new item has been created.
    Otherwise we'll be allocating a lot of unnecessary memory for
    change records at each execution.
  */
  if (arg != new_item)
    current_session->change_item_tree(&arg, new_item);
  return (this->*transformer)(args);
}

Item_result item_cmp_type(Item_result a,Item_result b)
{
  if (a == STRING_RESULT && b == STRING_RESULT)
    return STRING_RESULT;
  if (a == INT_RESULT && b == INT_RESULT)
    return INT_RESULT;
  else if (a == ROW_RESULT || b == ROW_RESULT)
    return ROW_RESULT;
  if ((a == INT_RESULT || a == DECIMAL_RESULT) &&
      (b == INT_RESULT || b == DECIMAL_RESULT))
    return DECIMAL_RESULT;
  return REAL_RESULT;
}


void resolve_const_item(Session *session, Item **ref, Item *comp_item)
{
  Item *item= *ref;
  Item *new_item= NULL;
  if (item->basic_const_item())
    return;                                     // Can't be better
  Item_result res_type=item_cmp_type(comp_item->result_type(),
				     item->result_type());
  char *name=item->name;			// Alloced by sql_alloc

  switch (res_type) {
  case STRING_RESULT:
  {
    char buff[MAX_FIELD_WIDTH];
    String tmp(buff,sizeof(buff),&my_charset_bin),*result;
    result=item->val_str(&tmp);
    if (item->null_value)
      new_item= new Item_null(name);
    else
    {
      uint32_t length= result->length();
      char *tmp_str= sql_strmake(result->ptr(), length);
      new_item= new Item_string(name, tmp_str, length, result->charset());
    }
    break;
  }
  case INT_RESULT:
  {
    int64_t result=item->val_int();
    uint32_t length=item->max_length;
    bool null_value=item->null_value;
    new_item= (null_value ? (Item*) new Item_null(name) :
               (Item*) new Item_int(name, result, length));
    break;
  }
  case ROW_RESULT:
  if (item->type() == Item::ROW_ITEM && comp_item->type() == Item::ROW_ITEM)
  {
    /*
      Substitute constants only in Item_rows. Don't affect other Items
      with ROW_RESULT (eg Item_singlerow_subselect).

      For such Items more optimal is to detect if it is constant and replace
      it with Item_row. This would optimize queries like this:
      SELECT * FROM t1 WHERE (a,b) = (SELECT a,b FROM t2 LIMIT 1);
    */
    Item_row *item_row= (Item_row*) item;
    Item_row *comp_item_row= (Item_row*) comp_item;
    uint32_t col;
    new_item= 0;
    /*
      If item and comp_item are both Item_rows and have same number of cols
      then process items in Item_row one by one.
      We can't ignore NULL values here as this item may be used with <=>, in
      which case NULL's are significant.
    */
    assert(item->result_type() == comp_item->result_type());
    assert(item_row->cols() == comp_item_row->cols());
    col= item_row->cols();
    while (col-- > 0)
      resolve_const_item(session, item_row->addr(col),
                         comp_item_row->element_index(col));
    break;
  }
  /* Fallthrough */
  case REAL_RESULT:
  {						// It must REAL_RESULT
    double result= item->val_real();
    uint32_t length=item->max_length,decimals=item->decimals;
    bool null_value=item->null_value;
    new_item= (null_value ? (Item*) new Item_null(name) : (Item*)
               new Item_float(name, result, decimals, length));
    break;
  }
  case DECIMAL_RESULT:
  {
    my_decimal decimal_value;
    my_decimal *result= item->val_decimal(&decimal_value);
    uint32_t length= item->max_length, decimals= item->decimals;
    bool null_value= item->null_value;
    new_item= (null_value ?
               (Item*) new Item_null(name) :
               (Item*) new Item_decimal(name, result, length, decimals));
    break;
  }
  default:
    assert(0);
  }
  if (new_item)
    session->change_item_tree(ref, new_item);
}

/**
  Return true if the value stored in the field is equal to the const
  item.

  We need to use this on the range optimizer because in some cases
  we can't store the value in the field without some precision/character loss.
*/

bool field_is_equal_to_item(Field *field,Item *item)
{

  Item_result res_type=item_cmp_type(field->result_type(),
				     item->result_type());
  if (res_type == STRING_RESULT)
  {
    char item_buff[MAX_FIELD_WIDTH];
    char field_buff[MAX_FIELD_WIDTH];
    String item_tmp(item_buff,sizeof(item_buff),&my_charset_bin),*item_result;
    String field_tmp(field_buff,sizeof(field_buff),&my_charset_bin);
    item_result=item->val_str(&item_tmp);
    if (item->null_value)
      return 1;					// This must be true
    field->val_str(&field_tmp);
    return !stringcmp(&field_tmp,item_result);
  }
  if (res_type == INT_RESULT)
    return 1;					// Both where of type int
  if (res_type == DECIMAL_RESULT)
  {
    my_decimal item_buf, *item_val,
               field_buf, *field_val;
    item_val= item->val_decimal(&item_buf);
    if (item->null_value)
      return 1;					// This must be true
    field_val= field->val_decimal(&field_buf);
    return !my_decimal_cmp(item_val, field_val);
  }
  double result= item->val_real();
  if (item->null_value)
    return 1;
  return result == field->val_real();
}

/**
  Dummy error processor used by default by Name_resolution_context.

  @note
    do nothing
*/

void dummy_error_processor(Session *, void *)
{}

/**
  Create field for temporary table using type of given item.

  @param session                   Thread handler
  @param item                  Item to create a field for
  @param table                 Temporary table
  @param copy_func             If set and item is a function, store copy of
                               item in this array
  @param modify_item           1 if item->result_field should point to new
                               item. This is relevent for how fill_record()
                               is going to work:
                               If modify_item is 1 then fill_record() will
                               update the record in the original table.
                               If modify_item is 0 then fill_record() will
                               update the temporary table
  @param convert_blob_length   If >0 create a varstring(convert_blob_length)
                               field instead of blob.

  @retval
    0  on error
  @retval
    new_created field
*/

static Field *create_tmp_field_from_item(Session *,
                                         Item *item, Table *table,
                                         Item ***copy_func, bool modify_item,
                                         uint32_t convert_blob_length)
{
  bool maybe_null= item->maybe_null;
  Field *new_field;

  switch (item->result_type()) {
  case REAL_RESULT:
    new_field= new Field_double(item->max_length, maybe_null,
                                item->name, item->decimals, true);
    break;
  case INT_RESULT:
    /*
      Select an integer type with the minimal fit precision.
      MY_INT32_NUM_DECIMAL_DIGITS is sign inclusive, don't consider the sign.
      Values with MY_INT32_NUM_DECIMAL_DIGITS digits may or may not fit into
      Field_long : make them Field_int64_t.
    */
    if (item->max_length >= (MY_INT32_NUM_DECIMAL_DIGITS - 1))
      new_field=new Field_int64_t(item->max_length, maybe_null,
                                   item->name, item->unsigned_flag);
    else
      new_field=new Field_long(item->max_length, maybe_null,
                               item->name, item->unsigned_flag);
    break;
  case STRING_RESULT:
    assert(item->collation.collation);

    enum enum_field_types type;
    /*
      DATE/TIME fields have STRING_RESULT result type.
      To preserve type they needed to be handled separately.
    */
    if ((type= item->field_type()) == DRIZZLE_TYPE_DATETIME ||
        type == DRIZZLE_TYPE_TIME || type == DRIZZLE_TYPE_DATE ||
        type == DRIZZLE_TYPE_TIMESTAMP)
      new_field= item->tmp_table_field_from_field_type(table, 1);
    /*
      Make sure that the blob fits into a Field_varstring which has
      2-byte lenght.
    */
    else if (item->max_length/item->collation.collation->mbmaxlen > 255 &&
             convert_blob_length <= Field_varstring::MAX_SIZE &&
             convert_blob_length)
      new_field= new Field_varstring(convert_blob_length, maybe_null,
                                     item->name, table->s,
                                     item->collation.collation);
    else
      new_field= item->make_string_field(table);
    new_field->set_derivation(item->collation.derivation);
    break;
  case DECIMAL_RESULT:
  {
    uint8_t dec= item->decimals;
    uint8_t intg= ((Item_decimal *) item)->decimal_precision() - dec;
    uint32_t len= item->max_length;

    /*
      Trying to put too many digits overall in a DECIMAL(prec,dec)
      will always throw a warning. We must limit dec to
      DECIMAL_MAX_SCALE however to prevent an assert() later.
    */

    if (dec > 0)
    {
      signed int overflow;

      dec= cmin(dec, (uint8_t)DECIMAL_MAX_SCALE);

      /*
        If the value still overflows the field with the corrected dec,
        we'll throw out decimals rather than integers. This is still
        bad and of course throws a truncation warning.
        +1: for decimal point
      */

      overflow= my_decimal_precision_to_length(intg + dec, dec,
                                               item->unsigned_flag) - len;

      if (overflow > 0)
        dec= cmax(0, dec - overflow);            // too long, discard fract
      else
        len -= item->decimals - dec;            // corrected value fits
    }

    new_field= new Field_new_decimal(len, maybe_null, item->name,
                                     dec, item->unsigned_flag);
    break;
  }
  case ROW_RESULT:
  default:
    // This case should never be choosen
    assert(0);
    new_field= 0;
    break;
  }
  if (new_field)
    new_field->init(table);

  if (copy_func && item->is_result_field())
    *((*copy_func)++) = item;			// Save for copy_funcs
  if (modify_item)
    item->set_result_field(new_field);
  if (item->type() == Item::NULL_ITEM)
    new_field->is_created_from_null_item= true;
  return new_field;
}

Field *create_tmp_field(Session *session, Table *table,Item *item,
                        Item::Type type, Item ***copy_func, Field **from_field,
                        Field **default_field, bool group, bool modify_item,
                        bool, bool make_copy_field,
                        uint32_t convert_blob_length)
{
  Field *result;
  Item::Type orig_type= type;
  Item *orig_item= 0;

  if (type != Item::FIELD_ITEM &&
      item->real_item()->type() == Item::FIELD_ITEM)
  {
    orig_item= item;
    item= item->real_item();
    type= Item::FIELD_ITEM;
  }

  switch (type) {
  case Item::SUM_FUNC_ITEM:
  {
    Item_sum *item_sum=(Item_sum*) item;
    result= item_sum->create_tmp_field(group, table, convert_blob_length);
    if (!result)
      my_error(ER_OUT_OF_RESOURCES, MYF(ME_FATALERROR));
    return result;
  }
  case Item::FIELD_ITEM:
  case Item::DEFAULT_VALUE_ITEM:
  {
    Item_field *field= (Item_field*) item;
    bool orig_modify= modify_item;
    if (orig_type == Item::REF_ITEM)
      modify_item= 0;
    /*
      If item have to be able to store NULLs but underlaid field can't do it,
      create_tmp_field_from_field() can't be used for tmp field creation.
    */
    if (field->maybe_null && !field->field->maybe_null())
    {
      result= create_tmp_field_from_item(session, item, table, NULL,
                                         modify_item, convert_blob_length);
      *from_field= field->field;
      if (result && modify_item)
        field->result_field= result;
    }
    else
      result= create_tmp_field_from_field(session, (*from_field= field->field),
                                          orig_item ? orig_item->name :
                                          item->name,
                                          table,
                                          modify_item ? field :
                                          NULL,
                                          convert_blob_length);
    if (orig_type == Item::REF_ITEM && orig_modify)
      ((Item_ref*)orig_item)->set_result_field(result);
    if (field->field->eq_def(result))
      *default_field= field->field;
    return result;
  }
  /* Fall through */
  case Item::FUNC_ITEM:
    /* Fall through */
  case Item::COND_ITEM:
  case Item::FIELD_AVG_ITEM:
  case Item::FIELD_STD_ITEM:
  case Item::SUBSELECT_ITEM:
    /* The following can only happen with 'CREATE TABLE ... SELECT' */
  case Item::PROC_ITEM:
  case Item::INT_ITEM:
  case Item::REAL_ITEM:
  case Item::DECIMAL_ITEM:
  case Item::STRING_ITEM:
  case Item::REF_ITEM:
  case Item::NULL_ITEM:
  case Item::VARBIN_ITEM:
    if (make_copy_field)
    {
      assert(((Item_result_field*)item)->result_field);
      *from_field= ((Item_result_field*)item)->result_field;
    }
    return create_tmp_field_from_item(session, item, table,
                                      (make_copy_field ? 0 : copy_func),
                                       modify_item, convert_blob_length);
  case Item::TYPE_HOLDER:
    result= ((Item_type_holder *)item)->make_field_by_type(table);
    result->set_derivation(item->collation.derivation);
    return result;
  default:					// Dosen't have to be stored
    return NULL;
  }
}


/**
  Wrapper of hide_view_error call for Name_resolution_context error
  processor.

  @note
    hide view underlying tables details in error messages
*/

/*****************************************************************************
** Instantiate templates
*****************************************************************************/

#ifdef HAVE_EXPLICIT_TEMPLATE_INSTANTIATION
template class List<Item>;
template class List_iterator<Item>;
template class List_iterator_fast<Item>;
template class List_iterator_fast<Item_field>;
template class List<List_item>;
#endif
