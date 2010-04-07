/* Copyright (C) 2000 MySQL AB

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

/* Routines to handle mallocing of results which will be freed the same time */

#include "config.h"

#include "drizzled/internal/my_sys.h"
#include "drizzled/internal/m_string.h"

#include <algorithm>

using namespace std;

namespace drizzled
{

static const unsigned int MAX_BLOCK_TO_DROP= 4096;
static const unsigned int MAX_BLOCK_USAGE_BEFORE_DROP= 10;

/**
 * @brief
 * Initialize memory root
 *
 * @details
 * This function prepares memory root for further use, sets initial size of
 * chunk for memory allocation and pre-allocates first block if specified.
 * Altough error can happen during execution of this function if
 * pre_alloc_size is non-0 it won't be reported. Instead it will be
 * reported as error in first alloc_root() on this memory root.
 *
 * @param  mem_root       memory root to initialize
 * @param  block_size     size of chunks (blocks) used for memory allocation
 *                       (It is external size of chunk i.e. it should include
 *                      memory required for internal structures, thus it
 *                      should be no less than memory::ROOT_MIN_BLOCK_SIZE)
 *
 */
void memory::init_alloc_root(memory::Root *mem_root, size_t block_size)
{
  mem_root->free= mem_root->used= mem_root->pre_alloc= 0;
  mem_root->min_malloc= 32;
  mem_root->block_size= block_size - memory::ROOT_MIN_BLOCK_SIZE;
  mem_root->error_handler= 0;
  mem_root->block_num= 4;			/* We shift this with >>2 */
  mem_root->first_block_usage= 0;

  return;
}


/**
 * @details
 * Function aligns and assigns new value to block size; then it tries to
 * reuse one of existing blocks as prealloc block, or malloc new one of
 * requested size. If no blocks can be reused, all unused blocks are freed
 * before allocation.
 *
 * @param  mem_root        memory root to change defaults of
 * @param  block_size      new value of block size. Must be greater or equal
 *                         than ALLOC_ROOT_MIN_BLOCK_SIZE (this value is about
 *                         68 bytes and depends on platform and compilation flags)
 * @param pre_alloc_size  new size of preallocated block. If not zero,
 *                        must be equal to or greater than block size,
 *                        otherwise means 'no prealloc'.
 */
void memory::reset_root_defaults(memory::Root *mem_root, size_t block_size,
                                 size_t pre_alloc_size)
{
  assert(alloc_root_inited(mem_root));

  mem_root->block_size= block_size - memory::ROOT_MIN_BLOCK_SIZE;
  if (pre_alloc_size)
  {
    size_t size= pre_alloc_size + ALIGN_SIZE(sizeof(memory::internal::UsedMemory));
    if (!mem_root->pre_alloc || mem_root->pre_alloc->size != size)
    {
      memory::internal::UsedMemory *mem, **prev= &mem_root->free;
      /*
        Free unused blocks, so that consequent calls
        to reset_root_defaults won't eat away memory.
      */
      while (*prev)
      {
        mem= *prev;
        if (mem->size == size)
        {
          /* We found a suitable block, no need to do anything else */
          mem_root->pre_alloc= mem;
          return;
        }
        if (mem->left + ALIGN_SIZE(sizeof(memory::internal::UsedMemory)) == mem->size)
        {
          /* remove block from the list and free it */
          *prev= mem->next;
          free(mem);
        }
        else
          prev= &mem->next;
      }
      /* Allocate new prealloc block and add it to the end of free list */
      if ((mem= static_cast<memory::internal::UsedMemory *>(malloc(size))))
      {
        mem->size= size;
        mem->left= pre_alloc_size;
        mem->next= *prev;
        *prev= mem_root->pre_alloc= mem;
      }
      else
      {
        mem_root->pre_alloc= 0;
      }
    }
  }
  else
  {
    mem_root->pre_alloc= 0;
  }
}


void *memory::alloc_root(memory::Root *mem_root, size_t length)
{
  size_t get_size, block_size;
  unsigned char* point;
  memory::internal::UsedMemory *next= NULL;
  memory::internal::UsedMemory **prev;
  assert(alloc_root_inited(mem_root));

  length= ALIGN_SIZE(length);
  if ((*(prev= &mem_root->free)) != NULL)
  {
    if ((*prev)->left < length &&
	mem_root->first_block_usage++ >= MAX_BLOCK_USAGE_BEFORE_DROP &&
	(*prev)->left < MAX_BLOCK_TO_DROP)
    {
      next= *prev;
      *prev= next->next;			/* Remove block from list */
      next->next= mem_root->used;
      mem_root->used= next;
      mem_root->first_block_usage= 0;
    }
    for (next= *prev ; next && next->left < length ; next= next->next)
      prev= &next->next;
  }
  if (! next)
  {						/* Time to alloc new block */
    block_size= mem_root->block_size * (mem_root->block_num >> 2);
    get_size= length+ALIGN_SIZE(sizeof(memory::internal::UsedMemory));
    get_size= max(get_size, block_size);

    if (!(next = static_cast<memory::internal::UsedMemory *>(malloc(get_size))))
    {
      if (mem_root->error_handler)
	(*mem_root->error_handler)();
      return NULL;
    }
    mem_root->block_num++;
    next->next= *prev;
    next->size= get_size;
    next->left= get_size-ALIGN_SIZE(sizeof(memory::internal::UsedMemory));
    *prev=next;
  }

  point= (unsigned char*) ((char*) next+ (next->size-next->left));
  /** @todo next part may be unneeded due to mem_root->first_block_usage counter*/
  if ((next->left-= length) < mem_root->min_malloc)
  {						/* Full block */
    *prev= next->next;				/* Remove block from list */
    next->next= mem_root->used;
    mem_root->used= next;
    mem_root->first_block_usage= 0;
  }
  return((void*) point);
}


/*
  Allocate many pointers at the same time.

  DESCRIPTION
    ptr1, ptr2, etc all point into big allocated memory area.

  SYNOPSIS
    multi_alloc_root()
      root               Memory root
      ptr1, length1      Multiple arguments terminated by a NULL pointer
      ptr2, length2      ...
      ...
      NULL

  RETURN VALUE
    A pointer to the beginning of the allocated memory block
    in case of success or NULL if out of memory.
*/

void *memory::multi_alloc_root(memory::Root *root, ...)
{
  va_list args;
  char **ptr, *start, *res;
  size_t tot_length, length;

  va_start(args, root);
  tot_length= 0;
  while ((ptr= va_arg(args, char **)))
  {
    length= va_arg(args, uint);
    tot_length+= ALIGN_SIZE(length);
  }
  va_end(args);

  if (!(start= (char*) memory::alloc_root(root, tot_length)))
    return(0);

  va_start(args, root);
  res= start;
  while ((ptr= va_arg(args, char **)))
  {
    *ptr= res;
    length= va_arg(args, uint);
    res+= ALIGN_SIZE(length);
  }
  va_end(args);
  return((void*) start);
}

#define TRASH_MEM(X) TRASH(((char*)(X) + ((X)->size-(X)->left)), (X)->left)

/* Mark all data in blocks free for reusage */

static inline void mark_blocks_free(memory::Root* root)
{
  memory::internal::UsedMemory *next;
  memory::internal::UsedMemory **last;

  /* iterate through (partially) free blocks, mark them free */
  last= &root->free;
  for (next= root->free; next; next= *(last= &next->next))
  {
    next->left= next->size - ALIGN_SIZE(sizeof(memory::internal::UsedMemory));
    TRASH_MEM(next);
  }

  /* Combine the free and the used list */
  *last= next=root->used;

  /* now go through the used blocks and mark them free */
  for (; next; next= next->next)
  {
    next->left= next->size - ALIGN_SIZE(sizeof(memory::internal::UsedMemory));
    TRASH_MEM(next);
  }

  /* Now everything is set; Indicate that nothing is used anymore */
  root->used= 0;
  root->first_block_usage= 0;
}


/*
  Deallocate everything used by memory::alloc_root or just move
  used blocks to free list if called with MY_USED_TO_FREE

  SYNOPSIS
    free_root()
      root		Memory root
      MyFlags		Flags for what should be freed:

        MARK_BLOCKS_FREED	Don't free blocks, just mark them free
        KEEP_PREALLOC		If this is not set, then free also the
        		        preallocated block

  NOTES
    One can call this function either with root block initialised with
    init_alloc_root() or with a zero:ed block.
    It's also safe to call this multiple times with the same mem_root.
*/

void memory::free_root(memory::Root *root, myf MyFlags)
{
  memory::internal::UsedMemory *next,*old;

  if (MyFlags & memory::MARK_BLOCKS_FREE)
  {
    mark_blocks_free(root);
    return;
  }
  if (!(MyFlags & memory::KEEP_PREALLOC))
    root->pre_alloc=0;

  for (next=root->used; next ;)
  {
    old=next; next= next->next ;
    if (old != root->pre_alloc)
      free(old);
  }
  for (next=root->free ; next ;)
  {
    old=next; next= next->next;
    if (old != root->pre_alloc)
      free(old);
  }
  root->used=root->free=0;
  if (root->pre_alloc)
  {
    root->free=root->pre_alloc;
    root->free->left=root->pre_alloc->size-ALIGN_SIZE(sizeof(memory::internal::UsedMemory));
    TRASH_MEM(root->pre_alloc);
    root->free->next=0;
  }
  root->block_num= 4;
  root->first_block_usage= 0;
  return;
}

/*
  Find block that contains an object and set the pre_alloc to it
*/

void memory::set_prealloc_root(memory::Root *root, char *ptr)
{
  memory::internal::UsedMemory *next;
  for (next=root->used; next ; next=next->next)
  {
    if ((char*) next <= ptr && (char*) next + next->size > ptr)
    {
      root->pre_alloc=next;
      return;
    }
  }
  for (next=root->free ; next ; next=next->next)
  {
    if ((char*) next <= ptr && (char*) next + next->size > ptr)
    {
      root->pre_alloc=next;
      return;
    }
  }
}


char *memory::strdup_root(memory::Root *root, const char *str)
{
  return strmake_root(root, str, strlen(str));
}


char *memory::strmake_root(memory::Root *root, const char *str, size_t len)
{
  char *pos;
  if ((pos=(char *)memory::alloc_root(root,len+1)))
  {
    memcpy(pos,str,len);
    pos[len]=0;
  }
  return pos;
}


void *memory::memdup_root(memory::Root *root, const void *str, size_t len)
{
  void *pos;
  if ((pos=memory::alloc_root(root,len)))
    memcpy(pos,str,len);
  return pos;
}

} /* namespace drizzled */
