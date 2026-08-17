#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# Spare devices (both files and disks) can be shared among different ZFS pools.
#
# STRATEGY:
# 1. Create two pools
# 2. Add the same spare device to different pools
# 3. Inject IO errors in the first pool and start a scrub
# 4. Verify ZED activates and releases the shared spare
# 5. Repeat the fault and recovery in the second pool
# 6. Verify both pools are online and the shared spare is available
#

verify_runnable "both"

if is_linux; then
	# Add one 512b spare device (4Kn would generate IO errors on replace)
	# NOTE: must be larger than other "file" vdevs and minimum SPA devsize:
	# add 32m of fudge
	load_scsi_debug $(($MINVDEVSIZE/1024/1024+32)) 1 1 1 '512b'
else
	log_unsupported "scsi debug module unsupported"
fi

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL
	destroy_pool $TESTPOOL1
	unload_scsi_debug
	rm -f $SAFE_FILEDEVPOOL1 $SAFE_FILEDEVPOOL2 $FAIL_FILEDEVPOOL1 \
	    $FAIL_FILEDEVPOOL2 $SPARE_FILEDEV
}

log_assert "Spare devices can be shared among different ZFS pools"
log_onexit cleanup

# Clear events from previous runs
zed_events_drain

SAFE_FILEDEVPOOL1="$TEST_BASE_DIR/file-safe-dev1"
FAIL_FILEDEVPOOL1="$TEST_BASE_DIR/file-fail-dev1"
SAFE_FILEDEVPOOL2="$TEST_BASE_DIR/file-safe-dev2"
FAIL_FILEDEVPOOL2="$TEST_BASE_DIR/file-fail-dev2"
SPARE_FILEDEV="$TEST_BASE_DIR/file-spare-dev"
SPARE_DISKDEV="$(get_debug_device)"

log_must truncate -s $MINVDEVSIZE $SAFE_FILEDEVPOOL1 $SAFE_FILEDEVPOOL2 $FAIL_FILEDEVPOOL1 $FAIL_FILEDEVPOOL2 $SPARE_FILEDEV

function exercise_shared_spare # pool peer faildev spare
{
	typeset pool=$1
	typeset peer=$2
	typeset faildev=$3
	typeset spare=$4

	# A shared spare can serve only one pool at a time. Exercise each pool
	# independently so the other pool cannot claim it during recovery.
	log_must zinject -d $faildev -e io -T all -f 100 $pool
	log_must zpool scrub $pool

	log_note "Wait for ZED to auto-spare in $pool"
	log_must wait_vdev_state $pool $faildev "FAULTED" 60
	log_must wait_vdev_state $pool $spare "ONLINE" 60
	log_must wait_hotspare_state $pool $spare "INUSE"
	log_must check_state $pool "" "DEGRADED"
	log_must check_state $peer "" "ONLINE"

	log_must zinject -c all
	log_must zpool clear $pool $faildev

	log_must wait_vdev_state $pool $faildev "ONLINE" 60
	log_must wait_hotspare_state $pool $spare "AVAIL"
	log_must is_pool_resilvered $pool
	log_must check_state $pool "" "ONLINE"
	log_must check_state $peer "" "ONLINE"
}

for spare in $SPARE_FILEDEV $SPARE_DISKDEV; do
	# 1. Create two pools
	log_must zpool create -f $TESTPOOL mirror $SAFE_FILEDEVPOOL1 $FAIL_FILEDEVPOOL1
	log_must zpool create -f $TESTPOOL1 mirror $SAFE_FILEDEVPOOL2 $FAIL_FILEDEVPOOL2

	# 2. Add the same spare device to different pools
	log_must_busy zpool add $TESTPOOL spare $spare
	log_must_busy zpool add $TESTPOOL1 spare $spare
	log_must wait_hotspare_state $TESTPOOL $spare "AVAIL"
	log_must wait_hotspare_state $TESTPOOL1 $spare "AVAIL"

	# 3-6. Prove the same spare can serve and recover from both pools.
	exercise_shared_spare $TESTPOOL $TESTPOOL1 $FAIL_FILEDEVPOOL1 $spare
	log_must wait_hotspare_state $TESTPOOL1 $spare "AVAIL"
	exercise_shared_spare $TESTPOOL1 $TESTPOOL $FAIL_FILEDEVPOOL2 $spare
	log_must wait_hotspare_state $TESTPOOL $spare "AVAIL"

	# Cleanup
	destroy_pool $TESTPOOL
	destroy_pool $TESTPOOL1
done

log_pass "Spare devices can be shared among different ZFS pools"
