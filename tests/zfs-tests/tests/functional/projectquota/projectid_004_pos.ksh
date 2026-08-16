#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# Copyright (c) 2026 vpsAdminOS contributors. All rights reserved.
#
# A same-directory rename does not move an object into another project. Keep
# this regression independent of upstream's separate policy change to assign
# project IDs to newly created symlinks and other special files.

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

function cleanup
{
	log_must rm -rf $PRJDIR1 $PRJDIR2 $PRJDIR3
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

if ! renameat2 -C; then
	log_unsupported "renameat2 is not supported on this Linux kernel"
fi

log_onexit cleanup

log_assert "Legacy project-mismatched entries may be renamed in place"

log_must mkdir $PRJDIR1 $PRJDIR2 $PRJDIR3
log_must touch $PRJDIR1/old-file
log_must ln -s target $PRJDIR1/old-link
log_must mkfifo $PRJDIR1/old-fifo

# The existing entries retain their old project ID when the directory gains
# inheritance. Rejecting their rename inside the same directory is EXDEV on
# the pre-fix source. An mv(1) oracle would be invalid: it may copy on EXDEV.
log_must chattr +P -p $PRJID1 $PRJDIR1
log_must renameat2 $PRJDIR1/old-file $PRJDIR1/new-file
log_must renameat2 $PRJDIR1/old-link $PRJDIR1/new-link
log_must renameat2 $PRJDIR1/old-fifo $PRJDIR1/new-fifo
log_must eval '[[ $(readlink $PRJDIR1/new-link) == target ]]'
log_must test -p $PRJDIR1/new-fifo
log_must ln -sfn replacement $PRJDIR1/new-link
log_must eval '[[ $(readlink $PRJDIR1/new-link) == replacement ]]'

# Cross-project movement and hard-link creation must remain denied. A normal
# file created after the directory was tagged may still move between two
# directories with matching project IDs.
log_must chattr +P -p $PRJID2 $PRJDIR2
log_mustnot renameat2 $PRJDIR1/new-file $PRJDIR2/moved
log_mustnot ln $PRJDIR1/new-file $PRJDIR2/hardlink
log_must test -f $PRJDIR1/new-file
log_must chattr +P -p $PRJID1 $PRJDIR3
log_must touch $PRJDIR1/tagged-file
log_must renameat2 $PRJDIR1/tagged-file $PRJDIR3/tagged-file
log_must test -f $PRJDIR3/tagged-file

log_pass "Same-directory project inheritance rename contract preserved"
