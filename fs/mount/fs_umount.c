/****************************************************************************
 * fs/mount/fs_umount.c
 *
 * Copyright (c) 2023 Huawei Device Co., Ltd. All rights reserved.
 * Based on NuttX originally written by Gregory Nutt 
 *
 *   Copyright (C) 2007-2009, 2015 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "vfs_config.h"

#include "sys/mount.h"
#include "errno.h"
#include "vnode.h"
#include "stdlib.h"
#include "unistd.h"
#include "string.h"
#include "disk.h"
#include "fs/mount.h"
#ifdef LOSCFG_MNT_CONTAINER
#include "los_mnt_container_pri.h"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: umount
 *
 * Description:
 *   umount() detaches the filesystem mounted at the path specified by
 *  'target.'
 *
 * Return:
 *   Zero is returned on success; -1 is returned on an error and errno is
 *   set appropriately:
 *
 *   EACCES A component of a path was not searchable or mounting a read-only
 *      filesystem was attempted without giving the MS_RDONLY flag.
 *   EBUSY The target could not be unmounted because it is busy.
 *   EFAULT The pointer argument points outside the user address space.
 *
 ****************************************************************************/

BOOL fs_in_use(struct Mount *mnt, const char *target)
{
  char cwd[PATH_MAX];
  char *pret = getcwd(cwd, PATH_MAX);
  if (pret != NULL)
    {
      if (!strncmp(target, cwd, strlen(target)))
        {
          return TRUE;
        }
    }
    return VnodeInUseIter(mnt);
}

int umount(const char *target)
{
  struct Vnode *mountpt_vnode = NULL;
  struct Vnode *blkdrvr_vnode = NULL;
  struct Vnode *covered_vnode = NULL;
  struct Mount *mnt = NULL;
  int ret;

  /* Verify required pointer arguments */

  if (target == NULL)
    {
      ret = -EFAULT;
      goto errout;
    }

  /* Find the mountpt */
  VnodeHold();
  ret = VnodeLookup(target, &mountpt_vnode, 0);
  if (ret != OK || !mountpt_vnode)
    {
      goto errout;
    }
  /* Verify that the vnode is a mountpoint */
  if (!mountpt_vnode || !(mountpt_vnode->flag & VNODE_FLAG_MOUNT_NEW))
    {
      ret = -EINVAL;
      goto errout;
    }

#ifdef LOSCFG_MNT_CONTAINER
  /* Verify that the vnode is a mountpoint */
  struct Mount *tMnt = NULL;
  bool found = false;
  LOS_DL_LIST_FOR_EACH_ENTRY(tMnt, GetMountList(), struct Mount, mountList)
    {
      if (tMnt->vnodeCovered == mountpt_vnode)
        {
          found = true;
          break;
        }
    }

  if (!found)
    {
      ret = -EINVAL;
      goto errout;
    }

  if (tMnt->vnodeCovered->mntCount > 0)
    {
      tMnt->vnodeCovered->mntCount--;
      LOS_ListDelete(&tMnt->mountList);
      free(tMnt);
      VnodeDrop();
      return OK;
    }
#endif

  /* Get mount point covered vnode and mount structure */
  mnt = mountpt_vnode->originMount;
  if (!mnt)
    {
      ret = -EINVAL;
      goto errout;
    }
  covered_vnode = mnt->vnodeBeCovered;
  if (!covered_vnode || !(covered_vnode->flag & VNODE_FLAG_MOUNT_ORIGIN))
    {
      ret = -EINVAL;
      goto errout;
    }

  /* Unbind the block driver from the file system (destroying any fs
   * private data.
   */

  if (mnt->ops == NULL || mnt->ops->Unmount == NULL)
    {
      /* The filesystem does not support the unbind operation ??? */

      ret = -EINVAL;
      goto errout;
    }

  /* Release the vnode under the mount point */
  if (fs_in_use(mnt, target))
    {
      ret = -EBUSY;
      goto errout;
    }

  ret = VnodeFreeAll(mnt);
  if (ret != OK)
    {
      goto errout;
    }
  /* Umount the filesystem */
  ret = mnt->ops->Unmount(mnt, &blkdrvr_vnode);
  if (ret != OK)
    {
      goto errout;
    }
#ifdef LOSCFG_MNT_CONTAINER
  struct Mount *tCacheMnt = NULL;
  LOS_DL_LIST_FOR_EACH_ENTRY(tCacheMnt, GetMountCache(), struct Mount, mountList)
    {
      if (tCacheMnt->vnodeCovered == mountpt_vnode)
        {
          LOS_ListDelete(&tCacheMnt->mountList);
          free(tCacheMnt);
          break;
        }
    }
#endif
  VnodeFree(mountpt_vnode);
  LOS_ListDelete(&mnt->mountList);

  free(mnt);

  /* Did the unbind method return a contained block driver */
  if (blkdrvr_vnode)
    {
      ; /* block driver operations after umount */
    }

  covered_vnode->newMount = NULL;
#ifdef LOSCFG_MNT_CONTAINER
  tCacheMnt = NULL;
  found = false;
  LOS_DL_LIST_FOR_EACH_ENTRY(tCacheMnt, GetMountCache(), struct Mount, mountList)
    {
      if (tCacheMnt->vnodeBeCovered == covered_vnode)
        {
          found = true;
          break;
        }
    }
  if (!found)
    {
      covered_vnode->flag &= ~(VNODE_FLAG_MOUNT_ORIGIN);
    }
#else
  covered_vnode->flag &= ~(VNODE_FLAG_MOUNT_ORIGIN);
#endif
  VnodeDrop();

  return OK;

  /* A lot of goto's!  But they make the error handling much simpler */
errout:
  VnodeDrop();
  set_errno(-ret);
  return VFS_ERROR;
}

int umount2(const char* __target, int __flags)
{
    /* TODO: __flags need to be support */
    if (__flags) {
        set_errno(ENOSYS);
        return VFS_ERROR;
    }
    return umount(__target);
}
