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
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2025, OpenZFS Contributors.
# All rights reserved.
#


. $STF_SUITE/include/libtest.shlib

if [[ "$(uname -s)" != "Linux" ]]; then
    log_skip "O_TMPFILE is not supported on this platform."
fi

verify_runnable "global"

log_assert "O_TMPFILE linking with xattr=sa should survive replay"

log_must zfs set xattr=sa $TESTPOOL/$TESTFS

typeset target_uid target_gid
if [[ $(id -u) -eq 0 ]]; then
    target_uid=1000
    target_gid=1001
else
    target_uid=$(id -u)
    target_gid=$(id -g)
fi
log_must [[ -n "$target_uid" && -n "$target_gid" ]]
export TARGET_UID="$target_uid"
export TARGET_GID="$target_gid"

export TESTFILE="tmpfile_replay.$$"

# Test under both sync modes
typeset sync_mode
for sync_mode in standard always; do
    log_note "Running O_TMPFILE crash-replay test with sync=$sync_mode"
    log_must zfs set sync=$sync_mode $TESTPOOL/$TESTFS
    # Run the C helper which performs the operations and verification
    log_must $STF_SUITE/tests/functional/tmpfile/tmpfile_replay
done

log_pass "Crash-replay of O_TMPFILE linking (xattr=sa) verified."
