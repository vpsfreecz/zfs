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
 * Copyright (c) 2014 by Chunwei Chen. All rights reserved.
 * Copyright (c) 2016, 2019 by Delphix. All rights reserved.
 */

#ifndef _ABD_OS_H
#define	_ABD_OS_H

#ifdef __cplusplus
extern "C" {
#endif

struct abd;

struct abd_scatter {
	uint_t		abd_offset;
	uint_t		abd_nents;
	struct scatterlist *abd_sgl;
};

struct abd_linear {
	void		*abd_buf;
	struct scatterlist *abd_sgl; /* for LINEAR_PAGE */
};

typedef int abd_iter_page_func_t(struct page *, size_t, size_t, void *);
int abd_iterate_page_func(struct abd *, size_t, size_t, abd_iter_page_func_t *,
    void *);

/*
 * Linux ABD bio functions
 * Note: these are only needed to support vdev_classic. See comment in
 * vdev_disk.c.
 */
unsigned int abd_bio_map_off(struct bio *, struct abd *, unsigned int, size_t);
unsigned long abd_nr_pages_off(struct abd *, unsigned int, size_t);

__attribute__((malloc))
struct abd *abd_alloc_from_pages(struct page **, unsigned long, uint64_t);

/*
 * Mark zfs data pages so they can be excluded from kernel crash dumps
 */
#ifdef _LP64
#define	ABD_FILE_CACHE_PAGE	0x2F5ABDF11ECAC4E

static inline void
abd_mark_zfs_page(struct page *page)
{
	get_page(page);
	SetPagePrivate(page);
	set_page_private(page, ABD_FILE_CACHE_PAGE);
}

static inline void
abd_unmark_zfs_page(struct page *page)
{
	set_page_private(page, 0UL);
	ClearPagePrivate(page);
	put_page(page);
}
#else
#define	abd_mark_zfs_page(page)
#define	abd_unmark_zfs_page(page)
#endif /* _LP64 */

#ifdef __cplusplus
}
#endif

#endif	/* _ABD_H */
