/****************************************************************************
 * include/nuttx/fs/automount.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __INCLUDE_NUTTX_AUDIO_AUTOMOUNT_H
#define __INCLUDE_NUTTX_AUDIO_AUTOMOUNT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "stdint.h"


#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************
 * Automounter configuration
 *   CONFIG_FS_AUTOMOUNTER - Enables automount support
 *
 * Prerequisites:
 *   CONFIG_SCHED_WORKQUEUE -  Work queue support is required
 *   And others that would only matter if you are working in a very minimal
 *   configuration.
 */

/* Helper macros ************************************************************/

#define AUTOMOUNT_ATTACH(s,isr,arg) ((s)->attach(s,isr,arg))
#define AUTOMOUNT_DETACH(s)         ((s)->attach(s,NULL,NULL))
#define AUTOMOUNT_ENABLE(s)         ((s)->enable(s,true))
#define AUTOMOUNT_DISABLE(s)        ((s)->enable(s,false))
#define AUTOMOUNT_INSERTED(s)       ((s)->inserted(s))

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* This is the type of the automount media change handler.  The lower level
 * code will intercept the interrupt and provide the upper level with the
 * private data that was provided when the interrupt was attached and will
 * also provide an indication if the media was inserted or removed.
 */

struct automount_lower_s; /* Forward reference.  Defined below */

typedef CODE int
  (*automount_handler_t)(FAR const struct automount_lower_s *lower,
                         FAR void *arg, bool inserted);

/* A reference to a structure of this type must be passed to the FS
 * automounter.  This structure provides information about the volume to be
 * mounted and provides board-specific hooks.
 *
 * Memory for this structure is provided by the caller.  It is not copied
 * by the automounter and is presumed to persist while the automounter
 * is active.
 */

struct automount_lower_s
{
  /* Volume characterization */

  FAR const char *fstype;     /* Type of file system */
  FAR const char *blockdev;   /* Path to the block device */
  FAR const char *mountpoint; /* Location to mount the volume */

  /* Debounce delay in system clock ticks.  Automount operation will not
   * be performed until the insertion/removal state has been unchanges
   * for this duration.
   */

  uint32_t ddelay;

  /* Unmount delay time in system clock ticks.  If a volume has open
   * references at the time that the media is removed, then we will be
   * unable to unmount it.  In that case, hopefully, the clients of the
   * mount will eventually fail with file access errors and eventually close
   * their references.  So, at some time later, it should be possible to
   * unmount the volume.  This delay specifies the time between umount
   * retries.
   */

  uint32_t udelay;

  /* Interrupt related operations all hidden behind callbacks to isolate the
   * automounter from differences in interrupt handling by varying boards
   * and MCUs.  Board interrupts should be configured both insertion and
   * removal of the media can be detected.
   *
   * attach  - Attach or detach the media change interrupt handler to the
   *           board level interrupt
   * enable  - Enable or disable the media change interrupt
   */

  CODE int (*attach)(FAR const struct automount_lower_s *lower,
                      automount_handler_t isr, FAR void *arg);
  CODE void (*enable)(FAR const struct automount_lower_s *lower,
                      bool enable);
  CODE bool (*inserted)(FAR const struct automount_lower_s *lower);
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: automount_initialize
 *
 * Description:
 *   Configure the automounter.
 *
 * Input Parameters:
 *   lower - Persistent board configuration data
 *
 * Returned Value:
 *   A void* handle.
 *           The only use for this handle is with automount_uninitialize().
 *   NULL is returned on any failure.
 *
 ****************************************************************************/

FAR void *automount_initialize(FAR const struct automount_lower_s *lower);

/****************************************************************************
 * Name: automount_uninitialize
 *
 * Description:
 *   Stop the automounter and free resources that it used.  NOTE that the
 *   mount is left in its last state mounted/unmounted state.
 *
 * Input Parameters:
 *   handle - The value previously returned by automount_initialize();
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void automount_uninitialize(FAR void *handle);

#undef EXTERN
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* __INCLUDE_NUTTX_AUDIO_AUTOMOUNT_H */
