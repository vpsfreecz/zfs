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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Quotas are enforced using the minimum of the two properties:
#	quota & refquota
#
# STRATEGY:
#	1. Set value for quota and refquota. Quota less than refquota.
#	2. Creating file which should be limited by quota.
#	3. Switch the value of quota and refquota.
#	4. Verify file should be limited by refquota.
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy -rf $TESTPOOL/$TESTFS
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "Quotas are enforced using the minimum of the two properties"
log_onexit cleanup

TESTFILE='testfile'
fs=$TESTPOOL/$TESTFS
log_must zfs set quota=15M $fs
log_must zfs set refquota=25M $fs

mntpnt=$(get_prop mountpoint $fs)
log_must mkfile 20M $mntpnt/$TESTFILE
log_must sync_pool $TESTPOOL
log_mustnot mkfile 1M $mntpnt/$TESTFILE.2
typeset -i used quota refquota
used=$(get_prop used $fs)
quota=$(get_prop quota $fs)
refquota=$(get_prop refquota $fs)
if ((used < quota || used >= refquota)); then
	log_fail "ERROR: used $used did not cross only quota $quota"
fi

#
# Switch the value of them and try again
#
log_must rm -f $mntpnt/$TESTFILE $mntpnt/$TESTFILE.2
log_must sync_pool $TESTPOOL
log_must zfs set quota=25M $fs
log_must zfs set refquota=15M $fs

log_must mkfile 20M $mntpnt/$TESTFILE
log_must sync_pool $TESTPOOL
log_mustnot mkfile 1M $mntpnt/$TESTFILE.2
used=$(get_prop used $fs)
quota=$(get_prop quota $fs)
refquota=$(get_prop refquota $fs)
if ((used < refquota || used >= quota)); then
	log_fail "ERROR: used $used did not cross only refquota $refquota"
fi

log_pass "Quotas are enforced using the minimum of the two properties"
