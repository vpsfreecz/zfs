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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define	MMAP_QUOTA_SIGBUS_EXIT	64

static void
sigbus_handler(int sig)
{
	(void) sig;
	_exit(MMAP_QUOTA_SIGBUS_EXIT);
}

int
main(int argc, char **argv)
{
	struct sigaction sa = {0};
	char *endp;
	long page_size;
	off_t offset;
	char *addr;
	int fd;

	if (argc != 3) {
		(void) fprintf(stderr, "usage: %s <file> <offset>\n", argv[0]);
		return (1);
	}

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0) {
		perror("sysconf");
		return (1);
	}

	errno = 0;
	offset = (off_t)strtoll(argv[2], &endp, 0);
	if (errno != 0 || *endp != '\0' || offset < 0 ||
	    (offset % page_size) != 0) {
		(void) fprintf(stderr, "invalid offset: %s\n", argv[2]);
		return (1);
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror("open");
		return (errno);
	}

	sa.sa_handler = sigbus_handler;
	if (sigemptyset(&sa.sa_mask) != 0) {
		perror("sigemptyset");
		(void) close(fd);
		return (errno);
	}
	if (sigaction(SIGBUS, &sa, NULL) != 0) {
		perror("sigaction");
		(void) close(fd);
		return (errno);
	}

	addr = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    offset);
	if (addr == MAP_FAILED) {
		perror("mmap");
		(void) close(fd);
		return (errno);
	}

	addr[0] = 'q';

	if (msync(addr, page_size, MS_SYNC) != 0) {
		perror("msync");
		(void) munmap(addr, page_size);
		(void) close(fd);
		return (errno);
	}

	if (fsync(fd) != 0) {
		perror("fsync");
		(void) munmap(addr, page_size);
		(void) close(fd);
		return (errno);
	}

	if (munmap(addr, page_size) != 0) {
		perror("munmap");
		(void) close(fd);
		return (errno);
	}

	if (close(fd) != 0) {
		perror("close");
		return (errno);
	}

	return (0);
}
