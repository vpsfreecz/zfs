#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

if ! is_linux; then
	log_unsupported "UID/GID dataset maps are a Linux-only feature"
fi

typeset base="$TESTPOOL/ugidmap-rename"
typeset left="$base/left"
typeset right="$base/right"
typeset same_parent="$base/same-parent"
typeset head="$left/head"
typeset same="$left/same"
typeset override="$left/override"
typeset tree="$left/tree"
typeset tree_child="$tree/child"
typeset tree_snapshot="$tree_child@mounted"
typeset legacy="$left/legacy"
typeset none="$left/none"
typeset head_mnt="$TESTDIR/ugidmap-rename-head"
typeset same_mnt="$TESTDIR/ugidmap-rename-same"
typeset override_mnt="$TESTDIR/ugidmap-rename-override"
typeset child_mnt="$TESTDIR/ugidmap-rename-child"
typeset snapshot_mnt="$TESTDIR/ugidmap-rename-snapshot"
typeset legacy_mnt="$TESTDIR/ugidmap-rename-legacy"
typeset none_mnt="$TESTDIR/ugidmap-rename-none"
typeset map_a="0:100000:1000"
typeset map_b="0:200000:1000"
typeset map_c="0:300000:1000"

function cleanup
{
	if ismounted "$tree_snapshot"; then
		log_must umount "$snapshot_mnt"
	fi
	if ismounted "$legacy"; then
		log_must umount "$legacy_mnt"
	fi
	if ismounted "$none"; then
		log_must umount "$none_mnt"
	fi
	datasetexists "$base" && destroy_dataset "$base" -Rf

	for mnt in "$head_mnt" "$same_mnt" "$override_mnt" \
	    "$child_mnt" "$snapshot_mnt" "$legacy_mnt" "$none_mnt"; do
		[[ -d "$mnt" ]] && log_must rmdir "$mnt"
	done
}

function check_map
{
	typeset dataset="$1"
	typeset expected="$2"

	log_must test "$(get_prop uidmap "$dataset")" = "$expected"
	log_must test "$(get_prop gidmap "$dataset")" = "$expected"
}

log_assert "rename cannot stale mounted UID/GID map tables"
log_onexit cleanup

log_must zfs create -u "$base"
log_must zfs create -u -o uidmap="$map_a" -o gidmap="$map_a" "$left"
log_must zfs create -u -o uidmap="$map_b" -o gidmap="$map_b" "$right"
log_must zfs create -u -o uidmap="$map_a" -o gidmap="$map_a" \
	"$same_parent"

# The ordinary -u ioctl path must reject a live head whose inherited maps
# would change after reparenting.
log_must zfs create -u -o mountpoint="$head_mnt" "$head"
log_must zfs mount "$head"
log_mustnot zfs rename -u "$head" "$right/head"
log_must zfs list "$head"
log_must test "$(get_prop mounted "$head")" = "yes"
check_map "$head" "$map_a"
log_must zfs unmount "$head"

# Equal effective parent maps are a true no-op and must remain renameable
# without disturbing a live mount.
log_must zfs create -u -o mountpoint="$same_mnt" "$same"
log_must zfs mount "$same"
log_must zfs rename -u "$same" "$same_parent/same"
log_must test "$(get_prop mounted "$same_parent/same")" = "yes"
check_map "$same_parent/same" "$map_a"

# A local value inside the moved subtree shields it from its new parent.
log_must zfs create -u -o mountpoint="$override_mnt" \
	-o uidmap="$map_c" -o gidmap="$map_c" "$override"
log_must zfs mount "$override"
log_must zfs rename -u "$override" "$right/override"
log_must test "$(get_prop mounted "$right/override")" = "yes"
check_map "$right/override" "$map_c"

# The moved head can be unmounted while an affected descendant or directly
# mounted snapshot still owns cached tables.  Both cases must block rename.
log_must zfs create -u "$tree"
log_must zfs create -u -o mountpoint="$child_mnt" "$tree_child"
log_must zfs snapshot "$tree_snapshot"
log_must mkdir -p "$snapshot_mnt"
log_must mount -t zfs "$tree_snapshot" "$snapshot_mnt"
log_mustnot zfs rename -u "$tree" "$right/tree"
log_must umount "$snapshot_mnt"
log_must zfs mount "$tree_child"
log_mustnot zfs rename -u "$tree" "$right/tree"
log_must zfs unmount "$tree_child"

# libzfs automatically preserves manual legacy and none mounts even without
# -u.  The kernel transaction must therefore reject their reparenting too.
log_must zfs create -u -o mountpoint=legacy "$legacy"
log_must mkdir -p "$legacy_mnt"
log_must mount -t zfs "$legacy" "$legacy_mnt"
log_mustnot zfs rename "$legacy" "$right/legacy"
log_must umount "$legacy_mnt"

log_must zfs create -u -o mountpoint=none "$none"
log_must mkdir -p "$none_mnt"
log_must mount -t zfs -o zfsutil "$none" "$none_mnt"
log_mustnot zfs rename "$none" "$right/none"
log_must umount "$none_mnt"

log_pass "rename preserves the mounted UID/GID map lifetime invariant"
