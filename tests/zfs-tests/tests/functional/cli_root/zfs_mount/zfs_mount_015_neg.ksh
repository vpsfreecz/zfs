#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib

verify_runnable "both"

is_linux || log_unsupported "Linux-specific mount option parser test"

function cleanup
{
	if mounted $TESTFS; then
		log_must zfs unmount $TESTFS
	fi

	log_must zfs mount $TESTFS
}

log_assert "Unknown mount and remount options are rejected"
log_onexit cleanup

TESTFS=$TESTPOOL/$TESTFS

datasetexists $TESTFS || log_must zfs create $TESTFS
log_must zfs mount $TESTFS
MNTPFS=$(get_prop mountpoint $TESTFS)

log_mustnot mount -o remount,notarealopt $TESTFS $MNTPFS

log_must zfs unmount $TESTFS
log_mustnot mount -t zfs -o notarealopt $TESTFS $MNTPFS
log_must zfs mount $TESTFS

log_pass "Unknown mount and remount options are rejected"
