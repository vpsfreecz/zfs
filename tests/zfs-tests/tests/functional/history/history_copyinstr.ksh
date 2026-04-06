#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

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

#
# Copyright (c) 2026 Pavel Snajdr
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"
is_linux || log_unsupported "copyinstr_history is Linux-only"

log_assert "copyinstr stops at NUL and rejects unterminated input"

typeset guard=COPYINSTR_GUARD_$$_$RANDOM
typeset unterminated=COPYINSTR_UNTERMINATED_$$_$RANDOM
typeset hold=$TESTDIR/copyinstr-hold

function cleanup
{
	exec 9>&-
	rm -f $hold
}

log_onexit cleanup

# Keep the mounted dataset busy so the legacy export ioctl logs its supplied
# history string but cannot actually export the test pool.
exec 9>$hold

log_must copyinstr_history $TESTPOOL guard $guard
log_must eval "zpool history -il $TESTPOOL | grep -F $guard"

log_must copyinstr_history $TESTPOOL unterminated $unterminated
log_mustnot eval "zpool history -il $TESTPOOL | grep -F $unterminated"

log_pass "copyinstr stops at NUL and rejects unterminated input"
