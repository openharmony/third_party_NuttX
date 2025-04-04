/****************************************************************************
 * fs/vfs/fs_open.c
 *
 * Copyright (c) 2023 Huawei Device Co., Ltd. All rights reserved.
 * Based on NuttX originally from nuttx source (nuttx/fs/ and nuttx/drivers/)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "vfs_config.h"

#include "errno.h"
#include "sys/types.h"
#include "fcntl.h"
#include "sched.h"
#include "assert.h"
#ifdef LOSCFG_FILE_MODE
#include "stdarg.h"
#endif
#include "stdlib.h"
#include "vnode.h"
#include "blockproxy.h"
#include "path_cache.h"
#include "unistd.h"
#ifdef LOSCFG_KERNEL_DEV_PLIMIT
#include "los_plimits.h"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static int oflag_convert_mode(int oflags)
{
  /* regular file operations */

  int acc_mode = 0;
  if ((oflags & O_ACCMODE) == O_RDONLY)
  acc_mode |= READ_OP;
  if (oflags & O_WRONLY)
  acc_mode |= WRITE_OP;
  if (oflags & O_RDWR)
  acc_mode |= READ_OP | WRITE_OP;

  /* Opens the file, if it is existing. If not, a new file is created. */

  if (oflags & O_CREAT)
  acc_mode |= WRITE_OP;

  /* Creates a new file. If the file is existing, it is truncated and overwritten. */

  if (oflags & O_TRUNC)
  acc_mode |= WRITE_OP;

  /* Creates a new file. The function fails if the file is already existing. */

  if (oflags & O_EXCL)
  acc_mode |= WRITE_OP;
  if (oflags & O_APPEND)
  acc_mode |= WRITE_OP;

  /* mark for executing operation */

  if (oflags & O_EXECVE)
  acc_mode |= EXEC_OP;
  return acc_mode;
}

int get_path_from_fd(int fd, char **path)
{
  struct file *file = NULL;
  char *copypath = NULL;

  if (fd == AT_FDCWD)
    {
      return OK;
    }

  int ret = fs_getfilep(fd, &file);
  if (ret < 0)
    {
      return -ENOENT;
    }

  if ((file == NULL) || (file->f_vnode == NULL) || (file->f_path == NULL))
    {
      return -EBADF;
    }

  copypath = strdup((const char*)file->f_path);
  if (copypath == NULL)
    {
      return VFS_ERROR;
    }

  *path = copypath;
  return OK;
}

static int do_creat(struct Vnode *parentNode, char *fullpath, mode_t mode, struct Vnode **node)
{
  int ret;
  char *name = strrchr(fullpath, '/') + 1;
  parentNode->useCount++;

  if (parentNode->vop != NULL && parentNode->vop->Create != NULL)
    {
      ret = parentNode->vop->Create(parentNode, name, mode, node);
    }
  else
    {
      ret = -ENOSYS;
    }

  parentNode->useCount--;
  if (ret < 0)
    {
      return ret;
    }

  struct PathCache *dt = PathCacheAlloc(parentNode, *node, name, strlen(name));
  if (dt == NULL)
    {
      // alloc name cache failed is not a critical problem, let it go.
      PRINT_ERR("alloc path cache %s failed\n", name);
    }
  return OK;
}

