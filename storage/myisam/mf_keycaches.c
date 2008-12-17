/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/*
  Handling of multiple key caches

  The idea is to have a thread safe hash on the table name,
  with a default key cache value that is returned if the table name is not in
  the cache.
*/

#include <drizzled/global.h>
#include <mysys/mysys_err.h>
#include <mysys/my_sys.h>
#include "keycache.h"
#include <mysys/hash.h>
#include <mystrings/m_string.h>

/*****************************************************************************
  General functions to handle SAFE_HASH objects.

  A SAFE_HASH object is used to store the hash, the mutex and default value
  needed by the rest of the key cache code.
  This is a separate struct to make it easy to later reuse the code for other
  purposes

  All entries are linked in a list to allow us to traverse all elements
  and delete selected ones. (HASH doesn't allow any easy ways to do this).
*****************************************************************************/

/*
  Struct to store a key and pointer to object
*/

typedef struct st_safe_hash_entry
{
  unsigned char *key;
  uint32_t length;
  unsigned char *data;
  struct st_safe_hash_entry *next, **prev;
} SAFE_HASH_ENTRY;


typedef struct st_safe_hash_with_default
{
  pthread_rwlock_t mutex;
  HASH hash;
  unsigned char *default_value;
  SAFE_HASH_ENTRY *root;
} SAFE_HASH;


/*
  Free a SAFE_HASH_ENTRY

  This function is called by the hash object on delete
*/

static void safe_hash_entry_free(SAFE_HASH_ENTRY *entry)
{
  free((unsigned char*) entry);
  return;
}


/* Get key and length for a SAFE_HASH_ENTRY */

static unsigned char *safe_hash_entry_get(SAFE_HASH_ENTRY *entry, size_t *length,
                                  bool not_used __attribute__((unused)))
{
  *length=entry->length;
  return (unsigned char*) entry->key;
}


/*
  Init a SAFE_HASH object

  SYNOPSIS
    safe_hash_init()
    hash		safe_hash handler
    elements		Expected max number of elements
    default_value	default value

  NOTES
    In case of error we set hash->default_value to 0 to allow one to call
    safe_hash_free on an object that couldn't be initialized.

  RETURN
    0  ok
    1  error
*/

static bool safe_hash_init(SAFE_HASH *hash, uint32_t elements,
			      unsigned char *default_value)
{
  if (hash_init(&hash->hash, &my_charset_bin, elements,
		0, 0, (hash_get_key) safe_hash_entry_get,
		(void (*)(void*)) safe_hash_entry_free, 0))
  {
    hash->default_value= 0;
    return(1);
  }
  pthread_rwlock_init(&hash->mutex, 0);
  hash->default_value= default_value;
  hash->root= 0;
  return(0);
}


/*
  Free a SAFE_HASH object

  NOTES
    This is safe to call on any object that has been sent to safe_hash_init()
*/

static void safe_hash_free(SAFE_HASH *hash)
{
  /*
    Test if safe_hash_init succeeded. This will also guard us against multiple
    free calls.
  */
  if (hash->default_value)
  {
    hash_free(&hash->hash);
    pthread_rwlock_destroy(&hash->mutex);
    hash->default_value=0;
  }
}

/*
  Return the value stored for a key or default value if no key
*/

static unsigned char *safe_hash_search(SAFE_HASH *hash, const unsigned char *key, uint32_t length)
{
  unsigned char *result;
  pthread_rwlock_rdlock(&hash->mutex);
  result= hash_search(&hash->hash, key, length);
  pthread_rwlock_unlock(&hash->mutex);
  if (!result)
    result= hash->default_value;
  else
    result= ((SAFE_HASH_ENTRY*) result)->data;
  return(result);
}


/*
  Associate a key with some data

  SYONOPSIS
    safe_hash_set()
    hash			Hash handle
    key				key (path to table etc..)
    length			Length of key
    data			data to to associate with the data

  NOTES
    This can be used both to insert a new entry and change an existing
    entry.
    If one associates a key with the default key cache, the key is deleted

  RETURN
    0  ok
    1  error (Can only be EOM). In this case my_message() is called.
*/

