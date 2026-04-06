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
 * Copyright (c) 2026 Pavel Snajdr
 */

#include <sys/types.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ioctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static void
usage(const char *name)
{
	fprintf(stderr, "usage: %s POOL guard|unterminated MARKER\n", name);
	exit(2);
}

int
main(int argc, char **argv)
{
	if (argc != 4)
		usage(argv[0]);

	const char *pool = argv[1];
	const char *mode = argv[2];
	const char *marker = argv[3];
	size_t marker_size = strlen(marker) + 1;
	long page_size = sysconf(_SC_PAGESIZE);
	void *mapping;
	char *history;
	size_t mapping_size;

	if (page_size <= 0)
		err(1, "sysconf(_SC_PAGESIZE)");
	if (marker_size >= HIS_MAX_RECORD_LEN || marker_size > page_size)
		errx(1, "marker is too long");

	if (strcmp(mode, "guard") == 0) {
		mapping_size = 2 * page_size;
		mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mapping == MAP_FAILED)
			err(1, "mmap");
		if (mprotect((char *)mapping + page_size, page_size,
		    PROT_NONE) != 0)
			err(1, "mprotect");
		history = (char *)mapping + page_size - marker_size;
		memcpy(history, marker, marker_size);
	} else if (strcmp(mode, "unterminated") == 0) {
		mapping_size = P2ROUNDUP(HIS_MAX_RECORD_LEN, page_size);
		mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mapping == MAP_FAILED)
			err(1, "mmap");
		history = mapping;
		memset(history, 'X', HIS_MAX_RECORD_LEN);
		memcpy(history, marker, marker_size - 1);
	} else {
		usage(argv[0]);
	}

	int fd = open(ZFS_DEV, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		err(1, "open(%s)", ZFS_DEV);

	zfs_cmd_t zc = { 0 };
	if (snprintf(zc.zc_name, sizeof (zc.zc_name), "%s", pool) >=
	    sizeof (zc.zc_name))
		errx(1, "pool name is too long");
	zc.zc_history = (uint64_t)(uintptr_t)history;

	if (ioctl(fd, ZFS_IOC_POOL_EXPORT, &zc) == 0)
		errx(1, "test pool was unexpectedly exported");
	if (errno != EBUSY)
		err(1, "export ioctl did not reach the busy pool");

	if (close(fd) != 0)
		err(1, "close");
	if (munmap(mapping, mapping_size) != 0)
		err(1, "munmap");
	return (0);
}