int fp_open(int dirfd, const char *path, int oflags, mode_t mode)
{
  int ret;
  int accmode;
  struct file *filep = NULL;
  struct Vnode *vnode = NULL;
  struct Vnode *parentVnode = NULL;
  char *fullpath = NULL;

  VnodeHold();
  ret = follow_symlink(dirfd, path, &vnode, &fullpath);
  if (ret == OK)
    {
      /* if file exist */
      if (vnode->type == VNODE_TYPE_BCHR)
        {
          ret = -EINVAL;
          VnodeDrop();
          goto errout;
        }
#ifdef LOSCFG_KERNEL_DEV_PLIMIT
      if (vnode->type == VNODE_TYPE_CHR)
        {
          if (OsDevLimitCheckPermission(vnode->type, fullpath, oflags) != LOS_OK)
            {
              ret = -EPERM;
              VnodeDrop();
              goto errout;
            }
        }
#endif
#ifdef LOSCFG_FS_VFS_BLOCK_DEVICE
      if (vnode->type == VNODE_TYPE_BLK)
        {
          VnodeDrop();
          int fd = block_proxy(fullpath, oflags);
          if (fd < 0)
            {
              ret = fd;
              goto errout;
            }
#ifdef LOSCFG_KERNEL_DEV_PLIMIT
          if (OsDevLimitCheckPermission(vnode->type, fullpath, oflags) != LOS_OK)
            {
              ret = -EPERM;
              goto errout;
            }
#endif
         return fd;
        }
#endif
      if ((vnode->originMount) && (vnode->originMount->mountFlags & MS_RDONLY) &&
          (((oflags & O_ACCMODE) != O_RDONLY) || (oflags & O_TRUNC)))
        {
          ret = -EROFS;
          VnodeDrop();
          goto errout;
        }
      if ((oflags & O_CREAT) && (oflags & O_EXCL))
        {
          ret = -EEXIST;
          VnodeDrop();
          goto errout;
        }
      if (vnode->type == VNODE_TYPE_DIR)
        {
          ret = -EISDIR;
          VnodeDrop();
          goto errout;
        }
      accmode = oflag_convert_mode(oflags);
      if (VfsVnodePermissionCheck(vnode, accmode))
        {
          ret = -EACCES;
          VnodeDrop();
          goto errout;
        }
    }

  if ((ret != OK) && (oflags & O_CREAT) && vnode)
    {
      /* if file not exist, but parent dir of the file is exist */
      if ((vnode->originMount) && (vnode->originMount->mountFlags & MS_RDONLY))
        {
          ret = -EROFS;
          VnodeDrop();
          goto errout;
        }
      if (VfsVnodePermissionCheck(vnode, (WRITE_OP | EXEC_OP)))
        {
          ret = -EACCES;
          VnodeDrop();
          goto errout;
        }
      parentVnode = vnode;
      ret = do_creat(parentVnode, fullpath, mode, &vnode);
      if (ret != OK)
        {
          VnodeDrop();
          goto errout;
        }
      vnode->filePath = strdup(fullpath);
    }

  if (ret != OK)
    {
      /* found nothing */
      VnodeDrop();
      goto errout;
    }
  vnode->useCount++;
  VnodeDrop();

  if (oflags & O_TRUNC)
    {
      if (vnode->useCount > 1)
        {
          ret = -EBUSY;
          goto errout_with_count;
        }

      if (vnode->vop->Truncate)
        {
          ret = vnode->vop->Truncate(vnode, 0);
          if (ret != OK)
            {
              goto errout_with_count;
            }
        }
      else
        {
          ret = -ENOSYS;
          goto errout_with_count;
        }
    }

  filep = files_allocate(vnode, oflags, 0, NULL, FILE_START_FD);
    if (filep == NULL)
    {
      ret = -EMFILE;
      goto errout_with_count;
    }

  if (filep->ops && filep->ops->open)
    {
      ret = filep->ops->open(filep);
    }

  if (ret < 0)
    {
      files_release(filep->fd);
      goto errout_with_count;
    }

  if (fullpath)
    {
      free(fullpath);
    }
  return filep->fd;

errout_with_count:
  VnodeHold();
  vnode->useCount--;
  VnodeDrop();
errout:
  if (fullpath)
    {
      free(fullpath);
    }
  set_errno(-ret);
  return VFS_ERROR;
}

int do_open(int dirfd, const char *path, int oflags, mode_t mode)
{
  int ret;
  int fd;

  if ((oflags & (O_WRONLY | O_CREAT)) != 0)
    {
      mode &= ~GetUmask();
      mode &= (S_IRWXU | S_IRWXG | S_IRWXO);
    }

  fd = fp_open(dirfd, path, oflags, mode);
  if (fd < 0)
    {
      ret = -get_errno();
      goto errout;
    }

  return fd;

errout:
  set_errno(-ret);
  return VFS_ERROR;
}

/****************************************************************************
 * Name: open
 *
 * Description: Standard 'open' interface
 *
 ****************************************************************************/

int open(const char *path, int oflags, ...)
{
  mode_t mode = DEFAULT_FILE_MODE; /* File read-write properties. */
#ifdef LOSCFG_FILE_MODE
  va_list ap;
  va_start(ap, oflags);
  mode = va_arg(ap, int);
  va_end(ap);
#endif

  return do_open(AT_FDCWD, path, oflags, mode);
}

int open64 (const char *__path, int __oflag, ...)
{
  mode_t mode = DEFAULT_FILE_MODE; /* File read-write properties. */
#ifdef LOSCFG_FILE_MODE
  va_list ap;
  va_start(ap, __oflag);
  mode = va_arg(ap, int);
  va_end(ap);
  if ((__oflag & (O_WRONLY | O_CREAT)) != 0)
    {
      mode &= ~GetUmask();
      mode &= (S_IRWXU|S_IRWXG|S_IRWXO);
    }
#endif
  return open (__path, ((unsigned int)__oflag) | O_LARGEFILE, mode);
}