static bool safe_hash_set(SAFE_HASH *hash, const unsigned char *key, uint32_t length,
			     unsigned char *data)
{
  SAFE_HASH_ENTRY *entry;
  bool error= 0;

  pthread_rwlock_wrlock(&hash->mutex);
  entry= (SAFE_HASH_ENTRY*) hash_search(&hash->hash, key, length);

  if (data == hash->default_value)
  {
    /*
      The key is to be associated with the default entry. In this case
      we can just delete the entry (if it existed) from the hash as a
      search will return the default entry
    */
    if (!entry)					/* nothing to do */
      goto end;
    /* unlink entry from list */
    if ((*entry->prev= entry->next))
      entry->next->prev= entry->prev;
    hash_delete(&hash->hash, (unsigned char*) entry);
    goto end;
  }
  if (entry)
  {
    /* Entry existed;  Just change the pointer to point at the new data */
    entry->data= data;
  }
  else
  {
    if (!(entry= (SAFE_HASH_ENTRY *) malloc(sizeof(*entry) + length)))
    {
      error= 1;
      goto end;
    }
    entry->key= (unsigned char*) (entry +1);
    memcpy(entry->key, key, length);
    entry->length= length;
    entry->data= data;
    /* Link entry to list */
    if ((entry->next= hash->root))
      entry->next->prev= &entry->next;
    entry->prev= &hash->root;
    hash->root= entry;
    if (my_hash_insert(&hash->hash, (unsigned char*) entry))
    {
      /* This can only happen if hash got out of memory */
      free((char*) entry);
      error= 1;
      goto end;
    }
  }

end:
  pthread_rwlock_unlock(&hash->mutex);
  return(error);
}


/*
  Change all entres with one data value to another data value

  SYONOPSIS
    safe_hash_change()
    hash			Hash handle
    old_data			Old data
    new_data			Change all 'old_data' to this

  NOTES
    We use the linked list to traverse all elements in the hash as
    this allows us to delete elements in the case where 'new_data' is the
    default value.
*/

static void safe_hash_change(SAFE_HASH *hash, unsigned char *old_data, unsigned char *new_data)
{
  SAFE_HASH_ENTRY *entry, *next;

  pthread_rwlock_wrlock(&hash->mutex);

  for (entry= hash->root ; entry ; entry= next)
  {
    next= entry->next;
    if (entry->data == old_data)
    {
      if (new_data == hash->default_value)
      {
        if ((*entry->prev= entry->next))
          entry->next->prev= entry->prev;
	hash_delete(&hash->hash, (unsigned char*) entry);
      }
      else
	entry->data= new_data;
    }
  }

  pthread_rwlock_unlock(&hash->mutex);
  return;
}


/*****************************************************************************
  Functions to handle the key cache objects
*****************************************************************************/

/* Variable to store all key cache objects */
static SAFE_HASH key_cache_hash;


bool multi_keycache_init(void)
{
  return safe_hash_init(&key_cache_hash, 16, (unsigned char*) dflt_key_cache);
}


void multi_keycache_free(void)
{
  safe_hash_free(&key_cache_hash);
}

/*
  Get a key cache to be used for a specific table.

  SYNOPSIS
    multi_key_cache_search()
    key				key to find (usually table path)
    uint32_t length			Length of key.

  NOTES
    This function is coded in such a way that we will return the
    default key cache even if one never called multi_keycache_init.
    This will ensure that it works with old MyISAM clients.

  RETURN
    key cache to use
*/

KEY_CACHE *multi_key_cache_search(unsigned char *key, uint32_t length)
{
  if (!key_cache_hash.hash.records)
    return dflt_key_cache;
  return (KEY_CACHE*) safe_hash_search(&key_cache_hash, key, length);
}


/*
  Assosiate a key cache with a key


  SYONOPSIS
    multi_key_cache_set()
    key				key (path to table etc..)
    length			Length of key
    key_cache			cache to assococite with the table

  NOTES
    This can be used both to insert a new entry and change an existing
    entry
*/


bool multi_key_cache_set(const unsigned char *key, uint32_t length,
			    KEY_CACHE *key_cache)
{
  return safe_hash_set(&key_cache_hash, key, length, (unsigned char*) key_cache);
}


void multi_key_cache_change(KEY_CACHE *old_data,
			    KEY_CACHE *new_data)
{
  safe_hash_change(&key_cache_hash, (unsigned char*) old_data, (unsigned char*) new_data);
}
