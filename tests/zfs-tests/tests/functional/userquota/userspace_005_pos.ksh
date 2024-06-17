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
# Copyright (c) 2026 by Pavel Snajdr. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       Check that zfs userspace reports explicit quotas instead of aborting
#       when default quota placeholders and real per-user quota rows coexist.
#
# STRATEGY:
#       1. Set default and explicit byte/object quotas for one user.
#       2. Create data so both used and objused rows are populated.
#       3. Verify zfs userspace succeeds and reports the explicit quota values
#          for both the live fs and a snapshot.
#

function cleanup
{
	datasetexists $snapfs && destroy_dataset $snapfs

	[[ -f $QFILE ]] && log_must rm -f $QFILE
	log_must cleanup_quota
}

function check_userspace_row
{
	typeset fs=$1
	typeset expected_quota=$2
	typeset expected_objquota=$3
	typeset row

	row=$(zfs userspace -H -p -t posixuser -o name,quota,objquota $fs |
	    awk -v user="$QUSER1" '$1 == user { print $2 " " $3 }')

	[[ "$row" == "$expected_quota $expected_objquota" ]] ||
		log_fail "unexpected userspace quota row for $fs: '$row'"
}

log_onexit cleanup

log_assert "zfs userspace prefers explicit quotas over default placeholders"

# Keep the defaults lower than the explicit quotas so we can distinguish them.
typeset default_quota=$UQUOTA_SIZE
typeset explicit_quota=$(($UQUOTA_SIZE * 2 + 1))
typeset default_objquota=8
typeset explicit_objquota=16

typeset snapfs=$QFS@snap

log_must zfs set defaultuserquota=$default_quota $QFS
log_must zfs set userquota@$QUSER1=$explicit_quota $QFS
log_must zfs set defaultuserobjquota=$default_objquota $QFS
log_must zfs set userobjquota@$QUSER1=$explicit_objquota $QFS

mkmount_writable $QFS
log_must user_run $QUSER1 mkfile 1m $QFILE
sync_all_pools

log_must zfs snapshot $snapfs

for fs in "$QFS" "$snapfs"; do
	log_must eval "zfs userspace -H -p -t posixuser \
	    -o name,quota,objquota $fs >/dev/null 2>&1"
	check_userspace_row $fs $explicit_quota $explicit_objquota
done

log_pass "zfs userspace reports explicit quotas over default placeholders"
