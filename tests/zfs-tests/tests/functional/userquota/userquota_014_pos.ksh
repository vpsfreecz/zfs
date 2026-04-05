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
# Copyright (c) 2026.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

if ! is_linux; then
	log_unsupported "Linux-specific mmap quota test"
fi

readonly MMAP_SIGBUS_EXIT=64
readonly LINUX_EDQUOT=122

function cleanup
{
	log_must zfs set userquota@$QUSER1=none $QFS
	log_must zfs set recordsize=$orig_recsize $QFS
	log_must rm -f $QFILE
	recovery_writable $QFS
}

log_assert "Shared mmap writes must fail once user block quota is exhausted"
log_onexit cleanup

orig_recsize=$(get_prop recordsize $QFS)
PAGESIZE=$(getconf PAGESIZE)

mkmount_writable $QFS
log_must zfs set recordsize=$PAGESIZE $QFS
log_must rm -f $QFILE

log_must user_run $QUSER1 "file_write -o create -f $QFILE -b $PAGESIZE -c 1 -d 1"
log_must user_run $QUSER1 "truncate -s $((PAGESIZE * 2)) $QFILE"
sync_all_pools

used=$(get_prop userused@$QUSER1 $QFS)
log_must zfs set userquota@$QUSER1=$used $QFS

user_run $QUSER1 "file_write -o overwrite -f $QFILE -b $PAGESIZE -c 1 -s $PAGESIZE -d 1"
buffered_ret=$?
[[ $buffered_ret -eq $LINUX_EDQUOT ]] ||
	log_fail "Buffered write returned $buffered_ret, expected $LINUX_EDQUOT"

user_run $QUSER1 "mmap_quota_write $QFILE $PAGESIZE"
mmap_ret=$?
[[ $mmap_ret -eq $MMAP_SIGBUS_EXIT ]] ||
	log_fail "mmap write returned $mmap_ret, expected $MMAP_SIGBUS_EXIT"

sync_all_pools
used_after=$(get_prop userused@$QUSER1 $QFS)
[[ $used_after -eq $used ]] ||
	log_fail "mmap write consumed quota: $used -> $used_after"

log_pass "Shared mmap writes fail once user block quota is exhausted"
