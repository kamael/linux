/*
 * runlist.h - Defines for runlist handling in NTFS Linux kernel driver.
 *	       Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_NTFS_RUNLIST_H
#define _LINUX_NTFS_RUNLIST_H

#include "types.h"
#include "layout.h"
#include "volume.h"

static inline void ntfs_init_runlist(runlist *rl)
{
	rl->rl = NULL;
	init_rwsem(&rl->lock);
}

typedef enum {
	LCN_HOLE		= -1,	/* Keep this as highest value or die! */
	LCN_RL_NOT_MAPPED	= -2,
	LCN_ENOENT		= -3,
} LCN_SPECIAL_VALUES;

extern runlist_element *ntfs_mapping_pairs_decompress(const ntfs_volume *vol,
		const ATTR_RECORD *attr, runlist_element *old_rl);

extern LCN ntfs_rl_vcn_to_lcn(const runlist_element *rl, const VCN vcn);

#endif /* _LINUX_NTFS_RUNLIST_H */
