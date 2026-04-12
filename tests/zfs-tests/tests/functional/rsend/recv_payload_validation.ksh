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
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

verify_runnable "both"

log_assert "receive validates WRITE/SPILL geometry and accepts normal spills"

typeset source=$POOL/payload-source
typeset valid=$POOL/payload-valid
typeset stream=$BACKDIR/payload-validation.stream
typeset attrvalue=abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz

function cleanup
{
	rm -f $stream $stream.*
	destroy_dataset $source "-rR"
	destroy_dataset $valid "-rR"
	for mode in write-zero write-oversize write-off-compressed \
	    write-compressed-oversize spill-off spill-compressed-oversize; do
		destroy_dataset $POOL/payload-$mode "-rR"
	done
}

log_onexit cleanup

log_must zfs create -o xattr=sa -o dnodesize=legacy $source
log_must mkfile 1m /$source/file
for i in {1..20}; do
	log_must set_xattr testattr$i $attrvalue /$source/file
done

log_must zfs snapshot $source@snap
log_must eval "zfs send $source@snap >$stream"

# The ordinary sender leaves non-raw compression metadata zeroed, which means
# ZIO_COMPRESS_INHERIT.  This valid SPILL form must make a full round trip.
log_must eval "zfs receive $valid <$stream"
typeset expected=$(recursive_cksum /$source)
typeset actual=$(recursive_cksum /$valid)
[[ $expected == $actual ]] ||
	log_fail "valid spill receive differs from its source"

for mode in write-zero write-oversize write-off-compressed \
    write-compressed-oversize spill-off spill-compressed-oversize; do
	typeset bad=$stream.$mode
	log_must eval "stream_mangle $mode <$stream >$bad"
	log_must eval "zstream dump $bad >/dev/null"
	log_mustnot eval "zfs receive $POOL/payload-$mode <$bad"
	destroy_dataset $POOL/payload-$mode "-rR"
done

log_pass "receive validates WRITE/SPILL geometry and accepts normal spills"
