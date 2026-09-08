// SPDX-License-Identifier: CDDL-1.0
/*
 * Truncate an mmap write source after prefault, but before copying it.
 *
 * The last iovec is a missing userfaultfd page.  Its prefault stops the
 * writer after the preceding file mapping has been faulted in, without
 * injecting a fault into ZFS.  Truncate that file and resolve the last
 * page: the copy must return EFAULT (or its completed prefix), not retry
 * forever while retaining the destination range lock.
 *
 * Run only in a disposable test environment: a broken kernel can leave
 * the writer unkillable.  Requires permission for kernel userfaultfd faults.
 */

#define	_GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <linux/userfaultfd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct writer {
	int fd;
	struct iovec iov[3];
	int niov;
	ssize_t result;
	int error;
};

static void
fail(const char *what)
{
	perror(what);
	exit(1);
}

static void *
write_thread(void *arg)
{
	struct writer *w = arg;
	w->result = pwritev(w->fd, w->iov, w->niov, 0);
	w->error = errno;
	return (NULL);
}

static void
run_case(const char *src, const char *dst, size_t page, int truncate_source,
    int prefix)
{
	size_t length = 14 * page;
	size_t total = length + page + (prefix ? page : 0);
	/* Do not take the whole-block fill/rollback path in the prefix case. */
	size_t destination_size = total + page;
	unsigned char *data = malloc(destination_size);
	unsigned char *actual = malloc(destination_size);
	if (data == NULL || actual == NULL)
		fail("malloc");
	int sfd = open(src, O_CREAT | O_TRUNC | O_RDWR, 0600);
	int dfd = open(dst, O_CREAT | O_TRUNC | O_RDWR, 0600);
	if (sfd < 0 || dfd < 0)
		fail("open");
	(void) memset(data, 'a', total);
	if (write(sfd, data, length) != (ssize_t)length)
		fail("write source");
	(void) memset(data, 'z', destination_size);
	if (write(dfd, data, destination_size) != (ssize_t)destination_size ||
	    fsync(dfd) != 0)
		fail("initialize destination");
	void *mapping = mmap(NULL, length, PROT_READ, MAP_SHARED, sfd, 0);
	void *gate = mmap(NULL, page, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	void *gate_data = mmap(NULL, page, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED || gate == MAP_FAILED ||
	    gate_data == MAP_FAILED)
		fail("mmap");
	(void) memset(gate_data, 'b', page);
	int uffd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd < 0) {
		if (errno == ENOSYS || errno == EPERM || errno == EACCES) {
			(void) fprintf(stderr,
			    "kernel userfaultfd unavailable\n");
			exit(77);
		}
		fail("userfaultfd");
	}
	struct uffdio_api api = { .api = UFFD_API };
	if (ioctl(uffd, UFFDIO_API, &api) != 0)
		fail("UFFDIO_API");
	struct uffdio_register reg = {
		.range = { .start = (unsigned long)gate, .len = page },
		.mode = UFFDIO_REGISTER_MODE_MISSING
	};
	if (ioctl(uffd, UFFDIO_REGISTER, &reg) != 0)
		fail("UFFDIO_REGISTER");
	struct writer w = { .fd = dfd };
	if (prefix) {
		(void) memset(data, 'p', page);
		w.iov[w.niov].iov_base = data;
		w.iov[w.niov++].iov_len = page;
	}
	w.iov[w.niov].iov_base = mapping;
	w.iov[w.niov++].iov_len = length;
	w.iov[w.niov].iov_base = gate;
	w.iov[w.niov++].iov_len = page;
	pthread_t thread;
	int error = pthread_create(&thread, NULL, write_thread, &w);
	if (error != 0) {
		errno = error;
		fail("pthread_create");
	}
	struct pollfd pfd = { .fd = uffd, .events = POLLIN };
	if (poll(&pfd, 1, 10000) != 1 || !(pfd.revents & POLLIN)) {
		errno = ETIMEDOUT;
		fail("waiting for prefault gate");
	}
	struct uffd_msg msg;
	if (read(uffd, &msg, sizeof (msg)) != sizeof (msg) ||
	    msg.event != UFFD_EVENT_PAGEFAULT ||
	    msg.arg.pagefault.address < (unsigned long)gate ||
	    msg.arg.pagefault.address >= (unsigned long)gate + page) {
		errno = EINVAL;
		fail("unexpected userfault event");
	}
	if (truncate_source && ftruncate(sfd, 0) != 0)
		fail("truncate source");
	struct uffdio_copy copy = {
		.dst = (unsigned long)gate,
		.src = (unsigned long)gate_data,
		.len = page
	};
	if (ioctl(uffd, UFFDIO_COPY, &copy) != 0 ||
	    copy.copy != (ssize_t)page)
		fail("UFFDIO_COPY");
	struct timespec deadline;
	if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
		fail("clock_gettime");
	deadline.tv_sec += 10;
	error = pthread_timedjoin_np(thread, NULL, &deadline);
	if (error != 0) {
		errno = error;
		fail("writer made no bounded progress");
	}
	ssize_t expected = truncate_source ?
	    (prefix ? (ssize_t)page : -1) : (ssize_t)total;
	if (w.result != expected || (expected == -1 && w.error != EFAULT)) {
		(void) fprintf(stderr,
		    "write returned %zd errno %d, wanted %zd\n",
		    w.result, w.error, expected);
		exit(1);
	}
	(void) memset(data, 'z', destination_size);
	if (!truncate_source) {
		(void) memset(data, 'a', length);
		(void) memset(data + length, 'b', page);
	} else if (prefix) {
		(void) memset(data, 'p', page);
	}
	/* A read also proves that the failed writer released its range lock. */
	if (pread(dfd, actual, destination_size, 0) !=
	    (ssize_t)destination_size ||
	    memcmp(data, actual, destination_size) != 0) {
		(void) fprintf(stderr,
		    "destination contents changed incorrectly\n");
		exit(1);
	}
	if (fsync(dfd) != 0 || ftruncate(dfd, 0) != 0)
		fail("destination remains locked");
	(void) close(uffd);
	(void) munmap(mapping, length);
	(void) munmap(gate, page);
	(void) munmap(gate_data, page);
	(void) close(sfd);
	(void) close(dfd);
	free(data);
	free(actual);
	(void) printf(
	    "truncate=%d prefix=%d: correct return, data and unlock\n",
	    truncate_source, prefix);
	(void) fflush(stdout);
}

int
main(int argc, char **argv)
{
	if (argc != 3) {
		(void) fprintf(stderr,
		    "usage: %s source destination\n", argv[0]);
		return (2);
	}
	long page = sysconf(_SC_PAGESIZE);
	if (page <= 0)
		fail("page size");
	run_case(argv[1], argv[2], page, 0, 0);
	run_case(argv[1], argv[2], page, 1, 0);
	run_case(argv[1], argv[2], page, 1, 1);
	return (0);
}
