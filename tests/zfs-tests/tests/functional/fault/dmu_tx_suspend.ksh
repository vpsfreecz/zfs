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

#
# Copyright (c) 2026 Pavel Snajdr
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

log_assert "transaction assignment waiters honor pool suspension"

typeset -i writer_pid=0
typeset -i acl_pid=0
typeset old_dirty_data_max=$(get_tunable DIRTY_DATA_MAX)
typeset old_txg_timeout=$(get_tunable TXG_TIMEOUT)

function cleanup
{
	(( writer_pid != 0 )) && kill -9 $writer_pid 2>/dev/null || true
	(( acl_pid != 0 )) && kill -9 $acl_pid 2>/dev/null || true
	zinject -c all >/dev/null 2>&1 || true
	zpool clear $TESTPOOL >/dev/null 2>&1 || true
	set_tunable64 DIRTY_DATA_MAX $old_dirty_data_max || true
	set_tunable32 TXG_TIMEOUT $old_txg_timeout || true
	destroy_pool $TESTPOOL
}

log_onexit cleanup

typeset disk=${DISKS%% *}
typeset dataset=$TESTPOOL/$TESTFS
typeset mountpoint=/$dataset
typeset file=$mountpoint/file
typeset -i dirty_before=$(kstat dmu_tx.dmu_tx_dirty_over_max)

log_must zpool create -f -o failmode=continue $TESTPOOL $disk
log_must zfs create -o acltype=posix $dataset
log_must touch $file
log_must setfacl -m u:65534:r-- $file
log_must eval "getfacl -n \"$file\" 2>/dev/null | " \
	"grep -qx 'user:65534:r--'"

# Keep physical writes slow enough to fill a deliberately small dirty-data
# window.  The kstat transition proves that the writer reached the
# dp_spaceavail_cv branch before the pool is suspended.
log_must set_tunable64 DIRTY_DATA_MAX $((16 * 1024 * 1024))
log_must set_tunable32 TXG_TIMEOUT 600
log_must zinject -d $disk -D1000:1 -T write $TESTPOOL

dd if=/dev/zero of=$file bs=1M count=1024 >/dev/null 2>&1 &
writer_pid=$!

typeset -i tries=300
while (( $(kstat dmu_tx.dmu_tx_dirty_over_max) <= dirty_before )); do
	if (( tries-- == 0 )); then
		log_fail "writer did not enter the dirty-space transaction wait"
	fi
	if ! kill -0 $writer_pid 2>/dev/null; then
		log_fail "writer exited before reaching the dirty-space wait"
	fi
	sleep 0.1
done

# Turn the outstanding slow writes into a suspended pool.  Both the dirty
# waiter and a new ZPL ACL transaction exercising DMU_TX_NOWAIT error mapping
# must return an error under failmode=continue instead of sleeping forever.
log_must zinject -d $disk -e io -T write $TESTPOOL
log_must zinject -d $disk -e nxio -T probe $TESTPOOL
log_must set_tunable32 TXG_TIMEOUT 1

tries=300
until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]]; do
	if (( tries-- == 0 )); then
		log_fail "pool did not suspend"
	fi
	sleep 0.1
done

tries=100
while kill -0 $writer_pid 2>/dev/null; do
	if (( tries-- == 0 )); then
		log_fail "dirty-space transaction waiter ignored suspension"
	fi
	sleep 0.1
done
wait $writer_pid
typeset -i writer_rc=$?
writer_pid=0
(( writer_rc != 0 )) || log_fail "suspended dirty writer returned success"

setfacl -m u:65534:rw- $file >/dev/null 2>&1 &
acl_pid=$!

tries=100
while kill -0 $acl_pid 2>/dev/null; do
	if (( tries-- == 0 )); then
		log_fail "DMU_TX_NOWAIT ACL transaction ignored suspension"
	fi
	sleep 0.1
done
wait $acl_pid
typeset -i acl_rc=$?
acl_pid=0
(( acl_rc != 0 )) || log_fail "ACL update succeeded on suspended pool"

log_pass "transaction assignment waiters honor pool suspension"
