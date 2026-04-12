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
#include <sys/zfs_ioctl.h>
#include <sys/zio_checksum.h>

#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zfs_fletcher.h"

typedef enum mangle_mode {
	MANGLE_WRITE_ZERO,
	MANGLE_WRITE_OVERSIZE,
	MANGLE_WRITE_OFF_COMPRESSED,
	MANGLE_WRITE_COMPRESSED_OVERSIZE,
	MANGLE_SPILL_OFF,
	MANGLE_SPILL_COMPRESSED_OVERSIZE,
} mangle_mode_t;

static void
usage(const char *name)
{
	fprintf(stderr, "usage: %s MODE <input >output\n", name);
	fprintf(stderr, "modes: write-zero, write-oversize, "
	    "write-off-compressed, write-compressed-oversize, spill-off, "
	    "spill-compressed-oversize\n");
	exit(2);
}

static mangle_mode_t
parse_mode(const char *name, const char *mode)
{
	if (strcmp(mode, "write-zero") == 0)
		return (MANGLE_WRITE_ZERO);
	if (strcmp(mode, "write-oversize") == 0)
		return (MANGLE_WRITE_OVERSIZE);
	if (strcmp(mode, "write-off-compressed") == 0)
		return (MANGLE_WRITE_OFF_COMPRESSED);
	if (strcmp(mode, "write-compressed-oversize") == 0)
		return (MANGLE_WRITE_COMPRESSED_OVERSIZE);
	if (strcmp(mode, "spill-off") == 0)
		return (MANGLE_SPILL_OFF);
	if (strcmp(mode, "spill-compressed-oversize") == 0)
		return (MANGLE_SPILL_COMPRESSED_OVERSIZE);

	usage(name);
	abort();
}

static boolean_t
read_exact(void *buf, size_t size, boolean_t allow_eof)
{
	size_t done = fread(buf, 1, size, stdin);

	if (done == size)
		return (B_TRUE);
	if (done == 0 && feof(stdin) && allow_eof)
		return (B_FALSE);
	if (ferror(stdin))
		err(1, "read");
	err(1, "truncated send stream");
}

static void
write_exact(const void *buf, size_t size)
{
	if (fwrite(buf, 1, size, stdout) != size)
		err(1, "write");
}

static void
dump_record(dmu_replay_record_t *drr, void *payload,
    size_t payload_size, zio_cksum_t *stream_cksum)
{
	size_t checksum_offset = offsetof(dmu_replay_record_t,
	    drr_u.drr_checksum.drr_checksum);

	if (checksum_offset != sizeof (*drr) - sizeof (zio_cksum_t))
		errx(1, "unexpected replay-record checksum layout");

	fletcher_4_incremental_native(drr, checksum_offset, stream_cksum);
	if (drr->drr_type != DRR_BEGIN) {
		if (!ZIO_CHECKSUM_IS_ZERO(
		    &drr->drr_u.drr_checksum.drr_checksum))
			errx(1, "record checksum was not cleared");
		drr->drr_u.drr_checksum.drr_checksum = *stream_cksum;
	}
	fletcher_4_incremental_native(
	    &drr->drr_u.drr_checksum.drr_checksum,
	    sizeof (zio_cksum_t), stream_cksum);
	write_exact(drr, sizeof (*drr));

	if (payload_size != 0) {
		fletcher_4_incremental_native(payload, payload_size,
		    stream_cksum);
		write_exact(payload, payload_size);
	}
}

static size_t
record_payload_size(const dmu_replay_record_t *drr)
{
	switch (drr->drr_type) {
	case DRR_BEGIN:
		return (drr->drr_payloadlen);
	case DRR_OBJECT:
		return (DRR_OBJECT_PAYLOAD_SIZE(&drr->drr_u.drr_object));
	case DRR_WRITE:
		return (DRR_WRITE_PAYLOAD_SIZE(&drr->drr_u.drr_write));
	case DRR_SPILL:
		return (DRR_SPILL_PAYLOAD_SIZE(&drr->drr_u.drr_spill));
	case DRR_WRITE_EMBEDDED:
		return (P2ROUNDUP((uint64_t)
		    drr->drr_u.drr_write_embedded.drr_psize, 8));
	case DRR_FREEOBJECTS:
	case DRR_WRITE_BYREF:
	case DRR_FREE:
	case DRR_END:
	case DRR_OBJECT_RANGE:
	case DRR_REDACT:
		return (0);
	default:
		errx(1, "unsupported replay record type %u", drr->drr_type);
	}
}

