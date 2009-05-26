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

#include "mysys/mysys_priv.h"
#include <mystrings/m_string.h>


/*
  Initialize memory root

  SYNOPSIS
    init_alloc_root()
      mem_root       - memory root to initialize
      block_size     - size of chunks (blocks) used for memory allocation
                       (It is external size of chunk i.e. it should include
                        memory required for internal structures, thus it
                        should be no less than ALLOC_ROOT_MIN_BLOCK_SIZE)
      pre_alloc_size - if non-0, then size of block that should be
                       pre-allocated during memory root initialization.

  DESCRIPTION
    This function prepares memory root for further use, sets initial size of
    chunk for memory allocation and pre-allocates first block if specified.
    Altough error can happen during execution of this function if
    pre_alloc_size is non-0 it won't be reported. Instead it will be
    reported as error in first alloc_root() on this memory root.
*/

void init_alloc_root(MEM_ROOT *mem_root, size_t block_size,
		     size_t )
{
  mem_root->free= mem_root->used= mem_root->pre_alloc= 0;
  mem_root->min_malloc= 32;
  mem_root->block_size= block_size - ALLOC_ROOT_MIN_BLOCK_SIZE;
  mem_root->error_handler= 0;
  mem_root->block_num= 4;			/* We shift this with >>2 */
  mem_root->first_block_usage= 0;

  return;
}


/*
  SYNOPSIS
    reset_root_defaults()
    mem_root        memory root to change defaults of
    block_size      new value of block size. Must be greater or equal
                    than ALLOC_ROOT_MIN_BLOCK_SIZE (this value is about
                    68 bytes and depends on platform and compilation flags)
    pre_alloc_size  new size of preallocated block. If not zero,
                    must be equal to or greater than block size,
                    otherwise means 'no prealloc'.
  DESCRIPTION
    Function aligns and assigns new value to block size; then it tries to
    reuse one of existing blocks as prealloc block, or malloc new one of
    requested size. If no blocks can be reused, all unused blocks are freed
    before allocation.
*/

void reset_root_defaults(MEM_ROOT *mem_root, size_t block_size,
                         size_t pre_alloc_size)
{
  assert(alloc_root_inited(mem_root));

  mem_root->block_size= block_size - ALLOC_ROOT_MIN_BLOCK_SIZE;
  if (pre_alloc_size)
  {
    size_t size= pre_alloc_size + ALIGN_SIZE(sizeof(USED_MEM));
    if (!mem_root->pre_alloc || mem_root->pre_alloc->size != size)
    {
      USED_MEM *mem, **prev= &mem_root->free;
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
        if (mem->left + ALIGN_SIZE(sizeof(USED_MEM)) == mem->size)
        {
          /* remove block from the list and free it */
          *prev= mem->next;
          free(mem);
        }
        else
          prev= &mem->next;
      }
      /* Allocate new prealloc block and add it to the end of free list */
      if ((mem= (USED_MEM *) malloc(size)))
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


void *alloc_root(MEM_ROOT *mem_root, size_t length)
{
  size_t get_size, block_size;
  unsigned char* point;
  register USED_MEM *next= 0;
  register USED_MEM **prev;
  assert(alloc_root_inited(mem_root));

  length= ALIGN_SIZE(length);
  if ((*(prev= &mem_root->free)) != NULL)
  {
    if ((*prev)->left < length &&
	mem_root->first_block_usage++ >= ALLOC_MAX_BLOCK_USAGE_BEFORE_DROP &&
	(*prev)->left < ALLOC_MAX_BLOCK_TO_DROP)
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
    get_size= length+ALIGN_SIZE(sizeof(USED_MEM));
    get_size= cmax(get_size, block_size);

    if (!(next = (USED_MEM*) malloc(get_size)))
    {
      if (mem_root->error_handler)
	(*mem_root->error_handler)();
      return((void*) 0);                      /* purecov: inspected */
    }
    mem_root->block_num++;
    next->next= *prev;
    next->size= get_size;
    next->left= get_size-ALIGN_SIZE(sizeof(USED_MEM));
    *prev=next;
  }

  point= (unsigned char*) ((char*) next+ (next->size-next->left));
  /*TODO: next part may be unneded due to mem_root->first_block_usage counter*/
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

void *multi_alloc_root(MEM_ROOT *root, ...)
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

  if (!(start= (char*) alloc_root(root, tot_length)))
    return(0);                            /* purecov: inspected */

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

static inline void mark_blocks_free(MEM_ROOT* root)
{
  register USED_MEM *next;
  register USED_MEM **last;

  /* iterate through (partially) free blocks, mark them free */
  last= &root->free;
  for (next= root->free; next; next= *(last= &next->next))
  {
    next->left= next->size - ALIGN_SIZE(sizeof(USED_MEM));
    TRASH_MEM(next);
  }

  /* Combine the free and the used list */
  *last= next=root->used;

  /* now go through the used blocks and mark them free */
  for (; next; next= next->next)
  {
    next->left= next->size - ALIGN_SIZE(sizeof(USED_MEM));
    TRASH_MEM(next);
  }

  /* Now everything is set; Indicate that nothing is used anymore */
  root->used= 0;
  root->first_block_usage= 0;
}


/*
  Deallocate everything used by alloc_root or just move
  used blocks to free list if called with MY_USED_TO_FREE

  SYNOPSIS
    free_root()
      root		Memory root
      MyFlags		Flags for what should be freed:

        MY_MARK_BLOCKS_FREED	Don't free blocks, just mark them free
        MY_KEEP_PREALLOC	If this is not set, then free also the
        		        preallocated block

  NOTES
    One can call this function either with root block initialised with
    init_alloc_root() or with a zero:ed block.
    It's also safe to call this multiple times with the same mem_root.
*/

void free_root(MEM_ROOT *root, myf MyFlags)
{
  register USED_MEM *next,*old;

  if (MyFlags & MY_MARK_BLOCKS_FREE)
  {
    mark_blocks_free(root);
    return;
  }
  if (!(MyFlags & MY_KEEP_PREALLOC))
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
    root->free->left=root->pre_alloc->size-ALIGN_SIZE(sizeof(USED_MEM));
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

void set_prealloc_root(MEM_ROOT *root, char *ptr)
{
  USED_MEM *next;
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


char *strdup_root(MEM_ROOT *root, const char *str)
{
  return strmake_root(root, str, strlen(str));
}


char *strmake_root(MEM_ROOT *root, const char *str, size_t len)
{
  char *pos;
  if ((pos=(char *)alloc_root(root,len+1)))
  {
    memcpy(pos,str,len);
    pos[len]=0;
  }
  return pos;
}


void *memdup_root(MEM_ROOT *root, const void *str, size_t len)
{
  void *pos;
  if ((pos=alloc_root(root,len)))
    memcpy(pos,str,len);
  return pos;
}