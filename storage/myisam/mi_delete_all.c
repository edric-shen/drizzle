/* Copyright (C) 2000-2003, 2005 MySQL AB

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

/* Remove all rows from a MyISAM table */
/* This clears the status information and truncates files */

#include "myisamdef.h"

int mi_delete_all_rows(MI_INFO *info)
{
  uint i;
  MYISAM_SHARE *share=info->s;
  MI_STATE_INFO *state=&share->state;

  if (share->options & HA_OPTION_READ_ONLY_DATA)
  {
    return(my_errno=EACCES);
  }
  if (_mi_readinfo(info,F_WRLCK,1))
    return(my_errno);
  if (_mi_mark_file_changed(info))
    goto err;

  info->state->records=info->state->del=state->split=0;
  state->dellink = HA_OFFSET_ERROR;
  state->sortkey=  (ushort) ~0;
  info->state->key_file_length=share->base.keystart;
  info->state->data_file_length=0;
  info->state->empty=info->state->key_empty=0;
  info->state->checksum=0;

  for (i=share->base.max_key_block_length/MI_MIN_KEY_BLOCK_LENGTH ; i-- ; )
    state->key_del[i]= HA_OFFSET_ERROR;
  for (i=0 ; i < share->base.keys ; i++)
    state->key_root[i]= HA_OFFSET_ERROR;

  /*
    If we are using delayed keys or if the user has done changes to the tables
    since it was locked then there may be key blocks in the key cache
  */
  flush_key_blocks(share->key_cache, share->kfile, FLUSH_IGNORE_CHANGED);
#ifdef HAVE_MMAP
  if (share->file_map)
    _mi_unmap_file(info);
#endif
  if (ftruncate(info->dfile, 0) || ftruncate(share->kfile, share->base.keystart))
    goto err;
  VOID(_mi_writeinfo(info,WRITEINFO_UPDATE_KEYFILE));
#ifdef HAVE_MMAP
  /* Map again */
  if (share->file_map)
    mi_dynmap_file(info, (my_off_t) 0);
#endif
  return(0);

err:
  {
    int save_errno=my_errno;
    VOID(_mi_writeinfo(info,WRITEINFO_UPDATE_KEYFILE));
    info->update|=HA_STATE_WRITTEN;	/* Buffer changed */
    return(my_errno=save_errno);
  }
} /* mi_delete */