static boolean_t
mangle_record(dmu_replay_record_t *drr, mangle_mode_t mode,
    size_t payload_size)
{
	struct drr_write *drrw = &drr->drr_u.drr_write;
	struct drr_spill *drrs = &drr->drr_u.drr_spill;

	switch (mode) {
	case MANGLE_WRITE_ZERO:
		if (drr->drr_type != DRR_WRITE)
			return (B_FALSE);
		drrw->drr_logical_size = 0;
		drrw->drr_compressiontype = ZIO_COMPRESS_LZ4;
		drrw->drr_compressed_size = payload_size;
		break;
	case MANGLE_WRITE_OVERSIZE:
		if (drr->drr_type != DRR_WRITE)
			return (B_FALSE);
		drrw->drr_logical_size = SPA_MAXBLOCKSIZE + SPA_MINBLOCKSIZE;
		drrw->drr_compressiontype = ZIO_COMPRESS_LZ4;
		drrw->drr_compressed_size = payload_size;
		break;
	case MANGLE_WRITE_OFF_COMPRESSED:
		if (drr->drr_type != DRR_WRITE)
			return (B_FALSE);
		drrw->drr_compressiontype = ZIO_COMPRESS_OFF;
		drrw->drr_compressed_size = payload_size;
		break;
	case MANGLE_WRITE_COMPRESSED_OVERSIZE:
		if (drr->drr_type != DRR_WRITE)
			return (B_FALSE);
		if (payload_size <= 8)
			errx(1, "WRITE payload is too small to mangle");
		drrw->drr_compressiontype = ZIO_COMPRESS_LZ4;
		drrw->drr_logical_size = payload_size - 8;
		drrw->drr_compressed_size = payload_size;
		break;
	case MANGLE_SPILL_OFF:
		if (drr->drr_type != DRR_SPILL)
			return (B_FALSE);
		drrs->drr_compressiontype = ZIO_COMPRESS_OFF;
		drrs->drr_compressed_size = 0;
		break;
	case MANGLE_SPILL_COMPRESSED_OVERSIZE:
		if (drr->drr_type != DRR_SPILL)
			return (B_FALSE);
		if (payload_size == 0)
			errx(1, "SPILL payload is empty");
		drrs->drr_compressiontype = ZIO_COMPRESS_LZ4;
		drrs->drr_length = payload_size - 1;
		drrs->drr_compressed_size = payload_size;
		break;
	}

	return (B_TRUE);
}

int
main(int argc, char **argv)
{
	if (argc != 2)
		usage(argv[0]);

	mangle_mode_t mode = parse_mode(argv[0], argv[1]);
	dmu_replay_record_t drr;
	zio_cksum_t stream_cksum;
	boolean_t changed = B_FALSE;
	void *payload = NULL;
	size_t payload_capacity = 0;

	fletcher_4_init();
	while (read_exact(&drr, sizeof (drr), B_TRUE)) {
		if (drr.drr_type == DRR_BEGIN)
			ZIO_SET_CHECKSUM(&stream_cksum, 0, 0, 0, 0);

		size_t payload_size = record_payload_size(&drr);
		if (payload_size > (1U << 28))
			errx(1, "record payload is too large");
		if (payload_size > payload_capacity) {
			void *new_payload = realloc(payload, payload_size);
			if (new_payload == NULL)
				err(1, "realloc");
			payload = new_payload;
			payload_capacity = payload_size;
		}
		if (payload_size != 0)
			(void) read_exact(payload, payload_size, B_FALSE);

		if (!changed)
			changed = mangle_record(&drr, mode, payload_size);

		if (drr.drr_type != DRR_BEGIN) {
			memset(&drr.drr_u.drr_checksum.drr_checksum, 0,
			    sizeof (drr.drr_u.drr_checksum.drr_checksum));
		}

		if (drr.drr_type == DRR_END &&
		    !ZIO_CHECKSUM_IS_ZERO(&drr.drr_u.drr_end.drr_checksum))
			drr.drr_u.drr_end.drr_checksum = stream_cksum;

		dump_record(&drr, payload, payload_size, &stream_cksum);
		if (drr.drr_type == DRR_END)
			ZIO_SET_CHECKSUM(&stream_cksum, 0, 0, 0, 0);
	}

	free(payload);
	fletcher_4_fini();
	if (!changed)
		errx(1, "send stream has no record matching mode %s", argv[1]);
	if (fflush(stdout) != 0)
		err(1, "flush");
	return (0);
}
