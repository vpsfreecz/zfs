// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

#ifndef _ZFS_PAGEMAP_COMPAT_H
#define	_ZFS_PAGEMAP_COMPAT_H

#include <linux/mm_compat.h>
#include <linux/pagemap.h>

#if !defined(HAVE_PAGEMAP_READAHEAD_PAGE) && \
    defined(HAVE_PAGEMAP_READAHEAD_FOLIO)
static inline struct page *
zfs_readahead_page(struct readahead_control *ractl)
{
	struct folio *folio = readahead_folio(ractl);
	struct page *page;

	if (folio == NULL)
		return (NULL);

	page = folio_page(folio, 0);
	get_page(page);
	return (page);
}
#define	readahead_page(ractl) zfs_readahead_page(ractl)
#endif

#endif
